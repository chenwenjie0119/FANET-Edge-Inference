// heartbeat: 基于 UDP 组播的最小节点发现/心跳示例。
// 每个节点加入同一组播组，周期性广播自己的 ID/Role，并维护存活成员表。
// 生产环境建议替换为 hashicorp/memberlist（见 docs/03-distributed-compute.md）。
package main

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"time"
)

const (
	groupAddr = "239.255.42.99:7946"
	interval  = 1 * time.Second
	timeout   = 5 * time.Second
)

// Announce 是节点广播的报文。
type Announce struct {
	ID   string `json:"id"`
	Role string `json:"role"`
	Seq  uint64 `json:"seq"`
	At   int64  `json:"at"`
}

func main() {
	id := os.Getenv("NODE_ID")
	if id == "" {
		id, _ = os.Hostname()
	}
	role := os.Getenv("NODE_ROLE")
	if role == "" {
		role = "uav"
	}

	addr, err := net.ResolveUDPAddr("udp4", groupAddr)
	if err != nil {
		fatal(err)
	}

	conn, err := net.ListenMulticastUDP("udp4", nil, addr)
	if err != nil {
		fatal(err)
	}
	defer conn.Close()

	// 周期广播心跳
	go func() {
		var seq uint64
		t := time.NewTicker(interval)
		defer t.Stop()
		for range t.C {
			seq++
			msg, _ := json.Marshal(Announce{ID: id, Role: role, Seq: seq, At: time.Now().UnixMilli()})
			if _, err := conn.WriteToUDP(msg, addr); err != nil {
				fmt.Fprintln(os.Stderr, "send:", err)
			}
		}
	}()

	// 接收并维护存活成员表
	peers := map[string]time.Time{}
	buf := make([]byte, 65507)
	for {
		_ = conn.SetReadDeadline(time.Now().Add(timeout))
		n, _, err := conn.ReadFromUDP(buf)
		if err != nil {
			continue
		}
		var a Announce
		if err := json.Unmarshal(buf[:n], &a); err != nil {
			continue
		}
		now := time.Now()
		peers[a.ID] = now

		var alive []string
		for pid, last := range peers {
			if now.Sub(last) <= timeout {
				alive = append(alive, pid)
			} else {
				delete(peers, pid)
			}
		}
		fmt.Printf("peer=%s role=%s seq=%d alive=%v\n", a.ID, a.Role, a.Seq, alive)
	}
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, "fatal:", err)
	os.Exit(1)
}

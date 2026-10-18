// master: Stage 0 地面站 Master。
//
// HTTP :8080
//
//	POST /api/v1/heartbeat  接收 Worker 心跳 JSON（worker.py）
//	GET  /api/v1/nodes      查询在线节点表
//
// 超过 3 秒未收到心跳 -> 标记 offline。
// 零第三方依赖，标准库 net/http 即可。
package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"
)

const (
	addr         = ":8080"
	offlineAfter = 3 * time.Second
	sweepEvery   = 500 * time.Millisecond
)

type Heartbeat struct {
	NodeID   string    `json:"node_id"`
	TS       float64   `json:"ts"`
	Attitude *Attitude `json:"attitude"`
	GPS      *GPS      `json:"gps"`
	Battery  *Battery  `json:"battery"`
}

type Attitude struct {
	Roll  float64 `json:"roll"`
	Pitch float64 `json:"pitch"`
	Yaw   float64 `json:"yaw"`
}

type GPS struct {
	Lat     float64 `json:"lat"`
	Lon     float64 `json:"lon"`
	Alt     float64 `json:"alt"`
	FixType int     `json:"fix_type"`
}

type Battery struct {
	Remaining int  `json:"remaining"`
	Low       bool `json:"low"`
}

type Node struct {
	ID       string     `json:"id"`
	Status   string     `json:"status"` // online / offline
	LastSeen time.Time  `json:"last_seen"`
	LastBeat *Heartbeat `json:"last_beat"`
}

type Registry struct {
	mu    sync.Mutex
	nodes map[string]*Node
}

func NewRegistry() *Registry {
	return &Registry{nodes: map[string]*Node{}}
}

// upsert 记录一次心跳，节点立即置为 online。
func (r *Registry) upsert(hb Heartbeat) {
	r.mu.Lock()
	defer r.mu.Unlock()
	n, ok := r.nodes[hb.NodeID]
	if !ok {
		n = &Node{ID: hb.NodeID}
		r.nodes[hb.NodeID] = n
	}
	n.Status = "online"
	n.LastSeen = time.Now()
	n.LastBeat = &hb
}

// sweep 扫描超时节点，仅 online -> offline 时打印。
func (r *Registry) sweep() {
	r.mu.Lock()
	defer r.mu.Unlock()
	now := time.Now()
	for id, n := range r.nodes {
		if n.Status == "online" && now.Sub(n.LastSeen) > offlineAfter {
			n.Status = "offline"
			fmt.Printf("[registry] node %s OFFLINE (no heartbeat > %s)\n", id, offlineAfter)
		}
	}
}

func (r *Registry) snapshot() []Node {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := make([]Node, 0, len(r.nodes))
	for _, n := range r.nodes {
		out = append(out, *n)
	}
	return out
}

func main() {
	reg := NewRegistry()

	http.HandleFunc("/api/v1/heartbeat", func(w http.ResponseWriter, req *http.Request) {
		if req.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var hb Heartbeat
		if err := json.NewDecoder(req.Body).Decode(&hb); err != nil {
			http.Error(w, "bad json: "+err.Error(), http.StatusBadRequest)
			return
		}
		if hb.NodeID == "" {
			http.Error(w, "node_id required", http.StatusBadRequest)
			return
		}
		reg.upsert(hb)
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]any{
			"status": "ok",
			"node":   hb.NodeID,
			"nodes":  len(reg.snapshot()),
		})
	})

	http.HandleFunc("/api/v1/nodes", func(w http.ResponseWriter, req *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(map[string]any{"nodes": reg.snapshot()})
	})

	go func() {
		t := time.NewTicker(sweepEvery)
		defer t.Stop()
		for range t.C {
			reg.sweep()
		}
	}()

	log.Println("master listening on", addr)
	log.Fatal(http.ListenAndServe(addr, nil))
}

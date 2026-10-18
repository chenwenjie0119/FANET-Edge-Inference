// master.go — Stage 1 真机适配版地面站 Master（Go，零第三方依赖）。
//
// HTTP 接口（与 Stage 0 完全兼容，worker.py 零改动）：
//   POST /api/v1/heartbeat   接收 Worker 心跳 JSON
//   GET  /api/v1/nodes       查询节点表（含 miss_count / backoff_secs）
//   GET  /metrics            Prometheus 文本格式指标（在线数/丢包计数等，Grafana 可直接抓）
//
// 真机适配（相对 Stage 0 的三个变化）：
//   1. 动态心跳超时：心跳周期 1s，连续丢满 maxMiss=3 次（第 4 秒）判离线，
//      4G/Wi-Fi 抖动不会把节点误判下线。
//   2. 指数退避：离线节点进入退避探测，1s -> 2s -> 4s -> ... -> 60s，
//      到达探测时刻才打印一条状态，避免每一秒疯狂刷屏。
//   3. ANSI 彩色终端日志：绿=上线，红=离线，黄=退避探测（--no-color 关闭）。
//
// 运行：go build -o master . && ./master
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"strings"
	"sync"
	"time"
)

const (
	addr           = ":8080"
	heartbeatEvery = time.Second        // worker 上报周期
	maxMiss        = 3                  // 连续丢满该次数即判离线
	backoffBase    = 1 * time.Second    // 指数退避初始值
	backoffMax     = 60 * time.Second   // 退避上限
)

/* ANSI 颜色（NO_COLOR 或 --no-color 时禁用） */
const (
	cReset  = "\033[0m"
	cGreen  = "\033[32m"
	cYellow = "\033[33m"
	cRed    = "\033[31m"
)

var noColor = flag.Bool("no-color", false, "disable ANSI colors")

func paint(c, s string) string {
	if *noColor {
		return s
	}
	return c + s + cReset
}

/* ---------- 数据模型 ---------- */

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
	ID          string     `json:"id"`
	Status      string     `json:"status"` // online / offline
	MissCount   int        `json:"miss_count"`
	BackoffSecs int        `json:"backoff_secs"`
	NextProbe   time.Time  `json:"next_probe"`
	LastSeen    time.Time  `json:"last_seen"`
	LastBeat    *Heartbeat `json:"last_beat,omitempty"`
}

type Registry struct {
	mu    sync.Mutex
	nodes map[string]*Node
}

func NewRegistry() *Registry { return &Registry{nodes: make(map[string]*Node)} }

/* 收到心跳：状态回 online，丢包计数清零，退避复位。 */
func (r *Registry) upsert(hb Heartbeat) {
	r.mu.Lock()
	defer r.mu.Unlock()

	n, ok := r.nodes[hb.NodeID]
	if !ok {
		n = &Node{ID: hb.NodeID}
		r.nodes[hb.NodeID] = n
	}
	wasOnline := n.Status == "online"

	n.Status = "online"
	n.MissCount = 0
	n.BackoffSecs = 0
	n.LastSeen = time.Now()
	n.LastBeat = &hb

	if !wasOnline {
		fmt.Println(paint(cGreen, fmt.Sprintf("[registry] node %s ONLINE", hb.NodeID)))
	}
}

/*
 * sweep 每秒执行一次：
 *   - online：超过一个心跳周期未更新 -> 记一次丢包；连续 maxMiss 次 -> 离线（红字）
 *   - offline：指数退避探测，到达 NextProbe 才打一条日志（黄字）
 */
func (r *Registry) sweep() {
	r.mu.Lock()
	defer r.mu.Unlock()

	now := time.Now()
	for id, n := range r.nodes {
		switch n.Status {
		case "online":
			if now.Sub(n.LastSeen) >= heartbeatEvery {
				n.MissCount++
				if n.MissCount >= maxMiss {
					n.Status = "offline"
					n.BackoffSecs = 1
					n.NextProbe = now.Add(backoffBase)
					fmt.Println(paint(cRed, fmt.Sprintf(
						"[registry] node %s OFFLINE (%d missed beats)", id, n.MissCount)))
				}
			} else {
				n.MissCount = 0 // 收到过心跳，丢包计数清零
			}

		case "offline":
			if now.Before(n.NextProbe) {
				continue // 退避窗口内不打扰
			}
			// 探测时刻到：翻倍退避并封顶，打印当前状态
			n.BackoffSecs *= 2
			if n.BackoffSecs > int(backoffMax.Seconds()) {
				n.BackoffSecs = int(backoffMax.Seconds())
			}
			n.NextProbe = now.Add(time.Duration(n.BackoffSecs) * time.Second)
			fmt.Println(paint(cYellow, fmt.Sprintf(
				"[probe] node %s still offline, next retry in %ds", id, n.BackoffSecs)))
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

/* ---------- HTTP ---------- */

func heartbeatHandler(r *Registry) http.HandlerFunc {
	return func(w http.ResponseWriter, req *http.Request) {
		if req.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var hb Heartbeat
		if err := json.NewDecoder(req.Body).Decode(&hb); err != nil {
			http.Error(w, "bad json", http.StatusBadRequest)
			return
		}
		r.upsert(hb)
		w.Header().Set("Content-Type", "application/json")
		fmt.Fprintf(w, `{"status":"ok"}`)
	}
}

func nodesHandler(r *Registry) http.HandlerFunc {
	return func(w http.ResponseWriter, req *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(r.snapshot())
	}
}

/* ---------- metrics ---------- */

/* 手写 Prometheus 文本格式（零第三方依赖，promhttp 需引入 client_golang）。
 * Grafana / Prometheus 按 http://<master>:8080/metrics 抓取即可。 */
func metricsHandler(r *Registry) http.HandlerFunc {
	return func(w http.ResponseWriter, req *http.Request) {
		r.mu.Lock()
		defer r.mu.Unlock()

		var online, offline int
		for _, n := range r.nodes {
			if n.Status == "online" {
				online++
			} else {
				offline++
			}
		}

		var sb strings.Builder
		fmt.Fprintf(&sb, "# HELP fanet_nodes_online 在线节点数\n")
		fmt.Fprintf(&sb, "# TYPE fanet_nodes_online gauge\n")
		fmt.Fprintf(&sb, "fanet_nodes_online %d\n", online)
		fmt.Fprintf(&sb, "# HELP fanet_nodes_offline 离线节点数\n")
		fmt.Fprintf(&sb, "# TYPE fanet_nodes_offline gauge\n")
		fmt.Fprintf(&sb, "fanet_nodes_offline %d\n", offline)
		fmt.Fprintf(&sb, "# HELP fanet_nodes_total 已注册节点总数\n")
		fmt.Fprintf(&sb, "# TYPE fanet_nodes_total gauge\n")
		fmt.Fprintf(&sb, "fanet_nodes_total %d\n", len(r.nodes))

		for id, n := range r.nodes {
			state := 0
			if n.Status == "online" {
				state = 1
			}
			fmt.Fprintf(&sb, "fanet_node_status{id=%q} %d\n", id, state)
			fmt.Fprintf(&sb, "fanet_node_miss_count{id=%q} %d\n", id, n.MissCount)
			fmt.Fprintf(&sb, "fanet_node_backoff_secs{id=%q} %d\n", id, n.BackoffSecs)
			/* 离线节点 last_seen 会随时间无限增大，无"延迟"语义，
			 * 跳过不输出，避免 Grafana 误判高延迟 */
			if n.Status == "online" {
				fmt.Fprintf(&sb, "fanet_node_last_seen_ago{id=%q} %.3f\n", id, time.Since(n.LastSeen).Seconds())
			}
		}

		w.Header().Set("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
		_, _ = w.Write([]byte(sb.String()))
	}
}

/* ---------- main ---------- */

func main() {
	flag.Parse()

	reg := NewRegistry()

	// 每秒扫描一次，驱动丢包计数 / 离线判定 / 退避探测
	go func() {
		t := time.NewTicker(time.Second)
		defer t.Stop()
		for range t.C {
			reg.sweep()
		}
	}()

	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/heartbeat", heartbeatHandler(reg))
	mux.HandleFunc("/api/v1/nodes", nodesHandler(reg))
	mux.HandleFunc("/metrics", metricsHandler(reg))

	fmt.Println(paint(cGreen, fmt.Sprintf("[master] listening on %s (maxMiss=%d)", addr, maxMiss)))
	log.Fatal(http.ListenAndServe(addr, mux))
}

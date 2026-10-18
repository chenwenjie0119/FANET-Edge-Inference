# 03 异构分布式计算与通信

## 节点发现与心跳（Go）

结论先行：生产级用 hashicorp/memberlist，最简原型用 UDP 组播（`examples/heartbeat`）。

| 库 | 定位 | 适合 | 不适合 |
|---|---|---|---|
| hashicorp/memberlist | SWIM gossip：成员管理 + 故障检测 | 高动态节点进出（无人机场景首选） | 大消息分发 |
| go-libp2p | 完整 P2P 栈：DHT/发现/NAT 穿透/多路复用 | 跨网络（4G/公网）P2P | 学习曲线陡、内存占用偏大 |
| NATS（nats.go） | 轻量消息总线，cluster + JetStream | 遥测订阅/指令下发 | 拓扑感知弱，需配服务发现 |
| ZeroMQ（pebbe/zmq4） | 消息队列：PUB/SUB、REQ/REP | 简单流式广播 | 节点发现要自己做 |
| gRPC | RPC + 流式 | 请求/响应、双向流 | 节点发现依赖 etcd/consul，移动场景不合适 |

高动态 FANET 的关键点：节点频繁加入/离开，中心化注册（etcd/consul）需要稳定固定节点，不合适。gossip 每个节点自治，天然适配。

memberlist 最小用法：

```go
import "github.com/hashicorp/memberlist"

list, _ := memberlist.Create(memberlist.DefaultLocalConfig())
list.Join([]string{"10.1.0.1:7946"})   // mesh 内任意已知节点
// list.Members() 实时成员列表，NodeMeta 可携带无人机 ID/角色
```

## 心跳与遥测管道建议

- 成员/心跳：memberlist（UDP 7946，gossip）
- 遥测分发：NATS 或 UDP multicast（mesh 内简单场景）
- 指令 RPC：gRPC 双向流（需要低延迟确认）
- 跨网段（4G 回传）：libp2p 或 WebRTC DataChannel

## 视频/张量共享

| 方案 | 适合 | 备注 |
|---|---|---|
| Pion WebRTC（Go） | 跨网段视频 + DataChannel 传张量 | 延迟 100~300ms，NAT 穿透内置 |
| GStreamer RTSP | 树莓派摄像头推流 | 最成熟，配合 gocv 处理 |
| UDP multicast | mesh 内多节点广播 | 最简单，Go 标准库直接支持，无拥塞控制 |
| libp2p pubsub | 张量/小消息广播 | 自带 Topic 语义 |

落地建议：mesh 内部视频先用 GStreamer RTSP（单播拉流）或 UDP multicast（组播）；要跨网段/公网再上 Pion WebRTC。张量数据（模型中间结果）用 DataChannel 或 UDP，不要用 HTTP/JSON 大包。

Pion WebRTC 关键依赖：github.com/pion/webrtc/v4、github.com/pion/rtp（解码端）。

## 分布式大模型边缘推理

三层可选：

1. 单节点推理：llama.cpp（GGUF 量化模型），每架飞机本地跑小模型，最稳
2. 异构集群推理：exo（把多台设备拼成一个推理集群，思路映射 Pocket-Cluster）；模型按层/按请求分发
3. 编排层：k3s（机载轻量 Kubernetes），管理容器化服务（遥测、推理、视频）

约束：FANET 链路带宽有限，模型张量并行的通信量很大。优先策略：模型量化到 Q4/Q8 + 单节点推理 + 请求级负载均衡；只有高速回传链路才考虑层间张量并行。

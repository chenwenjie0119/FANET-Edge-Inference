# 06 分阶段验证路线图

总原则：每一阶段都有可验证的验收标准，前序未通过不进下一阶段。

## 阶段 0：本地仿真（纯软件）

目标：跑通「虚拟无人机 → MAVLink → 分布式心跳/遥测」最小闭环。

工具/SDK：

- PX4 SITL + Gazebo（`make px4_sitl gazebo-classic` / `sitl_multiple_run.sh 2` 双机）
- mavlink-router（UDP 汇总）
- MAVSDK-Python 或 pymavlink
- examples/heartbeat（Go UDP 节点发现）
- tc/netem 模拟丢包延迟

验收：

- 双机 SITL 遥测同时进入一个 UDP 端点
- 心跳节点在丢包 10% 下 5 秒内感知成员变化
- 可选：GStreamer 推一路测试流验证 mesh 拓扑下视频通路

## 阶段 1：单台真机 + 外挂树莓派

目标：真机遥测稳定接入，数据进入分布式框架。

工具/SDK：

- PX4 / ArduPilot 开放飞控（Holybro Pixhawk 系列等）。大疆 Mini 5 Pro 飞控封闭，仅作编队视觉/载荷平台，不参与本项目机载链路
- 树莓派 4B/5：UART 接飞控 TELEM1（57600）
- mavlink-router + pymavlink / MAVSDK-Python
- 4G HAT（EC20/SIM7600）回传地面站

验收：

- 1 小时连续飞行遥测无断流（UART 层无丢帧）
- 姿态/GPS 进入 Go 服务并可被查询
- 4G 回传通路打通，地面站看到实时位置

## 阶段 2：地面双机 Mesh

目标：两节点自组网互通，视频/张量 P2P。

工具/SDK：

- batman-adv（或 802.11s）双 USB Wi-Fi 网卡
- memberlist 心跳 + 节点角色元数据
- GStreamer RTSP / Pion WebRTC 视频互推
- tc/netem 在 mesh 接口上压测

验收：

- 两节点 bat0 互通，Ping 稳定
- 断电一个节点，另一个 3 秒内感知
- 视频流跨 mesh 传输 1080p @ 20fps 无明显卡顿（或明确记录瓶颈参数）

## 阶段 3：空中双机/多机集群

目标：真实空中集群 + 边缘推理验证。

工具/SDK：

- PX4 / ArduPilot 开放飞控双机 + 树莓派节点
- batman-adv mesh + LoRa 带外控制（可选）
- k3s 机载编排，llama.cpp / exo 跑量化小模型
- 4G 回传地面站监控

验收：

- 双机空中编队遥测互见，mesh 链路持续 30 分钟以上
- 机载推理：单节点跑通 Q4 小模型；双节点 exo 集群完成请求分发
- 断链恢复（mesh 重建）不丢任务上下文

## 里程碑排期建议（单人业余时间）

| 阶段 | 建议周期 | 关键风险 |
|---|---|---|
| 0 | 2~3 周 | 仿真环境版本兼容（PX4 与 Gazebo 版本） |
| 1 | 3~4 周 | UART 接线/波特率、供电稳定性 |
| 2 | 3~4 周 | Wi-Fi 网卡驱动、mesh 配置 |
| 3 | 4~6 周 | 真机适航/场地、链路干扰 |

## 落地清单（买什么）

- 树莓派 4B/5 ×2（阶段 1 起）
- USB Wi-Fi 网卡（RTL8812AU / MT7612）×2
- LoRa 模块（E32-900T 或 433M 版本）×2（阶段 3 可选）
- 4G HAT（EC20/SIM7600）×2（阶段 1 起）
- 开放飞控板（PX4 兼容，如 Holybro Pixhawk 系列）

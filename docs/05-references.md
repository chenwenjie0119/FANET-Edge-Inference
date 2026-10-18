# 05 开源项目与参考书目

## GitHub 项目

| 项目 | 定位 | 关联点 |
|---|---|---|
| [PX4/PX4-Autopilot](https://github.com/PX4/PX4-Autopilot) | 开源飞控固件 | 飞行控制层基础，SITL 生态 |
| [ArduPilot/ardupilot](https://github.com/ArduPilot/ardupilot) | 开源飞控固件 | 备选固件，SITL 生态 |
| [mavlink-router/mavlink-router](https://github.com/mavlink-router/mavlink-router) | MAVLink 串口/UDP 路由 | 机载数据转发核心 |
| [Dronecode/MAVSDK](https://github.com/Dronecode/MAVSDK) | 官方跨语言 SDK | 姿态/任务高层 API |
| [mavlink/mavlink](https://github.com/mavlink/mavlink) | MAVLink 协议定义 | 消息/CRC 规范源头 |
| [k3s-io/k3s](https://github.com/k3s-io/k3s) | 轻量 Kubernetes | 机载容器编排 |
| [exo-explore/exo](https://github.com/exo-explore/exo) | 异构设备分布式 LLM 推理 | 集群推理，思路映射 Pocket-Cluster |
| [gazebosim/gz-sim](https://github.com/gazebosim/gz-sim) | 机器人仿真 | PX4 多机仿真后端 |

（以上均为活跃维护的官方/知名仓库，直接对应本框架各层。）

## 书籍

| 书 | 覆盖 | 备注 |
|---|---|---|
| Small Unmanned Aircraft: Theory and Practice（Beard & McLain） | 无人机动力学/控制 | 理解姿态与控制的前提 |
| Designing Data-Intensive Applications（Kleppmann） | 分布式系统 | 心跳/共识/复制通用知识 |
| Distributed Systems（van Steen & Tanenbaum） | 分布式系统基础 | 官网免费 PDF：distributed-systems.net |

## 协议与技术文档

- MAVLink 官方文档：mavlink.io（消息定义、CRC、序列化）
- PX4 用户指南：docs.px4.io（SITL 多机仿真教程）
- ArduPilot 开发文档：ardupilot.org/dev
- batman-adv 文档：batman-adv.readthedocs.io
- memberlist 文档：github.com/hashicorp/memberlist（README 即完整手册）
- Pion WebRTC 教程：github.com/pion/webrtc（examples 目录）

## 论文

- Sahingoz, "Flying Ad-Hoc Networks (FANETs): A Survey"（2013）——FANET 综述起点，几乎所有后续工作都引用它
- 按 "FANET routing protocol survey" 检索近五年综述，重点看基于 batman-adv / OLSR 的改进方案

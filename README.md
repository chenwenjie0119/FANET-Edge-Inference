# FANET-Edge-Inference

无人机空中自组网（FANET）与异构分布式大模型边缘推理实验框架。

把多台无人机 + 机载计算节点（树莓派 / 安卓盒子）组成本地自组网集群，在网内完成：遥测采集、视频回传、分布式推理。本文档给出具体的协议选型、SDK、工程实践与分阶段验证路线，全部基于可落地验证的开源组件。

> 注意：大疆 Mini 5 Pro 的飞控是封闭系统，不开放 MAVLink 串口/UDP 数据面。它只能作为编队视觉/载荷平台，不能参与本项目机载数据链路。机载链路请选用 PX4 / ArduPilot 开放飞控（Holybro Pixhawk 系列等）。详见 `docs/02-flight-control-sdk.md`。

## 目录结构

```
FANET-Edge-Inference/
├── README.md                     # 项目总览与快速开始
├── LICENSE                       # MIT
├── .gitignore
├── docs/
│   ├── 01-physical-comm.md       # 物理层与通信协议选型
│   ├── 02-flight-control-sdk.md  # 飞控交互与数据获取
│   ├── 03-distributed-compute.md # 异构分布式计算与通信
│   ├── 04-simulation.md          # 开发环境与本地仿真
│   ├── 05-references.md          # 开源项目与参考书目
│   └── 06-roadmap.md             # 分阶段验证路线图
├── examples/
│   ├── heartbeat/                # 最小节点发现/心跳（Go，零第三方依赖）
│   └── telemetry/                # MAVLink 遥测读取（pymavlink）
└── stage0/                       # 阶段0：本地最小闭环（三语言联调）
    ├── README.md                 # 环境准备 + 联调步骤
    ├── scripts/sitl-up.sh        # 拉起 SITL + mavlink-router
    ├── python/worker.py          # pymavlink 采集 -> JSON -> POST Master
    ├── go/                       # Go Master HTTP 服务（:8080，节点表 + 离线判定）
    └── c/udp_gateway.c           # C 原生 UDP 转发网关（可选，替代 mavlink-router）

stage1/                          # 阶段1：单台真机 + 外挂树莓派
    ├── README.md                 # 落地指南：串口准备 + 联调 + 弱网测试
    ├── scripts/setup-serial.sh   # 释放 /dev/ttyAMA0（禁蓝牙 + 关控制台）
    ├── scripts/serial-test.sh    # stty 57600 + 回环测试
    ├── c/mavlink_serial_gateway.c# 串口<->UDP 双向网关（termios + 断线重连 + 指数退避）
    ├── go/                       # 真机适配版 Master（动态超时 + 退避 + ANSI 彩色日志）
    └── netem/asymmetric-test.sh  # 上下行不对称弱网模拟（ifb + iptables）
```

## 技术栈总览

| 层 | 选型 |
|---|---|
| 机间短距链路 | Wi-Fi Mesh：batman-adv（规模化）/ 802.11s（原型） |
| 机间长距控制 | LoRa（SX1276/SX1262 模块，带外控制信道） |
| 回传链路 | 4G Cat4（Quectel EC20 / SIM7600，ModemManager 拨号） |
| 飞控数据 | MAVLink over UART（TELEM 口）→ mavlink-router → UDP |
| 飞控 SDK | MAVSDK-Python / pymavlink；ROS2 用 px4_ros_com |
| 节点发现/心跳 | Go：hashicorp/memberlist（生产）/ UDP 组播（最小示例） |
| 视频/张量传输 | Pion WebRTC（跨网段）/ GStreamer RTSP / mesh 内 UDP multicast |
| 边缘推理 | llama.cpp（单节点）+ exo（异构集群）/ k3s 编排 |
| 仿真 | PX4 SITL + Gazebo；网络用 tc/netem、NS-3 |

## 快速开始（纯本地仿真）

前置：Ubuntu 22.04/24.04 或 WSL2，装好 PX4 工具链。

```bash
# 1. PX4 SITL 多机仿真（2 机）
git clone --recursive https://github.com/PX4/PX4-Autopilot
cd PX4-Autopilot
make px4_sitl gazebo-classic        # 单机
bash Tools/sitl_multiple_run.sh 2   # 双机

# 2. mavlink-router 把多路 UDP 遥测汇总到 14550
mavlink-routerd -e 127.0.0.1:14550 127.0.0.1:14540 127.0.0.1:14541

# 3. 拉遥测（另开终端）
pip install pymavlink
python3 examples/telemetry/telemetry.py --endpoint udp:127.0.0.1:14550

# 4. 启动节点发现/心跳（双节点演示，另开两个终端）
cd examples/heartbeat
NODE_ID=uav-1 go run .
NODE_ID=uav-2 go run .
```

详见 `docs/04-simulation.md` 与 `docs/06-roadmap.md`。

## 文档导航

- 通信选型：`docs/01-physical-comm.md`
- 飞控 SDK：`docs/02-flight-control-sdk.md`
- 分布式计算：`docs/03-distributed-compute.md`
- 仿真环境：`docs/04-simulation.md`
- 参考资料：`docs/05-references.md`
- 路线图：`docs/06-roadmap.md`

## License

MIT

## 免责声明

本项目为个人学习与研究用途的开源实验项目，作者不对使用本项目产生的任何后果承担责任。无人机飞行需遵守当地法律法规，请勿在禁飞区域或未经许可的情况下进行飞行作业；项目中涉及的第三方开源组件与商标归其各自所有者所有。

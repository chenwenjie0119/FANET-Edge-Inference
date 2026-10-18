**中文版本：[README.zh-CN.md](README.zh-CN.md) | English**

# FANET-Edge-Inference

> Author: Chen Wenjie · GitHub: [chenwenjie0119](https://github.com/chenwenjie0119)

An experimental framework for unmanned aerial vehicle ad-hoc networks (FANET) and heterogeneous distributed edge inference for large models.

The goal is to build a local self-organizing cluster from multiple UAVs plus onboard compute nodes (Raspberry Pi / Android boxes), covering telemetry collection, video streaming, and distributed inference within the network. This document provides concrete protocol selection, SDKs, engineering practices, and a staged verification roadmap, all based on verifiable open-source components.

> Note: The DJI Mini 5 Pro flight controller is a closed system and does not expose the MAVLink serial/UDP data plane. It can only serve as a formation vision/payload platform and cannot join the onboard data link of this project. For the onboard link, use open flight controllers such as PX4 / ArduPilot (Holybro Pixhawk series, etc.). See `docs/02-flight-control-sdk.md`.

## Repository Structure

```
FANET-Edge-Inference/
├── README.md                     # Project overview and quick start
├── LICENSE                       # MIT
├── .gitignore
├── docs/
│   ├── 01-physical-comm.md       # Physical layer and communication protocol selection
│   ├── 02-flight-control-sdk.md  # Flight controller interaction and data acquisition
│   ├── 03-distributed-compute.md # Heterogeneous distributed computing and communication
│   ├── 04-simulation.md          # Development environment and local simulation
│   ├── 05-references.md          # Open-source projects and references
│   └── 06-roadmap.md             # Staged verification roadmap
├── examples/
│   ├── heartbeat/                # Minimal node discovery/heartbeat (Go, zero third-party deps)
│   └── telemetry/                # MAVLink telemetry reader (pymavlink)
└── stage0/                       # Stage 0: minimal local closed loop (three-language integration)
    ├── README.md                 # Environment setup + integration steps
    ├── scripts/sitl-up.sh        # Launch SITL + mavlink-router
    ├── python/worker.py          # pymavlink collection -> JSON -> POST Master
    ├── go/                       # Go Master HTTP service (:8080, node table + offline detection)
    └── c/udp_gateway.c           # Native C UDP forwarding gateway (optional, replaces mavlink-router)

stage1/                          # Stage 1: single real vehicle + external Raspberry Pi
    ├── README.md                 # Deployment guide: serial setup + integration + weak-network tests
    ├── scripts/setup-serial.sh   # Release /dev/ttyAMA0 (disable Bluetooth + console)
    ├── scripts/serial-test.sh    # stty 57600 + loopback test
    ├── c/mavlink_serial_gateway.c# Bidirectional serial<->UDP gateway (termios + reconnect + exponential backoff)
    ├── go/                       # Real-vehicle Master (dynamic timeout + backoff + ANSI colored logs)
    └── netem/asymmetric-test.sh  # Asymmetric weak-network simulation (ifb + iptables)
```

## Technology Stack

| Layer | Choice |
|---|---|
| Short-range inter-UAV link | Wi-Fi Mesh: batman-adv (scale) / 802.11s (prototype) |
| Long-range control | LoRa (SX1276/SX1262 modules, out-of-band control channel) |
| Backhaul link | 4G Cat4 (Quectel EC20 / SIM7600, ModemManager dial-up) |
| Flight controller data | MAVLink over UART (TELEM port) → mavlink-router → UDP |
| Flight controller SDK | MAVSDK-Python / pymavlink; px4_ros_com for ROS2 |
| Node discovery/heartbeat | Go: hashicorp/memberlist (production) / UDP multicast (minimal example) |
| Video/tensor transport | Pion WebRTC (cross-subnet) / GStreamer RTSP / in-mesh UDP multicast |
| Edge inference | llama.cpp (single node) + exo (heterogeneous cluster) / k3s orchestration |
| Simulation | PX4 SITL + Gazebo; tc/netem, NS-3 for networking |

## Quick Start (Local Simulation Only)

Prerequisites: Ubuntu 22.04/24.04 or WSL2 with the PX4 toolchain installed.

```bash
# 1. PX4 SITL multi-vehicle simulation (2 vehicles)
git clone --recursive https://github.com/PX4/PX4-Autopilot
cd PX4-Autopilot
make px4_sitl gazebo-classic        # single vehicle
bash Tools/sitl_multiple_run.sh 2   # two vehicles

# 2. mavlink-router aggregates multi-route UDP telemetry to 14550
mavlink-routerd -e 127.0.0.1:14550 127.0.0.1:14540 127.0.0.1:14541

# 3. Pull telemetry (another terminal)
pip install pymavlink
python3 examples/telemetry/telemetry.py --endpoint udp:127.0.0.1:14550

# 4. Start node discovery/heartbeat (two-node demo, two more terminals)
cd examples/heartbeat
NODE_ID=uav-1 go run .
NODE_ID=uav-2 go run .
```

See `docs/04-simulation.md` and `docs/06-roadmap.md`.

## Documentation Index

- Communication selection: `docs/01-physical-comm.md`
- Flight controller SDK: `docs/02-flight-control-sdk.md`
- Distributed computing: `docs/03-distributed-compute.md`
- Simulation: `docs/04-simulation.md`
- References: `docs/05-references.md`
- Roadmap: `docs/06-roadmap.md`

## License

MIT

## Disclaimer

This project is an open-source experiment for personal learning and research purposes. The author assumes no responsibility for any consequences arising from its use. UAV flight must comply with local laws and regulations; do not fly in no-fly zones or without proper authorization. Third-party open-source components and trademarks mentioned in this project belong to their respective owners.

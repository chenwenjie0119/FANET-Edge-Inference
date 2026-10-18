# Stage 0：本地最小闭环（三语言）

架构：

```
PX4 SITL (UDP 14540)
   │
   ▼
mavlink-router ──UDP 14550──▶ Python Worker ──HTTP POST /api/v1/heartbeat──▶ Go Master :8080
```

语言分工：

- Python：机载采集 Worker（pymavlink 读遥测，转 JSON，上报心跳，低电量告警 + 返航指令）
- Go：地面站 Master（HTTP 节点表，3 秒无心跳判离线）
- C：可选底层网关（原生 socket 转发 MAVLink 原始字节流，替代 mavlink-router）

## 0. WSL2 环境准备（最少命令）

```bash
# WSL2 内 Ubuntu 22.04/24.04
sudo apt update
sudo apt install -y git python3-pip cmake ninja-build

# PX4 SITL 工具链（官方一键脚本，跳过 NuttX 交叉编译）
git clone --recursive https://github.com/PX4/PX4-Autopilot.git
cd PX4-Autopilot
bash Tools/setup/ubuntu.sh --no-nuttx

# mavlink-router
cd ~
git clone https://github.com/mavlink-router/mavlink-router.git
cd mavlink-router
mkdir -p build && cd build
cmake .. && sudo ninja install
```

## 1. 拉起 SITL + 转发

方式 A：两个终端手动（推荐，便于观察日志）

```bash
# 终端 1：SITL（Win11 WSLg 可直接弹 Gazebo 窗口）
cd ~/PX4-Autopilot
make px4_sitl gazebo-classic
# SITL 默认 MAVLink：UDP 14540（机载） / UDP 14550（GCS）

# 终端 2：mavlink-router 把 14540 转发到 14550
mavlink-routerd -e 127.0.0.1:14550 127.0.0.1:14540
```

方式 B：一键脚本

```bash
bash stage0/scripts/sitl-up.sh
```

确认端口：`ss -ulpn | grep 145`

## 2. 编译启动 Go Master

```bash
cd stage0/go
go build -o master .
./master
# 日志: master listening on :8080
```

## 3. 启动 Python Worker

```bash
pip install pymavlink
cd stage0/python
python3 worker.py
# 可选: --node-id uav-sitl-0 --master http://127.0.0.1:8080
# 低电量阈值: BATTERY_WARN=20 python3 worker.py
```

## 4. 验证闭环

```bash
# 节点表（应看到 uav-sitl-0 online）
curl http://127.0.0.1:8080/api/v1/nodes

# 离线判定：Ctrl+C 杀掉 worker，3 秒内 master 日志打印
# [registry] node uav-sitl-0 OFFLINE (no heartbeat > 3s)

# 低电量告警：设低阈值演示
BATTERY_WARN=90 python3 worker.py   # 电量 <90% 触发红色告警 + 返航指令
```

## C 网关（可选进阶）

替代 mavlink-router：bind 14540 收 SITL 原始 MAVLink 字节流，原封转发 14550。

```bash
cd stage0/c
gcc -O2 -o udp_gateway udp_gateway.c
./udp_gateway
# 日志: udp gateway: 14540 -> 127.0.0.1:14550
```

停掉 mavlink-router 后 worker 仍连 14550，数据改由 C 网关转发。协议无状态，worker 无需改动。

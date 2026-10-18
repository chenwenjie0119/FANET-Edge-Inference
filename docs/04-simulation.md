# 04 开发环境与本地仿真

## 仿真框架选型

| 框架 | 多机支持 | MAVLink | 视觉/传感器 | 上手成本 | 推荐度 |
|---|---|---|---|---|---|
| PX4 SITL + Gazebo | 好（官方多机脚本） | 原生 UDP | 中等 | 低 | ★★★★★ |
| ArduPilot SITL | 好 | 原生 UDP | 中等 | 低 | ★★★★☆ |
| AirSim（UE5/Unity） | 一般 | 需桥接 | 强（真实感） | 高 | ★★★☆☆ |
| ROS 2 + Gazebo | 好 | 需 px4_ros_com | 中等 | 中 | ★★★★☆（走 ROS 生态时） |

推荐主线：PX4 SITL + Gazebo（Classic 或 Harmonic）。多机支持最省事，MAVLink 原生 UDP 输出，直接对接 `docs/02` 的链路。

## PX4 SITL 快速开始

```bash
git clone --recursive https://github.com/PX4/PX4-Autopilot
cd PX4-Autopilot
make px4_sitl gazebo-classic          # 单机
bash Tools/sitl_multiple_run.sh 2     # 双机
```

每架虚拟机的 MAVLink 默认端口：第一机 UDP 14540（机）/14550（GCS），第二机 14541/14551，依此类推。用 mavlink-router 汇总即可。

## ArduPilot SITL（备选）

```bash
git clone --recursive https://github.com/ArduPilot/ardupilot
cd ardupilot
./sim_vehicle.py -v ArduCopter -f gazebo-iris --map --console
```

## 网络仿真：丢包/延迟/抖动

### Linux tc + netem（最常用）

```bash
# 在网卡上加 50ms 延迟 + 10% 丢包 + 5ms 抖动
sudo tc qdisc add dev wlan0 root netem delay 50ms 5ms loss 10%

# 清除
sudo tc qdisc del dev wlan0 root
```

叠加在 mavlink-router 出口/UDP 端口上，即可测试心跳和视频在劣化链路下的表现。这是验证 memberlist 故障检测和重传逻辑的最快方式。

### NS-3（协议级）

NS-3 做全协议栈 FANET 场景（信道模型、路由协议对比），学习成本高。适合：对比 802.11s vs batman-adv 路由协议时的论文级验证。有社区现成 FANET 示例可改。

### mininet-wifi（折中）

基于 mininet 的 Wi-Fi 仿真，支持 mesh/ad-hoc 配置，比 NS-3 轻，适合验证路由逻辑和拓扑脚本。

## 仿真与真机的差异（提前注意）

1. SITL 时钟精确、链路理想，真机 Wi-Fi Mesh 会有射频干扰和拓扑抖动
2. GPS 仿真无多径，真机城市环境 eph 波动大
3. 仿真里 mavlink-router 不涉及串口竞态，真机 UART 需要先压测
4. 真机飞行时间有限，仿真要多跑参数扫描（丢包率、节点数）

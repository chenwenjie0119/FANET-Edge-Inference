# 02 飞控交互与数据获取

## SDK 对比

| SDK | 语言 | 维护状态 | 适用场景 |
|---|---|---|---|
| MAVSDK-Python | Python | 官方活跃 | 高层 API：姿态/任务/航点，快速原型首选 |
| DroneKit-Python | Python | 维护放缓 | 老牌 API，ArduPilot 生态成熟 |
| pymavlink | Python | 官方活跃 | 报文级底层，做网关/转发/协议解析 |
| px4_ros_com + px4_msgs | C++ / ROS2 | 官方活跃 | 走 ROS 2 生态时 |
| gopter/mavlink | Go | 社区维护 | Go 侧直接解析 MAVLink |
| MAVSDK（C++/Java/Swift） | 多语言 | 官方活跃 | 原生性能场景 |

推荐：机载节点主链路用 MAVSDK-Python（拿数据快、API 干净）；需要报文级控制/转发用 pymavlink；Go 服务要直接消费 MAVLink 用 gopter/mavlink。MAVLink 协议本身不复杂（定长/变长消息 + CRC），必要时可手写解析。

## 连接架构（关键）

真实机场景的标准做法：飞控 TELEM1 串口 → 树莓派 UART → mavlink-router 转发为 UDP → 上层应用消费。

```
[PX4/ArduPilot] --UART 57600/921600--> [树莓派 UART] --mavlink-router--> UDP 14550 --> [MAVSDK / pymavlink / Go]
```

mavlink-router（Intel 开源）负责串口和 UDP 之间的双向转发，比在应用里管串口可靠得多，也支持多路消费端。

树莓派 UART 配置（/boot/firmware/config.txt）：

```
enable_uart=1
dtoverlay=disable-bt    # 若用 GPIO 14/15 的 PL011，释放串口给飞控
```

接线：树莓派 TX（GPIO14）→ 飞控 RX，树莓派 RX（GPIO15）→ 飞控 TX，共地。波特率默认 57600（PX4 TELEM1）。

## UDP 直连（SITL / 地面测试）

SITL 默认把 MAVLink 发到 UDP 14540（机）与 14550（地面站）。地面消费：

```bash
mavlink-routerd -e 127.0.0.1:14550 127.0.0.1:14540
```

应用直接连 127.0.0.1:14550。

## 数据读取示例

见 `examples/telemetry/telemetry.py`（pymavlink，连 UDP 后阻塞读取姿态/GPS/电量）。

关键消息与字段：

| 消息 | 字段 | 说明 |
|---|---|---|
| ATTITUDE | roll / pitch / yaw | 姿态（弧度） |
| GLOBAL_POSITION_INT | lat / lon / alt | 经纬度（1e7 缩放）、海拔（mm） |
| GPS_RAW_INT | eph / epv / fix_type | GPS 精度与定位状态 |
| SYS_STATUS | battery_remaining | 剩余电量 |
| HEARTBEAT | autopilot / mode | 心跳与飞行模式 |

## 速率与带宽控制

PX4 参数（QGC 或 mavlink 指令）控制流率，避免链路被遥测灌满：

- PX4 的 MAV_* 参数按消息类型限速
- mavlink-router 对 UDP 端点做 rate limit

## 安全实践

- 地面节点对 HEARTBEAT 做 watchdog：连续 N 秒丢失触发降级/返航指令
- 不要在 Wi-Fi Mesh 链路上跑全量日志，只保留控制必需消息
- 真机首次联调前，用 SITL 相同的 UDP 端口跑通全链路
- 大疆 Mini 5 Pro 飞控封闭，不开放 MAVLink，只能做编队视觉/载荷平台；本项目机载链路必须用 PX4 / ArduPilot 开放飞控

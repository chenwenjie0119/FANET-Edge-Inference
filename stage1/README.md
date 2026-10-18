# Stage 1：单台真机 + 外挂树莓派

纯终端作业，无桌面环境。树莓派 Lite（Raspberry Pi OS Lite, Bookworm）。

```
[Pixhawk (PX4/ArduPilot)]
   │  UART 57600 8N1 (TELEM1 -> GPIO14/15)
   ▼
/dev/ttyAMA0  (树莓派，已释放蓝牙占用)
   │
   ▼
[mavlink_serial_gateway]  ←C 程序，双向桥
   │  sendto(127.0.0.1:14550)        ←遥测出口（worker 监听）
   │  bind(14540) ←recvfrom         ←指令入口（地面指令写回串口）
   ▼
[worker.py] --HTTP POST /api/v1/heartbeat--> [master.go :8080]
```

与 Stage 0 的衔接：worker.py 与 master.go 的 HTTP 协议完全复用。真机场景只替换数据来源（SITL → 串口网关），Worker 监听端口仍为 14550，零改动。

## 1. 树莓派串口释放（纯终端）

一键脚本（推荐）：

```bash
# 在树莓派上，把仓库拷过去后执行
cd FANET-Edge-Inference/stage1/scripts
sudo bash setup-serial.sh
sudo reboot
```

等价手工命令（了解每一步在干什么）：

```bash
# 1) 关闭串口控制台（login shell），启用硬件串口
sudo raspi-config nonint do_serial 1

# 2) 禁用蓝牙，释放 PL011 -> /dev/ttyAMA0（树莓派 4/5 默认蓝牙占用 PL011）
echo "dtoverlay=disable-bt" | sudo tee -a /boot/firmware/config.txt
echo "enable_uart=1" | sudo tee -a /boot/firmware/config.txt

# 3) 从 cmdline.txt 移除 console=serial，防止内核控制台抢占串口
sudo sed -i 's/ console=serial0,[0-9]*//; s/ console=ttyAMA0,[0-9]*//' /boot/cmdline.txt

# 4) 重启生效
sudo reboot
```

重启后确认：

```bash
ls -l /dev/ttyAMA0          # 存在
sudo systemctl is-active bluetooth   # inactive（已被禁用）
```

## 2. stty 配置与回环测试

回环测试前提：**拔掉飞控线**，用杜邦线把 GPIO14(TXD) 与 GPIO15(RXD) 短接。

```bash
cd stage1/scripts
bash serial-test.sh                 # 默认 /dev/ttyAMA0 @ 57600
# 输出 PASS: loopback OK 即通信链路正常
```

等价手工命令：

```bash
# 8N1 / 57600 / 非规范模式 / 无回显
stty -F /dev/ttyAMA0 57600 cs8 -cstopb -parenb -icanon -echo min 0 time 5

# 写一个标记，读回校验
echo "FANET-LOOPBACK-1" > /dev/ttyAMA0
timeout 2 head -c 64 /dev/ttyAMA0
```

接上飞控后的快速验证（不再短接）：应看到 MAVLink2 帧头 `0xFD`：

```bash
timeout 3 cat /dev/ttyAMA0 | xxd | head -5
# 期望: fd 00 00 00 ...（MAVLink2 起始字节）
```

## 3. C 网关（二合一，替代 mavlink-router）

```bash
cd stage1/c
gcc -O2 -o gateway gateway.c
# 串口需要权限：当前用户加入 dialout 组后重登，或直接 sudo
sudo ./gateway serial                # Stage 1 真机串口桥，默认 /dev/ttyAMA0
# 日志: [gw] serial /dev/ttyAMA0 open @57600
./gateway udp                        # Stage 0 仿真也可用: 14540 -> 14550 纯转发
```

`gateway.c` 是 `mavlink_serial_gateway.c` 与 `udp_gateway.c` 的二合一（子命令选择模式），
部署只编译一个文件。旧的两个程序仍保留可用；断线重连为非阻塞实现
（记录下次重连时间戳，期间不 sleep、UDP 指令不断流）。

端口约定：

- UDP **14550**：遥测出口（网关 sendto，worker 监听，与 Stage 0 一致）
- UDP **14540**：指令入口（网关 bind，地面指令 → 写回串口 → 飞控）

断线重连已内置：飞控断电/串口异常时程序不退出，指数退避 1s→2s→…→30s
循环重开，退避期间保持 UDP 指令可收（非阻塞）。

## 4. Go Master 真机适配版

```bash
cd stage1/go
go build -o master .
./master                       # 可选 --no-color
```

相对 Stage 0 的变化：

- 动态心跳超时：心跳周期 1s，**连续 3 次丢包**（第 4 秒）即判离线，弱网不误杀
- 指数退避：离线节点的探测/告警按 1s→2s→4s→…→60s 退避，不刷屏
- ANSI 彩色日志：绿=上线，红=离线，黄=退避探测
- `/metrics`：Prometheus 文本格式指标（零依赖手写），Grafana 直接抓

Prometheus 抓取配置（prometheus.yml）：

```yaml
scrape_configs:
  - job_name: fanet
    static_configs:
      - targets: ["<地面站IP>:8080"]
```

```bash
curl http://127.0.0.1:8080/metrics   # 手动验证: fanet_nodes_online / fanet_node_miss_count{id=...}
```

## 5. Worker 复用（零改动）

```bash
cd ../stage0/python
python3 worker.py              # 端点和 Master 地址都不变
```

## 6. 真实弱网测试（上下行不对称）

```bash
cd stage1/netem
bash asymmetric-test.sh start   # 上行: 仅 udp/14550 +200ms（其余流量直通）; 下行: 丢包 20%
bash asymmetric-test.sh stop    # 清理所有规则（iptables 按 comment 精确删除）
```

原理：tc netem 只能作用于 egress；egress 延迟用 prio + u32 filter 只命中 udp/14550，
不影响心跳 HTTP(8080) 和其它出站流量；ingress 需借 ifb 重定向后再施加 netem；
iptables 用 `statistic --mode random` 做精确到端口的丢包，规则带
`--comment fanet-netem`，stop 时先按完整规则删、再按 comment 兜底清残留。

## 7. 联调验证清单

```bash
# 1) 串口有 MAVLink 字节流（见第 2 节 xxd 验证）
# 2) 网关日志无报错，worker 每秒打印 JSON 且 master 返回 {"status":"ok"}
# 3) 节点状态
curl http://127.0.0.1:8080/api/v1/nodes
# 4) 断电测试：拔掉飞控供电，观察网关指数退避重连 + master 红字 OFFLINE
#    恢复供电后自动重新上线（绿字）
# 5) 弱网测试：asymmetric-test.sh start 后，worker 心跳偶发失败属正常，master 不应误判离线
```

## 8. 真机注意

- PX4 TELEM1 默认波特率 57600；ArduPilot 常见 57600/115200，按固件参数设
- 飞控 TX → 树莓派 RX(GPIO15)，飞控 RX → 树莓派 TX(GPIO14)，**必须共地**
- 树莓派 GPIO 是 3.3V TTL，接飞控 TELEM 口前确认电平兼容；长线用 5V 安全电平转换
- 上电顺序：先飞控后树莓派，避免串口电平竞争
- 机载 4G/Wi-Fi 回传时，master 跑在地面站机器，worker 上报地址改成地面站 IP

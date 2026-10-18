# 01 物理层与通信协议选型

## 链路矩阵（先看结论）

| 链路 | 带宽 | 时延 | 距离 | 用途 | 成本 |
|---|---|---|---|---|---|
| Wi-Fi Mesh（802.11s / batman-adv） | 20~100+ Mbps | 5~20 ms | 0.3~1 km（视天线） | 机间数据面：遥测/视频/张量 | 2×USB 网卡 |
| LoRa（SX1276/SX1262） | 0.3~50 kbps | 100~500 ms | 3~15 km | 带外控制信令、心跳、GPS 广播 | 模块 30~80 元 |
| 4G Cat4（EC20/SIM7600） | 10~75 Mbps（下行） | 30~100 ms | 全网覆盖 | 地面站回传、跨区管理 | HAT 150~300 元 |
| 5G（RM500Q 等） | 数百 Mbps | 10~30 ms | 全网覆盖 | 高带宽回传 | 贵，暂不推荐 |

## Wi-Fi Mesh：802.11s 还是 batman-adv

### 802.11s（Linux 内核原生）

mac80211 自带的 mesh point（MP）模式，路由由 HWMP（Hybrid Wireless Mesh Protocol）承担。优点：内核自带，无需额外模块，一条命令起 mesh；缺点：HWMP 基于 airtime 度量，拓扑剧烈变化时路由收敛偏慢。

```bash
# 每个节点执行
iw dev wlan0 interface add mesh0 type mp mesh_id fanet
ip link set mesh0 up
ip addr add 10.0.0.X/24 dev mesh0
```

### batman-adv

B.A.T.M.A.N. 数据面在内核、控制面在用户态的 Layer-2 路由，对移动拓扑更友好，是 FANET 文献中的主流方案。新版 B.A.T.M.A.N. V 用 ELP 估算链路吞吐量，动态环境表现更好。

```bash
sudo apt install batctl
sudo modprobe batman-adv

# 底层用 ad-hoc（IBSS）或 mesh point 都行，这里以 IBSS 为例
sudo iw dev wlan0 ibss join fanet 2412 MHz
sudo batctl if add wlan0
sudo ip link set bat0 up
sudo ip addr add 10.1.0.X/24 dev bat0
```

### 对比与结论

| 对比项 | 802.11s | batman-adv |
|---|---|---|
| 内核支持 | 原生 | 模块（多数发行版预编译） |
| 配置复杂度 | 低 | 低（batctl 一行） |
| 高动态拓扑 | HWMP 收敛慢 | B.A.T.M.A.N. V 更稳 |
| 规模化（>10 节点） | 一般 | 更好 |
| FANET 社区验证 | 有 | 论文验证更多 |

落地建议：原型阶段用 802.11s（少一个依赖），进入地面双机/空中集群阶段切换到 batman-adv。两张 USB Wi-Fi 网卡（RTL8812AU / MT7612 等支持 mesh/IBSS 的芯片）即可开始。

## LoRa：硬件与库

LoRa 只做带外控制信道。数据面（视频/模型数据）永远别指望 LoRa。

硬件：SX1276/SX1278 或 SX1262 模块，常见 EBYTE E32 系列、安信可 RA-02。树莓派走 UART（3.3V TTL）。频率注意中国法规：470-510 MHz（CN470）或 433 MHz ISM 免许可频段。

软件：

- RadioHead（C++，支持 RH_RF95）：树莓派/Arduino 通用，老牌稳定
- Python：pyserial 发 AT 指令或透传
- Go：go.bug.st/serial 发 AT 指令；模块工作在透传模式时直接用标准库读写串口，不需要专门库

EBYTE E32 系列的 AT 指令手册里有频率/功率/空中速率配置；透传模式下就是串口对串口，把 LoRa 当一根虚拟串口线用。

## 4G/5G：硬件与 AT

硬件：Quectel EC20/EC25、SIM7600 4G HAT。

关键工程实践：不要自己写 AT 指令拨号。用 ModemManager + NetworkManager 把 4G 变成标准网卡，上层走 IP 协议栈，Go 代码无感知。

```bash
sudo apt install modemmanager network-manager
mmcli -L                                   # 列出 modem
mmcli -m 0 --simple-connect="apn=cmnet"    # 中国移动默认 APN
```

AT 指令只用于信号查询和 SIM 管理：

```
AT+CSQ    # 信号强度
AT+CREG?  # 网络注册状态
```

Go 侧如需发 AT，用 go.bug.st/serial 打开 /dev/ttyUSB* 即可。5G 模块（RM500Q 等）成本高，本项目链路瓶颈不在下行带宽，4G Cat4 够用。

## 混合链路架构建议

控制面/数据面分离：

1. 控制面（低带宽、高可靠、长距）：LoRa 广播心跳/紧急指令/GPS
2. 数据面（高带宽、短距）：Wi-Fi Mesh 传遥测、视频、张量
3. 回传面（广域）：4G 传地面站、跨区调度、模型下发

好处：Wi-Fi Mesh 断链时 LoRa 仍能维持编队安全指令；4G 回传不依赖地面站位置。

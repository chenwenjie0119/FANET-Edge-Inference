#!/usr/bin/env bash
# asymmetric-test.sh
# 真实弱网环境模拟：上下行不对称（上行延迟 200ms，下行丢包 20%）。
#
# 原理：
#   tc netem 只能作用在 egress（本机发出）。下行（外部进入）流量需要先被
#   ifb（Intermediate Functional Block）重定向到虚拟设备，再施加 netem。
#   iptables 提供精确到端口的随机丢包（--dport 14550），作为 netem 的补充。
#
# 用法:
#   bash asymmetric-test.sh start [网卡名]   # 默认 eth0
#   bash asymmetric-test.sh stop  [网卡名]
set -euo pipefail

ACTION="${1:-start}"
IFACE="${2:-eth0}"

# 忽略失败执行：清理类动作（规则本就不存在时）不应中断脚本
quiet() { "$@" 2>/dev/null || true; }

# 依赖检查：tc/ip/iptables/modprobe 缺任意一个都无法完成配置
for cmd in sudo tc ip iptables modprobe grep; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $cmd" >&2
        exit 1
    fi
done

start_rules() {
    # 网卡不存在时 tc 的报错不够直观，这里先给出明确提示
    if ! ip link show "$IFACE" >/dev/null 2>&1; then
        echo "ERROR: network interface not found: $IFACE" >&2
        return 1
    fi

    echo "==> [UP]    $IFACE egress: +200ms delay on udp/14550 only"
    # 防重复添加：先清掉旧 root 与旧 ingress（重复执行 start 不会报错）
    quiet sudo tc qdisc del dev "$IFACE" root
    quiet sudo tc qdisc del dev "$IFACE" handle ffff: ingress
    # prio 默认流量全部进 1:2（直通无延迟），u32 只把 udp/14550 分流到 1:1 的 netem
    sudo tc qdisc add dev "$IFACE" root handle 1: prio bands 3 priomap 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2
    sudo tc qdisc add dev "$IFACE" parent 1:1 handle 10: netem delay 200ms
    sudo tc filter add dev "$IFACE" parent 1: protocol ip prio 1 u32 \
        match ip protocol 17 0xff \
        match udp dst port 14550 0xffff \
        flowid 1:1
    # 绝对兜底：未命中的 IP 包一律进 1:2 直通，防止被误加延迟
    sudo tc filter add dev "$IFACE" parent 1: protocol ip prio 2 u32 match u32 0 0 flowid 1:2

    echo "==> [DOWN]  redirect ingress -> ifb0"
    sudo modprobe ifb numifbs=1
    sudo ip link set ifb0 up
    sudo tc qdisc add dev "$IFACE" handle ffff: ingress
    # 把所有入口 IP 流量重定向到 ifb0
    sudo tc filter add dev "$IFACE" parent ffff: protocol ip u32 match u32 0 0 \
        action mirred egress redirect dev ifb0
    # 在 ifb0 上施加下行丢包 20%
    sudo tc qdisc add dev ifb0 root netem loss 20%

    echo "==> [ALT]   iptables: drop 20% of udp/14550 (下行精确丢包, 带 comment 便于清理)"
    sudo iptables -A INPUT -p udp --dport 14550 \
        -m statistic --mode random --probability 0.2 \
        -m comment --comment "fanet-netem" -j DROP

    echo "==> active rules:"
    sudo tc qdisc show dev "$IFACE"
    sudo tc qdisc show dev ifb0
    sudo iptables -L INPUT -n | grep 14550 || true
}

stop_rules() {
    echo "==> cleanup"
    # 先删子 qdisc 再删 root，顺序无关紧要（root 会级联清理）
    quiet sudo tc qdisc del dev "$IFACE" parent 1:1
    quiet sudo tc qdisc del dev "$IFACE" root
    quiet sudo tc qdisc del dev "$IFACE" handle ffff: ingress
    quiet sudo tc qdisc del dev ifb0 root
    quiet sudo ip link set ifb0 down
    # iptables：先按完整规则删，再按 comment 兜底清残留（防止重启后顺序漂移删错）
    quiet sudo iptables -D INPUT -p udp --dport 14550 \
        -m statistic --mode random --probability 0.2 \
        -m comment --comment "fanet-netem" -j DROP
    quiet sudo iptables -D INPUT -m comment --comment "fanet-netem" -j DROP
    echo "OK: 已清理"
}

case "$ACTION" in
    start) start_rules ;;
    stop)  stop_rules ;;
    *)
        echo "用法: $0 {start|stop} [网卡名]" >&2
        exit 1
        ;;
esac

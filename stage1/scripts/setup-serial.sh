#!/usr/bin/env bash
# setup-serial.sh
# 树莓派 Lite（无桌面）串口释放脚本：禁用蓝牙、关闭串口控制台，释放 /dev/ttyAMA0 给飞控。
#
# 用法: sudo bash setup-serial.sh    （执行后需重启）
set -euo pipefail

# 依赖检查：缺命令时提前报错，而不是在 sed/tee 处报 command not found
for cmd in sudo raspi-config sed grep tee; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $cmd" >&2
        exit 1
    fi
done

# 兼容不同系统的 /boot 挂载点（Bookworm 起为 /boot/firmware）
BOOT_DIR="/boot"
if [ -f /boot/firmware/config.txt ]; then
    BOOT_DIR="/boot/firmware"
fi
CONFIG="$BOOT_DIR/config.txt"
CMDLINE="$BOOT_DIR/cmdline.txt"

# 两个文件是后续 sed 的输入，缺失时直接失败并给出明确提示
for f in "$CONFIG" "$CMDLINE"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: file not found: $f" >&2
        exit 1
    fi
done

echo "==> [1/4] disable serial console login, enable serial hardware"
# raspi-config nonint do_serial 1 = 硬件串口启用 + 关闭串口控制台（login shell）
sudo raspi-config nonint do_serial 1

echo "==> [2/4] disable bluetooth to free PL011 -> /dev/ttyAMA0"
# 树莓派 4/5 默认蓝牙占用 PL011；disable-bt 后 GPIO14/15 映射到 /dev/ttyAMA0
sudo sed -i '/^dtoverlay=disable-bt/d;/^dtoverlay=miniuart-bt/d' "$CONFIG"
echo "dtoverlay=disable-bt" | sudo tee -a "$CONFIG" > /dev/null

# 确保 enable_uart=1（已存在则覆盖）
if sudo grep -q '^enable_uart' "$CONFIG"; then
    sudo sed -i 's/^enable_uart=.*/enable_uart=1/' "$CONFIG"
else
    echo "enable_uart=1" | sudo tee -a "$CONFIG" > /dev/null
fi

echo "==> [3/4] remove console=serial from cmdline (避免内核控制台抢占串口)"
sudo sed -i 's/ console=serial0,[0-9]*//; s/ console=ttyAMA0,[0-9]*//' "$CMDLINE"

echo "==> [4/4] done"
echo "请重启: sudo reboot"
echo "重启后确认: ls -l /dev/ttyAMA0 且 sudo systemctl is-active bluetooth 输出 inactive"

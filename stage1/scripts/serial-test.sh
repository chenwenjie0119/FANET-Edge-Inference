#!/usr/bin/env bash
# serial-test.sh
# 串口波特率配置 + 回环测试。
# 前提：拔掉飞控线，用杜邦线短接 GPIO14(TXD) 与 GPIO15(RXD)。
#
# 用法: bash serial-test.sh [/dev/ttyAMA0] [57600]
set -euo pipefail

DEV="${1:-/dev/ttyAMA0}"
BAUD="${2:-57600}"

echo "==> configure $DEV @ ${BAUD} 8N1 (raw, no echo)"
stty -F "$DEV" "${BAUD}" cs8 -cstopb -parenb -icanon -echo -echoe -echok min 0 time 5

marker="FANET-LOOPBACK-$$"
echo "$marker" > "$DEV"

recv="$(timeout 2 head -c 64 "$DEV" || true)"
if echo "$recv" | grep -q "$marker"; then
    echo "PASS: loopback OK ($DEV @ $BAUD)"
else
    echo "FAIL: no echo on $DEV"
    echo "检查: 杜邦线是否短接 GPIO14<->GPIO15，或 stty 是否报错"
    exit 1
fi

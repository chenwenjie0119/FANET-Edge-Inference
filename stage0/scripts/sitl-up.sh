#!/usr/bin/env bash
# 拉起 PX4 SITL（gazebo-classic）+ mavlink-router（14540 -> 14550）。
# 在 WSL2 内执行。Ctrl+C 退出并清理子进程。
set -e

PX4_DIR="${PX4_DIR:-$HOME/PX4-Autopilot}"

echo "==> starting PX4 SITL (gazebo-classic) from $PX4_DIR"
cd "$PX4_DIR"
make px4_sitl gazebo-classic &
SITL_PID=$!

echo "==> waiting for SITL UDP 14540 ..."
PORT_READY=0
for i in $(seq 1 30); do
    if ss -uln 2>/dev/null | grep -q ':14540 '; then
        echo "SITL up after ${i}s"
        PORT_READY=1
        break
    fi
    sleep 1
done

if [ "$PORT_READY" -ne 1 ]; then
    echo "ERROR: SITL did not bind 14540 within 30s" >&2
    kill "$SITL_PID" 2>/dev/null || true
    exit 1
fi

echo "==> starting mavlink-router (14540 -> 14550)"
mavlink-routerd -e 127.0.0.1:14550 127.0.0.1:14540 &
ROUTER_PID=$!

sleep 1
if ! ss -uln 2>/dev/null | grep -q ':14550 '; then
    echo "WARN: mavlink-router may not have bound 14550 yet" >&2
fi

cleanup() {
    echo "==> shutting down"
    kill "$ROUTER_PID" "$SITL_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

wait

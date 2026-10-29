#!/usr/bin/env bash
# 拉起 PX4 SITL（gazebo-classic）+ mavlink-router（14540 -> 14550）。
# 在 WSL2 内执行。Ctrl+C 退出并清理子进程。
set -euo pipefail

PX4_DIR="${PX4_DIR:-$HOME/PX4-Autopilot}"
SITL_PID=""
ROUTER_PID=""

# 子进程统一在此回收：正常退出、Ctrl+C、前置检查失败都会走到这里
cleanup() {
    if [ -z "$SITL_PID" ] && [ -z "$ROUTER_PID" ]; then
        return 0
    fi
    echo "==> shutting down"
    [ -n "$ROUTER_PID" ] && kill "$ROUTER_PID" 2>/dev/null || true
    [ -n "$SITL_PID" ] && kill "$SITL_PID" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT

# 依赖检查：缺工具时立即报错，避免后台命令静默失败后白等 30s
for cmd in make ss mavlink-routerd; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: required command not found: $cmd" >&2
        exit 1
    fi
done

if [ ! -d "$PX4_DIR" ]; then
    echo "ERROR: PX4 directory not found: $PX4_DIR" >&2
    exit 1
fi

echo "==> starting PX4 SITL (gazebo-classic) from $PX4_DIR"
cd "$PX4_DIR"
make px4_sitl gazebo-classic &
SITL_PID=$!

echo "==> waiting for SITL UDP 14540 ..."
PORT_READY=0
for ((i = 1; i <= 30; i++)); do
    if ss -uln 2>/dev/null | grep -q ':14540 '; then
        echo "SITL up after ${i}s"
        PORT_READY=1
        break
    fi
    sleep 1
done

if [ "$PORT_READY" -ne 1 ]; then
    echo "ERROR: SITL did not bind 14540 within 30s" >&2
    exit 1
fi

echo "==> starting mavlink-router (14540 -> 14550)"
mavlink-routerd -e 127.0.0.1:14550 127.0.0.1:14540 &
ROUTER_PID=$!

sleep 1
if ! ss -uln 2>/dev/null | grep -q ':14550 '; then
    echo "WARN: mavlink-router may not have bound 14550 yet" >&2
fi

wait

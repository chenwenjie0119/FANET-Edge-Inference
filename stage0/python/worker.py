#!/usr/bin/env python3
"""Stage 0 机载采集 Worker（Python）。

监听 UDP MAVLink（默认 14550），实时提取姿态/GPS/电量，转 JSON，
每 1 秒 POST 到 Go Master 的 /api/v1/heartbeat。
电量低于阈值时打印红色告警，并向飞控发送返航指令（MAV_CMD_NAV_RETURN_TO_LAUNCH）。

依赖: pip install pymavlink   （HTTP 用标准库 urllib，零额外依赖）
用法: python3 worker.py [--endpoint udp:127.0.0.1:14550] [--master http://127.0.0.1:8080] [--node-id uav-sitl-0]
"""
import argparse
import json
import os
import time
import urllib.request

from pymavlink import mavutil

RED = "\033[91m"
RESET = "\033[0m"
BATTERY_WARN = int(os.environ.get("BATTERY_WARN", "20"))


def log_red(msg: str) -> None:
    print(f"{RED}[LOW BATTERY] {msg}{RESET}", flush=True)


def build_payload(node_id: str, att, pos, sys) -> dict:
    return {
        "node_id": node_id,
        "ts": time.time(),
        "attitude": {
            "roll": att.roll,
            "pitch": att.pitch,
            "yaw": att.yaw,
        } if att else None,
        "gps": {
            "lat": pos.lat / 1e7,
            "lon": pos.lon / 1e7,
            "alt": pos.alt / 1e3,
            "fix_type": pos.fix_type,
        } if pos else None,
        "battery": {
            "remaining": sys.battery_remaining if sys else -1,
            "low": sys is not None and 0 <= sys.battery_remaining < BATTERY_WARN,
        },
    }


def post_heartbeat(master: str, payload: dict) -> str:
    req = urllib.request.Request(
        master + "/api/v1/heartbeat",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=3) as resp:
        return resp.read().decode("utf-8")


def request_rtl(conn, rtl_sent: int) -> int:
    """向飞控发送返航指令，每轮只发一次；断连异常不崩溃，下轮重试。"""
    if rtl_sent > 0:
        return rtl_sent
    try:
        conn.mav.command_long_send(
            conn.target_system, conn.target_component,
            mavutil.mavlink.MAV_CMD_NAV_RETURN_TO_LAUNCH,
            0, 0, 0, 0, 0, 0, 0, 0,
        )
    except Exception as e:  # noqa: BLE001 - 飞控断连不崩溃，保持采集循环
        print(f"rtl command failed: {e}", flush=True)
        return rtl_sent
    log_red("return-to-launch command sent to flight controller")
    return 1


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default="udp:127.0.0.1:14550")
    parser.add_argument("--master", default="http://127.0.0.1:8080")
    parser.add_argument("--node-id", default=os.environ.get("NODE_ID", "uav-sitl-0"))
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()

    conn = mavutil.mavlink_connection(args.endpoint)
    conn.wait_heartbeat()
    print(
        f"connected to flight controller: sys={conn.target_system} comp={conn.target_component}",
        flush=True,
    )

    att = pos = sys = None
    rtl_sent = 0
    prev_low = False
    last = 0.0

    while True:
        msg = conn.recv_match(
            type=["ATTITUDE", "GLOBAL_POSITION_INT", "SYS_STATUS"], blocking=True
        )
        if msg is None:
            continue
        t = msg.get_type()
        if t == "ATTITUDE":
            att = msg
        elif t == "GLOBAL_POSITION_INT":
            pos = msg
        elif t == "SYS_STATUS":
            sys = msg

        if time.time() - last < args.interval:
            continue
        last = time.time()

        payload = build_payload(args.node_id, att, pos, sys)
        print(json.dumps(payload, ensure_ascii=False), flush=True)

        low = payload["battery"]["low"]
        if low:
            rtl_sent = request_rtl(conn, rtl_sent)
        elif prev_low:
            # 边沿触发：电量从低恢复正常时复位，允许下次低电量再次触发返航
            rtl_sent = 0
            print("battery recovered, RTL guard reset", flush=True)
        prev_low = low

        try:
            print("master:", post_heartbeat(args.master, payload), flush=True)
        except Exception as e:  # noqa: BLE001 - 心跳失败不打断采集
            print("heartbeat failed:", e, flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nWorker stopped.", flush=True)

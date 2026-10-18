#!/usr/bin/env python3
"""读取 PX4 SITL / 真机 UDP MAVLink 遥测（姿态、GPS、电量）。

用法:
    pip install pymavlink
    python3 telemetry.py --endpoint udp:127.0.0.1:14550
"""
import argparse

from pymavlink import mavutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default="udp:127.0.0.1:14550",
                        help="MAVLink 端点，如 udp:127.0.0.1:14550 或 /dev/ttyS0")
    args = parser.parse_args()

    conn = mavutil.mavlink_connection(args.endpoint)
    conn.wait_heartbeat()
    print(f"connected: sys={conn.target_system} comp={conn.target_component}")

    while True:
        msg = conn.recv_match(type=["ATTITUDE", "GLOBAL_POSITION_INT", "SYS_STATUS"], blocking=True)
        if msg is None:
            continue
        t = msg.get_type()
        if t == "ATTITUDE":
            print(f"attitude roll={msg.roll:.3f} pitch={msg.pitch:.3f} yaw={msg.yaw:.3f}")
        elif t == "GLOBAL_POSITION_INT":
            print(f"pos lat={msg.lat / 1e7:.7f} lon={msg.lon / 1e7:.7f} alt={msg.alt / 1e3:.1f}m")
        elif t == "SYS_STATUS":
            print(f"battery {msg.battery_remaining}%")


if __name__ == "__main__":
    main()

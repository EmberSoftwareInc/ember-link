#!/usr/bin/env python3
"""Configure a developer/factory device over USB without printing its token."""
import argparse
import getpass
import json
import re
import time

import serial


def request(port, message):
    port.write((json.dumps(message, separators=(",", ":")) + "\n").encode())
    port.flush()
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        line = port.readline()
        if not line:
            continue
        body = json.loads(line)
        if body.get("id") != message["id"]:
            continue
        if not body.get("ok"):
            code = body.get("error", {}).get("code", "unknown_error")
            raise RuntimeError(f"Dongle rejected command: {str(code)[:80]}")
        return body
    raise TimeoutError("No response; close other serial clients and retry")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--api-base-url", required=True)
    parser.add_argument("--download-host", required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--enable", action="store_true")
    args = parser.parse_args()
    token = getpass.getpass("Unique device token (64 lowercase hex characters): ")
    if not re.fullmatch(r"[0-9a-f]{64}", token):
        raise SystemExit("Invalid token format")
    with serial.Serial(args.port, 115200, timeout=1, write_timeout=5) as port:
        port.dtr = True
        time.sleep(0.15)
        port.reset_input_buffer()
        request(port, {"id": 1, "cmd": "cloud_configure", "apiBaseUrl": args.api_base_url,
                       "downloadHost": args.download_host, "deviceId": args.device_id, "token": token})
        token = None
        if args.enable:
            request(port, {"id": 2, "cmd": "cloud_enable", "enabled": True})
        result = request(port, {"id": 3, "cmd": "cloud_status"})
        cloud = result.get("cloud", {})
        print("Cloud configured; enabled:", cloud.get("enabled", "busy; retry cloud_status"))
        print("State:", cloud.get("state", "busy"))


if __name__ == "__main__":
    main()

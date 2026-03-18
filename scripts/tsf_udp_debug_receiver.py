"""
UDP receiver for TSF DebugSink multicast logs.

Default group/port matches debugSink.hpp:
- group: 239.255.42.99
- port : 9876

Usage:
  python scripts/tsf_udp_receiver.py
  python scripts/tsf_udp_receiver.py --port 9999
  python scripts/tsf_udp_receiver.py --group 239.255.42.99 --interface 127.0.0.1

You can run multiple instances at the same time; each instance will receive the same logs.
"""

from __future__ import annotations

import argparse
import socket
import struct
from datetime import datetime


COLORS: dict[str, str] = {
    "IME": "\033[96m",
    "KEY": "\033[92m",
    "COMMIT": "\033[93m",
    "CANCEL": "\033[91m",
}
RESET = "\033[0m"


def parse_line(line: str) -> tuple[str, str]:
    tag = "UNK"
    text = line
    if line.startswith("["):
        end = line.find("]")
        if end != -1:
            tag = line[1:end]
            text = line[end + 1 :].lstrip()
    return tag, text


def main() -> int:
    parser = argparse.ArgumentParser(description="Receive TSF UDP multicast debug logs")
    parser.add_argument("--group", default="239.255.42.99", help="Multicast group address")
    parser.add_argument("--port", type=int, default=9876, help="UDP port")
    parser.add_argument(
        "--interface",
        default="127.0.0.1",
        help="Local interface for multicast membership (default: 127.0.0.1)",
    )
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # Some platforms expose SO_REUSEPORT; ignore if unsupported.
    if hasattr(socket, "SO_REUSEPORT"):
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        except OSError:
            pass

    sock.bind(("", args.port))

    mreq = struct.pack("4s4s", socket.inet_aton(args.group), socket.inet_aton(args.interface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    sock.settimeout(1.0)

    print(f"[TSF-UDP] listening on group {args.group}:{args.port} via {args.interface}")
    print("-" * 60)

    try:
        while True:
            try:
                data, src = sock.recvfrom(65535)
            except socket.timeout:
                continue

            message = data.decode("utf-8", errors="replace").strip()
            if not message:
                continue

            # Support one or many lines in a single datagram.
            for raw_line in message.splitlines():
                line = raw_line.strip()
                if not line:
                    continue

                tag, text = parse_line(line)
                color = COLORS.get(tag, "\033[97m")
                ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                print(f"{ts}  {color}[{tag:6s}]{RESET}  {text}  ({src[0]}:{src[1]})")
    except KeyboardInterrupt:
        print("\n[TSF-UDP] stopped")
    finally:
        try:
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_DROP_MEMBERSHIP, mreq)
        except OSError:
            pass
        sock.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

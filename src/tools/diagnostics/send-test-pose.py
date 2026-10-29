"""Send synthetic orientation to the optional UDP input, without third-party modules.

This desktop diagnostic sender does not read phone sensors.
"""
import argparse
import math
import socket
import struct
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--host", default="127.0.0.1")
parser.add_argument("--port", type=int, default=4242)
parser.add_argument("--token", required=True)
parser.add_argument("--seconds", type=float, default=10)
args = parser.parse_args()
if len(args.token) != 32 or any(c not in "0123456789abcdefABCDEF" for c in args.token):
    parser.error("token must contain 32 ASCII hex digits")
if not 1 <= args.port <= 65535 or not math.isfinite(args.seconds) or not 0 < args.seconds <= 7200:
    parser.error("invalid port or duration (0 < seconds <= 7200)")
start = time.monotonic()
sequence = 0
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
    while (elapsed := time.monotonic() - start) < args.seconds:
        yaw = math.sin(elapsed) * math.pi / 6
        data = struct.pack("<4sI32sQ4f", b"SLVR", 1, args.token.encode("ascii"), sequence,
                           math.cos(yaw / 2), 0, math.sin(yaw / 2), 0)
        connection.sendto(data, (args.host, args.port))
        sequence += 1
        time.sleep(1 / 90)

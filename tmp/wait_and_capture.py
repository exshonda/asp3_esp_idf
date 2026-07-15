#!/usr/bin/env python3
"""Wait for a by-id serial path to re-appear (post-reset re-enum), then
passively capture for N seconds without touching DTR/RTS.

Usage: wait_and_capture.py <port> <out_log> [seconds] [wait_timeout]
"""
import sys
import time
import os
import serial

port = sys.argv[1]
outfile = sys.argv[2]
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0
wait_timeout = float(sys.argv[4]) if len(sys.argv) > 4 else 10.0

t0 = time.time()
while not os.path.exists(port):
    if time.time() - t0 > wait_timeout:
        print(f"ERROR: {port} did not reappear within {wait_timeout}s")
        sys.exit(1)
    time.sleep(0.2)

# extra settle time after node appears
time.sleep(0.5)

ser = serial.Serial(port, 115200, timeout=0.2)
data = bytearray()
t0 = time.time()
while time.time() - t0 < secs:
    chunk = ser.read(4096)
    if chunk:
        data += chunk
ser.close()

with open(outfile, "wb") as f:
    f.write(data)

print(f"captured {len(data)} bytes")

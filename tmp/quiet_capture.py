#!/usr/bin/env python3
"""Passive console capture: opens the port (which itself typically triggers a
DUT reset on ESP32 USB-Serial/JTAG consoles) and reads for N seconds without
touching DTR/RTS afterward.

Usage: quiet_capture.py <port> <out_log> [seconds]
"""
import sys
import time
import serial

port = sys.argv[1]
outfile = sys.argv[2]
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 12.0

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

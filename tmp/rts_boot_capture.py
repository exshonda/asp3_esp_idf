#!/usr/bin/env python3
"""RTS-pulse reset + console capture for ESP32-C6 USB-JTAG (board C).

Usage: rts_boot_capture.py <port> <out_log> [seconds]
"""
import sys
import time
import serial

port = sys.argv[1]
outfile = sys.argv[2]
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 12.0

ser = serial.Serial(port, 115200, timeout=0.2)
# Classic hard reset via RTS (as esptool does for this board: RTS->EN).
ser.dtr = False
ser.rts = True
time.sleep(0.1)
ser.rts = False
time.sleep(0.1)
ser.rts = True

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

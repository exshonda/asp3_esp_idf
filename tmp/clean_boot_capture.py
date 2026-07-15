#!/usr/bin/env python3
"""Single clean hard-reset (RTS pulse matching esptool HardReset(uses_usb=True))
performed on an already-open serial fd, then passive capture for N seconds.
Avoids the close/reopen race that misses the USB re-enum window.

Usage: clean_boot_capture.py <port> <out_log> [seconds]
"""
import sys
import time
import serial

port = sys.argv[1]
outfile = sys.argv[2]
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 15.0

ser = serial.Serial(port, 115200, timeout=0.2)

# HardReset(uses_usb=True) sequence: RTS=True (EN low) 0.2s, RTS=False (EN high) 0.2s
ser.dtr = False
ser.rts = True
time.sleep(0.2)
ser.rts = False
time.sleep(0.2)

data = bytearray()
t0 = time.time()
while time.time() - t0 < secs:
    try:
        chunk = ser.read(4096)
    except Exception:
        chunk = b""
    if chunk:
        data += chunk
ser.close()

with open(outfile, "wb") as f:
    f.write(data)

print(f"captured {len(data)} bytes")

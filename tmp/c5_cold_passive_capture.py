#!/usr/bin/env python3
"""Passive (no-reset) console capture for the ESP32-C5 native USB-Serial-JTAG.

Why this exists (evidence-c5-05 §5):
  tmp/rts_boot_capture.py pulses RTS to force a reset. That is exactly wrong for
  a **true cold** observation: the pulse interrupts the cold boot that we want to
  observe, and the capture then shows a *warm* (RTS-reset) boot instead. In the
  first attempt the cold boot was truncated at `esp_bt_controller_enable(BLE)`
  precisely because of this.

How this works:
  The C5's USB-Serial-JTAG peripheral buffers console output even while no host
  is attached, and that buffer survives the CPU reset that opening the port can
  cause (memory: c3-usbjtag-serial-open-resets-dut). So we
    1. let the DUT power on and run to completion undisturbed (caller sleeps), then
    2. open the port with DTR/RTS held deasserted and never pulse them,
    3. drain whatever the peripheral buffered = the *complete* cold boot log.

Usage: c5_cold_passive_capture.py <port> <out_log> [seconds]
"""
import sys
import time
import serial

port = sys.argv[1]
outfile = sys.argv[2]
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0

# Open without asserting DTR/RTS. pyserial applies the control lines on open, so
# disable both *before* the port is opened by constructing it closed first.
ser = serial.Serial()
ser.port = port
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()
# Belt and braces: never assert them afterwards either.
ser.dtr = False
ser.rts = False

data = bytearray()
t0 = time.time()
while time.time() - t0 < secs:
    chunk = ser.read(4096)
    if chunk:
        data += chunk
ser.close()

with open(outfile, "wb") as f:
    f.write(data)

print(f"captured {len(data)} bytes (passive, no reset pulse)")

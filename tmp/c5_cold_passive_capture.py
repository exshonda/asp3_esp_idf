#!/usr/bin/env python3
"""Passive console capture for the ESP32-C5.

★★2026-07-17 WARNING — "passive" does NOT mean "no reset" (evidence-c5-05 §13):
  Opening the CP2102N (UART0) port REBOOTS the DUT. The bridge's DTR/RTS drive EN,
  and the kernel raises them on open regardless of the dtr/rts=False set below
  (they only take effect after the port is already open). Proven by a controlled
  A/B: with a bond established, opening this capture for 4 s made the next
  reconnect fail, and the capture itself recorded the reboot in the act:
      ESP-ROM:esp32c5-eco2-20250121
      rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
  This silently destroyed a whole round of conclusions (the "bond reuse bug" in
  §11 was purely this artifact - the reset wiped the RAM-backed bond store).

  RULES:
   1. Open the capture ONCE, BEFORE the state you care about starts, and hold it
      open for the whole measurement. Never open/close it midway through a test
      that depends on device state (bonds, counters, RAM contents).
   2. `rst:0x1 (POWERON)` here does NOT prove a true power cycle - this open
      produces the identical signature. The ONLY proof of a true cold boot is
      `uhubctl -l 1-6 -p 3-4 -a off` + reading back that the device disappeared
      from /dev/serial/by-id.
   3. The same applies to the USB-JTAG console, which resets with a different
      signature (rst:0x15 USB_UART_HPSYS).

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

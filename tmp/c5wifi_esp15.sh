#!/usr/bin/env bash
#
#  ESP32-C5 wifi_scan を «Espressif版 riscv32-esp-elf GCC 15.2.0» でビルド→
#  flash→boot する実機ヘルパ（c5wifi.sh の esp-15.2 版）。差分は c5ble_esp15.sh
#  と同じ3点（GCC_BIN／TOOLCHAIN_PREFIX／別BUILD dir）のみ。
#
#  使い方: c5wifi.sh と同じ（build/flash/boot/all/console/port）。既定app=wifi_scan。
#
BOARD_MAC="${BOARD_MAC:?BOARD_MAC を指定してください（対象ボードの MAC）。公開時にスクラブしたため既定値は持ちません}"		# C5#1
PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${BOARD_MAC}-if00"
UART_PORT="${UART_PORT:-/dev/ttyUSB3}"			# CP2102N console (uart0)

REPO="$HOME/TOPPERS/asp3_esp_idf"
GCC_BIN="$HOME/tools/espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin"
TOOLCHAIN_PREFIX="riscv32-esp-elf-"
ESPTOOL="$HOME/tools/espressif/python_env/idf6.1_py3.12_env/bin/esptool"
BUILD="${C5WIFI_BUILD:-$REPO/build/c5wifi_esp15}"
export PATH="$GCC_BIN:$PATH"

esp() { "$ESPTOOL" --chip esp32c5 --port "$PORT" "$@"; }

need_port() {
	if [ ! -e "$PORT" ]; then
		echo "## DUT not found at $PORT" >&2
		for d in /dev/ttyACM*; do
			udevadm info -q property -n "$d" 2>/dev/null | grep -q "USB_JTAG" && \
			  echo "   $d $(udevadm info -q property -n "$d" | grep ID_SERIAL_SHORT)" >&2
		done
		return 1
	fi
}

do_build() {
	local app="${1:-wifi_scan}"; shift || true
	local opts=(-DESP32C5_WIFI=ON)
	echo "## configure ($app) -> $BUILD [esp-15.2, prefix=$TOOLCHAIN_PREFIX]"
	rm -rf "$BUILD"
	cmake -S "$REPO/asp3/asp3_core" -B "$BUILD" -G Ninja \
	  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
	  -DRISCV64_TOOLCHAIN_PREFIX="$TOOLCHAIN_PREFIX" \
	  -DASP3_TARGET=esp32c5_espidf \
	  -DASP3_TARGET_DIR="$REPO/asp3/target/esp32c5_espidf" \
	  -DESP32C5_CONSOLE=uart0 \
	  -DASP3_APPLDIR="$REPO/apps/$app" -DASP3_APPLNAME="$app" \
	  -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration \
	  "${opts[@]}" "$@" >/dev/null || { echo "## configure FAILED"; return 1; }
	echo "## build"
	cmake --build "$BUILD" 2>&1 | tail -40
	[ -f "$BUILD/asp_flash.bin" ] && echo "## OK: $BUILD/asp_flash.bin" || echo "## !! no asp_flash.bin"
}

do_flash() {
	need_port || return 1
	[ -f "$BUILD/asp_flash.bin" ] || { echo "## no $BUILD/asp_flash.bin — run 'build' first"; return 1; }
	echo "## write-flash 0x0 (by-id $BOARD_MAC)"
	esp --no-stub write-flash 0x0 "$BUILD/asp_flash.bin" 2>&1 | tail -3
	do_boot
}

do_boot() {
	need_port || return 1
	echo "## boot (usb-reset -> watchdog-reset)"
	esp --before usb-reset --after watchdog-reset --no-stub flash-id 2>&1 | tail -2
}

do_console() {
	local secs="${1:-12}"
	python3 - "$UART_PORT" "$secs" <<'PYEOF'
import serial, sys, time
port, secs = sys.argv[1], float(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.5, dsrdtr=False, rtscts=False)
s.dtr = False
s.rts = False
end = time.time() + secs
buf = b""
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        buf += chunk
sys.stdout.buffer.write(buf)
PYEOF
}

case "${1:-}" in
	build) shift; do_build "$@" ;;
	flash) do_flash ;;
	boot)  do_boot ;;
	all)   shift; do_build "$@" && do_flash ;;
	console) shift; do_console "$@" ;;
	port)  echo "$PORT"; [ -e "$PORT" ] && echo "(present)" || echo "(ABSENT)" ;;
	*) sed -n '2,14p' "$0" ;;
esac

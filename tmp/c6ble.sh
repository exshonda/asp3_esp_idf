#!/usr/bin/env bash
#
#  ESP32-C6 BLE（ble_host_smoke_c6 ほか）の build→flash→boot 実機ヘルパ
#  （このマシン固有）。c5ble.sh の C6 版。
#
#  既定 app = ble_host_smoke_c6, v6.1(idf61) matched-set + SM
#  （-DESP32C6_BT=ON -DESP32C6_BT_IDF61=ON -DESP32C6_BT_IDF61_SM=ON）。
#  hal(v8) 版でテストしたい場合は BT_IDF61=0 を渡す（build関数の
#  引数 --hal を参照）。
#
#  使い方:
#    c6ble.sh build [app] [extra -Dcmake args...]   configure+build
#    c6ble.sh flash                                 asp_flash.bin を0x0へ書込み→boot
#    c6ble.sh boot                                  usb-reset->watchdog-resetでboot(RTS相当)
#    c6ble.sh all [app] [extra...]                  build→flash→boot
#    c6ble.sh port                                  by-id ポート表示
#
set -u

BOARD_MAC="${BOARD_MAC:-14:C1:9F:E0:5A:9C}"		# board C
PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${BOARD_MAC}-if00"

REPO="/home/honda/TOPPERS/asp3_esp_idf"
GCC_BIN="/home/honda/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
ESPTOOL="/home/honda/tools/espressif/python_env/idf6.1_py3.12_env/bin/esptool"
BUILD="${C6BLE_BUILD:-$REPO/build/c6ble}"
export PATH="$GCC_BIN:$PATH"

esp() { "$ESPTOOL" --chip esp32c6 --port "$PORT" "$@"; }

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
	local app="${1:-ble_host_smoke_c6}"; shift || true
	local opts=(-DESP32C6_BT=ON -DESP32C6_BT_IDF61=ON -DESP32C6_BT_IDF61_SM=ON)
	echo "## configure ($app) -> $BUILD  opts=${opts[*]} $*"
	rm -rf "$BUILD"
	cmake -S "$REPO/asp3/asp3_core" -B "$BUILD" -G Ninja \
	  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
	  -DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf- \
	  -DASP3_TARGET=esp32c6_espidf \
	  -DASP3_TARGET_DIR="$REPO/asp3/target/esp32c6_espidf" \
	  -DESP32C6_CONSOLE=usbjtag \
	  -DASP3_APPLDIR="$REPO/apps/$app" -DASP3_APPLNAME="$app" \
	  -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration \
	  "${opts[@]}" "$@" 2>&1 | tail -30 || { echo "## configure FAILED"; return 1; }
	echo "## build"
	cmake --build "$BUILD" 2>&1 | tail -20
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

case "${1:-}" in
	build) shift; do_build "$@" ;;
	flash) do_flash ;;
	boot)  do_boot ;;
	all)   shift; do_build "$@" && do_flash ;;
	port)  echo "$PORT"; [ -e "$PORT" ] && echo "(present)" || echo "(ABSENT)" ;;
	*) sed -n '2,20p' "$0" ;;
esac

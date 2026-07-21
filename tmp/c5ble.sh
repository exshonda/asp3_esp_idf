#!/usr/bin/env bash
#
#  ESP32-C5 BLE（ble_host_smoke_c5 ほか）の build→flash→boot 実機ヘルパ
#  （このマシン固有）。c3ble.sh の C5 版。
#
#  C3との違い:
#   - chip=esp32c5・ASP3_TARGET=esp32c5_espidf・-DESP32C5_BT=ON。
#   - ★C5/C6 は実機で mie/mip アクセスが «正常»（C3の csrw mie 罠は非該当）＝
#     ESP32C5_QEMU=OFF 不要。csrw mie は実機OK。
#   - C5 BT は IDF v6.1（~/tools/esp-idf-v6.1）の bt.c/ble.c/libble_app.a に依存。
#   - USB-JTAG は C3 同様 download-latch → watchdog-reset で起動（要確認）。
#   - RTC マーカは C5 の LP_AON STORE（C3の 0x60008050 系とは別アドレス）。
#
#  使い方:
#    c5ble.sh build [app] [extra -Dcmake args...]   configure+build（asp_flash.bin生成）
#    c5ble.sh flash                                 asp_flash.bin を 0x0 に書込み→boot
#    c5ble.sh boot                                  watchdog-reset で app 起動
#    c5ble.sh all [app] [extra...]                  build→flash→boot
#    c5ble.sh port                                  by-id ポート表示
#
#  既定 app = ble_host_smoke_c5。別ボードは BOARD_MAC 環境変数で上書き。
#
set -u

BOARD_MAC="${BOARD_MAC:?BOARD_MAC を指定してください（対象ボードの MAC）。公開時にスクラブしたため既定値は持ちません}"		# C5#1
PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${BOARD_MAC}-if00"

REPO="$HOME/TOPPERS/asp3_esp_idf"
#  ★C5の正典（canonical）コンパイラ＝xpack riscv-none-elf-gcc 15.2.0（固定）。
#  実施53(docs/c5-bringup.md)・docs/c5-toolchain.mdで実測確定：同一ツリー・
#  同一セッションでWiFi(wifi_scan,tmp/c5wifi.sh)とBLE(本script)の両方が
#  実機動作（BlueZ可視・AP検出）を実証．esp-14.2/esp-15.2は未検証(xpack15で
#  両方PASSのため計画上不要)．変更する場合はdocs/c5-toolchain.mdの動作表を要更新。
GCC_BIN="$HOME/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
ESPTOOL="$HOME/tools/espressif/python_env/idf6.1_py3.12_env/bin/esptool"
BUILD="${C5BLE_BUILD:-$REPO/build/c5ble}"
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
	local app="${1:-ble_host_smoke_c5}"; shift || true
	local opts=(-DESP32C5_BT=ON)
	echo "## configure ($app) -> $BUILD"
	rm -rf "$BUILD"
	cmake -S "$REPO/asp3/asp3_core" -B "$BUILD" -G Ninja \
	  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
	  -DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf- \
	  -DASP3_TARGET=esp32c5_espidf \
	  -DASP3_TARGET_DIR="$REPO/asp3/target/esp32c5_espidf" \
	  -DESP32C5_CONSOLE=usbjtag \
	  -DASP3_APPLDIR="$REPO/apps/$app" -DASP3_APPLNAME="$app" \
	  -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration \
	  "${opts[@]}" "$@" >/dev/null || { echo "## configure FAILED"; return 1; }
	echo "## build"
	cmake --build "$BUILD" 2>&1 | tail -5
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
	*) sed -n '2,30p' "$0" ;;
esac

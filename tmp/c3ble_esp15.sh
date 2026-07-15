#!/usr/bin/env bash
#
#  ESP32-C3 BLE（ble_host_smoke ほか）を «Espressif版 riscv32-esp-elf GCC 15.2.0»
#  でビルド→flash→boot する実機ヘルパ（c3ble.sh の esp-15.2 版）。
#
#  c3ble.sh（正典＝xpack riscv-none-elf-gcc 15.2.0）との差分は以下の3点のみ：
#   - GCC_BIN を esp-15.2 の bin へ
#   - -DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-（PATHだけでは不十分。
#     cmake呼び出しに焼き込まれているprefixを明示的に上書きする）
#   - BUILD dir を別（build/c3ble_esp15）にして xpack ビルドと衝突させない
#
#  march/mabi は C3 chip.cmake 固定＝-march=rv32imc_zicsr_zifencei -mabi=ilp32。
#  esp-15.2 のmultilibに `rv32imc_zicsr_zifencei/ilp32` が厳密一致で存在
#  （`riscv32-esp-elf-gcc -print-multi-lib` で確認済み。C5と同一パターン）。
#
#  -DESP32C3_QEMU=OFF は c3ble.sh と同じく必須のまま維持（csrw mie 罠）。
#  csrw mie 自己検査（xpack objdump相当）も継承。
#
#  使い方: c3ble.sh と同じ（build/flash/boot/all/mark/read/port）。既定app=ble_host_smoke。
#
set -u

#  DUT（board B）＝BLE MAC 兼 USB-JTAG iSerial。別機は BOARD_MAC=... で上書き。
BOARD_MAC="${BOARD_MAC:-60:55:F9:57:BA:BC}"
PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${BOARD_MAC}-if00"

REPO="/home/honda/TOPPERS/asp3_esp_idf"
GCC_BIN="/home/honda/tools/espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin"
TOOLCHAIN_PREFIX="riscv32-esp-elf-"
ESPTOOL="/home/honda/tools/espressif/python_env/idf6.1_py3.12_env/bin/esptool"
BUILD="${C3BLE_BUILD:-$REPO/build/c3ble_esp15}"
export PATH="$GCC_BIN:$PATH"

esp() { "$ESPTOOL" --chip esp32c3 --port "$PORT" "$@"; }

need_port() {
	if [ ! -e "$PORT" ]; then
		echo "## DUT not found at $PORT" >&2
		echo "## connected 303a nodes:" >&2
		for d in /dev/ttyACM*; do
			udevadm info -q property -n "$d" 2>/dev/null \
			  | grep -q "USB_JTAG" && \
			  echo "   $d $(udevadm info -q property -n "$d" | grep ID_SERIAL_SHORT)" >&2
		done
		return 1
	fi
}

do_build() {
	local app="${1:-ble_host_smoke}"; shift || true
	local opts=()
	case "$app" in
		ble_host_smoke|bt_smoke)
			opts+=(-DESP32C3_BT=ON -DESP32C3_BT_SM=ON) ;;
	esac
	echo "## configure ($app) -> $BUILD [esp-15.2, prefix=$TOOLCHAIN_PREFIX]"
	rm -rf "$BUILD"
	cmake -S "$REPO/asp3/asp3_core" -B "$BUILD" -G Ninja \
	  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
	  -DRISCV64_TOOLCHAIN_PREFIX="$TOOLCHAIN_PREFIX" \
	  -DASP3_TARGET=esp32c3_espidf \
	  -DASP3_TARGET_DIR="$REPO/asp3/target/esp32c3_espidf" \
	  -DESP32C3_QEMU=OFF \
	  -DESP32C3_CONSOLE=usbjtag \
	  -DASP3_APPLDIR="$REPO/apps/$app" -DASP3_APPLNAME="$app" \
	  -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration \
	  "${opts[@]}" "$@" >/dev/null || { echo "## configure FAILED"; return 1; }
	echo "## build"
	cmake --build "$BUILD" 2>&1 | tail -40
	[ -f "$BUILD/asp.elf" ] || { echo "## !! no asp.elf"; return 1; }
	#  実機安全性の自己検査：csrw mie が1つでもあれば QEMU パス混入＝危険。
	local mie
	mie=$("$GCC_BIN/${TOOLCHAIN_PREFIX}objdump" -d "$BUILD/asp.elf" | grep -c "csrw.*mie")
	if [ "$mie" != "0" ]; then
		echo "## !! WARNING: $mie x 'csrw mie' in elf — QEMU path leaked, would crash on HW"
		return 1
	fi
	echo "## OK: no csrw mie (real-HW safe). image: $BUILD/asp_flash.bin"
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
	echo "## boot (usb-reset -> watchdog-reset, escapes download latch)"
	esp --before usb-reset --after watchdog-reset --no-stub flash-id 2>&1 | tail -2
}

read_word() {  #  $1=addr ; echoes 0xVALUE（download-mode に留める）
	esp --before usb-reset --after no-reset --no-stub read-mem "$1" 2>/dev/null \
	  | grep -oiE "0x[0-9a-f]{8}" | tail -1
}

do_mark() {
	need_port || return 1
	echo "## RTC markers (esptool read-mem, board B $BOARD_MAC):"
	local sync pair adv conn disc enc advrc
	sync=$(read_word 0x60008050); pair=$(read_word 0x60008054)
	adv=$(read_word 0x6000805C);  conn=$(read_word 0x600080C0)
	disc=$(read_word 0x600080B8); enc=$(read_word 0x60008058)
	advrc=$(read_word 0x600080C4)
	printf "  0x50 SYNC/RESET = %s  (0x5ADE51C0=host sync / 0x5Exxxxxx=reset)\n" "$sync"
	printf "  0x54 PAIRING    = %s  (0x5DC0<st><our><peer>=PAIRING_COMPLETE; 他=alloc trace=未発火)\n" "$pair"
	printf "  0x5C ADV        = %s  (0x0ADE5000=advertising開始)\n" "$adv"
	printf "  0xC0 CONN       = %s  (0x604E<st><cnt>=接続)\n" "$conn"
	printf "  0xB8 DISC       = %s  (0xD15C<reason><cnt>=切断)\n" "$disc"
	printf "  0x58 ENC/WRITE  = %s  (0x5DE0<st>=ENC_CHANGE / 0x7717xx=write特性)\n" "$enc"
	printf "  0xC4 adv-rc     = %s  (0xAD0000<rc>=start_advertising 戻り値)\n" "$advrc"
	echo "## re-booting app..."
	do_boot >/dev/null
}

case "${1:-}" in
	build) shift; do_build "$@" ;;
	flash) do_flash ;;
	boot)  do_boot ;;
	all)   shift; do_build "$@" && do_flash ;;
	mark)  do_mark ;;
	read)  need_port && read_word "$2" ;;
	port)  echo "$PORT"; [ -e "$PORT" ] && echo "(present)" || echo "(ABSENT)" ;;
	*) sed -n '2,20p' "$0" ;;
esac

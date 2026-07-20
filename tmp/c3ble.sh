#!/usr/bin/env bash
#
#  ESP32-C3 BLE（ble_host_smoke_c3 ほか）の build→flash→boot→RTCマーカ読取を
#  一括化する実機操作ヘルパ（このマシン固有）。
#
#  ★ハマりどころを全部埋め込む（過去に実際にやらかした事故の再発防止）:
#   1. 実機ビルドは -DESP32C3_QEMU=OFF 必須。既定 ON だと TOPPERS_USE_QEMU が
#      定義され chip_initialize が `csrw mie` を出す→実機は mie 未実装で
#      «Illegal instruction» で即クラッシュ（docs/bt-shim.md:996-1006）。
#   2. USB-Serial/JTAG は write-flash 後の RTS hard reset だと download-mode に
#      ラッチしてアプリが Direct Boot しない。`--before usb-reset --after
#      watchdog-reset` で watchdog リセットがラッチを消費して起動する
#      （docs/bt-shim.md:274-283）。
#   3. リセットの度に /dev/ttyACMx 番号が流動する。安定した by-id パス
#      （USB iSerial=BLE MAC）で常に同じDUTを掴む。
#   4. gcc-15.2 の implicit-function-declaration は既存hal事象＝ -Wno-error 付与。
#
#  使い方:
#    c3ble.sh build [app] [extra -Dcmake args...]   configure+build（asp_flash.bin生成）
#    c3ble.sh flash                                 asp_flash.bin を 0x0 に書込み→boot
#    c3ble.sh boot                                  watchdog-reset で app 起動（latch脱出）
#    c3ble.sh all [app] [extra...]                  build→flash→boot
#    c3ble.sh mark                                  RTCマーカを読取・デコード（読後 boot）
#    c3ble.sh read <addr>                           任意RTC/メモリ語を1つ読む
#    c3ble.sh port                                  解決した by-id ポートを表示
#
#  既定 app = ble_host_smoke_c3（SM有効）。別ボードは BOARD_MAC 環境変数で上書き。
#  観測（ライブBLE）はホスト bluetoothctl / スマホから。コンソール開放は
#  DUTをリセットするので RTC マーカ読取を主判定にする。
#
set -u

#  DUT（board B）＝BLE MAC 兼 USB-JTAG iSerial。別機は BOARD_MAC=... で上書き。
BOARD_MAC="${BOARD_MAC:-60:55:F9:57:C2:60}"
PORT="/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_${BOARD_MAC}-if00"

REPO="/home/honda/TOPPERS/asp3_esp_idf"
GCC_BIN="/home/honda/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin"
ESPTOOL="/home/honda/tools/espressif/python_env/idf6.1_py3.12_env/bin/esptool"
BUILD="${C3BLE_BUILD:-$REPO/build/c3ble}"
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
	local app="${1:-ble_host_smoke_c3}"; shift || true
	local opts=()
	#  BLE系アプリは BT/SM を既定 ON。それ以外は素で通す。
	case "$app" in
		ble_host_smoke_c3|bt_smoke_c3)
			opts+=(-DESP32C3_BT=ON -DESP32C3_BT_SM=ON) ;;
	esac
	echo "## configure ($app) -> $BUILD"
	rm -rf "$BUILD"
	cmake -S "$REPO/asp3/asp3_core" -B "$BUILD" -G Ninja \
	  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
	  -DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf- \
	  -DASP3_TARGET=esp32c3_espidf \
	  -DASP3_TARGET_DIR="$REPO/asp3/target/esp32c3_espidf" \
	  -DESP32C3_QEMU=OFF \
	  -DESP32C3_CONSOLE=usbjtag \
	  -DASP3_APPLDIR="$REPO/apps/$app" -DASP3_APPLNAME="$app" \
	  -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration \
	  "${opts[@]}" "$@" >/dev/null || { echo "## configure FAILED"; return 1; }
	echo "## build"
	cmake --build "$BUILD" 2>&1 | tail -5
	#  実機安全性の自己検査：csrw mie が1つでもあれば QEMU パス混入＝危険。
	local mie
	mie=$("$GCC_BIN/riscv-none-elf-objdump" -d "$BUILD/asp.elf" | grep -c "csrw.*mie")
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
	#  bond 判定（0x54 PAIRING．0x5DC0 タグの有無で発火/未発火を判別）
	case "$pair" in
		0x5dc0*|0x5DC0*)
			local st our peer
			st=$(( (0x${pair#0x} >> 8) & 0xff ))
			our=$(( (0x${pair#0x} >> 4) & 0xf ))
			peer=$(( 0x${pair#0x} & 0xf ))
			echo "  -> PAIRING_COMPLETE status=$st  our_sec=$our peer_sec=$peer"
			if [ "$st" = "0" ] && [ "$our" -ge 1 ]; then
				echo "  -> ★DUT側 bond 成立（残る未成立は host 側）"
			elif [ "$st" = "0" ]; then
				echo "  -> status=0 だが bond件数0＝鍵保存されず（store要確認）"
			else
				echo "  -> DUT側 SM が status=$st で失敗（BLE_SM_ERR_* を要照合）"
			fi ;;
		*)
			echo "  -> PAIRING_COMPLETE 未発火（0x5DC0タグ無し＝alloc trace残存）＝"
			echo "     暗号ON(0x58=0x5DE0)後の鍵配布で停止 or そもそも未ペアリング" ;;
	esac
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
	*) sed -n '2,40p' "$0" ;;
esac

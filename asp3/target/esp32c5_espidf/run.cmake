#
#		実行用カスタムターゲット定義（ESP32-C5用）
#
#  ルートCMakeLists.txtが asp ターゲット定義後にincludeする
#  （ASP3_TARGET_RUN_CMAKE）．esp32c6版（asp3_target/esp32c6_espidf/
#  run.cmake相当）からのコピー・C5対応。
#
#  Espressif版QEMU forkにesp32c5マシンが追加されているかは【実機確認
#  待ち】（docs/c5-port-design.md §8.1 14番．C6の時のように「非対応」
#  と決め打ちしていない）。本ファイルは実機書込み用のフラッシュイメージ
#  生成のみを行う。
#

if(ASP3_SEAM_BOOT)
    #
    #  seam：実ESP-IDF 2nd-stage bootloader が読む標準イメージ形式
    #  （esptool elf2image）を生成する．Direct Boot の objcopy 生ダンプ
    #  とは別物（イメージヘッダ＋セグメントヘッダ＋checksum＋SHA256）。
    #
    #  出力は asp_seam.bin（app．flash 0x10000 へ書く）。bootloader と
    #  partition table は esp-idf 側でビルドしたものを使う
    #  （scripts/seam_c5/build_bootloader.sh 参照）。
    #
    add_custom_command(TARGET asp POST_BUILD
        COMMAND ${ESP32C5_ESPTOOL} --chip esp32c5 elf2image
                --flash-mode dio --flash-freq 80m --flash-size 2MB
                -o ${CMAKE_BINARY_DIR}/asp_seam.bin
                $<TARGET_FILE:asp>
        COMMAND ${ESP32C5_ESPTOOL} --chip esp32c5 image-info
                ${CMAKE_BINARY_DIR}/asp_seam.bin
        COMMENT "Generating asp_seam.bin (ESP32-C5 seam / ESP-IDF app image)"
        VERBATIM
    )
else()
    #  対象セクションを明示する（.dataが空のときにLMAがRAMアドレスへ
    #  落ちて巨大なイメージが生成されるのを防ぐ）．C5は分岐A（IROM/DROM
    #  分離無し．esp32c5.ld参照）を採用しているため抽出対象は.text/.data
    #  の2つのみでよい．LMAはFLASH基準（0x42000000起点）のため，--pad-toも
    #  FLASH基準で4MBを指定する．
    add_custom_command(TARGET asp POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary
                -j .text -j .data
                --pad-to=0x42400000 --gap-fill=0xff
                $<TARGET_FILE:asp> ${CMAKE_BINARY_DIR}/asp_flash.bin
        COMMENT "Generating asp_flash.bin (ESP32-C5 Direct Boot flash image)"
        VERBATIM
    )
endif()

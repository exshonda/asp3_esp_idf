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
#  生成のみを行う（objcopyによるDirect Boot形式イメージ．C6と同じ2
#  セクション抽出＝分岐A＝IROM/DROM分離なしの前提．esp32c5.ld参照）。
#

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

#
#		実行用カスタムターゲット定義（ESP32-C6用）
#
#  ルートCMakeLists.txtが asp ターゲット定義後にincludeする
#  （ASP3_TARGET_RUN_CMAKE）．
#
#  QEMUのesp32c6マシンは -kernel でのELF直ロードに対応せず，Direct
#  Boot形式のフラッシュイメージから起動するため，ポストビルドで
#  objcopyによりフラッシュイメージ（asp_flash.bin＝4MB・0xffパディング）
#  を生成する．実機書込み（esptool write_flash 0x0 asp_flash.bin）にも
#  同じイメージを使用できる．
#

#  対象セクションを明示する（.dataが空のときにLMAがRAMアドレスへ
#  落ちて巨大なイメージが生成されるのを防ぐ）．C6はIROM/DROM分離が
#  無く.rodataも.textセクションに統合されている（esp32c6.ld参照）ため
#  抽出対象は.text/.dataの2つのみでよい．LMAはFLASH基準
#  （0x42000000起点）のため，--pad-toもFLASH基準で4MBを指定する．
add_custom_command(TARGET asp POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            -j .text -j .data
            --pad-to=0x42400000 --gap-fill=0xff
            $<TARGET_FILE:asp> ${CMAKE_BINARY_DIR}/asp_flash.bin
    COMMENT "Generating asp_flash.bin (ESP32-C6 Direct Boot flash image)"
    VERBATIM
)

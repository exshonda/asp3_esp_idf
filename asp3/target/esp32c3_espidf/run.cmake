#
#		実行用カスタムターゲット定義（ESP32-C3用）
#
#  ルートCMakeLists.txtが asp ターゲット定義後にincludeする
#  （ASP3_TARGET_RUN_CMAKE）．
#
#  QEMUのesp32c3マシンは -kernel でのELF直ロードに対応せず，Direct
#  Boot形式のフラッシュイメージから起動するため，ポストビルドで
#  objcopyによりフラッシュイメージ（asp_flash.bin＝4MB・0xffパディング）
#  を生成する．実機書込み（esptool write_flash 0x0 asp_flash.bin）にも
#  同じイメージを使用できる．
#

#  対象セクションを明示する（.dataが空のときにLMAがRAMアドレスへ
#  落ちて巨大なイメージが生成されるのを防ぐ）．LMAはDROM基準
#  （0x3C000000起点）のため，--pad-toもDROM基準で4MBを指定する．
add_custom_command(TARGET asp POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary
            -j .text -j .rodata -j .data
            --pad-to=0x3C400000 --gap-fill=0xff
            $<TARGET_FILE:asp> ${CMAKE_BINARY_DIR}/asp_flash.bin
    COMMENT "Generating asp_flash.bin (ESP32-C3 Direct Boot flash image)"
    VERBATIM
)

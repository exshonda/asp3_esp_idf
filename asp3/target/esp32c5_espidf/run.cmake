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
    #  ★seam は POST_BUILD で esptool を呼ぶ＝**ビルド中に実体が要る**
    #  （Direct Boot は ${CMAKE_OBJCOPY} なので不要）。無いまま進むと
    #  `/bin/sh: 1: esptool: not found` という分かりにくいビルドエラーに
    #  なるため、configure 時に明示的に止める（実測：本ガード導入前は
    #  リンク成功後の POST_BUILD で落ちていた）。
    #
    asp3_require_esptool(ESP32C5_ESPTOOL "ESP32-C5 seam boot (ASP3_SEAM_BOOT=ON)")

    #
    #  seam：実ESP-IDF 2nd-stage bootloader が読む標準イメージ形式
    #  （esptool elf2image）を生成する．Direct Boot の objcopy 生ダンプ
    #  とは別物（イメージヘッダ＋セグメントヘッダ＋checksum＋SHA256）。
    #
    #  出力は asp_seam.bin（app．flash 0x10000 へ書く）。bootloader と
    #  partition table は esp-idf 側でビルドしたものを使う。
    #  ★実際に動く手順は
    #    .steering/20260716-c3c5c6-esp-idf-supply-migration/
    #      evidence-c5-11-seam-rerun-on-current-tree.md §1
    #  （最小 IDF プロジェクト＋submodule esp-idf v5.5.4。2026-07-21 実機再現）。
    #  ※従来ここが案内していた `scripts/seam_c5/build_bootloader.sh` は
    #    **リポジトリに存在しない**（scripts/ 自体が無い）＝案内先が実在しない
    #    エラーだったので差し替えた。
    #  ★seam は **真POWERON でしか起動しない**（USB-JTAG リセットでは WDT ループ）。
    #    観測は uart0 コンソール（既定）で行うこと（同 §2・§3）。
    #
    #
    #  ★引数は «アンダースコア» 形（`--flash_mode`／`image_info`）を使うこと。
    #    esptool v5 で `--flash-mode`／`image-info`（ハイフン）へ改名されたが、
    #    **本リポジトリが pin している esp-idf v5.5.4 の python env が持つのは
    #    esptool v4.12.0** で、v4 はハイフン形を受け付けない：
    #        esptool: error: unrecognized arguments: --flash-mode --flash-freq …
    #    アンダースコア形は **v4.12.0 と v5.3.1 の «両方» が受け付ける**ことを
    #    実測確認済み（両者で elf2image / image_info とも成功）。
    #    ＝pin した IDF だけの clean 環境でも、v6.1 系の新しい esptool が
    #      入った開発機でも、どちらでも通る。
    #  ★この不整合は CI（.github/workflows/build.yml）が実際に検出した。
    #    ハイフン形のままだと «開発機に IDF v6.1 の venv が同居している場合のみ
    #    通る» という典型的な "works on my machine" 状態だった
    #    （経緯＝.steering/20260721-docs-onboarding/evidence-02-ci.md §9）。
    #
    add_custom_command(TARGET asp POST_BUILD
        COMMAND ${ESP32C5_ESPTOOL} --chip esp32c5 elf2image
                --flash_mode dio --flash_freq 80m --flash_size 2MB
                -o ${CMAKE_BINARY_DIR}/asp_seam.bin
                $<TARGET_FILE:asp>
        COMMAND ${ESP32C5_ESPTOOL} --chip esp32c5 image_info
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

/*
 *  ESP32-C5用リンカスクリプト（seam＝実ESP-IDF 2nd-stage bootloader経由）
 *
 *  Direct Boot版（esp32c5.ld）との差分は「ブート方式」だけであり，
 *  カーネル・arch・チップ依存部には一切影響しない。既定は Direct Boot
 *  （ASP3_SEAM_BOOT=OFF）で，本ldは -DASP3_SEAM_BOOT=ON のときだけ使う。
 *
 *  ── なぜ2つのフラッシュセグメントが要るか（実測に基づく設計）──
 *
 *  (1) C5は SOC_MMU_DI_VADDR_SHARED=1（soc_caps.h:326）＝命令/データの
 *      仮想アドレス空間が共通（SOC_DROM_LOW == SOC_IROM_LOW == 0x42000000，
 *      soc/soc.h:148-153）。このためブートローダの unpack_load_app() は
 *      「共有vaddr」版が使われ（bootloader_utility.c:804 #if
 *      SOC_MMU_DI_VADDR_SHARED），フラッシュへマップされるセグメントを
 *      ちょうど2つ要求する：
 *
 *          assert(rom_index == 2);      ← bootloader_utility.c:843
 *
 *      Direct Boot版のldは .text に code と rodata を同居させた
 *      「1セグメント」構成のため，この assert に抵触する。よって seam
 *      では ESP-IDF 本家アプリと同じく text(IROM) と rodata(DROM) を
 *      別々の64KB MMUページへ分ける。
 *
 *  (2) esp_app_desc は「必ず segment #0」に置かれる前提でブートローダが
 *      読む（esp_image_format.c:767-778，efuse blk rev検証）。esptool は
 *      **出力セクション名が厳密に `.flash.appdesc`** のものを
 *      フラッシュセグメント列の先頭へ移動させる特別扱いを持つ
 *      （bin_image.py:805-811「Patch to support ESP32-C6 union bus
 *      memmap」）。したがって rodata 側の出力セクション名は
 *      `.flash.appdesc` でなければならない（名前が違うと app_desc が
 *      segment #0 に来ず，ブートローダが別データを app_desc として
 *      読んで不正な efuse 要件を検出する）。
 *
 *  (3) vaddr の下位16bitを 0x20 に揃える理由：フラッシュキャッシュMMUは
 *      (paddr % 64KB == vaddr % 64KB) を要求する。.bin は 0x18 バイトの
 *      ファイルヘッダ＋各セグメント 0x08 バイトのセグメントヘッダを持つ
 *      ため，先頭セグメントのデータは file offset 0x20 から始まる。
 *      0x20 起点にしておくと esptool のパディング計算が最小で済む
 *      （本家 esp-idf の memory.ld.in も同じ 0x20 を使う）。
 */

OUTPUT_ARCH(riscv)

/* esp-halのペリフェラル構造体インスタンス（SYSTIMER等．-Lで検索） */
INCLUDE esp32c5.peripherals.ld

MEMORY
{
    /*
     *  FLASH：Direct Boot版と同じ 0x42000000 起点の窓だが，seam では
     *  先頭 0x20 をイメージヘッダ相当の余白として空ける（上記(3)）。
     */
    FLASH(rx) : ORIGIN = 0x42000020, LENGTH = 4096k - 0x20

    /*
     *  RAM：Direct Boot版（esp32c5.ld）と完全に同一。
     */
    RAM(rwx)  : ORIGIN = 0x40800000, LENGTH = 384k
}

ENTRY(_flash_entry)

SECTIONS
{
    /*
     *  IROM（segment #1相当）＝コード。
     *
     *  Direct Bootマジック（.flash_header）は seam では不要（ROMではなく
     *  2nd-stage bootloaderがエントリを image header の entry_addr から
     *  取る）ため配置しない。エントリ .text.entry は ENTRY(_flash_entry)
     *  として elf2image が entry_addr へ焼き込む。
     */
    .flash.text : {
        KEEP(*(.text.entry))       /* エントリ（j start） */
        . = ALIGN(64);             /* trap_vector_tableのアライン（CLIC
                                    * non-vectoredモードは64byte要求．
                                    * Direct Boot版と同一） */
        *(.text*)
        /*
         *  IRAM_ATTR系・Wi-Fi/BLE blobが要求する高速実行用セクション。
         *  Direct Boot版（esp32c5.ld）と同一の一覧（明示ルールが無いと
         *  orphan section扱いになりLMAが壊れる）。本構成もIRAM領域を
         *  持たないXIPのためFLASHへ収容する。
         */
        *(.iram1 .iram1.*)
        *(.high_perf_code_iram1 .high_perf_code_iram1.*)
        *(.adv_fast_execute_code_iram1 .adv_fast_execute_code_iram1.*)
        *(.coexiram .coexiram.*)
        *(.wifi0iram .wifi0iram.*)
        *(.wifiextrairam .wifiextrairam.*)
        *(.wifiorslpiram .wifiorslpiram.*)
        *(.wifirxiram .wifirxiram.*)
        *(.wifislpiram .wifislpiram.*)
        *(.wifislprxiram .wifislprxiram.*)
        . = ALIGN(16);
    } > FLASH

    /*
     *  DROM（segment #0）＝app_desc＋rodata。
     *
     *  .flash.text の次の64KB MMUページ境界＋0x20 へ送る（本家の
     *  .flash_rodata_dummy と同じ効果を，明示アドレス式で表現したもの）。
     *  esptool は .flash.appdesc をフラッシュセグメント列の先頭へ
     *  移すため，ファイル上は rodata が先・text が後になる（本家アプリの
     *  image-info と同じ形）。
     *
     *  ★アドレスは `.`（ロケーションカウンタ）ではなく ADDR/SIZEOF で
     *  明示する。`> FLASH` のリージョン割付を使う場合，SECTIONS直下の
     *  `. = ALIGN(...)` はリージョンの位置カウンタに反映されず**黙って
     *  無視される**（実測：最初この書き方をして .flash.appdesc が
     *  0x4205db40＝非ページ境界に落ちた）。
     */
    .flash.appdesc ALIGN(ADDR(.flash.text) + SIZEOF(.flash.text), 0x10000) + 0x20 : {
        /* !この前に何も置かないこと！ app_descは segment #0 の先頭 */
        KEEP(*(.rodata_desc))
        . = ALIGN(16);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.rodata_wlog_*)
        . = ALIGN(16);
    } > FLASH

    /*
     *  Main DATA section (RAM)
     *
     *  Direct Boot版との違い：LMAをFLASHに置かない（AT()を使わない）。
     *  seam では 2nd-stage bootloader が .data をRAMへ直接ロードする
     *  （esptoolはRAMアドレスのセクションを ram_segment として出力し，
     *  ブートローダが load する）。LMA==VMA になるため start.S の
     *  ROM化コピーループ（__idata_start→__data_start）は自己コピー＝
     *  no-op となり，asp3_core側の共有start.Sを一切変更せずに済む。
     */
    .data : ALIGN(8) {
        __data_start = .;
        *(.data*)
        *(.dram1 .dram1.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(8);
        _thread_local_data_start = .;
        *(.tdata .tdata.*)
        _thread_local_data_end = .;
        . = ALIGN(4);
    } > RAM

    /*
     *  LMA==VMA（上記のとおりコピーは no-op）
     */
    __idata_start = LOADADDR(.data);
    __idata_end = __idata_start + SIZEOF(.data);

    /* MAIN BSS SECTION（Direct Boot版と同一．start.Sがクリアする） */
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM

    /* TLSテンプレートのBSS部分の境界（Direct Boot版と同一） */
    .tbss (NOLOAD) : ALIGN(8) {
        _thread_local_bss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
        _thread_local_bss_end = .;
    } > RAM

    /*
     *  Direct Bootマジック（flash_header.S の .flash_header）は seam では
     *  意味を持たない。SHF_ALLOC付きのため明示的に捨てないと orphan
     *  section としてどこかへ配置され，elf2image が余計なセグメントを
     *  作ってしまう（＝上記 assert(rom_index == 2) を壊しうる）。
     *  flash_header.S 自体は Direct Boot と共有のため無改変で残す。
     */
    /DISCARD/ : {
        *(.flash_header)
    }
}

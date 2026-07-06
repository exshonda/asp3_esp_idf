/*
 *  ESP32-C6用リンカスクリプト（Direct Boot・XIP実行．Phase B＝esp-hal統合版）
 *
 *  asp3_core の target/esp32c6_gcc/esp32c6.ld（Phase A．レジスタ直
 *  叩き版）に，esp-halのペリフェラル構造体インスタンス（SYSTIMER等．
 *  -Lで検索）のINCLUDEを追加しただけ（C3のPhase A→B移行と同じ変更）．
 *  C6はSOC_DROM_LOW＝SOC_IROM_LOW（共に0x42000000）＝IROM/DROM分離が
 *  無いため，C3のような複数ORIGIN分割は不要（詳細はPhase A版の
 *  コメント参照）．
 */

OUTPUT_ARCH(riscv)

/* esp-halのペリフェラル構造体インスタンス（SYSTIMER等．-Lで検索） */
INCLUDE esp32c6.peripherals.ld

MEMORY
{
    FLASH(rx) : ORIGIN = 0x42000000, LENGTH = 4096k
    /*
     *  DIAGNOSTIC（ハンドオフStep0調査・一時的）：
     *  通常のDirect Bootでは RAM ORIGIN=0x40800000・LENGTH=448k．
     *  しかしESP-IDFからのハンドオフ実験では，ESP-IDFがロック付きPMPで
     *  [0x40800000,0x408184b4) を読取専用(R+X,no-W)に保護しているため，
     *  ASP3の.data/.bss/istackをこの領域に置くとbssクリアのストアがfault
     *  してハングする（実測でPMP entry4=0x8dと確認）．PMP entry5が
     *  [0x408184b4,0x40880000) をRW(0x8b)として開けているので，そこへ
     *  移す．調査完了後にrevertする（通常ビルドは0x40800000/448kに戻す）．
     */
    RAM(rwx)  : ORIGIN = 0x40819000, LENGTH = 412k
}

ENTRY(_flash_entry)

SECTIONS
{
    /*
     *  Direct Bootマジック（+0）・エントリ（+8）・コード・rodataを
     *  単一のFLASH領域にまとめる（IROM/DROM分離が無いため区別不要）．
     */
    .text : {
        KEEP(*(.flash_header))     /* +0：マジックナンバー（8バイト） */
        KEEP(*(.text.entry))       /* +8：エントリ（j start） */
        . = ALIGN(256);            /* trap_vector_tableのアライン */
        *(.text*)
        /*
         *  IRAM_ATTR系関数（.iram1.N＝esp_hw_support/periph_ctrl.c・
         *  rtc_clk.c等）＋Wi-Fi blob（libcoexist.a/libnet80211.a/
         *  libpp.a等）が要求する高速実行用セクション一覧（C3の
         *  esp32c3_espidf/esp32c3.ldと同じ一覧．明示ルールが無いと
         *  orphan section扱いとなり.dataとLMAが重なる．本ldは独立した
         *  IRAM領域を持たないDirect Boot XIP構成のため通常コードと
         *  同じくFLASHへ収容する＝実行速度上の最適化は失うが機能上は
         *  問題ない）
         */
        *(.iram1 .iram1.*)
        *(.coexiram .coexiram.*)
        *(.wifi0iram .wifi0iram.*)
        *(.wifiextrairam .wifiextrairam.*)
        *(.wifiorslpiram .wifiorslpiram.*)
        *(.wifirxiram .wifirxiram.*)
        *(.wifislpiram .wifislpiram.*)
        *(.wifislprxiram .wifislprxiram.*)
        . = ALIGN(16);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        /*
         *  .rodata_wlog_{debug,error,info,verbose,warning}.N（ログ
         *  文字列圧縮用セクション．C3のesp32c3_espidf/esp32c3.ldと
         *  同じ扱い）
         */
        *(.rodata_wlog_*)
        . = ALIGN(16);
    } > FLASH

    /*
     *  Main DATA section (RAM)
     *
     *  LMAは.textの直後（FLASH基準）を明示する（.dataが空のときに
     *  LMAがVMA（RAM）へ落ちてフラッシュイメージが巨大化するのを防ぐ）．
     */
    .data : AT(LOADADDR(.text) + SIZEOF(.text)) ALIGN(8) {
        __data_start = .;
        LONG(0)     /* 番兵（.dataが空だとldがAT指定を無視しLMAがRAMに落ちるため） */
        *(.data*)
        /*
         *  .dram1.N（DRAM_ATTR変数．IRAM_ATTR関数と対の初期化済み
         *  データ．esp_hw_support/rtc_clk.c等．.dataと同じくRAMへ
         *  収容する．C3のesp32c3_espidf/esp32c3.ldと同じ扱い）
         */
        *(.dram1 .dram1.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > RAM
    /*
     *  ROM化コピー元はFLASH側のアドレスで取る（start.Sはデータバス
     *  経由で読み出す．IROM/DROMが同一アドレス空間のためそのまま読める）
     */
    __idata_start = LOADADDR(.data);
    __idata_end = __idata_start + SIZEOF(.data);

    /* MAIN BSS SECTION */
    .bss (NOLOAD) : ALIGN(4) {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM
}

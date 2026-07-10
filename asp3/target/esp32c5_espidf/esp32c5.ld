/*
 *  ESP32-C5用リンカスクリプト（Direct Boot・XIP実行）
 *
 *  esp32c6版（asp3_target/esp32c6_espidf/esp32c6.ld相当）をベースに，
 *  docs/c5-port-design.md §6.2の分岐A（IROM/DROM統合，C6と同型）を
 *  採用した。C6の一時的PMP診断用RAM ORIGINシフト（0x40819000／412k．
 *  §8.3「C6資産の非移植方針」参照）は持ち込まず，通常のDirect Boot
 *  配置（RAM ORIGIN=0x40800000）に戻している。
 *
 *  【実機確認待ち】docs/c5-port-design.md §8.1 8番。分岐Aの前提は
 *  SOC_DROM_LOW==SOC_IROM_LOW（共に0x42000000）だが，本移植時点では
 *  SOC_IROM_LOWの値のみ確認できておりSOC_DROM_LOWとの等号関係を示す
 *  記述をhalヘッダから確定できていない。分離型（C3方式）であることが
 *  判明した場合は本ldをC3の3領域（IROM/DROM/RAM）構成へ作り直す必要が
 *  ある。
 */

OUTPUT_ARCH(riscv)

/* esp-halのペリフェラル構造体インスタンス（SYSTIMER等．-Lで検索） */
INCLUDE esp32c5.peripherals.ld

MEMORY
{
    /*
     *  FLASH：SOC_IROM_LOW=0x42000000．C5のマップ窓はSOC_IROM_HIGH=
     *  0x44000000＝32MiB固定（SOC_MMU_PAGE_SIZE_CONFIGURABLEが存在せず
     *  可変ページサイズ非対応．docs/c5-port-design.md §3参照）だが，
     *  実搭載Flash容量はボード依存のため，C6と同じ4MiBを暫定窓幅として
     *  採用する（実機のFlash容量判明後に調整すること）。
     */
    FLASH(rx) : ORIGIN = 0x42000000, LENGTH = 4096k

    /*
     *  RAM：ESP32C5_DRAM_BASE=0x40800000，LENGTH=384k（C6の512Kより
     *  少ない．esp32c5.h ESP32C5_DRAM_SIZE参照．C6版に存在した一時的
     *  PMP診断用ORIGINシフトは持ち込まない）。
     */
    RAM(rwx)  : ORIGIN = 0x40800000, LENGTH = 384k
}

ENTRY(_flash_entry)

SECTIONS
{
    /*
     *  Direct Bootマジック（+0）・エントリ（+8）・コード・rodataを
     *  単一のFLASH領域にまとめる（分岐A＝IROM/DROM分離が無い前提）。
     */
    .text : {
        KEEP(*(.flash_header))     /* +0：マジックナンバー（8バイト） */
        KEEP(*(.text.entry))       /* +8：エントリ（j start） */
        . = ALIGN(64);             /* trap_vector_tableのアライン（CLIC
                                    * non-vectoredモードは64byteアライン
                                    * 要求．C6の256byteアラインとは異なる
                                    * 点に注意．docs/c5-port-design.md
                                    * §4・chip_support.S参照） */
        *(.text*)
        /*
         *  IRAM_ATTR系関数（.iram1.N）＋Wi-Fi blob（フェーズ2b）が
         *  要求する高速実行用セクション一覧（C3・C6のesp32cN.ldと同じ
         *  一覧．明示ルールが無いとorphan section扱いとなり.dataと
         *  LMAが重なる．本ldは独立したIRAM領域を持たないDirect Boot
         *  XIP構成のため通常コードと同じくFLASHへ収容する）。
         *  docs/c5-port-design.md §6.2の指示通り，C6が後追いで気づいた
         *  教訓を踏まえ最初からC3の全セクション名一覧を含めておく。
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
         *  文字列圧縮用セクション．C3・C6のesp32cN.ldと同じ扱い）
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
         *  データ．C3・C6のesp32cN.ldと同じ扱い）
         */
        *(.dram1 .dram1.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        /*
         *  TLS(スレッドローカルストレージ)初期化イメージ。
         *  init_additional_regs_start_r（asp3/arch/riscv_gcc/esp32c5/
         *  chip_asm.inc）がタスク実行開始時にここから読み出し，
         *  タスクスタック最下位へコピーする。本ldは.dataと同じ
         *  DROM→RAMコピー機構（__idata_*）をそのまま使うため，.data
         *  出力セクションの一部として同時にコピーされる。
         */
        . = ALIGN(8);
        _thread_local_data_start = .;
        *(.tdata .tdata.*)
        _thread_local_data_end = .;
        . = ALIGN(4);
    } > RAM
    /*
     *  ROM化コピー元はFLASH側のアドレスで取る（start.Sはデータバス
     *  経由で読み出す．IROM/DROMが同一アドレス空間のためそのまま読める．
     *  ただし分岐Aの前提が崩れた場合は本節も再設計要）
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

    /*
     *  TLS(スレッドローカルストレージ)テンプレートのBSS部分の境界
     *  シンボル（サイズ計算専用．NOLOADのため内容は読まない．
     *  init_additional_regs_start_rはこのサイズ分だけタスクスタック
     *  最下位をゼロ埋めする）。
     */
    .tbss (NOLOAD) : ALIGN(8) {
        _thread_local_bss_start = .;
        *(.tbss .tbss.*)
        *(.tcommon)
        _thread_local_bss_end = .;
    } > RAM
}

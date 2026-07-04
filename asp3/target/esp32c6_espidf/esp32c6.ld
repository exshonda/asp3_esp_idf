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
    RAM(rwx)  : ORIGIN = 0x40800000, LENGTH = 448k
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
        . = ALIGN(16);
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
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

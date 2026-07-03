/*
 *  ESP32-C3用リンカスクリプト（Direct Boot・XIP実行）
 *
 *  pico2_riscv（rpi_pico2.ld）のメモリマップをESP32-C3のDirect Boot
 *  流に変更したもの（espressif/esp32c3-direct-boot-exampleのld参照）：
 *    - フラッシュはROMブートローダのMMU設定により先頭から線形に
 *      二重マップされる（命令バス=IROM 0x42000000／データバス=DROM
 *      0x3C000000）．このため全フラッシュ常駐セクションで
 *      「VMAオフセット＝フラッシュオフセット（LMAオフセット）」を
 *      維持する必要がある．
 *    - LMAはDROM基準（0x3C000000起点）とする．cfgのpass1がシンボルの
 *      VMAでsrec（LMA）を引くため，.rodataはVMA==LMAが必須．
 *    - .textはDirect Bootマジック（.flash_header）・エントリ
 *      （.text.entry＝フラッシュ先頭+8）・コード本体を単一セクションに
 *      まとめる（セクション内ではVMA/LMAオフセットが常に一致するため，
 *      trap_vector_tableの256byteアラインが入ってもXIPが破綻しない）．
 *    - .dataはDROM側のロードアドレス（__idata_*）からRAMへstart.Sが
 *      コピーする．
 *
 *  RAMはROMブートローダの作業領域（SRAM後方）を避けて320KBに制限
 *  （direct-boot-exampleと同じ）．
 */

OUTPUT_ARCH(riscv)

MEMORY
{
    IROM(rx)  : ORIGIN = 0x42000000, LENGTH = 4096k
    DROM(r)   : ORIGIN = 0x3C000000, LENGTH = 4096k
    RAM(rwx)  : ORIGIN = 0x3FC80000, LENGTH = 320k
}

ENTRY(_flash_entry)

SECTIONS
{
    /*
     *  Direct Bootマジック（+0）・エントリ（+8）・コード（IROMで
     *  XIP実行）．LMA＝DROMの先頭（フラッシュオフセット0）
     */
    .text : AT(ORIGIN(DROM)) {
        KEEP(*(.flash_header))     /* +0：マジックナンバー（8バイト） */
        KEEP(*(.text.entry))       /* +8：エントリ（j start） */
        . = ALIGN(256);            /* trap_vector_tableのアライン */
        *(.text*)
        . = ALIGN(16);
    } > IROM
    _irom_size = SIZEOF(.text);

    /*
     *  .rodataはDROM（データバス側のフラッシュマッピング）に置く．
     *  VMA＝ORIGIN(DROM)＋フラッシュオフセット＝LMA（VMA==LMA）
     */
    .rodata ORIGIN(DROM) + _irom_size : AT(ORIGIN(DROM) + _irom_size) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(16);
    } > DROM

    /*
     *  Main DATA section (RAM)
     *
     *  LMAは.rodataの直後（DROM基準）を明示する（.dataが空のときに
     *  LMAがVMA（RAM）へ落ちてフラッシュイメージが巨大化するのを
     *  防ぐ）．
     */
    .data : AT(LOADADDR(.rodata) + SIZEOF(.rodata)) ALIGN(8) {
        __data_start = .;
        LONG(0)     /* 番兵（.dataが空だとldがAT指定を無視しLMAがRAMに落ちるため） */
        *(.data*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        . = ALIGN(4);
    } > RAM
    /*
     *  ROM化コピー元はDROM側のアドレスで取る（start.Sはデータバス
     *  経由で読み出すため）．LMAをDROM基準にしているため，LOADADDRが
     *  そのままDROM側アドレスになる．
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

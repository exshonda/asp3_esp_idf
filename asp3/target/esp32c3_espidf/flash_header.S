/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ESP32-C3 Direct Bootヘッダとエントリ（pico2_riscvのimage_def.S相当）
 *
 *  フラッシュ先頭8バイトがマジックナンバー（0xAEDB041D×2）だと，ROM
 *  ブートローダはフラッシュ全体をMMUでマップ（IROM=0x42000000／
 *  DROM=0x3C000000）し，フラッシュ先頭+8（＝.text.entry）へジャンプ
 *  する（二段ブートローダ・esptoolイメージ形式は不要）．
 *  ESP32-C3 ECO3以降のROMが対応（espressif/esp32c3-direct-boot-example）．
 *
 *  エントリはアーキ共通部のstart.Sへ飛ぶだけ（gp/sp設定・bssクリア・
 *  .dataのDROM→RAMコピーはstart.Sが行う）．
 */

    .section .flash_header, "a"
    .word   0xaedb041d
    .word   0xaedb041d

    .section .text.entry, "ax"
    .option push
    .option norvc
    .global _flash_entry
_flash_entry:
    j       start
    .option pop

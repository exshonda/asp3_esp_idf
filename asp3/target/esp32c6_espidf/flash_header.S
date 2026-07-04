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
 *  ESP32-C6 Direct Bootヘッダとエントリ（esp32c3_gcc/flash_header.S
 *  と同一．実機（rev v0.2・USB Serial/JTAG）で動作確認済み＝ROMの
 *  Direct Boot機構はC3と同じマジックナンバー・同じ規約で成立する．
 *  経緯・実機ログはdocs/dev/esp32c6-target.md．
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

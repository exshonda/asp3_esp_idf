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
 *  ESP32-C5 Direct Bootヘッダとエントリ（esp32c6版asp3_target/
 *  esp32c6_espidf/flash_header.S と同一．C3/C6で動作確認済みのマジック
 *  ナンバーをそのまま使用）。
 *
 *  【実機確認待ち】docs/c5-port-design.md §8.1 7番。ASP3独自のDirect
 *  Boot規約（esptool標準image header非使用）のためチップ非依存の値の
 *  はずだが，C5では未検証。B-0の最初の実機書込みで動作確認すること。
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

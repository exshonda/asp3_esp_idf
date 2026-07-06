/*
 *  最小ベアメタル・ジャンプ先（診断専用）
 *
 *  ASP3を一切通さず，UART0（0x60000000）のTX FIFOへ'U'(0x55)を
 *  ひたすら書くだけ．ESP-IDF(c6_handoff_source)からのクロスカーネル
 *  ジャンプ（asp3_jump_now: 0x42000008へジャンプ）が制御を渡せて
 *  いるかを切り分けるための最小オラクル．
 *
 *  - 文字が出る → 制御は0x42000008に届いている＝問題はASP3の起動処理側
 *  - 無音のまま → ジャンプ/MMU再マップ/キャッシュ無効化側の問題
 *
 *  レイアウトはASP3のflash_header.Sと同一（0x42000000にマジック，
 *  0x42000008にエントリ）．UART0はESP-IDF側で初期化済みの前提で
 *  そのままFIFOに書く（ボーレート等は再設定しない）．
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

    .section .text, "ax"
start:
    li      t0, 0x60000000      /* UART0 base（UART_FIFO_REG = +0x0） */
    li      t1, 0x55            /* 'U' */
1:
    sw      t1, 0(t0)           /* TX FIFOへ1バイト書く */
    li      t2, 0x00200000      /* 遅延カウント（FIFO溢れ回避） */
2:
    addi    t2, t2, -1
    bnez    t2, 2b
    j       1b

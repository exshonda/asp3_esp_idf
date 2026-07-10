/*
 *  DIAGNOSTIC (temporary，実施16)：C5 PHY校正ハング調査専用の
 *  regi2cトランザクション（read/read_mask/write/write_mask）トレース。
 *
 *  C6（docs/wifi-shim-c6.md 実施23/39〜42）はROM常駐関数ポインタテーブル
 *  （`g_phyFuns`経由の間接呼出し）をパッチする必要があったが，C5の
 *  libphy.a（IDF v6.1／v9世代）は`phy_i2c_readReg`/`phy_i2c_writeReg`/
 *  `phy_i2c_readReg_Mask`/`phy_i2c_writeReg_Mask`が**libphy.a内の通常の
 *  大域リンケージ関数**（`nm`で他.oから`U`参照される）として実在するため，
 *  `-Wl,--wrap`で直接フックできる（実施16机上調査で確認．ROM間接テーブル
 *  パッチは不要）。
 *
 *  ゲート＝`ESP32C5_WIFI_REGI2C_TRACE`（既定OFF）。既定のC5ビルドには
 *  一切影響しない．有効化時のみ本ファイルがコンパイル・リンクされる。
 *  Not for permanent use — 調査終了後に削除可．docs/c5-bringup.md 実施16参照。
 */
#ifndef WIFI_TRACE_H
#define WIFI_TRACE_H

#include <stdint.h>

/*
 *  op: 0=write, 1=write_mask, 2=read, 3=read_mask
 *  C6のwifi_regi2c_t（wifi-shim-c6.md 実施39）とバイトレイアウトを
 *  意図的に揃えてある（JTAG mdwデコードスクリプトを流用しやすくするため）。
 */
typedef struct {
	uint32_t	t_us_low;
	uint8_t		block, host_id, reg_add, data;
	uint8_t		msb, lsb;	/* write/read（非mask）では0xFFを格納 */
	uint8_t		op;
	uint8_t		_pad;
} wifi_regi2c_t;

extern void wifi_regi2c_reset(void);
extern void wifi_regi2c_dump_count(void);

/*
 *  JTAGでの生メモリ直読み用に，配列本体・pos・エントリサイズを一度だけ
 *  syslogへ出力する（実施15/実施23と同じ流儀のアドレス確認用）。
 */
extern void wifi_regi2c_dump_addr(void);

#endif /* WIFI_TRACE_H */

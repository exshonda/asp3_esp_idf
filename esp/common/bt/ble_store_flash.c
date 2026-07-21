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
 *  BLE bond ストアの不揮発化（フラッシュ直書き・NVS 非依存）
 *
 *  ■ 何を解決するか
 *  従来 bond ストアは NimBLE の `ble_store_config`（PERSIST=0）＝**RAM 保持**で、
 *  電源断で鍵が消えていた（README「既知の制限」）。本ファイルは
 *  `ble_hs_cfg.store_{read,write,delete}_cb` を差し替え、フラッシュ末尾の
 *  予約領域へ直接保存する。
 *
 *  ■ なぜ ESP-IDF の NVS を使わないか（設計判断）
 *  本リポジトリのブートは **Direct Boot**（2段ブートローダを使わない）であり、
 *  **パーティションテーブルが存在しない**。NVS（`nvs_flash`）は
 *  `esp_partition` 経由でパーティションを引く前提で、テーブルを後付けしても
 *  それを読む主体（bootloader）が居ない。自前で読む実装を書くくらいなら、
 *  保存対象が **固定長・最大3件** である本用途には直接書き込みの方が
 *  単純で依存も軽い（ROM の `esp_rom_spiflash_*` のみ）。
 *  `nvs_flash` は C++＋ヒープ多用で、hal 撤廃で減らした複雑さを戻すことになる。
 *  （検討の経緯＝`.steering/20260721-docs-onboarding/evidence-04-bond-nvs.md`）
 *
 *  ■ 禁則との整合
 *  **動的メモリ確保をしない**。保存領域は静的配列（`.bss`）で、
 *  1レコード＝`union ble_store_value`（固定長）。カーネル外（アプリ/ライブラリ層）で
 *  完結し、カーネルには一切触れない。
 *
 *  ■ フラッシュ配置（Direct Boot と衝突しないこと）
 *  予約＝**フラッシュ末尾 64KB**（`ASP3_BLE_STORE_FLASH_OFFSET`．既定 0x3F0000）。
 *  実測：アプリの実データ末尾は約 0x03dc48（≒253KB）で、以降 0x400000 まで
 *  0xff（未使用）。末尾に置くことで、将来アプリが太っても衝突しにくい。
 *  ★書込みは **セクタ単位（4KB）の消去→書込み**。ROM API はセクタ消去しか
 *  持たないため、1セクタに全レコードを収めて **read-modify-write** する
 *  （レコード総量は下記のとおり 1セクタに十分収まる）。
 *
 *  ■ 耐久性についての正直な注記
 *  ウェアレベリングは**しない**。bond の書込みは「ペアリング成立時」など
 *  稀なイベントに限られ（毎起動でも毎接続でもない）、フラッシュの
 *  消去回数上限（一般に10万回以上）に対して桁違いに少ないため。
 *  高頻度書込みが要るデータには本実装を使わないこと。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "host/ble_hs.h"
#include "host/ble_store.h"

/*
 *  予約領域（ビルド時に -D で上書き可）。既定＝4MB フラッシュの末尾 64KB。
 */
#ifndef ASP3_BLE_STORE_FLASH_OFFSET
#define ASP3_BLE_STORE_FLASH_OFFSET		0x3F0000U
#endif /* ASP3_BLE_STORE_FLASH_OFFSET */

#define ASP3_BLE_STORE_SECTOR_SIZE		0x1000U		/* ROM API の消去単位 */

/*
 *  レコード数。NimBLE の設定（CONFIG_BT_NIMBLE_MAX_BONDS＝3）に合わせる。
 *  CCCD は bond ごとに複数持ちうるため別枠で確保する。
 */
#define ASP3_BLE_STORE_MAX_SEC			(MYNEWT_VAL(BLE_STORE_MAX_BONDS))
#define ASP3_BLE_STORE_MAX_CCCD			(MYNEWT_VAL(BLE_STORE_MAX_CCCDS))

#define ASP3_BLE_STORE_MAGIC			0x42535331U	/* "BSS1" */

/*
 *  フラッシュ上の像。**固定長**（動的確保なし）。
 *  `valid` は「そのスロットが使用中か」。削除は valid=0 にして書き戻す。
 */
struct asp3_ble_store_image {
	uint32_t	magic;
	uint32_t	version;
	uint32_t	n_sec;
	uint32_t	n_cccd;
	struct {
		uint8_t					valid;
		struct ble_store_value_sec	val;
	} sec[ASP3_BLE_STORE_MAX_SEC * 2];		/* our_sec ＋ peer_sec */
	struct {
		uint8_t					valid;
		struct ble_store_value_cccd	val;
	} cccd[ASP3_BLE_STORE_MAX_CCCD];
	uint32_t	crc;
};

/*  RAM 上の作業像（.bss＝静的．書込み時にこれをそのままフラッシュへ）  */
static struct asp3_ble_store_image	store_image;
static bool_t						store_loaded;

/*
 *  ROM 提供の SPI flash API（3チップとも <chip>.rom.ld が供給することを実測確認）。
 *  ヘッダを引くと esp_flash/spi_flash 一式への依存が増えるため、必要な3つだけ
 *  自前で extern 宣言する（シグネチャは ESP-IDF の esp_rom/include/esp32cX/rom/spi_flash.h に一致）。
 */
extern int esp_rom_spiflash_read(uint32_t src_addr, uint32_t *dest, int32_t len);
extern int esp_rom_spiflash_write(uint32_t dest_addr, const uint32_t *src, int32_t len);
extern int esp_rom_spiflash_erase_sector(uint32_t sector_num);
extern int esp_rom_spiflash_unlock(void);

/*
 *  単純な加算 CRC（改竄検知ではなく「消去済み/中途半端な書込み」の検出が目的）。
 */
static uint32_t
store_crc(const struct asp3_ble_store_image *img)
{
	const uint8_t	*p = (const uint8_t *) img;
	size_t			n = offsetof(struct asp3_ble_store_image, crc);
	uint32_t		c = 0U;
	size_t			i;

	for (i = 0U; i < n; i++) {
		c = (c << 1) ^ (c >> 31) ^ (uint32_t) p[i];
	}
	return(c);
}

static void
store_load(void)
{
	if (store_loaded) {
		return;
	}
	store_loaded = true;

	(void) esp_rom_spiflash_read(ASP3_BLE_STORE_FLASH_OFFSET,
								 (uint32_t *) &store_image,
								 (int32_t) sizeof(store_image));

	if ((store_image.magic != ASP3_BLE_STORE_MAGIC)
			|| (store_image.crc != store_crc(&store_image))) {
		/*
		 *  未初期化（消去状態＝全 0xff）または壊れている。空で始める。
		 *  ★ここで syslog に出すのは「鍵が消えた」ことを黙って進めないため
		 *  （bond が復元されない事象をデバッグ可能にする）。
		 */
		if (store_image.magic != 0xFFFFFFFFU) {
			syslog(LOG_WARNING,
				   "ble_store_flash: image invalid (magic=0x%08x) -> start empty",
				   store_image.magic);
		}
		memset(&store_image, 0, sizeof(store_image));
		store_image.magic = ASP3_BLE_STORE_MAGIC;
		store_image.version = 1U;
	}
}

static int
store_commit(void)
{
	int		rc;

	store_image.crc = store_crc(&store_image);

	(void) esp_rom_spiflash_unlock();
	rc = esp_rom_spiflash_erase_sector(ASP3_BLE_STORE_FLASH_OFFSET
										/ ASP3_BLE_STORE_SECTOR_SIZE);
	if (rc != 0) {
		syslog(LOG_ERROR, "ble_store_flash: erase failed (%d)", rc);
		return(BLE_HS_ESTORE_FAIL);
	}
	rc = esp_rom_spiflash_write(ASP3_BLE_STORE_FLASH_OFFSET,
								(const uint32_t *) &store_image,
								(int32_t) sizeof(store_image));
	if (rc != 0) {
		syslog(LOG_ERROR, "ble_store_flash: write failed (%d)", rc);
		return(BLE_HS_ESTORE_FAIL);
	}
	return(0);
}

static bool_t
addr_eq(const ble_addr_t *a, const ble_addr_t *b)
{
	return((bool_t) ((a->type == b->type)
					 && (memcmp(a->val, b->val, 6) == 0)));
}

/*
 *  ------------------------------------------------------------------
 *  NimBLE store コールバック
 *  ------------------------------------------------------------------
 */
static int
store_read(int obj_type, const union ble_store_key *key,
		   union ble_store_value *value)
{
	int		i;
	int		idx = 0;

	store_load();

	switch (obj_type) {
	case BLE_STORE_OBJ_TYPE_OUR_SEC:
	case BLE_STORE_OBJ_TYPE_PEER_SEC:
		for (i = 0; i < (int) (sizeof(store_image.sec) / sizeof(store_image.sec[0])); i++) {
			if (store_image.sec[i].valid == 0U) {
				continue;
			}
			/*  peer_addr 指定があれば一致を要求．無ければ idx 番目を返す  */
			if (ble_addr_cmp(&key->sec.peer_addr, BLE_ADDR_ANY) != 0) {
				if (!addr_eq(&store_image.sec[i].val.peer_addr,
							 &key->sec.peer_addr)) {
					continue;
				}
			}
			if (idx < key->sec.idx) {
				idx++;
				continue;
			}
			value->sec = store_image.sec[i].val;
			return(0);
		}
		return(BLE_HS_ENOENT);

	case BLE_STORE_OBJ_TYPE_CCCD:
		for (i = 0; i < (int) (sizeof(store_image.cccd) / sizeof(store_image.cccd[0])); i++) {
			if (store_image.cccd[i].valid == 0U) {
				continue;
			}
			if (ble_addr_cmp(&key->cccd.peer_addr, BLE_ADDR_ANY) != 0) {
				if (!addr_eq(&store_image.cccd[i].val.peer_addr,
							 &key->cccd.peer_addr)) {
					continue;
				}
			}
			if ((key->cccd.chr_val_handle != 0)
					&& (store_image.cccd[i].val.chr_val_handle
						!= key->cccd.chr_val_handle)) {
				continue;
			}
			if (idx < key->cccd.idx) {
				idx++;
				continue;
			}
			value->cccd = store_image.cccd[i].val;
			return(0);
		}
		return(BLE_HS_ENOENT);

	default:
		return(BLE_HS_ENOTSUP);
	}
}

static int
store_write(int obj_type, const union ble_store_value *val)
{
	int		i;
	int		free_idx = -1;

	store_load();

	switch (obj_type) {
	case BLE_STORE_OBJ_TYPE_OUR_SEC:
	case BLE_STORE_OBJ_TYPE_PEER_SEC:
		/*  同一 peer は上書き．無ければ空きへ  */
		for (i = 0; i < (int) (sizeof(store_image.sec) / sizeof(store_image.sec[0])); i++) {
			if (store_image.sec[i].valid == 0U) {
				if (free_idx < 0) {
					free_idx = i;
				}
				continue;
			}
			if (addr_eq(&store_image.sec[i].val.peer_addr, &val->sec.peer_addr)) {
				store_image.sec[i].val = val->sec;
				return(store_commit());
			}
		}
		if (free_idx < 0) {
			return(BLE_HS_ESTORE_CAP);
		}
		store_image.sec[free_idx].valid = 1U;
		store_image.sec[free_idx].val = val->sec;
		return(store_commit());

	case BLE_STORE_OBJ_TYPE_CCCD:
		for (i = 0; i < (int) (sizeof(store_image.cccd) / sizeof(store_image.cccd[0])); i++) {
			if (store_image.cccd[i].valid == 0U) {
				if (free_idx < 0) {
					free_idx = i;
				}
				continue;
			}
			if (addr_eq(&store_image.cccd[i].val.peer_addr, &val->cccd.peer_addr)
					&& (store_image.cccd[i].val.chr_val_handle
						== val->cccd.chr_val_handle)) {
				store_image.cccd[i].val = val->cccd;
				return(store_commit());
			}
		}
		if (free_idx < 0) {
			return(BLE_HS_ESTORE_CAP);
		}
		store_image.cccd[free_idx].valid = 1U;
		store_image.cccd[free_idx].val = val->cccd;
		return(store_commit());

	default:
		return(BLE_HS_ENOTSUP);
	}
}

static int
store_delete(int obj_type, const union ble_store_key *key)
{
	int		i;
	bool_t	hit = false;

	store_load();

	switch (obj_type) {
	case BLE_STORE_OBJ_TYPE_OUR_SEC:
	case BLE_STORE_OBJ_TYPE_PEER_SEC:
		for (i = 0; i < (int) (sizeof(store_image.sec) / sizeof(store_image.sec[0])); i++) {
			if ((store_image.sec[i].valid != 0U)
					&& addr_eq(&store_image.sec[i].val.peer_addr,
							   &key->sec.peer_addr)) {
				store_image.sec[i].valid = 0U;
				memset(&store_image.sec[i].val, 0, sizeof(store_image.sec[i].val));
				hit = true;
			}
		}
		break;

	case BLE_STORE_OBJ_TYPE_CCCD:
		for (i = 0; i < (int) (sizeof(store_image.cccd) / sizeof(store_image.cccd[0])); i++) {
			if ((store_image.cccd[i].valid != 0U)
					&& addr_eq(&store_image.cccd[i].val.peer_addr,
							   &key->cccd.peer_addr)) {
				store_image.cccd[i].valid = 0U;
				memset(&store_image.cccd[i].val, 0, sizeof(store_image.cccd[i].val));
				hit = true;
			}
		}
		break;

	default:
		return(BLE_HS_ENOTSUP);
	}

	if (!hit) {
		return(BLE_HS_ENOENT);
	}
	return(store_commit());
}

/*
 *  初期化：`ble_store_config_init()` の代わりに呼ぶ（アプリの on_sync 前）。
 */
void
asp3_ble_store_flash_init(void)
{
	store_load();

	ble_hs_cfg.store_read_cb = store_read;
	ble_hs_cfg.store_write_cb = store_write;
	ble_hs_cfg.store_delete_cb = store_delete;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

	syslog(LOG_NOTICE,
		   "ble_store_flash: persistent bond store @0x%06x (image %u bytes)",
		   (unsigned int) ASP3_BLE_STORE_FLASH_OFFSET,
		   (unsigned int) sizeof(store_image));
}

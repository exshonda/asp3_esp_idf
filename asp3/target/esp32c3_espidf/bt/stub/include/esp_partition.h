/*
 *  esp_partition.h コンパイル用スタブ（Bluetooth統合．Phase D-1）
 *
 *  Wi-Fi shimのNVSスタブ（毎回起動時に全較正・永続化なし）と同じ方針：
 *  パーティションは常に「存在しない」として扱う．bt.cのパーティション
 *  参照はいずれもCONFIG_BT_CTRL_LE_LOG_STORAGE_EN（本ビルドでは未定義
 *  ＝0）配下でのみ実行されるため，実体（esp_shim_blobglue.c等）は
 *  find_firstがNULLを返す最小実装で足りる．
 */
#ifndef TOPPERS_BT_STUB_ESP_PARTITION_H
#define TOPPERS_BT_STUB_ESP_PARTITION_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

typedef enum {
	ESP_PARTITION_TYPE_APP  = 0x00,
	ESP_PARTITION_TYPE_DATA = 0x01,
	ESP_PARTITION_TYPE_ANY  = 0xff,
} esp_partition_type_t;

typedef enum {
	ESP_PARTITION_SUBTYPE_ANY = 0xff,
} esp_partition_subtype_t;

typedef enum {
	ESP_PARTITION_MMAP_DATA,
	ESP_PARTITION_MMAP_INST,
} esp_partition_mmap_memory_t;

typedef uint32_t esp_partition_mmap_handle_t;

typedef struct {
	esp_partition_type_t	type;
	esp_partition_subtype_t	subtype;
	uint32_t				address;
	uint32_t				size;
	char					label[17];
	bool					encrypted;
} esp_partition_t;

#ifndef TOPPERS_MACRO_ONLY
extern const esp_partition_t *esp_partition_find_first(
		esp_partition_type_t type, esp_partition_subtype_t subtype,
		const char *label);
extern esp_err_t esp_partition_erase_range(const esp_partition_t *partition,
											uint32_t offset, uint32_t size);
extern esp_err_t esp_partition_write(const esp_partition_t *partition,
									  uint32_t dst_offset, const void *src,
									  uint32_t size);
extern esp_err_t esp_partition_mmap(const esp_partition_t *partition,
									 uint32_t offset, uint32_t size,
									 esp_partition_mmap_memory_t memory,
									 const void **out_ptr,
									 esp_partition_mmap_handle_t *out_handle);
extern esp_err_t esp_partition_munmap(esp_partition_mmap_handle_t handle);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_STUB_ESP_PARTITION_H */

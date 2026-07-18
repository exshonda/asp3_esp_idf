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
 *  ESP32-C5 seam（実ESP-IDF 2nd-stage bootloader経由起動）用 esp_app_desc
 *
 *  本ファイルは ASP3_SEAM_BOOT=ON のときだけビルドに加わる（既定 OFF＝
 *  Direct Boot では未参照・未リンク＝非回帰）。
 *
 *  ── なぜ必要か（実測に基づく．S3(LX7)の先行事例と同型）──
 *
 *  ESP-IDF の 2nd-stage bootloader は，アプリイメージの **segment #0 の
 *  先頭を esp_app_desc_t として読み**，min/max_efuse_blk_rev_full で
 *  efuseブロックリビジョンを検証する：
 *
 *      esp_image_format.c:769-778
 *          if (segment == 0 && !is_bootloader(metadata->start_addr)) {
 *      #if !CONFIG_IDF_TARGET_ESP32                  ← C5は「esp32 classic」
 *              const esp_app_desc_t *app_desc = (const esp_app_desc_t *)src;
 *              ... bootloader_common_check_efuse_blk_validity(
 *                      app_desc->min_efuse_blk_rev_full,
 *                      app_desc->max_efuse_blk_rev_full);
 *
 *  `#if !CONFIG_IDF_TARGET_ESP32` のため esp32(classic/LX6)だけがこの検証を
 *  skipし，C3/C5/C6・S3 は **検証を受ける**（PROMPT.md の賭けは当たり＝
 *  ソース実測で確認）。有効な app_desc が無いと garbage が efuse要件として
 *  読まれ「Image requires efuse blk rev >= vX.Y, but chip is v1.4」の
 *  ような矛盾値で `No bootable app partitions` になる（S3で実際に発生）。
 *
 *  値の意味：
 *    min_efuse_blk_rev_full = 0      → IS_FIELD_SET(0) が偽＝minチェックskip
 *                                      （bootloader_common_loader.c:104）
 *    max_efuse_blk_rev_full = 0xFFFF → revision(実測数百) <= 65535 で必ずpass
 *                                      （同 :110）
 *    mmu_page_size          = 16     → log2(64KB)。C5は
 *                                      SOC_MMU_PAGE_SIZE_CONFIGURABLE 非定義
 *                                      のため実際には読まれない
 *                                      （esp_image_format.c:835 の #if）が，
 *                                      本家と整合する値を入れておく。
 *
 *  esp_app_desc.h を include せずローカル定義するのは，本ファイルが
 *  esp_app_format コンポーネントのインクルードパス（WiFi/BT OFFの素の
 *  構成では積まれない）に依存しないようにするため。レイアウトは
 *  esp-idf v5.5.4 の components/esp_app_format/include/esp_app_desc.h と
 *  1:1（_Static_assert で 256B・secure_version offset を機械照合する）。
 */

#include <stdint.h>
#include <stddef.h>

#define ASP3_SEAM_APP_DESC_MAGIC_WORD   0xABCD5432

typedef struct {
    uint32_t magic_word;             /* ESP_APP_DESC_MAGIC_WORD */
    uint32_t secure_version;
    uint32_t reserv1[2];
    char     version[32];
    char     project_name[32];
    char     time[16];
    char     date[16];
    char     idf_ver[32];
    uint8_t  app_elf_sha256[32];
    uint16_t min_efuse_blk_rev_full;
    uint16_t max_efuse_blk_rev_full;
    uint8_t  mmu_page_size;          /* log base 2 */
    uint8_t  reserv3[3];
    uint32_t reserv2[18];
} asp3_seam_app_desc_t;

/*
 *  本家 esp_app_desc.h の ESP_STATIC_ASSERT と同一の機械照合
 */
_Static_assert(sizeof(asp3_seam_app_desc_t) == 256,
               "asp3_seam_app_desc_t should be 256 bytes");
_Static_assert(offsetof(asp3_seam_app_desc_t, secure_version) == 4,
               "secure_version field must be at 4 offset");
/*
 *  4+4+8+32+32+16+16+32+32 = 176（magic/secure_version/reserv1/version/
 *  project_name/time/date/idf_ver/app_elf_sha256）。S3のHANDOFFは
 *  「offset 92/94付近」と記していたが実測は 176/178＝**引き継ぎ記述は誤り**。
 *  この _Static_assert は実際にその誤りを検出した（最初 188 と書いて失敗）。
 */
_Static_assert(offsetof(asp3_seam_app_desc_t, min_efuse_blk_rev_full) == 176,
               "min_efuse_blk_rev_full offset must match esp-idf v5.5.4");
_Static_assert(offsetof(asp3_seam_app_desc_t, max_efuse_blk_rev_full) == 178,
               "max_efuse_blk_rev_full offset must match esp-idf v5.5.4");
_Static_assert(offsetof(asp3_seam_app_desc_t, mmu_page_size) == 180,
               "mmu_page_size offset must match esp-idf v5.5.4");

/*
 *  `.rodata_desc` は esp32c5_seam.ld の .flash.appdesc 先頭へ KEEP される
 *  （本家 sections.ld.in の .flash.appdesc と同じセクション名を使う）。
 */
__attribute__((section(".rodata_desc"), used))
const asp3_seam_app_desc_t asp3_seam_app_desc = {
    .magic_word             = ASP3_SEAM_APP_DESC_MAGIC_WORD,
    .secure_version         = 0,
    .version                = "asp3-seam",
    .project_name           = "asp3_esp32c5",
    .time                   = __TIME__,
    .date                   = __DATE__,
    .idf_ver                = "v5.5.4",
    .min_efuse_blk_rev_full = 0,       /* minチェックskip */
    .max_efuse_blk_rev_full = 0xFFFF,  /* 必ずpass */
    .mmu_page_size          = 16,      /* log2(64KB) */
};

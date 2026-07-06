/*
 *  Step 0（遅いハンドオフ再現テスト）のジャンプ元：ネイティブESP-IDF
 *  （shim無し・Arduino代替のground truth側）でWi-Fiスキャンを完走させ，
 *  成功した直後にASP3(wifi_scan)へジャンプする．
 *
 *  tmp/c6_wifi_arduino_handoff_strategy.md Step0／
 *  docs/wifi-shim-c6.md 実施33参照．
 */
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "asp3_jump.h"

static const char *TAG = "c6_handoff_source";

/*
 *  GROUND-TRUTH DIAGNOSTIC：ネイティブESP-IDFのosi呼び出し頻度を採取する．
 *  リンカ --wrap で FreeRTOS プリミティブを横取りしてカウント（IDF無改変）．
 *  WiFiタスク主ループが叩く xQueueReceive の頻度が「正常値」．ASP3 shimの
 *  388Hzと比較して，どのosi呼び出しが過剰かを特定する足がかりにする．
 */
#include "freertos/queue.h"
#include "esp_timer.h"
extern BaseType_t __real_xQueueReceive(QueueHandle_t q, void *buf, TickType_t t);
extern BaseType_t __real_xQueueGenericSend(QueueHandle_t q, const void *buf,
										   TickType_t t, BaseType_t pos);
extern BaseType_t __real_xQueueSemaphoreTake(QueueHandle_t q, TickType_t t);
volatile uint32_t g_gt_qrecv = 0;
volatile uint32_t g_gt_qsend = 0;
volatile uint32_t g_gt_semtake = 0;
BaseType_t __wrap_xQueueReceive(QueueHandle_t q, void *buf, TickType_t t)
{
	g_gt_qrecv++;
	return __real_xQueueReceive(q, buf, t);
}
BaseType_t __wrap_xQueueGenericSend(QueueHandle_t q, const void *buf,
									TickType_t t, BaseType_t pos)
{
	g_gt_qsend++;
	return __real_xQueueGenericSend(q, buf, t, pos);
}
BaseType_t __wrap_xQueueSemaphoreTake(QueueHandle_t q, TickType_t t)
{
	g_gt_semtake++;
	return __real_xQueueSemaphoreTake(q, t);
}

void
app_main(void)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	uint16_t			num = 20;
	wifi_ap_record_t	recs[20];
	esp_err_t			err;

#ifdef LOADER_NO_WIFI
	/*
	 *  ローダモード（option2）：ESP-IDFの2段ブートローダで起動した後，
	 *  Wi-Fiを一切初期化せずに（＝blobのグローバル/ROM状態を汚さず）
	 *  即ASP3へジャンプする．ASP3側が自前でフレッシュにesp_wifi_initする
	 *  ため，ハンドオフ時のESP-IDF残留osiポインタ問題を回避できる．
	 *  Direct Bootが起動しなくなった現状の代替経路（2段ブートローダは
	 *  確実に動く）．
	 */
	ESP_LOGI(TAG, "LOADER_NO_WIFI: jumping to ASP3 at 0x200000 (no Wi-Fi init)");
	(void) cfg; (void) num; (void) recs; (void) err;
	(void) esp_task_wdt_deinit();
	vTaskDelay(pdMS_TO_TICKS(300));
	asp3_jump_now();	/* MMU remap 0x42000000<-0x200000 + jump; disables SWD */
	for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
#endif /* LOADER_NO_WIFI */

#ifdef GPIO_SELFTEST
	/*
	 *  DIAGNOSTIC 自己診断：D0/D1/D2(GPIO0/1/2)を出力に設定し，
	 *  ASP3側diag_markと同じ生W1TS/W1TCレジスタ書き込みで 000→111 を
	 *  巡回させる（各0.5秒）．Logic8/テスターでD0/D1/D2が全て正しく
	 *  トグルするか確認し，チェックポイント読み出し機構を検証する．
	 */
	{
		gpio_config_t io = {
			.pin_bit_mask = (1ULL << 0) | (1ULL << 1) | (1ULL << 2),
			.mode = GPIO_MODE_OUTPUT,
			.pull_up_en = GPIO_PULLUP_DISABLE,
			.pull_down_en = GPIO_PULLDOWN_DISABLE,
			.intr_type = GPIO_INTR_DISABLE,
		};
		unsigned int v = 0U;
		gpio_config(&io);
		ESP_LOGI(TAG, "GPIO_SELFTEST: cycling D0/D1/D2 through 0..7 (0.5s each)");
		while (1) {
			*(volatile uint32_t *)0x6009100CU = 0x7U;		/* W1TC: clear D0/D1/D2 */
			*(volatile uint32_t *)0x60091008U = (v & 0x7U);	/* W1TS: set value */
			ESP_LOGI(TAG, "GPIO_SELFTEST: value=%u (D2=%u D1=%u D0=%u)",
					 v, (v >> 2) & 1U, (v >> 1) & 1U, v & 1U);
			vTaskDelay(pdMS_TO_TICKS(500));
			v = (v + 1U) & 0x7U;
		}
	}
#endif /* GPIO_SELFTEST */

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	/* GROUND-TRUTH: アイドル2秒間のosi呼び出し頻度（基準線） */
	{
		uint32_t r0 = g_gt_qrecv, s0 = g_gt_qsend, m0 = g_gt_semtake;
		vTaskDelay(pdMS_TO_TICKS(2000));
		ESP_LOGW(TAG, "GT idle 2s: qRecv=%.0f/s qSend=%.0f/s semTake=%.0f/s",
				 (g_gt_qrecv - r0) / 2.0, (g_gt_qsend - s0) / 2.0,
				 (g_gt_semtake - m0) / 2.0);
	}

	ESP_LOGI(TAG, "esp_wifi_scan_start (blocking)");
	{
		uint32_t rb = g_gt_qrecv, sb = g_gt_qsend, mb = g_gt_semtake;
		int64_t t0 = esp_timer_get_time();
		err = esp_wifi_scan_start(NULL, true);	/* true=完走まで自タスクをブロック */
		int64_t t1 = esp_timer_get_time();
		double sec = (double)(t1 - t0) / 1e6;
		if (sec <= 0) sec = 1e-6;
		ESP_LOGW(TAG, "GT scan %.2fs: qRecv=%.0f/s qSend=%.0f/s semTake=%.0f/s",
				 sec, (g_gt_qrecv - rb) / sec, (g_gt_qsend - sb) / sec,
				 (g_gt_semtake - mb) / sec);
	}
	ESP_LOGI(TAG, "esp_wifi_scan_start -> %d", (int) err);

	err = esp_wifi_scan_get_ap_records(&num, recs);
	ESP_LOGI(TAG, "%d APs found (err=%d)", (int) num, (int) err);
	for (int i = 0; i < num; i++) {
		ESP_LOGI(TAG, "  [%d] %s (rssi=%d ch=%d)",
				 i, (const char *) recs[i].ssid,
				 (int) recs[i].rssi, (int) recs[i].primary);
	}

	if (num == 0) {
		ESP_LOGE(TAG, "scan found 0 APs -- Step0 precondition (scan success) "
				 "not met, aborting jump");
		for (;;) {
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}

	/*  GROUND-TRUTH（ASP3との modem/PHY レジスタ差分取得・一時的）：
	 *  ジャンプせず，受信できるネイティブESP-IDFのまま再スキャンし続ける．
	 *  JTAGでいつhaltしても「受信できている版」のレジスタ状態を読める． */
#if 1
	ESP_LOGI(TAG, "GT-REGDIFF: rescanning forever (no jump) for JTAG register comparison");
	for (;;) {
		err = esp_wifi_scan_start(NULL, true);
		num = 20;
		err = esp_wifi_scan_get_ap_records(&num, recs);
		{
			/*  追記12/16：ASP3と同じRF較正regi2c読み戻し→RTC[16..](0x50000040)．
			 *  g_phyFuns経由・ROM固定関数とも全0x77＝IDFはregi2cバスを
			 *  トランザクション毎にrefcountでenable/disableするため素の呼出し
			 *  ではクロック窓外＝ゴミ．IDF正規経路 regi2c_ctrl_read_reg_mask
			 *  （enable/クリティカルセクション込み・native ELFにリンク済み）を使う． */
			extern uint8_t regi2c_ctrl_read_reg_mask(uint8_t block, uint8_t host_id,
													 uint8_t reg_add, uint8_t msb, uint8_t lsb);
			volatile uint8_t *out = (volatile uint8_t *)0x50000040U;
			static const uint8_t blk[4] = {0x6b, 0x6a, 0x66, 0x6d};
			static const uint8_t hst[4] = {1, 1, 0, 1};
			int bi, r, o = 0;
			for (bi = 0; bi < 4; bi++) {
				if (blk[bi] == 0x6b) {
					/*  0x6b(RF)はIDF regi2c_ctrlのenable_block switch対象外．
					 *  テーブル照合の結果 idx23=0x4000412c は両ビルド同一の
					 *  ROM関数＝失敗要因は関数でなくバス状態．ANA_CONF2の
					 *  bit13-16(ASP3=set/native=clear＝IDFドライバがclearする
					 *  ROMデフォルト)を一時セットして g_phyFuns[23] で読む． */
					extern uint32_t *g_phyFuns;
					uint8_t (*rdm)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) =
						(uint8_t (*)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t))
							(uintptr_t)g_phyFuns[23];
					volatile uint32_t *conf2 = (volatile uint32_t *)0x600AF820U;
					uint32_t save = *conf2;
					*conf2 = save | 0x0001e000U;	/* bit13-16をASP3同等に */
					for (r = 0; r < 16; r++) {
						out[o++] = rdm(0x6b, hst[bi], (uint8_t) r, 7, 0);
					}
					*conf2 = save;
					continue;
				}
				for (r = 0; r < 16; r++) {
					out[o++] = regi2c_ctrl_read_reg_mask(blk[bi], hst[bi],
														 (uint8_t) r, 7, 0);
				}
			}
		}
		ESP_LOGI(TAG, "GT-REGDIFF rescan: %d APs (err=%d)", (int) num, (int) err);
	}
#endif

	ESP_LOGI(TAG, "Step0: scan succeeded (%d APs) -- jumping to ASP3 in 2s", num);
	vTaskDelay(pdMS_TO_TICKS(2000));

	/*
	 *  ハンドオフ後のASP3起動チェックポイントを保持出力するためのGPIO
	 *  （XIAO D0/D1/D2 = GPIO0/1/2）を出力=0で用意する．ここでIOMUX／
	 *  GPIOマトリクスまで正しく設定しておき，ジャンプ後のASP3側は
	 *  GPIO_OUTビットを書くだけにする．D6/D7(UART)以外は未使用なので安全．
	 */
	gpio_config_t diag_io = {
		.pin_bit_mask = (1ULL << 0) | (1ULL << 1) | (1ULL << 2),
		.mode = GPIO_MODE_OUTPUT,
		.pull_up_en = GPIO_PULLUP_DISABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};
	gpio_config(&diag_io);
	gpio_set_level(0, 0);
	gpio_set_level(1, 0);
	gpio_set_level(2, 0);
	ESP_LOGI(TAG, "diag GPIO0/1/2 (D0/D1/D2) armed = 000 (checkpoint latch for Logic8)");

	/*
	 *  DIAGNOSTIC：ハンドオフ後ASP3のstart.S bss/dataクリア（stores to
	 *  0x40800010..0x40803230）でハングする．ESP-IDFのPMP設定が当該RAMを
	 *  M-modeでも書込み不可（ロック付き読取専用）に保護している疑いの検証．
	 *  ジャンプ前（ESP-IDF文脈・ログ可能）にPMP CSRをダンプする．
	 */
	{
		uint32_t cfg[4], addr[16];
		__asm__ volatile("csrr %0, 0x3a0" : "=r"(cfg[0]));
		__asm__ volatile("csrr %0, 0x3a1" : "=r"(cfg[1]));
		__asm__ volatile("csrr %0, 0x3a2" : "=r"(cfg[2]));
		__asm__ volatile("csrr %0, 0x3a3" : "=r"(cfg[3]));
		__asm__ volatile("csrr %0, 0x3b0" : "=r"(addr[0]));
		__asm__ volatile("csrr %0, 0x3b1" : "=r"(addr[1]));
		__asm__ volatile("csrr %0, 0x3b2" : "=r"(addr[2]));
		__asm__ volatile("csrr %0, 0x3b3" : "=r"(addr[3]));
		__asm__ volatile("csrr %0, 0x3b4" : "=r"(addr[4]));
		__asm__ volatile("csrr %0, 0x3b5" : "=r"(addr[5]));
		__asm__ volatile("csrr %0, 0x3b6" : "=r"(addr[6]));
		__asm__ volatile("csrr %0, 0x3b7" : "=r"(addr[7]));
		__asm__ volatile("csrr %0, 0x3b8" : "=r"(addr[8]));
		__asm__ volatile("csrr %0, 0x3b9" : "=r"(addr[9]));
		__asm__ volatile("csrr %0, 0x3ba" : "=r"(addr[10]));
		__asm__ volatile("csrr %0, 0x3bb" : "=r"(addr[11]));
		__asm__ volatile("csrr %0, 0x3bc" : "=r"(addr[12]));
		__asm__ volatile("csrr %0, 0x3bd" : "=r"(addr[13]));
		__asm__ volatile("csrr %0, 0x3be" : "=r"(addr[14]));
		__asm__ volatile("csrr %0, 0x3bf" : "=r"(addr[15]));
		ESP_LOGI(TAG, "PMP cfg0=%08lx cfg1=%08lx cfg2=%08lx cfg3=%08lx",
				 (unsigned long)cfg[0], (unsigned long)cfg[1],
				 (unsigned long)cfg[2], (unsigned long)cfg[3]);
		for (int k = 0; k < 16; k++) {
			ESP_LOGI(TAG, "PMP addr%-2d=%08lx (region top ~%08lx)",
					 k, (unsigned long)addr[k], (unsigned long)(addr[k] << 2));
		}
		ESP_LOGI(TAG, "ASP3 bss=0x40800010..0x40803230 (target of the failing store)");
	}

	ESP_LOGI(TAG, "disabling task WDT before jump (defensive; rule out WDT-induced reboot confound)");
	(void) esp_task_wdt_deinit();
	ESP_LOGI(TAG, "asp3_jump_now: jumping now");
	asp3_jump_now();

	/*  到達しない */
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

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
#include "asp3_jump.h"

static const char *TAG = "c6_handoff_source";

void
app_main(void)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	uint16_t			num = 20;
	wifi_ap_record_t	recs[20];
	esp_err_t			err;

	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_sta();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "esp_wifi_scan_start (blocking)");
	err = esp_wifi_scan_start(NULL, true);	/* true=完走まで自タスクをブロック */
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

	ESP_LOGI(TAG, "Step0: scan succeeded (%d APs) -- jumping to ASP3 in 2s", num);
	vTaskDelay(pdMS_TO_TICKS(2000));
	ESP_LOGI(TAG, "disabling task WDT before jump (defensive; rule out WDT-induced reboot confound)");
	(void) esp_task_wdt_deinit();
	ESP_LOGI(TAG, "asp3_jump_now: jumping now");
	asp3_jump_now();

	/*  到達しない */
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

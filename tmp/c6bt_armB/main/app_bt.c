/*
 *  c6bt_stock: C6 BT «クロスカーネル・ハンドオフ» Milestone 0 + Arm A/B。
 *
 *  stock ESP-IDF v6.1（controller-only, raw VHCI HCI. NimBLE/Bluedroid
 *  ホスト無し）で BT controller を正規に enable し，PLL 較正
 *  （0x600a00cc bit8 assert）を確立してから adv する。ground-truth：
 *  IDF が完全な pmu_init/PHY 較正を経て BT PLL をロックできることを
 *  この基板上で実証する（タスク前提の検証・Milestone 0）。
 *
 *  ビルドフラグ：
 *    （既定）      Milestone 0：init+enable+adv のみ。ジャンプしない。
 *    ARM_B_NOBT_JUMP：BT を一切初期化せず，起動後すぐ ASP3 へジャンプ
 *                    （control：ブートローダ pmu_init 単体の寄与を見る）。
 *    ARM_A_JUMP    ：BT init+enable+adv 確立（bit8 assert 確認）後，
 *                    ASP3 へジャンプ（PLL ロック継承の本実験）。
 *
 *  ベースは esp-idf-v6.1 examples/bluetooth/hci/controller_vhci_ble_adv
 *  （Unlicense/CC0）。docs/wifi-shim-c6.md 実施33 の asp3_jump_now() 形状
 *  を asp3_jump.c として同梱（c6_handoff_source からそのまま移植）。
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "bt_hci_common.h"
#include "asp3_jump.h"

static const char *TAG = "c6bt_stock";

/*
 *  synth-lock ステータスレジスタ（docs/ble-c5c6-plan.md §17.3 で
 *  ram_set_chan_freq_sw_start が polling すると逆アセンブルで確認済み）。
 *  bit8(0x100) が assert されると synth PLL ロック済み。
 */
#define SYNTH_LOCK_REG   0x600a00ccUL
#define SYNTH_LOCK_BIT8  0x100UL

static inline uint32_t synth_lock_read(void)
{
	return *(volatile uint32_t *) SYNTH_LOCK_REG;
}

static uint8_t hci_cmd_buf[128];

static void controller_rcv_pkt_ready(void)
{
	ESP_LOGI(TAG, "controller rcv pkt ready");
}

static int host_rcv_pkt(uint8_t *data, uint16_t len)
{
	printf("host rcv pkt: ");
	for (uint16_t i = 0; i < len; i++) {
		printf("%02x", data[i]);
	}
	printf("\n");
	return 0;
}

static esp_vhci_host_callback_t vhci_host_cb = {
	controller_rcv_pkt_ready,
	host_rcv_pkt
};

static void hci_cmd_send_reset(void)
{
	uint16_t sz = make_cmd_reset(hci_cmd_buf);
	esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_ble_adv_start(void)
{
	uint16_t sz = make_cmd_ble_set_adv_enable(hci_cmd_buf, 1);
	esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_ble_set_adv_param(void)
{
	uint16_t adv_intv_min = 160;	/* 100ms */
	uint16_t adv_intv_max = 160;	/* 100ms */
	uint8_t adv_type = 0;			/* ADV_IND */
	uint8_t own_addr_type = 0;		/* Public */
	uint8_t peer_addr_type = 0;
	uint8_t peer_addr[6] = {0x80, 0x81, 0x82, 0x83, 0x84, 0x85};
	uint8_t adv_chn_map = 0x07;
	uint8_t adv_filter_policy = 0;

	uint16_t sz = make_cmd_ble_set_adv_param(hci_cmd_buf,
				  adv_intv_min, adv_intv_max, adv_type,
				  own_addr_type, peer_addr_type, peer_addr,
				  adv_chn_map, adv_filter_policy);
	esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

static void hci_cmd_send_ble_set_adv_data(void)
{
	/*
	 *  ★放射確認名（Milestone 0）。ASP3側の"ASP3-C6-BLE"と紛れないよう
	 *  別名にする（stockが放射している証拠に固有性を持たせる）。
	 */
	const char *adv_name = "STOCK-C6-BT";
	uint8_t name_len = (uint8_t) strlen(adv_name);
	uint8_t adv_data[31] = {0x02, 0x01, 0x06, 0x0, 0x09};
	uint8_t adv_data_len;

	adv_data[3] = name_len + 1;
	for (int i = 0; i < name_len; i++) {
		adv_data[5 + i] = (uint8_t) adv_name[i];
	}
	adv_data_len = 5 + name_len;

	uint16_t sz = make_cmd_ble_set_adv_data(hci_cmd_buf, adv_data_len, adv_data);
	esp_vhci_host_send_packet(hci_cmd_buf, sz);
}

/*
 *  bit8 が assert されたら真、そうでなければ偽。呼出し側でログする。
 */
static bool g_lock_confirmed = false;
static bool g_adv_started = false;

void bleAdvtTask(void *pvParameters)
{
	int cmd_cnt = 0;
	bool send_avail = false;
	int poll_n = 0;

	esp_vhci_host_register_callback(&vhci_host_cb);
	ESP_LOGI(TAG, "BLE advt task start");

	while (1) {
		vTaskDelay(200 / portTICK_PERIOD_MS);
		send_avail = esp_vhci_host_check_send_available();
		if (send_avail) {
			switch (cmd_cnt) {
			case 0: hci_cmd_send_reset(); ++cmd_cnt; break;
			case 1: hci_cmd_send_ble_set_adv_param(); ++cmd_cnt; break;
			case 2: hci_cmd_send_ble_set_adv_data(); ++cmd_cnt; break;
			case 3:
				hci_cmd_send_ble_adv_start();
				++cmd_cnt;
				g_adv_started = true;
				break;
			}
		}
		if ((poll_n++ % 5) == 0) {
			uint32_t v = synth_lock_read();
			bool locked = (v & SYNTH_LOCK_BIT8) != 0;
			if (locked) {
				g_lock_confirmed = true;
			}
			ESP_LOGW(TAG, "STATUS cmd_cnt=%d adv_started=%d synth=0x%08lx bit8(lock)=%d",
					 cmd_cnt, (int) g_adv_started, (unsigned long) v, (int) locked);
		}
	}
}

#ifdef ARM_A_JUMP
/*
 *  Arm A：BT init+enable+adv 確立（bit8 assert 確認）後，ASP3 へジャンプ。
 *  bleAdvtTask がadv開始＋lock確認してから最低3秒の安定adv観測窓を経て，
 *  別タスクからジャンプする（bleAdvtTaskはジャンプで消えるので専用タスク）。
 */
static void jumpWatchTask(void *pvParameters)
{
	int stable_after_lock = 0;
	while (1) {
		vTaskDelay(500 / portTICK_PERIOD_MS);
		if (g_lock_confirmed && g_adv_started) {
			stable_after_lock++;
			if (stable_after_lock >= 6) {	/* 3秒安定 */
				ESP_LOGW(TAG, "ARM_A_JUMP: lock+adv confirmed stable, jumping to ASP3 in 1s");
				vTaskDelay(1000 / portTICK_PERIOD_MS);
				ESP_LOGW(TAG, "ARM_A_JUMP: asp3_jump_now()");
				asp3_jump_now();
				/* 到達しない */
			}
		} else {
			stable_after_lock = 0;
		}
	}
}
#endif /* ARM_A_JUMP */

#ifdef ARM_B_NOBT_JUMP
/*
 *  Arm B（control）：BT を一切初期化せず，起動後すぐASP3へジャンプ。
 *  ブートローダのpmu_init単体（Direct Bootが飛ばす処理）の寄与を見る。
 */
static void armBJumpTask(void *pvParameters)
{
	ESP_LOGW(TAG, "ARM_B_NOBT_JUMP: no BT init, jumping to ASP3 in 2s");
	/*
	 *  §20.7：reboot ループに強い «stock 到達» マーカ．cold Arm B が
	 *  console 読めず inconclusive だったため，stock がここ（jump 直前）まで
	 *  到達したことを LP_AON STORE6(0x600B1018) に残す（guest の stage
	 *  マーカ STORE1 が出なければ «stock/jump 失敗»，STORE6 だけ出れば
	 *  «stock は jump 到達も guest 未到達» を親が read-mem で切り分ける）．
	 */
	*(volatile uint32_t *) 0x600B1018UL = 0x570C0001UL;
	vTaskDelay(2000 / portTICK_PERIOD_MS);
	*(volatile uint32_t *) 0x600B1018UL = 0x570C0002UL;	/* jump 直前 */
	ESP_LOGW(TAG, "ARM_B_NOBT_JUMP: asp3_jump_now()");
	asp3_jump_now();
	/* 到達しない */
}
#endif /* ARM_B_NOBT_JUMP */

void app_main(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	ESP_LOGW(TAG, "pre-init synth=0x%08lx", (unsigned long) synth_lock_read());

#ifdef ARM_B_NOBT_JUMP
	xTaskCreate(&armBJumpTask, "armb", 4096, NULL, 5, NULL);
	return;
#else
	esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

	if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
		ESP_LOGE(TAG, "controller init failed: %s", esp_err_to_name(ret));
		return;
	}

	if ((ret = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK) {
		ESP_LOGE(TAG, "controller enable failed: %s", esp_err_to_name(ret));
		return;
	}
	ESP_LOGW(TAG, "controller_enable OK, post-enable synth=0x%08lx",
			 (unsigned long) synth_lock_read());

	xTaskCreate(&bleAdvtTask, "bleAdvtTask", 4096, NULL, 5, NULL);
#ifdef ARM_A_JUMP
	xTaskCreate(&jumpWatchTask, "jumpwatch", 4096, NULL, 5, NULL);
#endif
#endif /* ARM_B_NOBT_JUMP */
}

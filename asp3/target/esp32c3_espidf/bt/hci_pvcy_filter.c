/*
 *  connect不可 恒久修正候補(ii)（ドラフト・既定OFF）
 *  docs/c3-ble-connect-plan.md 段階1／tmp/review_c3_connect.md 候補1
 *
 *  ★状態：未確定。実機A/B（PVCY=0でconnect成功／PVCY=1で失敗）で候補1が
 *  確定した場合にのみ投入する切り分け後の恒久修正ドラフト。既定では
 *  esp_bt.cmake の option(ESP32C3_BT_PVCY_FILTER) が OFF のためリンクされない。
 *
 *  機構：
 *    NimBLE ホストが送出する HCI コマンドは最終的に
 *    esp_vhci_host_send_packet(uint8_t *data, uint16_t len) でコントローラ(VHCI)へ
 *    渡る。esp_bt.cmake で -Wl,--wrap=esp_vhci_host_send_packet を付けると，本
 *    ファイルの __wrap_esp_vhci_host_send_packet が呼ばれ，
 *    __real_esp_vhci_host_send_packet が元の実体になる。
 *
 *  ★なぜ ble_hci_trans_hs_cmd_tx ではなく esp_vhci_host_send_packet を wrap するか：
 *    当初 ble_hci_trans_hs_cmd_tx を --wrap したが，実機 objdump で
 *    __wrap_ への直 jal が 0＝**inert（一度も呼ばれない）** と判明。ble_hci_trans_hs_cmd_tx
 *    は ble_transport_to_ll_cmd_impl から同モジュール内 tail-jump で到達され，
 *    --wrap（TU跨ぎの名前付き直接呼び出しのみ再束縛）が効かない経路だった。
 *    esp_vhci_host_send_packet は esp_nimble_hci.c から名前付き直 jal で呼ばれ
 *    （pvcy1.elf で jal 2件確認・既存 acl_trace.c も同関数を wrap 実績あり），
 *    確実に on-path。
 *
 *  バッファレイアウト（esp_vhci_host_send_packet 到達時点＝H4型付与済み。
 *  esp_nimble_hci.c:92-116 で ble_hci_trans_hs_cmd_tx が *cmd=BLE_HCI_UART_H4_CMD を
 *  書いてから wrapper→esp_vhci_host_send_packet を呼ぶ）：
 *    data[0]   H4 パケット型（コマンド=0x01。ACL=0x02 は対象外）
 *    data[1]   opcode LSB
 *    data[2]   opcode MSB
 *    data[3]   パラメタ長
 *    data[4..] パラメタ本体
 *
 *  対象：LE Set Address Resolution Enable
 *    OGF=0x08(LE), OCF=0x2D → BLE_HCI_OP(ogf,ocf)=(ocf|(ogf<<10))=0x202D
 *    → little-endian で data[1]=0x2D, data[2]=0x20。
 *    struct ble_hci_le_set_addr_res_en_cp { uint8_t enable; } ＝パラメタ1バイト
 *    → enable は data[4]。
 *
 *  方針（advisor 助言）：Command Complete を «偽造しない»。enable バイトを 0 に
 *  潰して __real_ へ渡す＝有効なコマンドとしてコントローラへ送りコントローラは
 *  正常に Command Complete を返す。よってアドレス解決を «一度も» 有効化しない
 *  （候補(i) の on_sync 事後無効化＝「一瞬有効になる窓」がある方式との違い）。
 *  resolving-list の Clear(0x2029)/Add(0x2027) は落とすと応答処理が要るため触らない。
 *
 *  段階1で「どのコマンドで詰まるか」が実機で判明したら，対象 opcode をここに
 *  追加する。
 */
#include <stdint.h>
#include <stddef.h>

#if defined(TOPPERS_ESP32C3_BT_PVCY_FILTER)

/*  __real_ 実体（controller/bt.c の esp_vhci_host_send_packet）  */
extern void __real_esp_vhci_host_send_packet(uint8_t *data, uint16_t len);

#define HCI_H4_TYPE_CMD                 0x01u	/* H4 command packet             */
#define HCI_OCF_LSB_LE_SET_ADDR_RES_EN  0x2Du	/* OCF 0x2D の下位                */
#define HCI_OGF_LE_HIGH                 0x20u	/* (OGF 0x08 << 10) の上位バイト  */

void
__wrap_esp_vhci_host_send_packet(uint8_t *data, uint16_t len)
{
	if (data != NULL && len >= 5u &&
	    data[0] == HCI_H4_TYPE_CMD &&
	    data[1] == HCI_OCF_LSB_LE_SET_ADDR_RES_EN &&
	    data[2] == HCI_OGF_LE_HIGH &&
	    data[3] >= 1u) {
		/*  LE Set Address Resolution Enable：enable(data[4]) を 0 に潰す。  */
		data[4] = 0x00u;
	}

	__real_esp_vhci_host_send_packet(data, len);
}

#endif /* TOPPERS_ESP32C3_BT_PVCY_FILTER */

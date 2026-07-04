# TCP/IP統合（Phase C）

## 課題

Wi-Fi os_adapter shim（Phase B-2）でWPA2 AP接続（L2）までは成立したが，
IPアドレス取得・実通信（DHCP／ping等）を行うTCP/IPスタックが無い．
esp_wifiバイナリblobはEthernetフレーム単位のtx/rxインタフェース
（`esp_wifi_internal_tx`／`esp_wifi_internal_reg_rxcb`）のみを提供し，
上位のTCP/IPスタックはOS側が用意する前提（ESP-IDFはlwIP＋esp_netif，
NuttXは自前のnetdev層＋lwIPを使用）．

## 設計方針：lwIP（NO_SYS=1）＋単一実行文脈

- lwIP本体は[lwip-tcpip/lwip](https://github.com/lwip-tcpip/lwip)
  （lwip.orgが案内する公式read-onlyミラー）をsubmoduleとして取り込む
  （`STABLE-2_2_1_RELEASE`にpin）．ASP3・NuttX・Zephyrいずれも同じ
  lwIPを使う既存の実績があり，esp-hal-3rdparty方式（下層hal/soc層の
  みSDKに依存）の思想と整合する．
- ASP3にはlwIPの`sys_arch`スレッド抽象化層（mbox/sem/thread）を実装
  せず，**`NO_SYS=1`（raw API）** を使う．lwIPコアAPI呼出しは
  **net_task（cfg生成の唯一のタスク）にすべて集約**し，単一実行文脈
  とすることでロックを一切必要としない設計にした（`SYS_ARCH_PROTECT`
  はNO_SYS=1でも参照されるため保険として実装するが，実質無競合）．
  Wi-Fi shim側の静的タスク／セマフォプール方式とは独立した，
  net専用の最小構成（CRE_DTQ 1個＋CRE_TSK 1個）．
- Wi-Fiドライバのrxコールバック（`esp_wifi_internal_reg_rxcb`）は
  Wi-Fiタスク文脈で呼ばれるため，**lwIP APIには一切触れず**受信フレ
  ームを`(buffer, len, eb)`のままボックス化してnet_taskのキューへ
  渡すだけに留める．pbuf確保・`netif->input`呼出し・
  `esp_wifi_internal_free_rx_buffer`はすべてnet_task側で行う．
  送信（`low_level_output`）はチェーンpbufを静的バッファへ線形化して
  から`esp_wifi_internal_tx`へ渡す（同APIは呼出し時に内容をコピー
  するため，呼出し後にバッファを再利用できる）．
- Wi-Fiイベントハンドラ（アプリ側）→net_taskの連絡は，lwIPに触れない
  フラグ変数（`netif_esp32c3_notify_link()`）のみで行う．
- ping（実通信の確認）はlwIP同梱の`contrib/apps/ping/ping.c`
  （raw API版）をそのまま採用．`sys_timeout`ベースで動作するため，
  net_taskが毎ループ呼ぶ`sys_check_timeouts()`から自動的に駆動される
  （追加のタスクや専用ループが不要）．

## スコープ

- 対応：DHCPによるIPアドレス取得，デフォルトゲートウェイへのraw API
  ICMP ping（実通信の確認），TCPエコーサーバ（ポート7．lwIP同梱の
  `contrib/apps/tcpecho_raw`をそのまま採用）．
- 非対応（将来課題）：DNS，ソケット／netconn API（`api/`配下はビルド
  対象外＝NO_SYS=1では使えない。BSDソケット互換が必要になった場合は
  `sys_arch`のmbox/sem/thread実装＋`tcpip_thread`化＝Wi-Fi shimの
  os_adapterと同様の作り込みが要る），IPv6．

## 実施結果

### 実機DHCP取得＋ping成立（2026-07-04）

`apps/wifi_dhcp`（`wifi_connect`にlwIP統合を重ねたデモ）で実機WPA2 AP
接続後，DHCPでIPアドレスを取得し，デフォルトゲートウェイへのraw API
pingが継続的に成功することを確認した。

```
event: STA_CONNECTED
wifi_dhcp: CONNECTED, waiting for DHCP
net: link up, starting DHCP
net: DHCP bound ip=192.168.1.56 gw=192.168.1.1
wifi_dhcp: IP acquired: 192.168.1.56
net: ping gateway -> OK
net: ping gateway -> OK
net: ping gateway -> OK
```

（`no time event is processed in hrt interrupt.`はfch_mnt拡張の既知の
定常ログであり本機能とは無関係．`docs/spec/11_usage_notes.md`参照）

### 実機TCP通信成立（2026-07-04）

`LWIP_TCP`を有効化し，lwIP同梱の`contrib/apps/tcpecho_raw`（raw API・
ポート7）をそのまま採用．開発機（同一LAN，デフォルトゲートウェイ共通）
から`nc`でTCP接続し，送信データがそのままエコーバックされることを
確認した（コールバック`tcp_recv`/`tcp_write`等はすべてnet_task文脈から
呼ばれるため単一実行文脈の原則は維持される）。

```bash
$ printf "hello asp3 tcp echo test 12345\n" | nc -q 1 192.168.1.56 7
hello asp3 tcp echo test 12345
```

TCP_MSS（536）を超える3000バイトのペイロード（複数セグメント・pbuf
チェーンを要する）も欠落・破損なく正しくエコーバックされることを確認．
DHCP＋ping（Wi-Fi shimの既存動作）への回帰もなし。

RAM増分はわずか（プールを小さめに抑えたため．下表参照）。

### 実装したファイル（すべてasp3_esp_idf側．asp3_core側の変更ゼロ）

| ファイル | 内容 |
|---|---|
| `.gitmodules`／`lwip`（submodule） | lwip-tcpip/lwip，`STABLE-2_2_1_RELEASE`にpin |
| `asp3/target/esp32c3_espidf/net/port/include/arch/cc.h` | lwIP適合層（型はGCC既定に委任．診断出力/assert/乱数のみesp_shim経由に上書き．`ctype.h`/`unistd.h`はツールチェーンに実体が無いため`LWIP_NO_CTYPE_H`/`LWIP_NO_UNISTD_H`で回避） |
| `asp3/target/esp32c3_espidf/net/port/include/lwipopts.h` | `NO_SYS=1`．DHCP/ARP/ICMP/RAW/UDP/TCP有効（TCP系プールは小さめに固定），DNS/IPv6/ソケット無効．ping結果通知（`PING_RESULT`）をnetif_esp32c3.cへ配線 |
| `asp3/target/esp32c3_espidf/net/port/sys_arch.c` | `sys_now()`・`sys_arch_protect/unprotect`のみ（NO_SYS=1で必要なのはこの2種のみ） |
| `asp3/target/esp32c3_espidf/net/netif_esp32c3.c/.h` | ethernet netif実装（esp_wifi_internal_tx/reg_rxcb上）＋net_task（lwIPコアの唯一実行文脈）＋DHCP開始／ping起動／tcpecho_raw起動 |
| `asp3/target/esp32c3_espidf/net/net_cfg.h`／`net.cfg` | 受信キュー（CRE_DTQ×1）＋net_task（CRE_TSK×1）の静的定義 |
| `asp3/target/esp32c3_espidf/target.cmake` | `ESP32C3_LWIP`オプション追加（`ESP32C3_WIFI`必須）．lwIPの`Filelists.cmake`（`lwipcore_SRCS`/`lwipcore4_SRCS`）＋`netif/ethernet.c`＋`contrib/apps/ping/ping.c`＋`contrib/apps/tcpecho_raw/tcpecho_raw.c`を採用 |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.c` | `esp_shim_log_write()`を実装（`esp_shim.h`に宣言のみ存在し未実装だった関数．lwIPの`LWIP_PLATFORM_DIAG`/`LWIP_PLATFORM_ASSERT`から利用するため） |
| `apps/wifi_dhcp/` | デモアプリ（`wifi_connect`にSTA_CONNECTED/DISCONNECTED時の`netif_esp32c3_notify_link()`呼出しを追加しただけ．lwIP APIには一切触れない） |

### RAM予算

Wi-Fi shimの静的ヒープ（192KB）と共存するため，lwIP側は小さめに抑えた
（`MEM_SIZE=4KB`・`PBUF_POOL_SIZE=4`×`PBUF_POOL_BUFSIZE=1600`・
net_taskスタック3KB．TCP有効化後も`MEMP_NUM_TCP_PCB=2`・
`MEMP_NUM_TCP_PCB_LISTEN=1`・`MEMP_NUM_TCP_SEG=8`と小さめに固定）。

| ビルド | RAM使用率（`--print-memory-usage`．320KB中） |
|---|---|
| wifi_connect（Phase B-2b．lwIPなし） | 88.58%（290244B） |
| wifi_dhcp（DHCP＋ping，TCP無効） | 93.68%（306964B，残り約20.7KB） |
| wifi_dhcp（TCP＝tcpecho_raw有効化後） | 93.86%（307572B，残り約19.6KB） |

### 検証結果

| テスト | 実施 | 結果 |
|---|---|---|
| POSIX | − | 対象外（ESP32-C3固有機能） |
| QEMU (esp32c3) | ○ | コンパイル・リンクのみ確認（QEMUのesp32c3にWi-Fi無線モデルが無いため実通信は不可） |
| 実機 | ○ | `apps/wifi_dhcp`：STA_CONNECTED→DHCP取得（`192.168.1.56`）→ゲートウェイへのraw ICMP ping継続成功→開発機から`nc`でTCPエコー（短文・3000バイトとも欠落なく確認） |
| 既存ビルドへの回帰 | ○ | `wifi_scan`（Wi-Fiのみ・lwIPなし）・`tp-hw`（Wi-Fiなし）とも再ビルドし0エラー確認．TCP有効化後もDHCP／ping継続動作を確認 |

### Git情報

- ベースコミット：Phase B-2b完了時点（`6956669`）
- 関連コミット範囲：本コミット
- ファイルリスト再現コマンド例：`git status --short`（lwip submodule追加＋net/・apps/wifi_dhcp/新規＋target.cmake/esp_shim.c変更）

# TCP/IP統合（Phase C）

## 課題

Wi-Fi os_adapter shim（Phase B-2）でWPA2 AP接続（L2）までは成立したが，
IPアドレス取得・実通信（DHCP／ping等）を行うTCP/IPスタックが無い．
esp_wifiバイナリblobはEthernetフレーム単位のtx/rxインタフェース
（`esp_wifi_internal_tx`／`esp_wifi_internal_reg_rxcb`）のみを提供し，
上位のTCP/IPスタックはOS側が用意する前提（ESP-IDFはlwIP＋esp_netif，
NuttXは自前のnetdev層＋lwIPを使用）．

## 設計方針（現行．NO_SYS=0＋BSDソケット互換）

- lwIP本体は[lwip-tcpip/lwip](https://github.com/lwip-tcpip/lwip)
  （lwip.orgが案内する公式read-onlyミラー）をsubmoduleとして取り込む
  （`STABLE-2_2_1_RELEASE`にpin）．ASP3・NuttX・Zephyrいずれも同じ
  lwIPを使う既存の実績があり，esp-hal-3rdparty方式（下層hal/soc層の
  みSDKに依存）の思想と整合する．
- 当初はlwIPの`sys_arch`を実装せず`NO_SYS=1`（raw API）＋net_task
  1タスクへの手動集約で実装したが（下記「旧設計」参照），BSDソケット
  互換（`socket()`/`connect()`/`send()`/`recv()`等の標準名）が
  必要になったため**`NO_SYS=0`＋実sys_arch実装へ移行**した．
- `sys_arch`（`net/port/sys_arch.c`）はWi-Fi shim（wifi/esp_shim.c）と
  同じ「ASP3静的プールからの動的割当て」方式で実装：
  - `sys_sem_t`／`sys_mutex_t`（`LWIP_COMPAT_MUTEX=1`でsem実装を流用）
    はCRE_SEMプールから．
  - `sys_mbox_t`はCRE_DTQプールに**1:1で対応**（Wi-Fi shimのキューと
    異なりヒープボックス化は不要．lwIPのmboxは常にポインタ1個しか
    運ばないため，ASP3 DTQの`intptr_t`アイテムにそのまま収まる）．
  - `sys_thread_t`はcfgで休止生成した唯一のタスク（NET_TSK）を
    `act_tsk`で起動する**単発実装**（lwIPは生涯に一度だけ，
    `tcpip_init()`内部から`tcpip_thread`生成のために`sys_thread_new()`
    を呼ぶという前提に基づく．`contrib/apps/ping`をソケット版
    〔`ping_thread`が2回目のsys_thread_newを呼ぶ〕にしないよう
    `PING_USE_SOCKETS=0`で固定しているのはこのため）．
- `LWIP_TCPIP_CORE_LOCKING=0`（メッセージパッシングモデル）を採用．
  ソケット呼出しは各アプリタスク文脈でmboxにメッセージを積み
  `op_completed`セマフォで待つ方式とし，core全体を保護する巨大
  ミューテックス（CORE_LOCKING=1のモデル）は使わない．
- Wi-Fiドライバのrxコールバック（`esp_wifi_internal_reg_rxcb`）は
  Wi-Fiタスク文脈で呼ばれるが，`pbuf_alloc`/`pbuf_take`は
  `SYS_ARCH_PROTECT`で保護されており任意の文脈から呼んでよく，
  `tcpip_input()`はまさに「外部文脈からの安全なパケット注入」用に
  lwIPが提供するAPIのため，**ボックス化・専用キューなしで直接呼べる**
  （旧設計より単純化）．
- リンクup/down（Wi-Fiイベントハンドラ→tcpip_thread）は
  `tcpip_callback()`で委譲し，`dhcp_start`等のraw API呼出しを
  tcpip_thread文脈に限定する．DHCP完了検出は`netif_set_status_callback`
  （ポーリング不要．旧設計はポーリングだった）．
- netif_add／tcpecho_raw_init等の初期化は`tcpip_init()`の
  init_doneコールバック内（＝tcpip_thread起動直後の文脈）で行う．
- ping（実通信の確認）はlwIP同梱の`contrib/apps/ping/ping.c`
  （raw API版．`PING_USE_SOCKETS=0`固定）をそのまま採用．
  `sys_timeout`ベースで動作するため，tcpip_thread内蔵のタイマ処理
  から自動的に駆動される．

### 旧設計（NO_SYS=1．2026-07-04当初実装．参考のため残す）

lwIPの`sys_arch`スレッド抽象化層を実装せず`NO_SYS=1`（raw API）を
使用．lwIPコアAPI呼出しは`net_task`（cfg生成の唯一のタスク）にすべて
集約し，単一実行文脈とすることでロックを一切必要としない設計だった．
Wi-Fiドライバのrxコールバックは受信フレームを`(buffer, len, eb)`の
ままボックス化して`net_task`のキューへ渡すだけに留め，pbuf確保・
`netif->input`呼出し・`esp_wifi_internal_free_rx_buffer`は
すべて`net_task`側（DTQ受信ループ＋`sys_check_timeouts()`のポーリング）
で行っていた．BSDソケット互換の必要が生じたため上記の現行設計へ移行．

## スコープ

- 対応：DHCPによるIPアドレス取得，デフォルトゲートウェイへのraw API
  ICMP ping（実通信の確認），TCPエコーサーバ（ポート7．lwIP同梱の
  `contrib/apps/tcpecho_raw`をそのまま採用），**BSDソケットAPI**
  （`socket()`/`bind()`/`listen()`/`accept()`/`recv()`/`send()`/
  `close()`．任意のアプリタスクから利用可能．デモは`apps/
  tcp_socket_echo`＝ポート8のソケットechoサーバ）．
- 非対応（将来課題）：DNS，IPv6．マルチスレッド化（`sys_thread_new`
  複数回呼出し対応）は現状スコープ外＝lwIPが内部生成するtcpip_thread
  以外に新規スレッドを生成するアプリコードは書けない（ソケット呼出し
  自体は複数アプリタスクから並行して問題なく呼べる．制限があるのは
  「新しいOSスレッドをlwIP側の都合で生成する」ケースのみ）．

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

### BSDソケット互換化・実機TCP（sockets API）通信成立（2026-07-04）

`NO_SYS=1`（raw API＋net_task手動集約）から`NO_SYS=0`（実sys_arch実装＋
`tcpip_thread`）へ移行し，`LWIP_SOCKET=1`／`LWIP_NETCONN=1`を有効化．
lwIPの`LWIP_COMPAT_SOCKETS`（既定1）により`socket()`/`bind()`/
`listen()`/`accept()`/`recv()`/`send()`/`close()`が標準名のまま使える．

デモ`apps/tcp_socket_echo`（`wifi_dhcp`のWi-Fi接続部分を流用し，DHCP
完了後に**専用のASP3アプリタスク（tcpip_threadでもmain_taskでもない
echo_task）** からBSDソケットAPIでポート8のechoサーバを起動）を実機で
検証：

```bash
$ printf "BSD socket compat test 42\n" | nc -q 1 192.168.1.56 8
BSD socket compat test 42
```

2000バイトのペイロードも欠落なくエコーバック．既存のraw APIエコー
（ポート7）と同時に問題なく動作し，DHCP／pingも継続動作（回帰なし）。

**実装中に見つかった問題と対応**：
- `lwip/src/api/sockets.c`が参照する`errno`のグローバル変数実体が
  どこにも定義されておらず（`hal_stub/include/errno.h`は`extern`
  宣言のみ）リンクエラーになった．`asp3/target/esp32c3_espidf/wifi/
  esp_shim_libc.c`に`int errno;`を追加（単一グローバル＝タスク単位
  ではない点は`errno.h`のコメントに既知の制限として明記）。
- `MEMP_NUM_TCP_PCB_LISTEN=1`のままだと，ポート7（tcpecho_raw）が
  唯一のlistenスロットを使い切り，ポート8（tcp_socket_echo）の
  `listen()`がERR_MEMで失敗した（実機で再現・特定）．
  `MEMP_NUM_TCP_PCB_LISTEN=2`・`MEMP_NUM_TCP_PCB=3`に引き上げて解消
  （RAM増分は約190バイトのみ）。

### BSDソケットクライアントデモ・実機TCP通信成立（2026-07-04）

`apps/tcp_socket_echo`（サーバ側）と対になる**クライアント側**のデモ
`apps/tcp_socket_client`を追加．`socket()`/`connect()`/`send()`/
`recv()`/`close()`を専用のASP3アプリタスク（`client_task`）から呼び，
`-DSERVER_ADDR=...`/`-DSERVER_PORT=...`で指定した外部サーバへ接続する．

開発機（同一LAN）でPythonの単純なTCPエコーサーバ（ポート9000）を
立てて実機から接続し，5往復のsend/recvがすべて正しく完了することを
確認した：

```
tcp_socket_client: connected
tcp_socket_client: sent 37 bytes
tcp_socket_client: recv 42 bytes: ack: hello from asp3 tcp_socket_client #0
...(#1〜#4まで同様)...
tcp_socket_client: done
```

開発機側ログ（`recv`）も5件とも完全一致．DHCP／pingの継続動作も確認
（回帰なし）。`recv()`が無応答サーバでブロックし続けないよう
`SO_RCVTIMEO`（`LWIP_SO_SNDRCVTIMEO_NONSTANDARD=1`でミリ秒int指定）を
新たに有効化した．

### BSDソケットUDPデモ・実機通信成立（2026-07-04）

`apps/udp_socket_echo`：`socket(AF_INET, SOCK_DGRAM, 0)`/`bind()`/
`recvfrom()`/`sendto()`（`connect()`/`listen()`/`accept()`を伴わない
コネクションレスAPI）による UDP echoサーバをポート9で実装．専用の
ASP3アプリタスク（`udp_task`）から動作する．

開発機からPythonの単純なUDPクライアントで送信し，実機からの応答が
送信内容とバイト単位で完全一致することを確認：

```
sent: b'hello asp3 udp echo test'
recv: b'hello asp3 udp echo test' from ('192.168.1.56', 9)
match: True
```

追加で3件のデータグラムも連続して正しくエコーされることを確認．
DHCP／ping・既存のTCPデモ（ポート7・8）への回帰もなし．

### 実装したファイル（すべてasp3_esp_idf側．asp3_core側の変更ゼロ）

| ファイル | 内容 |
|---|---|
| `.gitmodules`／`lwip`（submodule） | lwip-tcpip/lwip，`STABLE-2_2_1_RELEASE`にpin |
| `asp3/target/esp32c3_espidf/net/port/include/arch/cc.h` | lwIP適合層（型はGCC既定に委任．診断出力/assert/乱数のみesp_shim経由に上書き．`ctype.h`/`unistd.h`はツールチェーンに実体が無いため`LWIP_NO_CTYPE_H`/`LWIP_NO_UNISTD_H`で回避） |
| `asp3/target/esp32c3_espidf/net/port/include/arch/sys_arch.h` | `sys_sem_t`/`sys_mbox_t`/`sys_thread_t`型定義（ASP3のID＝int_tと同表現の素のint．kernel.hはここではincludeしない） |
| `asp3/target/esp32c3_espidf/net/port/include/lwipopts.h` | `NO_SYS=0`．`LWIP_TCPIP_CORE_LOCKING=0`・`LWIP_COMPAT_MUTEX=1`．DHCP/ARP/ICMP/RAW/UDP/TCP/ソケット/netconn有効，DNS/IPv6無効．`LWIP_ERRNO_STDINCLUDE=1`でerrno.hをhal_stub側へ委譲．ping結果通知（`PING_RESULT`）／`PING_USE_SOCKETS=0`固定をnetif_esp32c3.cへ配線 |
| `asp3/target/esp32c3_espidf/net/port/sys_arch.c` | `sys_now()`/`sys_arch_protect/unprotect`（従来通り）に加え，`sys_sem_*`/`sys_mbox_*`（ASP3 CRE_SEM／CRE_DTQ静的プール．mboxはボックス化不要）／`sys_thread_new`（cfg休止生成のNET_TSKを起動する単発実装）／`sys_init`／`net_task_entry`（トランポリン）を実装 |
| `asp3/target/esp32c3_espidf/net/netif_esp32c3.c/.h` | ethernet netif実装（esp_wifi_internal_tx/reg_rxcb上．rxコールバックは`tcpip_input()`を直接呼ぶよう簡素化）＋`tcpip_init_done`（netif_add／tcpecho_raw_init）＋`tcpip_callback`によるリンクup/down委譲＋`netif_set_status_callback`によるDHCP完了検出（ポーリング廃止）＋`netif_esp32c3_start()`公開API追加 |
| `asp3/target/esp32c3_espidf/net/net_cfg.h`／`net.cfg` | 受信キュー（CRE_DTQ）を廃止．sys_arch用セマフォプール（CRE_SEM×8）／メールボックスプール（CRE_DTQ×10）を追加．NET_TSKは`TA_NULL`（`sys_thread_new`が起動） |
| `asp3/target/esp32c3_espidf/target.cmake` | `lwipapi_SRCS`（api_lib/api_msg/err/if_api/netbuf/netdb/netifapi/sockets/tcpip）を追加採用 |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.c` | `esp_shim_log_write()`実装（前回同様） |
| `asp3/target/esp32c3_espidf/wifi/esp_shim_libc.c` | `int errno;`実体を追加（BSDソケットAPIのリンクに必須） |
| `asp3/target/esp32c3_espidf/hal_stub/include/errno.h` | BSDソケット関連のerrnoマクロ26個を追加（`sockets.c`/`err.c`/`if_api.c`の参照を網羅） |
| `apps/wifi_dhcp/` | `netif_esp32c3_start()`呼出しを追加 |
| `apps/tcp_socket_echo/` | 新規デモ：`echo_task`（専用ASP3タスク）からBSDソケットAPIでポート8のechoサーバを起動 |
| `apps/tcp_socket_client/` | 新規デモ：`client_task`（専用ASP3タスク）からBSDソケットAPI（`socket`/`connect`/`send`/`recv`）で外部サーバへ接続。`SO_RCVTIMEO`有効化に伴い`lwipopts.h`も追加変更 |
| `apps/udp_socket_echo/` | 新規デモ：`udp_task`（専用ASP3タスク）からBSDソケットUDP API（`socket(SOCK_DGRAM)`/`bind`/`recvfrom`/`sendto`）でポート9のechoサーバを起動 |

### RAM予算

Wi-Fi shimの静的ヒープ（192KB）と共存するため，lwIP側は小さめに抑えた
（`MEM_SIZE=4KB`・`PBUF_POOL_SIZE=4`×`PBUF_POOL_BUFSIZE=1600`．
`MEMP_NUM_TCP_PCB=3`・`MEMP_NUM_TCP_PCB_LISTEN=2`・
`MEMP_NUM_TCP_SEG=8`・`MEMP_NUM_NETCONN=4`）。

| ビルド | RAM使用率（`--print-memory-usage`．320KB中） |
|---|---|
| wifi_connect（Phase B-2b．lwIPなし） | 88.58%（290244B） |
| wifi_dhcp（DHCP＋ping，TCP無効，NO_SYS=1） | 93.68%（306964B，残り約20.7KB） |
| wifi_dhcp（TCP＝tcpecho_raw有効化後，NO_SYS=1） | 93.86%（307572B，残り約19.6KB） |
| wifi_dhcp（NO_SYS=0＋ソケット化後） | 94.56%（309860B，残り約17.8KB） |
| tcp_socket_echo（echo_task追加＋listen pcbプール拡張後） | 95.90%（314260B，残り約13.1KB） |
| tcp_socket_client（SO_RCVTIMEO有効化後） | 95.91%（314276B，残り約13.1KB） |
| udp_socket_echo | 95.91%（314276B，残り約13.1KB） |

### 検証結果

| テスト | 実施 | 結果 |
|---|---|---|
| POSIX | − | 対象外（ESP32-C3固有機能） |
| QEMU (esp32c3) | ○ | コンパイル・リンクのみ確認（QEMUのesp32c3にWi-Fi無線モデルが無いため実通信は不可） |
| 実機 | ○ | `apps/wifi_dhcp`：STA_CONNECTED→DHCP取得（`192.168.1.56`）→ゲートウェイへのraw ICMP ping継続成功→`nc`でTCPエコー（raw API・ポート7．短文・3000バイトとも欠落なく確認）／`apps/tcp_socket_echo`：BSDソケットAPI（ポート8）でも短文・2000バイトとも欠落なくエコー確認．ポート7・8同時動作，DHCP／ping継続動作も確認／`apps/tcp_socket_client`：開発機のPython TCPサーバへBSDソケットAPIで接続し5往復のsend/recvが完全一致／`apps/udp_socket_echo`：開発機のPython UDPクライアントから4件のデータグラムを送信し全てバイト単位で完全一致でエコー確認 |
| 既存ビルドへの回帰 | ○ | `wifi_scan`（Wi-Fiのみ・lwIPなし）・`tp-hw`（Wi-Fiなし）とも再ビルドし0エラー確認．NO_SYS=0移行後もDHCP／ping／raw APIエコー継続動作を実機確認 |

### Git情報

- ベースコミット：Phase B-2b完了時点（`6956669`）
- 関連コミット範囲：本コミット
- ファイルリスト再現コマンド例：`git status --short`（lwip submodule追加＋net/・apps/wifi_dhcp/新規＋target.cmake/esp_shim.c変更）

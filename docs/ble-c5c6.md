# BLE (ESP32-C5/C6) 実装ログ

机上設計は`docs/ble-c5c6-plan.md`。本ファイルはHWラウンド（BLE実施NN）の
実装・実機検証の記録体系を確立する。C5/C6のBLEコントローラは
`SOC_ESP_NIMBLE_CONTROLLER=1`の新世代（libble_app.a単一blob）で，
C3のD-1〜D-2bとは別の実装が必要（詳細はplan doc）。

## BLE実施01：C6 D-1（controller init + VHCI疎通）

**日付**：2026-07-14　**対象**：ESP32-C6 board C（`14:C1:9F:E0:5A:9C`，
`asp3/target/esp32c6_espidf`）　**目的**：BLEコントローラ起動＋VHCI
（HCI_Reset→Command Complete）疎通の実機確認。

### 設計参照との乖離（重要）

`docs/ble-c5c6-plan.md`は「ASP3が実装するのは3つの登録テーブル
（osi_coex_funcs_t／ext_funcs_t／npl_funcs_t）」という前提で書かれて
いたが，実装に着手して`controller/esp32c6/bt.c`を精読した結果，
**bt.c自身がosi_coex_funcs_tとnpl_funcs_tの両方を自前で埋める**こと
が判明した：

- `osi_coex_funcs_t`：bt.c内の`s_osi_coex_funcs_ro`が
  `coex_schm_status_bit_set/clear_wrapper`（`CONFIG_SW_COEXIST_ENABLE`
  未定義時は中身が空no-op）と`NULL`（wifi_sleep_set／
  core_ble_conn_dyn_prio_get）で構成済み。ASP3は何もしなくてよい。
- `npl_funcs_t`：`porting/npl/freertos/src/npl_os_freertos.c`
  （ESP-IDF純正の"新npl"実装）が`npl_freertos_funcs_get()`を提供し，
  bt.cがそれを（後述の1:1橋渡し経由で）呼ぶ。ASP3は実装しない。

ASP3が実際に実装する必要があったのは**ext_funcs_tが指す下位
プリミティブ**（`platform/os.h`のesp_os_*＝task/intr／`esp_random`／
`_ecc_*`はD-1未使用）と，**bt.c以外のポーティング層**
（`esp_timer_*`／`esp_pm_lock_*`／`esp_ipc_call_blocking`／
`esp_partition_*`）、および**上流ドリフト吸収の互換シム2件**
（後述）だった。設計書の「3テーブル実装」という骨格自体は正しい
（登録呼出しの構造は3テーブルだが，2つはblob側が自前で埋める）。

### npl署名検証（実装前の事前確認．設計書リスク4）

`npl_os_freertos.c`（1269行）が要求するFreeRTOS原始プリミティブの
集合は，C3向けに用意済みの
`asp3/target/esp32c3_espidf/bt/stub/include/freertos/*.h`
（`FreeRTOS.h`/`queue.h`/`semphr.h`/`task.h`/`timers.h`/`portable.h`）
と**完全に一致することを実測で確認した**（コンパイルエラー0件で
そのまま通った）。根拠：本hal（esp-hal-3rdparty）submoduleには
`porting/npl/freertos/src/npl_os_freertos.c`と物理的に別の場所
（`host/nimble/nimble/porting/npl/freertos/src/npl_os_freertos.c`）
に**ほぼ同型のファイルがもう1つ存在し，C3のD-2a（NimBLEホスト，
`docs/bt-shim.md`）が既にそちらを同じスタブヘッダ集合でビルド
成功させていた**——今回のC6 D-1はこの既存検証済み資産をそのまま
横展開した形になる。実装時に追加で必要になったのは`esp_shim.c`
（C6固有ファイル）への4関数追加のみ（後述「遭遇した壁」）。

一方，`platform/os.h`のesp_os_*（bt.c本体が直接呼ぶtask/intr）は
C3に前例が無い新規実装が必要だった。NuttX版参照実装
（`hal/nuttx/src/platform/os.c`）を読み，戻り値規約
（0=成功／負値=失敗，esp_shim_task_create等の1/0規約とは異なる）
を確認した上で実装した。

### 実装した3テーブル周辺の概要

| 要素 | 実装場所 | 内容 |
|---|---|---|
| `esp_os_create_task_pinned_to_core`/`esp_os_task_delete` | `bt/bt_shim.c` | `esp_shim_task_create/delete`（wifi/esp_shim.c）へ委譲，戻り値規約を変換 |
| `esp_os_intr_alloc`/`esp_os_intr_free` | `bt/bt_shim.c` | C6のINTMTX（ソースルーティング）＋PLIC_MX（CPU線制御）2ブロック構成に対応．**多重登録安全なスロット配列**（`BT_INTR_MAX_SLOT=2`，CPU線1・2）をS3/C3の教訓に基づき最初から実装 |
| `platform/os.h` | `bt/stub/include/platform/os.h` | C3のhal_stub版を`#include_next`で継承しつつesp_os_*/`tskNO_AFFINITY`を追加 |
| `esp_timer_*` | `bt/bt_shim.c` | C3のbt_shim.cと同一設計（専用タイマタスク＋固定プール）．**プールサイズ4→16に拡大**（後述） |
| `esp_pm_lock_*`／`esp_ipc_call_blocking`／`esp_partition_*` | `bt/bt_shim.c` | C3から無改変移植（チップ非依存ロジック） |
| `npl_os_*`→`npl_freertos_*`橋渡し | `bt/bt_shim.c` | 上流ドリフト吸収シム（後述） |
| `nimble/nimble_port_os.h` | `bt/stub/include/nimble/` | 上流ドリフト吸収シム（後述） |
| クロック下準備 | `bt/bt_shim.c`（`esp_shim_bt_clock_init`）＋`wifi/esp_shim.c`（`esp_shim_modem_icg_init`，WiFiと共有） | 実施91のICGアンロックをBTパスにも適用（後述） |

### 実装前チェック：APMのBT経路（設計書リスク2）

`target_kernel_impl.c`の`esp32c6_r87_apm_unblock()`は`hardware_init_
hook()`末尾で**`ESP32C6_WIFI`のガード外＝無条件**に呼ばれていることを
コード読解で確認済み（実装変更不要）。実機検証（後述）でBT経路でも
HP_APM M0-M3例外ラッチが起動時clear・HCI疎通後もclearのままである
ことを確認し，設計書の「構造上カバーされているはず」を実証に格上げ
した。

### 実装前チェック：ICGアンロック（実施91）のBTパス適用（設計書リスク1）

実施91が特定したPMU HP_ACTIVE ICG_MODEM_REG（0x600B000C）の
cold-boot根治（`esp_shim_modem_icg_init()`）は，元は`wifi/
esp_wifi_adapter.c`内のWiFi専用static関数だった。BT単体ビルド
（`ESP32C6_WIFI=OFF`）では当該ファイル自体がコンパイル対象外に
なるため，このままでは実施91の根治がBTパスに効かず，advisorの
指摘通り「壁1（modem-clock gating）」を新規ハードウェアラウンドで
再発見する形になるところだった。

対策：`esp_shim_modem_icg_init()`の実体を，WiFi/BT両方でリンクされる
共有ファイル`wifi/esp_shim.c`へ移設（非static化）。`esp_wifi_
adapter.c`は呼出し元のまま（externで参照），BT側は`bt/bt_shim.c`の
`esp_shim_bt_clock_init()`から呼ぶ。**実機検証の結果，cold boot
（usb-reset経由）でPHY初期化ハングは再現せず**，事前の懸念通り
この移設が必要だったことを確認した。

### D-1判定基準の結果

| 基準 | 結果 |
|---|---|
| (a) controller init戻り値 | `ESP_OK`（実測ログ`esp_bt_controller_init OK`，heap free=180536） |
| (b) HCI_Reset→Command Complete | 受信バイト列 `04 0e 04 01 03 0c 00`＝packet type EVT(0x04)・event Command Complete(0x0e)・param len 4・num_hci_cmd_pkts 1・opcode 0x0c03(HCI_Reset)・status 0(成功)．**内容一致，正しいHCI応答** |
| (c) 割込みが期待ソースで届く（多重登録トレース） | `esp_os_intr_alloc`が**2回**呼ばれることを確認（source7→CPU線1，source4→CPU線2）．C3のD-2bで踏んだ「単一handle上書き」の地雷を，最初からスロット配列化することで回避（設計通り） |
| (d) ストーム非発生 | HCI疎通完了後1秒間の割込みレート計測＝line1/line2とも**0回/秒**（閾値`>>1000/s`を大幅に下回る） |
| (e) heap健全 | init直後・enable直後・完了後いずれも**180536 Bytes一定**（変化なし＝リーク無し） |

**2ブート再現**：上記結果を独立した2回のリセット（`--before usb-reset`
書込み後の自然起動＝1回目，RTS経由の追加リセット＝2回目）で確認。
出力は`intr trace = 0xa1020704`まで含めて完全一致。

### APM BT経路の確認結果

`esp_bt_controller_enable()`直後・HCI Reset完了後の両時点でHP_APM
M0-M3例外ラッチ（`0x600990CC`+i*0x10，i=0..3）を読み取り，**いずれも
clear**（新規違反なし）であることを確認した。設計書リスク2
（「構造上カバーされているはずだが実測なし」）を実測で解消。

### 遭遇した壁と対処

実装時の想定（設計書）と実機ビルド／実機起動で判明した相違点：

1. **hal submoduleの上流ドリフト（typo/リネーム）が2箇所**：
   - `bt.c`が`#include "nimble/nimble_port_os.h"`（実在しない）を
     無条件includeする。`~/tools/esp-idf-v6.1`の正本を確認したところ
     `nimble/nimble_port_freertos.h`（実在する）が正しい。target側に
     `bt/stub/include/nimble/nimble_port_os.h`（1行の`#include`
     リダイレクトのみ）を新設して吸収。
   - `bt.c`が`npl_os_funcs_init/get/deinit`・`npl_os_mempool_init/
     deinit`・`npl_os_set_controller_npl_info`を呼ぶが，本hal
     submoduleの`npl_os_freertos.c`が実際に定義するシンボル名は
     `npl_freertos_*`（v6.1正本の`bt.c`も`npl_freertos_*`を直接
     呼んでいる＝本snapshotのbt.cだけがドリフトしている）。
     `bt/bt_shim.c`に6関数の1:1橋渡しシムを追加して吸収。
   - いずれも`hal/`は無編集（CLAUDE.md禁則遵守），target側のシムで
     吸収する設計方針をそのまま適用できた。
2. **libbtbb.aのリンク漏れ**（設計書6節1の「btbb相当は不要」という
   判断を訂正）：`hal/components/bt/CMakeLists.txt`のCONFIG_BT_
   CONTROLLER_ENABLED分岐だけを見ると`libble_app`単体に見えるが，
   実際は`esp_phy/CMakeLists.txt`が`CONFIG_SOC_BT_SUPPORTED`時に
   `libbtbb.a`＋`src/btbb_init.c`（`esp_btbb_enable/disable`，bt.cが
   呼ぶ）を別途要求する。実機リンクで`bt_bb_*`シンボル群の未定義
   参照として発覚し，追加した。
3. **os_mempool.h/os_mbuf.hの`SOC_ESP_NIMBLE_CONTROLLER`ガードが
   include順序で無効化される**：`hci_driver_standard.c`と
   `npl_os_freertos.c`が`"soc/soc_caps.h"`（`SOC_ESP_NIMBLE_
   CONTROLLER`の定義元）より前に`"os/os_mbuf.h"`／`"os/os_mempool.h"`
   をincludeするため，本来選ばれるべき`r_`プレフィクス版
   （blob/ROM実体と一致）ではなくplain名版が選択され未定義参照に
   なる。`-include soc/soc_caps.h`を全BTコンパイル単位へ強制する
   ことで解消（`esp_bt.cmake`）。
4. **os_mbuf.c/os_mempool.cを自前でリンクしてはいけない**：当初
   「`r_os_mbuf_copydata`等がnm上"U"（未定義）に見える」ことから
   別途リンクが必要と判断したが，これは誤り——`libble_app.a`が
   自分自身の`os_mbuf.c.o`を既に同梱しており，アーカイブ内member
   間の相互参照を`nm`の素朴な出力だけでは見抜けなかった。自前で
   リンクすると`g_msys_pool_list`の多重定義になる。除去して解決。
5. **リンカスクリプトに2種類の新規blobセクション名が必要**：
   `.high_perf_code_iram1.*`・`.adv_fast_execute_code_iram1.*`
   （`libble_app.a`が要求）に対する配置ルールが`esp32c6.ld`に
   無く，orphan section扱いで`.data`とLMAが重なりリンクエラーに
   なった。既存の`.iram1.*`等と同じ理由・同じ対策（`*(...)`追加）
   で解決（WiFi側には影響しない追加のみ）。
6. **`esp_timer`プール枯渇（実機起動で発覚，最重要の実機限定バグ）**：
   C3旧世代コントローラは`esp_timer_*`の実測要求が1個のみだった
   ため`BT_TIMER_NUM=4`で足りていたが，C6/C5世代は
   `BLE_NPL_USE_ESP_TIMER=1`選択下でnpl_os_freertos.cのcallout機構
   （LLスケジューリング用タイマ）を内部で多用し，`r_ble_controller_
   init()`内部だけで4個を超えるesp_timer_handle_tを要求する。
   プール枯渇時は"bt: esp_timer pool exhausted"に加え，コントローラ
   内部処理が破綻して`r_ble_controller_init failed 257`という形で
   現れた（**ビルドは通るがHW実機でのみ顕在化する**類の不具合の典型
   例）。`BT_TIMER_NUM`を4→16に拡大して解消・2ブート再現で確認。
7. **CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ等，カーネル共通sdkconfig.h
   による間接マクロの後勝ち**：`hal/nuttx/esp32c6/include/sdkconfig.h`
   （全ビルド共通で暗黙include）が`#define CONFIG_ESP_DEFAULT_
   CPU_FREQ_MHZ CONFIG_ESPRESSIF_CPU_FREQ_MHZ`という間接定義を持ち，
   コマンドラインで`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`を直接`-D`しても
   後から上書きされて無効になる。実体の`CONFIG_ESPRESSIF_CPU_FREQ_
   MHZ`側を定義することで解消。

### 未解決の軽微な既知不具合（D-1判定には無関係）

`esp_bt_controller_init`直後のログに文字化け（`I (24) @|...`のような
非ASCII断片）が散発する。実機で機能上のD-1判定基準（a〜e）は全て
満たしており，2ブート再現でも同一パターンで再現するため実害は無いと
判断したが，原因は未特定（コントローラタスクからのETS_PRINTF系出力と
syslogタスクの出力がUSB-Serial/JTAGドライバ上で競合している可能性が
高い）。D-2a（NimBLEホスト，マルチタスク化でさらに出力が増える）で
悪化する可能性があるため，次ラウンドの申し送り事項とする。

### 変更ファイル

- `asp3/target/esp32c6_espidf/esp_bt.cmake`（新規）：BT statement一式
- `asp3/target/esp32c6_espidf/bt/`（新規）：`bt_shim.c`・`bt_cfg.h`・
  `bt.cfg`・`stub/include/platform/os.h`・
  `stub/include/nimble/nimble_port_os.h`
- `asp3/target/esp32c6_espidf/target.cmake`：`ESP32C6_BT`オプション
  追加，WiFi/BT共有shim基盤の切出し（`esp_shim.c`／
  `esp_shim_blobglue.c`／`esp_shim_libc.c`を`(ESP32C6_WIFI OR
  ESP32C6_BT)`ゲートへ）
- `asp3/target/esp32c6_espidf/wifi/esp_shim.c`：
  `esp_shim_modem_icg_init`をesp_wifi_adapter.cから移設（非static
  化・WiFi/BT共有），`esp_shim_enter/exit_critical`（ネストカウンタ
  付きクリティカルセクション）・`esp_shim_sem_get_count`・
  `esp_shim_queue_reset`を追加（BLE実施01のnpl_os_freertos.cが要求，
  C3のbt_shim.c設計を移植），`esp_shim_task_delay`の
  `wifi_taskdelay_capture`呼出しをWiFi限定ガード
- `asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c`：
  `esp_shim_modem_icg_init`のextern参照化（実体は上記へ移設）
- `asp3/target/esp32c6_espidf/esp32c6.ld`：
  `.high_perf_code_iram1.*`・`.adv_fast_execute_code_iram1.*`の
  配置ルール追加（WiFi非影響）
- `asp3/target/esp32c3_espidf/wifi/esp_shim_libc.c`：`printf()`実装
  追加（libble_app.aが直接呼ぶ．C3/C6両方で共有・C3非影響）
- `apps/bt_smoke_c6/`（新規）：`bt_smoke_c6.c`・`.h`・`.cfg`

### 検証

- ビルド：`ESP32C6_BT=ON`でクリーンビルド成功（RAM使用65.77%）。
- 回帰確認：`ESP32C6_WIFI=ON`（`apps/wifi_scan`）の再ビルドが
  引き続き成功することを確認（共有ファイル変更の非破壊を実証）。
- 実機（board C，`14:C1:9F:E0:5A:9C`）：2ブート独立実行で判定基準
  (a)〜(e)全て満足，出力完全一致。

### C5への転移見込み

設計書1.2節で確認済みの通り，C5とC6のコントローラソース差分は
`ble.c`の数行のみ（`ble_priv.h`は完全一致）。今回発見した上流
ドリフト吸収シム（`nimble_port_os.h`・`npl_os_*`橋渡し）・btbb
リンク・リンカスクリプトのセクション名・esp_timerプールサイズは
**C5でも同型の壁になる可能性が高い**（特にesp_timerプール枯渇は
コントローラ内部ロジック起因のためC5の同世代blobでも同様に発生
すると予想される）。C5側は`esp_bt.cmake`を`esp32c5`向けに複製し，
本ラウンドの壁一覧を先読みして一括対応することで，C6よりも
早くD-1へ到達できる見込み。

### board C最終状態

BLE実験終了後，board Cへ`apps/wifi_scan`（`ESP32C6_WIFI=ON`，
`ESP32C6_CONSOLE=usbjtag`）を書き戻した。**`c6_r90_load_test`
（`apps/load_test_c6`＋`ESP32C6_LWIP=ON`＋WPA2認証情報）そのものでは
ない**——`ASP3_EXTRA_COMPILE_DEFS`に埋め込むWIFI_SSID/WIFI_PASSWORDは
リポジトリに平文で存在しない設計（意図的：資格情報を非コミット化）
のため，本セッションには利用可能な認証情報が無かった。`wifi_scan`は
認証情報を要さずWiFi機能全体（esp_wifi_init／PHY較正／スキャン）を
exerciseできるため，本ラウンドの必須要件「正常動作（scanまたは
DHCP疎通）確認」の**scan側**を満たす代替として採用した。

実機確認（cold boot，usb-reset経由・JTAG非経由）：
```
wifi_scan: 14 APs found (err=0)
  [0] <SSID-2G> (rssi=-64 ch=1)
  ...
wifi_scan: done
wifi_scan: RESCAN 15 APs (err=0)  （以後リスキャンも継続して正常動作）
```
実施91のICGアンロック・実施87/88のAPM解除を含むWiFi恒久修正一式が
本ラウンドの共有ファイル変更（`wifi/esp_shim.c`・`esp_wifi_
adapter.c`・`esp32c6.ld`・`target.cmake`）を経ても無傷であることを
実機で確認した（回帰なし）。

**申し送り**：`load_test_c6`（DHCP/TCP/UDP echo込みの本来の恒久
ビルド）へ戻すには，WIFI_SSID="<SSID-2G>"に対応するWPA2
パスワードを保有するセッション／人手で
```
cmake -S asp3/asp3_core -B build/c6_r90_load_test -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=<repo>/asp3/asp3_core/cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c6_espidf \
  -DASP3_TARGET_DIR=<repo>/asp3/target/esp32c6_espidf \
  -DASP3_APPLDIR=<repo>/apps/load_test_c6 -DASP3_APPLNAME=load_test_c6 \
  -DESP32C6_WIFI=ON -DESP32C6_LWIP=ON -DESP32C6_CONSOLE=uart0 \
  -DASP3_EXTRA_COMPILE_DEFS='WIFI_SSID="<SSID-2G>";WIFI_PASSWORD="<要補完>"'
```
をビルド・書込みすること（コンソールは`uart0`／`/dev/ttyUSB1`系，
`wifi_scan`用に書き込んだ今回のusbjtagコンソール設定とは異なる点に
注意）。

**リンク検証（本ラウンド内で実施済み・書込みはしていない）**：上記と
同一コマンドをダミー認証情報（`WIFI_SSID="dummy_verify_ssid"`；
`WIFI_PASSWORD="dummy_verify_pw"`）で`build/verify_load_test_c6_link`
へビルドし，リンクまで成功することを確認した（FLASH 617712B
14.73%・RAM 407512B 96.59%，多重定義／未解決シンボルなし）。
`wifi_scan`回帰は`ESP32C6_LWIP=OFF`構成のみを踏んでいたため，
本ラウンドで`esp_shim_libc.c`へ追加した`printf()`や共有ファイル
一式が，未検証だった`ESP32C6_LWIP=ON`（mbedtls／lwIPを含む本来の
恒久ビルド構成）でも衝突しないことをここで確認できた。検証用
ビルドディレクトリは確認後に削除済み。実際の書込みには本物の
WPA2パスワードが必要（本セッションには無い）。

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

## BLE実施03：C5 D-1（controller init + VHCI疎通）——ビルド/リンクは
達成，実機ブートが未知の壁でBLOCKED（カーネルHRT/systimerストーム）

**日付**：2026-07-14　**対象**：ESP32-C5 board C5#1（`D0:CF:13:F0:A7:44`，
`asp3/target/esp32c5_espidf`）　**目的**：BLE実施01（C6 D-1）の資産・
知見をC5へ転写し，C5でもcontroller init + VHCI疎通を達成する。

### 結論サマリ

- **ソース転写・ビルド・リンクは完全に成功**（後述の適応7件を実施）。
- **実機ブートがD-1到達前にBLOCKED**：`main_task`（bt_smoke_c5）の
  最初の1行すら実行されない＝**カーネル起動シーケンス自体が
  停止する**新規の壁を発見した。`esp_bt_controller_init()`より
  遥かに手前，バナー表示より前の段階のため，**BLE/BTコード自体は
  一度も実行されていない**。判定基準(a)-(e)・APM確認はいずれも
  「到達不能」＝未判定。
- 壁の性質はBT固有ではなく**カーネル共通のHRT（SYSTIMER）割込み
  ハンドラが再帰的に即時再発火し続ける**という新種の現象で，
  厳密な二分実験（後述）により，BT機能そのものではなく**BTビルドの
  何らかの副作用**（現時点では未特定）が引き金と判明した。

### ライブラリ選定（本ラウンドの中心判断）

C6のBLE実施01はhal submodule（esp-hal-3rdparty）の
`controller/esp32c6/bt.c`＋`libble_app.a`をそのまま採用したが，**C5は
IDF v6.1（`~/tools/esp-idf-v6.1`）からbt.c／ble.c／npl_os_freertos.c／
libble_app.a／esp_phy／esp_coexを丸ごと採用した**。判断根拠：

1. **C5のWiFi統合（実施09/10）が実機で確定させた事実**：hal
   submodule版のlibphy.a（v8世代／os_adapter 0x08）は，実機C5（eco2
   シリコン）のPHY RX較正（`phy_iq_est_enable_new`）で収束せず無限
   リトライループへ入る（実施09で逆アセンブル差分まで確認済み）。
   IDF v6.1のlibphy.a（v9世代／os_adapter 0x09）は同一チップで収束
   する（実施10で実機確認済み）。
2. **BTコントローラも同じ経路を通る**：`controller/esp32c5/bt.c`は
   `esp_bt_controller_enable()`の中で`esp_phy_enable(PHY_MODEM_BT)`
   を呼び，WiFiと**全く同一のlibphy.a／`register_chipv7_phy`経路**を
   通る（hal版・v6.1版どちらのbt.cも同じ呼び出しをソースレベルで
   確認済み）。hal世代のBT blob（`libble_app.a`）とv6.1世代のPHY
   blobを手で混ぜる「ハイブリッド」構成は，Espressifが実際には
   検証していないblob世代ABI境界を新規に作ることになり（advisor
   レビュー指摘），WiFiが実機で踏んだ「eco2で収束しないPHY較正
   ループ」をBTでも素朴に再現するリスクが高いと判断し，**PHY/coex
   と同じIDF v6.1 matched setへBTも統一した**。
3. **副次的な発見**：IDF v6.1の`controller/esp32c5/bt.c`は，hal
   submodule版（`"platform/os.h"`のesp_os_*経由．C6のBLE実施01が
   採った設計）とは異なり，**C3の旧世代`controller/esp32c3/bt.c`と
   同じプログラミングモデル**（`xTaskCreatePinnedToCore`/
   `vTaskDelete`を直接呼び，割込みは標準`esp_intr_alloc`/
   `esp_intr_free`APIを直接呼ぶ）を採用している。そのため：
   - `bt/stub/include`は**C6のような専用`platform/os.h`を新設せず，
     C3の`bt/stub/include`一式（`freertos/*.h`＋`esp_partition.h`）を
     インクルードパスでそのまま再利用する**（新規コピー無し）。
   - C6のBLE実施01で必要だった上流ドリフト吸収シム2件
     （`nimble_port_os.h`リダイレクト・`npl_os_*`→`npl_freertos_*`
     橋渡し）は，**v6.1のソースツリー自体に当該ドリフトが存在しない
     ため不要**（`bt.c`は`nimble/nimble_port_freertos.h`を正しく
     include・`npl_freertos_funcs_init/get`を直接呼ぶ）。
   - hal版とv6.1版の`bt.c`のソース差分は，実際に読むと
     「SPDX年号とUHCIログ出力関連の数行差分のみ」という設計書
     （`docs/ble-c5c6-plan.md`）の当初評価より大きく，sleep-retention
     ／`esp_pm_register_skip_light_sleep_callback`関連のコードが
     v6.1側にのみ追加で存在した。ただしこれらは全て
     `CONFIG_BT_LE_SLEEP_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE`
     （本ビルドは両方未定義）でコンパイル対象外になるため実害は
     無いことを確認済み。

### C6のBLE実施01から転写した知見・先読み適用したもの

- `BT_TIMER_NUM`＝最初から16（4→16への事後拡大を経験済みのC6の
  教訓を先読み適用）。
- 割込み多重登録耐性（スロット配列化，2枠）を最初から採用（S3/C3の
  教訓）。ただしC5はC6の"PLIC_MX"ではなく**標準RISC-V CLIC**
  （`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`の
  `set_intr_wrapper`と同一のINTMTX＋CLIC規約）を使うため，
  `bt/bt_shim.c`のCLIC操作コードは**C6のPLIC_MXコードではなくC5
  WiFi shimのCLICコードを土台に新規移植**した。
- 多重登録トレース計装（`BT_INTR_TRACE_REG=0x600B101C`，LP_AON
  STORE7相当）はC6と同一アドレスを採用——C5のLP_AON STORE1-4は
  既に別用途（実施35のRTC_SLOW_CLK_CAL・実施41の診断）で使用中
  であることを確認した上で，未使用のSTORE7を選んだ（衝突なし）。
- APM確認用のHP_APM M0-M3例外ラッチアドレス（`0x600990CC`+i*0x10）
  はC6と同一レイアウト——`target_kernel_impl.c`の実施42（C5版APM
  恒久修正，`esp32c5_r42_apm_unblock()`）のM1アドレス実測
  （`0x600990DC`）と整合することを確認した上で採用した。

### 実装で新規に必要だった適応（C6には無かった壁）

1. **`os_mempool.c`を自前でリンクする必要があった**：C6のBLE実施01
   は「os_mbuf.c/os_mempool.cは自前で持たない（libble_app.aが自分の
   os_mbuf.c.oを同梱）」という教訓を残していたが，**C5のIDF v6.1
   blobではこの前提が成立しない**——`os_memblock_get/put/from`・
   `os_mempool_init/clear/unregister/flags_set`等が未解決のまま
   （plain名で参照）リンクエラーになり，`bt/porting/mem/os_mempool.c`
   を自前でリンクして解決した（`os_mbuf.c`は引き続き不要）。
2. **`esp_bit_defs.h`の強制include**：`os_mempool.c`が`BIT()`マクロを
   使うが自ファイルではincludeせず（実ESP-IDFのビルドシステムが
   暗黙にグローバル可視にする前提），未定義のまま関数呼出しと誤解釈
   されてリンクエラーになった。`-include esp_bit_defs.h`をBT全体へ
   強制適用して解決（他のBTソースは別経路で既にesp_bit_defs.hが
   見えているため無害）。
3. **`CONFIG_XTAL_FREQ`/`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`の直接定義が
   必要**：C6のesp_bt.cmakeは「これらはhal/nuttx/esp32c6/include/
   sdkconfig.hの間接定義に頼れるため直接-Dは無効化される」という
   罠を記録していたが，**C5にはそもそもNuttXポートのsdkconfig.h一式
   が存在しない**（C5 WiFi統合が既に確認済みの事実）ため，この前提が
   成立しない。C6と逆に，**素直に`-DCONFIG_XTAL_FREQ=48
   -DCONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`が必要かつ有効**（実施32/34
   確定のC5実測値＝48MHz XTAL・BBPLL経由240MHz）。
4. **`esp32c5.ld`への2セクション追加が必要**（C6と同型の壁）：
   `libble_app.a`が要求する`.high_perf_code_iram1.*`・
   `.adv_fast_execute_code_iram1.*`の配置ルールが無いとorphan
   section扱いとなり`.data`とLMAが重なりリンクエラーになった。C6と
   同じ対策（`*(...)`追加）で解決（WiFi側の`.iram1`等のセクション
   一覧には影響しない追加のみ）。
5. **`wifi/esp_shim.c`への4関数追加が必要**：C5のesp_shim.cは
   WiFi専用として書かれていたため，C3/C6のbt/stub freertosシムが
   要求する`esp_shim_enter_critical`/`esp_shim_exit_critical`
   （ネスト対応クリティカルセクション）・`esp_shim_sem_get_count`・
   `esp_shim_queue_reset`が未実装だった。C3/C6と同一実装（chip非依存
   ロジック）を追加した。
6. **`esp_shim_modem_icg_init`の共有ファイルへの移設**：実施13の
   PMU ICGアンロック（C6の実施91相当，C5固有の実機JTAG確認済み
   恒久修正）は`wifi/esp_wifi_adapter.c`のstatic関数として実装されて
   いたため，BT単体ビルド（WiFi OFF）ではリンクされない。C6の
   BLE実施01と同じ理由・同じ対策で`wifi/esp_shim.c`（WiFi/BT共有）
   へ非static化して移設した（中身・呼出し箇所は無変更，WiFi回帰
   ビルドで無破壊を確認済み）。
7. **target.cmakeのゲート拡張**：C3/C6と同じパターンで，shim基盤
   （`wifi/esp_shim.c`・`esp_shim_blobglue.c`・`esp_shim_libc.c`）の
   ゲートを`ESP32C5_WIFI`単独から`(ESP32C5_WIFI OR ESP32C5_BT)`へ
   拡張し，`ESP32C5_BT`オプション（`ESP32C5_WIFI`との同時ON禁止＝
   C3/C6の前例踏襲）を新設した。

### ビルド検証

- `ESP32C5_BT=ON`でクリーンビルド成功：FLASH 326832B（7.79%），
  RAM 278268B（70.77%）。
- 回帰確認：`ESP32C5_WIFI=ON`（`apps/wifi_scan`）の再ビルドが引き続き
  成功することを確認（FLASH 498368B 11.88%・RAM 299832B 76.25%，
  従来と同水準——共有ファイル変更の非破壊を実証）。

### 実機（C5#1）：カーネル起動がD-1到達前にBLOCKED

**現象**：`asp_flash.bin`（ESP32C5_BT版）をC5#1へフル4MB書込み後，
UARTコンソール（`b04e3bcf…`，115200bps，console=uart0既定）を
複数回（esptool `--after watchdog-reset`／pyserial open自体が
CP2102Nブリッジ経由でチップをリセットする既知挙動，実施39）独立
リセットして観測した結果，**カーネルバナー（"TOPPERS/ASP3 Kernel"）
すら一度も表示されない**。表示されるのは`"no time event is
processed in hrt interrupt."`（カーネル共通の`kernel/time_event.c`
`signal_time()`が発するsyslogメッセージ．設計上は「処理すべき
タイムイベントが無い場合に頻発しうる」正常だが冗長な診断）のみが
**単一種のメッセージとして無限に連続出力される**（45秒間の観測で
一度も別の内容が現れず，`sort -u`で1種類のみと確認．デバイス発
出力は完全にこのメッセージのみ）。

**再現性**：独立した3回のフラッシュ書込み＋リセット（各回esptool
フル4MB書込み→pyserial openによるチップリセット→最大45秒観測）で
毎回同一現象を確認。

**A/B対照実験（決定的）**：WiFi/BT両方OFFの最小ベースライン
（`asp3/asp3_core/sample/sample1`，FLASH 25984B 0.62%・RAM 12848B
3.27%）を同一C5#1・同一手順でビルド・書込み・観測した結果，
**バナー・`task1 is running (NNN).`の周期出力とも正常**（1秒周期で
確認）。`"no time event..."`メッセージも時折**混在して**現れるが
（`signal_time()`の設計通りの正常な頻発），task1の進行を妨げない。
＝**この壁はBT機能そのものが原因ではなく，BTビルド固有の何らかの
副作用**（WiFi/BT共有shim基盤の変更でないことは上記回帰確認で
別途裏付け済み）。

**JTAG診断（OpenOCD，`adapter serial D0:CF:13:F0:A7:44`）**：
- Halt時のPCが常に`0x420052ac`〜`0x420052b0`（`dispatcher_1`/
  `dispatcher_2`＝カーネルのディスパッチ処理．3回の独立
  halt/resume/haltでも不変）に張り付く。`mepc=0x42007812`
  （`irc_begin_int_demote`＝割込み入口処理）。`mcause=0x88000020`
  （interrupt bit立＋cause=32＝CLIC内部線32＝ASP3 INTNO16。
  `target_timer.h`のコメント「割込み番号（Wi-Fi shimの線1-15予約
  により線16へ退避）」の通り，**これはBT/WiFiの動的割込みではなく
  カーネル自身のHRT（SYSTIMER）タイマ割込み**）。
- `SYSTIMER_INT_ST_REG`（`0x6000A070`）が**クリア呼出し
  （`target_hrt_handler`内の`systimer_ll_clear_alarm_int`）後も
  1のまま**——CLIC line 32のpendingバイト（`0x20801080`+0の
  1バイト目）も1のまま。これがCLIC level割込みの即時再発火の
  直接的な状態証拠。
- **反証実験1**：`LOGTASK_STACK_SIZE`を1024→4096（4倍）へ拡大して
  再ビルド・再現確認したが**症状不変**（同一メッセージ・同一PC）。
  ＝ログタスクのスタックオーバーフロー説は反証。
- **反証実験2**：`ESP_SHIM_HEAP_SIZE`を192KB→32KBへ縮小して
  再ビルド（RAM使用率70.77%→29.10%）・再現確認したが**症状不変**
  （PC/mcauseとも完全一致，SPのみRAMレイアウト変化に応じて平行
  移動）。＝RAM/ヒープ量・静的データ配置オーバーラップ説は反証
  （SP位置以外の挙動が量に非依存＝タイミング/シーケンス起因の
  可能性が高い）。
- ROM ld（`esp32c5.rom.phy.ld`・`esp32c5.rom.coexist.ld`）由来の
  シンボル衝突は，(i) これらは既にWiFiビルドで同一ファイルが実績
  稼働中（実施47等，600秒級負荷試験で健全動作を確認済みの資産）で
  あること，(ii) リンクが「multiple definition」エラー無く成功して
  いること（`symbol = 0xADDR;`形式の絶対割当てで衝突があれば必ず
  エラーになる），の両方から否定的（棄却はしないが優先度低）。

**申し送り（次段の最有力仮説）**：`kernel/time_event.c`の
`set_hrt_event()`は，保留中のタイムイベントが無い場合
（`LAST_INDEX()==0`，カーネル起動直後で本来正常に通る経路）に
`target_hrt_set_event(HRTCNT_BOUND)`（`HRTCNT_BOUND=4000000002U`
μs≈66.7分後）を呼ぶ設計になっている。ベースラインでは最初の
実タスク（`task1`の`tslp_tsk`等）が実際のタイムイベントを即座に
登録するため，この「イベント無し」経路は起動直後の一瞬しか通らない
（多くの場合，観測ウィンドウの外側）。**BTビルドでは`main_task`が
一度も走らない＝実イベントが永久に登録されないため，カーネルは
`HRTCNT_BOUND`経路に恒久的に留まる**。反証実験1/2（スタック・
ヒープ量に非依存）と符合する仮説は，**この経路自体（またはその
実行タイミング）に，通常は他のタスクの実イベントによって即座に
上書きされるため顕在化しない潜在バグがある**というもの——
`esp32c5_systimer_read()`の初回読出しタイミング（起動直後の
XTAL/PLL安定化ウィンドウ等）が絡む可能性を含め，次段の実機JTAG
ラウンドで`target_hrt_set_event()`実行直後に生カウンタ値・
`target_val`レジスタを直接読み，計算結果が本当に66分後を指して
いるかを検証すべき（本ラウンドでは時間予算の制約によりここまで
未実施）。**D-1のBT機能自体（`esp_bt_controller_init`以降）は
一度も実行されておらず，(a)-(e)・APM確認は全て「到達不能」で
評価不能**——これはBTコントローラ設計・ライブラリ選定の問題では
なく，カーネル共通コードとBTビルドの静的footprint／リンク結果の
何らかの相互作用によるカーネル起動阻害である。

### 変更ファイル

- `asp3/target/esp32c5_espidf/esp_bt.cmake`（新規）：BT statement一式
  （IDF v6.1由来のbt/phy/coexソース・include・link，hal由来の
  support層）
- `asp3/target/esp32c5_espidf/bt/`（新規）：`bt_shim.c`（CLIC対応版，
  C3型直接FreeRTOS shim）・`bt_cfg.h`・`bt.cfg`（chip非依存，C6から
  無変更移植）。**C5独自の`bt/stub/include`は作らない**——C3の
  `bt/stub/include`をesp_bt.cmakeのインクルードパスでそのまま参照。
- `asp3/target/esp32c5_espidf/target.cmake`：`ESP32C5_BT`オプション
  追加，WiFi/BT共有shim基盤の切出し（`(ESP32C5_WIFI OR
  ESP32C5_BT)`ゲートへ拡張）
- `asp3/target/esp32c5_espidf/wifi/esp_shim.c`：
  `esp_shim_enter/exit_critical`・`esp_shim_sem_get_count`・
  `esp_shim_queue_reset`を追加（C3/C6と同一実装）。
  `esp_shim_modem_icg_init`を`wifi/esp_wifi_adapter.c`から移設
  （非static化・WiFi/BT共有，中身・呼出し箇所は無変更）。
- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：
  `esp_shim_modem_icg_init`のextern参照化（実体は上記へ移設）。
- `asp3/target/esp32c5_espidf/esp32c5.ld`：
  `.high_perf_code_iram1.*`・`.adv_fast_execute_code_iram1.*`の配置
  ルール追加（WiFi非影響，回帰確認済み）。
- `apps/bt_smoke_c5/`（新規）：`bt_smoke_c5.c`・`.h`・`.cfg`
  （`apps/bt_smoke_c6`から移植，C5用アドレス・文字列調整のみ）。

### C5#1最終状態

BLE実験終了後，C5#1には**本ラウンドの`ESP32C5_BT=ON`ビルド
（`build/bt_smoke_c5/asp_flash.bin`，`ESP_SHIM_HEAP_SIZE`既定値192KB・
`LOGTASK_STACK_SIZE`既定値1024）をフル4MB `write-flash 0x0`済み**の
まま残置した（起動がD-1未到達でBLOCKEDのため，`load_test_c5_r47`
（実施47の最終状態）への復元は行っていない——次段の調査を同一条件
で即座に継続できるようにする判断。書き戻しコマンドは実施47節参照，
必要であれば`docs/c5-bringup.md`記載の手順でいつでも復元可能）。

**申し送り**：`load_test_c5_r47`（WiFi/TCP/UDP動作の恒久ビルド）へ
戻すには，
```
esptool --chip esp32c5 --port <C5#1のJTAG by-idパス> \
  --before usb-reset --after watchdog-reset write-flash 0x0 \
  build/load_test_c5_r47/asp_flash.bin
```
（ビルドディレクトリが残っていない場合は`docs/c5-bringup.md`実施47の
cmakeコマンドで再ビルドが必要）。

## BLE実施02：C6 D-2a（NimBLE host sync）＋D-2b（接続可能アドバタイズ）

**日付**：2026-07-14　**対象**：ESP32-C6 board C（`14:C1:9F:E0:5A:9C`，
`asp3/target/esp32c6_espidf`）　**目的**：BLE実施01のD-1（controller init
＋VHCI疎通）の上にNimBLEホストを載せてsync到達（D-2a）を狙い，届けば
接続可能アドバタイズ（D-2b）まで進める。

### 結論（先出し）

- **D-2a＝達成**：`g_ble_sync_done`＝ble_hs syncコールバック到達を
  LP_AON STORE0マーカ（`0x600B1000`=`0x5ade51c0`）で2ブート独立に確認。
- **D-2b＝達成**：`ble_gap_adv_start`が**rc=0**で完走（STORE3
  `0x600B100C`=`0xad000000`），割込みレートは線1/線2とも正常域（線2で
  20秒あたり数百〜数千＝十〜数百/秒オーダー，C3のストーム時
  ~33万/秒とは4〜5桁違う），host側リセット（`ble_hs_cfg.reset_cb`）は
  0回，**ホストhci0の`bluetoothctl scan le`で`ASP3-C6-BLE`
  （`14:C1:9F:E0:5A:9C`，RSSI -82）を実機検出**——C6 BLE史上初の
  over-the-air adv到達。
- 2ブートとも上記が完全再現。D-1で先読み実装済みだったsource多重登録
  対策（スロット配列，実施01）がNimBLE host＋adv負荷下でも機能し，
  C3が実施01→(1)(o)で踏んだストームは**再発しなかった**。
- 副産物：本ラウンドで**GCC 14.2.0の既定挙動変化
  （implicit-function-declaration／int-to-pointer変換がハードエラー）
  により，D-1（`bt_smoke_c6`）・WiFi（`wifi_scan`）とも「無改造では
  ビルド不能」という状態を検出・修正した**（後述）。これは本ラウンドの
  変更が原因ではなく，環境のツールチェーン差分による既存コードの
  潜在不具合の顕在化——同じ状態のまま気付かずにいた場合，次に誰かが
  このリポジトリを別環境でビルドした瞬間に同じ壁を踏む。

### 設計：C3のD-2a/D-2bとの決定的な違い（★最重要）

C3（旧世代コントローラ．`SOC_ESP_NIMBLE_CONTROLLER`非該当）は
`esp_nimble_hci.c`＋`hci_esp_ipc_legacy.c`（LEGACY VHCI経由でD-1の
`esp_vhci_host_*`へブリッジ）を使う。C6/C5（新世代．
`SOC_ESP_NIMBLE_CONTROLLER=1`）はこの経路が**存在しない**：
`hal/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c`
の`esp_nimble_init()`内部で`esp_nimble_hci_init()`の呼出し自体が
`#if !SOC_ESP_NIMBLE_CONTROLLER || !CONFIG_BT_CONTROLLER_ENABLED`で
コンパイルアウトされるため，C3方式をそのまま移植しても呼ばれない。

実際に必要なトランスポートは**新HCIトランスポート**（D-1で既にリンク
済みの`hci_transport.c`に加え，`porting/transport/driver/vhci/
hci_driver_nimble.c`＋`host/nimble/nimble/nimble/transport/esp_ipc/src/
hci_esp_ipc.c`）で，これはblob内部の`r_ble_hci_trans_*`／
`ble_transport_to_hs_*`へ直結する。D-1が使っていた
`hci_driver_standard.c`とは`hci_driver_vhci_ops`という同一シンボルを
取り合う関係＝二者択一（同時リンク不可）で，`ESP32C6_BT_NIMBLE`が
ONのときだけ`hci_driver_nimble.c`＋`hci_esp_ipc.c`へ差し替える。

アプリ配線は`nimble_port_init()`（内部でコントローラをデフォルト設定
で二重初期化してしまう）を避け，C3と同じ設計判断——
`esp_bt_controller_init(&cfg)`（D-1と同じ`BT_CONTROLLER_INIT_CONFIG_
DEFAULT()`マクロ）→`esp_bt_controller_enable(BLE)`→**`esp_nimble_init()`**
（ホストのみ．C6では上記の理由で`esp_nimble_hci_init()`は呼ばれない＝
新トランスポートは`ble_transport_ll_init()`経由で別途配線される）→
`ble_hs_cfg.sync_cb`／`reset_cb`登録→`nimble_port_freertos_init()`。

`CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE`はC6では**0にすることが必須**
（advisorレビューで指摘・実装前に確定）：単なるビルド選択フラグでは
なく，`ble_hs_hci.c`／`ble_hs_mbuf.c`のmbufヘッダ余白計算
（`BLE_HS_CTRL_DATA_HDR_SZ`を足すか否か）を分岐する——実際の
トランスポートと不整合だと実行時にバッファオフセットずれを起こし
うる。C3のbt_nimble_config.hはこれを1にしているため**流用不可**と
判断し，C6専用の`bt/stub/include/bt_nimble_config.h`を新規に書いた。

### 実装した変更

#### 新規ファイル

- `apps/ble_host_smoke_c6/`：`ble_host_smoke_c6.c`/`.h`/`.cfg`
  （`apps/ble_host_smoke`のC3版を土台に，C6の新世代cfg構造体
  （`BT_CONTROLLER_INIT_CONFIG_DEFAULT()`マクロ使用．C3の手書き構造体
  とは形状が別）・LP_AON STOREマーカ番地（後述）・デバイス名
  `ASP3-C6-BLE`へ調整。C3のHRT/SYSTIMER凍結検証プローブ（CPU飽和仮説は
  C3側で既に決着済み）は持ち込まず，割込みレート監視タスク
  （`storm_monitor_task`，`esp_shim_int_count[1]/[2]`をLP_AON STORE4/5
  へ200ms周期でミラー）のみ最小限で追加）。
- `asp3/target/esp32c6_espidf/bt/stub/include/bt_nimble_config.h`
  （新規．C3版を流用せず独自作成——理由は上記「決定的な違い」節，
  ファイル冒頭にも同内容のコメントを記載）。
- `asp3/target/esp32c6_espidf/bt/stub/include/npl_os_bridge.h`
  （新規．GCC14.2.0対応．後述「ツールチェーン差分」節）。

#### `asp3/target/esp32c6_espidf/esp_bt.cmake`

- `ESP32C6_BT_NIMBLE`オプション追加（`ASP3_APPLNAME==ble_host_smoke_c6`
  で自動ON．D-1の`bt_smoke_c6`は影響なし）。判定はファイル冒頭で先出し
  （「2. ソースファイル」節のhci_driver二者択一に使うため）。
- NimBLEホスト本体のソース集合はC3のesp_bt.cmakeのD-2節と**ほぼ同一
  トリム**（`ble_svc_gap/gatt`のみ採用．ans/bas/dis/hr/htp/ias/ipss/
  lls/prox/cts/tps/hid/sps/cte/ras等の他サービス，
  `ble_store_config/nvs`（永続ボンディング），`ble_cs`/`ble_ead`/
  `ble_aes_ccm`/`ble_gattc_cache*`/`ble_eatt`（新機能）は不採用——
  sync/adv到達には不要）。C3との違いは上記の通りトランスポート層のみ。
- `MYNEWT_VAL_BLE_SM_LEGACY/SC=0`でble_sm\*.cをnear-empty化し
  mbedTLS/tinycryptのリンクを回避（C3のD-2aと同じ判断．sync/adv単体に
  暗号は不要）。
- `CONFIG_BT_CONTROLLER_ONLY=1`をD-1限定（`if(NOT ESP32C6_BT_NIMBLE)`）
  へ変更（★advisorレビュー指摘）。実ESP-IDFのKconfigでは
  `CONTROLLER_ONLY`と`NIMBLE_ENABLED`は排他選択であり，両方1のまま
  動かすのは未検証の組合せ。唯一発見できた参照箇所
  （`hci_transport.c`のACL rxガード）は`NIMBLE_ENABLED && (ROLE_
  CENTRAL||ROLE_PERIPHERAL)`のORで別途真になり無害と確認済みだが，
  C6実機のBLE初回結果を「未検証の組合せ」の疑いから切り離すため
  原則通りに分離した。
- `TRUE=1`／`BT_HCI_LOG_INCLUDED=0`を`ESP32C6_BT_NIMBLE`限定で追加
  （後述「壁」節）。
- `bt_nimble_config.h`にCONFIG_BT_NIMBLE_GATT_SERVER=1ほか，
  GATT_SERVER=1化で新規に直接参照されるようになったCONFIG_BT_NIMBLE_*
  一式を追加（後述「壁」節）。

#### 共有ファイルの変更（C3/C6双方に影響．strictly additive）

`asp3/target/esp32c3_espidf/wifi/esp_shim_cfg.h`・`esp_shim.cfg`：
NimBLEホスト用の静的プール拡張ゲート（`#ifdef TOPPERS_ESP32C3_BT_
NIMBLE`）を`#if defined(TOPPERS_ESP32C3_BT_NIMBLE) ||
defined(TOPPERS_ESP32C6_BT_NIMBLE)`へOR拡張。SEM24→28／MTX8→12／
DTQ4→8／TSK6→8（C3のD-2a節が既に検証済みの値をそのまま流用）。C3の
既存条件・値は一切変更していない（regression確認：`bt_smoke_c6_regress`
のRAM使用率がBLE実施01の記録値`65.77%`と完全一致することで，D-1側の
非回帰も確認済み）。

### ★ツールチェーン差分の発見と対処（GCC 14.2.0）

本ラウンドの実機ビルドで，**BLE実施01の`bt_smoke_c6`（無改造）と
`wifi_scan`（無改造）が現在の環境ではどちらもビルド不能**であることが
判明した。原因はGCC 14.2.0が`implicit-function-declaration`／
`int-to-pointer`変換を既定でハードエラーにする挙動変更（実施01時点の
ツールチェーンでは警告どまりで実害が無かった）。hal/（submodule）は
編集できないため，target側でプロトタイプ／CONFIG値を補い
`-include`で強制する既存パターン（esp_intr_alloc.h等）をそのまま
踏襲して以下を追加した（すべて追加のみ，既存動作を変更しない）：

| 症状（実機ビルドで発覚順） | 原因 | 対処 |
|---|---|---|
| `bt.c`の`npl_os_funcs_init/get/deinit`等5関数が暗黙宣言エラー | 実体は`bt_shim.c`のブリッジだがプロトタイプ無し（実施01からの既知の上流ドリフト，ツールチェーン差でエラー化） | 新規`npl_os_bridge.h`をD-1区間から`-include`（D-1/D-2a共通） |
| `npl_os_freertos.c`の`esp_timer_is_active`/`esp_timer_get_expiry_time`が暗黙宣言エラー | `esp_timer.h`は`asp3/target/esp32c3_espidf/hal_stub/include/esp_timer.h`（WiFi統合時の簡略スタブ）が本物より先にインクルードパスで見つかり，この2関数だけ未宣言 | 同じ`npl_os_bridge.h`へ2関数のプロトタイプを追加（スタブ自体は非改変） |
| `esp_phy/src/phy_init.c`の`_lock_acquire`/`_lock_release`が暗黙宣言エラー（**D-1・WiFi共通**） | `sys/lock.h`スタブ（`hal_stub/include/sys/lock.h`）が`_lock_t`型のみ供給しプロトタイプ無し．実体は`esp_shim_libc.c`に既存 | スタブへ10関数のプロトタイプ追加（`_lock_init/close/acquire/release`系） |
| mbedtls `bignum.c`の`fgets`，`ctr_drbg.c`の`setbuf`が暗黙宣言エラー（**WiFiのみ**） | `hal_stub/include/stdio.h`に無い（mbedtlsのMBEDTLS_FS_IO経路．`setbuf`は既に実体があったが本ラウンドで発覚するまでプロトタイプが無かった） | `fgets`は実体＋プロトタイプを新規追加（常に失敗を返すスタブ．他のfopen系と同型）．`setbuf`は既存実体にプロトタイプのみ追加 |
| wpa_supplicant等の`bzero`/`strchr`/`strncpy`/`strncat`/`strrchr`/`strspn`/`strcspn`/`qsort`/`strdup`/`strlcpy`/`strcpy`/`strcat`が暗黙宣言エラー（**WiFiのみ**） | `hal_stub/include/string.h`に無い | ROM提供（`esp32c6.rom.libc.ld`等，既存ldに実体あり）はプロトタイプのみ追加。`bzero`のみROM非提供のため`esp_shim_libc.c`へ`memset`委譲の実体を新規追加 |
| `nimble_port.c`の`#if (BT_HCI_LOG_INCLUDED == TRUE)`が，定義元`bt_common.h`のinclude（同じ関数内でこの後に来る）より前に評価され，存在しない`hci_log/bt_hci_log.h`をincludeしようとしてfatal error（**D-2aのみ**） | `TRUE`はstdbool.hの`true`ではなく`bt_common.h`独自定義（大文字）．最小includeチェーンでは評価時点でTRUEもBT_HCI_LOG_INCLUDEDも未定義＝共に0＝`0==0`＝真という上流の順序バグ | `-DTRUE=1 -DBT_HCI_LOG_INCLUDED=0`をESP32C6_BT_NIMBLE限定で追加（`0==1`＝偽へ強制．後段でbt_common.hが同じ値へ再定義するため無矛盾） |
| `ble_gattc.c`内`ble_gattc_process_status`が定義側（`MYNEWT_VAL(BLE_GATTS)\|\|MYNEWT_VAL(BLE_GATTC)`）で不在なのに使用側（`MYNEWT_VAL(BLE_GATTS)`単独）で存在という不整合でimplicit宣言エラー（**D-2aのみ**） | `CONFIG_BT_NIMBLE_GATT_SERVER`未定義＝`esp_nimble_cfg.h`が`MYNEWT_VAL_BLE_GATTS`を0にフォールバックするはずが，実測では使用側ブロックが生き残っていた（根本原因は完全には特定し切れていない——esp_nimble_cfg.h以外の設定経路が介在している可能性が残るが，実害を止める対処を優先） | `CONFIG_BT_NIMBLE_GATT_SERVER=1`を明示（本ビルドは実際に`ble_gatts.c`等をリンクしており矛盾しない選択） |
| `esp_nimble_cfg.h`が`#ifndef`フォールバック無しで直接参照する残りのCONFIG_BT_NIMBLE_\*（`ATT_MAX_PREP_ENTRIES`等9個） | GATT_SERVER=1化で到達する分岐が増えた | 実ESP-IDFのKconfig既定値相当を`bt_nimble_config.h`へ追加 |

**非回帰の確認**：`bt_smoke_c6_regress`（D-1）は全修正後もRAM使用率
`65.77%`（実施01記録値と完全一致）で0エラービルド。`wifi_scan`は
後述の通り実機で14 APs検出（実施01記録値と同数）を確認——検出数の
完全一致は，これらの追加が純粋にコンパイル可否のみに作用し
（プロトタイプ宣言の追加はコード生成に影響しない），実行時挙動を
一切変えていないことの独立した実測的裏付けになっている。

### D-2a判定基準の結果（2ブート独立）

sync到達は`g_ble_sync_done`（グローバル）／LP_AON STORE0
（`0x600B1000`，usb-reset生存）の両方で観測。JTAGはadv開始前
（RF活動前）なら安全だが，syslog出力がHRTの正常系ログ
（後述）で輻輳するため，本ラウンドは**esptool `--no-stub read-mem`
（JTAG不要）でSTORE系レジスタを読む方式**を主とした
（advの実RF活動下ではJTAGが死ぬ可能性があるというC3の教訓を踏まえ，
最初からJTAG非依存の計装で統一）。

| 指標 | boot1（syslog経由） | boot2（20秒run，STORE経由） |
|---|---|---|
| sync到達 | syslog: "ble_host_smoke_c6: ble_hs SYNC, host up"（暗黙。"advertising started"の出力自体がon_sync→start_advertisingの呼出し連鎖の証拠） | STORE0=`0x5ade51c0`（一致） |
| host reset回数 | 未計測 | STORE6=`0x00000000`（0回＝ble_hsは一度もresetされていない） |

（boot1はsyslogバッファ輻輳で個々のログ行が欠落したため，boot2以降は
STORE系の直接読出しへ切替えた。boot1でも"advertising started"という
adv成功メッセージ自体がsync到達の動かぬ証拠——on_syncからのみ到達する
経路のため。）

### D-2b判定基準の結果（2ブート独立，判定基準は実施前に固定）

判定基準：①`ble_gap_adv_start`のrc　②割込みレート（線1/線2，閾値
`>>1000/s`＝ストーム域）　③ホストhci0での実機検出，の3点を独立事実
として記録する（advisorレビュー指摘：rc=0かつレート正常でも
hci0未検出はC6のdeaf-RX/TX非放射歴からは「起こりうる正当な結果」で
あり，機械的に「adv失敗」と混同しないこと）。

| 指標 | boot C（~20秒以上run，時間不明） | boot D（20秒固定run） |
|---|---|---|
| adv attemptマーカ(STORE2) | `0x0ade5000`（一致） | `0x0ade5000`（一致） |
| adv-return(STORE3) | `0xad000000`＝**rc=0** | `0xad000000`＝**rc=0** |
| 割込みレート・線1(STORE4) | 0 | 0 |
| 割込みレート・線2(STORE5) | 3897（推定20秒弱で約195/秒） | 936（20秒で約47/秒） |
| host reset回数(STORE6) | 0 | 0 |
| 多重登録トレース(STORE7) | `0xa1020704`（nalloc=2,src1=7,src2=4．実施01のD-1結果と完全一致） | `0xa1020704`（同左） |
| ①rc | **0** | **0** |
| ②割込みレート | 線1=0/s・線2=47〜195/s＝**正常域**（C3のストーム時~33万/秒とは4〜5桁の差） | 同左 |

**③hci0実機検出**：ホストの`bluetoothctl scan`で
`[NEW] Device 14:C1:9F:E0:5A:9C ASP3-C6-BLE`を確認（RSSI -82）。
同時に`ASP3-C3-BLE`（`60:55:F9:57:C2:60`，別セッションの既存advertise
状態）も検出できており，hci0スキャン自体の陽性対照としても機能した。

**★判定＝D-2a達成・D-2b達成**。実施01がD-1で先読みしていた
source多重登録対策（S3/C3の教訓＝スロット配列化）が，NimBLEホスト＋
実際のadv RF活動という初めての負荷下でも機能し，C3が実施01→(1)(o)で
辿った「単一handle上書き→ストーム」の再発を防いだ——事前の予防設計が
実機で実証された。

**注**：線1(source7)が2ブートとも0のまま（線2(source4)のみ活動）は
未解明（実害無し・rc=0/adv検出済みのため深追いしていない）。C3の
D-1では両線とも0（HCI疎通のみでadv無し），本ラウンドはadv活動下での
初観測のため単純比較はできない。C5移植時に同じ非対称が出るか要確認。

### 遭遇した壁と対処（まとめ）

1. **GCC14.2.0によるD-1・WiFiの潜在ビルド不能化**（上記ツールチェーン
   節，最大の想定外）。
2. **npl_os_\*ブリッジ関数・esp_timer 2関数のプロトタイプ欠落**
   （同上）。
3. **`_lock_acquire`/`_lock_release`欠落**（D-1・WiFi共通）。
4. **mbedtls/wpa_supplicantのlibc関数群の欠落**（WiFi再ビルド限定）。
5. **`BT_HCI_LOG_INCLUDED`評価順序バグ**（D-2a限定）。
6. **GATT_SERVER設定の不整合**（D-2a限定）。
7. **CONFIG_BT_CONTROLLER_ONLYとNIMBLE_ENABLEDの排他違反**
   （advisorレビュー指摘．実害未確認だが原則通り分離）。

いずれも本体（sync/adv到達）を阻む壁ではなく，**ビルドを通すための
壁**だった点が実施01（D-1，実機挙動そのものの壁）と対照的。sync/adv
自体は初回のビルド成功・初回の実機書込みで両方とも一発で達成した。

### board C最終状態

BLE実験終了後，board Cへ`apps/wifi_scan`（`ESP32C6_WIFI=ON`，
`ESP32C6_CONSOLE=usbjtag`，本ラウンドの共有ファイル修正を含む
**新規フルビルド**）を書き戻した。

**★重要な経緯**：当初は実施01の記録に基づき既存のビルド成果物
（`build/wifi_scan_c6_hw/asp_flash.bin`，2026-07-10ビルド）を再利用
しようとしたが，**実機で2ブートとも「0 APs found」**（環境には
`<SSID-2G>`ほか多数のAP実在をホスト側`nmcli`で確認済み＝環境要因
ではない）となり異常を検出。原因は追跡していない（本ラウンドの
スコープ外と判断——WiFi側の独立した回帰の可能性があるが，深追いせず
確実な代替手段を取った）。**上記のGCC14.2.0対応を含む`wifi_scan`の
新規フルリビルドで解決**——2ブート独立で14〜18 APs検出（実施01の
記録値`14 APs`と同数を初回で再現）を確認した。

```
wifi_scan: 14 APs found (err=0)
wifi_scan: RESCAN 15 APs (err=0)
wifi_scan: RESCAN 14 APs (err=0)
...（以下RESCAN 10〜18 APsで変動，2ブートとも同様のパターンで安定）
```

**申し送り**：`build/wifi_scan_c6_hw`等，本ラウンド以前の日付の
ビルド成果物（asp_flash.bin）は，今回の実機検証で少なくとも1つが
不具合を示した。真因未特定のため，**今後board Cを復元する際は
必ず新規ビルドを使うこと**（cmakeコマンドはBLE実施01節と同一．
`-DASP3_APPLDIR=apps/wifi_scan -DASP3_APPLNAME=wifi_scan
-DESP32C6_WIFI=ON`，ESP32C6_CONSOLEは既定のusbjtagのまま）。

### C5への転移事項

1. **トランスポート層はC3ではなくC6の本ラウンドを参照すること**
   （`hci_driver_nimble.c`＋`hci_esp_ipc.c`，`LEGACY_VHCI_ENABLE=0`，
   `nimble_port.c`のSOC_ESP_NIMBLE_CONTROLLER分岐）——C5もC6と同じ
   新世代コントローラのため，同型の壁になる可能性が高い。
2. **GCC14.2.0対応の全項目**（npl_os_bridge.h／sys-lock.h／
   string.h／stdio.h／`TRUE=1`+`BT_HCI_LOG_INCLUDED=0`／
   `CONFIG_BT_NIMBLE_GATT_SERVER=1`ほか）はC5の同型ビルドでも
   ほぼ確実に同じ形で発生する。C6側の修正（共有ファイル＋C6固有
   ファイル）を先読み適用すればC5はビルド面で高速化できる見込み
   （BLE実施01の申し送りと同じ構図）。
3. **`CONFIG_BT_CONTROLLER_ONLY`はNimBLEビルドで1にしないこと**
   （D-1限定に閉じ込める設計をそのまま踏襲）。
4. **LP_AON STOREマップの重複に注意**：C6は`0x600B1000`
   （STORE0=sync／STORE2=adv attempt／STORE3=adv rc／STORE4-5=割込み
   レート／STORE6=reset／STORE7=intr_alloc trace，STORE1は既知の
   ノイズにつき使用禁止）。C5の同種計装は別デバイス・別レジスタ
   ベースになるはずだが，番地の使い回し設計思想は流用可。
5. **`build/`配下の古いビルド成果物を無条件に信用しない**
   （wifi_scanで実際に踏んだ罠．C5でも同様に，復元用バイナリは
   可能な限り当該セッションで再ビルドしたものを使うこと）。

### 変更ファイル（総括）

| ファイル | 内容 |
|---|---|
| `apps/ble_host_smoke_c6/`（新規） | `ble_host_smoke_c6.c`/`.h`/`.cfg`：D-2a/D-2bアプリ |
| `asp3/target/esp32c6_espidf/bt/stub/include/bt_nimble_config.h`（新規） | C6専用NimBLEホスト設定（C3版を流用しない） |
| `asp3/target/esp32c6_espidf/bt/stub/include/npl_os_bridge.h`（新規） | GCC14.2.0対応：npl_os_*ブリッジ＋esp_timer 2関数のプロトタイプ |
| `asp3/target/esp32c6_espidf/esp_bt.cmake` | `ESP32C6_BT_NIMBLE`ブロック追加，hci_driver二者択一，CONTROLLER_ONLYのD-1限定化，TRUE/BT_HCI_LOG_INCLUDED定義 |
| `asp3/target/esp32c3_espidf/hal_stub/include/sys/lock.h` | `_lock_*`系10関数のプロトタイプ追加（C3/C6/C5共有，strictly additive） |
| `asp3/target/esp32c3_espidf/hal_stub/include/stdio.h` | `fgets`/`setbuf`/`remove`/`rename`のプロトタイプ追加（同上） |
| `asp3/target/esp32c3_espidf/hal_stub/include/string.h` | `strcpy`/`strcat`/`strchr`/`strncpy`/`strncat`/`strrchr`/`strspn`/`strcspn`/`qsort`/`strdup`/`strlcpy`/`bzero`のプロトタイプ追加（同上） |
| `asp3/target/esp32c3_espidf/wifi/esp_shim_libc.c` | `fgets`（新規スタブ）・`bzero`（新規，memset委譲）の実体追加 |
| `asp3/target/esp32c3_espidf/wifi/esp_shim_cfg.h`・`esp_shim.cfg` | NimBLEプール拡張ゲートに`TOPPERS_ESP32C6_BT_NIMBLE`をOR追加（strictly additive） |

submodule（`asp3/asp3_core`・`hal/`）変更なし。C5系ファイル
（`asp3/target/esp32c5_espidf/`・`apps/bt_smoke_c5/`等）は未接触。

### Git情報

- ベースコミット：`fc296a1`（BLE実施01完了時点）
- ブランチ：`claude/c6-wifi-c5-dev-5vc6x9`
- 本ラウンドは**git commitしていない**（作業指示通り）。

## BLE実施04：C5 BTビルドの「カーネル起動阻害」調査——★実施03は誤診・C5 D-1は実は達成済み。真因は「no time event氾濫」＝C6と同型のSYSTIMERアラームlevel再ラッチ（良性ノイズ）がコンソールを埋め尽くしていただけ

**日付**：2026-07-14　**対象**：C5#1（`D0:CF:13:F0:A7:44`，UART=`b04e3bcf…`／
JTAG=ttyACM2）　**出発点**：実施03「BTビルドは実機ブートがD-1到達前に
BLOCKED，`main_task`の最初の1行すら走らずHRT割込みが再帰的即時再発火」。

### ★結論サマリ（実施03の診断を訂正）

- **C5 BTビルドは実は D-1 を達成していた**。実施03の「BLOCKED／main_task
  非到達」は**誤診**であった。真因は，`no time event is processed in hrt
  interrupt.`の**無限氾濫がsyslogバッファを溢れさせ（`N messages are
  lost`），バナー・`main_task`のD-1到達ログを含む全出力をコンソール上で
  かき消していた**こと。氾濫行を`grep -av`で除去すると，2回の独立ブート
  いずれもD-1シーケンスが完走していた（後述）。
- **立てた3仮説はすべて実機観測で反証**：
  - **H1（`LAST_INDEX()==0`／HRTCNT_BOUND経路）＝反証**。JTAGで
    `_kernel_tmevt_heap[0].last_index=1`（＝保留タイムイベントが常に存在）。
    HRTCNT_BOUND経路は一度も通っていない。
  - **H2（RAM枯渇・オーバーラップ）＝反証**。BT版RAM 70.77%（.bss末尾
    0x40843EFC・HP-SRAM 384KBに対し約115KBの余裕），かつ**より大きな
    RAMを使うWiFiビルド（76.25%）が正常起動**する（実施03のヒープ
    192KB→32KB反証実験とも整合）。
  - **H3（TICKS_PER_US較正）＝反証**。`ESP32C5_SYSTIMER_TICKS_PER_US=16`
    は実施03でJTAG実測16.00MHz確定済み。
- **氾濫（storm）の真因＝target層のSYSTIMER oneshotアラームの
  「level再ラッチ」**：C6の実施50追記7(B)で特定済みの機構と同型。
  無線（BTコントローラ）稼働で誘発される良性だがCPU浪費・コンソール
  占有型のノイズであり，**カーネル致命ではない**（D-1が完走した事実が
  その証拠）。asp3_core共通部（`kernel/time_event.c`）のバグではなく，
  **修正するなら`asp3/target/esp32c5_espidf/target_timer.*`のtarget層で
  閉じる**（ただしC6と共有の挙動＝下記「申し送り」参照）。

### 実機JTAG物証（BLOCKED状態のC5#1，2ブート＋動的トレース）

OpenOCD（`adapter serial D0:CF:13:F0:A7:44`／`board/esp32c5-builtin.cfg`／
super-WDT解錠後にhalt）。SYSTIMER base=`0x6000A000`。

- **livelock署名**（boot1）：halt時PCは常に`0x420052ac`〜`0x420052b0`
  （`dispatcher_1/2`），`mcause=0x88000020`（interrupt・cause32＝HRT
  タイマ割込みINTNO16），`mepc=0x42007812`（`irc_begin_int_demote`）。
  ＝HRT割込みがCPUをディスパッチャに張り付かせている（実施03の観測と一致）。
- **SYSTIMER比較器 vs 実カウンタ直読み（H1直接検証／boot1）**：
  `TARGET0`（armed）＝119,641,053 ticks，`UNIT0`実カウンタ（snapshot）＝
  124,709,809 ticks → **アラームtargetは実カウンタより5,068,756 ticks
  ＝約316ms「過去」**。`INT_ST bit0=1`かつ`INT_RAW bit0=1`（両方ラッチ），
  `TGT0_CONF`のperiodビット=0（**oneshot**），`INT_ENA=1`，
  `CONF=0x47000000`（TARGET0_WORK_EN bit24=1＝アラーム有効のまま）。
  → **oneshotが発火後も自動解除されず（WORK_EN残），かつ
  `systimer_ll_clear_alarm_int`は`int_clr`しか叩かないため，
  `counter>=target`が恒真の間はINT_RAW/INT_STがクリア直後に即再ラッチ**
  ＝level再ラッチstormの直接的レジスタ証拠。
- **保留イベントの正体（動的トレース／単一ブートで5サンプル）**：ヒープ
  先頭イベントの`callback=0x42007450=_kernel_wait_tmout_ok`（タスクの
  タイムアウト付き待ちのタイムアウトコールバック），`arg`＝**tskid9の
  TCB（＝`LOGTASK`，内部prio2）**。`evttim`は毎サンプル
  **current_evttimの常に約9,998μs（=10ms）先**を追走（6.33M→8.35M→
  9.15M→9.95M→10.74M）＝LOGTASKが`dly_tsk`≒10ms周期でループし，
  タイムアウトイベントを絶えず再登録している通常挙動。`signal_time`は
  **前進している**（2秒resumeで`current_evttim`が+2,498,667μs進行）＝
  カーネルは氷結しておらず，storm下でも遅いながら前進する。
- **全TCBダンプ**（tcb_table=`0x408336b0`・TCB=0x20）：
  tskid8=`BT_TIMER_TSK`（SEM待ち＝初期化済），tskid9=`LOGTASK`
  （DLY待ち），**tskid10=`MAIN_TASK`（内部prio9＝MAIN_PRIORITY 10）
  が`tstat=0x00=TS_DORMANT`**。`main_task`は末尾で`return`する実装
  （`apps/bt_smoke_c5/bt_smoke_c5.c`）のため，**DORMANT＝
  main_taskは走り切って正常終了した**ことを意味する（＝実施03の
  「main_task非到達」を直接反証）。

### ★D-1達成の物証（コンソール，氾濫除去，2ブート独立）

`grep -av "no time event is processed"`で氾濫を除去したUART採取：

- **boot1**：`TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5`バナー →
  `esp_bt_controller_init OK (heap free=179888)` →
  `esp_bt_controller_enable(BLE)` → `phy: libbtbb version: 92325d6` →
  `ble_ll_task -> tskid 1 (prio 23)` → `intr rate/1s line1=0 line2=0`
  （**BT線の割込みstorm無し**＝スロット配列・多重登録耐性が機能） →
  `controller enabled, sending HCI Reset` → **`Phase D-1 milestone
  reached`** → `HP_APM M0-M3 exception latch clear (BT path OK)`。
- **boot2**（独立reset）：同上に加え **`intr trace = 0xa1020704
  (nalloc=2 src1=7 src2=4)`＝C6のD-1結果と完全一致**，`sending HCI
  Reset` → `VHCI recv 7 bytes [0]=0x04 [1]=0x0e …`＝**HCI Command
  Complete（04 0e …）受信**（C6 D-1の`04 0e 04 01 03 0c 00`と同型）。
- ＝**判定基準(a)controller init／(b)enable=0／(c)HCI_Reset往復／
  (d)多重登録トレース／(e)APM例外ラッチ健全 の全てを2/2ブートで充足**。
  C5でBLEのcontroller初期化＋VHCI疎通（D-1）が達成された。

### 氾濫（level再ラッチstorm）の機構と局在

氾濫の1サイクル：LOGTASKの10ms `dly_tsk`イベントが発火→`signal_time`が
処理・LOGTASKが新イベント（10ms先）を再登録・`set_hrt_event`が新target
（未来）をarm→復帰。しかし**oneshotの旧発火がクリア直後に即再ラッチ**
（`counter>=旧target`が恒真・WORK_EN残・`int_clr`だけでは解除されない）
→ 次の実イベントが実際にdueになる10ms後まで，その隙間をISRが高頻度の
空振り（`nocall==0`→`no time event`）で埋め尽くす。**実イベントは10ms毎に
確実に前進する**ため，D-1は数秒〜十数秒かけて完走する（致命ではない）。

これはC6の実施50追記7(B)で「無線稼働で誘発されるHRTアラームlevel
再ラッチ」として既に特定・「良性・恒久HRTドライババグではない」と
切り分け済みの現象と**同型**。素のASP3（sample1）・WiFiビルドでは実
イベントの処理が十分速く常に未来targetがarmされ隙間が生じないため
顕在化しない（実施03のA/B対照＝sample1正常，WiFi正常と整合）。C5では
氾濫が桁違いに激しくコンソールを完全占有する点がC6（adv中でも読める）
と異なるが，機構は同一。

### 修正の可否と申し送り

- **真因はtarget層（`target_timer.*`）で閉じる**。asp3_core共通部
  （`kernel/time_event.c`の`set_hrt_event`）は仕様通りで無罪
  （HRTCNT_BOUND経路は不使用・EVTTIM比較も正しい）。
- **本ラウンドでは氾濫の恒久修正は行わない**（判断）。理由：(i) D-1は
  既に達成＝本ラウンドの主目的は満たされた，(ii) 氾濫はC6が意図的に
  許容している同型の良性現象であり，タイマ中核（全C5ビルド共有：WiFi
  600s負荷・test_porting）に影響する変更は，回帰試験を尽くした上で
  **C6と統一して**慎重に行うべき（BLEブリングアップの片手間で入れる
  べきでない），(iii) 厳密性基準（未検証の変更を「動くはず」で入れない）。
- **提案する修正（次段・要回帰試験）**：`target_hrt_set_event()`で
  target設定前後にアラームを一度disable→（set_target・apply）→enable
  し，oneshotの旧比較を確実に解除して新未来targetでクリーンに再arm
  する（`systimer_ll_enable_alarm(&SYSTIMER,0,false/true)`を挟む）。
  あるいは`target_hrt_handler()`でクリア後に`counter>=現target`なら
  アラームをdisableして空振り再ラッチを断つ。**必ず** sample1・WiFi
  scan・load_test（600s）・test_porting_c5で回帰確認し，C6にも同時
  適用して整合を取ること。

### WiFiビルド回帰

`ESP32C5_WIFI=ON`（`apps/wifi_scan`）は実施03で再ビルド成功済み・
本ラウンドではソース未変更（target層に手を入れていない）ため回帰なし。
共有shim基盤も無変更。

### C5#1最終状態

BLE実施03の`ESP32C5_BT=ON`ビルド（`build/bt_smoke_c5/asp_flash.bin`，
D-1達成が確認された当該バイナリ）を**フラッシュしたまま残置**（次段の
氾濫修正を同一条件で継続できるようにする判断）。`load_test_c5_r47`への
復元手順は実施03節に記載のとおり。

### 使用した計測資産（scratchpad）

`r04_hrtprobe.py`（SYSTIMER比較器 vs 実カウンタ・PCサンプル・kernel
変数直読み），`r04_hrtprobe2.py`（保留イベント同定・前進検証），
`r04_dyn.py`（全TCBダンプ＋動的トレース），`r04_console.py`（reset＋
UART採取／氾濫は`grep -av`で除去）。いずれも実施21系`r21_capture.py`の
OOCD／RTSリセット・reenum待ちハーネスを流用。

## BLE実施05：C5 D-2a（NimBLE host sync）＋D-2b（接続可能アドバタイズ）——★両方達成：sync到達・adv rc=0・**ホストhci0で「ASP3-C5-BLE」をOTA検出＝C5初のBLE電波到達**、割込みストーム非発生（nalloc=2トレースがC6/C5 D-1と完全一致）

**日付**：2026-07-14　**対象**：C5#1（`D0:CF:13:F0:A7:44`、UART=`b04e3bcf…`
＝ttyUSB0／JTAG=`D0:CF:13:F0:A7:44`のUSB-Serial/JTAG＝ttyACM2）　**目的**：
BLE実施02（C6 D-2a/D-2b）の手順をC5へ転写し、実施03/04で達成済みのC5 D-1の
上にNimBLEホストを載せてsync到達（D-2a）→接続可能アドバタイズ（D-2b）まで進める。

### 結論（先出し）

- **D-2a＝達成**：`ble_hs`のsyncコールバック到達をLP_AON STORE0
  （`0x600B1000`＝`0x5ade51c0`）で**2ブート独立に確認**。氾濫除去コンソールでも
  `ble_host_smoke_c5: ble_hs SYNC, host up`を両ブートで確認。
- **D-2b＝達成**：`ble_gap_adv_start`が**rc=0**（STORE2`0x600B1008`＝
  `0xad000000`＝`0xAD000000|rc`のrc=0、2/2ブート）、**ホストhci0の
  `bluetoothctl scan le`で`ASP3-C5-BLE`（`D0:CF:13:F0:A7:44`、RSSI -63〜-68）を
  実機検出＝C5 BLE史上初のover-the-air adv到達**。割込みレートは線1=0・
  線2=45〜48/秒（累積STORE5＝1226〜2009、実行時間で割ると数十/秒＝正常域。
  C3のストーム時~33万/秒とは4〜5桁差）、host reset回数=0（STORE3＝
  `0x00000000`＝`on_reset`未呼出し）。
- 多重登録トレース（STORE7`0x600B101C`）＝**`0xa1020704`（nalloc=2 src1=7
  src2=4）＝C6のD-1/D-2b・C5のD-1（実施04）と完全一致**。D-1で先読み実装済み
  だったsource多重登録対策（スロット配列、C3実施→(1)(o)のストーム地雷回避）が、
  NimBLEホスト＋実adv RF活動下でも機能し、C3が踏んだ「単一handle上書き→
  ストーム」は**再発しなかった**（C6実施02と同じ予防設計の実証）。
- heap free=168712（D-1＝179888より約11KB少ない＝NimBLEホスト分。妥当）。

### 氾濫（実施04）対策の実施方法

実施04で確立した通り、C5では`no time event is processed in hrt interrupt.`が
氾濫しコンソールを埋めうるため、**コンソールを唯一の判定根拠にしない**方式を
採った：

1. **判定はコンソールに依存させない**：sync（D-2a）・adv rc・割込みレート
   線1/線2・多重登録トレースの全てを、コンソールの補助を要さず単独で判定できる
   ようLP_AON STOREマーカへ書き、**esptool `--no-stub read-mem`（JTAG非依存）**で
   回収した（advのRF活動下でJTAGが死ぬC3/C6の教訓＋氾濫の両方に備えた）。
   実機で`read-mem`はDUTのUSB-Serial/JTAG（ttyACM2）経由で動作し、LP_AON STOREが
   reset-to-bootloaderを生存することを実証（D-1残値`0xa1020704`が保持されていた
   ことで事前確認）。
2. **コンソールは氾濫除去してから補助的に読む**：`grep -av "no time event is
   processed"`で氾濫行を除去。今回は実施04より氾濫が軽微（adv活動でアプリ出力が
   常時流れるためか、30秒キャプチャ中の氾濫行は12行のみ）で、除去後は
   D-2a/D-2bシーケンス全行（banner→init OK→enable→nimble_init→SYNC→
   advertising started→g_adv_rc=0→intr rate line2=48）が読めた。
3. STOREマーカとコンソールの**2系統が両ブートで一致**＝厳密性基準を満たす
   独立2証拠。

### C5用LP_AON STOREマップ（C6実施02からの再配置）

C6は`0x600B1000`ベースのSTORE0/2/3/4/5/6/7を使ったが、C5は実施35が
STORE1（RTC_SLOW_CLK_CAL）を、実施41がSTORE2-4を診断ミラーに使う。ただし
**実施41の起動時書込みは`ESP32C5_R41_CALL_BOOTHOOK`未定義の本BTビルドでは
走らない**（`target_kernel_impl.c`のガードを確認）ため、STORE2-4は「reset生存
かつROM非改変」を実施41が実証済みの**安全な空きレジスタ**として再利用した
（advisorレビューでも「STORE2-4は最も実証度が高い」と確認）。最終マップ：

| STORE | 番地 | 用途 | 根拠 |
|---|---|---|---|
| STORE0 | `0x600B1000` | sync（`0x5ade51c0`） | C6でreset生存実証、C5起動は非改変 |
| STORE2 | `0x600B1008` | adv-return rc（`0xAD0000\|rc`。試行＋rc兼用） | 実施41でreset生存実証 |
| STORE3 | `0x600B100C` | ble_hs reset reason/count（`0x5E…`。0＝未reset） | 同上 |
| STORE4 | `0x600B1010` | 割込みレート 線1累積ミラー | 同上 |
| STORE5 | `0x600B1014` | 割込みレート 線2累積ミラー（副次） | 本ラウンドでreset生存を確認（=`0x4ca`/`0x7d9`） |
| STORE7 | `0x600B101C` | 多重登録トレース（bt_shim.c、予約） | D-1（実施03/04）から使用 |

C6の独立「adv開始試行」マーカ（STORE2）は落とし、adv-rcマーカ（`0xAD0000|rc`）が
「試行して戻り値を得た」証拠を兼ねる形へ統合（レジスタ節約。実害なし）。

### 設計（C6実施02の転写＋IDF v6.1固有の差分）

- **NimBLEホストソースはhal submoduleではなくIDF v6.1（`~/tools/esp-idf-v6.1`）
  から採る**：C5はbt/phy/coex/nimbleを実施10のmatched set（eco2対応）で統一する
  方針（実施03の中心判断）に従い、nimbleもv6.1側で揃えた。トランスポートは
  C6と同型の新世代経路（`hci_transport.c`＝D-1既存＋`hci_driver_nimble.c`＋
  `nimble/transport/esp_ipc/src/hci_esp_ipc.c`。`hci_driver_standard.c`とは
  `hci_driver_vhci_ops`を取り合う二者択一＝`if(NOT ESP32C5_BT_NIMBLE)`で分離）。
- **`CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE=0`が必須**（新世代コントローラは
  `esp_nimble_hci_init()`がコンパイルアウトされる。C3版bt_nimble_config.hは
  LEGACY=1のため流用不可）。★C5のインクルードパスはC3の`bt/stub/include`
  （LEGACY=1版を同梱）を含むため、C5専用`bt/stub/include/bt_nimble_config.h`
  （LEGACY=0）を`list(PREPEND …)`でC3より前に置いた（順序を誤ると
  LEGACY=1が選ばれサイレントなmbufオフセットずれになる＝advisor指摘）。
- **`CONFIG_BT_CONTROLLER_ONLY=1`はD-1限定**（NIMBLE_ENABLEDと排他。
  `if(NOT ESP32C5_BT_NIMBLE)`へ移動。C6実施02と同じ分離）。
- アプリ配線はC6実施02と同一（`nimble_port_init()`の二重初期化を避け、
  `esp_bt_controller_init/enable`明示→`esp_nimble_init()`→`sync_cb/reset_cb`
  登録→`nimble_port_freertos_init()`）。

### 遭遇した壁と対処（C6実施02より遥かに少ない）

C6実施02はGCC 14.2.0のimplicit-declaration hard error群（npl_os_bridge.h・
sys/lock.h・string.h・stdio.h・TRUE/BT_HCI_LOG_INCLUDED順序バグ・GATT_SERVER
不整合など計7類）を踏んだが、**C5は壁が1件のみ**だった：

1. **`nimble_mem_free`/`nimble_mem_calloc`未定義参照（リンク時）**：ble_att_svr.c
   ／ble_gatts.cが直接呼ぶ。実体は`bt/host/nimble/port/src/esp_nimble_mem.c`
   （heap_caps_*＝esp_shim_libc.cへ委譲）。ソース集合に追加して解決。

C6の壁がC5でほぼ出なかった理由：(i) C5はビルドに`riscv64-unknown-elf-gcc
13.2.0`（asp3_coreの既定toolchain-riscv64.cmake）を使い、C6実施02が踏んだ
**GCC 14.2.0固有のhard error化がそもそも発生しない**、(ii) IDF v6.1の
`nimble_port.c`は`bt_common.h`（TRUE／BT_HCI_LOG_INCLUDEDの定義元）を`#if`より
**前に**includeするため、C6/halで踏んだ順序バグがv6.1ソースツリーに存在しない
（`-DTRUE=1 -DBT_HCI_LOG_INCLUDED=0`は不要と判断し追加しなかった。実機ビルドで
実際に不要だったことを確認）、(iii) sdkconfig.hはC5の`sdkconfig_stub/sdkconfig.h`
（全ビルド共通include。D-1から既存）へ解決される。GATT_SERVER不整合対策
（`CONFIG_BT_NIMBLE_GATT_SERVER=1`）はbt_nimble_config.hへ先読みで入れておいた。

### 判定基準（実施前に固定）と結果（2ブート独立）

判定はC6実施02と同じ3点（①adv rc　②割込みレート　③hci0実機検出）を独立事実
として記録（rc=0かつレート正常でもhci0未検出は「起こりうる正当な結果」で
機械的に「失敗」と混同しない、というadvisor基準を踏襲）。

| 指標 | boot1 | boot2 |
|---|---|---|
| sync到達（STORE0） | `0x5ade51c0` | `0x5ade51c0` |
| ①adv-return rc（STORE2） | `0xad000000`＝**rc=0** | `0xad000000`＝**rc=0** |
| host reset回数（STORE3） | `0`（未reset） | `0`（未reset） |
| ②割込みレート 線1/線2（STORE4/5） | 0／`0x7d9`（累積2009、~80/s、正常域） | 0／`0x4ca`（累積1226、~50/s、正常域） |
| 多重登録トレース（STORE7） | `0xa1020704`（nalloc=2 src1=7 src2=4） | `0xa1020704`（同左） |
| コンソール（氾濫除去） | SYNC／advertising started 'ASP3-C5-BLE'／g_adv_rc=0／intr line2=48 | 同左（line2=45） |

**③hci0実機検出（2ブート独立）**：ホストhci0（`8C:1D:96:BA:6D:BD`）の
`bluetoothctl scan le`で`[NEW] Device D0:CF:13:F0:A7:44 ASP3-C5-BLE`を
**boot1（RSSI -63〜-68）・boot2（キャッシュを`bluetoothctl remove`で消してから
再スキャン＝鮮度保証、RSSI -62〜-83）の両方で検出**。MACはC5#1のBASE MAC
（`D0:CF:13:F0:A7:44`）と一致（`own_addr_type=0`＝public address）。RSSIが
ブート間・時系列で変動することが実放射（キャッシュヒットでない）の傍証。
この`D0:CF:13:F0:A7:44`はこれまでBLEを一度も送出していない個体のため、
検出は本ラウンドのadvが初出であることを意味する。

**注：STORE4（線1）が2ブートとも0（線2のみ活動）**は、C6実施02が記録した
同型の非対称（線1=0/線2活動）と一致する。多重登録トレース（STORE7）が
nalloc=2＝2ソース（src7＋src4）とも正しく登録済みを示すため良性であり、
C5固有の異常ではない（rc=0・hci0検出済みのため深追いしない）。

**★判定＝D-2a達成・D-2b達成**。C5でBLEホストsync到達・接続可能アドバタイズ・
OTA電波到達までを2ブート再現で確認した。

### 変更ファイル

| ファイル | 内容 |
|---|---|
| `apps/ble_host_smoke_c5/`（新規） | `ble_host_smoke_c5.c`/`.h`/`.cfg`：D-2a/D-2bアプリ（ble_host_smoke_c6を土台に、デバイス名`ASP3-C5-BLE`・C5用LP_AON STOREマップ・実施13 ICG init・sync待ちを100ms×200へ延長） |
| `asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h`（新規） | C5専用NimBLEホスト設定（LEGACY_VHCI=0。C3版＝LEGACY=1を流用しない） |
| `asp3/target/esp32c5_espidf/esp_bt.cmake` | `ESP32C5_BT_NIMBLE`option＋自動ON、`bt/stub/include`のPREPEND、`CONFIG_BT_CONTROLLER_ONLY`と`hci_driver_standard.c`を`if(NOT ESP32C5_BT_NIMBLE)`へ、D-2a節（IDF v6.1のnimbleホスト一式＋`esp_nimble_mem.c`）追加 |
| `asp3/target/esp32c3_espidf/wifi/esp_shim_cfg.h`・`esp_shim.cfg`（共有・strictly additive） | NimBLEプール拡張ゲート（SEM24→28/MTX8→12/DTQ4→8/TSK6→8）に`TOPPERS_ESP32C5_BT_NIMBLE`をOR追加。C3/C6の既存条件・値は不変（C5もC6同様にNimBLEホストのeventq/mutex/sem/taskを共有esp_shimプールから確保するため必要）。**注：本ファイルはesp32c3_espidf配下だが、C5がinclude/CFGで再利用している共有ファイルであり、追加はC3/C6の挙動を変えない**（禁則対象はasp3_core/hal/idfのみ） |

submodule（`asp3/asp3_core`・`hal/`）・`~/tools/esp-idf-v6.1`はいずれも無変更。

### 非回帰の確認

- D-1（`bt_smoke_c5`）を`ESP32C5_BT=ON`で再ビルド：**RAM 70.77%（実施03の記録値と
  完全一致）**、0エラー。gatingが正しいことを確認（D-1は`hci_driver_standard.c`を
  リンク・`hci_driver_nimble.c`は不参照、D-2aは逆＝多重定義なし）。
- D-2aビルド：FLASH 383584B（9.15%）・RAM 303920B（77.29%。WiFiビルドの76.25%と
  同水準で余裕あり）、多重定義／未解決シンボルなし。

### C5#1最終状態

BLE実施05の`ESP32C5_BT=ON`＋`ESP32C5_BT_NIMBLE`（自動ON）ビルド
（`build/ble_host_smoke_c5/asp_flash.bin`）をフル4MB `write-flash 0x0`済みのまま
残置（D-2a/D-2b達成バイナリ。次段＝D-2c接続確立を同一条件で継続できる判断）。
`load_test_c5_r47`（WiFi/TCP/UDPの恒久ビルド）への復元手順は実施03節に記載。

### 使用した計測資産（scratchpad）

`r04_console.py`（RTSリセット＋UART採取。氾濫は`grep -av`で除去。実施04から流用）、
esptool v5.3.1 `--chip esp32c5 --before usb-reset --after no-reset read-mem <addr>`
（LP_AON STORE回収。JTAG非依存）、ホストhci0の`bluetoothctl scan le`（OTA検出）。

### 申し送り（次段＝D-2c 接続確立）

C6実施02はD-2bで止まっている（D-2c接続はC3が`docs/bt-shim.md`で追っている段階）。
C5もadv到達まで達成。次段でセントラルから接続を試みる場合、本ビルドは
`CONFIG_BT_NIMBLE_SECURITY_ENABLE=0`（暗号なし）・GATTサーバは`ble_svc_gap/gatt`のみ
の最小構成のため、GATT探索でNULL関数ポインタに当たる可能性（C3のD-2cで既知）に
注意。氾濫の恒久修正（実施04申し送り＝`target_hrt_set_event`のクリーン再arm）は
本ラウンドでも未実施（D-2a/D-2bはstore主体の判定で完遂できたため）。

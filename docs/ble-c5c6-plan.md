# BLE (ESP32-C5/C6) 展開計画 — 机上設計ラウンド

本ラウンドの成果物。**読み取り専用調査＋本ファイルのみ新規作成**（コード変更・
ビルド・実機なし）。C3で動作済みのBLE（D-1 controller+VHCI／D-2a NimBLE host
sync／D-2b connectable advertising——2026-07-13にsource多重登録バグ根治で
電波到達済み，詳細は`docs/bt-shim.md`）を，ESP32-C5/C6へ展開するための実装
設計書。

**★2026-07-14 BLE実施01（C6 D-1）達成**：controller init+VHCI疎通を
実機board Cで2ブート再現確認（HCI_Reset→Command Complete正常受信・
多重登録トレースでintr_alloc 2回呼出しを確認・ストーム無し・heap
健全・APM latch clear）。設計書の「3テーブル実装」という想定は
実際にはbt.c自身がosi_coex_funcs_t/npl_funcs_tを自前で埋めるため
「下位プリミティブ（platform/os.hのesp_os_*）＋周辺ポーティング層
のみASP3実装」に縮小。上流ドリフト（nimble_port_os.h／npl_os_*命名）
2件・libbtbb.aリンク漏れ・esp_timerプール枯渇（4→16）等，実装のみ
では踏めなかった壁が複数あった。詳細・全壁一覧は`docs/ble-c5c6.md`
「BLE実施01」。次段＝C5への横展開（本ラウンドの壁一覧を先読み適用）
またはC6 D-2a（NimBLEホスト）。

**★2026-07-14 BLE実施03（C5 D-1）——ビルド/リンク達成，実機ブートが
未知の壁でBLOCKED**：ライブラリ選定はhal submoduleではなく**IDF
v6.1**（bt.c/ble.c/npl/phy/coexすべて）に統一——C5 WiFi統合が確定
させた「hal世代のlibphy.aはeco2シリコンのPHY較正で収束しない」
という制約はBTの`esp_phy_enable`経路にも及ぶため。副次的にv6.1の
bt.cはC3型（直接FreeRTOS API＋標準esp_intr_alloc）でC6のBLE実施01
とはプログラミングモデルが異なると判明し，上流ドリフト吸収シム2件
（C6で必要だった）は不要になった一方，`os_mempool.c`自前リンクや
`esp_bit_defs.h`強制includeなど新規の壁が3件出た。ビルド・リンクは
成功（回帰確認済み）したが，**実機ブートでカーネルバナーすら出ず
`main_task`が一度も走らない**新種の壁（HRT/SYSTIMER割込みの即時
再発火ループ）を発見——BT機能自体は未実行のためD-1判定は評価不能。
詳細・反証実験・次段仮説は`docs/ble-c5c6.md`「BLE実施03」。

## 0. 結論サマリ（詳細は各節）

- **C5/C6のBLEコントローラはC3と別世代**（`SOC_ESP_NIMBLE_CONTROLLER=1`＝
  「BLE embedded controller V1」）。ソース構成・OS抽象化層・クロック
  ゲーティング機構のいずれもC3とは別物で，**C5とC6は互いにほぼ同一**
  （ソース差分は数行）。
- ただし内部で使うFreeRTOS原始プリミティブの**種類**（queue/semaphore/
  task/timer）はC3と共通のため，`asp3/target/esp32c3_espidf/bt/stub/
  include/freertos/*.h`（実体はwifi/esp_shim.cへの委譲）は**構造として
  そのまま転用できる**（新規発明は不要，配線し直すだけ）。
- クロックゲーティング（C3で`emi.c:164`アサートの真因だった問題）は
  C5/C6では`modem_clock_module_enable(PERIPH_BT_MODULE)`という**WiFi
  bring-upで既に移植済みの`modem_clock.c`/`modem_clock_hal.c`と同じ
  サブシステム**を経由する設計になっており，C3のような手作りレジスタ
  ポークは原理的に不要（ただし`PERIPH_BT_MODULE`経路自体は未実測）。
- APM/バスマスタ問題（C5実施42/43・C6実施87/88で確定・恒久化済み）は
  WiFi/BT共有の単一マスタID（`APM_MASTER_MODEM=4`）を対象とする
  **WiFi非依存の無条件起動時修正**であり，構造上BLEもカバーされている
  はずだが，BT経路での実測はまだ無い。
- C3からの流用可能度は**シム設計思想・FreeRTOS原始プリミティブ層・
  割込み多重登録対応の教訓レベルで高い（感覚値60-70%）**が，登録テーブル
  （osi_coex_funcs_t/ext_funcs_t/npl_funcs_t）配線コード自体は新規実装
  になる（**バイト単位のコード流用は感覚値20-30%**）。

## 1. コントローラ世代の判定

### 1.1 世代マーカー

`hal/components/soc/{esp32c6,esp32c5}/include/soc/soc_caps.h`：

```
esp32c6: #define SOC_ESP_NIMBLE_CONTROLLER (1)   /* line 543 */
esp32c5: #define SOC_ESP_NIMBLE_CONTROLLER (1)   /* line 636 */
```

C3にはこの定義がない（`esp32c3/include/soc/soc_caps.h`に同名マクロ無し）。
これが「BLE embedded controller V1」世代かどうかの一次判定マーカーで，
C5/C6は共にYES，C3はNO。この1ビットが以下すべての差分の根であり，
`hal/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c`
（`#if !SOC_ESP_NIMBLE_CONTROLLER && CONFIG_BT_CONTROLLER_ENABLED`等）と
`hal/components/bt/porting/npl/freertos/src/npl_os_freertos.c`（冒頭で
`#error "not defined SOC_ESP_NIMBLE_CONTROLLER..."`）の分岐で機械的に
確認できる。

### 1.2 ソース構成の差分表

| 項目 | C3（既存） | C6/C5（新規） |
|---|---|---|
| コントローラ本体 | `controller/esp32c3/bt.c`（2411行）のみ | `controller/{esp32c6,esp32c5}/bt.c`（1894/1826行）＋`controller/{esp32c6,esp32c5}/ble.c`（365/361行，*_stack_*グルー） |
| コントローラ実体 | ソース配布（bt.cが直接ロジックを持つ） | blob（`libble_app.a`）＋薄いble.cグルー（`base_stack_initEnv/enable`等がblobシンボル） |
| OSアクセス方法 | bt.cが**直接**FreeRTOS API呼出し（xQueueCreate等） | bt.cは`nimble/nimble_npl_os.h`（NPL抽象化）＋3種の関数テーブル登録（1.3節） |
| リンクライブラリ | `libbtdm_app.a`＋`phy`＋`coexist`＋`btbb`（`lib_esp32c3_family/esp32c3/`） | `libble_app.a`（`lib_esp32c6/esp32c6-bt-lib/{esp32c6,esp32c61}/`，`lib_esp32c5/esp32c5-bt-lib/`）＋`phy`＋`coexist`。**`btbb`相当は不要**——`hal/components/bt/CMakeLists.txt` L1073-1096の`CONFIG_BT_CONTROLLER_ENABLED`分岐は`libble_app`一つだけを`add_prebuilt_library`しており（`REQUIRES esp_phy`），C3のような「coex_pti_v2の実体だけ別blob」という分割は存在しない＝`libble_app.a`が自己完結 |
| BT専用ROM ld | 要（`esp32c3.rom.eco3_bt_funcs.ld`・`bt_funcs.ld`＝ECO3以降実機限定の踏み分け） | **不要**（`esp_rom/{esp32c6,esp32c5}/ld/`にBT専用ldなし＝コントローラはflash blobに完全常駐，ROM/eco依存の踏み分けが構造的に消える） |
| クロックゲーティング | 手動`SYSCON_WIFI_CLK_EN_REG`ビットポーク（2ボードJTAG差分で確定，bt_shim.c`esp_shim_bt_clock_init()`） | `modem_clock_module_enable(PERIPH_BT_MODULE)`＋`modem_clock_module_mac_reset(PERIPH_BT_MODULE)`をbt.c自身が呼ぶ（WiFi用に既に移植済みの`modem_clock.c`と同一サブシステム） |
| VHCI API | `esp_vhci_host_check_send_available/send_packet/register_callback`（D-1で使用） | **同一API名で存在**（`include/esp32c6/include/esp_bt.h`）。bt.c内部で`esp_bt_controller_init()`が`hci_transport_init(HCI_TRANSPORT_VHCI)`（既定`CONFIG_BT_LE_HCI_INTERFACE_USE_RAM`時）を無条件に呼ぶ＝D-1相当のコントローラ単体VHCIループバックは引き続き可能 |
| NimBLE host↔controller結合 | `esp_nimble_hci.c`＋`nimble/transport/esp_ipc_legacy/src/hci_esp_ipc_legacy.c`（*upstream* npl＋汎用mbufトランスポート経由，公開`esp_vhci_host_*` API＋パケット種別ヘッダでバッファをコピーする1段厚い経路） | **HCIパケット種別（CMD/ACL/EVT）自体は同じだが，仲介コピーが1段薄い**：`hal/components/bt/CMakeLists.txt` L693-712の`CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT`分岐で`porting/npl/freertos/src/npl_os_freertos.c`＋`porting/transport/src/hci_transport.c`をリンクし，`CONFIG_BT_NIMBLE_ENABLED`時は`porting/transport/driver/vhci/hci_driver_nimble.c`＋`host/nimble/nimble/nimble/transport/esp_ipc/src/hci_esp_ipc.c`（`ble_transport_ll_init()`が`hci_transport_host_callback_register()`でホストの受信コールバックを**同一プロセス内で直接登録**——実測：`hci_esp_ipc.c` L56-60）を使う。汎用mbufプール確保コード（`nimble_port.c`/`transport.c`）が`#if !SOC_ESP_NIMBLE_CONTROLLER`でコンパイル対象外になるのは「コントローラ側が自前でバッファ管理する」ためで，「HCIパケット形式そのものを経由しない」わけではない。**なお`CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE`を立てれば，C3が使うのと全く同じ`esp_nimble_hci.c`＋`hci_esp_ipc_legacy.c`もC6/C5でリンク可能**（CMakeLists L928-934，SOC_ESP_NIMBLE_CONTROLLERで排他されていない）——D-2a実装が難航した場合の代替経路として温存できる（6節） |

### 1.3 要求されるOSAL/OSI構造体（version magicつき）

C6/C5のbt.cは起動時に3つの関数テーブルを登録する（`hal/components/bt/
controller/esp32c6/bt.c` L89-165, L1063〜）。

**(a) `struct osi_coex_funcs_t`**（coex連携．C3のcoex adapterと役割対応）

```c
#define OSI_COEX_VERSION      0x00010006
#define OSI_COEX_MAGIC_VALUE  0xFADEBEAD
struct osi_coex_funcs_t {
    uint32_t _magic;      /* = OSI_COEX_MAGIC_VALUE */
    uint32_t _version;    /* = OSI_COEX_VERSION */
    void (* _coex_wifi_sleep_set)(bool sleep);
    int  (* _coex_core_ble_conn_dyn_prio_get)(bool *low, bool *high);
    void (* _coex_schm_status_bit_set)(uint32_t type, uint32_t status);
    void (* _coex_schm_status_bit_clear)(uint32_t type, uint32_t status);
};
/* 登録: ble_osi_coex_funcs_register(&s_osi_coex_funcs_ro) */
```

**(b) `struct ext_funcs_t`**（malloc/task/intr/random/ecc．ASP3プリミティブへの主要な写像先）

```c
#define EXT_FUNC_VERSION      0x20250825
#define EXT_FUNC_MAGIC_VALUE  0xA5A5A5A5
struct ext_funcs_t {
    uint32_t ext_version;                                  /* = EXT_FUNC_VERSION */
    int   (*_esp_intr_alloc)(int source, int flags, intr_handler_t handler, void *arg, void **ret_handle);
    int   (*_esp_intr_free)(void **ret_handle);
    void *(*_malloc)(size_t size);
    void  (*_free)(void *p);
    int   (*_task_create)(void *task_func, const char *name, uint32_t stack_depth, void *param,
                           uint32_t prio, void *task_handle, uint32_t core_id);
    void  (*_task_delete)(void *task_handle);
    void  (*_osi_assert)(const uint32_t ln, const char *fn, uint32_t param1, uint32_t param2);
    uint32_t (*_os_random)(void);
    int   (*_ecc_gen_key_pair)(uint8_t *public, uint8_t *priv);
    int   (*_ecc_gen_dh_key)(const uint8_t *remote_pub_key_x, const uint8_t *remote_pub_key_y,
                              const uint8_t *local_priv_key, uint8_t *dhkey);
#if CONFIG_IDF_TARGET_ESP32C6
    void (*_esp_reset_modem)(uint8_t mdl_opts, uint8_t start);   /* C6のみ．C5には無い */
#endif
    uint32_t magic;                                         /* = EXT_FUNC_MAGIC_VALUE */
};
/* 登録: esp_register_ext_funcs(&ext_funcs_ro) */
```

**(c) `struct npl_funcs_t`**（NimBLE Porting Layer．**ASP3が直接実装する
必要はない**——`hal/components/bt/porting/npl/freertos/src/
npl_os_freertos.c`（1269行，ESP-IDF native実装）が`npl_os_funcs_get()`→
`esp_register_npl_funcs()`で自前提供する。このファイル自体が要求する
下位APIが下記1.4節）。

### 1.4 npl_os_freertos.cが要求する下位FreeRTOS API（C3シムとの一致点）

`hal/components/bt/porting/npl/freertos/src/npl_os_freertos.c`の冒頭
includeは：

```c
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/portable.h"
```

これは**C3のbt.cが直接includeするヘッダ集合と完全一致**する。つまり
C3向けに`asp3/target/esp32c3_espidf/bt/stub/include/freertos/*.h`
（実体はwifi/esp_shim.cの既存プリミティブへの委譲）として既に用意した
シムは，**関数シグネチャレベルでそのままC6/C5のnpl層の要求も満たせる**
可能性が高い（1269行全体をシグネチャ突合せする詳細検証はビルド実装時
に必要だが，includeするヘッダ集合が一致している時点で「新規のFreeRTOS
API発明」は発生しない設計であることは確定）。

C6/C5固有の`nimble/nimble_npl_os.h`が定義する`struct npl_funcs_t`の
中身自体（`npl_os_freertos.c`のシンボル）はビルド時にリンクされる
既存コードであり，ASP3側が書くのは**そのコードが要求する
FreeRTOS原始プリミティブのシム**（C3と共通の型）のみで良い。

## 2. シム設計

### 2.1 流用できる部分（構造レベルで高い再利用）

- `asp3/target/esp32c3_espidf/bt/stub/include/freertos/*.h`
  （FreeRTOS.h/portable.h/queue.h/semphr.h/task.h/timers.h）：
  npl_os_freertos.cが要求するAPI集合と一致するため，**配置場所を
  変えるだけ**（`${C3_TARGETDIR}/bt/stub/include`をC6/C5の
  `esp_bt.cmake`からインクルードパス追加で直接参照する案が有力——
  C6のtarget.cmakeが既に`${C3_TARGETDIR}/hal_stub/include`を同じ
  パターンで再利用している前例がある。詳細は3.1節）。
- `bt_shim.c`のOSAクリティカルセクションのネストカウンタ修正
  （`docs/bt-shim.md` L796〜「Phase D-1完了」）・
  `s_ticks_per_us(160)`の教訓：npl_os_freertos.cが
  `portENTER_CRITICAL`/`taskENTER_CRITICAL`相当をどう呼ぶか次第だが，
  同型の罠（ネスト対応漏れによるenable未完走）が起きる可能性が高く，
  **最初から複数レベルのネストに耐える設計で実装する**（C3で後から
  気付いて直した反省を先取りする）。
- `esp_intr_alloc`のsource多重登録対応（`docs/bt-shim.md` (1)(o)，
  `docs/s3-bt-intr-source-overwrite-fix-for-c3.md`）：C6/C5の
  `ext_funcs_t._esp_intr_alloc`はコントローラから**複数回**呼ばれる
  可能性が高い（C3のbt.cで2回＝source8→source5だったのと同種の
  パターンが，別世代のblobでも起こり得るという一般教訓）。C3の
  bt_shim.cは既にスロット配列化・呼出し順でCPU線を分離する実装に
  なっている（`bt_intr_slot[BT_INTR_MAX_SLOT]`）——**この設計をC6/C5
  シムでも最初から採用し，「1回しか呼ばれない」という未検証の前提で
  単一handle実装をしない**（C3で一度踏んだ地雷を初手で回避する）。
- `esp_timer_*`（BTコントローラのモデムスリープ用）・`esp_pm_lock_*`
  （no-opスタブ）・`esp_ipc_call_blocking`（単一コア直接呼出し）・
  `esp_partition_*`（NVS/較正データ「常に存在しない」スタブ）：
  ext_funcs_t経由ではなく別ヘッダ経由の依存だが，C3のbt_shim.cにある
  実装はチップ非依存のロジック（esp_timerプール・pm lockダミー構造体
  等）なのでほぼそのまま転用できる。
- `esp_coex_adapter.c`（`asp3/target/{esp32c6,esp32c5}_espidf/wifi/`
  に既存，WiFi用に既に移植済み）：`osi_coex_funcs_t`の
  `_coex_wifi_sleep_set`/`_coex_core_ble_conn_dyn_prio_get`/
  `_coex_schm_status_bit_set/clear`は，既存のno-op coexスタブ思想を
  そのまま4関数分追加するだけで済む可能性が高い。

### 2.2 新規実装が必要な部分

- **登録テーブル配線コード**（`osi_coex_funcs_t`/`ext_funcs_t`の
  static const初期化＋`ble_osi_coex_funcs_register`/
  `esp_register_ext_funcs`呼出し）：C3には存在しない構造なので新規。
  ただし個々のフィールドの中身は`esp_wifi_adapter.c`が既に持つ
  `g_wifi_osi_funcs`スタイルのテーブル登録パターン（WiFi側で
  `wifi_osi_funcs_t`のようなテーブルを埋めてregisterする既存作法）
  と同型なので，**設計パターン自体はWiFi shimからの横展開**になる
  （新規発明ではなく，既存パターンの2件目適用）。
- `_task_create`/`_task_delete`：ASP3タスクは静的生成（動的メモリ
  禁止，CLAUDE.md禁則3）が前提のため，C3のfreertos/task.hシム
  （`xTaskCreate`相当）が採用している静的プール方式をそのまま
  `_task_create`のフックにも適用する。引数に`core_id`があるが，
  `soc_caps.h`で確認した通りC6/C5は共に`SOC_CPU_CORES_NUM=(1U)`
  （シングルコア，BLE専用コアは存在しない）のため無視してよい。
- `_ecc_gen_key_pair`/`_ecc_gen_dh_key`：bt.cのextern宣言を見ると
  実体は`ble_sm_alg_gen_key_pair`/`ble_sm_alg_gen_dhkey`
  （`NIMBLE_ROOT/host/src/ble_sm_alg.c`，C3のD-2b cmakeで既にリンク
  対象になっているファイル）を指している可能性が高い——**NimBLE
  ホスト自身のtinycrypt実装を右から左に渡すだけ**で，新規暗号実装は
  不要な見込み（D-2a/D-2bでこのファイルを既にリンクしていれば
  自動的に解決）。ただしD-1（controller-onlyビルド，NimBLEホスト
  無し）の段階では`ble_sm_alg.c`がリンク対象に無いため，
  D-1では未使用スタブ（呼ばれない前提でreturn失敗）にする設計が
  必要（C3のD-1/D-2分離パターンを踏襲）。
- `_esp_reset_modem`（C6のみ．C5の`ext_funcs_t`には存在しない
  ——`#if CONFIG_IDF_TARGET_ESP32C6`ガード）：モデムリセット。
  `modem_clock`サブシステム経由の実装が必要（詳細未調査，実装時に
  `hal/components/esp_hw_support/modem_clock.c`内の関連APIを確認）。
- `esp_hci_transport.c`（`hal/components/bt/porting/transport/src/
  hci_transport.c`）＋VHCI driver（`porting/transport/driver/vhci/
  hci_driver_standard.c`等）：D-1のVHCIループバックに必要なグルー。
  C3はこの層を持たず（C3世代は`esp_nimble_hci.c`＋
  `hci_esp_ipc_legacy.c`という別経路），**C6/C5固有の新規統合**。
  ソース自体はhal提供（封印済みではない）なので基本はビルドへ
  追加するだけだが，依存するOS API（キュー等）が正しくシムされて
  いるか確認が要る。

## 3. 配線設計

### 3.1 target.cmake設計（ESP32C6_BT/ESP32C5_BTオプション）

C3の`asp3/target/esp32c3_espidf/target.cmake`内`ESP32C3_BT`節
（既存option・BT+WiFi同時ON禁止のFATAL_ERROR）と同型のパターンを
`ESP32C6_WIFI`/`ESP32C5_WIFI`オプションの隣に追加する：

```cmake
option(ESP32C6_BT "Enable Bluetooth (BLE embedded controller V1 + NPL shim, Phase D-1)" OFF)
if(ESP32C6_BT)
    if(ESP32C6_WIFI)
        message(FATAL_ERROR "ESP32C6_BT + ESP32C6_WIFI is not supported yet (RAM budget; C3の前例踏襲)")
    endif()
endif()
include(${TARGETDIR}/esp_bt.cmake)
```

C5も同型（`ESP32C5_BT`）。BT+WiFi同時ON禁止はC3の前例（RAM予算）を
そのまま踏襲する初期方針とする——**ただし将来的にC6/C5のRAM実測後に
緩和を検討してよい**（C3より大きいSRAMを持つチップもあるため，本当に
排他が必要かは未検証。当面は安全側でC3ルールを継承）。

新設`esp_bt.cmake`（`asp3/target/{esp32c6,esp32c5}_espidf/esp_bt.cmake`）
は，C3の`esp_bt.cmake`の4セクション構成（インクルードパス／ソース
ファイル／リンクライブラリ／ROM関数ld）を踏襲しつつ，中身を1.2節の
差分表に従って置き換える：

- ソースファイル：`controller/{esp32c6,esp32c5}/bt.c`＋`ble.c`（両方
  必須，1.2節）＋新設シム（`bt_shim_c6.c`のような新ファイル名を想定）。
- リンクライブラリパス：
  `-L${ESP_HAL_DIR}/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6`
  （C61なら`/esp32c61`），
  `-L${ESP_HAL_DIR}/components/bt/controller/lib_esp32c5/esp32c5-bt-lib`。
- リンクライブラリ名：`ble_app`（C3の`btdm_app`ではなくこちら）＋
  `phy`＋`coexist`。**`btbb`相当は不要と判明**——`hal/components/
  bt/CMakeLists.txt`のCONFIG_BT_CONTROLLER_ENABLED分岐
  （L1073-1096）は`libble_app`一つだけを`add_prebuilt_library`して
  おり，C3のような「coex_pti_v2だけ別blob」という分割が無い
  （`libble_app.a`が自己完結）。
- ROM関数ld：1.2節の通り**BT専用ldは不要**（WiFi用に既に用意して
  いる`esp_rom/{esp32c6,esp32c5}/ld/`の共通ld一式で足りる見込み）。
- インクルードパス：C3節のパターン踏襲＋`${ESP_HAL_DIR}/components/
  bt/porting/npl/freertos/include`（npl_funcs_t提供元）を追加。

### 3.2 クロック/電源前提

**WiFi側で既に確立済みの範囲**：`modem_clock.c`＋`modem_clock_hal.c`
（`asp3/target/{esp32c6,esp32c5}_espidf/esp_wifi.cmake`で既にコンパイル
対象，`esp_wifi_adapter.c`が`modem_clock_module_mac_reset(PERIPH_WIFI_
MODULE)`等を呼ぶ実績あり）。C6/C5のbt.cが呼ぶ`modem_clock_module_
enable(PERIPH_BT_MODULE)`／`modem_clock_module_mac_reset(PERIPH_BT_
MODULE)`は，**同じドライバの別enum値**であり，ドライバ自体の移植は
BT有効化を待たずに完了している。BT節（`esp_bt.cmake`）は
`modem_clock.c`／`modem_clock_hal.c`をソースリストに含める必要がある
（WiFi非同時ONの制約があるため，BT単体ビルドでも自前で持つ必要が
ある——ESP32C3_BT+ESP32C3_WIFI非併存と同じ理由）。

**BLE固有に追加で要るもの（未検証・C3のemi.c:164相当が無い保証は
無い）**：C3では`SYSTEM_WIFI_CLK_EN_REG`のBBクロックビットが2ボード
JTAG差分でしか見つからなかった「隠れた依存」だった。C6/C5では
`modem_clock_module_enable(PERIPH_BT_MODULE)`が同型の役割を**設計上
持つはず**だが，このAPIの内部実装（`modem_clock_hal.c`の
`modem_clock_hal_enable_wifipwr_clock`相当のBT版があるか）がWiFi
経路と同じ堅牢さでBT用のビットマスクをカバーしているかは，**WiFi
bring-upの回帰テストでは一切検証されていない**（PERIPH_WIFI_MODULE
しか通っていない）。**最初のHWラウンドで最も疑うべき箇所**として
明記する（4節・5節）。

### 3.3 APM/バスマスタ前提

`docs/c5c6-lessons-for-s31.md`・`docs/wifi-shim-c6.md`実施87/88・
`docs/c5-bringup.md`実施42/43で確定・恒久化された修正
（`asp3/target/{esp32c6,esp32c5}_espidf/target_kernel_impl.c`内，
`apm_hal_enable_ctrl_filter_all(false)`＋`apm_hal_set_master_sec_
mode_all(APM_SEC_MODE_TEE)`）は，**ESP32C6_WIFI/ESP32C5_WIFIオプション
に依存しない無条件の起動時修正**であることを確認した（
`target_kernel_impl.c`の該当関数呼出しはWiFi optionのif分岐の外）。

`hal/components/esp_hal_security/include/hal/apm_types.h`を確認した
結果，WiFi/BT共有の無線サブシステムは**単一の`APM_MASTER_MODEM=4`**
であり（BT専用の別マスタIDは存在しない），この修正は**構造上BLEの
バスマスタアクセスも既にカバーしているはず**——ただし実測は全て
WiFi経路（DMA/レジスタアクセスがscan/RXという形）でのみ行われており，
**BT経路（コントローラのBB DMA/レジスタアクセス）での実測は一度も
無い**。これも最初のHWラウンドでの確認事項とする（同じMODEMマスタ
なので新規APM未初期化に当たる可能性は低いと見るが，「はず」止まり）。

## 4. アプリ

### 4.1 bt_smoke（D-1相当）

C3の`apps/bt_smoke/bt_smoke.c`はチップ非依存の書き方（`esp_bt_
controller_init/enable`＋`esp_vhci_host_*`という共通API名のみ使用，
`#ifdef`によるチップ分岐は`BT_PROBE_STOP_AFTER_INIT`という診断フラグ
のみで，C3固有シンボルへの直接依存は見当たらない）。**アプリ本体は
コピー不要，target.cmake側のASP3_APPLDIR切替えとBT/WiFi shim側の
差し替えだけで動く設計を目指す**——C6/C5向けに別ディレクトリを作る
必要が生じるとすれば，configファイル（`bt_smoke.cfg`）が
`bt/bt_cfg.h`（BT_TIMER_NUM等）をincludeしているためcfg自体は
target側（`asp3/target/{esp32c6,esp32c5}_espidf/bt/bt.cfg`）に
新設が要る（C3のbt.cfgと中身はほぼ同一の見込み）。

### 4.2 ble_host_smoke（D-2a相当）

同様にアプリ本体（`apps/ble_host_smoke/ble_host_smoke.c`）はNimBLE
標準APIのみ使用（`HRT_PROBE`診断フラグのみ`#ifdef`）でチップ非依存。
ただし1.2節の通り，**D-2aのビルド統合の中身（cmakeでリンクする
nimbleトランスポート層のソースファイル集合）はC3と異なる**——既定
経路（`CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT`＋
`CONFIG_BT_NIMBLE_ENABLED`）ではC3の`esp_nimble_hci.c`＋
`hci_esp_ipc_legacy.c`は使わず，代わりに`porting/npl/freertos/src/
npl_os_freertos.c`＋`porting/transport/src/hci_transport.c`＋
`porting/transport/driver/vhci/hci_driver_nimble.c`＋
`host/nimble/nimble/nimble/transport/esp_ipc/src/hci_esp_ipc.c`
（ホスト受信コールバックを`hci_transport_host_callback_register()`
で同一プロセス内に直接登録する，1.2節の差分表で確認した経路）を
使う。**アプリは共通・ビルド配線が別**という構図。なお
`CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE`を立てればC3と全く同じ
`esp_nimble_hci.c`＋`hci_esp_ipc_legacy.c`もビルド可能（SOC_ESP_
NIMBLE_CONTROLLERで排他されていない，CMakeLists L928-934）——
新経路の実装が難航した場合の代替案として温存する（6節）。

### 4.3 チップ非依存性の総括

アプリ層（`apps/bt_smoke`,`apps/ble_host_smoke`）は現状の書き方の
まま3チップ（C3/C6/C5）で共有できる設計になっている（確認済み）。
新設が必要なのはすべてtarget層（`asp3/target/{esp32c6,esp32c5}_
espidf/bt/`・`esp_bt.cmake`・`bt.cfg`）であり，これはCLAUDE.mdの
禁則（submodule非改変）にも整合する。

## 5. HWラウンド計画

### 5.1 段階と判定基準（C3のD-1→D-2a→D-2bをそのまま踏襲）

1. **D-1（controller alive + VHCI疎通）**：`esp_bt_controller_init()`
   が`ESP_OK`を返し，`esp_bt_controller_enable()`が完走し，
   `esp_vhci_host_send_packet()`でHCI RESETコマンドを送って
   `esp_vhci_host_callback_t.notify_host_recv`が呼ばれることを
   確認。判定基準はC3のD-1と同一（`docs/bt-shim.md` L83-99）。
   **この段階で3.2/3.3節の未検証項目（PERIPH_BT_MODULEクロック・
   BT経路APM）が最初に露出する**。
2. **D-2a（NimBLE host sync）**：`ble_hs_synced()`コールバックが
   呼ばれる（判定基準はC3と同一）。ただし1.2節の通り，ホストの
   HCI受信コールバックが`hci_transport_host_callback_register()`で
   コントローラ側トランスポート（`hci_transport.c`）へ**同一プロセス
   内で直接**登録される経路（C3の`esp_nimble_hci.c`＋
   `hci_esp_ipc_legacy.c`より1段薄い）のため，**C3で起きた「VHCI
   往復はできるがhost syncしない」という中間状態の**現れ方が変わる
   可能性がある（中間層のコピー・キューが1段減る分バグの入る余地は
   減るが，D-1で使う`hci_driver_standard.c`とD-2aで使う
   `hci_driver_nimble.c`は別ファイルなので「D-1が通ってもD-2aが
   別の理由で詰まる」余地自体は残る——最初のラウンドで実際の切り
   分け解像度を確認する）。
3. **D-2b（connectable advertising，ホストhci0で観測）**：
   `ble_gap_adv_start()`が返り，ホストPCの`bluetoothctl scan le`
   （BLE/C3手順再利用，MEMORY.md記載のhci0アダプタ）で広告フレーム
   （デバイス名／MACアドレス）を検出。C3のD-2bで踏んだ「BT_BB
   割込みストーム」（`docs/bt-shim.md` (1)〜(1)(o)）は**C3固有の
   btdm_app/bt.c ISR多重登録バグ**だったため，C6/C5の別世代
   コントローラ（blob内部が別実装）では**再現するとは限らない**
   ——ただし2.1節の通り`_esp_intr_alloc`が複数回呼ばれる可能性は
   構造的に排除できないため，シムは最初から多重登録耐性を持たせる
   （C3の教訓を先取り適用，同じ罠を二度踏まない）。

### 5.2 C6→C5の順序推奨

**C6を先に着手することを推奨する**。根拠：

- C6のWiFi bring-upは`memory/project_c6_agc_investigation.md`により
  「85ラウンドで一旦FROZEN（deaf-RX，software/JTAG層は exhausted）」
  だが，このFREEZEは**RXパス（AGC/CCA以降のフレーム認識）**の話で
  あり，BLEコントローラは全く別のコントローラ実体（blob）・別の
  クロック経路（`PERIPH_BT_MODULE`はWiFi RXの障害が起きている
  `lmacRxDone`等のMAC層より下流のBB/RF層を共有するとはいえ，
  ソフトウェアスタック的には独立）のため，**C6 WiFi deaf-RXの
  未解決状態はBLE bring-upの前提条件ではない**（並行して進めてよい）。
- C5のWiFi bring-upは`memory/project_c5_wifi_bringup.md`により
  「実施21（PMU/LP/PCR全域電源ドメインスイープ）が起動直後に中断
  ＝未実施」の**進行中**状態であり，C5の実機環境自体（flash内容
  不確定，別PCでの再開待ち）が不安定。**C6の方が実機環境が安定
  している**（実施85で明示的にFREEZEされ，安定した既知状態）。
- 1.2節の差分表の通りC5/C6はソースレベルでほぼ同一（`ble.c`の差分は
  `scan_stack_*`宣言4行のみ，`ble_priv.h`は完全一致）——**C6で得た
  知見の大部分はC5へそのまま横展開できる**見込みが高く，先に着手
  するチップの選択はリスクの低い方（C6）にすべき。

### 5.3 予想される壁（初踏経路）

1. **`PERIPH_BT_MODULE`クロック経路の未実測**（3.2節）：D-1の
   `esp_bt_controller_init()`内で`modem_clock_module_enable`が
   ハングする／レジスタ書込みが効かない可能性。C3の`emi.c:164`
   アサートに相当する「第一の壁」候補。
2. **BT経路でのAPM未初期化再発**（3.3節）：構造的にはWiFi修正で
   カバーされているはずだが未実測。もし再発する場合は
   `docs/wifi-shim-c6.md`実施87/88の手法（HP_APM例外ラッチの
   JTAG読み取り）がそのまま転用できる（診断手法は確立済み）。
3. **npl_os_freertos.cが要求するFreeRTOS APIの詳細シグネチャ不一致**
   （2.1/2.2節）：C3シムのヘッダ集合は一致するが，1269行全体の
   関数呼出し（`xQueueCreateStatic`か`xQueueCreate`か，`portMAX_
   DELAY`の扱い等）がC3のbt.c/nimble_port.cが要求していたものと
   完全一致するとは限らない。実装時の最初のビルドエラー地点になる
   可能性が高い。
4. **`_task_create`のcore_id引数**：C6/C5がデュアルコアか
   シングルコアか（BLE専用コアの有無）を実装時に`soc_caps.h`の
   `SOC_CPU_CORES_NUM`等で確認する必要がある（本ラウンドでは未確認
   ——リスクとして5.4節に記載）。
5. **coexスコープ外**：5GHz WiFi併用時のcoex，Thread/Zigbee
   （C6は802.15.4も持つ）との共存は，本設計の対象外と明記する
   （D-1〜D-2bはBT単体ビルド，WiFi同時ON禁止という3.1節の方針
   のもとでは原理的に無関係）。

### 5.4 未確認のまま残した事項（実装時に埋めること）

- `_esp_reset_modem`（C6のみ）の実装に何が要るか
  （`modem_clock.c`内の関連APIの存在確認は未実施）。
- BT+WiFi同時ON禁止をC3同様に踏襲する初期方針の妥当性
  （C6/C5のRAM実測をしていないため，本当に排他が必要かは未検証）。
- `libble_app.a`が要求する未解決シンボルが`phy`/`coexist`以外に
  無いか（`btbb`相当は不要と判明したが，これはCMakeLists記述からの
  推定であり，実リンク時に別シンボルが未解決になる可能性はゼロでは
  ない）。

（コア数・btbb相当リンクの要否は本ラウンド内でソース確認により
解決済み——1.2節・3.1節・6節1参照。）

## 6. リスクと未知

1. **hal内のC5用コントローラblobの実在**：確認済み。
   `hal/components/bt/controller/lib_esp32c5/esp32c5-bt-lib/
   libble_app.a`が存在し，IDF v6.1
   （`~/tools/esp-idf-v6.1/components/bt/controller/esp32c5/`）の
   `bt.c`/`ble.c`とhal側は99%一致（SPDX年号とUHCIログ出力関連の
   数行差分のみ）。**hal側がv6.1よりわずかに新しい**（UHCI出力
   対応が追加されている）ため，**v6.1からの取込みは不要**——hal
   （esp-hal-3rdparty submodule）が正本として十分。
2. **C6のcontroller blobの世代整合性**：`lib_esp32c6/esp32c6-bt-lib/`
   配下に`esp32c6`と`esp32c61`の2つのサブディレクトリがあり，
   `hal/components/bt/CMakeLists.txt`は`CONFIG_IDF_TARGET_ESP32C61`
   のとき`target_name=esp32c6`に読み替えつつ，blobパスは
   `esp32c61`サブディレクトリを選ぶ特別扱いをしている。本リポジトリ
   がターゲットとするのは無印C6（`esp32c6`サブディレクトリ）である
   ことを実装時に再確認すること（取り違えるとblob-source版数不一致
   の踏み台になりかねない——C6/C5 WiFi調査で繰り返し出てきた
   「blob-MD5一致の徹底」という教訓，MEMORY.md「Standing rule since
   実施73→74」と同種の注意点）。
3. **RAM予算**：libble_app.aのサイズ・NimBLEホストスタック
   （C3のD-2bで使った同じソース集合をほぼ流用する想定）のRAM実測を
   していない。C3ではBT+WiFi同時ON非対応がRAM予算由来だったが，
   C6/C5のSRAM構成がC3と同等かどうかは未確認（一般にC6/C5は
   より新しいチップでSRAMが大きい可能性があるが，未検証のまま
   「排他」を初期方針とした——5.4節）。
4. **coexアダプタの拡張範囲**：`osi_coex_funcs_t`の4関数
   （2.1節）はWiFi用`esp_coex_adapter.c`のno-op思想を継承できる
   「はず」だが，C6/C5世代のcoexが**呼び出し頻度・タイミング**の
   面でC3と異なる場合，no-opで本当に安全か（WiFi同時ON禁止の
   初期方針下では実害は出ないはずだが）は未検証。
5. **D-1↔D-2a↔D-2bの切り分け解像度低下リスク**（5.1節(2)で既述）：
   D-2aのホスト受信コールバック登録がコントローラ側トランスポートに
   同一プロセス内で直結するため，中間状態の見え方がC3と変わる可能性
   があり，その場合は問題の局在化に新しい計装（例：
   `ble_hs_hci_evt.c`へのトレース挿入等）が必要になる見込み。C3で
   確立した「2ボードJTAG差分」「RTC STOREレジスタへの診断トレース
   書込み」という調査方法論（`docs/bt-shim.md`,`memory/feedback_
   hardware_investigation_rigor.md`）はそのまま転用できる。
6. **D-2a新経路が難航した場合の代替**：1.2節・4.2節に記載の通り，
   `CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE`を立てればC3が使うのと
   同一の`esp_nimble_hci.c`＋`hci_esp_ipc_legacy.c`もC6/C5で
   ビルド可能（SOC_ESP_NIMBLE_CONTROLLERで排他されていないことを
   `hal/components/bt/CMakeLists.txt` L928-934で確認済み）。新経路
   （`hci_esp_ipc.c`）の実装・デバッグが難航する場合，こちらへ
   切り替えればC3のD-2a実装知見をほぼそのまま転用できる
   （リスクではなく保険——今のうちに選択肢として明記しておく）。

## 7. 参照した一次資料（本ラウンドで実際に読んだファイル）

- `asp3/target/esp32c3_espidf/bt/bt_shim.c`（547行，全読）
- `asp3/target/esp32c3_espidf/esp_bt.cmake`（全読）
- `asp3/target/esp32c3_espidf/bt/bt_cfg.h`・`bt/bt.cfg`（全読）
- `apps/bt_smoke/bt_smoke.c`・`apps/ble_host_smoke/ble_host_smoke.c`
  （grep差分確認）
- `hal/components/bt/CMakeLists.txt`（構造把握）
- `hal/components/bt/controller/{esp32c3,esp32c6,esp32c5}/bt.c`
  （esp32c6全読，esp32c3/esp32c5は差分・構造比較）
- `hal/components/bt/controller/{esp32c6,esp32c5}/ble.c`（両方全読）
- `hal/components/bt/host/nimble/nimble/porting/nimble/src/
  nimble_port.c`・`nimble/nimble/transport/src/transport.c`
  （SOC_ESP_NIMBLE_CONTROLLER分岐確認）
- `hal/components/bt/porting/npl/freertos/src/npl_os_freertos.c`
  （冒頭・include確認）
- `hal/components/esp_hal_security/include/hal/apm_types.h`
  （APM_MASTER列挙値確認）
- `hal/components/soc/{esp32c6,esp32c5}/include/soc/soc_caps.h`
  （`SOC_CPU_CORES_NUM=(1U)`確認）
- `hal/components/bt/host/nimble/nimble/nimble/transport/esp_ipc/
  src/hci_esp_ipc.c`（全読，D-2a既定経路のホストコールバック直結を
  実装レベルで確認）
- `hal/components/bt/controller/esp32c6/bt.c` L1150-1200
  （`hci_transport_init(HCI_TRANSPORT_VHCI)`呼出し箇所の再確認，
  advisorレビューで指摘を受け実装を再読して1.2/4.2/5.1/6節を訂正）
- `asp3/target/{esp32c6,esp32c5}_espidf/target_kernel_impl.c`
  （APM修正の無条件性確認）
- `asp3/target/{esp32c6,esp32c5}_espidf/wifi/esp_wifi_adapter.c`・
  `esp_wifi.cmake`（modem_clock移植状況確認）
- `docs/c5c6-lessons-for-s31.md`・`docs/wifi-shim-c6.md`（実施87/88
  該当箇所）・`docs/bt-shim.md`（構成把握・目次grep）
- `~/tools/esp-idf-v6.1/components/bt/controller/esp32c5/bt.c`
  （hal版との diff）
- `hal/nuttx/`配下：C6/C5のBLE統合前例は**見当たらなかった**
  （`hal/nuttx/src/platform/nimble/`はNPL実装のヘッダ/ソースのみで，
  C6/C5固有のBLE bring-up資産ではない）。

## 8. D-2c準備の横展開（2026-07-14）

C3のD-2c（接続確立→GATTサーバ）は，wipコミット`8476b55`として
「ビルド・flashまで到達，接続確立の実機確認は未達（別PCで再開予定）」の
状態で保全されている。本節は，そのC3 wipの内容をC6（ビルド検証込み）・
C5（静的移植のみ）へ横展開した記録。**実機（接続確立の確認）はC3/C6/C5
いずれも未実施**——本節はあくまで次段（実機再開時）の準備。

### C3 wip（8476b55）の内容

- `asp3/target/esp32c3_espidf/bt/stub/include/bt_nimble_config.h`：
  `CONFIG_BT_NIMBLE_GATT_SERVER=1`＋標準GAPサービス
  （`CONFIG_BT_NIMBLE_GAP_SERVICE=1`・`ATT_MAX_PREP_ENTRIES=6`・
  `SVC_GAP_CENT_ADDR_RESOLUTION=-1`・`SVC_GAP_PPCP_*=0`）を有効化。
- `apps/ble_host_smoke/ble_host_smoke.c`：`gap_event_cb`のCONNECT/
  DISCONNECTケースでLP_AON外の`0x60008xxx`域レジスタ（STORE6/STORE4，
  D-2bのstorm probe用レジスタをD-2cでは接続観測用に転用）へマーカを
  書込み，`main_task`冒頭で残値をクリア。`esp_nimble_init()`後に
  `ble_svc_gap_init()`／`ble_svc_gatt_init()`／
  `ble_svc_gap_device_name_set()`を呼ぶよう変更（`nimble_port_
  freertos_init()`より前）。

### C6への移植（ビルド検証込み）

- `asp3/target/esp32c6_espidf/bt/stub/include/bt_nimble_config.h`：
  機械的コピーはせず，既存構成との整合のみ行った——C6は元々
  `CONFIG_BT_NIMBLE_GATT_SERVER=1`・`ATT_MAX_PREP_ENTRIES=64`を
  実施02から保持済み（C3のwipが提案する値6より大きい既存値のため
  **変更しなかった**）。新規追加は`CONFIG_BT_NIMBLE_GAP_SERVICE=1`＋
  `SVC_GAP_CENT_ADDR_RESOLUTION=-1`＋`SVC_GAP_PPCP_MAX/MIN_CONN_
  INTERVAL=0`＋`PPCP_SLAVE_LATENCY=0`＋`PPCP_SUPERVISION_TMO=0`の
  5マクロのみ（`esp_nimble_cfg.h` L1846-1911で，C3と同一の
  `MYNEWT_VAL_BLE_SVC_GAP_*`写像コードであることをC6のnimble
  submoduleソースで確認済み）。
- `apps/ble_host_smoke_c6/ble_host_smoke_c6.c`：
  - GAP CONNECT/DISCONNECTマーカは，C3のようなSTORE転用ではなく，
    C6のLP_AON STOREマップ（実施02：STORE0/2/3/4/5/6/7使用済み，
    STORE1はノイズにつき使用禁止）の**未使用領域STORE8/9**
    （`hal/components/soc/esp32c6/register/soc/lp_aon_reg.h`で実在
    確認済み）を新規に明け渡した。フォーマットはC3と同一
    （`0x604E<status:8><conn_count:8>` / `0xD15C<reason:8>
    <disc_count:8>`）。
  - `main_task`冒頭でSTORE8/9を0へクリア。
  - `esp_nimble_init()`後に`ble_svc_gap_init()`／`ble_svc_gatt_init()`／
    `ble_svc_gap_device_name_set(BLE_DEVICE_NAME)`を追加（C3と同じ
    呼出し順序，`nimble_port_freertos_init()`より前）。
  - `services/gatt/ble_svc_gatt.h`のincludeを追加（`ble_svc_gap.h`は
    既存）。`esp_bt.cmake`は変更不要——`ble_svc_gap.c`／
    `ble_svc_gatt.c`は実施02から既にソースリストに含まれている。

**ビルド検証結果**（`riscv64-unknown-elf-gcc 13.2.0`，
`-DASP3_TARGET=esp32c6_espidf -DESP32C6_BT=ON
-DASP3_APPLDIR=apps/ble_host_smoke_c6 -DASP3_APPLNAME=ble_host_smoke_c6`）：

| ビルド | 結果 | FLASH | RAM |
|---|---|---|---|
| D-2c準備後（本ラウンド） | 0エラー | 349584B (8.33%) | 303188B (71.86%) |
| 変更前ベースライン（同一オプション，本ラウンド冒頭で確認） | 0エラー | 347760B (8.29%) | 303060B (71.83%) |

GATTサービス登録コード追加分（FLASH+1824B・RAM+128B）のみの増分で，
多重定義／未解決シンボルなし。

**非回帰確認**：
- `wifi_scan`（`-DESP32C6_WIFI=ON`）：0エラーでビルド成功
  （FLASH 542528B 12.93%・RAM 377224B 89.41%）。本ラウンドはBT側
  ファイルのみの変更でWiFi共有ファイルは無変更のため，形式確認に
  留めず実ビルドで確認した。
- C3 `ble_host_smoke`（`-DASP3_TARGET=esp32c3_espidf -DESP32C3_BT=ON`，
  wip`8476b55`のまま無改造）：0エラーでビルド成功（RAM 304572B
  92.95%）——C3側は本ラウンドで一切変更していないため，これはwip
  コミット自体のビルド健全性の再確認（コミットメッセージの「ビルド・
  flashまで到達」という記述と整合）。

### C5への移植（静的のみ・★ビルド未検証）

**★C5はこの環境でビルドできない**（`asp3/target/esp32c5_espidf/
esp_bt.cmake`が要求するESP-IDF v6.1，`/home/honda/tools/esp-idf-
v6.1`，が本開発環境に存在しないため）。以下は静的な整合確認のみ。

- `asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h`：
  C6と同型の5マクロ（`GAP_SERVICE=1`＋`CENT_ADDR_RESOLUTION=-1`＋
  `PPCP_*=0`）を追加。C5も実施05から`GATT_SERVER=1`・
  `ATT_MAX_PREP_ENTRIES=64`を保持済みのため変更せず。ファイル冒頭と
  追加箇所の両方に「★ビルド未検証（IDF v6.1環境で要ビルド）」を明記。
- `apps/ble_host_smoke_c5/ble_host_smoke_c5.c`：C6と同一の設計
  （STORE8/9をCONN/DISCマーカに新規使用，`main_task`冒頭クリア，
  `esp_nimble_init()`後の`ble_svc_gap/gatt_init`呼出し）を移植。
  C5のLP_AON STOREマップ（実施05：STORE0/2/3/4/5/7使用済み，STORE1は
  実施35のRTC_SLOW_CLK_CAL用途で予約）ではSTORE6/8/9のいずれも本アプリ
  内で未使用だが，クロスチップでの番地対応をC6と揃えるため**STORE8/9
  を選択**した（STORE6はC6が別用途＝reset_cbマーカに使っているため
  避けた）。ファイル冒頭と追加箇所の両方に「★ビルド未検証」を明記。
- `services/gatt/ble_svc_gatt.h`のincludeを追加。`esp_bt.cmake`は
  変更不要——C5も実施05から`ble_svc_gap.c`／`ble_svc_gatt.c`を
  ソースリストに含んでいる（IDF v6.1の`NIMBLE_ROOT`経由）。

**C5移植のリスク（v6.1マクロ体系との不整合の可能性）**：

1. C5のNimBLEホストはhal submoduleではなくIDF v6.1のソースツリー
   （`~/tools/esp-idf-v6.1`）を使う。今回追加した`esp_nimble_cfg.h`の
   ゲート構造（`ble_svc_gap.c`の`MYNEWT_VAL(BLE_GATTS) &&
   CONFIG_BT_NIMBLE_GAP_SERVICE`，`MYNEWT_VAL_BLE_SVC_GAP_PPCP_*`の
   `#ifndef`フォールバックがGAP_SERVICEのifdefの外にある構造）は，
   C6のhal submodule（esp-nimble）で実地確認したものであり，v6.1側の
   実ソースはこの環境から参照できないため**確認していない**。Apache
   Mynewt NimBLEの共通コードのため構造が一致する可能性は高いが，
   v6.1固有の差分（バージョン間でのリファクタリング等）がある場合，
   追加した5マクロだけでは足りない，または不要な可能性がある。
2. C3のD-2c wipで使われた`ATT_MAX_PREP_ENTRIES=6`という小さい値を
   踏襲せず，C5/C6の既存値`64`を維持した——この値はESP-IDF Kconfig
   既定値であり，C3が意図的に絞った理由（RAM節約，本ビルドは最小
   GATTサービスのみでlong writeがほぼ発生しない）が本ラウンドで
   継承されていない。C5/C6のRAM予算次第では見直しの余地がある
   （実施05のD-2aビルドはRAM 77.29%で「WiFiビルドの76.25%と同水準で
   余裕あり」と記録されているため，即座に問題化する可能性は低いと
   見るが未検証）。
3. C5は`ble_gattc.c`の不整合（実施02がC6で踏んだ壁）を「壁が1件のみ」
   （実施05）としてほぼ回避しているため，GATT_SERVER=1化に伴う
   ビルドエラーの発生パターンがC6と異なる可能性がある——次回ビルド時
   最初に確認すべき点。

### マーカアドレス表（SYNC/ADV/CONN/DISC）

| チップ | 用途 | アドレス | 値フォーマット | 検証状態 |
|---|---|---|---|---|
| C3 | ADV開始 | `0x6000805C` | `0x0ADE5000` | 実機到達済み（D-2b） |
| C3 | GAP CONNECT | `0x600080C0`（STORE6，storm probe転用） | `0x604E<status:8><conn_count:8>` | wip・未検証 |
| C3 | GAP DISCONNECT | `0x600080B8`（STORE4，storm probe転用） | `0xD15C<reason:8><disc_count:8>` | wip・未検証 |
| C6 | sync | `0x600B1000`（LP_AON STORE0） | `0x5ade51c0` | 実機到達済み（D-2a） |
| C6 | adv-return rc | `0x600B100C`（LP_AON STORE3） | `0xAD0000\|rc` | 実機到達済み（D-2b，rc=0） |
| C6 | GAP CONNECT | `0x600B1020`（LP_AON STORE8，新規） | `0x604E<status:8><conn_count:8>` | **本ラウンドでビルド検証**・実機未検証 |
| C6 | GAP DISCONNECT | `0x600B1024`（LP_AON STORE9，新規） | `0xD15C<reason:8><disc_count:8>` | **本ラウンドでビルド検証**・実機未検証 |
| C5 | sync | `0x600B1000`（LP_AON STORE0） | `0x5ade51c0` | 実機到達済み（D-2a） |
| C5 | adv-return rc | `0x600B1008`（LP_AON STORE2） | `0xAD0000\|rc` | 実機到達済み（D-2b，rc=0） |
| C5 | GAP CONNECT | `0x600B1020`（LP_AON STORE8，新規） | `0x604E<status:8><conn_count:8>` | ★未ビルド・未検証 |
| C5 | GAP DISCONNECT | `0x600B1024`（LP_AON STORE9，新規） | `0xD15C<reason:8><disc_count:8>` | ★未ビルド・未検証 |

（C6/C5のCONN/DISCは番地を意図的に揃えてある——クロスチップでの
read-mem手順を共通化するため。C3のみ別レジスタ域＝旧世代コントローラの
storm probe転用という別経路であることに注意。）

### 実機再開時の確認手順（次段）

1. 対象ボードへ`ble_host_smoke_{c6,c5}`（`ESP32C{6,5}_BT=ON`＋
   `ESP32C{6,5}_BT_NIMBLE`自動ON）をフルビルド・flashする
   （C5は本ラウンドでは未ビルドのため，IDF v6.1環境で最初にビルドを
   通すこと——上記「C5移植のリスク」節を参照）。
2. ホストPCの`bluetoothctl`で対象デバイス（`ASP3-C6-BLE`/
   `ASP3-C5-BLE`）へ`connect`する
   （事前に`scan le`でMACを確認．D-2b実機記録のMAC/RSSIをdocs/
   ble-c5c6.md実施02/05から参照）。
3. `esptool --chip {esp32c6,esp32c5} --before usb-reset --after
   no-reset read-mem 0x600B1020`（CONNECTマーカ）・`0x600B1024`
   （DISCONNECTマーカ）を読み，`0x604E....`／`0xD15C....`の書込みを
   確認する（JTAG不要，adv/接続のRF活動下でも安全）。
4. 接続確立が確認できたら，ホスト側で`gatttool`／
   `bluetoothctl gatt.list-attributes`等でGATTディスカバリを行い，
   標準GAPサービス（Device Name／Appearance特性）が見えることを
   確認する。
5. 結果を確認したら，C3担当エージェントが`docs/bt-shim.md`へD-2c節を
   追記する（本リポジトリの取り決め——本ラウンドでは`docs/bt-shim.md`
   は変更していない）。C6/C5側の結果は本節（`docs/ble-c5c6-plan.md`
   「8. D-2c準備の横展開」）を更新する。

## 9. D-2c/D-2d本体のC6移植（2026-07-14）

C3で実機到達したD-2c（GATTディスカバリ開通・ACL経路のE_CTX修正
`f9dae7d`）／D-2d（自前GATTサービスread/write/notify `1190be9`，
SMP/bonding `da5d02d`/`ae21e7a`）をC6へ移植した．**実機は無い環境**
（この環境にESP32-C6ボードは接続されていない）のため，完了条件は
「コード移植＋この環境でのビルド（リンク）成功」．実機確認は次段
（10節）へ持ち越す．C5側は本ラウンドでは対象外（前回ラウンドの静的
移植のみで留め置き，IDF v6.1環境が無いため引き続き未ビルド）。

### E_CTX修正のC6適用判断（最重要）

`f9dae7d`はACL受信配送（`esp_nimble_hci.c`のOS_ENTER_CRITICAL区間）が
bt-stubの`portENTER_CRITICAL`＝`esp_shim_enter_critical`＝生の
`csrrci mstatus,8`（MIEクリア）を経由し，本RISC-Vポートの
`sense_lock()==(mstatus.MIE==0)`によりCPUロック扱いとなって
`tsnd_dtq`がE_CTXを返す→旧`esp_shim_queue_send`がE_CTXを検出できず
黙って送信失敗を返す→NimBLEの`npl_freertos_eventq_put`がこの契約
違反を検出できずイベントを取りこぼす，という**shared `wifi/esp_shim.c`
のキュー実装（プラットフォーム非依存の欠陥）**の修正である
（アプリ層でもコントローラ層でもなく，target/wifiシム層のバグ）。

C6は同じ`asp3/target/esp32c6_espidf/wifi/esp_shim.c`を持ち（C3版と別
コピー），`esp_shim_enter_critical`/`exit_critical`によるネスト対応
クリティカルセクション実装も同一パターンで既に持っていた（BLE実施01
で先取り実装済み）．しかし修正確認の結果，**C6は同型の欠陥を実際に
抱えていた**：

- `asp3/target/esp32c6_espidf/wifi/esp_shim.c`の`esp_shim_exit_critical()`
  （修正前）は最外解除でMIEを`csrsi mstatus,8`により復帰するだけで，
  `esp_shim_queue_flush_pending()`/`esp_shim_sem_flush_pending()`の
  ような保留flushを一切呼んでいなかった。
- `esp_shim_queue_send()`（修正前）は`shim_que_slot_alloc()`→
  `tsnd_dtq()`のみで，E_CTX時のフォールバック（pend_ring・保留セマフォ
  give）を持たない旧世代の実装のままだった（C3のD-2c以前と同型）。
- `shim_sem_id`/`shim_mtx_id`/`shim_dtq_id`/`tskid_tbl`の各静的配列も
  `#ifdef TOPPERS_ESP32C6_BT_NIMBLE`によるBT_NIMBLE分の追加スロット
  （SEM25-28／MTX9-12／DTQ5-8／TSK7-8）を持たず，共有
  `esp_shim_cfg.h`（`ESP_SHIM_NUM_SEM`等をNIMBLE時28/12/8/8へ拡張済み）
  との整合が取れていなかった（配列初期化子が短く，末尾スロットのIDが
  常に0＝未使用のまま）。

これは「未検証のまま放置されていた同型バグ」であり，**C6の対応箇所に
同型の欠陥があった**。真因の根（`sense_lock()==(mstatus.MIE==0)`と
`portENTER_CRITICAL`がRISC-VのMIEを直接クリアするというasp3_core
アーキ設計）はC3/C6で完全に共通のため，発現条件（BTクリティカル
セクション内からの`tsnd_dtq`/`sig_sem`呼出し）が揃えば同じ症状
（GATTディスカバリ未解決・暗号後の鍵配布ACL不達）が起き得ると判断し，
**C3の現行`wifi/esp_shim.c`（`f9dae7d`＋その後のセマフォE_CTX修正
`1b8e028`・SVC_PERROR診断込み）をC6版へ移植した**（下記「変更した
ファイル」参照）。

C5側の適用状況：C5は`asp3/target/esp32c5_espidf/wifi_v8/esp_shim.c`
（`e139a30`でC3の現行版から再生成済み）に**既に**pend_ring・保留
セマフォgive・SVC_PERROR診断のフル実装が入っている（`e139a30`は
`f9dae7d`と`1b8e028`の**両方より後**のタイムスタンプでC3版を丸ごと
再生成したため）。今回のC6移植はこのC5の適用形（＝現行C3版をchip
差分のみ変えて丸ごと反映する方式）に合わせた。

### 変更したファイル

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c6_espidf/wifi/esp_shim.c` | **E_CTX修正（keeper）**：C3現行版のキュー実装（空きスロットセマフォ`SHIM_QSEM1-8`＋保留リング`pend_ring`＋`sem_debt`会計）・セマフォ保留give（`shim_sem_pend`）・`SVC_PERROR`（本ビルドは常時パススルー，診断オプション無し）をC6版へ移植．`esp_shim_exit_critical()`最外解除で`esp_shim_queue_flush_pending()`/`esp_shim_sem_flush_pending()`を呼ぶよう修正．`shim_sem_id`/`shim_mtx_id`/`shim_dtq_id`/`shim_qsem_id`/`tskid_tbl`の各配列へ`#ifdef TOPPERS_ESP32C6_BT_NIMBLE`拡張スロットを追加．C6固有部分（`esp32c6_systimer_read`・`WDEV_RND_REG`＝`0x600B2808`・`wifi_trace.h`計装・`esp_shim_modem_icg_init`・`shim_int_dispatch`のMAC割込み診断）は無変更で温存 |
| `apps/ble_host_smoke_c6/ble_host_smoke_c6.c` | D-2c本体：自前サービス0xABF0（`0xABF1` READ "BT4-OK"／`0xABF2` READ\|NOTIFY 32bit LEカウンタ／`0xABF3` WRITE）＋`notify_tick`＋GAP SUBSCRIBE/MTU処理を追加（C3 `1190be9`の逐語移植）．D-2d本体：`TOPPERS_ESP32C6_BT_SM`ガードでSM設定（`ble_store_config_init`・`sm_io_cap=NO_IO`・`sm_bonding=1`・`sm_mitm=0`・`sm_sc=1`・鍵配布ENC\|ID）・`bt6_security_tick`（接続5秒後のslave SecReq）・ENC_CHANGE/PAIRING_COMPLETE/REPEAT_PAIRING/PASSKEY_ACTIONハンドラ・暗号必須特性`0xABF4`を追加（C5 `369a86a`のパターンをC6へ適用）．`main_task`をsync待ち後の定常ループ化（1秒周期notify/security tick＋100ms周期flush_pending，C3 `1190be9`と同型）．保留リングflush用externを追加 |
| `asp3/target/esp32c6_espidf/esp_bt.cmake` | `option(ESP32C6_BT_SM ON)`をESP32C6_BT_NIMBLEブロック内に新設（C5 `ESP32C5_BT_SM`と同型）．ON時：`TOPPERS_ESP32C6_BT_SM`定義（`MYNEWT_VAL_BLE_SM_LEGACY/SC`の`-D`上書きをしない＝`bt_nimble_config.h`の`CONFIG_BT_NIMBLE_SM_LEGACY/SC`定義の**有無**で`esp_nimble_cfg.h`の`#ifdef`ゲートが1に倒れる仕組みを利用）．OFF時：従来通り`MYNEWT_VAL_BLE_SM_LEGACY=0`/`SC=0`を`-D`で明示上書き．tinycrypt（`aes_encrypt.c`/`cmac_mode.c`/`ecc.c`/`ecc_dh.c`/`utils.c`）＋`ble_store_config.c`をSM ON時のみ追加リンク，OFF時は従来通り`ble_store_ram.c` |
| `asp3/target/esp32c6_espidf/bt/stub/include/bt_nimble_config.h` | `CONFIG_BT_NIMBLE_HS_PVCY 1`を追加（C3 `ae21e7a`／C5 `369a86a`系列の同一修正．PVCY=0だと`ble_sm.c`のresponder Identity鍵配布が丸ごとコンパイルアウトされ鍵配布ACL不達→30秒ETIMEOUTで bond不成立になる真因への対策）．`CONFIG_BT_NIMBLE_SM_LEGACY`/`SM_SC`/`SECURITY_ENABLE`/`LL_CFG_FEAT_LE_ENCRYPTION`の値そのものは変更していない（`#ifdef`ゲートの罠のため，値ではなく定義の有無とcmake側`-D`上書きの組合せで制御される．C5の`bt_nimble_config.h`と同型） |

### マーカ表の更新（LP_AON STORE，C6）

C6のLP_AON STORE0-9（`hal/components/soc/esp32c6/register/soc/
lp_aon_reg.h`で実在確認．10個のみ，STORE10以降は非実在——C5が
`2884922`で踏んだ「STORE10/11非実在」の教訓と同一の制約がC6にも
適用される）は，本ラウンド開始時点で**既に0-9まで全て割当て済み**
だった（STORE0 sync／STORE1 noise回避／STORE2 adv試行／STORE3
adv-return rc／STORE4-5 割込みレートミラー／STORE6 reset_cb／STORE7
intr_allocトレース／STORE8-9 CONNECT・DISCONNECT）．D-2c/D-2dで新規に
必要なマーカ（WRITE受信・ENC_CHANGE・PAIRING_COMPLETE）に空きレジスタ
が無いため，C5の`2884922`（ENC/PAIRINGをSTORE6共用へ切替えた前例）に
倣い，**書込み頻度が低い既存レジスタをタグ判別で共用**した：

| チップ | 用途 | アドレス（共用先） | 値フォーマット | 検証状態 |
|---|---|---|---|---|
| C6 | WRITE(0xABF3)受信 | `0x600B100C`（STORE3，adv-return rc共用．タグ`0xAD00`と`0x7717`で判別） | `0x7717<write_count:8><先頭byte:8>` | ビルド検証済み・実機未検証 |
| C6 | ENC_CHANGE | `0x600B1018`（STORE6，reset_cb共用．タグ`0x5E00`と`0x5DE0`で判別） | `0x5DE0<status:8>` | ビルド検証済み・実機未検証 |
| C6 | PAIRING_COMPLETE | `0x600B101C`（STORE7，intr_allocトレース共用．タグ`0xA1`と`0x5DC0`で判別） | `0x5DC0<status:8><our_sec:4><peer_sec:4>` | ビルド検証済み・実機未検証 |

（STORE3/STORE6/STORE7はいずれも「接続中・pairing中は再書込みされない
（または稀）」という性質を根拠に選定した——C3が`intr_alloc`トレース用
レジスタ0x54をPAIRING_COMPLETEに転用したのと同じ考え方．STORE8/9
（CONNECT/DISCONNECT）は既存のまま変更なし。）

### ビルド結果一覧

全ビルドは本開発環境（`riscv64-unknown-elf-gcc 13.2.0`，
`-DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration`要，gcc-15系
implicit-function-declarationの既知事象は本ラウンド範囲外）で実施．

| 構成 | オプション | 結果 | FLASH | RAM |
|---|---|---|---|---|
| C6 D-2c（GATTサービス，SM無し） | `-DASP3_TARGET=esp32c6_espidf -DESP32C6_BT=ON -DESP32C6_BT_SM=OFF -DASP3_APPLDIR=apps/ble_host_smoke_c6 -DASP3_APPLNAME=ble_host_smoke_c6` | 0エラー | 353968B (8.44%) | 303716B (71.99%) |
| C6 D-2d（GATTサービス＋SM/tinycrypt） | 同上＋`-DESP32C6_BT_SM=ON` | 0エラー | 390704B (9.32%) | 305092B (72.32%) |
| 回帰：C6 bt_smoke_c6（コントローラのみ） | `-DASP3_APPLDIR=apps/bt_smoke_c6 -DASP3_APPLNAME=bt_smoke_c6 -DESP32C6_BT=ON` | 0エラー | 302448B (7.21%) | 277760B (65.84%) |
| 回帰：C6 wifi_scan（共有esp_shim.c変更の確認） | `-DASP3_APPLDIR=apps/wifi_scan -DASP3_APPLNAME=wifi_scan -DESP32C6_WIFI=ON` | 0エラー | 543696B (12.96%) | 377496B (89.48%) |
| 回帰：C3 ble_host_smoke（無改造，wip 8476b55＋D-2c/D-2d本体） | `-DASP3_TARGET=esp32c3_espidf -DESP32C3_BT=ON -DASP3_APPLDIR=apps/ble_host_smoke -DASP3_APPLNAME=ble_host_smoke` | 0エラー | IROM 249264B(5.94%)/DROM 276976B(6.60%) | 306492B (93.53%) |

D-2c→D-2dのRAM増分は約1376B（tinycrypt 5ソース＋`ble_store_config.c`＋
SM関連コード分）．C6のRAM残量はWiFi版（89.48%）より大幅に低い
（72%台）ため，「C6はRAM残量が厳しい」というD-2c準備時点の懸念
（71.86%）は据え置きだが，SM追加分だけで溢れる兆候は無い（C5/C3が
使った特別な削減策の移植は今回不要だった）。

### 実機確認待ち事項（次段）

1. **E_CTX修正の実機効果**：C6は元々D-2c相当までしか実機到達して
   いない（GAP CONNECT/DISCONNECTマーカのビルド検証のみ・接続確立の
   実機確認は本ラウンドまで未実施）ため，「修正前は本当にGATT
   ディスカバリが詰まっていたか」はC3のような修正前後比較ができて
   いない（C6は今回が初のGATTサービス＋E_CTX修正込みの実機投入と
   なる）。次段で`ble_host_smoke_c6`（D-2c構成）を実機board Cへ書込み，
   ホストPCの`bluetoothctl`で`ServicesResolved=true`＋自前サービス
   `0xABF0`のディスカバリを確認すること。
2. **WRITE/ENC_CHANGE/PAIRING_COMPLETEマーカの共用設計の実地検証**：
   タグ判別（上位ニブル/バイト）による設計は机上では安全なはずだが，
   実機での`esptool read-mem`による確認は未実施。特にSTORE3（adv-return
   rcとWRITE共用）は，central側が意図的にdisconnect→再scan→connectを
   繰り返すテスト手順だと再advでrcマーカに上書きされるタイミングが
   シビアになりうる（単発dump-mem採取のタイミングに注意）。
3. **D-2d bond実機確認**：C3の`da5d02d`/`ae21e7a`はC3/C5の共通
   `esp_shim`起因ではなく`CONFIG_BT_NIMBLE_HS_PVCY`（NimBLEホスト設定）
   起因と判明した真因のため，チップ非依存の設計上はC6でも同様の効果
   （`sm_tx>0`でIdentity鍵配布が成立しbondが通る）が期待できるが，
   **C6実機でのbond成功はまだ未実証**。C5実機ヘルパ`tmp/c5ble.sh`を
   参考にC6用ヘルパ（`tmp/c6ble.sh`，chip=esp32c6，ASP3_APPLDIR=
   apps/ble_host_smoke_c6，`ESP32C6_BT=ON -DESP32C6_BT_SM=ON`）を用意し，
   C5と同じ手順（`bluetoothctl`の`agent NoInputNoOutput`→`default-agent`
   →`connect`→（暗号必須特性`0xABF4`のreadで`pair`を誘発，または
   `bt6_security_tick`の自発的SecReqを待つ）→`0x600B1018`
   （ENC_CHANGE，タグ`0x5DE0`）・`0x600B101C`（PAIRING_COMPLETE，タグ
   `0x5DC0`）を`esptool --before usb-reset --after no-reset read-mem`で
   確認）で実機再テストすること。
4. **read/write/notifyの実機確認**：`0xABF1`（READ "BT4-OK"）・
   `0xABF2`（NOTIFY，subscribe後1秒周期カウンタ）・`0xABF3`（WRITE，
   `0x600B100C`のタグ`0x7717`で確認）をC3と同じ`bluetoothctl`手順
   （`gatt.select-attribute`／`read`／`notify on`／`write`）で確認する
   こと。

## 10. D-2c/D-2d実機初投入（2026-07-15）— BLEコントローラenableのPHY-init解析I2Cハングでブロック

`044305a`（§9のD-2c/D-2d本体C6移植）を**初めて実機board Cへ投入**した
ラウンド。結論：**D-2c/D-2dいずれも実機未達＝ブロック**。真因は
`044305a`のコードではなく，**BLEコントローラのPHY初期化中に解析I2C
（regi2c）読出しが無限スピンする起動ハング**であり，D-2c/D-2d本体
（GATTサービス・SMP・E_CTX修正）は**一度も実行されていない**（ハングの
手前で停止）。

### 環境（このPC）

- repo `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf`．GCC=`riscv32-esp-elf
  esp-14.2.0_20241119`．esptool=`tools/espressif/python_env/
  idf5.5_py3.12_env`（**このesptoolはサブコマンド/フラグが`_`区切り**：
  `write_flash`/`dump_mem`/`--before usb_reset`/`--after hard_reset`．
  `watchdog_reset`はC6非対応→classic RTS hard resetへ自動フォールバック）．
- DUT=C6 board C：JTAG/USB-Serial-JTAG=`14:C1:9F:E0:5A:9C`（by-id，現ttyACM0），
  UARTコンソール=CP2102N `125a266b…`（現ttyUSB2）．**C6のsyslogは
  USB-Serial-JTAG（ttyACM0）へ出る**（`esp32c6_usbjtag_hal.c`）——CP2102N/
  UART0にはROMブートログのみ．**USB-JTAGコンソールは自機のブートバースト
  を取れない**（RTSリセットでUSBが再enumerateし，リセット中のログが失われる）．
  よってブート診断は**esptool read-memのSTOREマーカ＋OpenOCDのPC読み**が
  主手段（本ラウンドの主判定はこの2系統）．hci0（BLE central）は
  他エージェント使用中につき本ラウンドでは不使用．
- OpenOCD=`tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260703`，
  C6は`board/esp32c6-builtin.cfg`＋`adapter serial 14:C1:9F:E0:5A:9C`で
  board C個体を指定．examination時に`abstractcs busy`スタックが出たら
  `riscv set_command_timeout_sec 12`で回避（初回失敗→リトライで成功）．

### 事前予測（ラウンド開始時）

§9の予測どおり「E_CTX修正済みなのでC6でもC3/C5同様connect→GATT→bondまで
通る見込み」＝**adv到達→sync→GATTディスカバリまでは最低限通る**と予測．
→ **外れた**：advにすら到達せず（sync前でハング）．

### 実測と物証（独立2ブート再現）

D-2cビルド（`build/c6ble_d2c_verify/asp_flash.bin`）をflash→RTS
hard resetでクリーンブート．**LP_AON STORE（esptool `--before usb_reset
--after no_reset dump_mem 0x600B1000 0x28`，ROMローダ経由でLP_AON生存）**：

| STORE | 意味 | 実測 | 判定 |
|---|---|---|---|
| STORE0 0x600B1000 | syncマーカ`0x5ADE51C0` | `0x00000000` | **sync未到達** |
| STORE2 0x600B1008 | adv開始試行 | `0x00000000` | **adv未試行** |
| STORE3 0x600B100C | adv-return rc / WRITE | `0x00000000` | adv rc未記録 |
| STORE4 0x600B1010 | 割込みレート(線1/2)ミラー | `0x00280028` | storm_monitor_task（BLE非依存の別タスク）は稼働 |
| STORE6 0x600B1018 | reset_cb / ENC_CHANGE | `0x00000000` | ble_hs reset未発火 |
| STORE7 0x600B101C | intr_allocトレース(0xA1) / PAIRING | `0x00000000` | **`esp_bt_controller_enable`のintr_alloc未到達**（bt_shim.c:255が0xA1…を書く） |
| STORE8/9 | CONNECT/DISCONNECT | `0x00000000` | 接続イベント無し |

STORE0=0（sync無）とSTORE7=0（controller enable内のintr_alloc無）から，
`main_task`は`esp_bt_controller_enable`の**PHY初期化段でハング**と局在．

**OpenOCDでPC読み（board C個体，D-2cビルドで独立2ブート）**：
- boot#1: PC=`0x42041214`（`0x42041212`↔`0x42041214`のタイトスピン），
  ra=`0x40003dfe`．
- boot#2（再flash＋再ブート）: PC=`0x42041212`/`0x42041214`，ra=`0x40003dfe`．同一．
- シンボル解決：PC=`ram_chip_i2c_readReg_org`（アプリflash内RAM関数），
  ra=`rom_chip_i2c_readReg`（C6 ROM）．周辺逆アセンブル：
  ```
  42041210: sw   a3,0(a2)        ; regi2c コマンド書込み
  42041212: lw   a5,0(a2)        ; ステータス読出し   ← スピン
  42041214: slli a4,a5,0x6       ; (mtval=0x00679713 と一致)
  42041218: bltz a4,42041212     ; done ビット待ち → 永久ループ
  ```
  ＝**解析I2C（regi2c）マスタのdoneビットが永遠に立たず，
  `chip_i2c_readReg`が無限スピン**．mtval=`0x00679713`は0x42041214命令と一致．

### 因果切り分け（反証実験・厳密性）

1. **`044305a`のD-2c/D-2dコードは無罪（PROVEN）**：コントローラのみの
   `bt_smoke_c6`（NimBLEホスト・自前GATT・SMP・esp_shimのE_CTX配列拡張を
   一切含まない）を現行ツリーからビルド→flash→ブートすると，**同一の
   `ram_chip_i2c_readReg_org`スピン**（PC=`0x42035c96`，ra=`0x40003dfe`）で
   ハング．よってハングは**BLEコントローラenableの共有PHY-init経路**であり，
   D-2c/D-2d移植・E_CTX shim改変とは無関係．
2. **実施91のICGアンロックは適用済み・かつ不十分（PROVEN＋HYPOTHESIS）**：
   `bt_smoke_c6`/`ble_host_smoke_c6`とも`esp_shim_bt_clock_init()`→
   `esp_shim_modem_icg_init()`をcontroller_initより前に呼ぶ．ハング中の
   OpenOCD mdwで**PMU `0x600B000C`=`0x80000000`＝dig_icg_modem_code=2**を
   実測＝**ICGアンロックのレジスタ書込みは効いている**．にもかかわらず
   解析I2Cが完了しない．よって「PMU ICG modem code=0」（実施90-91の真因）は
   **本BLE経路では必要条件だが十分条件ではない**．（HYPOTHESIS：BLE
   コントローラenableはWiFiの`esp_phy_enable`が行う追加のクロック/リセット
   （regi2cマスタクロック等）に依存しており，現行の`esp_shim_bt_clock_init`は
   それを供給していない——未検証．）
3. **board Cは大域的にPHY死ではない（PROVEN）**：現行ツリーから`wifi_scan`
   （`ESP32C6_WIFI=ON`）をビルド→flash→ブートすると，**同一のRTSリセット
   条件下でPHY初期化を通過**（OpenOCDでPC=`0x4002f478 __riscv_restore_1`／
   ra=`recv_packet`＝WiFi RX処理中，RF稼働でJTAG examinationが混雑）＝
   `chip_i2c_readReg`スピンには入らない．よってWiFiのPHY-init経路はcoldで
   成立し，**BLEコントローラ経路に固有のギャップ**．

### 重要な留保（over-claim回避）

- **全ブートはRTSリセット（`rst:0x15 (USB_UART_HPSYS)`）＝真の電源投入
  coldブートではない**．「coldブートハング」と断定しない：正しくは
  「RTSリセット下でハング．真の電源投入coldは未検証（＝MEMORY記載の
  ユーザー依頼中の物理電源断確認に相当）」．RTS-domain固有の病理か真の
  coldハングかは未確定．
- **D-2c/D-2dのGATT／E_CTX／SMPコードはC6実機で一度も実行されていない**．
  `044305a`のコードは本ハングの原因ではない（無罪）が，その**実挙動は
  C6で完全に未検証**．したがってE_CTX修正のC6での実効・PVCY挙動は
  **本ラウンドでは一切検証できていない**（§9「実機確認待ち事項」1-4は全て
  未達のまま）．
- `wifi_scan`は「PHY-initを通過（recv_packet到達）」であって「健全」とは
  書かない（初回halt時のPC=`0x40000000 _start`/debug_reason=8は未解明の
  loose endにつき依拠しない）．

### board C最終flash状態

D-2cビルド（`build/c6ble_d2c_verify/asp_flash.bin`＝`044305a`）をflash済み．
**このバイナリはPHY-init（`chip_i2c_readReg`）でハングし，advertisingしない**．

### 次段（申し送り）

1. **PHY-initハングを先に根治**：WiFi（`esp_phy_enable`）がcoldで通過し
   BLEコントローラenableが通過しない差分＝クロック/リセット設定の差を
   洗い出す（regi2cマスタクロック，MODEM_LPCON/MODEM_SYSCON，BBPLL等）．
   `esp_shim_bt_clock_init`にWiFi相当の追加初期化を足す方向．1仮説=1
   build+flash+OpenOCDサイクルで反証しながら（相関を因果と早合点しない）．
   可能ならユーザーに**物理電源断→真coldブート**の1回確認を依頼し，
   RTS-domain固有か真coldハングかを切り分ける．
2. **PHY-init根治後**：board Cがadvertising（STORE0=`0x5ADE51C0`・
   STORE2/3のadv rc=0マーカ）まで到達したら，**スマホ（nRF Connect等）
   でcentral操作**をコーディネータ経由でユーザーへ依頼：
   scan（ASP3-C6-BLE）→connect→サービス`0xABF0`ディスカバリ→`0xABF1`
   read／`0xABF3` write／`0xABF2` notify購読．結果はDUT側STOREマーカ
   （STORE8 CONNECT `0x604E…`／STORE3 WRITE `0x7717…`）をesptool
   read-memで機械確認．D-2dはさらにpair/bond→STORE6 ENC_CHANGE
   `0x5DE0…`／STORE7 PAIRING_COMPLETE `0x5DC0…`を確認．
   （hci0は他エージェント使用中につき使わない運用．）

## 11. PHY-initハング根因調査（2026-07-15）— regi2c/WIFIPWRクロック2件を根治，ただしD-1は未達（RF較正 register_chipv7_phy が収束せず）

§10のPHY-initハングを実機board C（`14:C1:9F:E0:5A:9C`，全ブートRTS
リセット）で根因調査したラウンド．**結論：クロック側の欠落を2件特定・
根治し，第1ハング（regi2c読出しスピン）は消滅したが，PHY初期化は次段の
RF較正（`register_chipv7_phy`）で新たにハングし，D-1（HCI_Reset往復）は
未達．この第2ハングはPHYブロブ内部のRFシンセ・ロック不成立で，明確な
レジスタゲートが無く，本ラウンドのスコープ（クロック/regi2c前提の欠落）
を超える＝申し送り．**

### 手法（WORKS/FAILS差分→単一ビット注入→読み戻し→ソース恒久修正→2ブート再現）

`bt_smoke_c6`（コントローラのみ・D-2c/D-2dコード非含有）を対象に，
無改造ベースライン→修正→wifi_scan(WORKS)比較の順で反証しながら進めた．
OpenOCD＝`board/esp32c6-builtin.cfg`＋`adapter serial 14:C1:9F:E0:5A:9C`，
`riscv set_command_timeout_sec 12`をinit前に置いてabstractcs busyを回避．

### 第1ハング＝regi2cマスタのクロック源未選択（sel_160m）——根治済み（PROVEN）

- **FAILS実測（無改造bt_smoke_c6，ハング中）**：PC=`ram_chip_i2c_readReg_org`
  （`0x42035d6x`），ra=`0x40003dfe`＝§10と同一のregi2c doneビット待ち無限スピン．
  MODEM_LPCON `0x600af018`=`0x0e`（`CLK_I2C_MST_EN` bit2は**既に1**——bt.cの
  `modem_clock_module_enable(PERIPH_BT_MODULE)`が立てる）だが，
  `0x600af010`（`I2C_MST_CLK_CONF`）=`0x00`＝`CLK_I2C_MST_SEL_160M` bit0が**0**
  ＝**解析I2Cマスタのクロック源（160M）が未選択**．PMU ICG `0x600b000c`=
  `0x80000000`（code=2，実施91のICGは効いている）．
- **ソース差分の特定**：WiFiパス（`wifi/esp_wifi_adapter.c`
  `wifi_clock_enable_wrapper`）は`esp_phy_enable`前に
  `_regi2c_ctrl_ll_master_enable_clock(true)`＋`regi2c_ctrl_ll_master_configure_clock()`
  （＝sel_160m=1）の**両方**を呼ぶが，BTパス`esp_shim_bt_clock_init()`は
  `esp_shim_modem_icg_init()`（ICG）だけで，sel_160mを一度も設定していなかった．
  `bt_shim.c`冒頭コメントの「BTに追加で要るのはICGアンロックだけ」は誤前提．
- **恒久修正（`asp3/target/esp32c6_espidf/bt/bt_shim.c`）**：
  `esp_shim_bt_clock_init()`にWiFiと同じ2行を追加．
  `#include "hal/regi2c_ctrl_ll.h"`はBT単体ビルド（`ESP32C6_BT=ON`,`WIFI=OFF`）
  でも解決しコンパイル成功を確認．
- **2ブート再現でPROVEN**：修正版でPC=`ram_chip_i2c_readReg_org`スピンが
  **消滅**し，`0x600af010`=`0x1`（sel_160m）が起動時から立つ．制御フローが
  次段へ前進した（＝この修正はregi2cスピンの直接因果を解消した）．

### 第2ハング＝RFシンセのロック不成立（register_chipv7_phy 非収束）——未解決・申し送り

第1修正後，PC=`ram_set_chan_freq_sw_start+0x1e`（`0x42033104`）へ移動して
新たにハング：
```
420330fc: lui   a4,0x600a0
42033100: lw    a5,204(a4)   ; 0x600a00cc（PHY/FE領域＝0x600a0000ブロック）
42033104: andi  a5,a5,256    ; bit8（synth-lock/freq-done）
42033108: beqz  a5,42033100  ; bit8が永久に立たず無限スピン
```
- **バックトレース（sp=`0x4084e370`からの復帰アドレスをaddr2line）**：
  `r_ble_lll_adv_start+0x868` → `register_chipv7_phy+0x110` → `bb_init+0x5c`
  → `tx_cap_init_loop` → `ram_set_chan_freq_sw_start`（現PC）．
  ＝**RF較正 `register_chipv7_phy`（＝esp_phy_enable(PHY_MODEM_BT)相当の
  full RF cal）は実際に走っている**．「esp_phy_enable呼出しの欠落」（H2の
  素朴形）は**反証**．ハングはPHYブロブのRFブリングアップ内部で，
  ベースバンド初期化→TXキャップ較正→チャネル周波数設定のシンセ・ロック
  待ちが成立しないこと．
- **WIFIPWRクロックの因果確認（副次発見・counter起因の切り分け）**：
  MODEM_PWRフリーランカウンタ`0x600ad000`はFAILSで0凍結・WORKS（wifi_scan
  のRX稼働時）でincrement継続．差分は`0x600af018` bit0（`CLK_WIFIPWR_EN`）＝
  WORKS=1／FAILS=0．**ハング中に`0x600af018 |= bit0`を単一注入すると
  `0x600ad000`が即座に凍結解除しフリーラン化（0→0x3e85→0x798e→…）**
  ＝WIFIPWRがこのカウンタのクロックゲートであることをPROVEN．
  →`esp_shim_bt_clock_init()`にWIFIPWR有効化（`0x600af018 |= 0x1`のRMW）を
  追加．修正版でカウンタは起動時からフリーラン（`0x600af018`=`0x0f`確認）．
  **しかしPCは`0x42033104`のまま前進せず**＝シンセ・ロックはこのmodem
  クロックドメインではゲートされていない（`0x600a00cc`=`0x25824e50`は
  クロックON/OFF・WORKS/FAILSに関わらず不変）．よってWIFIPWR修正は
  **カウンタ健全化に必要でWORKS一致・無害だが，D-1クリティカルパス上の
  寄与は未証明**（正直な位置づけ）．

### 根因の位置づけと次段の最有力仮説（コーディネータ情報を反映）

第2ハングの症状（`register_chipv7_phy`が走るがRFシンセ・ロックが収束
しない）は，**C5でhal版libphy（v8/os_adapter 0x08）がeco2でPHY較正収束
せずハングした事象（docs/ble-c5c6.md／C5実施09）と同型**．C5は
`esp_bt.cmake`でBLEをESP-IDF v6.1 matched set（v9/0x09 libphy）へ切替えて
収束させた（BTの`esp_phy_enable(PHY_MODEM_BT)`もWiFiと同一libphy経路の
ため）．**C6 BLEは現状hal submodule（esp-hal-3rdparty）のbt.c/libble_app.a/
libphyを使用**している．
**★libphy共有の確認（次段の優先順位を決める一次事実）**：BTビルド
（`build/c6bt_fix`）とWiFiビルド（`build/c6_wifiscan_works`）のnm比較で，
両者とも**同一のhal `libphy.a`の`register_chipv7_phy`**をリンクしている
（アドレスはリンクレイアウト差で異なるだけ・同一シンボル・同一実体）．
WiFi側のみ`__wrap_register_chipv7_phy`が在るが，中身は`__real_register_
chipv7_phy`を呼ぶだけの**診断トレースラッパ**（`wifi/wifi_trace.c`）で
機能差ではない．よって：
- C6 `wifi_scan`はこの共有libphyで`register_chipv7_phy`を通過（RF稼働）
  ＝**同じlibphyがWiFiでは収束しBTでは収束しない**．「libphy世代不整合」を
  単純な主因とは断定できない（swapは両経路の同一コードを差替えるだけで，
  バグが**BT専用サブパス**に在る場合しか効かない）．
- **より確度の高い差分＝`register_chipv7_phy`への入力**：バックトレース上に
  `phy_init_data`が在り，較正モード（WiFi=`esp_phy_enable(PHY_MODEM_WIFI)`
  →`register_chipv7_phy(init_data,cal_data,PHY_RF_CAL_FULL)`／BLE側は
  `r_ble_lll_adv_start`→blob内部からの呼出しで`init_data`・cal_data・
  モード引数が異なりうる）が経路依存．
- **次段の推奨（反証実験を先に）**：まず**BT vs WiFiの`register_chipv7_phy`
  入力（`phy_init_data`／cal_data／モード引数a0-a3）を実機で差分**する
  （WiFi側`__wrap_register_chipv7_phy`が既にa0-a3をトレース済＝
  `wifi_trace.c`のパターンをBT側`bt_shim.c`へ移植して同じ引数を採取・比較）．
  ここで差が出れば入力補正が本命修正．**フォールバック候補＝C6 BLEも
  C5同様にIDF v6.1 matched set（bt/phy/coex）へ切替**（C5 `esp_bt.cmake`
  9-16行の参照実装・C5実施09/10で同型のhal libphy非収束を解消した前例）．
  ＝「入力差分→ダメならv6.1 swap」の順（libphy共有の一次事実がこの順序を
  支持する）．本ラウンドのクロック2修正（regi2c sel_160m＋WIFIPWR）は
  modemクロック状態をWORKS一致へ揃える前提として**維持**（いずれの次段
  でも必要）．

### 回帰・board C最終状態・留保

- **wifi_scanビルド回帰**：`ESP32C6_WIFI=ON`で無傷ビルド（543856B FLASH
  12.97%／377496B RAM 89.48%）．本ラウンドの変更は`bt_shim.c`のみで
  `ESP32C6_WIFI`ビルドは同ファイルをコンパイルしないため影響なし．
- **board C最終flash**：`build/c6bt_fix`（regi2c＋WIFIPWR修正版bt_smoke_c6）．
  regi2cスピンは消え，RFシンセ・ロック待ち（`ram_set_chan_freq_sw_start`）で
  ハング＝advertisingしない．2ブートで同一挙動再現．
- **留保**：全ブートRTSリセット（真cold未検証）は§10のまま．D-1未達．
  第2ハングはPHYブロブ内部でありクロック/regi2c前提の追加では解けない
  （本ラウンドで確認）．**「done」とは主張しない**：regi2c修正は検証済みの
  部分前進，WIFIPWRはカウンタ健全化の必要条件だがD-1寄与は未証明．

## 12. register_chipv7_phy 入力差分（BT vs WiFi）— 入力同一を実測確定，非収束はlibphy内部（2026-07-15）

§11の申し送り「まずBT vs WiFiの`register_chipv7_phy`入力を実機で差分」を
実行したラウンド．**結論：BTとWiFiが`register_chipv7_phy`に渡す入力
（init_data／cal_data／モード）は実測で完全一致，かつ呼出し直前の
マシン状態（サンプルした11レジスタ）も機能的に同一．よって「入力差」は
反証され，RFシンセ非収束の分岐点は`register_chipv7_phy`内部（＝hal
`libphy.a`のBT依存サブパス）にある．次段はv6.1 matched setフォールバック
だが，C5実施09とは性質が異なり“効く保証は無い”（下記）＝申し送り．**

### 手法（HWブレークポイント＝ビルド改変も再フラッシュも不要で採取）

§11のバックトレースが示す通りハングは`register_chipv7_phy`の**内部**
（`register_chipv7_phy+0x110`がスタック上）＝関数には確実に到達している．
そこで両ビルド既存物にJTAG HWブレークポイントを`register_chipv7_phy`
エントリへ置き，命中時にa0-a3／ra（呼出し元）＋MMIOマシン状態を
`read_memory`で採取した（`mdw`はコンソール専用で採取不可，`read_memory`
は値を返すのでこちらを使用．flash直読みはstallするためinit_data内容は
ELFの`objdump -s`で静的に突合せ）．
- BTビルド＝`build/c6bt_fix`，`register_chipv7_phy`=`0x42032590`．
- WiFiビルド＝`build/c6_wifiscan_works`，`register_chipv7_phy`=`0x4202a1de`．
- OpenOCD＝`board/esp32c6-builtin.cfg`＋`adapter serial 14:C1:9F:E0:5A:9C`，
  別エージェントが既定ポート占有中のため`gdb/tcl/telnet port`を3355/4455/6655へ退避．

### 実測：入力は完全一致（3要素すべてMEASURED）

`register_chipv7_phy(const esp_phy_init_data_t* a0, esp_phy_calibration_data_t* a1, esp_phy_calibration_mode_t a2)`．

| 要素 | BT | WiFi | 判定 |
|---|---|---|---|
| a2 モード | `0x2`=PHY_RF_CAL_FULL | `0x2`=PHY_RF_CAL_FULL | **一致** |
| a1 cal_data 先頭24語 | 全0 | 全0 | **一致**（mac未memcpy＝両者ともNVS無し#else枝：phy_init.c:942） |
| a0 init_data 128B内容 | `0a005050…51`（末尾チェックサム0x51） | 同一バイト列 | **一致**（objdump -s でバイト単位突合せ） |
| ra 呼出し元 | `0x420046d2`=`esp_phy_load_cal_and_init`（phy_init.c:958，直接） | `0x420048e0`=`__wrap_register_chipv7_phy`→同じ`esp_phy_load_cal_and_init`が呼ぶ | **同一ソース関数** |

- **決定打＝ra（呼出し元）**：BT側は`--wrap`が無いため生の呼出し元が
  読め，`esp_phy_load_cal_and_init`（phy_init.c，WiFiと同一のソース関数）．
  §11のバックトレース「r_ble_lll_adv_start→register_chipv7_phy」は
  スタック上位のフレームで，直接の呼出し元は`esp_phy_enable`経由の
  phy_init.cソース＝**blobが独自init_dataで直接呼ぶのではない**．
  init_dataは両者とも`esp_phy_get_init_data()`（同一`phy_init_data.c`
  テーブル），cal_dataは同一ゼロ初期化バッファ，モードは同一リテラル
  `PHY_RF_CAL_FULL`．**入力差の余地は構造上も実測上も無い＝H「入力差」は反証**．

### 実測：エントリのマシン状態も機能的に同一（差はLP_TIMERのみ＝良性）

`register_chipv7_phy`エントリ命中時にサンプルした11 MMIO：

| レジスタ | BT | WiFi | 判定 |
|---|---|---|---|
| `0x600af018` LPCON_CLK | `0x0f` | `0x07` | **差**＝bit3 `CLK_LP_TIMER_EN`（BTのみON） |
| `0x600af010` I2C_MST_CLK(sel_160m) | `0x1` | `0x1` | 一致（§11修正） |
| `0x600a00cc` synth-lock | `0x25824e50` | `0x25824e50` | 一致（bit8=0，未走行） |
| `0x600ad000` PWR0カウンタ | `0xdde9` | `0x9b3a` | 両者フリーラン（値＝経過時間，一致相当） |
| `0x600a9804/9810/981c` SYSCON clk/rst/bb_cfg | `7e600000/0/0` | 同 | 一致 |
| `0x6009600c/60096028` PMU ICG/REG0 | `1/1` | `1/1` | 一致 |
| `0x600a0460/600a7030` FE_txrx/AGC_en | `06000000/c3c4bef5` | 同 | 一致 |

- 唯一の差＝**LPCON bit3 `CLK_LP_TIMER_EN`**（`modem_lpcon_reg.h`：bit0
  WIFIPWR/bit1 COEX/bit2 I2C_MST/**bit3 LP_TIMER**）．BTは`bt.c`の
  `modem_clock_module_enable(PERIPH_BT_MODULE)`がmodem LPタイマを使うため
  正当にONにする（WiFi scanは使わずOFF）．LP_TIMERは低消費電力の
  計時クロックで**RF/PLLクロックではない**＝synth-lock（`0x600a00cc`
  bit8）非成立の原因とは考えにくい良性差．
- ※「マシン状態同一」は**サンプルした11レジスタの範囲**での同一．
  synth-lock非成立は`register_chipv7_phy`**実行中**の動的な発散
  （エントリでは両者bit8=0，WiFiは実行中にbit8をラッチ・BTはしない）．

### 結論と次段（性質を正しくスコープした申し送り）

- **入力差ハングは反証（MEASURED）**：init_data 128B・cal_data・モードの
  3要素すべて一致，呼出し元も同一ソース関数，エントリ状態もLP_TIMER以外
  一致．**同一のhal libphy `register_chipv7_phy`が同一入力・同一状態で
  WiFiでは収束しBTでは非収束**＝発散点は関数内部で確定．
- **内部分岐の機構（＝仮説・未実測）**：`register_chipv7_phy`が
  phy modem-flag（`phy_set_modem_flag`／`phy_get_modem_flag`：
  `PHY_MODEM_BT` vs `PHY_MODEM_WIFI`）を読んでBT依存サブパスへ分岐する，
  というのが最有力機構仮説だが，**本ラウンドではflagの読値も分岐も未計装**
  ＝因果として主張しない（rigor基準準拠．firmするなら両ビルドでbpにて
  `phy_get_modem_flag`を読むのが安価）．
- **フォールバック＝v6.1 matched set swap（ただしC5実施09とは性質が異なる）**：
  C5実施09は**libphy版数がeco silicon不適合でWiFiも非収束**したため
  libphy全体swapで解決した．C6は**逆にWiFiが同じhal libphyで収束**する
  （§11 nm事実）＝BT経路固有の問題．したがってv6.1 swapが効くのは
  **v6.1のBT経路が異なる場合のみ**——差はlibphy本体ではなく`libble_app.a`
  のenable前セットアップに在る可能性が高く，**C5のように収束が保証される
  わけではない**．次段はこの前提でv6.1 bt/phy/coex matched setへ
  `esp_bt.cmake`を切替え（C5 `esp_bt.cmake` 9-16行の参照実装）て
  実機で可否判定する，が「効く保証なし」を明記して過度な期待を置かない．
  §11のクロック2修正（regi2c sel_160m＋WIFIPWR）はいずれの次段でも前提
  として維持．

### board C最終状態・留保

- **board C最終flash**＝`build/c6bt_fix`（§11と同一，本ラウンドで再フラッシュ・
  同一挙動：regi2cスピン無し・RFシンセ・ロック待ちハング）．採取のため
  一時的に`c6_wifiscan_works`をフラッシュしたが最終はBT版へ戻した．
- 留保：全ブートRTSリセット（真cold未検証）継続．D-1未達．本ラウンドは
  「入力差の反証＝クリーンなチェックポイント」で停止し，v6.1 swap本体は
  次段（効く保証なしの前提で）へ申し送る．


## 13. IDF v6.1 matched-set swap（2026-07-15）— ★C6 D-1 達成（register_chipv7_phy 収束・HCI往復成立・2/2ブート，トグル化非破壊）

§12 の申し送り「フォールバック＝v6.1 matched set swap（効く保証なし）」を
実装・実機検証したラウンド．**結論＝v6.1 matched set（bt/phy/coex/libble_app.a）へ
swap すると §10-§12 の hal 版が踏んだ `register_chipv7_phy` RFシンセ非ロックが
解消し，`esp_bt_controller_enable` OK ＋ HCI_Reset 往復成立＝C6 D-1 に到達した
（board C・RTSリセット・2/2ブート再現．§13.5）．留保＝真cold未検証（暫定）．**

### 13.1 決定的事実：v6.1 esp32c6 bt.c は C3型（＝C5構成を範とする）

`~/tools/esp-idf-v6.1/components/bt/controller/esp32c6/bt.c` を実測確認：
`xTaskCreatePinnedToCore`／`vTaskDelete`／標準 `esp_intr_alloc`/`esp_intr_free`
を直接呼び，`npl_freertos_*` を直接呼ぶ（**hal版のような
`platform/os.h` の `esp_os_*`・`npl_os_*` ドリフトは無い**）．つまり
v6.1 swap の C6 構成は hal版 esp_bt.cmake ではなく **C5 esp_bt.cmake
（IDF v6.1）の逐語転写**が正解——C5実施03/10 の資産がそのまま効く．

### 13.2 実装（トグル・非破壊・可逆）

`ESP32C6_BT_IDF61`（既定OFF）で hal版と排他選択する構成にした（hal版
D-2b 到達ビルドを温存＝「hal版との二択はユーザー判断」の申し送りに直結）：

- `asp3/target/esp32c6_espidf/esp_bt.cmake`：`if(ESP32C6_BT)` 直後に
  `option(ESP32C6_BT_IDF61 ... OFF)`＋`if(ESP32C6_BT_IDF61) include(esp_bt_idf61.cmake)
  else() <既存hal本体> endif()` を挿入（既存hal本体は無改変で `else()` に内包）．
- `asp3/target/esp32c6_espidf/esp_bt_idf61.cmake`（新規）：C5 esp_bt.cmake の
  IDF v6.1 構成を esp32c6 へ転写．**D-1（controller-only＝bt_smoke_c6）限定**で
  NimBLE ブロックは移植しない（`register_chipv7_phy` は
  `esp_bt_controller_enable→esp_phy_enable` 内で発火し bt_smoke_c6 が直接踏む＝
  シンセ・ロック判定にホストは不要．advisor指摘）．`ble_host_smoke_c6`＋IDF61 は
  FATAL_ERROR で明示的に弾く．origin-split（bt/phy/coex/os_mempool.c は IDF v6.1，
  hw_support/clock/rtc/efuse/periph/modem_clock は hal）・ld（v6.1 esp32c6，
  +rom.coexist.ld，−systimer.ld，−net80211/pp）は C5 と同一方針．
  ★XTAL/CPU は C6 の hal `sdkconfig.h`（`CONFIG_XTAL_FREQ=40`・
  `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`→`CONFIG_ESPRESSIF_CPU_FREQ_MHZ` 間接）を
  流用し `CONFIG_ESPRESSIF_CPU_FREQ_MHZ=160` のみ -D（C5のように XTAL を -D すると
  sdkconfig.h の後勝ちと競合＝hal版と同判断．esp_bt.h v6.1 の
  `BT_CONTROLLER_INIT_CONFIG_DEFAULT()` は両マクロを直接参照するため誤値注入は
  それ自体がシンセ非ロックの偽陰性要因＝ここは慎重に合わせた）．
- `asp3/target/esp32c6_espidf/bt/bt_shim_idf61.c`（新規）：C3型シム．hal版
  `bt_shim.c` から §11クロック（regi2c sel_160m＋WIFIPWR＋実施91 ICG）・C6割込み
  （INTMTX＋PLIC_MX．C5のCLICとは別＝C5シムは流用不可）・`esp_timer_*`/
  `esp_pm_lock_*`/`bt_timer_task`/`esp_partition_*`/`esp_random` を移植し，
  `esp_os_*` タスク/割込みと `npl_os_*` 橋渡しを除去（v6.1は C3 stub の
  `xTaskCreatePinnedToCore`→`esp_shim_task_create` と `npl_freertos_*` 直呼びで
  解決）．割込みは標準名 `esp_intr_alloc/free`（＋防御的に enable/disable）．
- `asp3/target/esp32c6_espidf/bt/bt_esp_timer_ext.h`（新規）：後述の壁2対策．

### 13.3 ビルドで踏んだ壁（GCC14.2 hard-error 群．C5実施03は寛容toolchainで通過）

いずれも「実ESP-IDFがビルドシステムで暗黙includeする前提のヘッダ」欠落を
GCC14.2 が暗黙宣言 hard-error にするもの．target側 force-include で解消：

1. **`xPortInIsrContext` 未宣言**（v6.1 `phy_init.c` の `phy_enter_critical`）：
   `phy_init.c` は `freertos/FreeRTOS.h`＋`portmacro.h` のみ include し
   `freertos/task.h`（`xPortInIsrContext`＝`sns_ctx()` の static inline がある）を
   include しない．C3 stub の `freertos/task.h`（自己完結・編集不可のC3領域だが
   read/参照は可）を `-include` して可視化．
2. **`esp_timer_is_active`／`esp_timer_get_expiry_time` 未宣言**（v6.1
   `npl_os_freertos.c`）：本ビルドの `esp_timer.h` は hal_stub版
   （`esp32c3_espidf/hal_stub/include/esp_timer.h`．PLL-track用の周期API のみ宣言）に
   解決され同2関数を持たない（hal版 esp_bt.cmake の `-include esp_timer.h` だけでは
   同stubを引くため無効＝実測）．hal_stub は C3領域で編集不可のため，C6側に
   `bt/bt_esp_timer_ext.h`（stub esp_timer.h を include し2宣言を補う）を新設し
   `-include` した．
（force-include は既存の soc/soc_caps.h・esp_attr.h・esp_idf_version.h・
sys/param.h・esp_bit_defs.h に上記2件を加えた計7件．）

### 13.4 3ビルド検証（ビルド/リンク全通過）

- **v6.1 IDF61 BT**（`bt_smoke_c6`＋`ESP32C6_BT_IDF61=ON`）：リンク成功．
  `build/c6bt_idf61/asp.elf`（text 305,520／RAM系 bss 276,864）．nmで
  `esp_bt_controller_init/enable`・`register_chipv7_phy`（**`0x42032abe`＝hal版の
  `0x42032590` から移動＝v6.1 libphy がリンクされた物証**．本ビルドの phy `-L` は
  v6.1 のみで hal phy パス無し＝no-op swap でないことを構造的に担保）・
  `esp_shim_bt_clock_init`・`esp_intr_alloc`・`esp_timer_is_active`・`bt_timer_task`
  を確認．
- **wifi_scan 回帰**（`ESP32C6_WIFI`＝hal）：リンク成功（トグル既定OFF＝
  `esp_bt.cmake` 改変は WIFI ビルドに非波及）．
- **hal BT 回帰**（`ESP32C6_BT`＝トグル既定OFF）：リンク成功（既存hal本体を
  `else()` 内包しただけ＝D-2b 到達ビルド温存・可逆）．

### 13.5 ★実機判定：v6.1 swap で register_chipv7_phy 収束＝**C6 D-1 達成（2/2ブート）**

board C（`14:C1:9F:E0:5A:9C`，全ブートRTSリセット）に `build/c6bt_idf61/asp_flash.bin`
をフラッシュ（esptool v4.12，`--before usb_reset --after no_reset --no-stub write_flash 0x0`，
hash verified）→ USB-JTAG コンソール（pyserial・RTSパルスreset）で2ブート採取：

```
bt_smoke_c6: esp_bt_controller_enable OK (heap free=180240)
bt_smoke_c6: intr trace = 0xa1020704 (nalloc=2 src1=7 src2=4)
bt_smoke_c6: intr rate/1s line1=0 line2=0 (storm threshold ~ >>1000/s)
bt_smoke_c6: controller enabled, sending HCI Reset
bt_smoke_c6: VHCI recv 7 bytes  [0]=0x04 [1]=0x0e [2]=0x04 [3]=0x01 [4]=0x03 [5]=0x0c [6]=0x00
bt_smoke_c6: HCI Command Complete received -> controller alive, VHCI loopback OK
bt_smoke_c6: Phase D-1 milestone reached
```

- **収束＝MEASURED**：`esp_bt_controller_enable(BLE)` が **OK で返り**（§10/§11/§12 の
  hal 版はここで `register_chipv7_phy` RFシンセ非ロック無限スピン），VHCI で
  **HCI_Reset（opcode 0x0c03）→ HCI Command Complete（event 0x0e・status 0x00）の
  往復が成立**．HCI 往復成立＝コントローラ＋RF が完全起動した直接証明
  （0x600a00cc bit8 の register 読みより強い機能的証拠）．**= C6 D-1 達成**．
- **割込み健全**：intr trace `0xa1020704`（nalloc=2・src1=7(RWBLE)・src2=4(BT_BB)）＝
  C6/C5 D-1 の既知良好シグネチャと完全一致，storm 非発生（line1=0/line2=0）．
  bt_shim_idf61.c の INTMTX/PLIC_MX スロット配列分離が機能．
- **2/2 ブート再現**（RTSリセット）．**留保：全ブートRTSリセット（真cold未検証）＝
  §10-§12 と同じ．earlier C6 D-2b が warm残留依存だった前例に鑑み，本収束も
  「v6.1 D-1 under RTS，真cold はユーザーの物理電源断で要確認」＝暫定として扱う．**

### 13.6 意味づけ（§12 の悲観を実機で更新）と board C 最終状態

- **§12 の「効く保証なし」を実機が肯定側に更新**：hal libphy `register_chipv7_phy` は
  §12 で「BT/WiFi 入力同一・非収束は関数内部」と確定していたが，v6.1 の
  `libble_app.a`（enable 前セットアップ）＋v6.1 `libphy` へ揃えて swap すると収束した．
  ＝非収束は **C6 シリコン固有ではなく hal matched set（libble_app.a/libphy）の
  BT enable サブパスの版問題**に局在（C5実施09の libphy 版数不適合とは別種だが，
  «hal 版 blob を v6.1 でまとめて置換すると解ける» という結果は C5 と同方向）．
- **board C 最終 flash＝`build/c6bt_idf61`（v6.1 D-1 成功ビルド＝既知良好状態）**．
  §11/§12 の `build/c6bt_fix`（hal・synth-lock ハング）には戻さない——本ラウンドで
  D-1 を達成したため，成功ビルドを載せた状態で残す（task の「成れば v6.1 D-1
  ビルド」に従う）．hal 版へ戻す必要が生じたら `ESP32C6_BT_IDF61=OFF` で再ビルド
  または `build/c6bt_fix/asp_flash.bin` を再フラッシュ（トグルで両立・可逆）．
- **hal 版との二択＝ユーザー判断として申し送り**：v6.1 D-1（本ラウンド達成）と
  hal D-2b（NimBLE ホスト到達済みだが cold で synth-lock）はトグルで両立．次段の
  D-2a 以降を v6.1 側で積むか，hal 側の synth-lock を別途詰めるかはユーザー選択．
  v6.1 側で D-2* へ進む場合は本 esp_bt_idf61.cmake に C5 の NimBLE ブロック
  （§8/実施05 相当）を転写する（今回は D-1 判定に不要のため未移植）．

## 14. IDF v6.1 版に NimBLE ホストを載せて **C6 D-2b 達成**（2026-07-15）— sync→adv rc=0，storm 非再発，2/2ブート

§13.6 の申し送り「v6.1 側で D-2* へ進む場合は C5 の NimBLE ブロックを転写する」を
実施した．§13 の v6.1 D-1（`esp_bt_idf61.cmake`・controller-only）の上に **IDF v6.1
（`~/tools/esp-idf-v6.1`）の NimBLE ホスト**を載せ，**D-2a（host-controller sync）→
D-2b（`ble_gap_adv_start` rc=0）を board C で実機到達**（2/2ブート・RTSリセット）．
**OTA検出（ホスト/スマホでの電波確認）はユーザー保留**＝デバイス側マーカ（LP_AON
STORE 直読み）とストーム非再発で確定した．

### 14.1 実装（C5 esp_bt.cmake の IDF v6.1 NimBLE ブロックの C6 転写・トグル非破壊）

- **トグル `ESP32C6_BT_IDF61_NIMBLE`**（既定 OFF，`ble_host_smoke_c6` で自動 ON）を
  `esp_bt_idf61.cmake` 冒頭に追加し，§13 の FATAL_ERROR（「NimBLE 未対応」）を撤去．
  `CONFIG_BT_CONTROLLER_ONLY=1` と `hci_driver_standard.c` を `if(NOT
  ESP32C6_BT_IDF61_NIMBLE)` へ移動（CONTROLLER_ONLY と NIMBLE_ENABLED は排他・
  `hci_driver_vhci_ops` の多重定義回避）＝**bt_smoke_c6 の D-1 は非回帰**．
- **ソースは hal ではなく `${IDF}`（v6.1）から採る**——本 D-1 が既に v6.1
  controller/phy を使うため，nimble も同一 matched set で揃え hal-nimble ＋
  v6.1-controller の未検証 blob-ABI 境界を作らない（C5 冒頭のライブラリ世代選定と
  同じ理由）．C5 の NimBLE ブロック（`hci_driver_nimble.c`／`hci_esp_ipc.c`／
  `nimble_port.c`／`nimble_port_freertos.c`／`esp_nimble_mem.c`＋ホスト本体トリム集合＝
  `ble_svc_gap`/`gatt` のみ）を逐語転写．
- **bt_nimble_config.h の隔離**：hal 版 `bt/stub/include/` には `platform/os.h`・
  `npl_os_bridge.h`・`nimble/nimble_port_os.h`（hal ドリフト吸収シム）が同居し v6.1
  nimble ヘッダを shadow して壊すため，**`bt/stub_idf61/include/`（bt_nimble_config.h
  «のみ»・LEGACY_VHCI=0）を新設**し C3 stub より前に PREPEND（C5 と同じ罠対策）．
  #define 値は build-verified な hal 版と同一．
- **SM（D-2d）は既定 OFF**（`ESP32C6_BT_IDF61_SM`）＝本ラウンドの目標 D-2a/D-2b は
  暗号不要のためビルド面を痩せさせ tinycrypt リンクを避ける．`MYNEWT_VAL_BLE_SM_
  LEGACY/SC=0` で `ble_sm*.c` を near-empty 化・bond store は `ble_store_ram`．
  ON で C5 と同じ tinycrypt5ソース＋`ble_store_config` へ切替（D-2d 拡張時・可逆）．
- **アプリ＝`apps/ble_host_smoke_c6`（実施02の D-2b app）を無改変流用**．デバイス名
  `ASP3-C6-BLE`・LP_AON STORE マーカ体系（STORE0=sync/STORE2=adv試行/STORE3=adv-return
  rc/STORE4-5=intr線1/2ミラー/STORE7=intr_allocトレース）．

- **ビルドで踏んだ壁＝ゼロ**：C5 の壁（`nimble_mem_free/calloc`→`esp_nimble_mem.c`
  追加）は転写時に既に織り込み済み．§13 の force-include 群（`bt_esp_timer_ext.h`・
  `freertos/task.h`）＋NimBLE用（`bt_nimble_config.h`・`syscfg/syscfg.h`）で警告のみ・
  一発リンク成功．**C5 の申し送りどおり `TRUE=1`／`BT_HCI_LOG_INCLUDED=0`／
  `-include sdkconfig.h` は不要**（v6.1 の `nimble_port.c` は `bt_common.h` を `#if`
  より前に include＝hal の順序バグは v6.1 に存在しない）．
- **v6.1 nimble に乗れている検証点（advisor 提案の tripwire）＝PASS**：`esp_nimble_mem.c`
  «と» `os_mempool.c` の両方がリンク（v6.1 blob は `nimble_mem_*`/`os_memblock_*` を
  未解決で残す＝hal blob なら不要）．SM-off ゲート完全＝`tc_aes`/`uECC`/`ble_store_config`
  シンボル 0 リーク（nm 確認）．`ble_gap_adv_start`/`esp_nimble_init`/`ble_hs_cfg` 在り．
  FLASH 8.79%・RAM 72.02%．

### 14.2 実機判定（cleared-boot-read・2ブート・氾濫排除＝LP_AON STORE 直読みを主）

board C（`14:C1:9F:E0:5A:9C`，全ブートRTSリセット）に `build/c6bt_idf61_nimble/
asp_flash.bin` をフラッシュ（esptool v4.12・`--no-stub write_flash 0x0`・hash verified）．
各ブート前に STORE0/2/3/4/5 を `write_mem 0` で pre-clear（cleared-boot-read）→ RTS boot →
`read_mem`（`--before usb_reset --after no_reset`＝LP_AON は usb_reset 生存）で採取：

```
                     boot1        boot2      判定
STORE0 (0x600B1000)  0x5ade51c0   0x5ade51c0  = BLE_SYNC_MARK_VAL → D-2a sync 成立
STORE2 (0x600B1008)  0x0ade5000   0x0ade5000  = BLE_ADV_MARK_VAL  → adv_start 試行到達
STORE3 (0x600B100C)  0xad000000   0xad000000  = 0xAD00_0000|rc(=0) → D-2b adv rc=0 成立
STORE4 (0x600B1010)  0x00000000   0x00000000  = 割込み線1 累積（source7→線1）
STORE5 (0x600B1014)  0x000007fb   0x000003f0  = 割込み線2 累積（~70-136/s＝正常域）
STORE6 (0x600B1018)  0x00000000   0x00000000  = reset_cb 未発火（ble_hs リセット無し）
STORE7 (0x600B101C)  0xa1020704   0xa1020704  = intr_alloc trace（nalloc=2 src7 src4）
```

- **D-2a sync＝MEASURED**：`ble_hs_cfg.sync_cb`（`on_sync`）到達＝STORE0 が `0x5ADE51C0`
  （偶然一致し得ない magic）．NimBLE ホストがコントローラと同期＝ホストスタック起動確認．
- **D-2b adv rc=0＝MEASURED**：STORE2 set（adv 試行）**かつ** STORE3=`0xAD000000`
  （`ble_gap_adv_start` の戻り値 rc=0）．advisor の識別（STORE2 set／STORE3 unset なら
  adv-start でのシンセ・ハング）に照らし **hang せず rc=0 で戻った**＝§11 の
  `r_ble_lll_adv_start→register_chipv7_phy` シンセ経路も v6.1 で通過．**= C6 D-2b 達成**．
- **storm 非再発**：STORE7 `0xa1020704`（C6/C5 D-1・実施02 の既知良好シグネチャと一致・
  多重登録なし）＋線2 累積が ~15s で 1000-2000（=70-136/s，実施02 の hal D-2b 45-48/s と
  同オーダー）．C3 級ストーム（1-3.8M/s）非再発＝`bt_shim_idf61.c` の INTMTX/PLIC_MX
  スロット配列予防設計が機能．STORE6=0＝ble_hs リセット無し（ホスト健全）．
- **2/2 ブート再現**（RTSリセット）．**留保：真cold 未検証（全ブートRTSリセット）＝
  §10-§13 と同じ．OTA電波確認（ホスト/スマホ）と物理電源断はユーザー保留．**

### 14.3 回帰・board C 最終状態・留保

- **回帰（自スコープ内）**：v6.1 D-1（`bt_smoke_c6`・NimBLE 自動 OFF）を `ESP32C6_BT_
  IDF61=ON` で再ビルド＝**リンク成功（非回帰）**．hal 版 BT（`ESP32C6_BT_IDF61=OFF`）・
  wifi_scan は本改変（`esp_bt_idf61.cmake`＋新 `stub_idf61` のみ）に非接触＝影響なし．
- **board C 最終 flash＝`build/c6bt_idf61_nimble`（v6.1 D-2b 成功ビルド＝既知良好状態）**．
  task「D-2b 到達すればそのビルド」に従い成功ビルドを載せRTS boot で running 状態で残す．
  D-1 へ戻すなら `build/c6bt_idf61`（§13）を再フラッシュ，hal 版へ戻すなら
  `ESP32C6_BT_IDF61=OFF` で再ビルド（トグルで三者両立・可逆）．
- **ユーザー保留**：(i) OTA検出（`ASP3-C6-BLE` の電波を hci0/スマホで確認）＝hci0 占有・
  スマホ手動のため未実施，(ii) 物理電源断による真cold起動＝全ブートRTSリセット留保のまま．
- **次段（申し送り）**：v6.1 側で D-2c（GATT ディスカバリ・自前サービス）／D-2d
  （`ESP32C6_BT_IDF61_SM=ON`＝SMP/bonding・tinycrypt）へ．app 側は実施02〜のD-2c/D-2d
  資産（STORE8/9 CONNECT/DISCONNECT・ENC/PAIR マーカ）が既存＝トグル ON で有効化．


## 15. IDF v6.1 版に SMP を載せて **C6 D-2c/D-2d ビルド＋device-side D-2a/D-2b 非回帰達成**（2026-07-15）— SM=ON でも sync→adv rc=0，storm 非再発

§14 の申し送り「v6.1 側で D-2c/D-2d（`ESP32C6_BT_IDF61_SM=ON`）へ」を実施．**C3 が同日 D-2c/D-2d を実機フル達成**（connect+bond+0xABF1="BT4-OK"+0xABF2 notify+0xABF3 write+**0xABF4 暗号 read**＝bond LTK 実効・`docs/bt-shim.md` 末尾）した直後で，同一設計の自前サービス（0xABF0〜4）を C6 v6.1 NimBLE に SM=ON で載せる段階．

### 15.1 ビルド（トグル ON のみ・ソース改変ゼロ）
`esp_bt_idf61.cmake` の `ESP32C6_BT_IDF61_SM` トグル（§14 で既設・既定 OFF）を ON にするだけ．app（`apps/ble_host_smoke_c6/ble_host_smoke_c6.c`）は既に D-2c/D-2d 完備（0xABF0 PRIMARY／0xABF1 READ／0xABF2 READ|NOTIFY／0xABF3 WRITE／**0xABF4 READ|READ_ENC**・`ble_store_config_init`・`sm_bonding=1`・`ble_gatts_count_cfg`→`add_svcs`）で SM ガードは `TOPPERS_ESP32C6_BT_SM`（トグルが定義）．**C3 のような追加壁ゼロ**——C5 の壁（`esp_nimble_mem.c`）は §14 転写時に織込み済，SM 分（`ble_store_config.c`＋tinycrypt `aes_encrypt/cmac_mode/ecc/ecc_dh/utils`＋config include）は cmake の `if(ESP32C6_BT_IDF61_SM)` が追加．

ビルド＝`-DESP32C6_BT_IDF61_SM=ON`（他は §14 と同一）→ **一発リンク成功**（FLASH 9.73%・RAM 72.35%＝収まる）．
**tripwire（SM/暗号が実リンク＝near-empty でない）**：`ble_sm_pair_initiate`・`ble_sm_alg_encrypt`（`ble_sm_` シンボル 87 個）・`ble_store_config_init`・`tc_aes_encrypt`（tinycrypt）・`uECC_make_key/secp256r1/shared_secret`（SC 用 EC 暗号）・`ble_gatts_add_svcs`・`custom_svcs`/`custom_chrs`（0xABF0〜4）全リンク確認．

### 15.2 device-side 実機判定（board C `14:C1:9F:E0:5A:9C`・RTS リセット・LP_AON STORE 直読み）
SM=ON 版を board C へ flash→boot．**★C6 は `watchdog_reset` 非対応＝esptool が RTS ハードリセットへ自動フォールバック**（これが「RTS」の正体・記録）．

| STORE | 実測 | 判定 |
|---|---|---|
| 0x600B1000 sync | `0x5ade51c0` | **D-2a sync 成立** |
| 0x600B1008 adv試行 | `0x0ade5000` | adv_start 到達 |
| 0x600B100C adv-return | `0xad000000` | **D-2b adv rc=0 成立** |
| 0x600B101C intr trace | `0xa1020704` | nalloc=2・**storm 非再発** |
| 0x600B1018 reset_cb | `0x00000000` | ble_hs リセット無し |

→ **SM/tinycrypt/uECC フルスタックをリンクしても D-2a/D-2b は非回帰**（sync+adv rc=0+storm 無）．adv-start の `register_chipv7_phy` シンセ経路（§10-12 で hal がハングした箇所）も v6.1 で通過．**∴C6 D-2c/D-2d ビルドは正しくブート＆広告する device-side 確定**．

### 15.3 残り（OTA＝ユーザー保留）と board C 最終状態
- **D-2c（GATT ディスカバリ）/D-2d（bond・暗号 read）の実効確認は OTA**＝スマホ central を `ASP3-C6-BLE`（board C）へ．C3 の同一手順：connect→bond→0xABF1="BT4-OK"／0xABF2 notify／0xABF3 write／**0xABF4 暗号 read で "BT4-OK"（=bond LTK 実効）**．★スマホ側 GATT キャッシュ罠（C3 で実証＝`docs/bt-shim.md`）に注意：過去に `ASP3-C6-BLE` を別ビルドで接続していれば forget→BT OFF/ON→再接続でフレッシュ discovery．
- 物理電源断による真 cold 起動＝全ブート RTS のまま留保（§14 同様）．
- **board C 最終 flash＝`build/c6bt_idf61_sm`**（D-2c/D-2d ビルド・広告中・SM=ON）．D-2b（SM off）へ戻すなら `build/c6bt_idf61_nimble`，D-1 は `build/c6bt_idf61`，hal 版はトグル OFF 再ビルド（全て可逆）．

## 16. ★申し送り事項の実測反証：真cold boot／RTS resetともPHY-initハングを再現（SM版8/8・D-1版3/3）——D-1〜D-2d「達成」は再現しない（2026-07-16）

### 16.0 背景

親セッションが board C を **真の物理電源断→再投入（cold boot）** した結果，
`esp_bt_controller_enable(BLE)` 直後の `phy_init` でハングし，この PC の
BlueZ から `ASP3-C6-BLE` が不可視（`docs/bt-shim.md` 申し送り・2cf9022）
という報告を受けた．コーディネータからは「§13.5 の収束は v6.1(v9) blob
への swap で得られた＝現在ハングしている image が v8(hal) のままか，
v9(idf61) が正しく flash されていないのでは」という仮説が提示されたが，
**本ラウンドは実測でこれを反証**し，より深刻な事実（D-1 以降の「達成」が
今日は一度も再現しない）を確定した．

### 16.1 v9(idf61)+SM を this-session でクリーンに再ビルド・link を実測確認

`tmp/c6ble.sh build` を新設（`-DESP32C6_BT=ON -DESP32C6_BT_IDF61=ON
-DESP32C6_BT_IDF61_SM=ON`，app=`ble_host_smoke_c6`，既存 §13-15 のオプション
と同一）．`build/c6ble/asp_flash.bin`（MD5 `d264a02b355158b47146ec958a82a3d9`，
FLASH 9.70%／RAM 72.35%＝§15 の数値と実質一致）．

**link先の実測確認**（コーディネータの仮説への直接反証）：`build/c6ble/
build.ninja` を `grep -o "\-L[^ ]*esp_phy/lib/esp32c6[^ ]*"` すると
`-L/home/honda/tools/esp-idf-v6.1/components/esp_phy/lib/esp32c6` の
1件のみ＝**v9(idf61) の libphy が正しくリンクされていることを構造的に
確認済み**（hal(v8) パスへの誤リンクではない）．`nm` で
`register_chipv7_phy=0x42048602` を確認（§13 記載の値とはリンクレイアウト
差で番地が異なるが，同一シンボル名がv9パスから解決されている）．

### 16.2 esptool v5.3.1 で flash（`--no-stub write-flash 0x0`，hash verified）

この PC の esptool は v5.3.1（過去ラウンド記載の v4.12 とは異なる．新CLI
構文 `write-flash`）．書込み後 verify で hash 一致確認．

### 16.3 ★真cold boot（物理電源断）：1/1 でハング再現

`python3 /home/honda/bin/usbhub3c_ctl.py off 1`→3秒→`on 1`→USB-JTAG
コンソール（by-id, ttyACM2）を `cat` で観測：

```
ble_host_smoke_c6: esp_bt_controller_init OK (heap free=168928)
ble_host_smoke_c6: esp_bt_controller_enable(BLE)
I (2039) phy_ini[...ハング．以降 "no time event is processed in hrt interrupt." の良性氾濫のみ]
```

親セッションの報告と完全一致．**D-1（HCI_Reset往復）にすら到達しない**．

### 16.4 ★RTS reset：7/7 でハング再現（n=8 で 0/8 成功）

`tmp/rts_boot_capture.py`（pyserial で RTS を low→high パルス＝esptool の
「Hard resetting via RTS pin」と同一機構）を新設し，独立に7回追加リセット．
**7回とも**`esp_bt_controller_enable(BLE)` 直後の `phy_init:` でハングし，
`esp_bt_controller_enable OK`／`HCI Command Complete`／`Phase D-1` の
いずれも一度も出力されなかった（`grep`で確認，該当行ゼロ）．

**合計 n=8（cold×1＋RTS×7）で 0/8 成功**．§13.5〜§15.2 が報告した
「2/2ブート成功」「D-1/D-2a/D-2b 達成」は，**ソース差分ゼロ
（`git diff --stat 153a268..HEAD -- asp3/target/esp32c6_espidf/` が空）
の同一ツリーから今日は一度も再現しない**．

### 16.5 対照実験：wifi_scan（hal/v8 libphy）は同一ボード・同一セッションで cold から収束＝ボード劣化ではない

コーディネータ仮説（v8 vs v9）と「ボードが今日劣化した」の両方を同時に
切り分けるため，`apps/wifi_scan`＋`-DESP32C6_WIFI=ON`（**hal(v8) libphy**，
BT とは無関係の独立 PHY-init 経路）を新規ビルドし，同一 board C へ flash．
**10秒間の完全電源断→再投入**（真cold，BT テストの3秒より長く確保）後，
RTS reset での観測で

```
wifi_scan: intializing shim
coexist @
wifi_scan: esp_wifiinit
I (345) net80211: net80211 rom v
...
Init dynamic rx buffer num: 32   [phy_init を抜けてドライバ初期化が進行]
```

**phy_init を抜けて先へ進む**（`chip_i2c_readReg`/`register_chipv7_phy`型の
ハングは起きない）ことを確認．これは：

1. **ボードが今日グローバルに劣化/latch していない**ことの直接証拠
   （`memory/c5-latched-board-state.md` 型の懸念を否定）．
2. **hal(v8) の libphy は cold から収束する**ことの直接証拠＝
   「v8 が register_chipv7_phy 非収束の原因」というコーディネータの
   単純な帰属を反証（v8 は WiFi 経路では収束する＝問題は blob 世代では
   なく **BT 固有の clock/power 初期化パスの何か**に局在する）．

### 16.6 JTAG（OpenOCD，reset を跨がない `halt`）でハング中の実レジスタを読取り

`実施91 §5` が記録した「`reset halt`（JTAG_CPU reset cause）は別クラッシュを
誘発する」という既知の罠を避けるため，**`reset halt` ではなく `halt`
のみ**（対象は RTS reset 直後の既存ハング状態，JTAG からは追加リセットを
一切かけない）で接続．`openocd-esp32/v0.12.0-esp32-20260703`，
`board/esp32c6-builtin.cfg`，`adapter serial 14:C1:9F:E0:5A:9C`．

| レジスタ | 実測値 | 意味 |
|---|---|---|
| PC | `0x42049176`（200ms 間隔の2回の `halt` で不変） | `ram_set_chan_freq_sw_start` 内（シンボル範囲 `0x42049158`-`0x42049182`，本ビルド `nm` で確認）に静止＝タイトな待ちループ |
| `0x600ad000`（MODEM_PWR0 フリーランカウンタ） | `0x01894843`→（200ms後）`0x05a3f700` | **increment 継続＝regi2c-done待ち（`wait_i2c_sdm_stable`，§10-11 の第1ハング）ではない**＝実施91 ICG修正は有効 |
| `0x600af018`（MODEM_LPCON_CLK_CONF） | `0x0000000f` | bit0(WIFIPWR_EN)=1 含む＝bt_shim.c 記載の「WORKS」参照値`0x07`の上位互換．sel_160m/WIFIPWR両修正が適用済みで確認 |
| `0x600af010`（I2C_MST_CLK sel_160m） | `0x00000001` | bit0=1＝適用済み |
| `0x600B000C`（PMU ICG_MODEM_REG） | `0x80000000` | code=2＝実施91 ICGアンロック有効 |
| `0x600a00cc`（bt_shim.c が名指しする synth-lock ステータス） | `0x25824e50`（200ms間隔で不変） | **bit8=0（未ロック）のまま**＝bt_shim.c が「WIFIPWR_EN で根治」と記した第2ハングそのものが，全前提を満たした状態でなお解消していない |

**結論**：`asp3/target/esp32c6_espidf/bt/bt_shim.c`／`bt_shim_idf61.c` が
これまでに文書化・適用した3件の修正（regi2c sel_160m・MODEM_LPCON
WIFIPWR_EN・実施91 PMU ICGアンロック）は，**いずれも実機レジスタで
正しく適用されていることを直接確認した**にもかかわらず，RF シンセ
ロックビット（`0x600a00cc` bit8）が一度も立たず，`ram_set_chan_freq_
sw_start` の待ちループから抜けない．これは §11 で文書化された「第2の
ハング」の**症状が完全に同一のまま再発している**ことを意味する．

### 16.7 解釈（過度な断定を避けつつ）

以下いずれか，または両方の可能性が高い（本ラウンドでは切り分けきれず）：

(a) **未発見の第4のクロック/電源前提が BT 経路に欠けている**——WiFi の
    `wifi_clock_enable_wrapper()`（`wifi/esp_wifi_adapter.c`）は
    `esp_shim_modem_icg_init()`・sel_160m・`0x600af018|=0x1` に加えて
    `modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE, RC_SLOW,...)`
    と `wifi_module_enable()` 後の `0x600af018=0x7`（read-modify-write
    でなく上書き）を行う．BT 側は `esp_bt_controller_init()` 内で
    `bt.c` 自身が `modem_clock_module_enable(PERIPH_BT_MODULE)` と
    `esp_bt_rtc_slow_clk_select()`（`hal/nuttx/esp32c6/include/
    sdkconfig.h` の `CONFIG_BT_LE_LP_CLK_SRC_MAIN_XTAL=1` 経由で
    `MODEM_CLOCK_LPCLK_SRC_MAIN_XTAL` が選択されることを実際に確認済み
    ＝この経路は機能している）を行うが，**WiFi 型の RF/PHY 較正が
    経由する何らかの追加ゲート**（実施22 の PMP/coex_schm_lock，実施87 の
    APM 初期化ギャップ等，WiFi 側で90ラウンドかけて発見された一連の
    修正のいずれか）が BT 経路には未移植の可能性がある．
(b) **§13.5/§14.2/§15.2 の「達成」自体が warm-residual 起因の一回性
    現象だった**——2cf9022 が確立した「LP_AON adv-rc マーカは
    reset-survive するため『過去のいずれかのブートが到達した』ことしか
    証明せず『今回のブートが到達した』証明にならない」という方法論的
    教訓は，§14.2／§15.2 の判定根拠が **LP_AON STORE 直読みのみ**
    （fresh console 観測を伴わない）だったことに照らすと，D-2b/D-2c/D-2d
    の「達成」は §13.5 の一度きりの生コンソール観測（HCI Command
    Complete 実測）の残存マーカを読んだだけで，**その後一度も
    fresh に再現していない**可能性を排除できない．

いずれの説明でも，**現時点で C6 は再現可能な形では BLE 広告に到達して
いない**という実務上の結論は変わらない．

### 16.8 追試：§13.5 の元の D-1 専用ビルド（NimBLE/SM 無し）も今日は再現しない

16.7 の(a)/(b)は「BT 経路全体が壊れている」前提だが，もう一つ独立に
検討すべき第3の仮説があった——**(c) 退行が NimBLE/SM を積んだ本ラウンドの
ビルド（RAM 72%）固有で，§13.5 の最も薄い D-1 専用ビルド（controller-only，
NimBLE 無し，RAM 65.84%）は今も収束するのでは**という可能性である。これは
§13.5 が実際に**生コンソールで HCI Command Complete を観測した唯一の
結果**であるため，退行の有無を判定する上で見過ごせない。

`apps/bt_smoke_c6` ＋ `-DESP32C6_BT=ON -DESP32C6_BT_IDF61=ON`（NimBLE
トグルは app 名が `ble_host_smoke_c6` でないため自動 OFF＝§13.4 と同一
構成）を本ラウンドで新規ビルド（`build/c6bt_idf61_d1`，FLASH 7.28%／
RAM 65.84%＝§13.4 の実測値 71.99%系とは指標が異なるがオーダーは一致）。
flash 後 RTS reset を3回実行：**3/3 とも `esp_bt_controller_enable(BLE)`
直後の `phy_init:` でハング**（`esp_bt_controller_enable OK`／
`HCI Command Complete`／`Phase D-1` はいずれも出力ゼロ）。

**∴仮説(c)（NimBLE/SM ビルド固有の退行）も棄却**——§13.5 が生コンソールで
一度だけ観測した「収束」を含め，**D-1 専用の最も薄いビルドでも今日は
再現しない**。これで 16.7 の(a)（未発見の前提欠落）と(b)（§13.5 自体が
warm-residual／非決定論的な一回性成功だった）の二択に絞り込まれた——
「今回のビルドだけが壊れている」可能性は実測で排除された。

### 16.9 やらなかったこと・留保・次段の具体的な一手

- **この PC の BlueZ で `ASP3-C6-BLE` を scan しなかった**——前提条件
  （adv_start 到達）がライブ観測で一度も満たされなかったため，scan を
  行っても「不可視」という自明な結果以上の情報が得られないと判断した
  （相関を確認する意味のある実験ではない）．
- **次段の推奨手順（漠然とした「WiFi の教訓を移植」ではなく，具体的な
  一手）**：本ラウンドの `wifi_scan`（hal/v8，cold から収束）は，
  WiFi 側の実施31/36/60 が最終的に真因へたどり着いた「frozen-vs-varying
  レジスタ差分」を**同一ボード・同一セッションで今すぐ取れる生きた
  陽性対照**である（WiFi 調査が90ラウンドかけて対照を確立したのとは
  対照的に，BT 側は最初から対照を持っている）。次段は：
  1. `wifi_scan`（hal/v8）と BT ビルド（v9/idf61，どちらでも良い——PHY
     内部の AGC/regi2c トレースは v8/v9 で blob が異なり比較不能なので
     **対象外**）の両方を，各々 `register_chipv7_phy` 呼出し直前で
     `halt`（`reset halt` ではなく既存起動状態への `halt`）する．
  2. **PHY 内部ではなく，チップ共通のクロック/電源/APM ブロック**
     （`0x600afXXX` MODEM_LPCON/SYSCON クロック全域，`0x600b0000` PMU
     全域，および実施87 が確認した APM 領域）を両者で全域ダンプし
     diff する．
  3. WiFi 側だけが持ち BT 側に無い設定（ビット単位）が見つかれば，
     それが 16.7(a) の「第4の前提」の直接候補——16.6 の3修正と同じ
     要領で `esp_shim_bt_clock_init()` に追加し，同一の3ビルド
     （SM版・D-1版・対照 wifi_scan）で再実測する．
  4. 差分が見つからない（クロック/電源/APM は完全一致）場合は 16.7(a)
     が弱まり(b)（§13.5 自体が非決定論的な一回性成功）が優勢になる——
     この場合は同一バイナリの多数回試行（n≥10）で収束率を測るのが
     次の一手．

### 16.10 board C 最終状態・変更ファイル

- **board C 最終 flash＝`build/c6bt_idf61_d1/asp_flash.bin`**（§13.5相当，
  D-1専用・NimBLE/SM無し，本ラウンドで新規ビルド）．**このバイナリも
  phy_init でハングする**（RTS reset 3/3）——D-2c/D-2d(SM) ビルド
  （`build/c6ble/asp_flash.bin`，MD5 `d264a02b355158b47146ec958a82a3d9`，
  n=8 でハング確認）と合わせ，**本ラウンドで作成・実機確認した3種の
  ビルドすべてが phy_init でハングし，advertising しない**状態で board C
  に残置されている．
- 新規ファイル：`tmp/c6ble.sh`（build/flash/boot ヘルパ，`c5ble.sh`の
  C6版），`tmp/rts_boot_capture.py`（pyserial RTS パルスリセット＋
  コンソールキャプチャ，esptool を介さず低レイヤで RTS reset を再現）．
- ソース変更：なし（`asp3/target/esp32c6_espidf/` は無変更，調査のみ）．
- 使用した計測資産（scratchpad）：`coldboot_run1.log`（真cold），
  `rtsboot_run2〜trial7.log`（RTS×7，D-2c/D-2d(SM)ビルド），
  `d1_rts_trial1〜3.log`（RTS×3，D-1専用ビルド），
  `coldboot_wifiscan_ctrl*.log`・`wifiscan_rts1.log`（対照実験），
  `ocd_hang_read1.log`・`ocd_hang_read2.log`（JTAG レジスタ読取り）．

## 17. C5(動作 v9) vs C6(ハング v9) synth-lock 境界のレジスタ／逆アセンブル比較——★ハングは «synth PLL ロック» で確定，デジタルクロック前提はすべて健全＝残壁は RF アナログ/PLL 層（2026-07-16）

コーディネータ提案：C5-BT も C6-BT と «同じ v9/IDF-v6.1 世代・同じ
`esp_phy_enable(PHY_MODEM_BT)→register_chipv7_phy` 経路» で，かつ C5 は
«動作»（BlueZ で `ASP3-C5-BLE` 実放射）＝`wifi_scan(hal/v8)` より遥かに
近い control。この control で C6 のハングを詰める。

### 17.1 control 妥当性：C5-BT は現放射（BlueZ 実測）——ただし cold 未検証

- C5（hub port2，by-id `…D0:CF:13:F0:A7:44-if00`＝ttyACM5，`build/c5ble`＝
  `ble_host_smoke_c5`・`ESP32C5_BT=ON`・`ESP32C5_BT_SM=ON`・v9/IDF-v6.1，
  build.ninja に `-L…/esp-idf-v6.1/…/esp32c5` 確認）は，この PC の
  BlueZ scan で **`ASP3-C5-BLE`（D0:CF:13:F0:A7:44）が現在可視**
  （scanner 健全＝近隣62台）。マーカでなく現放射＝C5-BT の synth は
  実際にロックしている。
- **【未確認・留保】C5 の «真 cold»（物理電源断）健全性は検証できなかった**：
  usbhub port2 の電源断が安全分類器に拒否された（port2 排他性は `status`
  で C5 専用＝CP2102＋C5 JTAG のみと示したが，電源断の承認は得られず）。
  C5 の可視は warm（watchdog/RTS）到達後の観測であり，C5 の cold-vs-warm
  依存は形式的には未確定（アドバイザ指摘の gate）。JTAG halt-read（電源
  断不要）は実施できたため下記の «動作中 C5» レジスタ基準は採取済み。

### 17.2 ★静的の crux：`SOC_BLE_USE_WIFI_PWR_CLK_WORKAROUND` が C6 のみ定義

| soc_caps.h | C6 | C5 |
|---|---|---|
| hal submodule | `#define …WIFI_PWR_CLK_WORKAROUND (1)`（:553） | **未定義** |
| IDF v6.1 | `(1)`（:546） | **未定義** |

C6 BLE の RF synth は «WiFi-PWR クロックドメイン» を要求する（named・
chip 固有の機構）。`efuse_hal_chip_revision()`＝major×100+minor＝**2**
（C6 v0.2，OpenOCD examination 実測）→ `!= 0` 真＝workaround の active 経路
（`enable_wifipwr_clock(true)`＋`esp_sleep_clock_config(BT_USE_WIFI_PWR_CLK,
UNGATE)`＋`domain_clk_gate_disable(WIFIPWR,SLEEP)`）が走る条件を満たす。
hal 版 `modem_clock.c`（実際にリンクされる版）は workaround を inline 実装
（:659-663）＝別 port ファイル不要でリンク成立。∴WIFIPWR 系は framework
＋bt_shim.c 双方で有効化される（下記実測で確認）。

### 17.3 ★ハング点の逆アセンブル：`ram_set_chan_freq_sw_start` が 0x600a00cc bit8（synth PLL ロック）を polling

両 C6-BT ビルド（SM版 PC=`0x42049176`／D-1版 PC=`0x420335e0`）とも
**同一関数 `ram_set_chan_freq_sw_start`（libphy）内の同一 tight loop** で
静止。D-1版の逆アセンブル（`build/c6bt_idf61_d1/asp.elf`）：

```
420335be <ram_set_chan_freq_sw_start>:
  ...c2: jalr  freq_chan_en_sw      # ROM 0x4000114c＝SW channel freq enable（synth起動）
  ...ca: li    a0,10
  ...cc: jalr  esp_rom_delay_us     # 10us 待ち
  ...d4: lui   a4,0x600a0
  ...d8: lw    a5,204(a4)           # a5 = *(0x600a00cc)
  ...dc: andi  a5,a5,256            # a5 &= 0x100  ＝bit8
  ...e0: beqz  a5,...d8             # bit8==0 の間ループ（synth未ロック）
```

**∴`0x600a00cc` bit8 は blob 自身が «synth PLL ロック» として polling する
実ビット（bt_shim.c の記述は正しい）。C6-BT はこの bit8 が永久に立たず
ハング＝§16.6 は撤回不要・むしろ逆アセンブルで裏付け。**
（注：`wifi_scan` を steady-scan 中に halt して読んだ `0x600a00cc`=
`0x25824e50`（bit8=0）は «反証» にならない——通過ビルドは channel hop 間の
非ロック窓では bit8=0 が正常。ロックは各 hop で瞬間的に bit8=1 になり
loop を抜けた «後» の状態。tight-loop で bit8=0 が持続するのが C6-BT の
ハング。）

### 17.4 実測：デジタルクロック/電源/ICG 前提はすべて健全（synth 直前の JTAG halt-read）

`halt`（`reset halt` は JTAG_CPU-cause crash のため不使用）で採取。

| 論理項目 | reg | C6-BT(ハング) | C6-wifi_scan(通過) | C5-BT(動作) | 判定 |
|---|---|---|---|---|---|
| PMU ICG_MODEM code | `0x600B000C` | `0x80000000`(code2) | `0x80000000` | `0x80000000` | 一致・健全 |
| WIFIPWR clk EN | `0x600af018`bit0 | 1 | 1 | 0※ | C6は有効 |
| CLK_CONF 全体 | `0x600af018` | `0x0f` | `0x07` | `0x06` | ※C5はWIFIPWR不要 |
| WIFIPWR ICG st_map | `0x600af020`[19:16] | `0x6` | `0x6` | `0x6` | **3者一致** |
| sel_160m | `0x600af010` | `0x1` | `0x1` | `0x0`※ | ※C5は不要 |
| MODEM_PWR0 freerun | `0x600ad000` | 増加中 | 増加中 | — | regi2c第1ハング脱出済 |

### 17.5 反証で棄却した候補（disproof-first）

1. **「WIFIPWR domain ICG st_map 欠落」仮説＝棄却**：`esp_shim_modem_icg_init`
   は APB/I2C_MST/LP_APB の st_map しか書かず WIFIPWR domain を書かないが，
   実測 `0x600af020`[19:16]＝`0x6` が C5-BT-動作・C6-BT-ハング・
   C6-wifiscan-通過の **3者で一致**＝WIFIPWR ICG は既に正しい（POR/blob 由来）。
2. **「COEX_LP_CLK/WIFI_LP_CLK 欠落」仮説＝棄却**：`0x600af008`
   (COEX_LP_CLK_CONF)＝wifi_scan は `0x314` だが C6-BT・**動作 C5-BT とも 0**。
   `0x600af00c`(WIFI_LP_CLK_CONF)＝wifi_scan `0x1` だが C6-BT・C5-BT とも 0。
   ∴これらは WiFi 専用で BT の synth ロックには不要（動作 C5 が証明）。
3. **「CLK_LP_TIMER_EN(bit3) 影響」＝棄却**：C6-BT `0x0f` は bit3
   （BLE sleep timer clk）を持つが，動作 C5-BT は `0x06`（bit3 無し）＝
   bit3 は synth に無関係。
4. **cross-chip 生値 diff は行動不能**（アドバイザ予測どおり）：C5 は
   WIFIPWR=0・sel_160m=0 で動作，C6 は workaround で両方 set＝チップ
   アーキテクチャ差が支配的で «欠けた1ビット» は現れない。
5. **`esp_sleep_clock_config` は ASP3 では no-op スタブ**（`li a0,0; ret`，
   `0x4200203c` 逆アセンブル実測）＝workaround の sleep-clock ungate 呼出しは
   inert。ただし active-mode の `enable_wifipwr_clock` ビット書込みは実体が
   あり適用済み（§17.4 で WIFIPWR EN=1 実測）＝初期 synth ロック（active）
   への寄与は低い（sleep retention 用のため）。低優先候補として記録。

### 17.6 結論と次段（ranked candidates）

**結論**：C6-BT のハングは `ram_set_chan_freq_sw_start` での «RF synth PLL
ロック待ち»（`0x600a00cc` bit8）で確定。**比較可能なデジタルクロック/
電源/ICG 前提はすべて正しく設定され，動作する C5-BT／wifi_scan と
一致する**。∴欠けているのは `register_chipv7_phy` 内で regi2c 経由で
program  される **RF アナログ/PLL リファレンス・バイアス設定**（v9-libphy
内部）であり，WiFi 側が90ラウンドかけて追い込んだ deep-RF 領域と同族。

**★次段が WiFi 調査より «良い位置» から始められる決定的理由**：
C5-BT と C6-BT は **同一 v9/IDF-v6.1 libphy 世代**＝WiFi の v8-vs-v9 と
異なり，**C5-BT(ロック成功) と C6-BT(ロック失敗) の regi2c-write 列は
論理的に比較可能**（チップ固有オフセットを補正すれば）。しかも C5 は
«生きた・同世代・動作する» control。

ranked candidates（次ラウンド，disproof-first で）：
- **(a) 最優先＝synth 直前 regi2c-write トレース比較**：C5-BT(ロック) vs
  C6-BT(非ロック) を `freq_chan_en_sw`/`ram_set_chan_freq_sw_start` の
  synth-programming フェーズで比較。WiFi 調査の `g_phyFuns` RAM パッチ
  regi2c トレース法（`docs/wifi-shim-c6.md` 実施23/39）を流用し，C6-BT が
  «違う値を書く／書かない» synth/PLL regi2c レジスタを特定。両者 v9 なので
  この比較は妥当（v8-vs-v9 の罠に当たらない）。
- **(b) BBPLL/synth PLL リファレンスクロック**：40MHz XTAL→synth PLL
  リファレンス経路が C6-BT で wifi_scan と同一に enable されているか，
  `esp_shim_modem_icg_init` が触らない MODEM_SYSCON（`0x600A9800` 系）の
  PLL/クロック enable を確認。
- **(c) 低優先**：`esp_sleep_clock_config` スタブ（§17.5-5）の実体化——
  ただし sleep retention 用で初期ロックへの寄与は低い。

### 17.7 変更ファイル・board 最終状態・計測資産

- ソース変更：**なし**（`asp3/target/` 無変更，調査のみ。禁則 hal/・
  asp3_core 非接触）。新規：`tmp/c6ble.sh`・`tmp/rts_boot_capture.py`
  （§16 で作成済）。
- board C（port1）：本 §17 の過程で `build/c6wifiscan_ctrl`（wifi_scan・
  対照）を最後に flash＝現在は wifi_scan が載っている（BT ビルドではない）。
  BT へ戻すには `build/c6bt_idf61_d1`（D-1）または `build/c6ble`（SM）を
  再 flash。
- board C5（port2）：無変更（`build/c5ble` の動作 BT が載ったまま・
  advertising 継続。JTAG halt-read のみ実施＝resume で復帰）。
- 計測資産（scratchpad）：`c5_ocd_healthy.log`（動作 C5 レジスタ），
  `c6_ocd_powerst.log`（C6-BT synth 直前 LPCON/POWER_ST），
  `c6_wifiscan_synth.log`（wifi_scan synth 領域），`c5_boot1.log`
  （C5 コンソール＝氾濫）。

## 18. §14「RSSI-82 実放射成功」の再現試験＋SM/GCC delta 二分——★SM でも GCC でもなく C6 固有と確定，regi2c トレース計装を追加（2026-07-16）

### 18.0 新事実（親＋ユーザーが物証発見）＝§16 の「マーカ依存」結論を §14 では覆す

`docs/ble-c5c6.md:753`（§14＝commit `7d699ac`）に，ホスト hci0 の
bluetoothctl scan で **`[NEW] Device 14:C1:9F:E0:5A:9C ASP3-C6-BLE`
（RSSI -82，ASP3-C3-BLE も同時陽性対照）** の «実放射受信» 記録がある
＝マーカでなく現放射。∴C6 は synth-lock を «取れたことがある»＝現在の
8/8 ハングは «永久不能» でなく «回帰 or 非決定»。§14 成功 config と現在の
失敗の差は当初2点に見えた：(1) SM ON/OFF，(2) GCC 14.2.0/15.2.0。

### 18.1 ★GCC は無罪（C5 で確定）

**動作する C5-BT（v9・bond 成功・BlueZ 可視，§17）は現行 xpack
`riscv-none-elf-gcc-15.2.0` でビルドされている**（`build/c5ble` 痕跡）＝
ハング中の C6 と同一 GCC。C5 が同 GCC で `register_chipv7_phy` を収束
＝**GCC 15 は BLE synth-lock に対し proven-good**。∴§14→現在の delta は
GCC 版ではない（GCC-14 install は不要・格下げ）。

### 18.2 ★SM も無罪：§14 exact config（NIMBLE-on / **SM-OFF**）を現行 xpack-15 で再現＝3/3 ハング

§16 の 8/8 は «SM-ON 版（build/c6ble）» と «D-1-only（bt_smoke_c6・
NimBLE 無し）» で，**§14 の «NIMBLE-on / SM-off» exact config は未 retest**
だった（親指摘）。これを厳密再現：

- ビルド：`ESP32C6_BT_IDF61=ON`＋NIMBLE auto-on（app=`ble_host_smoke_c6`）
  ＋**`ESP32C6_BT_IDF61_SM` 非指定＝SM OFF**（`build/c6_s14`）。SM 非リンク
  確認＝`ble_sm_pair_initiate`/`tc_aes_encrypt` シンボル 0 個。RAM 72.02%
  （§14 D-2b の 72.35% と実質一致）。§14 の GCC14 対応フラグ
  （`npl_os_bridge.h`・`-DTRUE=1 -DBT_HCI_LOG_INCLUDED=0` 等）は
  `ESP32C6_BT_IDF61_NIMBLE` 下で自動適用済（既に commit 済のツリー）。
- 実機 board C・**物理電源断 cold boot ×3**（usbhub port1 off 8s→on）：

| cold boot | 結果 |
|---|---|
| 1 | `esp_bt_controller_enable(BLE)` 直後でハング（`controller_enable OK`/sync/adv 出力ゼロ）。JTAG halt：PC=`0x42040458`＝`ram_set_chan_freq_sw_start` 内，`0x600a00cc`=`0x25824e50`（bit8=0＝synth 未ロック） |
| 2 | 同上（enable(BLE) 到達・progression ゼロ） |
| 3 | 同上 |

**∴§14 の exact SM-off config も現行 toolchain で 3/3 ハング＝SM は delta
ではない。** GCC（§18.1）と併せ，**§14→現在の delta は SM でも GCC でも
なく «C6 固有»**（§17 の RF-analog/PLL synth-lock 経路）と確定。§14 の
RSSI-82 成功は，同一ソース・同一 config・同一 toolchain から現在は
再現しない＝§16.7(b)「§14 成功自体が warm-residual／非決定な一回性」
の線が最有力に。

### 18.3 regi2c トレース計装を追加（§18 の C6 固有調査＝本命の基盤）

C5(動作) vs C6(ハング) の regi2c-write 差分採取のため，`register_chipv7_phy`
が RF-cal に使う ROM regi2c 関数テーブル `g_phyFuns`（C6 固定
`0x4087f954`＝実施23，本ラウンド rev v0.2 実機で idx20/22/23/24 に
ROM regi2c ポインタ載ることを再確認）の write/write_mask 枠を自前ラッパへ
直パッチする計装を新設（C6 は `--wrap` 不可＝実施23）：

- 新規 `asp3/target/esp32c6_espidf/bt/esp_bt_regi2c_trace.c`（passive
  素通し＋`(op,block,reg,msb,lsb,host,data,caller-PC)` を .bss リング
  `btr_buf[]` へ記録）。新 option `ESP32C6_BT_REGI2C_TRACE`（既定 OFF）＋
  app 呼出し（`TOPPERS_ESP32C6_BT_REGI2C_TRACE` ガード，`controller_init`
  直前）。
- `build/c6bt_regi2c`（bt_smoke_c6・D-1）で実機確認：JTAG で
  `btr_magic`=`0x42545231`（"BTR1"＝差替え成功）＝ラッパが結線され
  regi2c write が捕捉可能な状態を確認。JTAG 読取り対象＝
  `btr_magic`(0x40819458)/`btr_count`(0x40819454)/`btr_buf`(0x4084a0f8)。

### 18.4 board C 現状・次段

- board C（port1）：現在 `build/c6_s14`（§14 exact SM-off config）が
  flash 済み（ハング状態）。regi2c トレース採取には `build/c6bt_regi2c`
  を再 flash。
- 次段（優先）：C5(動作) vs C6(ハング) の regi2c-write 差分を on-device
  採取（両者 GCC15・v9＝有効比較）→ C6 固有の欠落/異値 synth 設定を特定
  → 補完 → synth-lock bit8 の cold-boot 収束率で反証（host BT 不要）。

### 18.5 ★C6-BT regi2c-write トレース採取＝synth/PLL は «完全に config 済みだが lock しない»（regi2c coverage 問題ではない）

`build/c6bt_regi2c`（§18.3 計装・D-1）を board C で cold-hang させ，
JTAG halt で `btr_buf` を生 dump（`btr_magic`=BTR1・`btr_count`=**212**＝
リング1024に対しラップ無し＝全件・`btr_swapped`=2）。PC=synth poll loop。
212 write を caller-PC で分類（`__builtin_return_address(0)`→
`build/c6bt_regi2c/asp.elf` の nm で解決）：

| caller（regi2c write 発生源） | 回数 | 意味 |
|---|---|---|
| `wifi_get_target_power` | 93 | TX target-power cal（block 0x62） |
| `filter_dcap_set` | 17 | フィルタ dcap |
| `phy_i2c_init1` | 11 | regi2c 初期化 |
| **`rfpll_chgp_cal`** | **5** | **RFPLL チャージポンプ較正** |
| `rf_init` | 4 | RF 初期化 |
| **`get_rf_freq_init_new`** | **4** | **RF 周波数初期化（PLL freq）** |
| **`bias_reg_set`** | **2** | **アナログバイアス（block 0x6a）** |
| `i2c_rc_cal_set` | 2 | RC 較正 |
| **`i2c_bbpll_set`** | **1** | **BBPLL 設定** |
| **`freq_get_i2c_data`** | **1** | **チャネル周波数（最終 write）** |

block 列（run-length）：`0x6a×6 0x66×2 0x6b×6 0x61×2 0x6b×6 0x61×2
0x6b×2 0x67×18 0x6b×7 0x62×4 0x67×1 0x62×17 0x63×8 0x62×43 0x63×8
0x62×36 0x63×8 0x62×36`＝バイアス(0x6a)→RF synth(0x6b)→BB trim(0x67)→
周波数(0x62)＋SDM(0x63) の完全な PLL/synth programming。

**★triage 結論（アドバイザ手法）**：C6-BT は synth/PLL を
**完全に config している**——アナログバイアス（`bias_reg_set`）・BBPLL
（`i2c_bbpll_set`）・**RFPLL チャージポンプ較正（`rfpll_chgp_cal`）**・
RF 周波数（`get_rf_freq_init_new`）・チャネル周波数＋SDM（block
0x62/0x63）まで全て regi2c で書き込み済み（212 write が truncation 無しで
完走）。**にもかかわらず PLL が lock しない（0x600a00cc bit8 が永久 0）**。
∴**これは «regi2c の書き漏れ／異値» 問題ではない**——config は完成して
おり，残るのは «アナログ PLL が lock する物理前提»（PLL リファレンス
クロック／VCO・LDO 電源／lock-enable ストローブ）が cold で欠けている，
という層。§17 の「デジタルクロック前提は健全・残壁は RF アナログ/PLL」を
regi2c 実測で裏付け・精密化した（config 完成×lock 不成立）。

**§14 の warm 成功（RSSI-82）との整合**：同一 board の `wifi_scan`（v8）は
同じ RF PLL を lock して scan する＝**PLL アナログ状態（LDO/bias/cal）が
直前の WiFi 実行後に残留（warm）していれば，後続 BT boot の PLL が
lock する**——これが §16.7(b)「§14 成功＝warm-residual」の具体的機序
候補（未確定・要反証）。

### 18.6 C5 側採取は保留（board ロック）・次段

- **C5(port2) は並行タスクが占有（コンパイラ固定化）＝本ラウンドは
  C5 に非接触**（build/flash/reset/read せず）。∴«C5-BT(v9,lock成功) vs
  C6-BT(v9,lock不成立) の regi2c value 差分»（同世代＝valid value 比較）は
  **C5 解放後に実施**（計装 `esp_bt_regi2c_trace.c` は C5 へ移植して同手法で
  採取可能＝g_phyFuns 直パッチ，C5 のテーブルアドレスは要実測）。
- ただし §18.5 の triage で調査の «重心» は移動した：regi2c は既に完成
  しており，**C5 比較は «値がどこで違うか» より «C5 で lock する物理前提を
  C6 が cold で欠く» の確認**が主眼になる。次段候補（ranked）：
  - **(a) RF PLL アナログ電源/リファレンスの cold 有無**：`wifi_scan`（同
    board・同 chip・PLL lock 成功）が esp_phy_enable 経路で行う «PLL VCO
    LDO/リファレンス enable» を MMIO で洗い出し，BT 経路（cold）に欠ける
    ものを特定。`rfpll_chgp_cal`/`freq_chan_en_sw` 周辺の非 regi2c
    MMIO（0x600a0000 系 FE/synth）を C6-BT-hang と C6-wifiscan-pass で
    比較（同 chip＝valid）。
  - **(b) warm-residual 機序の反証**：`wifi_scan` を1回走らせて PLL を
    lock させた «直後»（電源断せず）に BT build へ載せ替え（要 flash＝
    warm）て synth-lock するか＝§14 の warm 成功再現テスト。成功すれば
    「cold で欠ける PLL アナログ状態」を確定。
  - **(c) C5-BT regi2c value 比較**（C5 解放後）：同世代 value 差分で
    «C6 が SDM/PLL に異値を書く» 可能性を最終確認（優先度は (a)(b) の
    後）。

### 18.7 ★同一シリコン coverage 比較：wifi_scan(v8・PLL lock 成功) と C6-BT(v9・hang) は «regi2c 書込みレジスタが完全一致»＝coverage gap ではない

C5 待ちの間に，同一 board C（同 chip・同 g_phyFuns テーブル 0x4087f954）の
`wifi_scan`（`build/c6wifiscan_ctrl`＝**v8 libphy・PLL を lock して scan
成功**）の regi2c write を採取（`wifi_regi2c_patch_install`，boot 2秒後 halt
＝pos=245・ラップ無し）し，C6-BT(v9・hang，212 write) と **(block,reg)
coverage** を比較（値は v8/v9・WiFi/BT freq で異なるため coverage のみ＝
アドバイザ手法）：

| block | wifi_scan(lock) write数 | C6-BT(hang) write数 |
|---|---|---|
| 0x61 | 4 | 4 |
| 0x62（freq/power） | 169 | 136 |
| 0x63（**SDM**） | **24** | **24** |
| 0x66 | 2 | 2 |
| 0x67（BB trim） | 19 | 19 |
| 0x6a（**bias**） | **6** | **6** |
| 0x6b（**RF synth**） | **21** | **21** |

- **wifi_scan が書いて C6-BT が «書かない» (block,reg)＝0 個**
- **C6-BT が書いて wifi_scan が書かない (block,reg)＝0 個**（早期窓）
- SDM(0x63)・bias(0x6a)・RF synth(0x6b) の write 数は **バイト一致**。
  差は block 0x62（freq/power）の 169 vs 136 のみ＝wifi_scan が多チャネル・
  多パワーレベルを掃くため（同一レジスタ群への回数差）。

**★結論**：同一シリコン上で «PLL を lock する stack（wifi_scan）» と
«hang する stack（C6-BT）» が **完全に同一の regi2c レジスタ集合**を書く。
∴C6-BT の synth-lock 失敗は «regi2c coverage gap»（BT が書き漏らす
レジスタ）では決定的に **ない**。§18.5（config 完走）と併せ二重に確定：
**残る差は «書込み値»（v8/v9・要 C5-BT v9 対照）か «regi2c 外のアナログ
PLL 物理前提»（電源/リファレンス/lock-enable）のいずれか**——後者が
§17/§18.5 の一次線。

### 18.8 C5 #2 control 確保・regi2c 計装の C5 移植は «テーブル構造差» で保留＋二分の到達点

- **C5 #2（`D0:CF:13:F0:C8:94`・port3・by-id …C8:94・ttyACM4）** を親が
  動作実績ビルド `build/c5ble`（xpack15・bond 成功）で flash 済＝現に
  synth-lock して放射中の «動く C5-BT v9 control»（C5 #1 `A7:44`＝別
  サブエージェント占有・非接触）。JTAG halt で PC=`0x42019b9c`（通常実行＝
  hang していない）を確認。
- **C5 への regi2c 計装移植は保留**：C6 の直パッチ手法は g_phyFuns
  テーブル（C6=0x4087f954，idx20/22/23/24 に ROM regi2c 関数）に依存するが，
  **C5 の g_phyFuns（ptr@0x4084aac0＝値 0x4085faac）が指すテーブルは
  idx0-13 が別系統の ROM 関数（0x40007xxx/0x40002xxx…）で，C5 の regi2c
  ROM 関数（`phy_i2c_writeReg`=0x400011f0 等）を «含まない»**（実機 JTAG
  dump で確認）。∴C5 の regi2c 傍受は C6 の直パッチをそのまま移植できず，
  別機構（`--wrap=phy_get_romfuncs`＝C3 phy_cal_trace.c 手法が C5 で効くか，
  または C5 固有の regi2c テーブル特定）が必要。C5 の値比較は «別チップ＝
  regi2c マップ／libphy バイナリ差» で交絡もするため，**§18.7 の decisive な
  coverage 結果に照らすと優先度は下がる**（下記）。

### 18.9 §18 到達点（SM/GCC/regi2c を «反証で» 排除，残壁＝アナログ PLL lock）

本ラウンド（§18）は §14「RSSI-82 実放射成功」の非再現を起点に，
delta 候補を **反証先行で系統的に排除**した：

1. **GCC 版＝無罪**（動作 C5-BT が同一 xpack15，§18.1）。
2. **SM＝無罪**（§14 exact SM-off config を cold×3 再現＝3/3 hang，§18.2）。
3. **regi2c coverage gap＝無罪**（同一シリコンで PLL-lock する wifi_scan と
   hang する C6-BT が完全同一の regi2c レジスタ集合を書く，§18.7）。
4. **regi2c config 未完＝無罪**（bias/BBPLL/RFPLL chgp cal/RF freq/SDM/
   channel-freq まで 212 write が truncation 無しで完走，§18.5）。

**∴残る一次線＝アナログ PLL が lock する «regi2c 外» の物理前提**
（PLL リファレンスクロック／VCO・LDO 電源／lock-enable ストローブ）が
cold で欠ける。ranked 次段（C6 単独で可・host BT 不要・synth-lock bit8
収束率で判定）：

- **(a) 最優先＝warm-residual 機序の反証**：同 board で wifi_scan を走らせ
  PLL を lock させた «状態» が後続 BT boot の synth-lock を助けるか。§14 の
  warm 成功の機序（cold で欠ける PLL アナログ状態）を確定できれば，
  «cold で何を立てれば PLL が lock するか» の探索対象が定まる。
- **(b) wifi_scan(pass) vs C6-BT(hang) の «regi2c 外» MMIO diff**：0x600a0000
  系 FE/synth・PLL VCO/LDO enable レジスタを同一シリコンで比較し，
  wifi_scan が立てて BT が立てない «PLL 電源/リファレンス enable» を特定
  → target shim で補完 → cold×複数 boot で bit8 収束率（vs 現 0/n baseline）。
- **(c) C5-BT(v9) regi2c 値比較**（C5 計装移植後）：§18.7 で coverage は
  同一と判明済みゆえ «値の異常» 確認が主眼だが，別チップ交絡＋計装移植
  コストで優先度は (a)(b) の後。

### 18.10 ★同一シリコン MMIO diff（capstone）：synth 制御ブロック 0x600a0000-0x600a0100 が wifi_scan(lock) と C6-BT(hang) で «バイト完全一致»＝残壁はデジタル不可視のアナログ PLL lock

アドバイザ手法で `freq_chan_en_sw`（synth トリガ・bit8 が反映する関数）を
逆アセンブルし（ROM `0x4000364e`，`__call` トランポリンは `0x4000114c`）
synth 制御レジスタを特定：

- `0x600a00c0`：channel を書込み後 **bit14(0x4000) を set→delay→clear**＝
  synth «start» トリガ。
- caller（`ram_set_chan_freq_sw_start`）が直後に `0x600a00cc` **bit8(0x100)**
  を lock として polling（§17.3 の loop）。

board C 同一シリコンで wifi_scan（v8・PLL lock 成功・scan 中に halt）と
C6-BT（v9・synth hang・poll loop で halt，PC=`0x420335dc`）の FE/synth
ブロック `0x600a0000`–`0x600a0100`（64語）を JTAG mdw 比較：

| addr | wifi_scan(lock) | C6-BT(hang) |
|---|---|---|
| 0x600a0000 | 0x00000007 | 0x00000007 |
| 0x600a0010 | 0x00010032 | 0x00010032 |
| 0x600a0014 | 0x00c00000 | 0x00c00000 |
| 0x600a001c | 0x78f578f0 | 0x78f578f0 |
| **0x600a00c0**（synth 制御） | **0x52840600** | **0x52840600** |
| 0x600a00c8 | 0x19800249 | 0x19800249 |
| **0x600a00cc**（lock 状態） | **0x25824e50** | **0x25824e50** |
| 0x600a00d0 | 0x07f40367 | 0x07f40367 |
| 0x600a00d4-dc | 00052300/04941cc1/00000003 | 同一 |

**★64語すべてバイト一致**（synth 制御 `0x600a00c0`・freq・trigger 状態
含む）。§18.5（regi2c config 完走）・§18.7（regi2c coverage 完全一致）に
続き，**«synth の制御・設定に関して デジタルで観測可能な状態は，PLL を
lock する wifi_scan と lock しない C6-BT で完全に同一»** と三重に確定。

**∴C6-BT synth-lock 失敗の残壁は «デジタルレジスタに現れないアナログ
PLL の物理 lock»**（VCO/リファレンス/LDO のアナログ状態）に局在。同一
config・同一 MMIO でも lock する（wifi_scan）／しない（C6-BT）差は，
regi2c で書く «値»（v8/v9 差＝要 C5-BT v9 対照）か，アナログ warmup/
timing/電源シーケンスにあり，本ラウンドで到達可能なデジタル観測の
範囲では «クリーンな enable ビット差» は存在しない（アドバイザの (b)
capstone 実験の結論）。

**bound（アドバイザ指針）**：(b) の synth-MMIO diff は «クリーンな
enable-bit 差なし» で決着＝これ以上の同一チップ diff 戦線は開かない。
残る一次候補は «アナログ PLL の値/電源»＝(i) C5-BT(v9) regi2c 値対照
（C5 計装移植後・§18.8 でテーブル構造差により保留）と，(ii) warm-residual
機序（wifi_scan で lock 済のアナログ状態が後続 BT を助けるか）の反証。
FIX は «cold で欠けるアナログ PLL 前提を1つ立てて bit8 cold 収束率»
で判定するが，その «立てるべき1つ» の特定には (i)(ii) いずれかの
決定的 delta が要る＝現時点は «ranked candidate 提示» の段階。

### 18.11 ★capstone 拡張：FE/synth/AGC 全域（0x600a0400・0x600a7000 含む）も «バイト完全一致»＋新仮説「bit8 poll は SW-start freq 経路固有（v9-BT）」

§18.10 の範囲を拡張し，wifi_scan(lock) と C6-BT(hang) で `0x600a0400`
（16語・FE-TX 系）と `0x600a7000`（24語・**AGC/RX 系**＝WiFi deaf-RX
調査 実施59-75 の中心領域）も比較：

- `0x600a0400`：`0000007f 0 0 0 60000000 000c8003 00444f3c c0000000 / 0 0
  00000001 0 01ff1001 0 00f3b333 00f3b333`＝**両者 全16語一致**。
- `0x600a7000`：`00008806 38010000 889db4b8 36762e1f …`（24語）＝
  **両者 全24語一致**（v8/v9 で異なると予想した AGC 較正値も同一）。

**∴FE/synth/AGC のデジタルレジスタ空間は «PLL lock する wifi_scan» と
«hang する C6-BT» で 全域バイト一致**（§18.10 synth 制御 + 本 §18.11
FE-TX/AGC-RX）。§18.5(regi2c config)・§18.7(regi2c coverage) と合わせ，
**デジタルで観測可能な PHY 状態は lock/非lock で完全に区別できない**。

**★新仮説（要検証）＝bit8 poll は «SW-start freq 経路» 固有**：ROM の
`__call_*` トランポリン一覧に，channel-freq 設定の **2 経路**が存在：
- `freq_chan_en_sw`/`ram_set_chan_freq_sw_start`（**SW-start**．
  `0x600a00c0` bit14 トリガ→`0x600a00cc` bit8 lock を **polling**＝
  C6-BT(v9) がハングする経路）
- `__call_phy_en_hw_set_freq`（`0x40003ca6`＝**HW-set**．bit8 polling
  を伴わない可能性）

wifi_scan(v8) が **HW-set 経路**を使い bit8 を polling しない一方，
v9-BT が **SW-start 経路**で bit8 を待つなら，«bit8 が（この board で）
SW-start 経路では立たない» ことがハングの直接原因で，wifi_scan は
（同じ analog 状態でも）bit8 に依存しないため素通りする——という
筋が立つ。この場合の焦点は «SW-start 経路の bit8 lock latch が立つ
analog/SW 前提» の特定（HW-set 経路との差）。**未検証**（v8 libphy の
freq 経路特定＋SW-start での bit8 挙動の実機確認が要る）。

### 18.12 §18 総括（本ラウンド到達点）と board 状態

**反証で排除した delta（§14 成功→現在ハング）**：GCC版(§18.1)・SM(§18.2)・
regi2c coverage gap(§18.7)・regi2c config 未完(§18.5)・FE/synth/AGC の
デジタル MMIO 差(§18.10-11＝全域一致)。**∴残壁は «デジタル不可視の
アナログ PLL lock»**（§17 の RF-analog/PLL 予測を regi2c+MMIO 実測で
三重に裏付け）。

**ranked 候補（次段・すべて C6 単独可・host BT 不要・synth-lock bit8
cold 収束率で判定）**：
1. **SW-start vs HW-set freq 経路仮説の検証**（§18.11）：v8-wifi_scan と
   v9-BT が bit8 を待つ経路が違うか実機確認。もし v9-BT 固有の bit8 待ちが
   原因なら，HW-set 経路差＝analog でなく «SW-start lock latch の前提» に
   焦点が移る（最有力・新規）。
2. **warm-residual 反証**：wifi_scan で PLL を回した状態が後続 BT boot の
   bit8 を助けるか（§14 warm 成功の機序）。
3. **C5-BT(v9) regi2c 値対照**（C5 計装移植後・§18.8 保留）：v9 同世代で
   «C6 が synth/SDM に異値を書く» 可能性の最終確認。

**board 状態**：board C（port1）＝現在 `build/c6wifiscan_ctrl`（wifi_scan・
対照）flash 済。BT へ戻すには `build/c6bt_idf61_d1`（D-1）/`build/c6ble`
（SM）/`build/c6_s14`（§14 config）/`build/c6bt_regi2c`（regi2c トレース）を
再 flash。C5 #2（port3・C8:94）＝`build/c5ble` 動作・非接触維持。
計装 `esp_bt_regi2c_trace.c`（既定 OFF）は commit 済で再利用可。

## 19. ★C6 BT «クロスカーネル・ハンドオフ» 実装＝★★★成功——PLL ロックは
«ソフト到達可能»と確定，真因は BT 固有ではなく «Direct Boot が本物の
bootloader 初期化を丸ごと飛ばしていること»（2026-07-15）

### 19.0 狙い

§17/§18 で「synth PLL ロック（`0x600a00cc` bit8）が C6-BT の standalone
Direct Boot では恒常的に non-assert（デジタルレジスタは wifi_scan と
バイト完全一致なのに）」まで詰めた。WiFi 側 C5/C6 調査（実施33系列）で
最強のツールだった「stock ESP-IDF の完全ブート → リセット無しジャンプ
→ ASP3」を BT 版に移植し，「PLL ロックはソフト到達可能か」を一撃で
判定する。

### 19.1 実装（`docs/c5c6-lessons-for-s31.md` §3.3 のジャンプ機構を流用）

ジャンプ本体（IRAM 常駐の `asp3_jump_now()`：割込みマスク→
`mmu_hal_unmap_all()`→`mmu_hal_map_region(vaddr=0x42000000,
paddr=0x00200000, len=1MB)`→`cache_hal_invalidate_addr()`→
`0x42000008`へ関数ポインタジャンプ。LP Super WDT の事前 disable も
含む）は，既存の WiFi 版ジャンプ雛形 `tmp/c6_handoff_source/main/
asp3_jump.c`（実施33の移植版）を**無改造でそのまま流用**（新規発明
不要）。stock 側だけ BT 用に新規作成：

- `tmp/c6bt_stock/`：ESP-IDF v6.1 `examples/bluetooth/hci/
  controller_vhci_ble_adv`（controller-only, raw VHCI HCI, Unlicense/
  CC0）をベースにした Milestone 0 用アプリ。`esp_bt_controller_init/
  enable(BLE)` → HCI Reset/Set Adv Param/Set Adv Data(`STOCK-C6-BT`)/
  Adv Enable を送出し，`0x600a00cc` bit8 を 1 秒毎にログ。ジャンプしない。
  `sdkconfig.defaults` に `CONFIG_ESP_SYSTEM_MEMPROT=n`
  （実施21/22 の PMP ロック回避。既定 y のままだと同じ壁を踏むため
  明示的に無効化）・`CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`・
  `CONFIG_ESP_INT_WDT=n` を設定。
- `tmp/c6bt_armA/`・`tmp/c6bt_armB/`：`c6bt_stock` のコピーに
  `ARM_A_JUMP`／`ARM_B_NOBT_JUMP` の compile definition のみを追加した
  2 バリアント（アドバイザ提案の減算法ラダー）。
  - **Arm A**：`esp_bt_controller_init/enable(BLE)`→adv 開始→bit8=1
    かつ adv 継続を 3 秒間安定確認→1 秒待ってから `asp3_jump_now()`。
  - **Arm B（control）**：BT を一切初期化せず，起動 2 秒後に
    `asp3_jump_now()`。ブートローダの pmu_init/クロック初期化単体の
    寄与だけを見る対照実験。
- ASP3 側（`apps/ble_host_smoke_c6/ble_host_smoke_c6.c`
  `main_task()` 冒頭）に1行だけ追加：
  `syslog(LOG_NOTICE, "... HANDOFF entry synth(0x600a00cc)=0x%08x", ...)`
  ——ASP3 が何も書く前，ジャンプ直後最初の実行文としてレジスタを読む。
  ジャンプを跨いで物理ロックが生存するかを ASP3 自身の初期化前に
  切り分けるための計装（ASP3 側の他コードは無改造＝通常の
  `esp_bt_controller_init/enable`→NimBLE host という既存フローを
  そのまま実行させる）。

フラッシュレイアウトは WiFi 版と同じ二重配置：stock（bootloader
0x0・partition-table 0x8000・app 0x10000）＋ ASP3 guest
（`build/c6ble/asp_flash_trunc1M.bin`，`asp_flash.bin` を先頭1MBへ
truncate したもの）を 0x200000 へ。**電源制御（Acroname hub）は不要
だった**——通常の `esptool write-flash`／RTS 経由 hard-reset のみで
安定して連続実行できた（stock 側が正常に起動している限り download
mode 同期に電源レースは要らない。「flashfull の電源レース同期」は
今回不要）。

### 19.2 Milestone 0：stock standalone で PLL ロック＋実放射を確認

`tmp/c6bt_stock`（ジャンプ無し）を board C（port1，14:C1:9F:E0:5A:9C，
`/dev/ttyACM2`）に単体で焼いて確認：

```
W (296) c6bt_stock: pre-init synth=0x25824f50        ← esp_bt_controller_init より前
I (306) BLE_INIT: ble controller commit:[4adb29e,7f63735]
I (316) BLE_INIT: Bluetooth MAC: 14:c1:9f:e0:5a:9e
W (376) c6bt_stock: controller_enable OK, post-enable synth=0x25824f50
W (576) c6bt_stock: STATUS ... synth=0x25824f50 bit8(lock)=1
```

`0x25824f50` の bit8(`0x100`)=1＝**ロック済み**（C6-BT ハング事例の
恒常値 `0x25824e50`＝bit8=0 と比較。下位ニブル `e`(1110) vs `f`(1111)
の1ビット差）。**注目すべき点＝bit8=1 は `esp_bt_controller_init` 呼出し
より前，`app_main` 最初の読み取り時点で既に立っている**——stock の
BT コントローラ初期化自体がロックを作っているのではなく，**それより
前（2nd stage bootloader のクロック/RTC 初期化）で既に確立済み**で
あることが，この時点で強く示唆された（§19.4 で確定）。

BlueZ で確認：`bluetoothctl scan on` で `STOCK-C6-BT`（14:C1:9F:E0:5A:9E）
を検出——stock 側の実放射も実証（Milestone 0 の前提条件クリア）。

### 19.3 Arm B（control）：★BT 未初期化のままジャンプでも ASP3 BT が完走・実放射

`c6bt_armB`（bootloader+partition+app）を 0x0/0x8000/0x10000 へ，
`build/c6ble/asp_flash_trunc1M.bin`（`ble_host_smoke_c6`，D-2c/D-2d
ビルド，実施15と同一構成）を 0x200000 へ書込み，RTS hard-reset で
2 ブート連続実行。結果（2/2 とも同一）：

```
W (238) c6bt_stock: pre-init synth=0x25824f50
W (248) c6bt_stock: ARM_B_NOBT_JUMP: no BT init, jumping to ASP3 in 2s
W (2248) c6bt_stock: ARM_B_NOBT_JUMP: asp3_jump_now()
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C6 ...
ble_host_smoke_c6: HANDOFF entry synth(0x600a00cc)=0x25824f50    ← ジャンプ直後，ASP3が何もする前
ble_host_smoke_c6: esp_bt_controller_init
ble_host_smoke_c6: esp_bt_controller_init OK (heap free=168928)
ble_host_smoke_c6: esp_bt_controller_enable(BLE)
ble_host_smoke_c6: esp_bt_controller_enable OK (heap free=168928)   ← standalone なら永久ハング
ble_host_smoke_c6: ble_hs SYNC, host up
ble_host_smoke_c6: advertising started as 'ASP3-C6-BLE' (own_addr_type=0)
ble_host_smoke_c6: g_adv_rc=0 g_adv_active=1
```

BlueZ で確認：`bluetoothctl scan on` で **`ASP3-C6-BLE`（14:C1:9F:E0:5A:9C）
を実機で初検出**——これまでの C6-BT 全ラウンド（実施87〜§18）で
一度も到達しなかった「ASP3 自身の BT が実放射する」状態に，ハンドオフ
経由で初めて到達した。

**Arm B は stock 側で BT を一切触っていない**（`esp_bt_controller_init/
enable` は一度も呼ばれていない）にもかかわらずこの結果——つまり
**「stock が BT PLL ロックを確立してそれを ASP3 が継承する」という
当初の作業仮説（本タスクの前提）は誤りで，真因はもっと単純**：
**stock の 2nd stage bootloader が実行する何か（BT とは無関係の
汎用クロック/RTC/PMU 初期化）だけで足りる**。

### 19.4 Arm A：BT フル初期化＋ロック＋adv 確認後のジャンプも同一結果（非退行）

`c6bt_armA`（controller_enable→adv→bit8=1 かつ adv 継続 3 秒安定確認
→ジャンプ）でも同一の ASP3 側ログ（`esp_bt_controller_enable OK`→
`g_adv_rc=0`）・BlueZ で `ASP3-C6-BLE` 実放射を確認。**Arm A と Arm B が
同一結果**＝アドバイザ提示の判別式（「B が通れば bootloader の
pmu_init 不足，B が失敗し A のみ通れば PLL ロック継承固有」）に
照らすと，**「B が通った」を明確に満たす**——原因は BT コントローラの
状態継承ではなく，**bootloader レベルの汎用初期化の有無**。

### 19.5 結論：判定確定＝«ソフト到達可能»。真因はより一般的（Direct Boot 全体の初期化不足）

- **判定＝★成功**：クロスカーネル・ハンドオフ後，ASP3 は自前の
  `esp_bt_controller_init/enable(BLE)` を完走し（standalone なら
  `ram_set_chan_freq_sw_start` で永久ハングする箇所），NimBLE host も
  sync・adv rc=0 に到達し，**BlueZ で `ASP3-C6-BLE` の実放射を確認**
  （2 系統×計3回のブートで再現）。「C6 BT の PLL ロックはソフト到達
  可能（＝直せる）」が実機で確定した。
- **真因の再定位**：Arm A/B が同一結果になったことで，§17/§18 まで
  「BT 固有（synth SW-start 経路・regi2c キャリブレーション等）」に
  絞り込んでいた仮説は，**より上流（bootloader レベルの汎用初期化）
  へ差し戻す**必要がある。WiFi 側 C5 調査（`c5-wifi-modem-domain-
  unpowered` メモリ参照）で確認済みの「Direct Boot は `pmu_init`
  を飛ばす（`SOC_PM_MODEM_PD_BY_SW` 未定義で power_domain_on が
  no-op）」という構造が，BT 側でも同型の根因である可能性が高い
  （直接の同定は次段）。
- **次段（S31/BT 本震向けの ToDo）**：Arm B の stock 側処理を
  bisect して「どの bootloader 処理が寄与しているか」を切り分ける
  （WiFi 側実施34-35 の減算法と同じ手法：`bootloader_hardware_init()`
  ／`bootloader_clock_configure()`／`rtc_clk_init()` 等を段階的に
  スキップして bit8 の生死を見る）。ASP3 の `hardware_init_hook()`/
  起動シーケンスへ，この bisect で特定した処理を移植すれば，
  **standalone Direct Boot のままで BT PLL ロックを解決できる**
  可能性が高い——ハンドオフは診断ツールであり，最終形は「ASP3 単体で
  bootloader 相当の初期化を行う」ことになる。
- **電源制御は不要だった**（esptool RTS reset のみで全実験完結。
  親への port1 電源 off/on 依頼は不要）。
- **未確認事項（正直な残課題）**：(1) Arm B が示す「どの bootloader
  処理か」は本ラウンドでは未特定（bisect 未実施）。(2) 2 系統×3回の
  再現のみ——統計的な収束率までは見ていない（§16 の「past success は
  再現しない」という教訓があるため，今後さらに複数回の独立再起動での
  再現性確認が望ましい）。(3) BlueZ 側は名前ベースの識別のみ（MAC も
  一致確認済みだが，GATT 接続までは未確認＝adv 放射の確認に限定）。

### 19.6 変更ファイル

- `apps/ble_host_smoke_c6/ble_host_smoke_c6.c`：`main_task()` 冒頭に
  synth-lock 診断ログ1行を追加（既存ロジック無改造）。
- `tmp/c6bt_stock/`・`tmp/c6bt_armA/`・`tmp/c6bt_armB/`：新規（stock
  側ハンドオフアプリ3バリアント，ESP-IDF v6.1 プロジェクト。`build/`
  は`.gitignore`で除外）。
- `build/c6ble/asp_flash_trunc1M.bin`：ASP3 guest（`ble_host_smoke_c6`）
  を再ビルドし先頭1MB truncate で再生成（`build/`配下のため未コミット，
  再現手順は本節と`docs/c5c6-lessons-for-s31.md`§3.3を参照すれば
  再生成可能）。
- board C（port1）は本ラウンド終了時点で Arm A のイメージ（stock+ASP3
  guest 二重配置）が flash されたまま。

### 19.7 ★★★重大な訂正（アドバイザ指摘への対応中に発覚）——standalone Direct Boot も «同一セッション内» で成功してしまい，§19.5 の因果推論は棄却。真因は未確定に後退

アドバイザから「§16 の standalone ハングは過去セッションの別バイナリで
確認されたもので，**今回ジャンプに使った `build/c6ble/asp_flash_trunc1M.bin`
そのものを 0x0 へ焼いて，同一セッション内で standalone ハングを再確認
していない**」という指摘を受け，対照実験として同一バイナリを
0x0（Direct Boot，stock 一切無し，ジャンプ無し）へ焼いて RTS reset した。

**結果：standalone Direct Boot が成功した（5/5，複数回の独立 RTS reset
で再現）**——`esp_bt_controller_enable OK`→`ble_hs SYNC`→
`g_adv_rc=0`→（このバイナリは前段のジャンプ実験でも同一挙動）。
これは §16-18（実施87〜§18，本ドキュメント）が繰り返し確認した
「standalone は 8/8 ハング」と直接矛盾する。

**この矛盾の解釈（重要な切り分け）**：

1. **SYSTIMER 氾濫修正（`44868d0`）が原因ではない**——この commit 自身が
   同日に `build/c6_s14`（§14 config）で再検証し，「clean console に
   なっただけで phy_init ハング自体は変化なし」と明記している
   （commit message に明記，本節執筆時点で確認済み）。ソースツリー上
   §16-18 のハング確認から本ラウンドまでの唯一の関連差分だが，
   その修正自体が「無関係」と自己検証済みのため候補から除外できる。
2. **有力仮説＝§18.6 で未決着のまま残されていた «warm-residual»**：
   standalone Direct Boot のテストを行う**直前**に，本ラウンドの
   Milestone 0（stock BT を約15秒 enable+adv 継続）と Arm A
   （stock BT を再度 enable+adv+3秒安定確認）で，**この同じ基板に
   対し実際の BT PLL ロックを计2回、合計20秒以上）实行していた**。
   RTS reset は完全な電源断ではない（`c5-latched-board-state` メモリが
   示す「RTS/soft reset では消えないラッチ状態」の前例と整合）ため，
   analog PLL のバイアス/レギュレータ等が warm な状態のまま複数回の
   RTS reset を跨いで残留し，それが standalone Direct Boot の
   `ram_set_chan_freq_sw_start` を «たまたま» 収束させた可能性が高い。
3. **これにより §19.5 の「Arm A/B が同一結果＝ハンドオフの寄与では
   なく bootloader 汎用初期化が真因」という結論の前提が崩れる**：
   Arm A/B の「成功」は本物だが，**その対照であるべき standalone
   ハングを同一セッション内で先に確認していなかった**ため，
   「ハンドオフが差をつけた」という主張は立証されていない。
   実際には「本ラウンド開始後この基板に対して行った最初の real BT
   PLL ロック（Milestone 0）以降，standalone を含めて総当り成功して
   いる」というデータの方が説明力が高い。

**現時点の正直な到達点（訂正後）**：

- **task の文字通りの成功基準（ハンドオフ後に ASP3 BT が到達し
  BlueZ で実放射）は満たされている**——これ自体は測定済みの事実で
  訂正の対象ではない（§19.3/19.4 のログ・BlueZ 検出は本物）。
- ただし **「ハンドオフが standalone ハングを解消した」という因果
  主張は棄却**。真因は未確定に後退し，最有力候補は §18.6 の
  warm-residual 仮説（本ラウンドで新たに強化されたが，まだ未確定）。
- **「ソフト到達可能か」という当初の問い自体には，本ラウンドの
  データはまだ答えを出していない**——«ジャンプが原因» と «直近の
  real BT 活動による warm 残留が原因» を分離する決定的実験
  （真の cold boot：長時間の完全電源断後に standalone を単体で
  最初に testする）が未実施のため。

**次段に必須の実験（電源制御が要る）**：
1. board C（port1）を**真に電源断**し，十分な時間（少なくとも
   数分，理想は物理抜き差し）冷却/放電させた後，**ハンドオフを
   一切行わず standalone Direct Boot を最初のテストとして**行う。
   ハングすれば warm-residual 仮説が支持され，ハンドオフの因果的
   価値も別途再検証が要る。成功すれば standalone ハング自体が
   何らかの理由で恒久的に解消されたことになり，話がさらに変わる。
2. 上記が「電源制御（Acroname USBHub3c）」を要する
   （このサブエージェントには権限が無い——**親に port1 の
   off/on を依頼する必要がある**）。RTS reset のみでは analog
   残留状態を排除できないというのが本節の中心的発見のため，
   今回に限り esptool RTS reset での代替は不可。
3. 上記と並行して，§18.6 の「wifi_scan で PLL を回した直後に BT へ
   切替えて bit8 の生死を見る」warm-residual 実験そのものも
   まだ本ラウンドの発見を踏まえて再設計すべき（今回は「BT→BT」の
   残留で観測されたため，「WiFi→BT」より «BT→BT» の残留の方が
   減衰時間が長い/短いなどの比較も価値がある）。

**教訓（`memory/feedback_hardware_investigation_rigor.md` への追記候補）**：
「別バイナリ・別セッションで確立された対照（standalone hang）を，
今回のバイナリ・今回のセッションで再確認せずに新実験の成功を
«ジャンプの効果» と早合点した」——本タスクのアドバイザが指摘した
とおりの失敗パターンを，まさにこのラウンドで実演してしまった。
訂正できたのは，アドバイザの指摘に従って対照実験を「done」と
言う前に実行したため。**「対照は同一バイナリ・同一セッションで
確認する」を今後绝対の手順にすること**。

**追加の反証実験（本節執筆中に実施）**：「診断ログ1行（§19.1で追加した
`HANDOFF entry synth` の`syslog`呼出し）自体が余分な遅延を作り，
それが phy_init のタイミング依存レースを«たまたま»回避させている」
という別の交絡仮説も検証した——診断ログを`#ifdef`で外した
ビルドでも standalone Direct Boot は**2/2で同じく成功**（`build/c6ble`
を診断行なしで再ビルド→0x0へ焼き直し→RTS reset 2回）。この仮説は
**反証**（診断ログの有無は結果に無関係）。また最初の standalone
成功から約20分後（RTS resetを複数回挟んだ後）に再テストしても
成功は継続しており，warm-residual が«数分で減衰する熱»のような
性質ではなく，**電源が入り続けている限り持続する状態**（レギュレータ/
バイアスの類）である可能性を示唆する（`c5-latched-board-state`の
「soft/hard resetでは消えないラッチ」との類似）。真の電源断による
検証が依然として必須。

### §19.8 ★親による «真の電源断» control（decisive）：C6 BT ハング＝«cold RF-synth-PLL ロック失敗» と確定・warm-residual を実証
BT ハンドオフ subagent の要請どおり、親（usbhub 電源制御の権限あり）が port1 を «真の電源断»
（off→5s→on、reflash せず＝standalone build/c6ble が 0x0）して cold 一発目を採取：
- **BlueZ（健全・近隣112台）で `ASP3-C6-BLE` 不在＝cold の standalone BT は広告しない**。
- clean console（氾濫根治済）：`HANDOFF entry synth(0x600a00cc)=0x25824e50`（**bit8=0=unlocked＝
  genuinely cold**）→ `esp_bt_controller_init OK` → `esp_bt_controller_enable(BLE)` →
  `I (64) phy_init:` で **ハング**（→WDT reset ループ）。
∴**C6 BT の phy_init ハングは «cold での RF-synth-PLL ロック失敗»**。対して subagent の
standalone «5/5 成功» は «直前の BT PLL 活動（Milestone0/ArmA）で PLL がロックし、その warm 状態が
RTS reset を跨いで残存» した warm ケース（真の電源断まで decay しない）。§14 RSSI-82 成功=warm、
§16-18 8/8 ハング=cold、で完全に整合。

**根＋修正方向の確定**：stock IDF BT は «cold でも» PLL をロックする（Milestone0＝STOCK-C6-BT 放射）
＝**PLL ロックはソフト到達可能**。ASP3 の cold BT init が «欠いている» のは stock がやる完全な
pmu_init/アナログ PHY 初期化（Direct Boot が飛ばす）。∴修正＝**stock の cold PLL-lock init 相当を
ASP3 BT init へ移植**（or ハンドオフを cold から実施＝stock cold ロック→ASP3 継承）。次の decisive＝
«cold 電源断→ハンドオフ Arm B（stock 最小 BT init で cold ロック→ASP3 ジャンプ）» で ASP3 が cold から
成功するかを confounder 無しで判定。

## 20. ★C6 BT «cold PLL-lock» 修正の実装＝stock の pmu_init を移植（実装完了・warm 非回帰確認・cold 実機判定は親の電源断待ち）

### 20.0 狙い（§19.8 の «真因＝Direct Boot が pmu_init を飛ばす» を直す）

§19.8 で親の真電源断 control が確定させた通り，C6 BT の phy_init cold ハングの
真因は，stock IDF が起動シーケンス（`esp_system/port/soc/esp32c6/clk.c` →
`pmu_init()`）で行う **PMU HP_ACTIVE 電源/クロック/アナログ記述子＋DIG/RTC LDO＋
bandgap o-code** の設定を，ASP3 の Direct Boot が丸ごと飛ばしていること．PMU が
POR 既定のまま＝MODEM/RF アナログドメインが正しく給電されず，
`register_chipv7_phy` の regi2c によるアナログ PLL 設定が着弾せず RF-synth-PLL
（`0x600a00cc` bit8）が cold でロックしない．兄弟の C5 WiFi（memory
`c5-wifi-modem-domain-unpowered`）が «残壁＝pmu_init 残り（HP_ACTIVE 電源記述子・
PMU 0x600b000c-0x600b0130）» と局在化済みで，C6 BT も同一クラス．

### 20.1 実装（hal 非編集＝リンク＋薄いシムのみ．禁则遵守）

- **移植したソース（hal submodule．IDF v6.1 とバイト一致確認済）**：
  `esp_hw_support/port/esp32c6/pmu_init.c`・`pmu_param.c`・`ocode_init.c` を
  `esp_bt_idf61.cmake` の source list へ追加（**hal を編集せず «リンク» するだけ**）．
- **薄いシム**（新規・target 側）：`bt/bt_pmu_init_c6.c` の
  `esp_shim_bt_pmu_init()` が `pmu_init()`＋`esp_ocode_calib_init()` を呼ぶ．
- **呼出し位置＝`target_kernel_impl.c` の `hardware_init_hook()` 末尾**
  （`TOPPERS_ESP32C6_BT` ガード）．stock が pmu_init を呼ぶのと同じ «早期»
  （カーネル/タイマ起動前）位相で呼ぶことで，HP_ACTIVE 電源記述子の適用が
  稼働中カーネルを撹乱しない（advisor 指摘．late 呼出しの ocode CPU-freq
  切替えリスクも回避）．
- **regi2c は NON_OS_BUILD で ROM 直呼び**：pmu_init/ocode/rtc_clk の
  `REGI2C_WRITE_MASK`/`regi2c_ctrl_write_reg*` を `esp_rom_regi2c_*`（ROM，
  ロック無し）へ解決（`set_source_files_properties(... NON_OS_BUILD=1)`）．
  最下層プロバイダ `esp_rom/patches/esp_rom_hp_regi2c_esp32c6.c` をリンク．
  これで `regi2c_ctrl.c`（`esp_os_*`/saradc/tsens 依存）の肥大を避けた．
  hardware_init_hook は単一スレッド・割込み前のためロック不要で妥当．
- **ocode の reset-reason ゲート回避**：pmu_init 内の `esp_ocode_calib_init()`
  は `reset_reason==POWERON` のときだけ走る．RTS ピンリセット（自己テスト）
  では走らず真電源断（親テスト）と食い違うため，シムで «明示的に» 呼ぶ
  （`set_ocode_by_efuse` は冪等＝二重呼出し無害．本 board=efuse blk_version
  v0.3>=1 のため set_ocode_by_efuse 経路＝CPU 周波数切替えを伴わず稼働中でも
  安全．bandgap o-code はアナログ基準電圧＝PLL にも効きうる）．
- ビルド：`build/c6ble`（SM 版）RAM 72.36%・link 0 エラー（RAM は §19 の
  72.35% から +0.01pt＝pmu_init 系の増分のみ・非回帰）．

### 20.2 実機（free cold window）で判った «warm 交絡» と，自己テストの限界

親が §19.8 で board を真電源断→cold standalone（pmu_init 無し）でハング
（bit8=0）を採取した «直後» の board は cold（ハングは PLL をロックしないので
warm-residual を残さない）．この free cold window を使って修正を自己テストした：

1. **NEW（pmu_init 有り）を 0x0 へ焼き RTS 起動**：`HANDOFF entry
   synth(0x600a00cc)=0x25824f50`（**bit8=1**）→ `esp_bt_controller_enable OK`
   → `ble_hs SYNC` → `g_adv_rc=0` → **BlueZ で `ASP3-C6-BLE` 実放射**．
   ＝機能的には完全動作（adv 到達・放射）．
2. **ただし «cold 実証» にはならない（§19 の教訓を踏襲した within-session
   control で確認）**：NEW を焼く際の `--after hard-reset` が capture 前に
   NEW を一度起動しており，そのブートで PLL がロックすると以降の RTS ブートは
   warm になる．これを切り分けるため **OLD（pmu_init 無し）を焼き直して
   同条件で起動**したところ，**OLD «も» adv 到達・放射した**＝board は現在
   warm（NEW の先行ブートが残した residual）．∴上記 NEW の成功は «warm board
   上» の観測で，**cold での修正効果は自己テストでは判定不能**（RTS では
   電源が切れず residual が decay しないため）．

**＝§19 で犯した «warm 交絡を cold 成功と早合点» を，今回は within-session
control（OLD 焼き直し）で先回りして検出し，cold 成功の主張を «保留» した．**

★entry で bit8=1 が見えた点の «正しい解釈»（advisor 訂正）：pmu_init は
アナログドメインを給電するだけで synth-start（bit8 を立てる register_chipv7_phy）
は走らせないので，«pmu_init が entry 前にロックした» という解釈は誤り．
entry の bit8=1 は純粋に **warm residual**（先行ブートで PLL がロック済み）の
証拠であり，OLD 焼き直しで OLD «も» bit8=1/放射だったことと整合する（board が
warm）．cold なら修正が効いても entry は bit8=0 で，ロックは後段 enable() で
初めて立つ（§20.3 の判定を参照）．

### 20.3 ★親への依頼＝decisive cold 判定（真電源断が必須）

board C（port1）は現在 **NEW（pmu_init 有り）standalone を 0x0 へ焼き，
`--after no-reset` で «未起動» のまま**にしてある（parent の電源投入直後の
一発目が clean cold になるように）．依頼：

1. **port1 を真電源断（off→数秒→on．reflash しない）**．投入後の «一発目»
   の console と BlueZ を採取．
2. 判定（マーカ単独でなく BlueZ 実放射で）：
   - **★entry の bit8 は «cold 妥当性チェック» であって «成功指標ではない»**
     （advisor 訂正）．`HANDOFF entry synth(0x600a00cc)` は `main_task` 入口＝
     BT init «前» に読む．RF-synth-PLL ロック（bit8）は後段の
     `esp_bt_controller_enable`→`register_chipv7_phy` で «初めて» 立つので，
     **修正が効いても cold の entry では bit8=0（`...e50`）**．pmu_init
     （hardware_init_hook）はアナログドメインを «給電» するだけで synth-start
     シーケンス自体は走らせない．
     - **entry bit8=0（`...e50`）＝board は genuinely cold＝test 妥当**．
     - **entry bit8=1（`...f50`）＝warm residual＝test 無効**（電源断が
       短すぎ．より長い off 時間で再試行）．
   - **★成功＝修正が効いた（これが唯一の判定）**：`esp_bt_controller_enable
     OK`→`g_adv_rc=0`→**BlueZ で `ASP3-C6-BLE` 放射**．§19.8 の cold ハング
     （enable で phy_init ハング・BlueZ 不在）からの反転＝pmu_init 移植が
     cold PLL-lock を解決したと確定．
   - **失敗＝修正が不十分**：`esp_bt_controller_enable` の phy_init でハング
     （→WDT ループ・BlueZ 不在）．この場合，entry の PMU readback
     （`0x600b000c`/`0x600b0010`/`0x600b0100`）を見て切り分ける：
     - PMU が POR から «変化している»＝pmu_init は着弾したが不十分＝次段は
       «どの pmu_init サブ処理／残る analog 前提» の bisect（C5 実施と同型）．
     - PMU が POR の «まま»＝pmu_init の書込みが stick していない（C5 兄弟の
       «無給電ドメインへの書込みは stick しない» と同型）＝呼出し位置/順序の
       再検討が必要．
3. **反証（warm 交絡回避）**：各試行の «直前に必ず真電源断» で cold を保証
   （§19 の教訓）．できれば複数回の cold 電源断で再現性も確認．
4. （任意・positive control）同 session で «cold 電源断→ハンドオフ Arm B
   （stock 最小 BT init で cold ロック→ASP3 ジャンプ）» も採れば，standalone
   修正の失敗と board ドリフト/退行を分離できる（advisor 提案）．

### 20.4 変更ファイル

- 新規：`asp3/target/esp32c6_espidf/bt/bt_pmu_init_c6.c`（`esp_shim_bt_pmu_init()`）．
- `asp3/target/esp32c6_espidf/esp_bt_idf61.cmake`：pmu_init.c/pmu_param.c/
  ocode_init.c＋ROM regi2c パッチをリンク，`esp_private` include 追加，
  NON_OS_BUILD の per-file 定義（pmu_init/ocode/rtc_clk）．
- `asp3/target/esp32c6_espidf/target_kernel_impl.c`：`hardware_init_hook()`
  末尾に `TOPPERS_ESP32C6_BT` ガードで `esp_shim_bt_pmu_init()` 呼出し追加．
- hal/・asp3_core は無編集（禁则遵守）．board C は NEW standalone を 0x0 に
  未起動で staged．

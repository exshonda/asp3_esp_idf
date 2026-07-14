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


# CONFIG_XX 監査（我々の手書き供給 vs 「動作する ESP-IDF」）

本ドキュメントは read-only 監査の成果物。**コードは変更していない**（監査のみ）。
背景・目的は指示書の通り：本プロジェクトは idf.py を使わず `CONFIG_XX`
（sdkconfig 相当）を手書きヘッダ／cmake の `-D` で供給しており，過去に
**「CONFIG_XX が 1 個足りず silent に動作しない」バグが実在した**
（`CONFIG_BT_NIMBLE_HS_PVCY` 未定義 → `MYNEWT_VAL(BLE_HS_PVCY)=0` →
responder の Identity 鍵配布が `#if MYNEWT_VAL(BLE_HS_PVCY)` でコンパイル
アウト → BLE bond 不成立。詳細＝`docs/bt-shim.md` 2646 行「★★★ D-2d bond
真因 «確定»」節）。目的＝同型の «未定義→機能サイレント消失» を系統的に
洗い出すこと。

## 0. 結論（要約）

- **PVCY 自体の回帰は無い**：`CONFIG_BT_NIMBLE_HS_PVCY=1` が
  **C3/C5/C6(hal)/C6(idf61) 全 4 変種で確認済み**（§2）。
- **新規の「PVCY 型」高リスク差分を 1 件検出した**：
  `CONFIG_BT_NIMBLE_SM_SIGN_CNT`（ATT Signed Write の CSRK sign-counter
  更新）が **C3/C5/C6/S3 の全変種で未定義**＝`MYNEWT_VAL(BLE_SM_SIGN_CNT)`
  は 0 にフォールバックする一方，ESP-IDF v6.1 Kconfig の既定値は
  **`default y`**（§3.11）。PVCY と同じ「chip 横断で共通に欠落している
  ため chip 間 diff では検出できない」型の差分であり，`comm` によるチップ
  間比較だけでは見えないことを踏まえ，Kconfig 既定値との照合で発見した
  （§3.11 に詳細・影響評価）。**ただし影響範囲は PVCY より大幅に狭い**
  （bonding 自体は壊れない．ATT Signed Write という meta の PDU 種別の
  replay-counter 更新のみに限定．§3.11 参照）。
- BT NimBLE 側で「C5/C6 にあり C3 に無い」10 個の `CONFIG_BT_NIMBLE_*`
  （`SM_LVL`/`SMP_ID_RESET`/`SM_SC_DEBUG_KEYS`/`MAX_EADS`/
  `HOST_BASED_PRIVACY`/`HS_FLOW_CTRL_*`/`L2CAP_ENHANCED_COC`/
  `DYNAMIC_SERVICE`）を実際に消費側コード（`esp_nimble_cfg.h` の
  fallback ロジック＋実際に参照する `.c`）まで降りて **1 個ずつ実証**した
  結果，**全て「未定義→フォールバック値が明示値と一致する」か
  「機能自体が別のマスタースイッチで無効化されており死コードになる」**
  ため **PVCY のような機能消失は起きていない**（§3 に file:line 付きで
  各々の根拠を記載）。
- WiFi 側の高リスクパターン（`CONFIG_SOC_WIFI_HE_SUPPORT` 等の SOC 能力
  ミラー欠落＝`wifi_osi_funcs_t` レイアウトずれ→`esp_wifi_init` 失敗，
  実施11で C5 において実際に発生した既知バグ）は **C3/C5/C6 の 3 チップ
  全てで soc_caps.h の実値と我々の供給値が一致**していることを確認した
  （§4）。coex 誤有効化（`CONFIG_ESPRESSIF_BLE`+`CONFIG_ESPRESSIF_WIFI`→
  `CONFIG_SW_COEXIST_ENABLE=1`）も 3 チップとも回避できていることを
  確認した（§5）。
- 値が違う（(b) 型）ものは 1 件（`CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES`：
  C3=6／C5・C6・S3=64）確認したが，コメントに明記された意図的な RAM
  節約でありリスクではない（§3.9）。
- **要修正候補＝§8 に 5 件列挙**（最有力は上記 `SM_SIGN_CNT`）。いずれも
  **本監査では修正しない**（read-only 監査のため）。今後の横展開作業で
  新しい `CONFIG_BT_NIMBLE_*` を追加するときは §3 の判定パターン（fallback を
  必ず消費側コードで確認する）を再利用すること。

## 1. 参照（「動作する ESP-IDF」）の取得方法

advisor レビューに基づき **idf.py ビルドは行っていない**（環境に
`~/tools/esp-idf-v6.1` は実在するが，フルビルドはこの監査の目的に対して
過剰＝ノイズが増えるだけ。以下の非対称な 3 本立てで代替した）：

1. **消費側コードそのもの**（一番信頼できる真実）：
   - C3 / C6(hal 変種) が使う NimBLE ソース＝
     `hal/components/bt/host/nimble/port/include/esp_nimble_cfg.h`
     （esp-hal-3rdparty submodule 同梱）。
   - C5 / C6(idf61 変種) が使う NimBLE ソース＝
     `~/tools/esp-idf-v6.1/components/bt/host/nimble/port/include/esp_nimble_cfg.h`
     （このマシンに実在する v6.1 ツリー，read-only で参照のみ）。
   - 両者の fallback ロジックは **diff で確認した通り実質同一**
     （行番号が数行ずれるだけで `#ifdef`/`#ifndef`/既定値は同一。§3 冒頭）
     ＝ C5 の fallback を hal 版のコメントで代用して差し支えない。
2. **姉妹プロジェクト S3**（`$HOME/TOPPERS/esp32_s3`，v5.5.4，
   WiFi+BT 実機動作確認済み）の `wifi/bt/stub/include/bt_nimble_config.h`
   ＝ PVCY バグの発見の元になった「働く比較対象」そのもの。
   C3 の同ファイルは元々この S3 版からの移植（ヘッダのコメント参照）。
3. **ESP-IDF v6.1 の Kconfig 既定値**（`~/tools/esp-idf-v6.1/components/bt/host/nimble/Kconfig.in`）
   ＝ 消費側コードの fallback が「chip 非依存に安全な既定」であることの
   独立クロスチェックとして使用（§3.11・§3.12）。

idf.py を使わなかった判断根拠：フルビルドで得られる `sdkconfig` は数百件の
`CONFIG_XX` を含み，そのほとんど（LWIP・SPI_FLASH・大半の ESP_WIFI 系）は
本プロジェクトがコンパイルしないコンポーネント向けであり，症状的な全数
diff は 99% ノイズになる。代わりに「実際にリンクされるソースが読む
`MYNEWT_VAL`/`CONFIG_XX` だけ」を出発点にして参照値を逆引きする方式を
取った（advisor 助言）。

## 2. 我々の CONFIG_XX 供給元（列挙）

| チップ | ファイル | 行数 | 備考 |
|---|---|---|---|
| C3 | `asp3/target/esp32c3_espidf/bt/stub/include/bt_nimble_config.h` | 195 | NimBLE host 一式＋controller 一式（旧世代 LEGACY_VHCI=1，security=ON＝D-2d bond 済） |
| C3 | `asp3/target/esp32c3_espidf/hal_stub/include/nuttx/config.h` | - | `CONFIG_ESPRESSIF_WIFI_*`（WiFi バッファ/AMPDU/SAE 既定），C6 と共有 |
| C3 | `asp3/target/esp32c3_espidf/esp_bt.cmake` / `esp_wifi.cmake` | 508 / ~600 | `ASP3_COMPILE_DEFS` の `CONFIG_*=`／`MYNEWT_VAL_BLE_SM_LEGACY=0`等 |
| C5 | `asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h` | 187 | NimBLE host のみ（新世代 LEGACY_VHCI=0），security=OFF（D-2a フェーズ） |
| C5 | `asp3/target/esp32c5_espidf/sdkconfig_stub/sdkconfig.h` | 279+ | `CONFIG_ESP_WIFI_*`／`CONFIG_SOC_WIFI_*`／`CONFIG_MBEDTLS_*` 一括供給（C5 は hal に nuttx 板ファイルが無いため全量自前） |
| C5 | `asp3/target/esp32c5_espidf/esp_bt.cmake` / `esp_wifi_v8.cmake` | 561 / - | IDF v6.1 のソースを bt.c/ble.c/ソース単位で取り込み，hal の wifi(v8)を使用 |
| C6 | `asp3/target/esp32c6_espidf/bt/stub/include/bt_nimble_config.h` | 183 | hal 版 nimble 用（新世代 LEGACY_VHCI=0） |
| C6 | `asp3/target/esp32c6_espidf/bt/stub_idf61/include/bt_nimble_config.h` | 191 | IDF v6.1 版 nimble 用（値は hal 版と同一，独立ディレクトリで PREPEND） |
| C6 | `asp3/target/esp32c6_espidf/esp_bt.cmake` / `esp_bt_idf61.cmake` / `esp_wifi.cmake` | 583 / 545 / - | 2 系統（hal／idf61）を option で切替 |
| 全チップ共通 | `asp3/target/*/esp_bt.cmake` 内 `ASP3_COMPILE_DEFS` | - | `CONFIG_BT_ENABLED`・`CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1`・`MALLOC_CAP_DMA/INTERNAL` 等 |

**CONFIG_BT_NIMBLE_HS_PVCY の現況確認**（回帰チェック）：

```
C3   : bt/stub/include/bt_nimble_config.h:189-193 → ESP32C3_BT_PVCY オプション経由，既定 ON→1
C5   : bt/stub/include/bt_nimble_config.h:127      → #define CONFIG_BT_NIMBLE_HS_PVCY 1
C6hal: bt/stub/include/bt_nimble_config.h:122      → #define CONFIG_BT_NIMBLE_HS_PVCY 1
C6v61: bt/stub_idf61/include/bt_nimble_config.h:130 → #define CONFIG_BT_NIMBLE_HS_PVCY 1
```

4 変種全てで 1（有効）＝**回帰なし**。

## 3. BT NimBLE：fallback 分析（高リスク最優先）

### 3.0 判定基準

advisor 指摘に基づき，「参照と値が違う」ことそのものはリスクの指標に
ならない。リスクの本質は次の一点：

> **我々が定義し «忘れた» CONFIG_XX が，消費側コード
> （`esp_nimble_cfg.h` の `#ifdef`/`#ifndef` fallback）を経由して
> «機能を丸ごとコンパイルアウトする» か否か**。

したがって各差分について (a) fallback 先の実値，(b) その値を実際に読む
`.c` ファイルでの使われ方（`#if` ガードか，配列サイズ等の直接値か），
(c) 明示値との比較，の 3 段で判定した。「未定義だとビルドが割れる」もの
（多数のヘッダコメントが警告済み）は **すでにビルドで自己防御されている
ため低優先**（advisor 指摘通り）。危険なのは「未定義でもビルドは通り，
機能だけ静かに消える」逆方向のパターン（PVCY 型）。

### 3.1 差分一覧（C3 に無く C5/C6 にある `CONFIG_BT_NIMBLE_*`）

`comm` によるキー集合比較（`grep -oE '^#define +CONFIG_BT_'` の和集合）：

```
CONFIG_BT_NIMBLE_DYNAMIC_SERVICE
CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY
CONFIG_BT_NIMBLE_HS_FLOW_CTRL_ITVL
CONFIG_BT_NIMBLE_HS_FLOW_CTRL_THRESH
CONFIG_BT_NIMBLE_HS_FLOW_CTRL_TX_ON_DISCONNECT
CONFIG_BT_NIMBLE_L2CAP_ENHANCED_COC
CONFIG_BT_NIMBLE_MAX_EADS
CONFIG_BT_NIMBLE_SM_LVL
CONFIG_BT_NIMBLE_SMP_ID_RESET
CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS
```

（逆方向＝C5/C6 に無く C3 にある物は `CONFIG_BT_NIMBLE_SVC_GAP_PPCP_*` 4
件のみ＝§3.8 で判定，および C3 の controller 一式 `CONFIG_BT_CTRL_*`／
`CONFIG_BT_BLE_*`／`CONFIG_BT_CONTROLLER_ENABLED` 群＝これは C5/C6 が
controller 設定を **cmake の `-D` で別供給**しているための構造差であり
ヘッダの欠落ではない＝リスク対象外）。

以下，§3.1 の 10 件を 1 つずつ検証する。

### 3.2 `CONFIG_BT_NIMBLE_SM_LVL`（C3 未定義）

- fallback（hal `esp_nimble_cfg.h:1037-1041`）：
  ```
  #ifndef MYNEWT_VAL_BLE_SM_LVL
  #ifdef CONFIG_BT_NIMBLE_SM_LVL
  #define MYNEWT_VAL_BLE_SM_LVL CONFIG_BT_NIMBLE_SM_LVL
  #else
  #define MYNEWT_VAL_BLE_SM_LVL (0)
  #endif
  #endif
  ```
  未定義→ **0**。C5/C6 の明示値も **0**。**同値＝差分なし**。
- 消費側：`ble_hs_cfg.c:31`（`.sm_sec_lvl = MYNEWT_VAL(BLE_SM_LVL)`），
  `ble_svc_hid.c`（HID 権限テーブルの `#if ... == 2/3` 分岐，本ビルドは
  HID service 未使用のため到達しない）。
- 判定：**リスクなし**（fallback = 明示値）。

### 3.3 `CONFIG_BT_NIMBLE_SMP_ID_RESET`（C3 未定義）

- fallback（`esp_nimble_cfg.h:1057-1061`）：`#ifdef` ガード付き，未定義→
  **0**。C5/C6 の明示値も **0**。**同値**。
- 消費側：`ble_gap.c:8899,9028`（`#if NIMBLE_BLE_SM && MYNEWT_VAL(BLE_SMP_ID_RESET)`）。
- 判定：**リスクなし**。

### 3.4 `CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS`（C3 未定義）

- fallback（`esp_nimble_cfg.h:834-836`）：
  ```
  #ifndef MYNEWT_VAL_BLE_SM_SC_DEBUG_KEYS
  #define MYNEWT_VAL_BLE_SM_SC_DEBUG_KEYS CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS
  #endif
  ```
  **`#ifdef` ガードが無い**＝`CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS` が未定義の
  ままトークン展開され，`MYNEWT_VAL_BLE_SM_SC_DEBUG_KEYS` は「未定義識別子
  `CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS`」というトークン列に展開される。
- 消費側：`ble_sm_alg.c:759,894` の **`#if MYNEWT_VAL(BLE_SM_SC_DEBUG_KEYS)`**
  のみ（配列サイズ等の直接値としては未使用）。C 前処理系の規則により，
  `#if` の定数式内で未定義識別子は **0 として評価される**（未展開のまま
  コンパイルエラーにはならない）。よって C3 では実質 **0**（デバッグ鍵
  無効）＝C5/C6 の明示値 **0** と **同値**。
- 判定：**リスクなし**（この 1 件は「`#ifdef` ガードが無い」珍しいパターン
  だが，用途が `#if` 文脈のみのため C の未定義識別子=0 規則で偶然安全側に
  落ちている。**今後同種のマクロを新規に追加する開発者への注意点として
  doc に残す価値あり**＝配列サイズ等，`#if` 以外の文脈で使われる同種の
  ガード無しマクロがもしあれば，それは硬いビルドエラーになる。今回確認
  した範囲では該当なし）。

### 3.5 `CONFIG_BT_NIMBLE_MAX_EADS`（C3 未定義）

- fallback（hal 版 `esp_nimble_cfg.h:1093-1095`／v6.1 版同 `1097-1098`，
  両者内容同一）：**`#else` 無し**＝`CONFIG_BT_NIMBLE_MAX_EADS` 未定義なら
  `MYNEWT_VAL_BLE_STORE_MAX_EADS` 自体が **定義されない**（マクロ不在）。
- 消費側：`ble_store_config_priv.h:59,135`／`ble_store_ram.c:69,627`／
  `ble_store_util.c:345`／`ble_store_config.c:66,675`／
  `ble_store_config_conf.c:75` で **配列サイズとして直接使用**（`#if`
  文脈ではない）＝もしこのマクロが素通しで参照されればハードなビルド
  エラーになるはずだが，全ての使用箇所が
  **`#if MYNEWT_VAL(ENC_ADV_DATA)` の内側**でガードされている
  （`ble_store_config_priv.h:57-61,133-137` 等）。
- `ENC_ADV_DATA` の fallback（`esp_nimble_cfg.h:115-118`）：
  ```
  #ifndef CONFIG_BT_NIMBLE_ENC_ADV_DATA
  #define MYNEWT_VAL_ENC_ADV_DATA (0)
  ```
  **4 チップとも `CONFIG_BT_NIMBLE_ENC_ADV_DATA` を定義していない**＝
  全チップで `ENC_ADV_DATA=0`＝MAX_EADS を使うコードブロック自体が
  **全チップ共通で死コード**。C5/C6 が `MAX_EADS=1` を明示しているのは
  「ENC_ADV_DATA を将来 ON にする時への備え」であり，実行時の挙動には
  現状影響しない。
- 判定：**リスクなし**（ENC_ADV_DATA という上位マスタースイッチが 4
  チップとも OFF のため，MAX_EADS 自体は現状無意味）。

### 3.6 `CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY`（C3 未定義）

- fallback（`esp_nimble_cfg.h:969-973`，`#if CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY`
  という raw `#if` 参照＝未定義→0）→ **0**。C5/C6 の明示値も **0**。
  **同値**。
- 判定：**リスクなし**。（余談：`esp_nimble_cfg.h:965` に
  `#if defined(CONFIG_IDF_TARGET_ESP32)` 専用の強制 `(1)` 分岐があり，
  これは原型 ESP32 のみに適用される特殊ケース＝C3/C5/C6 には無関係。
  S3 のヘッダコメント L190-201 が詳述する「S3 の HS_PVCY undefined ref」
  問題と同根だが，C3/C5/C6 は既に PVCY=1 を明示しているため無関係）。

### 3.7 `CONFIG_BT_NIMBLE_HS_FLOW_CTRL_ITVL` / `_THRESH` / `_TX_ON_DISCONNECT`（C3 未定義）

- マスタースイッチは別マクロ `CONFIG_BT_NIMBLE_HS_FLOW_CTRL`（末尾
  `_ITVL` 等が無い版）。fallback（`esp_nimble_cfg.h:842-846`）：
  `#ifdef CONFIG_BT_NIMBLE_HS_FLOW_CTRL` → 未定義なら
  `MYNEWT_VAL_BLE_HS_FLOW_CTRL = 0`。
  **C3/C5/C6 の «どれも» `CONFIG_BT_NIMBLE_HS_FLOW_CTRL`（マスター）を
  定義していない**＝全チップで flow control 自体が無効。
  `_ITVL`/`_THRESH`/`_TX_ON_DISCONNECT` は C5/C6 では明示値があるが，
  マスターが 0 のため **全チップで死値**（C3 が未定義でも実害なし）。
- 判定：**リスクなし**（C5/C6 の値も実質使われていない＝チップ差分では
  なく，そもそも本プロジェクト全体で HS_FLOW_CTRL 機能を使っていない）。

### 3.8 `CONFIG_BT_NIMBLE_L2CAP_ENHANCED_COC`（C3 未定義）

- fallback（hal 版 `esp_nimble_cfg.h:585-588` / v6.1 版 `589-592`，
  両者内容同一）：
  `#if CONFIG_BT_NIMBLE_L2CAP_ENHANCED_COC || CONFIG_BT_NIMBLE_EATT_CHAN_NUM`
  （raw `#if`，どちらも未定義なら 0）。
  C3 は `CONFIG_BT_NIMBLE_EATT_CHAN_NUM=0` を明示（`bt_nimble_config.h:112`）
  かつ `L2CAP_ENHANCED_COC` は未定義（=0 扱い）→ 式全体 = 0。
  C5/C6 は両方明示的に 0 → 式全体 = 0。**同一結果**。
- 判定：**リスクなし**。

### 3.9 `CONFIG_BT_NIMBLE_DYNAMIC_SERVICE`（C3 未定義）

- fallback（`esp_nimble_cfg.h:591-595`）：`#ifdef` ガード付き，未定義→
  **0**。C5/C6 明示値も **0**。**同値**。
- 判定：**リスクなし**。

### 3.9' `CONFIG_BT_NIMBLE_SVC_GAP_PPCP_*`（4件，逆方向＝S3に無くC3/C5/C6にある）

- fallback（`esp_nimble_cfg.h:1893-1914`）：**`#else` 無し**＝未定義だと
  `MYNEWT_VAL_BLE_SVC_GAP_PPCP_*` は「未定義識別子」に展開される。
- 消費側：`ble_svc_gap.c:30-35` の `PPCP_ENABLED` マクロが
  `MYNEWT_VAL(BLE_SVC_GAP_PPCP_MIN_CONN_INTERVAL) || ...`
  という **`#if` 文脈でのみ** 4 つの値を OR 参照する。実際の配列代入
  （`ble_svc_gap.c:311-316`）は `#if PPCP_ENABLED` の内側にあり，未定義→
  0 評価により **S3 では `PPCP_ENABLED` 自体が偽**＝配列代入コードごと
  コンパイルアウトされる（コンパイルエラーにはならない）。
  C3/C5/C6 は明示的に全部 0 を与えているため，同じく `PPCP_ENABLED`=偽。
  **S3 の「未定義」も C3/C5/C6 の「明示 0」も帰結は同じ**（PPCP
  characteristic 値は出力されないが，PPCP 自体を無効化する設計は
  意図通り）。
- 判定：**リスクなし**（S3 のヘッダコメントが「未定義だとコンパイル
  不能」と推測していたのは，`#if` context での未定義識別子=0 の扱いを
  見落としたものだが，実害が無いことを確認した）。

### 3.10 値の相違（(b) 型）：`CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES`

| 変種 | 値 |
|---|---|
| C3 | 6 |
| C5 | 64 |
| C6(hal/idf61) | 64 |
| S3 | 64（ESP-IDF Kconfig 既定） |

C3 のヘッダコメント（`bt_nimble_config.h:72-76`）に「RAM 節約のため小さく
取る（本ビルドは最小 GATT サービスのみ＝long write はほぼ発生しない）」
と明記された **意図的な値**。消費側は `ble_att_svr.c` の prepared-write
（long write）バッファプールのエントリ数のみに影響し，6 エントリを超える
同時 prepared write が来た場合はエラー応答（insufficient resources）に
なるだけで silent な機能消失ではない。**リスクなし（意図的な低リスク
トレードオフ，要修正候補としない）**。

### 3.11 ★高リスク（PVCY 型・新規検出）：`CONFIG_BT_NIMBLE_SM_SIGN_CNT`

**advisor レビューで指摘された通り，PVCY は「C3 と C5 と S3（の祖先）が
同時に 0 を共有した」共通モード欠落であり，チップ間 `comm` 比較や
S3 との diff では原理的に見えない**。同じ見え方をする欠落を洗い出す
ため，`esp_nimble_cfg.h` が条件参照する 117 個の `CONFIG_BT_NIMBLE_*`
のうち「**我々の 5 変種（C3/C5/C6hal/C6v61/S3）のどれも定義していない**
かつ **そのフォールバックを参照する `.c` が実際にリンクされている**」
ものだけを抽出し（`ble_gap.c`／`ble_sm*.c`／`ble_att_svr.c`／
`ble_gatts.c`／`ble_l2cap.c` 等，C3 の `ASP3_SYSSVC_TARGET_C_FILES` に
実在），該当した 6 件について ESP-IDF v6.1 の Kconfig 既定値を
1 件ずつ確認した：

| CONFIG | 消費側 `.c` | Kconfig 既定 | 我々の実効値 | 判定 |
|---|---|---|---|---|
| `BT_NIMBLE_HIGH_DUTY_ADV_ITVL` | `ble_gap.c` | `default` 行なし＝bool 既定 n | 0 | 一致・リスクなし |
| `BT_NIMBLE_HANDLE_REPEAT_PAIRING_DELETION` | `ble_gap.c` | `default n` | 0 | 一致・リスクなし |
| `BT_NIMBLE_HOST_QUEUE_CONG_CHECK` | `ble_gap.c`/`ble_hs_hci_evt.c` | `default n` | 0 | 一致・リスクなし |
| `BT_NIMBLE_CHK_HOST_STATUS` | `ble_gap.c:10450`/`ble_gatts.c:3478` | **`default y`** | 0 | §3.11.1 |
| `BT_NIMBLE_RECONFIG_MTU` | `ble_l2cap.c:253` | **`default y`** | 0 | §3.11.1 |
| **`BT_NIMBLE_SM_SIGN_CNT`** | `ble_att_svr.c:2548`/`ble_sm.c:2721` | **`default y`** | **0** | **★高リスク（下記）** |

#### 3.11.1 `CHK_HOST_STATUS` / `RECONFIG_MTU`：Kconfig 既定と乖離しているが低リスク

両方とも `MYNEWT_VAL(...)` ガードが囲むのは **アプリが明示的に呼び出す
public API 関数の定義そのもの**（`ble_gap_host_check_status()`
`ble_gatts.c:3478` の `ble_gatts_get_cfgable_chrs()`／`ble_l2cap.c:253`
の `ble_l2cap_reconfig_mtu_mps()`）。プロトコル層が自動的に踏む経路では
なく，**呼び出し元が存在しなければ「関数がリンクされていないだけ」で
挙動に一切影響しない**。本プロジェクトの `ble_host_smoke.c` 等アプリ層は
これらを呼んでいない（grep 0 件）＝**実害なし**。Kconfig 既定との
形式的な乖離はある（要修正候補 §8 に軽微差分として記載）が silent な
機能消失ではない。

#### 3.11.2 `CONFIG_BT_NIMBLE_SM_SIGN_CNT`：★高リスク・実プロトコル経路への影響あり

- ESP-IDF v6.1 Kconfig（`~/tools/esp-idf-v6.1/components/bt/host/nimble/Kconfig.in:1299-1304`）：
  ```
  config BT_NIMBLE_SM_SIGN_CNT
      bool "Enable Sign counter operations"
      default y
      depends on BT_NIMBLE_ENABLED
  ```
  **ESP-IDF の既定は有効**。一方 C3/C5/C6(両変種)/S3 の
  `bt_nimble_config.h` は **どれも `CONFIG_BT_NIMBLE_SM_SIGN_CNT` を
  定義していない**（5 変種横断で共通欠落＝PVCY と同型のパターン）。
- fallback（`esp_nimble_cfg.h:2272-2278`）：`#else` 付きで **0**。
- 消費側（実リンク確認済み＝`ble_att_svr.c`/`ble_sm.c` は C3
  `ASP3_SYSSVC_TARGET_C_FILES` に実在）：
  - `ble_att_svr.c:2495-2565`（`ble_att_svr_dispatch_signed_write` 相当，
    受信 ATT **Signed Write Command** の処理）：CSRK による AES-CMAC
    署名検証（2526-2546 行）は `MYNEWT_VAL(BLE_SM_SIGN_CNT)` の **外側**
    で常に実行される（＝認証自体は弱まらない）。ガードされているのは
    2548-2554 行の `ble_sm_incr_peer_sign_counter(conn_handle)` 呼び出し
    のみ＝**signed write 受理後のリプレイ防止用カウンタ更新だけがスキップ
    される**。
  - `ble_sm.c:2721` 以降：`ble_sm_incr_our_sign_counter()`／
    `ble_sm_incr_peer_sign_counter()` の**定義自体**が丸ごとガード内
    ＝関数が存在しない。
- **影響評価**：ATT **Signed Write Command**（CSRK 使用，暗号化リンク
  無しでの署名付き書き込み。通常の Write Request/Command とは異なる稀な
  PDU 種別）を実際に送ってくるピアがいた場合，署名検証（認証）自体は
  通るが，**サーバ側で sign counter が更新されないため，同一署名済み
  PDU の再送（リプレイ）が「カウンタ不一致」で弾かれる保護が働かない**
  （`ble_att_svr.c:2537-2540` の counter 比較はストア側の
  `value_sec.sign_counter` を参照するが，我々のビルドでは
  `ble_sm_incr_peer_sign_counter` が呼ばれないためストア側の値が
  更新されず，結果として古い署名済みメッセージが窓を持って再度受理され
  得る）。**PVCY（bonding 自体が全滅）より深刻度は大幅に低い**
  （Signed Write は現行 GATT サービス設計＝Read/Write/Notify の通常
  操作では使われず，D-2d までの実機テストでも到達実績が無い経路）。
  【未確認】実際にこのプロジェクトの central（BlueZ/スマホ）が Signed
  Write を送ってくるかどうかは確認していない（一般的な GATT クライアント
  は通常使わないため，実運用リスクは低いと推測するが実証はしていない）。
- **要修正候補**：`bt_nimble_config.h`（4 チップ分）に
  `CONFIG_BT_NIMBLE_SM_SIGN_CNT 1` を追加すれば
  `ble_sm_incr_our_sign_counter`/`ble_sm_incr_peer_sign_counter` が
  コンパイルインされ ESP-IDF 既定に揃う。**本タスクでは修正しない**
  （§8 に列挙）。

### 3.12 網羅性についての注記

`esp_nimble_cfg.h` は `CONFIG_BT_NIMBLE_*` を条件参照する箇所が 117 件
存在する（`grep -oE '#(ifdef|ifndef|if) CONFIG_BT_NIMBLE_'` で抽出）。
本監査は §3.1 の「C3/C5/C6 間で定義有無が割れている」10+4 件を優先して
file:line まで実証した。残りの大半（BLE Mesh／ISO／Channel Sounding／
GATT Caching／Ext-Adv 拡張機能等）は **3 チップとも一貫して未定義**
であり，かつ ESP-IDF v6.1 の Kconfig 既定（`Kconfig.in`）を確認した
代表例（`BT_NIMBLE_GATT_CACHING`＝menuconfig で `default y` 無し，
`BT_NIMBLE_STATIC_PASSKEY`＝`default n`，`BT_NIMBLE_SM_SC_ONLY`＝
`default 0`）と整合しているため，「参照 ESP-IDF との差分」ではなく
**そもそも未実装機能への到達不能**（該当 `.c` を ASP3_SYSSVC_TARGET_C_FILES
に積んでいない）と判断し，個別の file:line 実証は行っていない
【残 100 件超は Kconfig 既定一致の代表確認のみ・網羅実証はしていない】。
将来これらの機能（Mesh/ISO/CS 等）を有効化する場合は，本監査と同じ
手順（fallback を `esp_nimble_cfg.h` で確認 → 消費側 `.c` で `#if`
文脈か直接値かを確認）を踏むこと。

## 4. WiFi／PHY：SOC 能力ミラー（HE/5G/NAN 等）

既知バグ（実施11・`docs/c5-wifi-osi-abi-he-field.md` 系）＝
`CONFIG_SOC_WIFI_HE_SUPPORT` 欠落 → `wifi_osi_funcs_t`（旧 v9 版
`wifi_os_adapter.h`）の `_wifi_disable_ac_ax` フィールドがガードアウト
→ 構造体レイアウトが 4 バイトずれ → `esp_wifi_init` が 0x3001 で失敗，
という **構造体レイアウト silent 崩壊**パターンの横展開チェック。

現行ビルド（v9 は実施52で削除済み・v8/hal が唯一の WiFi 実装）が使う
`hal/components/esp_wifi/include/esp_private/wifi_os_adapter.h` には
`_wifi_disable_ac_ax` フィールド自体が存在しない（grep 0 件）＝当時の
バグ実体は削除済み v9 固有のものだったが，`CONFIG_SOC_WIFI_HE_SUPPORT`
マクロ自体は現行 hal ヘッダ（`esp_wifi_types_generic.h`／
`esp_wifi_he_private.h`／`esp_wifi_types_native.h`）でも参照され続けて
おり，供給の要否は現在も生きている：

| チップ | `CONFIG_SOC_WIFI_HE_SUPPORT` | `CONFIG_SOC_WIFI_SUPPORT_5G` | `CONFIG_SOC_WIFI_MAC_VERSION_NUM` | `CONFIG_SOC_WIFI_NAN_SUPPORT` | soc_caps.h 実値との一致 |
|---|---|---|---|---|---|
| C3 | 未定義 | 未定義 | 未定義 | 未定義 | ✅ 一致（C3 soc_caps.h に該当マクロ自体が無い＝HE/5G/NAN 非対応チップ） |
| C5 | `sdkconfig_stub/sdkconfig.h:115`＝1 | 同:116＝1 | 同:117＝3 | 同:118＝1 | ✅ `soc/esp32c5/soc_caps.h:628-631` と一致 |
| C6 | `hal/nuttx/esp32c6/include/sdkconfig.h:329`＝1（upstream ファイル，我々は編集していない） | 未定義 | 同:330＝2 | 未定義 | ✅ `soc/esp32c6/soc_caps.h:537-538` と一致（C6 は 5G/NAN 非対応で soc_caps.h 自体に無い） |

**判定：3 チップとも一致・欠落なし**。C6 は禁則（hal 直接編集禁止）通り
upstream の `hal/nuttx/esp32c6/include/sdkconfig.h` を使っており，これは
vendor 提供の実値ファイルのため我々の供給責任範囲外（監査対象は
「我々が書く」ファイルのみだが，参考として一致を確認した）。

## 5. coex 誤有効化の回避（3 チップ共通パターン）

`CONFIG_ESPRESSIF_BLE` を立てると（`hal/nuttx/esp32c3,c6/include/sdkconfig.h`
の `#if defined(ESPRESSIF_BLE) && defined(ESPRESSIF_WIFI)` 経由で）
`CONFIG_SW_COEXIST_ENABLE=1` になり coex 経路が意図せず有効化される，と
いう既知リスク（C3/C6 のヘッダ冒頭コメントに明記）について：

- grep で `asp3/target/**/*.cmake`・`bt/stub*/include/*.h`・
  `sdkconfig_stub/*.h`・`hal_stub/include/nuttx/config.h` を横断確認した
  結果，**`CONFIG_ESPRESSIF_BLE` を実際に定義している箇所は 0 件**
  （C3/C6 とも，コメントで「立てない」と明記され，実際に立てていない）。
- C5 はそもそも `hal/nuttx` の sdkconfig.h 機構を使わない独立スタブ構成
  のため，この二重フラグ問題自体が構造的に存在しない。
- `CONFIG_SW_COEXIST_ENABLE` そのものを定義している箇所も 0 件（3 チップ
  とも）。

**判定：3 チップとも意図通り coex は無効のまま**（プロジェクトの現フェーズ
＝WiFi+BLE coexist 未着手，`docs/`直近コミット「WiFi blob を v6.1 へ揃える
べきかの机上レビュー」参照＝coexist 着手時まで意図的に保留）。

## 6. WiFi バッファ／AMPDU/SAE 既定値の横断確認（軽量チェック）

`asp3/target/esp32c3_espidf/hal_stub/include/nuttx/config.h`
（C3/C6 共有）の `CONFIG_ESPRESSIF_WIFI_*` と，C5 の
`sdkconfig_stub/sdkconfig.h` の `CONFIG_ESP_WIFI_*` を突き合わせ：

| 項目 | C3/C6 (`CONFIG_ESPRESSIF_WIFI_*`) | C5 (`CONFIG_ESP_WIFI_*`) |
|---|---|---|
| STATIC_RX_BUFFER_NUM | 10 | 10 |
| DYNAMIC_RX_BUFFER_NUM | 32 | 32 |
| STATIC_TX_BUFFER_NUM | 0 | 0 |
| DYNAMIC_TX_BUFFER_NUM | 32 | 32 |
| TX_BUFFER_TYPE | 1（動的） | 1 |
| AMPDU_TX/RX_ENABLED | 1/1 | 1/1 |
| TX/RX_BA_WIN | 6/6 | 6/6 |
| ENABLE_WPA3_SAE | 1 | 1 |
| SAE_PK/SAE_H2E/SOFTAP_SAE/OWE_STA | 0/0/0/0 | 0/0/0/0 |
| STA_DISCONNECT(ED)_PM | 1 | 1 |

**一致**。ただし本チェックは主要パラメータのみの軽量比較であり，
`CSI_ENABLED`／`AMSDU_TX_ENABLED`／`NVS_ENABLED`／`MGMT_SBUF_NUM`／
`ESPNOW_MAX_ENCRYPT_NUM`／`TX_HETB_QUEUE_NUM` 等の残りは
**網羅的な file:line 突合を行っていない**（C5 側にのみ明示定義がある
ことは確認したが，C3/C6 側の `hal_stub/include/nuttx/config.h` 全文は
本監査で全読していない）。今後の詳細監査候補として残す。

## 7. 相互参照

- `docs/bt-shim.md` 2646 行「★★★ D-2d bond 真因 «確定»」節＝PVCY 型
  silent 故障の一次資料（本監査の起点）。
- `docs/blob-inventory.md`／`docs/c5-toolchain.md`／
  `docs/c5-wifi-osi-abi-he-field.md`（`memory/` 側）＝
  `CONFIG_SOC_WIFI_HE_SUPPORT` 欠落バグ（実施11）の一次資料（§4 の横展開元）。
- `docs/wifi-shim-c6.md`／`docs/ble-c5c6.md`／`docs/ble-c5c6-plan.md`＝
  C5/C6 の WiFi/BLE 実装経緯全般。

## 8. 要修正候補（本タスクでは修正しない・列挙のみ）

本監査で発見した差分のうち，bonding／接続そのものを壊す「今すぐ直すべき」
高リスク差分は **無い**（PVCY は既に修正済み・回帰なし）。ただし
`CONFIG_BT_NIMBLE_SM_SIGN_CNT`（4 項目）は ESP-IDF 既定と chip 横断で
共通に乖離する **PVCY 型の実差分**として次回対応の最有力候補に挙げる。
残りは優先度低・任意の改善候補：

1. `CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS` の fallback
   （`esp_nimble_cfg.h:834-836`，hal/v6.1 双方）は `#ifdef` ガードが
   無く「未定義識別子を `#if` 文脈でのみ使う」という偶然の安全性に
   依存している。新規に同種のマクロを追加する際，配列サイズ等
   `#if` 以外の文脈で使われる可能性がある場合は要注意（このリポジトリ
   のコードを直接直す話ではなく，hal/IDF 側の upstream 挙動の注意点）。
2. §6 の WiFi 詳細バッファパラメータ（CSI/AMSDU/NVS/MGMT_SBUF 等）の
   file:line 網羅監査は未実施＝次回監査候補。
3. §3.12 の Mesh/ISO/Channel-Sounding 等，3 チップとも未実装の
   NimBLE 拡張機能は，将来有効化する際に本監査と同じ fallback 確認
   手順を踏むこと（手順を §3.0/§3.12 に記載済み）。
4. **`CONFIG_BT_NIMBLE_SM_SIGN_CNT`**（§3.11.2）＝ESP-IDF Kconfig
   既定 `y` に対し 4 チップ全変種で未定義（実効値 0）。ATT Signed
   Write の replay-counter 保護が働かない。Bonding/接続には影響
   しないため優先度は中〜低だが，本監査で見つかった «唯一の»
   PVCY 型（chip 横断の共通欠落）差分＝次回対応の最有力候補。
5. `CONFIG_BT_NIMBLE_CHK_HOST_STATUS`／`CONFIG_BT_NIMBLE_RECONFIG_MTU`
   （§3.11.1）＝ESP-IDF Kconfig 既定 `y` に対し未定義（実効値 0）。
   ただしどちらもアプリが明示的に呼ぶ optional API の欠落のみで，
   本プロジェクトは呼んでいない＝実害なし・優先度は最低。

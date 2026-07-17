# C6 evidence-07 — **D-2c/D-2d のデバイス側準備（スマホ central へ引き渡し）**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`＝board C**（hub `1-6` port2）
前段: `evidence-c6-06`（W1 真cold 達成／BT 既定を v5.5.4 submodule へ flip）

★**本ラウンドは «デバイス側の準備» まで**。スマホ操作はユーザーが行う。
★**認証情報は一切使っていない**（BT のみ・WiFi 不使用）。

---

## 1. ★既定構成の実測（依頼 (a)）— **コーディネータの読みの2件は誤り／1件は正しい**

### 1.1 トグル体系（**名前が紛らわしい**＝実測で確定）

| option | 定義場所 | 既定 | **意味** |
|---|---|---|---|
| `ESP32C6_BT_IDF61` | `esp_bt.cmake:58` | **ON** | ★**«hal を使わない» の意**（外側トグル）。**«v6.1 を使う» の意ではない** |
| **`ASP3_BT_IDF_V554`** | `esp_bt_idf61.cmake:119` | **ON**（evidence-c6-06 で flip） | **内側トグル＝供給の実体を選ぶ**。ON＝**submodule v5.5.4** ／ OFF＝外部 v6.1 |
| `ESP32C6_BT_IDF61_NIMBLE` | `esp_bt_idf61.cmake:159` | OFF | ★`ASP3_APPLNAME STREQUAL "ble_host_smoke_c6"` で**自動 ON**（同 `:160`） |
| **`ESP32C6_BT_IDF61_SM`** | `esp_bt_idf61.cmake:592` | **OFF** | **非hal 経路の SM トグル**→`TOPPERS_ESP32C6_BT_SM` |
| `ESP32C6_BT_SM` | `esp_bt.cmake:440` | ON | **hal 経路 «専用» の SM トグル**（同じ app ガードを定義するが**別経路**） |

⇒ **既定＝「hal でない」×「v5.5.4 submodule」×「NimBLE 自動ON」×「SM=OFF」**。

### 1.2 コーディネータの読みの検算

| 読み | 実測 | 判定 |
|---|---|---|
| 「`ESP32C6_BT_IDF61:BOOL=ON` ＝ v6.1 ＝ flip 報告と噛み合わない」 | **`ESP32C6_BT_IDF61` は «hal でない» の意**。同じ cache に **`ASP3_BT_IDF_V554:BOOL=ON`** が在る。**ビルド実体でも「外部 v6.1 tree 参照＝0／submodule `bt.c` 参照＝3」** | **★誤読。flip は効いている** |
| 「`ASP3_APPLNAME` が空」 | **`ASP3_APPLNAME:UNINITIALIZED=ble_host_smoke_c6`**（型が `UNINITIALIZED`） | **★誤読**（`:STRING` 等で grep すると見えない） |
| 「`ESP32C6_BT_IDF61_NIMBLE:BOOL=OFF`」 | cache は **option の宣言既定**。実効は `:160` で自動 ON＝**`ble_gap_adv_start` 実リンク=1** で確認 | **★cache と実効値の混同** |
| **「`ESP32C6_BT_IDF61_SM:BOOL=OFF` ⇒ `0xABF4` が動かない＝D-2d 不可」** | **`e06_dflt` の `nm`：`ble_sm_pair_initiate`=0／`tc_aes_encrypt`=0／`ble_store_config_init`=0** | **★正しい** |

★**教訓（再利用可能）**：**cmake の `CMakeCache.txt` は «option の宣言既定» を記録するので、
`if()` で後から強制された実効値とは食い違う**。**供給や機能の判定は cache でなく
«ビルド実体（`build.ninja` の実パス）» と «ELF のシンボル» で行う**。
★**`ESP32C6_BT_IDF61` という名前は flip 後もはや誤称**（既定供給は v5.5.4）＝**改名は残課題**（§5）。

---

## 2. SM 込みビルドの tripwire（依頼 (b)）

**構成**＝既定（＝v554）＋ **`-DESP32C6_BT_IDF61_SM=ON`**（`build/e07_d2d`）。

| tripwire | e07_d2d（SM=ON） | e06_dflt（SM=OFF・対比） |
|---|---|---|
| `ble_sm_pair_initiate` | **1** | **0** |
| `ble_sm_alg_encrypt` | **1** | — |
| `tc_aes_encrypt`（tinycrypt） | **1** | **0** |
| `ble_store_config_init`（bond store） | **1** | **0** |
| `uECC_*` | **15** | — |
| `custom_svcs` / `custom_chrs` | 1 / 1 | 1 / 1 |
| `ble_gap_adv_start` | 2 | 1 |
| UUID `abf0`/`abf1`/`abf2`/`abf3`/`abf4` の実体 | **全て image に在り** | — |
| `TOPPERS_ESP32C6_BT_SM` のコンパイル箇所 | **134** | 0 |

★**シンボル数で «機能可否» は判断していない**（親指示）。**«不在の確認»**（SM=OFF 側が全て 0）
と **«実リンクの確認»** に使った。

---

## 3. 真cold の物証（依頼 (c)）

**真cold の証明**＝`uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 読み戻し 0**、
**かつ センチネル**（★`ble_host_smoke_c6` は STORE0-9 を**全部**使い空きが無いため、
**判定対象の `STORE0` 自身**に `0xCAFE5A9C` を置く＝**`0xCAFE5A9C` が残った run は
「POR 未発生＝無効」として弾く自己検証**。全 run で発生せず＝全て有効）。
★**`STORE4`(`=RTC_XTAL_FREQ_REG`)・`STORE1`(`=RTC_SLOW_CLK_CAL_REG`) は判定に使っていない。**
★**cold 中は UART を開いていない**（判定は LP_AON 直読み）。

| 構成 | 電源 | STORE0 sync | STORE2 adv | STORE3 adv rc | STORE7 intr |
|---|---|---|---|---|---|
| **e07_d2d（v554＋SM=ON）** | **真cold** | **`0x5ade51c0`** | **`0x0ade5000`** | **`0xad000000`** | **`0xa1020704`** |
| **e07_d2d** | **warm** | `0x5ade51c0` | `0x0ade5000` | `0xad000000` | `0xa1020704` |

⇒ **SM/tinycrypt/uECC フルスタックでも真cold・warm とも adv 到達＝D-2a/D-2b 非回帰**（依頼 (e)）。

---

## 4. ★★ユーザー向け手順（スマホ central）＝依頼 (d)

### 4.0 いまボードは **advertising 状態**

- **広告名＝`ASP3-C6-BLE`**（`apps/ble_host_smoke_c6/ble_host_smoke_c6.c:202`
  `#define BLE_DEVICE_NAME "ASP3-C6-BLE"` を実測）。
- **ペアリングは Just Works**（`sm_io_cap = BLE_SM_IO_CAP_NO_IO`・`sm_bonding=1`・
  **`sm_sc=1`＝LE Secure Connections**・`sm_mitm=0`）＝**PIN 入力は出ない**。
- ★**最後に `--after hard-reset` でアプリを起動して残した**。
  （**注意**：`esptool --after no-reset` で読むと **ROM download mode に留まり広告が止まる**。
  次に誰かが LP_AON を読んだら、**読み終わったら必ず `--after hard-reset` で起動し直すこと**。）

### 4.1 ★★最初に必ず：**GATT キャッシュの罠**（C3 で実際に踏んだ）

> **過去に `ASP3-C6-BLE` へ接続したことがある端末は、古い GATT 定義をキャッシュしており、
> 新しい特性（`0xABF4` 等）が «丸ごと不可視» になる。**
> C3 ではこれで「デバイス側の GATT 登録失敗」と**誤診しかけた**（実際はデバイス側は正しく登録済みで、
> 犯人は 100% スマホのキャッシュだった）。

**⇒ スマホ側で先に：**
1. Bluetooth 設定で **`ASP3-C6-BLE` を «このデバイスの登録を解除／forget»**
2. **Bluetooth を OFF → ON**
3. そのうえで nRF Connect 等で**新規スキャン→接続**（＝フレッシュ discovery）

★**「接続＋bond が成功した」は «古い bond の再利用» でも起こる**。
**必ず forget 後のフレッシュ試行で測ってください**（これも C3 の教訓）。

### 4.2 手順（nRF Connect 等）

| # | 操作 | 期待 | 意味 |
|---|---|---|---|
| 0 | スキャンして **`ASP3-C6-BLE`** を探す → CONNECT | 接続できる | 広告到達＋接続 |
| 1 | サービス **`0xABF0`** を開く | `0xABF1`〜`0xABF4` の4特性が見える | ★見えなければ **4.1 のキャッシュ**を疑う（デバイス側は登録済み） |
| 2 | **`0xABF1`** を **READ** | **`42 54 34 2d 4f 4b`＝`"BT4-OK"`** | 基本 READ |
| 3 | **`0xABF2`** を **subscribe（notify ON）** | 32bit **LE** カウンタが周期受信 | NOTIFY |
| 4 | **`0xABF3`** に任意値を **WRITE** | 書ける | WRITE |
| 5 | **★`0xABF4`** を **READ** | **未ペアなら «弾かれてペアリング要求»**→**Just Works で bond**→**再 READ で `"BT4-OK"`** | **★これが D-2d の本体**（bond で確立した LTK による暗号が実効） |

★**期待される挙動**：`0xABF4` は `BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC`（実測）なので、
**暗号化されていない接続では read が拒否され、スマホが自動でペアリングを開始**します。
**ペアリング後に read が通れば D-2d 成立。**

### 4.3 ★デバイス側の裏取り（ユーザーがスマホで触った «後»、次ラウンドで読む）

`esptool --chip esp32c6 --port <by-id JTAG> --before usb-reset --after no-reset --no-stub read-mem <addr>`
（★読むと広告が止まるので、**読み終わったら `--after hard-reset` で起動し直す**）

| addr | 名前 | **期待値**（実測したエンコード） |
|---|---|---|
| **`0x600B1020`** | STORE8 = **GAP CONNECT** | **`0x604E<status:8><conn_count:8>`**（例 `0x604e0001`＝status=0・1回目） |
| **`0x600B1024`** | STORE9 = **GAP DISCONNECT** | **`0xD15C<reason:8><disc_count:8>`** |
| **`0x600B1018`** | STORE6 = **ENC_CHANGE**（★D-2d の本体） | **`0x5DE000<status>`**＝**`0x5de00000` なら暗号化成功**。★同レジスタは `ble_hs` reset_cb と**共用**で、そちらは **tag `0x5E00`**＝**上位バイトで判別**（`0x5DE0` vs `0x5E00`） |
| **`0x600B100C`** | STORE3 = **`0xABF3` write 受信** | **`0x7717…`**（★adv rc `0xad000000` と**共用**＝write が来ると上書きされる） |
| `0x600B1000` | STORE0 = sync | `0x5ade51c0` |
| `0x600B1008` | STORE2 = adv | `0x0ade5000` |
| `0x600B101C` | STORE7 = intr trace | `0xa1020704`（storm 無） |

★**`0x600B1010`(STORE4)＝`RTC_XTAL_FREQ_REG`／`0x600B1004`(STORE1)＝`RTC_SLOW_CLK_CAL_REG`
＝ «判定に使ってはいけない»**（`evidence-c6-04` の申し送り。app が上書きする潜在バグは未修正）。

---

## 5. 残ブロッカー（依頼 (f)）

1. **D-2c/D-2d の OTA 判定はユーザー待ち**（スマホ central）。**デバイス側は準備完了・広告中**。
2. ★**`ESP32C6_BT_IDF61` は誤称になった**（既定供給は v5.5.4 なのに名前は "IDF61"）。
   **改名は «挙動を変えない» が全ビルド指定に波及**するので**ユーザー判断で別ラウンド**。
   ★現状は `evidence-c6-06`／本 evidence／option 説明文で «意味» を明記して回避している。
3. ★**`ble_host_smoke_c6.c` の STORE4/STORE1 潰し（`RTC_XTAL_FREQ_REG`/`RTC_SLOW_CLK_CAL_REG`）は
   未修正**。**判定に使わない運用で回避中**。恒久修正には «診断用ミラーを削るか移す» 判断が要る
   （STORE0-9 は全て使用中＝空き無し）。
4. **§10-12 の hal ハングの正体は未解決**（`evidence-c6-06` §6：個体差でも rev でも tree 変化でもない）。
   次の1手＝**当時のリセット経路(RTS/EN)で hal を起動し `STORE5`(clk src) を読む**＝1 run。
5. **「hal 参照 0」は未達**（`evidence-c6-05` §5.6 の壁）。

# C6 evidence-10 — **`ESP32C6_BT_SM` の既定を ON へ（C5 と同型・完全一致）**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`＝board C**（hub `1-6` port2）
前段: `evidence-c6-09`（hal 経路撤去・C5 と同型化。**SM 既定だけ C5(ON) と違い OFF** で申し送り）
ユーザー判断: **「ON にして」**

★**認証情報は一切使用していない**（BT のみ）。★**`apps/` は1バイトも変更していない**。

---

## 1. 結論（先に3行）

1. **★既定 ON にできた。C5 と «完全に» 同型になった**（`ESP32C5_BT_SM`(ON) ↔ `ESP32C6_BT_SM`(ON)）。
2. **★RAM は溢れない＝増加は +1376 B（+0.33pt）だけ**（tinycrypt/uECC はほぼ FLASH 側）。**残 116,732 B**。
3. **非回帰＝ビルド 7構成 PASS／hal deps 0 維持／実機 D-1・D-2b とも 真cold+warm 維持**。
   **`bt_smoke_c6`（NimBLE 無し）は «完全に無影響»**（構造・実測の両方で確認）。

---

## 2. ★RAM 実測（依頼 (b)）＝**既定変更 «前» に測った**

`ble_host_smoke_c6`（`-DESP32C6_BT_SM=OFF/ON` の単一変数 A/B）：

| | FLASH | RAM | RAM %（412KB 中） |
|---|---|---|---|
| **SM=OFF** | 362,128 B | 303,780 B | 72.00% |
| **SM=ON** | 400,640 B | **305,156 B** | **72.33%** |
| **★増加** | **+38,512 B** | **★+1,376 B** | **+0.33 pt** |

★**RAM 残＝116,732 B**（FLASH は 4MB 中 9.55%＝潤沢）。
★**tinycrypt/uECC はほぼ `.text`（FLASH）側**なので、**RAM への影響は +0.33pt に留まる**
＝**«RAM が増える» という懸念は実測では «ほぼ無い»**（＝止める理由が無い）。
★memory の §15 記録「一発リンク成功(RAM72%)」とも整合（72.33%）。

---

## 3. ★SM のゲート条件（依頼 (d)）＝**推測せず構造と実測の両方で確認**

**構造**（`if` のネストを機械的に辿った・実測）：

| | option 行 | 囲む `if` |
|---|---|---|
| **C5** | `ESP32C5_BT_SM` @445 | `if(ESP32C5_BT)` @37 → **`if(ESP32C5_BT_NIMBLE)` @432** |
| **C6** | `ESP32C6_BT_SM` @614 | `if(ESP32C6_BT)` @33 → **`if(ESP32C6_BT_NIMBLE)` @601** |

⇒ **SM は NimBLE ゲート配下**＝**NimBLE を使わないビルドでは option 自体が宣言されない**
＝**C5 と同じ構造**。

**実測**（既定 ON にした «後» の ELF シンボル）：

| build | NimBLE | `ble_sm_pair_initiate`/`tc_aes_encrypt`/`ble_store_config_init` |
|---|---|---|
| **`bt_smoke_c6`（既定・D-1）** | **OFF** | **0 / 0 / 0 ＝★完全に無影響** |
| `ble_host_smoke_c6`（既定） | 自動 ON | **1 / 1 / 1**（＋`uECC_*`=15） |
| `ble_host_smoke_c6` ＋`-DESP32C6_BT_SM=OFF` | 自動 ON | **0 / 0 / 0 ＝★可逆** |

⇒ **`bt_smoke_c6` は既定 ON でも壊れない**（構造・実測とも一致）。

---

## 4. 非回帰（依頼 (c)）

### 4.1 ビルド＝**7構成 PASS／FAIL 0**（hal 参照＝指標[1]＝`ninja -t deps` の hal 行数）

| build | hal deps | SM syms |
|---|---|---|
| `bt_dflt`（既定・D-1系） | **0** | **0**（NimBLE 無し） |
| **`ble_dflt`（既定＝SM=ON）** | **0** | **3**（＋`uECC_*`=15） |
| `ble_smoff`（`-DESP32C6_BT_SM=OFF`） | **0** | **0**（★可逆） |
| `ble_v61`（`-DASP3_BT_IDF_V554=OFF`） | **0** | 3 |
| `wifi` / `plain` | **0** | 0 |
| `bt_supoff`（`-DASP3_ESPIDF_SUPPLY=OFF`） | 1924 | 0 |

**tripwire（既定ビルド／SM=OFF 側との対比）**：
`ble_sm_pair_initiate` 1/0・`ble_sm_alg_encrypt` 1/0・`tc_aes_encrypt` 1/0・
`ble_store_config_init` 1/0・`uECC_*` 15/0・`ble_gap_adv_start` 2/2（＝SM 以外は不変）。
★**「不在」の対比まで取った**（＝ゲートが効いていることの実証）。

### 4.2 実機＝**D-1・D-2b とも 真cold + warm で維持**

真cold の証明＝`uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 読み戻し 0** ＋ センチネル
（`bt_smoke_c6`＝STORE6／`ble_host_smoke_c6`＝判定対象 STORE0 自身の自己検証）。
**cold 中は UART を開いていない**。**`STORE4`/`STORE1` は判定に使っていない**。

| 構成 | 電源 | 結果 |
|---|---|---|
| **`bt_smoke_c6` 既定＝D-1**（NimBLE 無し＝SM 無影響） | **真cold** | **`0xb1d00008`**・`0xa1020704`・sentinel `0x00000000` |
| 同 | warm | **`0xb1d00008`**・`0xa1020704` |
| **`ble_host_smoke_c6` 既定＝SM/tinycrypt/uECC フルスタック** | **真cold** | **sync `0x5ade51c0`／adv `0x0ade5000`／rc `0xad000000`／intr `0xa1020704`** |
| 同 | warm | 同左 |

⇒ **SM フルスタックを既定に載せても D-1／D-2a／D-2b は真cold・warm とも不変**。

### 4.3 ★ユーザーの D-2c/D-2d 検証を引き継げる（依頼 (c)）

**`apps/` は1バイトも変更していない**（`git status -- apps/` が空＝実測）。
かつ **`evidence-c6-08` §6 でユーザーが検証したのは «hal-free ＋ SM=ON» のビルド**。
⇒ **本ラウンドの変更は «既定値だけ»** であり、**既定構成＝そのとき検証された構成**になった
＝**既定と検証済み構成の乖離が «消えた»**（ユーザー判断の狙いどおり）。
**board は既定ビルドを真cold で焼いて `ASP3-C6-BLE` で広告中**。

---

## 5. 可逆性（依頼 (e)）

`-DESP32C6_BT_SM=OFF` ⇒ **SM シンボル 0／`ble_gap_adv_start` は 2 のまま**＝
**tinycrypt/uECC 非リンクの従来構成（D-2a/D-2b のみ）へ完全復帰**（ビルド PASS・hal deps 0）。

---

## 6. ★C5 との最終対比＝**完全一致**

| 項目 | C5 | **C6（最終）** |
|---|---|---|
| ファイル | `esp_bt.cmake` 1本 | **1本** |
| IDF61 系 | 0 | **0** |
| 供給 | `ASP3_BT_IDF_V554`(ON) | **同(ON)** |
| NimBLE | `ESP32C5_BT_NIMBLE`(OFF・APPLNAME 自動ON) | **`ESP32C6_BT_NIMBLE`(OFF・同)** |
| **SM** | `ESP32C5_BT_SM`(**ON**) | **`ESP32C6_BT_SM`(ON)** ★本ラウンドで一致 |

★**`evidence-c6-09` §2.2 で «意図的に揃えなかった» 最後の1点が解消**＝**C6 BT は C5 と同型**。

---

## 7. 残ブロッカー（依頼 (f)）

1. **孤児 `bt/bt_shim.c`**（hal 経路専用・参照 0）＝未削除（ビルドに入らないので無害・次ラウンドで可）。
2. `ble_host_smoke_c6.c` の **STORE4=`RTC_XTAL_FREQ_REG` / STORE1=`RTC_SLOW_CLK_CAL_REG` 潰し**は未修正
   （STORE0-9 全使用で空き無し＝「判定に使わない」運用で回避中）。
3. **§10-12 hal ハングの正体**は未解決（`evidence-c6-06` §6）。
   ★hal 経路を削除したので «当時の hal ビルド» 再現には git で戻す必要がある（候補2）。
   **候補1（RTS/EN リセットで `STORE5`(clk src) を読む＝1 run）は今でも可能**。
4. **D-2c/D-2d の «既定ビルドでの» OTA 再確認**はユーザー任意
   （§4.3 のとおり **アプリ無改変＋既定値だけの変更**なので引き継げる）。

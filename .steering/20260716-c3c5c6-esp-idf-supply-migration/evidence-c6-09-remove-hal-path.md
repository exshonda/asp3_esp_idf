# C6 evidence-09 — **hal 経路と `ESP32C6_BT_IDF61` を撤去（C5 と同型化）**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`＝board C**（hub `1-6` port2）
前段: `evidence-c6-08`（BT の hal 参照 0 達成。hal 経路は «退避路» として残置）
ユーザー判断: **「削除」**（退避路を捨てることを承知のうえでの決定）

★**認証情報は一切使用していない**（BT のみ）。

---

## 1. 結論（先に4行）

1. **★`ESP32C6_BT_IDF61` は消滅。hal 経路（`else()` ブロック 546行）も削除。C5 と同型になった**（§2）。
2. **派生トグルを C5 の命名へ改名**：`ESP32C6_BT_IDF61_NIMBLE`→**`ESP32C6_BT_NIMBLE`**（×10）／
   `ESP32C6_BT_IDF61_SM`→**`ESP32C6_BT_SM`**（×5）。**ファイルも1本に統合**（§3）。
3. **非回帰＝ビルド 9構成 PASS／hal 参照 0 維持／実機 D-1・D-2b とも 真cold+warm 維持**（§4）。
4. **副作用は1件・実測で確認**：**`ASP3_ESPIDF_SUPPLY=OFF` の «意味» が変わった**が
   **壊れてはいない**（§5）。**WiFi の可逆性は無傷**。

---

## 2. ★到達点（C5）を実測してから揃えた

### 2.1 ★私の測定ミスを1件、先に訂正する

私は当初「**C5 に SM トグルは存在しない**」と報告したが、**これは誤り**。
**原因＝grep の非対称**：C5 は `^option(`（行頭アンカー）、C6 は `^\s*option(` で数えていた。
**C5 の `ESP32C5_BT_SM` は `if()` の中で «インデントされている»**（`esp_bt.cmake:445`）ため
行頭アンカーで漏れた。**コーディネータの推測（`ESP32C5_BT_SM` があるはず）が正しく、
私の測定の方が誤っていた**。
★**教訓：2つの対象を比較するときは «同じ grep» で測れ。** 非対称な検査は存在しない差を作る。

### 2.2 C5 と C6（最終形）の対比＝**同型**

| | **C5（見本）** | **C6（本ラウンド後）** |
|---|---|---|
| ファイル数 | **1**（`esp_bt.cmake`） | **1**（`esp_bt.cmake`）★統合した |
| IDF61 系トグル | **0** | **0** ★撤去した |
| 供給選択 | `ASP3_BT_IDF_V554`（ON） | `ASP3_BT_IDF_V554`（ON） |
| NimBLE | `ESP32C5_BT_NIMBLE`（OFF・APPLNAME で自動ON） | **`ESP32C6_BT_NIMBLE`**（OFF・APPLNAME で自動ON） |
| SM | `ESP32C5_BT_SM`（**ON**） | **`ESP32C6_BT_SM`**（**OFF**） |
| 診断トグル | `..._APIERR_TRACE`／`..._RXTRACE` | `ESP32C6_BT_REGI2C_TRACE`／`ESP32C6_BT_PMU_INIT` |

★**SM の既定だけ C5 と違う（C5=ON／C6=OFF）。これは «意図的に» 揃えていない**：
**既定を ON にすると tinycrypt/uECC がリンクされ RAM と挙動が変わる**＝**命名の話ではなく
挙動の変更**。本ラウンドの実機非回帰は **SM=OFF 既定**で取っている。
⇒ **SM 既定を C5 に合わせるかは «別の判断»** としてユーザーへ申し送る（§6-2）。

---

## 3. 実装（刻んで実施・各段でビルド実測）

| 段 | 内容 | 検証 |
|---|---|---|
| **1** | `git rm esp_bt.cmake`（hal 版）＋**`git mv esp_bt_idf61.cmake esp_bt.cmake`**＝**生き残る内容の側を rename**して履歴を保つ。前文を差替え `if(ESP32C6_BT)` ラッパを付与 | **BUILD OK・hal deps 0** |
| **2** | 派生トグル改名（`..._IDF61_NIMBLE`→`..._NIMBLE` ×10／`..._IDF61_SM`→`..._SM` ×5） | **BUILD OK**・`-DESP32C6_BT_SM=ON` で tripwire（`ble_sm_pair_initiate`/`tc_aes_encrypt`/`ble_store_config_init` 各1）実リンク |
| **3** | **live な `FATAL_ERROR` 文言の是正**（§3.2） | **★実際に発火させて確認** |

### 3.1 ★段1で私が踏んだ自分のバグ（記録する）

最初、前文の差替えを **`s.index('option(ASP3_BT_IDF_V554')` で切って**しまい、
**1-118 行（＝`set(BT_TARGETDIR ${TARGETDIR}/bt)` などの «実コード» を含む）を丸ごと破壊**した。
症状＝`Cannot find source file: //bt_shim_idf61.c`（**変数が空**）。
★**復旧**＝`git show HEAD:...` から復元し、**「1-35 行は純コメント／実コードは 36 行目から」を
実測してから**、**コメントだけを差し替えた**（36 行目以降は1バイトも触っていない）。
★**教訓：`index()` で «最初に見えたコード» を境界にするな。境界は実測して決めろ**
（`grep -nE "^[^#]" | head` で最初の実コード行を確認する）。

### 3.2 ★live な誤案内を1件見つけて是正した

`ASP3_BT_IDF_V554=OFF` かつ v6.1 tree が無いときの `message(FATAL_ERROR ...)` が
**「退避先として `-DESP32C6_BT_IDF61=OFF (hal)` を使え」と案内していた**——
**その退避先は本ラウンドで削除された＝存在しない指示**。
⇒ 文言を「既定（`-DASP3_BT_IDF_V554=ON`）へ戻せ／**hal fallback はもう無い**」へ是正し、
**`-DESP_IDF61_DIR=/nonexistent` で実際に発火させて新文言を確認**した。
★**コメントだけでなく «実行時メッセージ» も «削除で嘘になる»**。

### 3.3 docs/.steering の旧名は**書き換えていない**（親指示）

**62箇所の旧名は当時の記録として保全**。**cmake 側に読み替え表を1箇所だけ置いた**：

```
ESP32C6_BT_IDF61=ON      → 現在の既定（hal 非経由＝本ファイル）
ESP32C6_BT_IDF61=OFF     → 削除された hal 経路（もう存在しない）
ESP32C6_BT_IDF61_NIMBLE  → ESP32C6_BT_NIMBLE
ESP32C6_BT_IDF61_SM      → ESP32C6_BT_SM
```

---

## 4. 非回帰（依頼 (c)）

### 4.1 ビルド＝**9構成 PASS／FAIL 0**（hal 参照は指標[1]＝`ninja -t deps` の hal 行数）

| build | hal deps |
|---|---|
| `bt_dflt`（既定・D-1系） | **0** |
| `ble_dflt`（既定・D-2b系） | **0** |
| `ble_sm`（`-DESP32C6_BT_SM=ON`＝D-2c/D-2d系） | **0** |
| `ble_v61`（`-DASP3_BT_IDF_V554=OFF`＝外部v6.1） | **0** |
| `wifi` / `plain` | **0** |
| `bt_supoff`（`-DASP3_ESPIDF_SUPPLY=OFF`） | 1924 |
| `wifi_off` / `plain_off`（同 OFF） | 7167 / 177 |

### 4.2 実機＝**D-1・D-2b とも 真cold + warm で維持**

真cold の証明＝`uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 読み戻し 0** ＋ センチネル。
**cold 中は UART を開いていない**。**`STORE4`/`STORE1` は判定に使っていない**。

| 構成（**hal deps 0**） | 電源 | 結果 |
|---|---|---|
| `bt_smoke_c6` 既定＝**D-1** | **真cold** | **`0xb1d00008`**・`STORE7=0xa1020704`・sentinel `0x00000000` |
| 同 | warm | **`0xb1d00008`** |
| `ble_host_smoke_c6` 既定＝**D-2b** | **真cold** | **sync `0x5ade51c0`／adv `0x0ade5000`／rc `0xad000000`／intr `0xa1020704`** |
| 同 | warm | 同左 |
| `ble_host_smoke_c6`＋`SM=ON` | **真cold** | 同左（＝D-2c/D-2d 構成で adv 到達） |

### 4.3 ★D-2c/D-2d は **ユーザーの検証を引き継げる**

**`apps/` は1バイトも変更していない**（`git status -- apps/` が空＝実測）。
⇒ **本ラウンドの変更は «cmake の供給元/トグル» だけ**＝`evidence-c6-08` §6 でユーザーが
確認した D-2c/D-2d は**同じ論法（「供給元だけの変更」）で引き継げる**。
**board は `ASP3-C6-BLE` で広告中**（**hal-free ＋ SM=ON ＋ 真cold ブート**）。

---

## 5. ★副作用（依頼 (d)）— **1件・実測で確認・壊れてはいない**

**`ASP3_ESPIDF_SUPPLY=OFF` の «意味» が変わった**：

| | 削除前 | **削除後** |
|---|---|---|
| BT で `=OFF` | 基盤を hal から採る **＋ `-DESP32C6_BT_IDF61=OFF` で hal の BT 経路も選べた** | **基盤だけ hal から採る。BT の blob+グルーは常に esp-idf/v6.1**（hal の BT 経路は存在しない） |
| ビルド | 通る | **通る（実測：`bt_supoff` PASS・hal deps 1924）** |

⇒ **«hal の BT コントローラで動かす» という選択肢は失われた**（＝ユーザーが承知で捨てた退避路）。
**ただし `=OFF` 自体は壊れていない**（基盤 hal＋esp-idf BT で整合してビルドが通る）。

**WiFi 側の可逆性＝無傷**（実測）：`wifi`（既定）hal deps **0** ／ `wifi_off`（`=OFF`）**7167**
＝**従来どおり hal 供給へ戻せる**。`plain` も 0／177 で同様。

---

## 6. 残ブロッカー／ユーザー判断事項（依頼 (e)）

1. ★**孤児ソース `asp3/target/esp32c6_espidf/bt/bt_shim.c`**（hal 経路専用。参照数 **0**＝実測）。
   **削除していない**＝親指示「削除は動くものを壊しうる／刻め」に従い、**本ラウンドの範囲外**。
   **次ラウンドで消せる**（ビルドに一切入らないので無害）。
2. ★**`ESP32C6_BT_SM` の既定が C5（ON）と違う（C6=OFF）**＝**意図的**（§2.2）。
   **揃えるなら «挙動の変更»**（tinycrypt/uECC リンク・RAM 増）＝**実機再測が要る**＝ユーザー判断。
3. `ble_host_smoke_c6.c` の **STORE4=`RTC_XTAL_FREQ_REG` / STORE1=`RTC_SLOW_CLK_CAL_REG` 潰し**は未修正。
4. **§10-12 hal ハングの正体**は未解決（`evidence-c6-06` §6）。
   ★**hal 経路を削除したので «当時の hal ビルドを再現して測る» には `git` で戻す必要がある**
   （`evidence-c6-06` §6.2 の候補2）。**候補1（RTS/EN リセットで `STORE5` を読む）は今でも可能**。
5. **D-2c/D-2d の再確認**はユーザー待ち（アプリ無改変＝引き継ぎ可・§4.3）。

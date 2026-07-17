# C3 evidence-03 — BT 供給移行（**hal 参照 0 達成**）／実機ラウンドへの**事前登録予測**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `60:55:F9:57:BA:BC`**（hub `1-6` **port1**）
雛形: **C5 `esp32c5_espidf/esp_bt.cmake`（`ESP_HAL_DIR`＝0箇所）の転写＝新規設計ではない**

> ★**本ファイルの §3（予測）は実機を1回も触らずに書き，実機の前に commit した。**
> 以後の改訂で予測を書き換えない。

---

## 1. 結論（ビルド段階．先に3行）

1. **C3 BT/BLE の供給移行完了＝`ninja -t deps` の hal 参照 **0**、かつ**リンク行（`-L`/`-T`）も 0**。
   実際にリンクされる `libbtdm_app.a` は **`859e8c8e`＝真の v5.5.4 タグ**（実測）。
2. **新規の壁は 1 件だけ**（`freertos/FreeRTOSConfig.h`）＝**C5 の転写でほぼ足りた**。
3. **可逆性・非回帰も実測**（`-DASP3_ESPIDF_SUPPLY=OFF` で hal 供給へ完全復帰・ビルド成立）。

---

## 2. ビルド実測（依頼 (a)(b)(e)）

| build | 供給 | cmake | build | **.d hal** | **link hal** | 備考 |
|---|---|---|---|---|---|---|
| `c3bt_idf`（`bt_smoke`） | **esp-idf submodule** | 0 | **0** | **0** | **0** | `-L` は submodule の bt/esp_phy/esp_coex/soc-ld のみ |
| `c3ble_idf`（`ble_host_smoke`＋NimBLE） | **esp-idf submodule** | 0 | **0** | **0** | **0** | RAM 306,460 B (93.52%) |
| `c3bt_halbk` | hal（`-DASP3_ESPIDF_SUPPLY=OFF`） | 0 | **0** | 890 | 62 | **可逆性 OK** |
| `c3ble_halbk` | hal（同上） | 0 | **0** | 5690 | 158 | RAM 306,492 B (93.53%)＝**esp-idf 版と実質同一** |

**`~/tools/` 参照は全ビルドで 0。**

### 2.1 移行の内訳（`esp_bt.cmake` の `ESP_HAL_DIR` 35箇所 → **1箇所**＝hal fallback の代入のみ）

C5 の写像をそのまま当てた：

| 対象 | 移行先 | 根拠 |
|---|---|---|
| **BTツリー**：`bt/*` ヘッダ・controller `bt.c`・PHYソース・**blob**・**ROM ld**・**NimBLE**・tinycrypt | **`${BT_IDF}`** | C5 は同じものを `${IDF}` から採る。blob と «同世代» でなければ osi ABI が合わない |
| **基盤**：`esp_hw_support`/`esp_system`/`esp_rom`/`heap`/`log`/`riscv`/`efuse`/`esp_event`/`esp_pm`/`esp_timer` | **`${ESP_SUP_DIR}`** | C5 と同一 |
| **hal 分割 `esp_hal_<x>`**（gpio/clock） | **`${ESP_SUP_HAL_<x>}`** | esp-idf では `components/hal` に集約 |

### 2.2 ★移行前は「BTツリー自体」が混成していた（**本ラウンドの発見**）

移行前の C3 は
- `${BT_IDF}/components/bt/include/...`（＝トグルで切り替わる）
- `${ESP_HAL_DIR}/components/bt/common/...`・`bt/porting/...`（＝**hal 固定**）

を同時に参照していた ⇒ **`ASP3_BT_IDF_V554=ON` にすると「`esp_bt.h` だけ v5.5.4・`porting` は hal」**
という**版の混成**が起きる構造だった。C5 は `bt/*` を**全て同一ツリー**から採る。本移行でそれに揃えた。

★**これは §3 の予測に直接効く**（後述：**過去の bond 失敗は «混成ビルド» での観測だった可能性がある**）。

### 2.3 踏んだ壁と解き方（依頼 (b)）

| # | 壁（実測） | 解き方 | 新規か |
|---|---|---|---|
| 1 | **`esp_task.h:25` が `freertos/FreeRTOSConfig.h` を要求**（`esp_bt.h:14` → `esp_task.h`）。hal 版 `esp_task.h:24` は `platform/os.h` ＋ `OS_TASK_PRIO_MAX` に**置換**されており不要だった（実測：両ツリーの差はこの include だけ） | **C5 の同名スタブを転写**（`bt/stub/include/freertos/FreeRTOSConfig.h`．`configMAX_PRIORITIES=25`／`configTICK_RATE_HZ=1000`＝**C3 既存 `FreeRTOS.h` と同値**）。★C5 のコメント自身が「C3 の方針に倣った」と述べており，**その C3 側の対応物を作っただけ** | **新規1件**（ただし C5 に完成品があった） |
| 2 | `option(ASP3_BT_IDF_V554 … OFF)` が**ファイル前半**にあり、後段の «`ASP3_ESPIDF_SUPPLY` に追従» ロジックより先に **cache へ OFF を焼く** | option 宣言を供給移行ブロックへ**移動** | 実装上の罠（実測で判明） |
| — | `shared_periph_module_t`／`soc_root_clk_circuit_t`（evidence-c3-01 §5-3 で «混ぜたことによる破綻» と特定） | **BTツリーごと移した結果、混成そのものが消滅**＝**再現しなくなった**。以前入れた「`ESP32C3_BT=ON` なら `ASP3_ESPIDF_SUPPLY` 既定 OFF」の例外は**撤去** | 解決 |

⇒ **HANDOFF §4-3-2 の一般則（「ヘッダとソースを揃えて移せばリネーム問題は消滅する」）が、
BT でもそのまま成立した**。C6（122箇所）と違い C3（35箇所）は C5 の写像で足りた。

### 2.4 供給元の «混成» を構造的に禁止した

`ASP3_BT_IDF_V554` を **`ASP3_ESPIDF_SUPPLY` に追従**させ、食い違う指定は **`FATAL_ERROR`** で落とす
（＝「混ぜた」ことに起因する難解なコンパイルエラーを、設定段階の明示的エラーに置き換える）。
- `(SUPPLY=ON, BT=ON)` … 全 esp-idf ＝**既定**＝hal 0
- `(SUPPLY=OFF, BT=OFF)` … 全 hal ＝可逆 fallback
- 混成 … **configure 時に停止**

### 2.5 tripwire（「本当に esp-idf の NimBLE に乗っているか」の実測）

| 検査 | 結果 |
|---|---|
| `ble_gap.c` の供給元 | **`esp-idf/components/bt/host/nimble/.../ble_gap.c`**（hal 版は 0 件） |
| `bt.c`（controller グルー）の供給元 | **`esp-idf/components/bt/controller/esp32c3/bt.c`** |
| `ble_gap_adv_start`／`ble_hs_init`／`esp_bt_controller_init` | **各 1**（実リンク） |
| OSI_VERSION `0x1000B` リテラル | **2 件**（＝v5.5.4 `bt.c` の OSI 0x0001000B と整合。hal は 0x0001000A） |
| `nimble_port_init` | **0（esp-idf・hal 両方とも 0）**＝**非回帰**（アプリが独自 init 経路を使う） |
| RAM | esp-idf 306,460 B ／ hal 306,492 B ＝ **差 32 B**＝版差による RAM 破綻なし |

---

## 3. ★★実機ラウンドへの事前登録予測（**実機を触る前に commit**）

### 3.1 何を測るか

`c3ble_idf`（＝BLE・esp-idf 供給・hal 0）を **真cold** で起動し、
既存の RTC マーカ（`ble_host_smoke.c` が既に持つ）を直読みする：

| marker | addr | 意味 |
|---|---|---|
| SYNC | `0x60008050` | NimBLE ホスト sync 到達 |
| ADV | `0x6000805C` | `ble_gap_adv_start` 試行 |
| adv rc | （同上系） | `ble_gap_adv_start` の戻り値 |
| CONN | `0x600080C0` | 接続確立 |
| PAIR | `0x60008054` | ペアリング／bond 結果 |

★`0x600080BC`(STORE5) は **ROM が上書き**するので使わない（evidence-c3-02 §6.2 で実測再確認）。

### 3.2 予測（確度つき）

| # | 予測 | 確度 | 根拠 |
|---|---|---|---|
| **P1** | **D-1 到達**（controller init/enable → HCI Reset → **Command Complete**） | **85%** | v5.5.4 の `bt.c`（OSI `0x0001000B`＋`_malloc_retention`）と v5.5.4 blob（`859e8c8e`）は**同一ツリー＝自己整合**。過去の失敗は **bond（D-2d 相当）**であって D-1 ではない＝D-1 は当時も通っていた |
| **P2** | **D-2b 到達**（NimBLE sync → `ble_gap_adv_start` **rc=0**）・**hci0 で `ASP3-C3-BLE` を検出** | **75%** | 同上（過去に bond «試行» まで行っている＝adv は出ていた）。ただし**真cold での BLE は C3 で一度も検証されていない**（evidence-c3-02 §7-3）ので warm 実績より確度を下げる |
| **P3** | **bond が成立する**（＝`AuthenticationTimeout` が**再現しない**） | **55%** | §2.2 の発見が効く——**過去の «v5.5.4 で bond 失敗» は (i) 実際には +1169(≡v6.1) ツリー、かつ (ii) `bt/common`+`bt/porting` が hal のままの «混成ビルド» での観測**。**2つの交絡が同時に除去された**のが今回。ただし**「v5.5.4 blob が bond 健全である」という積極的証拠は無い**ので 55% に留める |

### 3.3 ★★決定的対照（これを省くと «偽の成功譚» になる）

**P3 は単独では帰属を決められない**ので、**同一手順で 2 アームを測る**：

| アーム | 供給 | ビルド |
|---|---|---|
| **A** | **esp-idf（真の v5.5.4）** | `c3ble_idf` |
| **B（対照）** | **hal**（＝D-2c/D-2d 実機達成の実績がある構成） | `c3ble_halbk` |

| 実機結果 | 帰属 |
|---|---|
| **A 成功・B 成功** | **据置きの根拠は死ぬ**（v5.5.4 で bond が通る）。**既定を esp-idf にできる** |
| **A 失敗・B 成功** | **供給に帰属**＝「v5.5.4 は bond でこける」が**初めて真のタグに対して**示される＝据置きが正当化される |
| **A 失敗・B 失敗** | **★私のハーネス／central 側の問題**＝**供給の話ではない**（＝この対照が無ければ «v5.5.4 のせい» と誤断していた） |
| **A 成功・B 失敗** | 想定外＝要再測（hal 実績と矛盾するので測定系を疑う） |

### 3.4 ★含意の自問（「反証条件も仮説である」）

| | 主張 | 健全か |
|---|---|---|
| (A) | 「bond 成功 ⇒ **v5.5.4 は bond 健全**」 | **★健全でない**。1回・1 central の成功は**「記録された失敗が再現しない」までしか言えない**。恒常性は別（過去の失敗が間欠的だった可能性も残る） |
| (B) | 「bond 失敗 ⇒ **据置きの根拠は生きている**」 | **★健全でない**＝私の移行が新たな不具合を入れた可能性・central 側の差・GATT キャッシュ等と区別できない。**だから §3.3 の B アームが要る** |
| (C) | 「D-1 が通る ⇒ bond も通る」 | **健全でない**（層が違う）。**P1 と P3 は独立に測る** |
| (D) | 「hal で bond できた（過去実績）⇒ 今日も hal で bond できる」 | **健全でない**＝**過去実績は全て warm**（evidence-c3-02 §7-3）。**B アームも今日・同条件で測り直す**のが対照の要件 |

### 3.5 ★測定の作法（過去に事故ったもの）

- **bond の «成功» は古い bond の再利用でも起こる**（memory `c3-ble-d2d-gatt-notify-sm`）
  ⇒ **各試行の前に central 側で `remove`（forget）してフレッシュに測る**。
- **GATT キャッシュ罠**：過去に接続歴があると古いサービス表が使われる ⇒ forget + BT off/on。
- **真cold の証明＝`uhubctl -l 1-6 -p 1 -a off` ＋ by-id の読み戻し 0**（`rst:` は証明にならない）。
- **コンソール（ACM）の open は DUT をリセットする**＝真cold の判定に使わない（マーカ直読み）。
- **マーカは reset を跨いで生き残る**＝**cleared-boot-read**（0 クリア→0 を検証→1回だけ制御ブート→読む）。
- `esptool ... | head -N` 禁止（SIGPIPE が `write-flash` を切る＝evidence-c3-02 §6.3 で実際に踏んだ）。

---

## 4. 残ブロッカー（この時点．実機前）

1. **実機未測定**（本節までは全てビルド段階）。§3 の予測を先に固定した。
2. **真cold での BLE は C3 で一度も検証されていない**（過去の D-1〜D-2d は**全て warm**）。
3. `-DASP3_ESPIDF_SUPPLY=OFF` の**実機**非回帰は未実施（ビルドは通過）。
</content>

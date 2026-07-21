# C3 evidence-03 — BT 供給移行（**hal 参照 0 達成**）／実機ラウンドへの**事前登録予測**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `<MAC-19>`**（hub `1-6` **port1**）
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

---

## 5. ★★実機結果（依頼 (c)(d)）— **予測 P3 は外れ。据置きの根拠は «真のタグ» に対して初めて実証された**

DUT `<MAC-19>`（hub 1-6 port1）。**全 run とも真cold**
（`uhubctl -l 1-6 -p 1 -a off` → **by-id から DUT が消えることを毎回確認**）。
central＝ホストの `hci0`（`<MAC-33>`）。**各試行の前に `bluetoothctl remove` でフレッシュ化**。

### 5.1 予測の答合せ（依頼 (d)）

| # | 予測 | 確度 | 実機 | 判定 |
|---|---|---|---|---|
| **P1** | D-1 到達 | 85% | **SYNC=`0x5ade51c0`**（NimBLE sync 到達） | **★的中** |
| **P2** | D-2b（adv rc=0）＋ hci0 検出 | 75% | **ADV=`0x0ade5000`・advRC=`0xad000000`（rc=0）**・**hci0 で `ASP3-C3-BLE` を検出（RSSI −51〜−58・MAC 一致）** | **★的中** |
| **P3** | **bond 成立（`AuthenticationTimeout` 非再現）** | **55%** | **失敗 2/2**（`org.bluez.Error.AuthenticationCanceled`・`Paired: no`） | **★外れ** |

★**P1/P2 は C3 BLE が «真cold» で成立した初の実測**（過去の D-1〜D-2d は全て warm）。

### 5.2 ★★決定的対照の結果（§3.3 の表をそのまま適用）

**同一 DUT・同一 central・同一手順・全て真cold**：

| アーム | 供給 | bond 結果 |
|---|---|---|
| **A** | **esp-idf（真の v5.5.4 タグ．`libbtdm_app.a`=`859e8c8e`）** | **失敗 2/2**（`AuthenticationCanceled`） |
| **B（対照）** | **hal** | **成功 2/2**（`Pairing successful`・`Bonded: yes`・`Paired: yes`・`ServicesResolved: yes`・UUID `0000abf0`＝独自GATT可視） |

⇒ §3.3 の事前登録表の **「A 失敗・B 成功 ⇒ 供給に帰属」**に該当。
**B が同条件で成功している**ので **central／ハーネス／GATTキャッシュ側の問題ではない**
（＝**この対照が無ければ «v5.5.4 のせい» と誤断する余地が残っていた／逆に «私の環境のせい» と
逃げる余地も残っていた。対照が両方を閉じた**）。

**∴ HANDOFF §4-4 の「C3 BT を v5.5.4 にしない」という判断は «結果として正しかった»。
ただし本ラウンド以前，その根拠は «+1169(≡v6.1) を v5.5.4 と誤認した測定» であり
（evidence-c3-01 §4.1），**真のタグに対する実証は今回が初めて**である。**

### 5.3 ★私が途中で犯した誤り（自己修正の記録）

1. **«adv 非到達» の誤断**：cold ブート後に**マーカを先に読んだ**ため
   （`esptool --before usb-reset` が **DUT を download mode へ落とす**）、
   その後の hci0 スキャンで `ASP3-C3-BLE` が見えず「不可視」と読みかけた。
   **順序を «cold → スキャン → 最後にマーカ読み» に変えたら RSSI −51 で見えた**
   ＝**「採取が DUT を止める」罠**（memory の既知事項）を自分で踏んだ。
2. **★«connect event がホストに届いていない» の過剰主張**：A run#1 の `CONN=0x00000000`
   だけを見て「A は接続イベントを登録できていない」と書いたが、**A run#2 は
   `CONN=0x604e0001`（B と同値）**で**再現しなかった**。
   ⇒ **単一 run から機序の物語を作りかけた**＝撤回。**`CONN` マーカは A で不安定**であり、
   **機序の主張には使えない**。**帰属に使えるのは «bond 結果 2/2 vs 2/2» の方だけ**。
   （PAIR マーカは A では 2 run とも書かれず＝割込みトレース値のまま＝**A は SM 完了に至らない**
   という点だけは一貫しているが、**これも «結果» であって «機序» ではない**。）

### 5.4 ★含意の自問（§3.4 の答合せ）

- (A)「bond 成功 ⇒ v5.5.4 健全」は**そもそも不健全**と宣言していた ⇒ **今回は成功しなかったので出番なし**。
- (B)「bond 失敗 ⇒ 据置き正当」を**単独では不健全**と宣言し、**B アームを要件にした**
  ⇒ **その B が成功したので、今回は帰属が成立する**。**宣言どおりの手順で結論が出た**。
- (C)「D-1 が通る ⇒ bond も通る」は不健全と宣言 ⇒ **実際に D-1/D-2b は通り bond は落ちた＝宣言が正しかった**。
- (D)「hal の過去実績（warm）⇒ 今日も通る」は不健全と宣言し **B を今日測り直した**
  ⇒ **hal は真cold でも bond 成功**＝**C3 BLE bond の真cold 実証は今回が初**（副次的な新発見）。

### 5.5 ★既定の決定（依頼 (a) への最終回答）

**「hal 参照 0 を達成できたか」と「それを既定にしてよいか」は別問題**：

| build | 供給 | `.d` hal | link hal | 実機 |
|---|---|---|---|---|
| `-DESP32C3_BT=ON`（**既定**） | hal | 890 / 5690 | 62 / 158 | **bond 成功 2/2**（真cold） |
| `-DESP32C3_BT=ON -DASP3_ESPIDF_SUPPLY=ON` | **esp-idf** | **0** | **0** | **adv/D-2b は真cold で成立・bond 失敗 2/2** |
| WiFi（`wifi_scan`）／素（`sample1`）＝**既定** | **esp-idf** | **0** | **0** | evidence-c3-02 で真cold 実証済 |

⇒ **BT の既定は hal に戻した**（＝**実機で bond が通る唯一の構成**）。
根拠は**当初のビルド上の理由ではなく，本ラウンドの実機 A/B**（＝根拠が入れ替わった）。
**`-DASP3_ESPIDF_SUPPLY=ON` で hal 参照 0 の BT/BLE ビルドは «選べる»**（adv までは真cold で動く）
＝**移行の成果は残しつつ，既定は実測に従う**。

---

## 6. 残ブロッカー（実機後・依頼 (f)）

1. **★esp-idf（真の v5.5.4）供給での bond 失敗の «機序» は未特定**。
   本ラウンドが確定したのは**帰属（供給に起因）**までで、**なぜ落ちるかは未解明**
   （§5.3-2 のとおり `CONN` マーカは不安定で機序の根拠に使えない）。
   次の一手の候補（**予測を先に書く前提で**）：
   - **VHCI 層のトレース**（HCI イベントを RTC マーカ or IRAM リングへ）で
     **LE Connection Complete / SM パケットがどこで落ちるか**を A/B 比較する。
   - **`libbtdm_app.a` 単体差替え**（bt.c/porting は hal のまま blob だけ v5.5.4）＝
     **blob 起因かグルー起因かを分離する «3アーム目»**。★ただし §2.2 のとおり
     **それは «混成» そのもの**なので、**ABI 不整合と機能差を区別できない**点に注意
     （＝安易にやると帰属を誤る。C6 evidence-c6-05 の教訓と同型）。
2. **`AuthenticationCanceled` ≠ 記録の `AuthenticationTimeout`**＝**失敗モードが違う**。
   旧記録は +1169(≡v6.1) に対するものなので**別現象の可能性**がある（同一視しない）。
3. **BLE の D-2c/D-2d（独自GATT read/notify/write/暗号read）の真cold 実証は未実施**。
   本ラウンドで真cold 実証したのは **hal の bond+`ServicesResolved`+UUID 可視**まで。
4. `-DASP3_ESPIDF_SUPPLY=ON`（BT）の**adv 以降**（connect/GATT）は落ちるため、
   **この構成は «bond 不要な用途» 限定**。
5. WiFi 側の残（evidence-c3-01 §9・evidence-c3-02 §7）は不変。
</content>

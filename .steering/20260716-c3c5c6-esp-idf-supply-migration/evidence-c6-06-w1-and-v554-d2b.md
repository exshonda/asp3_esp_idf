# C6 evidence-06 — **W1（GOT IP+ping）を真cold で／v554 の D-2b／board C 誤認の訂正**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration` ／ 前段 commit: `bf8e92d`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`**（hub `1-6` port2）＝**★board C 本体**（§2）

---

## 1. 目的

1. **A：C6 の W1（GOT IP + ping）を «真cold» で通す**＝**ミッションの完了条件**。
2. **B：v554（submodule 供給）で D-2b**＝**既定 flip の判断材料**（`evidence-c6-05` の申し送り）。
3. **C：§10-12 の hal ハングが «同じボードで» 消えた理由（tree 変化）の候補特定**。

---

## 2. ★★訂正：**board C ＝ 本DUT そのもの**／**rev v0.3 は存在しない**

### 2.1 一次情報を**自分で**確認した（引き継ぎを鵜呑みにしない）

| 主張 | 検算 | 判定 |
|---|---|---|
| **`14:C1:9F:E0:5A:9C` ＝ board C** | `docs/wifi-shim-c6.md` に同 MAC が **8回**。加えて `docs/ble-c5c6.md`（「ESP32-C6 board C（`14:C1:9F:E0:5A:9C`）」）・`docs/load-test-c3c5c6.md`（「DUT＝C6 board C（`14:C1:9F:E0:5A:9C`）」）・`docs/c5-toolchain.md`・`docs/blob-unify-v554.md` が**独立に**同じ対応を記録 | **★正しい＝本DUT は board C** |
| 「board C は **rev v0.3**」 | `docs/wifi-shim-c6.md` に **`v0.3` は 0 件**＝**一次情報が存在しない**。esptool 実測＝**`ESP32-C6FH4 (QFN32) (revision v0.2)`** | **★誤り** |

### 2.2 ★誤りの発生源＝**2つの別レジスタの取り違え**（辿った）

`evidence-c6-02:195`：
> board C＝**rev v0.3**（§20 コミットが「本board **efuse blk v0.3>=1**」と明記）

★これは `asp3/target/esp32c6_espidf/bt/bt_pmu_init_c6.c:30` の
「本 board は **efuse blk_version**= v0.3>=1 のため `set_ocode_by_efuse` 経路」を根拠にしている。
**しかし stock の `esp-idf/components/hal/include/hal/efuse_hal.h` は**

```c
uint32_t efuse_hal_chip_revision(void);   /* line 29 ＝チップ・リビジョン */
uint32_t efuse_hal_blk_version(void);     /* line 36 ＝eFuse ブロック版数 */
```

**＝«別API・別レジスタ»**。∴ **「efuse blk_version=v0.3」から「chip rev v0.3」は導けない**。
⇒ **rev v0.2/v0.3 という交絡は «実在しない差» だった**（`evidence-c6-03` がそれを
「不要」と判断したのは**結果的に正しかった**）。

### 2.3 ★★技術的な含意（**話はむしろ良くなる**）

- **§10-12 で hal が synth-lock ハングしたのは «この同じボード»**（board C）。
- **`evidence-c6-05` は «同じボード・同じ warm 条件（`rst:0x15`）» で hal が D-1 到達**と実測。
- ⇒ **差はボードではない**。**個体差・rev 差・「board C 非接続」は全て消える**。
- ⇒ **残る説明は «ツリー（コード）の変化» のみ**＝**`git log` で追える**（§6）。
- ★**教訓**：**「個体差」を交絡候補の筆頭に置いたのは誤りだった**。
  **本物の交絡は cold/warm**（`evidence-c6-04` が突き止めたとおり）。
  **「別個体だから比較できない」は、個体の同定を一次情報で確認してから言うこと。**

---

## 3. ★認証情報の取扱い（★このファイルには**書かない**）

- **ビルド注入のみ**：`-DASP3_EXTRA_COMPILE_DEFS='WIFI_SSID="…";WIFI_PASSWORD="…"'`
  （plumbing 実在を確認：`asp3/asp3_core/CMakeLists.txt:69-70` が
  `ASP3_EXTRA_COMPILE_DEFS` を `ASP3_COMPILE_DEFS` へ append）。
  ★`-DWIFI_SSID=` を cmake へ直接渡しても**効かない**（plumbing 不在）。
- **`apps/wifi_dhcp/wifi_dhcp.c` は `WIFI_SSID` の既定値を持たない**
  （＝未注入ならコンパイルエラー＝「既定値のまま `reason=201`」の罠は wifi_dhcp では起きない）。
- ★**`wifi_dhcp.c:255` は `syslog(LOG_NOTICE, "wifi_dhcp: SSID='%s'", WIFI_SSID)` で SSID を出す**
  ⇒ **本ファイルに貼る物証は SSID をマスクする**。
- ★**パスワードは docs/evidence/commit/memory/ログに一切書かない**
  （既存 `docs/ble-c5c6.md:274` も `WIFI_PASSWORD="<要補完>"` とマスクしている＝先例に従う）。
- **commit 前に `git diff` で混入 0 を確認する**（§5 で報告）。

---

## 4. ★★実機前に固定する予測（★ここから下は実機を1回も触らずに書いた）

### 4.1 予測

| # | 測定 | **予測** | 確度 | 根拠 |
|---|---|---|---|---|
| **R1** | **`wifi_scan` で対象 SSID が実在** | **在る** | **90%** | `evidence-c6-04` の真cold `wifi_scan` が **16 APs** を返し、その先頭が対象 SSID だった（＝実在を既に観測している） |
| **R2** | **W1：真cold で GOT IP** | **PASS** | **70%** | 真cold の PHY/RF は通っている（16-20 AP）。**★70% に留める理由**＝GOT IP は **PHY より上（4-way handshake／DHCP／lwIP）**であり、**C6 の真cold で一度も通ったことがない層**。C5 の W1 実績はあるが C6 ではない |
| **R3** | **W1：真cold で ping 応答** | **PASS**（R2 が通れば） | **60%** | R2 の後段。AP 側/ルータ側の要因もある |
| **R4** | **v554＋NimBLE が «ビルド» できる** | **50%** | — | `esp_bt_idf61.cmake` の NimBLE ブロックは `${IDF}` を見るので v5.5.4 側にも NimBLE は在る。**ただし §14 は「v5.5.4タグでは `bt/porting/mem/os_mempool.c` が存在しない」と実測記録**＝**版差の壁が事前に判っている**（`if(NOT ASP3_BT_IDF_V554)` で除外済み）。**壁は他にも出うる** |
| **R5** | **v554 D-2b（warm）** | **50%/50%** | — | **本ラウンドの問い。事前に賭けない** |
| **R6** | **v554 D-2b（真cold）** | R5 が通れば PASS | 80% | `evidence-c6-04` の cold 修正は供給非依存（`(WIFI OR BT)` ゲート） |

### 4.2 ★含意表 —「反証条件も仮説である」を自問した結果

| | 主張 | 健全か |
|---|---|---|
| **A-1** | 「R2 が PASS ⇒ **C6 の真cold は実用水準**」 | **★健全**（GOT IP は PHY＋MAC＋supplicant＋lwIP の総合） |
| **A-2** | 「R2 が **FAIL** ⇒ **cold 修正が不十分**」 | **★健全でない＝断定禁止**。**対抗仮説**：(i) AP 環境（`reason=201` は AP 側でも出る＝親指示）、(ii) C6 の WiFi 接続（connect 以降）が **warm でも未実証**なら cold のせいではない。⇒ **FAIL したら «warm でも同じか» を必ず測る**（`evidence-c6-04` の「相手と同じ条件か」の教訓） |
| **A-3** | 「R5 が PASS ⇒ **既定を v554 へ flip してよい**」 | **★概ね健全だが条件つき**＝**warm・cold 両方で D-2b 非回帰**を示してから。**v6.1 の D-2c/D-2d 実績を D-2b の実績で上書きしない**（`evidence-c6-05` §6.3 の自分の申し送りを守る） |
| **A-4** | 「R5 が FAIL ⇒ **blob 有罪**」 | **★健全でない**＝`evidence-c6-05` が **hal・v554・v61 の3供給すべてで D-1** を実測済＝**blob は無罪**。D-2b で落ちるなら **NimBLE 層／版差の壁**であって blob の RF 収束ではない。**「D-1 は通るが D-2b は落ちる」は «別レイヤの問題»** |
| **A-5** | 「R4 が FAIL（ビルド不能）⇒ **v5.5.4 統一は不可能**」 | **★健全でない**＝**ビルドの壁は «実装作業» であって «不可能» ではない**。**壁の高さを測って報告する**（親指示「壁が高ければ止めて報告」） |

### 4.3 ★既知のハザード（**踏まないために事前に書く**）

★**`apps/ble_host_smoke_c6.c` は LP_AON を STORE0-9 まで «全部» 使う**が、C6 では

| store | app の用途 | **C6 での正体** |
|---|---|---|
| `STORE1`(`0x600B1004`) | 「handoff 到達 stage（**正規未使用**）」 | **★`RTC_SLOW_CLK_CAL_REG`**（`esp_rom/esp32c6/rom/rtc.h:59`）＝**「正規未使用」というコメントが誤り** |
| `STORE4`(`0x600B1010`) | 割込みレート CPU線1 ミラー | **★`RTC_XTAL_FREQ_REG`**（同 `:62`）＝**`rtc_clk_xtal_freq_get()` が読む本物** |

★**`evidence-c6-04` の cold 修正は `rtc_clk_cpu_freq_mhz_to_config()`→`rtc_clk_xtal_freq_get()`
＝`STORE4` を «読む»**。**ただし読むのは `software_init_hook`（＝アプリ起動前）**なので、
**アプリが後から `STORE4` を潰しても起動時の判断には影響しない**（＝§14 が v6.1 で D-2b を
達成できた理由と整合）。**が、起動後に xtal を読み直す経路があれば壊れる**＝**潜在ハザード**。
⇒ **本ラウンドでは «踏まない»**：**D-2b の判定に `STORE4`/`STORE1` を使わない**
（判定＝`STORE0`(sync)・`STORE2`(adv)・`STORE3`(adv rc)・`STORE7`(intr trace)）。
**D-2b が落ちたらこのハザードを第一容疑にする**（が、**先に «warm でも落ちるか» を測る**）。

### 4.4 測定の作法

- **真cold の証明＝2重**：`uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 読み戻し 0**、
  **かつ** センチネル `0xCAFE5A9C`→**`0x00000000`**。
- **BT の判定は LP_AON 直読み（console 非依存）**。**cold で console が要る W1 のみ**、
  `evidence-c6-04` で確立した **coldcap**（電源ON→ポーリングで最速 open→保持）を使う
  ＝**「状態が始まる前に1回だけ open して保持」を満たす**（途中 open をしない）。
- **`grep -a` 必須**。**`rst:` を毎回記録**。
- **`STORE0=0` を «ハング» と読む前にトレース有効を確認**。

---

## 5. 実機測定（2026-07-17）

（実機実施後に記入）

---

## 6. §10-12 の hal ハングが «同じボードで» 消えた理由（tree 変化）

（調査後に記入）

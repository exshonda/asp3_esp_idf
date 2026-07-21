# C6 evidence-06 — **W1（GOT IP+ping）を真cold で／v554 の D-2b／board C 誤認の訂正**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration` ／ 前段 commit: `bf8e92d`
DUT: **ESP32-C6 `<MAC-03>`**（hub `1-6` port2）＝**★board C 本体**（§2）

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
| **`<MAC-03>` ＝ board C** | `docs/wifi-shim-c6.md` に同 MAC が **8回**。加えて `docs/ble-c5c6.md`（「ESP32-C6 board C（`<MAC-03>`）」）・`docs/load-test-c3c5c6.md`（「DUT＝C6 board C（`<MAC-03>`）」）・`docs/c5-toolchain.md`・`docs/blob-unify-v554.md` が**独立に**同じ対応を記録 | **★正しい＝本DUT は board C** |
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

### 5.0 結論（先に3行）

1. **★A：W1（GOT IP + ping）が «真cold» で通った**＝**ミッションの完了条件を達成**
   （`IP acquired: 192.168.1.69`・**ping 36/36 OK・失敗0・切断0**）。
2. **★B：v554 は D-2b を «warm・真cold の両方» で達成**し、**SM=ON（§15 と同じ D-2c/D-2d 構成）でも
   device-side D-2a/D-2b 非回帰**＝**v6.1 の実機実績（warm のみ）を全レベルで «追い越した»**
   ⇒ **既定を `ASP3_BT_IDF_V554=ON` へ flip**（**外部 v6.1 tree 参照が 0 になった**・OFF で可逆）。
3. **★C：§10-12 の hal ハングは «tree 変化» でも説明できない**（hal 経路のコードも hal submodule
   ポインタも §10-12 と**同一**＝実測）＝**未解決のまま候補列挙に留める**（§6）。

### 5.1 ★A：W1（GOT IP + ping）＠**真cold**（依頼 (a)）

**真cold の証明**：by-id 読み戻し **0** ＋ センチネル `0xCAFE5A9C`→**`0x00000000`**。
**採取＝coldcap**（電源ON→ポーリング最速 open→45s 保持）＝**途中 open をしない**。

| 項目 | 実測 |
|---|---|
| **GOT IP** | **`wifi_dhcp: IP acquired: 192.168.1.69`** |
| **ping** | **`net: ping gateway -> OK` × 36**／**失敗・timeout 0** |
| 切断 | **`reason=` 0 件**（＝切断なし） |
| DHCP | `net: DHCP bound` |
| capture | 1252 byte（`evidence-c6-04` の cold `wifi_scan` は 601 byte／修正前は **0 byte**） |

⇒ **C6 の WiFi は真cold で «PHY → MAC → supplicant(WPA2 4-way) → DHCP → lwIP → ICMP» まで
一気通貫で動く**。**★これは `evidence-c6-04` の cold 修正が «PHY だけでなく上位層まで» 効いて
いることの実証**（`wifi_scan` は PHY/RX までしか示さない）。

★**SSID/パスワードは本ファイルに書かない**（§3）。ログの `wifi_dhcp: SSID='…'` はマスク。
**パスワードのログ出現＝0 件**（`grep -ac` で確認）。

### 5.2 ★B：v554 の D-2b／SM=ON（依頼 (b)）

**判定＝`STORE0`(sync)・`STORE2`(adv)・`STORE3`(adv rc)・`STORE7`(intr trace)**。
★**`STORE4`(=`RTC_XTAL_FREQ_REG`)・`STORE1`(=`RTC_SLOW_CLK_CAL_REG`) は判定に使っていない**（§4.3）。
★**センチネルは «判定対象の `STORE0` そのもの» に置いた**（`ble_host_smoke_c6` は STORE0-9 を
**全部**使うので空きが無い）＝**自己検証**：`STORE0==0xCAFE5A9C` なら **POR が起きていない＝
その run は無効**として弾く設計（全 run で発生せず＝全て有効）。

| # | arm | 供給 | **warm** | **真cold** |
|---|---|---|---|---|
| 1 | **v554**（`ble_host_smoke_c6`） | **submodule v5.5.4** | **sync `0x5ade51c0` / adv `0x0ade5000` / rc `0xad000000` / intr `0xa1020704`＝D-2b** | **同左＝D-2b** |
| 2 | **v61**（対照・同一セッション） | 外部 v6.1 | **D-2b** | **D-2b** |
| 3 | **v554 ＋ `SM=ON`**（§15 と同一構成） | submodule v5.5.4 | **D-2a/D-2b 非回帰** | **D-2a/D-2b 非回帰** |
| 4 | **★既定ビルド（フラグ無し＝flip 後）** | submodule v5.5.4 | **D-2b** | **D-2b** |

**SM=ON の tripwire（§15 と同一）**：`ble_sm_pair_initiate`=1・`ble_store_config_init`=1・
`tc_aes_encrypt`=1・`uECC_*`=14 ＝**v554/v61 で完全一致**。

### 5.3 ★★既定 flip の判断（依頼 (b)）＝**flip した**

**実機実績の比較（すべて同一ボード＝board C）**：

| level | **v6.1**（§13-15） | **v554**（本ラウンド） |
|---|---|---|
| D-1 | warm | **warm ＋ ★真cold**（`evidence-c6-05`） |
| D-2b（sync→adv rc=0） | warm | **warm ＋ ★真cold** |
| SM=ON build ＋ device-side D-2a/D-2b | warm | **warm ＋ ★真cold** |
| **D-2c/D-2d OTA** | **未実施**（memory「残 D-2c/D-2d は OTA」） | **未実施**（＝同条件） |

⇒ **v554 は全レベルで v6.1 «以上»**（v6.1 側に真cold の実績が無い）
＝**自分の申し送り「D-1 の実績で D-2b の実績を上書きしない」（`evidence-c6-05` §6.3）に抵触しない**
⇒ **既定 `ASP3_BT_IDF_V554=ON` へ flip した**。

**flip の実測効果**：

| build | **外部 v6.1 tree 参照** | submodule BT 参照 |
|---|---|---|
| **既定（flip 後）** | **★0**（＝provenance の罠が既定で外れた） | 264 |
| `-DASP3_BT_IDF_V554=OFF` | 311 | — |

⇒ **既定で «外部の非 submodule tree（`$HOME/tools/esp-idf-v6.1`）» に一切触らなくなった**
＝`evidence-c6-01 §6` の据置き撤回の**実装**。**OFF で完全に戻せる＝可逆**。

### 5.4 ★v554 で踏んだ «版差の壁» は 2件・いずれも低かった（依頼 (b)）

| # | 壁 | 実測した原因 | 修正（既存パターン踏襲） |
|---|---|---|---|
| 1 | cmake `Cannot find source file: …/port/src/esp_nimble_mem.c` | **v6.1** は `esp_nimble_mem.h` が `void *nimble_mem_malloc(…)` を**関数宣言**し実体は同名 `.c`（339行）。**v5.5.4** は同ヘッダが `#define nimble_platform_mem_malloc bt_osi_mem_malloc` ＝**マクロで `bt_osi_mem_*` へ直接展開**＝**`.c` は存在しない**（`port/src/` は `nvs_port.c` のみ） | `if(NOT ASP3_BT_IDF_V554)` で囲む（**`os_mempool.c` と同じ版差吸収パターン**）。実体供給は v5.5.4 実在の `porting/mem/bt_osi_mem.c` |
| 2 | `fatal error: hci_log/bt_hci_log.h: No such file` | **v5.5.4 の `nimble_port.c:49` が include する**（v6.1 の同ファイルは要求しない＝版差）。実体は `components/bt/common/hci_log/include/` に**両版とも実在** | include path に追加（供給元非依存＝無害）。★`esp_bt_idf61.cmake` の既存注記「もし実機ビルドで `hci_log/bt_hci_log.h` が要求されたら」が**現実化したもの** |

**tripwire（版差が正しく吸収されたことの実証）**：
`esp_nimble_mem.c` のコンパイル数＝**v61:3 / v554:0**（＝v554 は積んでいない）、
`bt_osi_mem_malloc`＝**両アームとも 1**（＝v554 はマクロ経由でここへ着地）。

---

## 6. ★C：§10-12 の hal ハングが «同じボードで» 消えた理由（依頼 (f)）＝**未解決**

### 6.1 ★「tree 変化」説も**実測で支持されなかった**

親の推論「差はボードでなくツリー＝07-15 以降の tree 変化が唯一生き残った説明」を**検算した**：

| 検算 | 実測 | 判定 |
|---|---|---|
| 07-15 以降に **hal 経路（`bt/bt_shim.c`・`esp_bt.cmake`）** を触った commit | **`35c37ac` の1件のみ** | — |
| その `35c37ac` が **`bt_shim.c`** に与えたコード差分 | **0 行**（stat に `bt_shim.c` が出ない＝`esp_bt.cmake` のみ 32+/5-） | **★コード変化なし** |
| `35c37ac` が `esp_bt.cmake` でした «機能的» 変更 | **`option(ESP32C6_BT_IDF61 … OFF)` → `… ON)` ＝既定値だけ** | **★hal 経路の挙動は不変** |
| §11 のクロック2修正（`cba11c6`）は §10-12 当時に入っていたか | **入っていた**（§11＝`cba11c6` が §12 より前） | **★差ではない** |
| **`hal/` submodule ポインタ**（07-15 vs 現在） | **`b90b1837cb5` で完全一致** | **★hal ツリーも不変** |

⇒ **hal 経路のコードも hal submodule も §10-12 と «同一»**。
⇒ **「07-15 以降の tree 変化」では説明できない**（＝親の推論も、私の「個体/rev」も、両方外れ）。
★**私の cold 修正でもない**（`evidence-c6-05` の逆方向対照＝hal は `COLD_CPU_PLL=OFF` でも warm で D-1）。

### 6.2 ★残る候補（**憶測で断定しない**）と、**実機で決める方法**

| # | 候補 | 実機で決める方法（安い順） |
|---|---|---|
| **1** | **§10-12 の観測自体が条件交絡していた**（例：当時の «warm» が実は PCR ごと落ちるリセット＝cold 相当だった。**memory：C6 は `watchdog_reset` 非対応→esptool が RTS へ自動フォールバック**／§13 の留保「**全RTSリセット**」） | **★最有力かつ最安**：**§10-12 当時のリセット経路（RTS/EN）で hal を起動し `STORE5`（clk src）を読む**。`0xbb110280`(XTAL/40) が出れば **«当時の warm» は cold 相当だった**＝§10-12 のハング＝私が直した cold バグ、で全部繋がる。**1 run で決まる** |
| **2** | 当時のビルド構成が今と違う（app／マーカ／トグル） | **`git checkout` で §10-12 期のツリーを取り出して hal をビルド→本ボードで実行**（コード同一の主張の直接検証） |
| **3** | 環境要因（温度・電源） | **候補としては挙げるが、1と2を潰す前に持ち出さない**（rigor 標準「「壊れている」と言う前に安い判別を先に」） |

★**候補1が正しければ話は完全に閉じる**：「§10-12 の hal synth-lock ハング」＝
「`evidence-c6-04` の真cold バグ（ROM が XTAL@40MHz で渡す）」＝**同一現象**。
そして「§13 で v6.1 が直した」ように見えたのは……**v6.1 も真cold ではハングする**
（`evidence-c6-03` #4＝`c6u_v61_after` が真cold `0xb1d00005` 2/2）ので、
**候補1だけでは §13 の成功を説明できない**＝**まだ穴がある**。
⇒ **∴ 断定しない。上の1→2 の順で測るのが次の1ラウンド。**

---

## 7. 結論・申し送り

### 7.1 一言

**W1（GOT IP+ping）が真cold で通った＝ミッションの完了条件を達成。**
**v554 は D-2b/SM=ON まで warm・真cold で v6.1 «以上» の実績を得たので既定を flip
＝外部 v6.1 tree 依存が既定で 0 になった。**
**§10-12 の hal ハングの正体は «個体» でも «rev» でも «tree 変化» でもなく、依然未解決。**

### 7.2 ★認証情報の混入 0（依頼 (e)）

- **注入はビルド時のみ**（`-DASP3_EXTRA_COMPILE_DEFS='WIFI_SSID="…";WIFI_PASSWORD="…"'`）。
  **値はシェル呼出しの中だけに存在し、ファイルへ書いていない**。
- **`build/` は `.gitignore` 済み**（`git check-ignore -q build` で確認）＝`CMakeCache.txt` は追跡外。
- **`git diff` / `git status` で作業ツリーへの混入 0 を commit 前に確認**（両タスクとも）。
- **本 evidence にも memory にも SSID/パスワードを書いていない**（ログ引用時は SSID をマスク）。
- **ログ中のパスワード出現＝0 件**（`grep -ac` 実測）。

### 7.3 ★残ブロッカー／ユーザー判断事項

1. **§10-12 の hal ハングの正体＝未解決**（§6.2 の候補1→2 で次ラウンド。**ユーザーの物理作業は不要**）。
   ★**「個体差」「rev v0.2/v0.3」「board C 非接続」は全て消えた**（§2）。**「tree 変化」も実測で
   支持されなかった**（§6.1）。**残るのは «当時の観測条件» の再現**。
2. **C6 の D-2c/D-2d は «OTA 未実施»**（v6.1 も v554 も同条件）＝**スマホ central が要る（ユーザー手動）**。
   ★**C3 で実証した GATT キャッシュの罠**に注意（過去に `ASP3-C6-BLE` 接続歴があれば forget→BT OFF/ON）。
3. **「hal 参照 0」は未達**（`evidence-c6-05` §5.6 の壁＝`esp_bt*.cmake` の `ESP_HAL_DIR` 計122箇所＋
   clk/periph API ドリフト）。**BT 供給の submodule 化とは別作業**。
4. ★**`ble_host_smoke_c6.c` の `STORE4`＝`RTC_XTAL_FREQ_REG`／`STORE1`＝`RTC_SLOW_CLK_CAL_REG`
   は潜在バグのまま**（本ラウンドは «判定に使わない» ことで回避しただけ）。
   **`evidence-c6-04` の cold 修正は `software_init_hook`（アプリ起動前）で `STORE4` を読むので
   現状は無害**だが、**起動後に xtal を読み直す経路が増えたら壊れる**。
5. `rst:` の **EN/RTS 経路での `STORE5` 読み**は未実施（§6.2 候補1＝**次ラウンドの本命**）。

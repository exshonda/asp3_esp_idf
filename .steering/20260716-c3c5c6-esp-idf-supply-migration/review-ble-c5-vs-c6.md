# review-ble-c5-vs-c6 — BLE の C5 と C6 の静的比較レビュー

**作成**: 2026-07-17 ／ **担当**: レビュー専任エージェント
**ミッション**: 「Android で結果が割れる」差分を **静的に** 局在させる。**修正はしない**（レビュー）。
**実機・BlueZ hci0 は一切使用していない**（別エージェントが C3 実機作業中のため）。

---

## 0. 前提となる実測（親から供与。本レビューの土台。私は測っていない）

| チップ | 供給 | iPhone | Android | 物証 |
|---|---|---|---|---|
| **C5** | esp-idf（真v5.5.4） | **OK** | **NG** | `ENC=0x5de00007`＝`BLE_HS_ENOTCONN`(7)・2アーム（GCC14/15）で9レジスタ完全一致 |
| **C6** | esp-idf（真v5.5.4） | **OK** | **OK** | `ENC=0x5de00000`＋`PAIR=0x5dc00011`（`our_sec=1 peer_sec=1`）を両端末で |
| **C3** | hal | （NG） | **OK** | `0x5dc00011` |
| **C3** | esp-idf | — | **NG** | `ENC=0x5de0000d`＝`ETIMEOUT`(13) |

⇒ toolchain は両チップで除外済み。「Android 端末が壊れている」「forget 手順が効いていない」も死んでいる。

**C5×Android の病態（`evidence-c5-08 §8.1`＝他エージェントの記録を読んだもの。私の測定ではない）**：

| reg | 値 | 意味 |
|---|---|---|
| `0x600B1020` | `0x604e0001` | **CONNECT status=0（成功）・count=1** |
| `0x600B1024` | `0xd15c1301` | **DISCONNECT reason byte=0x13・count=1**＝`BLE_ERR_REM_USER_CONN_TERM`＝**スマホ側から切断** |
| `0x600B1018` | `0x5de00007` | **ENC_CHANGE status=7（ENOTCONN）** |
| `0x600B1014` | `0` | WRITE(`0xABF3`) 未到達 |

⇒ **接続は成立 → スマホがペアリング要求 → スマホが諦めて切断 → その後 SM proc が ENOTCONN で解決**。
つまり **「デバイスが SMP を時間内に完遂できず、対向が先に降りた」** という形。

---

## 1. 測り方（★すべて «出力» で測った。CMakeCache は使っていない）

`feedback_hardware_investigation_rigor.md` 第7再発（「cache は入力、ninja/ELF が出力。出力だけが証拠」）に従う。

| 指標 | 取得方法 |
|---|---|
| 実効 `-D` マクロ集合 | `build/<dir>/build.ninja` の当該 `.obj` の `DEFINES` 行 |
| 実効 NimBLE config | 上記 `FLAGS`+`DEFINES`+`INCLUDES` で `gcc -dM -E` を実行し `MYNEWT_VAL_*` を抽出 |
| 実効 BLE ロジック | 同上で `gcc -E` → cpp の行マーカーで **nimble ツリー由来の行だけ**に絞って diff |
| controller init 値 | app を `-E` してマクロ展開後の `esp_bt_controller_config_t cfg = {...}` を分解 |
| リンク実体 | `build.ninja` の `asp.elf` リンク行（`.obj` 列・`-L`・`-l`） |
| 機能の有無 | `nm asp.elf` / `nm --undefined-only <app>.obj` / `objdump -dr` |

**比較対象ビルド**（本日 17:02 の既定同型ペア。`ASP3_APPLNAME` で同定）：
- `build/nr_c5_ble` … `ASP3_APPLNAME=ble_host_smoke_c5` / `ASP3_TARGET=esp32c5_espidf`
- `build/nr_c6_ble` … `ASP3_APPLNAME=ble_host_smoke_c6` / `ASP3_TARGET=esp32c6_espidf`
- 両者とも toolchain は **同一**（`esp-14.2.0_20260121`）＝親の「toolchain 除外」と整合。

---

## 2. ★★ NULL（＝機械的に «同一» と確定した。仮説を殺す側の成果）

**これが本レビューの最大の成果**である。以下は「同じはず」ではなく **測って同一**。

| # | 主張 | 指標（数値） |
|---|---|---|
| **N1** | **NimBLE ホストの実効 config は完全同一** | `MYNEWT_VAL_*` **414/414 が byte 一致**（`ble_sm.c`/`ble_hs_pvcy.c`/`ble_hs_resolv.c`/`ble_sm_sc.c`/`ble_gap.c` の5 TU すべてで） |
| **N2** | **★NimBLE ホストの実効コードが完全同一** | cpp 出力を nimble ツリー由来に絞った diff が **13 TU すべて 0 行差**：`ble_sm.c` **6878行一致**／`ble_sm_sc.c` 5258／`ble_sm_alg.c` 5027／`ble_sm_cmd.c` 4595／`ble_hs_pvcy.c` 4820／`ble_gap.c` 8178／`ble_hs_hci.c` 5032／`ble_hs_conn.c` 4954／`ble_store.c` 4991／`ble_att_svr.c` 6824／`ble_l2cap.c` 4875／`ble_hs.c` 5057 |
| **N3** | controller init 値は **チップ定数を除き同一** | 63 vs 64 フィールド。名前差は `.version_num` **1個のみ**、値差は `.cpu_freq_mhz`(240/160)・`.main_xtal_freq`(48/40) **のみ** |
| **N4** | SM/暗号スタックは同一にリンク | `ble_sm_pair_initiate`/`ble_sm_enc_change_rx`/`tc_aes_encrypt`/`ble_store_config_init`/`ble_gap_security_initiate`/`ble_hs_pvcy_set_our_irk`/`ble_sm_sc_init`/`uECC_make_key` が **8/8 とも 1:1** |
| **N5** | **ホストベース privacy は両方 OFF** | `MYNEWT_VAL_BLE_HOST_BASED_PRIVACY=0` 両方／`ble_hs_resolv.c` は**両方ともコード0**（後述 §5-b）／ELF の `ble_hs_resolv_*`=**0/0** ⇒ **RPA 解決はどちらも controller 側が行う＝対称** |
| **N6** | NimBLE ソースは同一パス | 両方 `esp-idf/components/bt/host/nimble/...`（axis 7 の問い＝**同一**） |
| **N7** | 供給はどちらも hal-free | `-L` は両方 `esp-idf/components/...` のみ。hal 参照 0 |
| **N8** | 既定トグルは同値 | `-D` 実測で `TOPPERS_ESP32C{5,6}_BT_SM` **両方あり**＝SM=ON、`ASP3_BT_IDF_V554` 既定 ON 両方 |
| **N9** | bt_shim の関数面は同一 | C5 `bt_shim.c` 26 fn ／ C6 `bt_shim_idf61.c` 26 fn ／ **片側限定 0 個**。どちらも ACL/HCI を触らない（`acl`=0/0, `hci`=0/0） |
| **N10** | esp_shim の関数面はほぼ同一 | 両方 **1586 行**。C5 のみ `esp_shim_svc_perror`（診断・既定OFF）。**pend_ring 救済機構は両方に存在**（18 hit 同数） |

### ★N1/N2 の含意（重い）

**「C5 の NimBLE ホスト側の設定・コードが C6 と違うから Android が落ちる」系の仮説は、すべて死んだ。**
`BLE_SM_*`・`BLE_RPA_TIMEOUT`・`BLE_LL_CFG_FEAT_LL_PRIVACY`・`BLE_HOST_BASED_PRIVACY`・
`BLE_SM_SC`・`BLE_SM_OUR_KEY_DIST` … **414 個すべて一致**。ホスト SM のコードも 6878 行一致。
⇒ 残る容疑は **(a) app／(b) blob／(c) チップ固有 target ファイル／(d) チップ定数** の4つだけ。

---

## 3. ★ 差分の全一覧（機械的に測ったもの）

### 3-A. アプリ層（`apps/ble_host_smoke_c5/…c` vs `…_c6/…c`）

| # | 差分 | C5 | C6 | 測り方 | 分類 |
|---|---|---|---|---|---|
| **D1** | **★保留リング(pend_ring)の周期flush** | **無し（0）** | **有り（100ms周期）** | `nm --undefined-only <app>.obj` ＝ C5:**0** / C6:**2**（`esp_shim_queue_flush_pending`・`esp_shim_sem_flush_pending`）。`objdump -dr` の call 実体も **0 / 2** | **説明しうる①** |
| D2 | 定常ループ周期 | 1s | 100ms（notify/securityは1s） | source | 説明できない（§4） |
| D3 | main_task 入口診断 | 無し | PMU/HANDOFF dump＋STORE1 stage | source | 説明できない |
| D4 | `report_intr_rate()` | **2回呼ぶ（各1s sleep）** | 関数自体が無い | source | 説明できない（host init 前） |
| D5 | 初期化順序 | svc登録 → `ble_store_config_init`+SM cfg | `ble_store_config_init`+SM cfg → svc登録 | source | 説明できない（§4） |
| D6 | ENC_CHANGE ハンドラ | マーカのみ | マーカ＋`sec_state` ログ | source | 説明できない |
| D7 | `PASSKEY_ACTION` case | 無し | 有り（ログのみ） | source | 説明できない（NoIO/JustWorks では来ない） |
| D8 | STORE マップ | STORE6 に ENC/PAIR **共用** | STORE6=ENC / STORE7=PAIR **分離** | source | 説明できない（観測系。ただし §6 の注意） |
| D9 | storm_monitor ミラー | STORE4 のみ | STORE4+STORE5 | source | 説明できない |
| D10 | `ble_hs_cfg.sm_*` の値 | NoIO/bond=1/mitm=0/sc=1/ENC\|ID | **同一** | source | **同一＝差分でない** |

### 3-B. ビルド／マクロ

| # | 差分 | C5 | C6 | 測り方 | 分類 |
|---|---|---|---|---|---|
| D11 | `-march` | **rv32imac**（atomic 有） | **rv32imc**（atomic 無） | build.ninja FLAGS | 説明しうる④（弱） |
| D12 | force-include | 2個 | **4個**（`bt/bt_esp_timer_ext.h`・`freertos/task.h` 追加） | build.ninja FLAGS | 判定不能→§5-a で **実質NULL** |
| D13 | `TRUE=1` / `BT_HCI_LOG_INCLUDED=0` | **定義あり** | **定義なし** | `-D` diff | **説明できない（§5-a で in-situ 測定して NULL 確定）** |
| D14 | `CONFIG_XTAL_FREQ` / CPU freq | 48 / 240 | 未定義 / 160 | `-D` diff・cfg展開 | 説明できない（チップ定数） |
| D15 | `.version_num` | **struct にフィールドが無い** | `= efuse_hal_chip_revision()` | `esp_bt.h` grep（C5:**0 hit**／C6:2 hit）＋cfg展開 | 説明できない（§5-c） |

### 3-C. ターゲット依存ファイル（リンク行＝出力で測定）

| # | 差分 | C5 | C6 | 分類 |
|---|---|---|---|---|
| D16 | cold クロック初期化 | 無し | **`cold_clk_init_c6.c`** | 説明しうる③（弱） |
| D17 | PMU 一式 | 無し | **`pmu_init.c`/`pmu_param.c`/`ocode_init.c`/`bt_pmu_init_c6.c`** | 説明しうる③（弱） |
| D18 | regi2c ROM patch | 無し | **`esp_rom_hp_regi2c_esp32c6.c`** | 説明しうる③（弱） |
| D19 | `lp_timer_hal.c` | 無し | 有り | 判定不能（§6） |
| D20 | bt shim | `bt_shim.c` (532行) | `bt_shim_idf61.c` (513行) | **N9 で実質 NULL** |

### 3-D. blob（★我々が変えられない）

| # | 差分 | C5 | C6 | 測り方 |
|---|---|---|---|---|
| D21 | `libble_app.a` 実体 | md5 `015db3db…` / 1,860,394 B / 155 member / 4310 sym | md5 `75db98e5…` / 1,810,788 B / 155 member / 4118 sym | `md5sum`/`ar t`/`nm` |
| **D22** | **★resolv/IRK シンボル面** | **58個**。C6 に無い：`r_ble_hw_resolv_list_search` | **61個**。C5 に無い：**`r_ble_ll_resolv_change_irk`・`r_ble_ll_resolv_irk_change`・`r_ble_ll_resolv_restore_irk`・`r_ble_ll_resolv_get_index`** | `nm --defined-only` の集合差 | **説明しうる②** |

★**「実質空アーカイブ」チェック済**：155 member・12,898/12,767 defined sym＝どちらも実体あり（`libcore.a` 事故の型ではない）。
★**核心の RPA 機構は両方にある**（`r_ble_ll_resolv_gen_rpa`・`r_ble_ll_resolv_rpa`・`r_ble_ll_resolv_set_peer_rpa`・
`r_ble_ll_adv_rpa_timeout`・`r_ble_ll_get_peer_irk` … 共通 55 個）⇒ **「C5 は RPA 非対応」ではない**。
★**測定の健全性**：この grep は **共通側に 12 個ヒットしている**＝「0 を読んだが測定対象が無いだけ」ではない。

---

## 4. 分類：「Android の非対称を説明しうるか」

### (a) 説明しうる（要検証）＝順位づけ

| 順位 | 仮説 | 変えられるか |
|---|---|---|
| **①** | **D1：C5 app に pend_ring の周期 flush が無い**（SMP PDU が保留リングに滞留 → SM が対向 PDU を待つ → 対向が先に降りる） | **★変えられる（app 2行）** |
| **②** | **D22：C5 blob に IRK change/restore 系 4関数が無い**（Android=RPA・鍵配布に ID を含むため IRK 経路を通る） | **変えられない（blob）** |
| ③ | D16-D18：C6 のみ cold クロック/PMU/regi2c 初期化を持つ | 変えられる（移植）※実装は禁止 |
| ④ | D11：`-march` の atomic 有無 | 変えられる |

### (b) 説明できない（理由つき）

| 差分 | 説明できない理由 |
|---|---|
| **N1/N2 系すべて** | **ホストの config・コードが 414/414・13 TU 全一致**＝ホスト層に差が無い。ここに原因は置けない |
| D13 `TRUE=1` | **§5-a で in-situ 測定＝どの条件式も反転しない**（両方 FALSE）。定義の有無は挙動に出ない |
| D2/D3/D4/D6/D7/D9 | ログ・マーカ・診断であり SMP プロトコル挙動を変えない。D4 は host init 前に完了 |
| D5 初期化順序 | 両方とも `nimble_port_freertos_init`（＝`ble_hs_start`）より前。`ble_store_config_init` は cb を張るだけ、`ble_svc_*_init` は定義をキューへ積むだけで、実登録は `ble_gatts_start`（ホストタスク側）。よって順序は観測不能 |
| D7 `PASSKEY_ACTION` | `sm_io_cap=NO_IO`・`sm_mitm=0`＝Just Works ⇒ 発火しない。かつ C6 側もログのみで挙動を変えない |
| D14/D15 | チップ定数・チップ別 struct（§5-c）。**C5 の `version_num` 未設定は «バグ» ではない**＝フィールド自体が存在しない |
| D10 | SM 設定値は **完全同一** ＝そもそも差分でない |

### (c) 判定不能（何を測れば判定できるか）

| 差分 | 判定に必要な測定 |
|---|---|
| **D22（blob IRK）** | シンボルの «不在» は機能の不在を証明しない（別名・inline 化・別系譜の可能性）。**チップを跨ぐ blob 差し替えは禁止（混成）**。⇒ 代替＝**E3（鍵配布から ID を外す）** で IRK 経路の関与を機能的に問う |
| D19 `lp_timer_hal.c` | C6 のみリンク。BLE sleep を使うか（`.sleep_en=0` 両方＝使わない想定）を ELF の呼出しで確認すれば判定可能。`.sleep_en=0` が両方なので **関与は薄い** |
| D16-D18（cold/PMU） | 両セルとも真cold で adv/接続まで到達し、C5×iPhone は bond まで通る ⇒ 粗いクロック/PHY 障害では «iPhone だけ通る» を説明できない。**C5 に移植して A/B** すれば判定可能だが優先度は低い |
| D11 `-march` | C5 を `rv32imc` で、C6 を `rv32imac` でビルドして A/B。ただし blob は事前ビルド＝ISA は固定なので効くのは我々のコードのみ |

---

## 5. ★ 私が途中で踏みかけた «測定の罠»（自己申告。3件）

本レビュー中に **自分の測定バグを3件作り、3件とも自分で検出して訂正した**。記録に残す。

### 5-a. `-dM` は `#if` が «見た値» ではない（危うく機構を捏造しかけた）

`-dM -E`（TU 終端の最終状態）では C6 は `TRUE=true`・`BT_HCI_LOG_INCLUDED=FALSE` に見えた。
しかし **`#if` はその行で評価される**ので終端状態は無関係。実際、私が書いたプローブ TU では
C6 側で `#if (BT_HCI_LOG_INCLUDED == TRUE)` が **TRUE に評価されて `#error` が発火**した
（＝プローブは本物の `ble_hs_hci.c` と include 順が違い、代表性が無かった）。
**決着＝in-situ 測定**：実際の `ble_hs_hci.c` を各ビルドの実フラグで `-E` し、
`bt_hci_log` への参照を数えた ⇒ **C5:0 / C6:0**。
⇒ **どちらも取り込まない＝`TRUE=1` の非対称は何も反転させない（D13 は NULL）**。
（`BLE_ADV_REPORT_FLOW_CONTROL == TRUE` も同様に両方 FALSE。）
★もし `-dM` だけで結論していたら「C6 だけ HCI ログ有効」という **存在しない差分**を報告していた。

### 5-b. `0` を読んだら測定対象の存在を確かめる（`ble_hs_resolv.c`）

nimble 由来コードの diff で `ble_hs_resolv.c` だけ **「IDENTICAL (0 lines)」** と出た。
0 は «フィルタのバグ» の可能性があるので実 `.obj` を見た ⇒
C5 `defined_syms=0` / **C6 `defined_syms=23`** ＝**非対称に見えた**。
さらに中身を見ると C6 の 23 個は **すべて DWARF ラベル**（`.LASF*`・`.Ldebug_*`・型 `N`）＝**コードではない**
（C6 の object が大きいのは force-include が増やしたデバッグ文字列のため）。
⇒ **両方とも実コード 0＝host privacy は両方 OFF（N5）**。
★`nm --defined-only` は **デバッグシンボルも数える**＝件数だけ見ると非対称に見える。

### 5-c. 自分の `head -60` が «存在しない差分» を作った

controller cfg の比較で両側を `head -60` に切ったため **C6 側が途中で切れ**、
「C6 には `.dl_itvl_phy_sync_en` が無い」という **偽の差分**を出しかけた（実際は C6 も持つ＝`esp_bt.h:239/309`）。
無制限で取り直して訂正 ⇒ 真の差は `.version_num` **1個のみ**。
同様に、`grep -oE '…/bt/[a-z0-9_]+\.c'` が **`bt.cfg` の中の `bt.c` にマッチ**して
「`asp3/target/esp32c5_espidf/bt/bt.c` を compile している」と誤認しかけた（**そんなファイルは存在しない**）。
★**第8再発（単位なき数値）と同型**＝指標と範囲を書かずに数えると嘘になる。

---

## 6. ★ 副次的に見つけた «記録の腐り»（修正はしない・報告のみ）

| 箇所 | 内容 |
|---|---|
| `asp3/target/esp32c5_espidf/esp_bt.cmake:523-524` | 「**C6 の esp_bt.cmake:437-438 と同一の確立済み対処**を適用」と書いてあるが、**C6 の 437-438 は現在 `ESP32C6_BT_PMU_INIT` ブロック**であり、C6 は `TRUE=1` を **どこにも定義していない**（C6 側の言及は `:594-599` の «追加しない» というコメントだけ）。⇒ **C5 のコメントが指す先は既に別物**。機能影響は §5-a のとおり **無い**が、記述は誤り |
| `apps/ble_host_smoke_c5/ble_host_smoke_c5.c:133-134` | 「PAIRING は ENC の «後» に発火するので、成功時は 0x5DC0…」＝**親の指示どおり «逆» が実測で確定済**（`evidence-c5-08 §11`）。**このコメントはまだ直っていない**（＝次に読む人が同じ誤読をする） |
| `apps/ble_host_smoke_c5/ble_host_smoke_c5.c:111, 574, 635, 795, 846` | 「★ビルド未検証」が5箇所残るが、冒頭コメントは「解消済」と述べる＝**自己矛盾**。実際 `nr_c5_ble` はビルド通過 |
| `apps/ble_host_smoke_c5/ble_host_smoke_c5.c:678` | 「STORE11 に 0x5DC0…」＝**STORE11 は C5 に非実在**（実際は STORE6 共用）。定義は正しく `LP_AON_STORE_SM`(=STORE6) を使っているが**コメントだけ旧仕様** |

★**D8（ENC/PAIR 共用）の観測上の含意**：C5 は `0x5DE0`(ENC) が `0x5DC0`(PAIR) を **後勝ちで上書き**するため、
**`0x5DC0` の «不在» は原理的に何も証明しない**（`evidence-c5-08 §8.3` で既に撤回済）。
C6 は STORE6/STORE7 に **分離**しているので両方観測できる。
⇒ **この観測能力の差自体が «C5 の方が見えない» を生んでおり、比較の非対称の一部は «計器の差»**。
（＝挙動差ではないが、**C5 と C6 の «証拠の質» は同等ではない**。）

---

## 7. ★ 順位づけした仮説と «反証可能な実験»

★**共通規則**（本リポジトリの事故に基づく）：
- **C0（計装の非破壊性）を必ず先に**。C0＝**健全なセル（C5×iPhone）が壊れないこと**を確認する。
  `evidence-c3-04` では 20語 map を hot path に入れたことが **bond 失敗そのものを作り**、
  C0 を測らなければ «供給の失敗» と誤読していた。**C5×iPhone は現在 «唯一の健全な陽性対照»**。
- **`--wrap` を使うなら «実際に噛んだか» を逆アセンブルで確認**（`evidence-c3-05`：
  `--wrap=ble_transport_to_ll_acl` は **噛まない**＝`_impl` が正。
  C5 の `ESP32C5_BT_RXTRACE` は既に `ble_transport_to_ll_acl_impl` を使っており **この点は正しい**）。
- **「blob だけ差し替える第3アーム」は禁止**（混成＝帰属を誤る）。**チップ跨ぎの blob 交換も同様に禁止**。
- **単一 run で結論しない**（C5 は run ごとに揺れた前例あり）。**最低 2 run**。

---

### E0（前提の検定）— **★最優先。全 RPA 系仮説の «前提» を測る**

**問い**：**C5×Android で device が実際に受け取る peer アドレスは本当に RPA か？**

- **根拠**：`memory/c5-ble-bond-persistence.md` は「**H1(RPA/IRK) は実測で死亡＝peer は public `8C:1D:96:BA:6D:BD`**」と
  するが、**それは BlueZ 相手の測定**。**Android 相手では未測定**。
  親の指示どおり「死んだ仮説が «別の条件で» 死んだのか」を確かめる必要がある。
- **測るもの**：既存の `TOPPERS_C5_LTK_DIAG`（既定OFF）が
  `CONNECT` 時に `peer_id_addr` / `peer_ota_addr` の `type`・`val[0]`・`val[5]` を syslog＋STORE5 へ出す
  （`ble_host_smoke_c5.c:584-612`）。**新規実装は不要**。
- **判定**：`ota_addr.type==1` かつ `val[5]` の上位2bit が `0b01` ⇒ **RPA**（H1系の前提が生きる）。
  `type==0`（public）⇒ **RPA 系仮説（②）は Android でも死ぬ**。
- **仮説を殺す結果**：`type==0` なら **②（blob IRK）はほぼ消える**。
- **C0**：LTK_DIAG は `store_read_cb` を包む＋syslog を足す。**per-packet ではない**ので `evidence-c3-04` の
  hot-path 事故ほどの危険は無いが、**C0 は必須**＝
  **LTK_DIAG=ON のビルドで C5×iPhone が bond できること**を先に確認する。
  壊れたら **計装が原因**＝この計装で Android を語ってはいけない。
- **予測**：**Android の peer が RPA である＝90%**（Android は既定で LE privacy を使う）。
- **コスト**：ビルド1本＋実機1セル。**最も安い。最初にやる。**

---

### E1（①の «反証先行» 版）— **★fix より先に «滞留が起きているか» を測る**

**問い**：**C5×Android のペアリング中に pend_ring に実際に滞留が起きるか？**

- **根拠**：`apps/ble_host_smoke/ble_host_smoke.c:944-955`（C3）のコメントは、これを **実測確定** と書く：
  > 「ペアリング/鍵配布の SMP PDU（ACLデータ）が E_CTX フォールバックで pend_ring に退避された後、
  > 以後キュー交通が途絶えると exit_critical / queue-op の機会的 flush が走らず滞留し、
  > **NimBLE の SM proc が対向 PDU を待って最終的に 30秒で `BLE_HS_ETIMEOUT` する**
  > （docs/bt-shim.md「D-2d bond診断」で**暗号後の Identity PDU 滞留＝30秒待ちを実測確定**）」
  ＝**これは «安全網» と呼ばれているが、由来は実測された実バグの修正**である。
- **測るもの**：C5 の shim には既に **`shim_que_pend_total`**（保留総数）がある。
  これを `storm_monitor_task`（200ms 周期・既存）から **LP_AON へ «最大値（high-water）» でミラー**するだけ。
  **`--wrap` 不要・hot path に触らない・per-packet コード無し**。
- **判定**：
  - high-water **== 0** ⇒ **①は死ぬ**（滞留が無いのに flush の有無を語れない）。**fix を書く前に殺せる**。
  - high-water **> 0**（特に切断時刻まで非0が残る）⇒ ①が生き、E2 へ進む価値がある。
- **★この実験の価値**：**①を «直す前に» 殺せる**。親の「反証実験を先に」に合致。
- **C0**：この計装込みで **C5×iPhone が bond すること**を先に確認（`storm_monitor_task` は既存タスク＝
  追加は 1 レジスタ書込みのみだが、**C0 を省略しない**）。
- **予測**：**high-water > 0 が観測される＝45%**。
  - 上げない理由：`exit_critical` は **BLE の接続イベントごとに** 走る（クリティカルセクションは常時出入りする）ので、
    «交通が完全に途絶える» 窓は原理的に狭い。**恒久滞留は自明には到達しない**。
  - 下げない理由：C3 で **同じ機構が実測された**（30秒待ちが観測されている）。
- **★正直な限界**：high-water>0 でも «それが SMP PDU だった» ことは示さない（種別を持たない）。
  種別まで要るなら `--wrap` が必要で、そのときは **噛んだかを逆アセンブルで確認**すること。

---

### E2（①の本体）— **C5 app に周期 flush を足す**

- **変更**：`ble_host_smoke_c5.c` の定常ループ先頭に、C3/C6 と **逐語同一**の 2 行
  （`esp_shim_queue_flush_pending(); esp_shim_sem_flush_pending();`）＋`extern` 宣言、
  ループを 100ms 化。**★本レビューでは実装しない（レビュー範囲外）。**
- **根拠**：**C5 は 4 セル中で唯一これを持たない**。しかも **C5 の shim 側には救済機構が «ある»**
  （`esp_shim_queue_flush_pending` は定義済・`exit_critical` から呼ばれている）＝
  **「機構はあるのに、それを定期的に駆動する側だけが app に無い」** という形。
  C5 の shim 自身のコメント（`esp_shim.c:155-160`）も **「★D-2d bond修正：セマフォ側の保留give…も同時に精算する」**
  と書いており、**C5 の shim は bond 修正を知っているのに app が安全網を張っていない**。
- **仮説を殺す結果**：flush 追加後も Android が **`ENC=0x5de00007`（ENOTCONN）＋`DISC=0xd15c13xx`** のままなら **①は死ぬ**。
- **支持する結果**：`PAIR=0x5dc000xx`（`our_sec>=1`）が立ち、ユーザーの端末に登録される。
- **C0（必須）**：**先に C5×iPhone（既知の健全セル）が bond し続けることを確認**。
  iPhone が壊れたら「flush 追加が壊した」＝**Android の結果を供給や機構に帰属してはならない**（`evidence-c3-07` の
  PVCY draft(ii) が BlueZ を壊した型＝**「原理的に無害」という推論は実機で反証された**前例そのもの）。
- **予測：Android が OK に変わる＝35%**
  - 上げない理由（★重要・正直に）：
    1. **C3-idf は flush を «持っていて» Android に失敗する**（ETIMEOUT）⇒ flush は **十分条件ではない**。
    2. **C5×iPhone は flush 無しで成功する**⇒ flush 欠如は **単独では非対称を説明しない**。
       ①が正しいなら「Android の SMP には交通が途絶える窓があり、iPhone には無い」という
       **追加の仮定**が要る。これは **未測定**（E1 が部分的に答える）。
    3. `exit_critical` の機会的 flush が既にある（上記）。
  - 下げない理由：
    1. **app 層で唯一の機能差**（D2-D9 はすべて観測系）。
    2. 機構は C3 で **実測確定**しており、**推測ではない**。
    3. C5 の病態（**対向が pairing 中に降りる**）は「**デバイスが SMP PDU に応答しない**」の症候群に属し、
       滞留仮説の予測と **矛盾しない**。
    4. **ENOTCONN(7) と ETIMEOUT(13) の違いは «どちらが先に諦めたか» で説明がつく**
       （対向が先に切れば ENOTCONN、我々の 30s タイマが先なら ETIMEOUT）＝**同一機構の2つの出方でありうる**。
       ★ただしこれは **仮説であって測定ではない**。物語にしない。

---

### E3（②の機能的代替）— **鍵配布から ID（IRK）を外す**

- **問い**：**IRK 交換経路が C5×Android の失敗に関与するか？**
- **変更（診断専用・出荷構成ではない）**：C5 の
  `ble_hs_cfg.sm_our_key_dist` / `sm_their_key_dist` を `BLE_SM_PAIR_KEY_DIST_ENC` のみにする（`ID` を外す）。
- **なぜこれか**：**blob は変えられない**し、**チップ跨ぎの blob 差し替えは «混成» で禁止**。
  ⇒ D22（C6 のみ `resolv_change_irk`/`restore_irk` を持つ）を **直接**は試せない。
  代わりに **IRK 経路を通らせない**ことで、その経路の関与を機能的に問う。
- **判定**：
  - ENC-only で **Android が通る** ⇒ **IRK 配布経路が関与**（②が強く生きる）。
  - ENC-only でも **同じ `0x5de00007`** ⇒ **②はほぼ死ぬ**（IRK 経路は無関係）。
- **C0**：ENC-only ビルドで **C5×iPhone が bond すること**を先に確認（鍵配布を削ると
  そもそも bond の意味が変わるため、**iPhone 側で «ENC-only でも bond する» ことの確認が前提**）。
- **予測：ENC-only で Android が通る＝25%**。
- **★限界**：ID を外すと **再接続時の RPA 解決ができなくなる**＝**出荷構成にはできない**。
  これは **帰属のための診断**であって修正案ではない。

---

### E4（③④）— 低優先

- ③（cold/PMU 移植）：**両セルとも真cold で adv 到達・C5×iPhone は bond 成功**⇒
  粗いクロック/PHY 障害では «同一ビルドで iPhone だけ通る» を説明できない。**予測：関与 8%**。
  ★ただし C6 自身の歴史（`c6-cold-boot-init-gap.md`＝warm しか見ていなかったので前提が真に見え続けた）は
  「**やらない判断が warm でしか検証されていない**」型の再発を警告している。**否定はしないが後回し**。
- ④（`-march`）：blob は事前ビルドで ISA 固定。効くのは我々のコードのみ。**予測：関与 5%**。

---

## 8. ★ 「我々が変えられるもの／変えられないもの」

| | 対象 | 実行可能性 |
|---|---|---|
| **変えられる** | **D1（app の周期 flush）** | ★**最も安い。2行＋extern。C3/C6 に逐語の前例あり** |
| **変えられる** | D2-D9（app の観測系・ループ周期・STORE 分離） | 容易。**D8 の分離は «証拠の質» を C6 と揃える価値がある**（§6） |
| **変えられる** | D5（初期化順序）・`ble_hs_cfg.sm_*`（E3 の診断） | 容易 |
| **変えられる** | D11（`-march`）・D16-D18（cold/PMU 移植） | 可能だが高コスト・低優先 |
| **変えられない** | **D21/D22（`libble_app.a` の中身・IRK 関数の有無）** | **blob。`esp-idf` submodule も編集禁止（CLAUDE.md 禁則）** |
| **変えられない** | D14/D15（チップ定数・チップ別 struct） | シリコン/ヘッダ由来 |
| **変えられない** | N1/N2（NimBLE ホスト）を «C5 だけ» 変えること | submodule 編集禁止。かつ **同一なので変える理由が無い** |

---

## 9. ★★ 正直な自己評価：局在できたか

### できていないこと

- **「原因」は特定できていない。** 静的比較で言えるのは «差がある» までである。
- **①も②も、C5 の «iPhone OK / Android NG» という非対称を単独では説明しない。**
  - ①（flush 欠如）：**C5×iPhone は flush 無しで成功している**。①が正しいなら
    「Android にだけ交通の途絶窓がある」という **未測定の追加仮定**が要る。
  - ②（blob IRK）：**シンボルの不在は機能の不在を証明しない**。かつ **C5 の RPA 中核機構は存在する**。
- **4セルの表は、どの単一仮説とも整合しない**：

  | セル | flush | 供給 | Android |
  |---|---|---|---|
  | C3 hal | **有** | hal | **OK** |
  | C3 idf | **有** | idf | **NG**（ETIMEOUT・`ltk_req=0`＝controller が暗号を開始しない段で局在済） |
  | C5 idf | **無** | idf | **NG**（ENOTCONN） |
  | C6 idf | **有** | idf | **OK** |

  ⇒ **flush は «有» でも落ちる（C3 idf）**＝**十分条件でない**。
  ⇒ **idf 供給でも通る（C6）**＝**供給だけでも決まらない**。
  ⇒ **`memory` の «チップが効く» という読みと整合的**だが、**「チップの何が」は本レビューでは出せなかった**。
- **C3 idf と C5 の病態は別段**（C3 idf＝`ltk_req=0`＝ENC_CHANGE に到達すらしない／
  C5＝ENC_CHANGE は発火する）。**同一視していない**。

### できたこと（＝仮説空間を大幅に削った）

- **NimBLE ホスト層（config 414/414・コード 13 TU 全一致）を «丸ごと» 容疑から外した。**
  これは「同じはず」ではなく **測って同一**。**RPA/privacy/SM の設定差という筋は完全に死んだ。**
- **controller init 値・SM/暗号スタックのリンク・host privacy の ON/OFF・供給の hal-free 性・
  toolchain・bt_shim の関数面** も同様に NULL。
- ⇒ **残る容疑は 4 本のみ**（app の flush ／ blob ／ C6 固有の cold/PMU ／ `-march`）。
  **そのうち «我々が変えられて» «機構が実測済み» なのは D1 ただ一つ。**
- **差分の «数» は多くない**（実質 4）。この意味では **局在は «部分的に» 進んだ**が、
  **単一原因には絞れていない**。

### 次に測るべき順序（コスト昇順・情報量降順）

1. **E0**（前提：Android の peer は RPA か）— 既存計装・ビルド1本 — **これが `type=0` なら②が消える**
2. **E1**（滞留は実在するか）— 既存カウンタのミラーのみ — **これが 0 なら①が消える**
3. **E2**（flush を足す）— ①の本体
4. **E3**（ENC-only）— ②の機能的代替

★**1・2 はどちらも «仮説を殺せる» 実験であり、修正より先に行うべき**（親の「反証実験を先に」）。

---

## 10. 参照（すべて絶対パス）

- app: `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/apps/ble_host_smoke_c5/ble_host_smoke_c5.c`
- app: `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/apps/ble_host_smoke_c6/ble_host_smoke_c6.c`
- app(C3・flush の «正»): `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/apps/ble_host_smoke/ble_host_smoke.c:944-957`
- shim: `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/asp3/target/esp32c5_espidf/wifi_v8/esp_shim.c:145-165`
- shim: `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/asp3/target/esp32c6_espidf/wifi/esp_shim.c:112-130`
- cmake: `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/asp3/target/esp32c5_espidf/esp_bt.cmake:509-531`（`TRUE=1`／記述の腐り）
- cmake: `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf/asp3/target/esp32c6_espidf/esp_bt.cmake:594-599`
- header: `/home/honda/TOPPERS/asp3_esp_idf/esp-idf/components/bt/include/esp32c5/include/esp_bt.h:239,306`
- header: `/home/honda/TOPPERS/asp3_esp_idf/esp-idf/components/bt/include/esp32c6/include/esp_bt.h:210,288,309`
- evidence(他エージェント・読取のみ): `.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-c5-08-toolchain-idf-alignment.md:508-567`

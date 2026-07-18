# C3 × スマホ BLE «SMP 処理直後のリンク沈黙死» 調査計画（rev2）

2026-07-18 起案 → **同日 rev2 全面改訂**（Codex・fable の二重レビュー＋**btsnoop の自前再解析**により
初版の中心記述が誤りと判明したため）。
正本の既往＝`.steering/20260716-c3c5c6-esp-idf-supply-migration/`。
一次証拠＝同 `snoop/rc_c3_step{2,3}_btsnoop.log`（**揮発していた `/tmp` から退避**）＋`snoop/btsnoop_verify.py`（再解析器）。
規律の正本＝memory `feedback_hardware_investigation_rigor`。

---

## 0. rev2 で何が変わったか（初版の誤りと訂正）

| 初版の記述 | 実測（`snoop/btsnoop_verify.py` で本改訂時に再解析・独立確認済） | 帰結 |
|---|---|---|
| 「活動 **5–6s 後**に沈黙死」 | **phone リンクの suptmo は 5000ms**（初版が使った 420ms は **BlueZ リンク**の値）。⇒ **沈黙開始 = 切断時刻 − 5.0s** | 「5–6s 生きて死ぬ」は**timeout 遅延の誤読**。実際は**SMP 直後（≤1s）に沈黙** |
| H1 筆頭＝**2M PHY 更新**等の LL 手続き誤処理 | **LE Set PHY / PHY Update Complete は 0 件＝2M PHY は一度も発生していない**。観測された手続き（feature exchange・DLE・conn update×2 → 7.5ms→30ms）は**全て status=0 で完了**し、沈黙の **4.8–7.5s «前»** | **H1 は筆頭から降格**（起きていない事象を二分探索する無駄を回避） |
| 死の直前事象＝不明 | **Step2**：phone の **Pairing Request 受信の 57ms 後**に沈黙（＝我々が Pairing Response を返す前に死ぬ）。**Step3**：**LESC フル成功**（PK×2・Confirm・Random×2・DHKey Check×2）→ **暗号化後に我々が鍵配布**（Identity Info/Addr）→ **その 0.77s 後**に沈黙 | **両ケースとも直前は «我々の SMP 処理»** |

**⇒ 中心仮説を差し替える。**

## 1. 目標と成功基準

- **根治**：C3 が Android／iPhone と bond し、その後もリンクを維持（C5/C6 と同等）。
- **最低限**：機構の一意特定＋恒久回避。**先出し可能な部分成果**＝wedge（reset 必須）の解消。
- 判定は**両端点の実測一致**でのみ行う。単側ログ・自己申告を根拠にしない。

## 2. 病態の記述（**実測に基づく・rev2 で訂正**）

- 切断は**能動 terminate でなく supervision timeout(0x08)**。**phone リンク**は `itvl 30ms / lat 0 / suptmo 5000ms`
  （接続直後に `→7.5ms`→`→30ms` の conn update ×2 が status=0 で完了）。
- **沈黙開始 = 切断イベント − 5.0s**。実測：Step2 = 接続+5.6s（Pairing Request 受信 +57ms）、
  Step3 = 接続+8.3s（鍵配布 +0.77s）。**接続からの経過時間は一定でない**＝「時間/イベント数依存の枯渇」は弱い。
- **DISC=0**：切断イベントが device host に配送されず conn=1 を永久固持（**wedge**・reset でのみ復帰）。
  **能動 terminate は 8/8 配送**される。
- 症状の形は「取りこぼし」ではなく**«永久に黙る»**＝**恒久ブロック（デッドロック／ハング）と同型**。
- 3態（初回bond可/再接続不可・pairing 不着・bond 直後切断）は**同一機構と決めつけない**（§3 の軸分割）。

## 3. 仮説（rev2）

- ★**H5（筆頭）＝我々の統合が «SMP 処理経路» で恒久ブロックに陥る。**
  SMP PDU 処理（クリティカルセクション／E_CTX 保留救済／暗号処理）でレース起因のデッドロックが起き、
  controller の LL 給餌が止まる。**発火点が SMP 内で毎回違う**（Step2=Pairing Request 処理中、
  Step3=鍵配布直後）のは**レースと整合**＝**間欠性の説明になる**。
  この仮説なら **DISC=0 も «配送路のバグ» でなく «そもそも生成されない»**（同一故障の帰結）で説明でき、
  別軸を立てる必要がない。
- **H1'（降格・ただし生存）**＝conn update の**値域/instant**、DLE、feature exchange の処理。
  ※2M PHY は除外（発生していない）。※**HCI に写らない `LL_CHANNEL_MAP_IND` は未観測**＝候補に残す
  （`LE Set Host Channel Classification 0x2014` で誘発可能）。
- **H2**＝アンカードリフト／**H3**＝PHY・RF 劣化／**H4**＝資源枯渇（**時間一定でない実測により弱い**）。

## 4. 「再検証しない」の線引き（**rev2 で scope down**・両レビュアの指摘を反映）

| # | 確定として扱う範囲（**これ以上広げない**） | 生きている部分（再検証対象） |
|---|---|---|
| B/C | terminate 8/8 配送・timeout 死限定の DISC=0 | 直接証拠は snoop 2/2。**Step1 型の死は未撮影** |
| D | 層＝我々の統合（stock は同一 blob/board/端末で通る＝stock-01 で実測） | — |
| E | 「我々の host の param **policy** が stock と違う」説**のみ**反証 | ★**phone リンクの実 param 比較（stock 成功 vs 我々 失敗）は未実施＝conn param の «関与» は生きている** |
| F | 「ISR stall が原因」は **BlueZ regime・>420ms 閾値**で不支持 | ★**phone regime で必要な >5000ms の先行停止は未測定**＝**恒久ブロック(H5)は否定されていない** |
| G | 素host 20/20 生存（＝BlueZ 接続は殺さない） | ★**「計装摂動と判明」は言い過ぎ（evidence は «疑い/lead»）**。★**«SC 完遂＋暗号化＋鍵配布» の leg は
スマホ以外で一度も生存試験されていない**＝**H5 を定義で殺していた（初版最大の誤り）** |
| H | C3 の bond は toolchain **esp-14.2.0_20260121** で成立（15.2 は 0/5） | — |

★**未解決の矛盾（要決着）**：`evidence-rc-c3-reconnect-prereg.md` の「BlueZ とは bond しない」と
`evidence-c3-10` の「**esp-idf 供給×GCC14×BlueZ で bond 成功 5/6**」が**同一 hci0 で正反対**。
差分は **build**（clean `rb_B_ble` vs 計装入り `rc_c3_evt`）の疑い＝**計装が BlueZ bond まで壊した可能性**。
これが決着すると **BlueZ をスクリプト可能セントラルとして使える**＝ESP32 セントラル新造が不要になりうる。

## 5. 軸の分割（Codex 指摘・**単一根因と決めつけない**）

| 軸 | 症状 | 個別に定義するもの |
|---|---|---|
| **A1** | Android 初回 pairing 不着（Step2 型） | 陽性対照・成功条件・btsnoop signature |
| **A2** | Android 再接続不可（Step1 型・**snoop 未撮影**） | 同上（まず撮る） |
| **A3** | bond 直後切断（Step3 型）／iPhone post-bond timeout・wedge | 同上 |

**統合してよい条件**＝直前 LL/HCI 事象・timeout 時刻構造・DISC=0/EVT 欠落が一致するとき**のみ**。

## 6. フェーズ（**安い決定実験を先に**・両レビュア一致の再配列）

### P0 — 既存資産の精査（費用ほぼゼロ・**一部は本改訂で実施済**）
- ✅ 一次 snoop を repo へ退避（`snoop/`）＋再解析器を同梱。
- ✅ 病態記述と H1 容疑者を実測で書き換え（§0）。
- ⬜ **A2（再接続不可）の snoop が無い**＝最初に撮る（既存 capture で足りない唯一の軸）。

### P1 — **恒久ブロック(H5)の直接判定**（最安・最決定的）
1. **JTAG post-mortem**：死の直後に halt し、**全タスクの状態・PC・バックトレース**を読む。
   - **観測→結論**：特定タスクがロック/セマフォで永久待ち＝**H5 確定**＋箇所特定（一発）。
     全タスク正常動作で controller だけ黙る＝H5 反証、H1'/H2/H3 へ。
2. **低摂動カウンタ**（Codex 指摘5）：HCI Disconnection Complete 到達数・`ble_mqueue_put` 失敗・
   `esp_shim_queue_send(_from_isr)` 失敗・`shim_que_debt_conflict`・pending high-water。
   - **観測→結論**：HCI disconn 自体が無い＝controller/前段で**生成されていない**（H5 と整合）。
     HCI はあるが GAP DISC が無い＝host/eventq の配送落ち（別軸として復活）。
3. **wedge ワンショット**（fable 指摘6）：死後に (a) 受動スキャンで**再広告の有無**、
   (b) app watchdog から `ble_gap_terminate` → **Disconnection Complete が返るか**。
   - **返る**＝controller 生存・conn 文脈のみ死 ⇒ **watchdog で reset 不要化が即出荷可能**（先出し成果）。
     **返らない**＝controller 全体 wedge ⇒ H5 と同根。

### P2 — **stock 成功 vs 我々 失敗の同一スマホ btsnoop 差分**（両レビュア筆頭）
同一スマホ・同一 C3・連続採取。conn param 実値／conn update の値と instant／DLE／feature／
timeout 時刻／RSSI・再送を時系列で差分。
- **観測→結論**：手続き列も値も同一で我々だけ死ぬ ⇒ **E/H2 は弱まり H5・統合層処理**が濃厚。
  値や instant が違う ⇒ **E/H1' が復活**（＝以降の再現系の入力値をここで固定する）。

### P3 — **BlueZ 階段実験（clean GCC14 build）**（fable 提案・§4 の矛盾決着を兼ねる）
`rb_B_ble` 相当の**計装最小 clean build** で、BlueZ に phone の観測列を段階的に積む：
1. **full bond + リンク 30s 保持 ×N**（★**暗号後にリンクが保つかは、どの build でも未測定**）
2. ＋ phone の param regime（30ms/0/5000ms）＋ `hcitool lecup` で update×2（7.5ms→30ms）
3. ＋ DLE（要 Intel AC-3168 の対応確認）／＋ channel map 更新（`0x2014`）
- **観測→結論**：どこかで死ねば**スマホ不要の再現系が完成**＝二分探索へ直行（ESP32 セントラル不要）。
  全段生存なら「HCI に写る差分では殺せない」が確立し、**その時初めて**次段に投資する根拠ができる。

### P4 —（P1–P3 で決まらない場合のみ）ESP32 セントラル
**位置づけを変更**：**«陽性再現装置» としてのみ使う。再現しないことを H1'/H5 の棄却に使わない**（両レビュア一致）。
着手前に**制御可能性の feasibility 表**（値・順序・遅延・instant・controller 自動手続き）を作る。
※`blecent` から LL PDU の exact order/instant/empty PDU cadence/再送/feature mask は**直接制御できない**。

## 7. 統計と対照（両レビュア指摘・**初版の N≥10 は不十分**）

- **再現の定義を «率の一致» でなく «安定不変量»** に：supervision timeout 0x08 ＋ DISC=0 ＋
  **§2 の時間構造（沈黙 = 切断−suptmo、直前が SMP 事象）**。
- **baseline 発生率を各フェーズで先に測る**。非再現は**信頼区間つき**で扱い、**0/N を棄却に使わない**。
  腕ごとの必要 N は **Fisher で事前計算・事前登録**（Step F は 20/20 vs 7/12＋Fisher でやれていた）。
- **同日・同 binary・同スマホで «我々 baseline / stock 対照 / 再現系» を交互順**に回す。
- **陽性対照は両側**：生存側（正常接続が撮れる）だけでなく**死の検出器の較正**
  （接続中に DUT 電源断→セントラルが 0x08 を正しく記録する実演）。
- **計装摂動を常に疑う**：計装有無 A/B を常備。**P1 と P2/P3 は同一 DUT image** で回す
  （計装 build は BlueZ bond まで変えた疑いがある＝§4 の矛盾）。
- 新規 btsnoop 採取は **snoop ON＝BT 再起動＝観測者効果**（C5 で «治って撮れなくなった» 前科）。
  手続き列の同定は**既存 capture で足りる**ので、新規採取は A2 軸に限定する。

## 8. オフランプ

- **P1-3（wedge ワンショット）は単独で出荷価値**（reset 不要化）。根治が長引く場合の先出し。
- P1 で H5 が確定すれば、P2–P4 の大半は不要（コード側のデッドロック修正に直行）。
- P3 で phone 不要の再現系が得られれば P4 は不要。

# C3 × スマホ BLE «活動後リンク沈黙死» 調査計画（ESP32 セントラル化方針）

2026-07-18 起案。正本の既往＝`.steering/20260716-c3c5c6-esp-idf-supply-migration/`
（`evidence-rc-c3-death-mechanism-CDEF.md`＝Steps C–F／`evidence-c3-05-smp-tx-localization.md`／
`evidence-c5-10-erx-smp-flow.md`＝両側観測の型）。memory＝`ble-android-connect-fails`・
`feedback_hardware_investigation_rigor`（調査規律の正本）。

本書は**残る唯一の機能課題**（C3 だけがスマホと安定して bond/接続維持できない）の調査計画。

---

## 1. 目標と成功基準

- **根治**：C3 が Android／iPhone と bond し、**活動後もリンクを維持**（C5/C6 と同等）。
- **最低限（根治不能でも可）**：«活動 5–6s 後の沈黙死» の**機構を一意特定**し、恒久回避策を出荷。
- 判定は**両端点の実測一致**でのみ行う（自己申告・単側ログを根拠にしない）。

## 2. 確定事項 — **再検証しない**（時間を捨てないための境界）

| # | 確定内容 | 出典 |
|---|---|---|
| A | 病態＝**間欠**。同一条件で3態（初回bond可/再接続不可・pairing不着・bond直後切断） | rc-c3 Step1-3 |
| B | 死に方＝**能動 terminate でなく supervision timeout(0x08) の沈黙死** | Steps C–F |
| C | 安定不変量＝**DISC=0**（切断が device host に配送されず conn=1 固持＝**wedge**・reset でのみ復帰）。ただし**能動 terminate は 8/8 配送**＝構造的欠落ではなく **supervision-timeout 死限定** | Step B/C |
| D | **層＝我々の統合**（stock ESP-IDF は**同一 blob・同一ボード・同一端末**で通る＝blob でない／host は SMP も terminate も正しく PDU を出す＝host ソフトでもない） | Steps C–F |
| E | **«conn param/supervision の扱いが stock と違う» 説＝反証済**（両者 passive・値は central 依存。実測 `itvl=40(50ms) latency=0 suptmo=42(420ms)`） | Step C |
| F | **«shim ISR stall が原因» 説＝不支持**（死点で凍結するが idle でも 11s 前に凍結・保持接続でも凍結・**死の直前に >420ms の先行 stall 無し**）＝**帰結であって原因でない** | Step D |
| G | 素host（SM-ON 出荷 build・BlueZ セントラル）は **20/20 生存**。素host wedge 線は計装摂動（layout 敏感）と判明し**畳んだ** | Steps C–F |
| H | **toolchain 拘束**：C3 の BLE bond は **esp-14.2.0_20260121 必須**（esp-15.2.0 は 0/5）。本調査の全 build はこれで固定 | evidence-c3-10 |
| I | 計器：C3 は **PAIR(0x54)/ENC(0x58) が別レジスタ＝`0x5DC0` マーカー有効**（C5 は共用で不可＝逆） | rc-c3 |

**⇒ 未解明はただ一点**：*我々の駆動下で、**スマホ接続に限り** ~5–6s 後に OTA リンクが維持できなくなる機構*。
（素host が 20/20 生存＝**トリガはスマホ固有の接続特性**であって「SMP を実行したこと」自体ではない〔G〕。）

## 3. 中心仮説（最有力・反証可能な形で）

> **H1：スマホが接続直後に投げる LL 制御手続き**（`LL_PHY_REQ`＝2M PHY 更新／`LL_LENGTH_REQ`＝DLE／
> `LL_CONNECTION_UPDATE_REQ`／`LL_FEATURE_REQ`）**のいずれかを我々の統合が誤処理し、その後リンク維持が壊れる。**

根拠（相関であって因果でない点は自覚）：BlueZ 素host は 20/20 生存〔G〕＝BlueZ はこれらを投げない／投げ方が違う。
一方スマホは接続直後に定型で投げる。層は我々の統合〔D〕で、ISR stall は原因でない〔F〕＝
**「イベントを取りこぼす」型ではなく「LL 状態遷移で壊れる」型**を示唆。

対立仮説（同時に検定する）：
- **H2**：接続アンカーのドリフト（我々の controller 給餌クロック起因）で受信窓を外す。
- **H3**：PHY/RF の劣化（stock が行う周期処理を我々が欠く）。
- **H4**：時間経過/イベント数依存の資源枯渇（~100 接続イベント＝5s 相当で顕在化）。

## 4. 方法の核 — **スマホを «スクリプト可能な ESP32 セントラル» に置き換える**

**問題**：スマホは*制御不能*かつ*間欠*＝二分探索ができない。
**解**：スマホの LL 手続き列を btsnoop で採取し、**ESP32（C6 or C5）をセントラルにして逐語再現**する。
死が再現すれば、**手続きを1つずつ外す二分探索で «殺す1手続き» を一意化**できる。

実現可能性（実測確認済）：
- `esp-idf/examples/bluetooth/nimble/blecent`＝セントラル雛形。
- LL 手続き制御 API が揃っている：`ble_gap_set_prefered_le_phy()`（PHY 更新）／`ble_gap_set_data_len()`（DLE）／
  `ble_gap_update_params()`（conn 更新）／`ble_gap_read_le_phy()`。
- `esp-idf/examples/bluetooth/hci/controller_hci_uart_*`＝ESP32 を **PC(BlueZ) 直結 HCI コントローラ**化も可能
  （`btattach`＋`btmon` でセントラル側 HCI を全採取）。
- 機材：hub1-6 に **C3(DUT)/C6/C5 が同時在席**＝**C3 を DUT、C6 をセントラル**に割当可能（hub1-5 は別プロジェクト＝不触）。

**受動 sniffer の限界は正直に**：ESP32 に**接続追従型 LL sniffer の公式サポートは無い**
（adv/`CONNECT_IND` の捕捉までは可能）。よって «空中の真実» は
**「両端点とも我々の制御下に置く」**ことで代替する（DUT 側マーカー＋セントラル側 HCI/LL ログ）。
将来 nRF52 等が入手できれば H2（アンカードリフト）の直接観測に使う。

## 5. フェーズ

### Phase 0 — 土台（間欠性を扱える形にする）
0-1. **スマホの LL 手続き列を採取**：Android 開発者オプションで btsnoop 有効化 →`adb pull`→ Wireshark で
     接続直後〜死までの **LL 制御 PDU 列と時刻**を書き出す（iPhone は PacketLogger／可能なら後回し）。
0-2. **C3 DUT 側計装**：`0x5DC0` マーカー〔I〕＋ GAP/SMP/LL イベント trace＋接続イベント計数。
     **計装は layout 敏感〔G〕**なので、**出荷 build に対する差分を最小化**し、計装有無の A/B を必ず取る。
0-3. **ESP32 セントラル雛形**を C6 に構築（`blecent` ベース、LL 手続きを個別に on/off できる config）。
0-4. **陽性対照**：セントラル雛形が «素の接続» で C3 と 20/20 生存すること（＝BlueZ 素host と同値）を先に実演。
     *これが出せない限り以降の «死んだ» は計器不良と区別できない。*

### Phase 1 — **死の再現をスマホ無しで**（本計画の勝負所）
1-1. 0-1 で採取した**スマホの手続き列を ESP32 セントラルで逐語再現**（同 conn param・同順序・同時刻差）。
1-2. **N≥10 試行**で 3態〔A〕の発生率を測り、**スマホでの発生率と一致するか**を突合。
     - **再現した** → Phase 2 へ（決定的に前進：制御可能な再現系を得た）。
     - **再現しない** → スマホ固有要因は LL 手続き列«以外»（RF 特性・タイミング粒度・端末実装）
       ⇒ H1 を棄却し H2/H3 へ軸足を移す（**この «再現しない» も等価に価値のある結果**）。

### Phase 2 — **殺す1手続きの一意化**（二分探索）
2-1. 再現系から手続きを**1つずつ除去/追加**（2M PHY／DLE／conn update／feature exchange／MTU）して A/B。
2-2. **反証を先に**：容疑手続きを**無効化して死が消える**ことと、**単独で加えて死が出る**ことの**両方**を要求。
     片方だけなら «相関» として保留する（過去に相関を因果と誤読した轍〔F〕を踏まない）。
2-3. 一意化できたら **stock vs 我々**で同手続きを実行し、**stock は生きて我々は死ぬ**ことを確認
     （〔D〕の再確認＝差分が確かにその手続きの処理にあること）。

### Phase 3 — 機構の特定と修正
3-1. 特定手続きの処理経路（我々の shim／HCI 経路／controller 給餌）を trace し、**stock との実処理差**を出す。
3-2. 修正 → **Phase 1 の再現系で消えること**を N 試行で確認 → **スマホで最終確認**（Android／iPhone）。

### Phase 4 — 独立軸：DISC=0（wedge）の解消
supervision-timeout 死の**切断イベントが host に配送されない**〔C〕件。根治後も残るなら独立に扱う。
能動 terminate は配送される＝経路は生きている ⇒ **timeout 起因の切断だけが落ちる条件**を特定。
（**単独でも UX 改善**＝reset 不要化。Phase 1–3 が長引く場合の先出し候補。）

## 6. 厳格性ガード（[[feedback_hardware_investigation_rigor]]）

- **陽性対照を先に**（0-4）。計器が «差を出せる» ことの実演なき「差が無い」は無価値。
- **間欠は率で語る**（N≥10）。単発を一般化しない。
- **反証優先**（2-2）：消す実験と足す実験の両方。
- **相関を因果と読まない**（〔F〕で実際に踏んだ）。
- **計装の摂動を疑う**（〔G〕：layout 敏感で素host wedge が出た前科）＝計装有無 A/B を常備。
- **未検証の判別指標に乗らない**（`ENC status=7` は指紋でない、等）。
- 事実（実測値）と解釈を分けて記録。各フェーズ末に `.steering/` へ evidence を追記。

## 7. 想定コストとオフランプ

- Phase 0–1 が最大の投資（ESP32 セントラル構築＋btsnoop 解析）。**Phase 1 で再現しなければ H1 は棄却**され、
  計画は H2/H3 へ組み替え（＝**早期に分岐が決まる設計**）。
- 根治が長期化する場合の**先出し価値**：Phase 4（wedge 解消＝reset 不要化）は独立に出荷可能。

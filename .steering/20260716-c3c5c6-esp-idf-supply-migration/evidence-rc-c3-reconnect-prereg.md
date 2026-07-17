# evidence-rc-c3 — C5-10 «両側観測» を C3 «Android 再接続不可» 軸へ転写（Step 0 + 事前登録 + Step 1 準備）

**作成**: 2026-07-18 ／ **担当**: rc-c3 エージェント（PLAN Phase 2）
**修正コードは書かない（計器のみ）。** 他エージェントの evidence/review/PLAN は読むだけ。
DUT: **ESP32-C3 `60:55:F9:57:BA:BC`**（hub 1-6 `-p 1`）。

---

## 0. ★最重要の枠組み（測定前に固定）

- C5-10 の結末＝**snoop 有効化を伴う BT 再起動を境に C5×Android bond 失敗が «消えた»**（成功 4/4・機序未決）。
  **同じ Android 端末（Fold 7・`RFGL436H8LZ`・adb=device）を C3 でも使う。**
- ⇒ **«C3 の Android 再接続不可も既に消えている可能性» を «最初に» 安く測る**（re-baseline）。
- ★**C3 の病態は C5 と «別»**：C5=bond 未到達（`ENOTCONN`）。**C3=bond «成立» するが «再接続» できない**
  （evidence-c3-09/stock-01：切断は届く・CONN 増える・切るのはスマホ側）。**機序を混ぜない。**

## 1. Step 0 — ★bond するビルドの «実測» 確定（«載っている» ≠ «bond する»）

| 事実 | 実測 |
|---|---|
| **rb_B_ble** = esp-14.2.0_20260121（GCC14=IDF標準）・esp-idf供給・SM=ON | ★**Android とは bond «する»**（evidence-c3-09：`PAIR=0x5dc00011`）。**BlueZ とは bond «しない»**（下記 §4） |
| **rb_A_ble** = esp-15.2.0（GCC15） | bond «失敗» アーム（evidence-c5-08） |
| ★**BlueZ×esp-idf の実測（本ラウンド §4）** | **`AuthenticationCanceled`＝bond 失敗**（evidence-c3-03/04 の «esp-idf 供給=BlueZ bond 失敗 2/2» を再現） |

⇒ ★★**この esp-idf ビルドの «健全な bond/再接続» を出せる central は «Android だけ»**
（BlueZ=hal 供給でしか bond しない）。**⇒ 再接続軸の陽性対照（C0）は «Android の re-baseline» と同一イベントになる。**

## 2. Step 0 — 計器（既存 evt_trace を再接続軸へ向ける・逆asm検証済）

**ビルド**: `build/rc_c3_evt`（現リポジトリ ASP3CORE からフレッシュ・焼き直しの罠回避）
= GCC14（esp-14.2.0_20260121）・**esp-idf 供給（真v5.5.4 blob md5=`859e8c8e`・hal参照 0）**・SM=ON・
`ESP32C3_QEMU=OFF`（★既定 ON は実機で csrw mie illegal）・`ESP32C3_BT_EVT_TRACE=ON`＋`TOPPERS_C3_EVT_FAST_MAP`。

**計器の実体（逆asmで «噛んだ» 確認）**：`evt_trace.c` の 5 wrap（全て cross-TU で `__wrap_` へ jal/j 確認）：
`ble_hs_hci_evt_process`（HCI EVT：conn/disc+reason/ltk_req/enc_chg+status）・`ble_mqueue_put/get`（ACL RX rx_q）・
`ble_l2cap_rx`（L2CAP RX 数）・`ble_transport_to_ll_acl_impl`（TX ACL：CID分離 SMP/ATT＋opcode列）。

**記録先**：
- **RTC FAST `0x50000100`（EVT map・20語）**：[2]conn_cmpl [3]disc [4]disc_reason [5]ltk_req [6]enc_chg
  [7]enc_status [8]put [9]get [10]l2cap [13]le_other [14..19]到着順 EVT code。magic=`0x5c3e0001`。
- **RTC FAST `0x50000160`（TX map・7語）**：[2]tx_smp [3]tx_att [4..6]SMP opcode列。magic=`0x5c3e0002`。
- **RTC STORE**：CONN`0xC0`（毎起動0・接続回数）・DISC`0xB8`（毎起動0・`0xD15C<reason><n>`）・
  PAIR`0x54`（`0x5DC0`=bond発火／`0xA102`=未発火）・ENC`0x58`（`0x5DE0<delta><status>`・後勝ち）。

### 2.1 ★計器較正の «実測» 結果（BlueZ 失敗セッション §4 から得た陽性/盲の判別）

| 計器 | 状態 | 根拠 |
|---|---|---|
| **TX SMP CID=6 カウンタ** | ★**陽性対照 OK** | 失敗 bond で `tx_smp=3`・opcode `0x02/0x0c/0x03`（Pairing Response・PubKey・Confirm）を正しく計数 |
| ACL RX（put/get/l2cap） | 動作 OK | 2/2/2 |
| **app GAP マーカ（CONN/ENC/DISC）** | ★**一次計器・code非依存で信頼** | CONN=1・ENC=`0x5de0000d`（ETIMEOUT）を GAP callback で捕捉（HCI code に依存しない） |
| **`conn_cmpl`（EVT[2]）** | ★**盲を発見→修正**（計器のみ） | 旧条件 0x01 のみ＝esp-idf の C3 controller は **Enhanced Conn Complete(0x0A)** を出す（seq に `0xea` 実測・CONN=1 と乖離）。**0x0A を追加**（C5-10 §7.6「新コード盲」同型）。enc_chg にも v2(0x59) を追加 |
| `ltk_req`（EVT[5]）・`enc_chg`（EVT[6]） | ★**«健全再接続» 未較正** | BlueZ が bond しない＝暗号段に到達せず未検定。**健全な Android 再接続でのみ較正可能** ⇒ **一次判定は app ENC マーカで行い、FAST の ltk_req/enc_chg は突き合わせ用（0 を単独で «盲か実か» 断定しない）** |

## 3. ★事前登録（★測定前に commit・以後書き換えない）

### 3.1 P_still_fails ＝ **C3 Android 再接続が «今日もまだ失敗する» 確率 = 55%**

**較正の根拠（正直に）**：C5 は «消えた»（同じ Fold 7）。しかし **C3 の再接続病態は C5 の bond 病態と別機序**＝
C5 の «消滅» は弱い傍証にしかならない。一方、この Fold 7 は C5 較正の過程で **BT 再起動・開発者モード有効化**を
経ており、C3 にも同種の «スマホ側状態変化» が及んだ可能性がある。⇒ **«まだ失敗» に傾けつつ（別機序＝
より構造的）、«消えた/現在は通る» も実質確率（45%）**として残す。**両分岐とも収穫**：
- **まだ失敗（55%）** ⇒ 失敗が «在る» うちに Step 2（snoop 併用の両側観測）へ進む価値。
- **もう成功（45%）** ⇒ ★**「C3 も消えた」＝スマホ側現象の強い傍証**（C5 と同型）＝**見出し**。深追いせず両側確認で閉じる方向を提案。

### 3.2 失敗が残る場合の分岐（失敗＝100% を上の 55% 内で細分）

| # | 実測パターン（app マーカ主・FAST 従） | 意味 | 条件付き確率 |
|---|---|---|---|
| **F1** | 再接続で **CONN が増えない**（bond 後 CONN=1 のまま） | 再接続 CONNECT がデバイスに届かない（LL/電波 or phone 不再接続） | 15% |
| **F2** | CONN 増える・**ENC に新 status 無し / ltk_req=0 かつ enc_chg=0** | 再接続はするが **暗号化再開が始まらない**（controller が host に LTK を要求しない／host が返さない） | 20% |
| **F3** | CONN 増える・**ENC status≠0**（`0d`ETIMEOUT/`07`ENOTCONN） | 暗号化再開が **始まるが完了しない** | 30% |
| **F4** | CONN 増える・**ENC status=0**・**DISC reason=phone 由来**（例 0x13） | 再接続＋暗号 OK だが **スマホが切る**＝evidence-c3-09「切るのはスマホ側」と最整合 | 35% |

**★反証条件も仮説＝独立測定で検算**：F1-F4 は app マーカ（CONN/ENC/DISC）で一次判定し、
FAST map（ltk_req/enc_chg/tx_smp）で機序を補強。**FAST の 0 は app ENC と突き合わせるまで «盲か実か» 断定しない。**

### 3.3 成功（P_reconnect_ok=45%）時の予測（＝健全再接続の較正も兼ねる）

| # | 予測 | 登録値 |
|---|---|---|
| P_TAG | FAST magic `0x5c3e0001`/`0x5c3e0002` が読める | 95% |
| P_CONNfix | 修正後 conn_cmpl が接続回数を数える（≥ 接続数） | 85% |
| P_TXSMP | 初回 bond の TX SMP≥3・opcode 整合（§2.1 で実証済） | 90% |
| P_LTK | **再接続で ltk_req≥1・enc_chg≥1・enc_status=0**（＝LTK 暗号再開を我々が完了）・**再接続の tx_smp=0**（LTK 再利用＝SMP 無し） | 70%（成功条件下） |

### 3.4 射程（超えて書かない）

- 語れるのは **軸 B（C3×Android 再接続）のみ**。軸 C（iPhone wedge）は別セル・後回し。
- 「なぜ切れるか」は reason/opcode の粒度まで＝**それ以上は snoop（Step 2）が要る**。
- ★**snoop 有効化＝BT 再起動＝病態を消しうる**（C5 で実証）。**一次は app マーカ（スマホ不変）で測り、
  snoop は «失敗が残っていれば» Step 2 で撮る。**

## 4. BlueZ セッション実測（Step 0 の較正・失敗＝esp-idf×BlueZ 既知病態の再現）

真cold→BlueZ で Pair 試行 → **`AuthenticationCanceled`**。デバイス側：
CONN=`0x604e0001`・PAIR=`0xa1020805`（未発火）・ENC=`0x5de0000d`（ETIMEOUT）・
FAST: conn_cmpl=0（★旧盲・修正済）/ ltk_req=0 / enc_chg=0（暗号未到達＝正しい 0）/ put=get=l2cap=2 /
**tx_smp=3（opcode 0x02/0x0c/0x03）**。seq=Enhanced Conn Complete(0xea)…（0x08 無し＝暗号は起きていない）。
⇒ **BlueZ は esp-idf 供給を bond «できない»＝再接続軸の陽性対照に使えない**（Android のみ）。

## 5. Step 1 セルの状態（★準備完了・実測）

| 項目 | 実測 |
|---|---|
| ビルド | `build/rc_c3_evt`（**verify-flash digest matched**＝焼き直し無し・4MB 一致） |
| マーカ | RTC STORE 7本＋FAST map（EVT 20語/TX 7語）を **0 クリア→読み戻し ALL ZERO**（0xBC=ROM上書き＝不触） |
| 1セル1ボード | **`-p 3-4`(C5)/`-p 2`(C6) 電源断→by-id `A7:44`/`5A:9C` GONE 読み戻し**＋**スキャンで ASP3-C5/C6 不在**（較正5デバイス）。hub 1-5 不触 |
| 真cold | `-p 1` off→by-id `BA:BC` GONE 読み戻し→on（POR で RTC クリア＋app 再init）→**`ASP3-C3-BLE` 広告をスキャン実測** |
| BlueZ | device remove・Pairable=True・接続0 |

## 6. ★ユーザー手順（Step 1 = re-baseline・★スマホ状態を «変えず» に安く測る）

★**観測者効果の回避**：**snoop は ON にしない・BT の OFF/ON もしない**（＝C5 で «直した» 操作を «まだ» 加えない。
まず «今の状態で失敗が残るか» を測る）。forget は per-device 操作＝可。

1. **Android・iPhone «両方» で `ASP3-C3-BLE` を «登録解除（forget）»**。
   （デバイスは真cold＝鍵ゼロ。片側の古い bond が残ると «鍵不一致» で偽の失敗になる＝evidence-c3-09 §0.1。
    Android 自動再接続はボードを吊る＝iPhone も forget。）**★BT の OFF/ON は «しない»。snoop も ON にしない。**
2. **Android の Bluetooth 設定から `ASP3-C3-BLE` をペアリング**（フレッシュ bond）。
   観測を逐語で：ペアリング要求 popup→登録できたか／エラーか。
3. ★**bond できたら «再接続» を試す**（＝軸 B の本題）：
   一旦 **切断**（設定で «接続» をトグル off、または端末を離す）→ **もう一度 «接続»**。
   観測を逐語で：**再接続できたか／できないか／切れたのは «すぐ» か «数秒» か／どちらが切ったように見えるか**。
4. ★**iPhone は触らない**（1セル1端末＝カウンタ混ぜない）。**終わったら «終わった» と報告**（解釈はこちらで数値から）。
5. ★**エージェントはユーザー実施 «前» にマーカを読まない**（read-mem は download mode＝広告停止）。

## 7. ★判定（ユーザー報告後にこちらで実施）

- **CONN `0xC0`**：接続回数（bond=1／再接続で 2,3…）。**F1（増えない）判定。**
- **ENC `0x58`**：`0x5DE0<delta><status>`。**status=0=暗号再開成功／0d=ETIMEOUT/07=ENOTCONN**。**F2/F3 判定。**
- **DISC `0xB8`**：`0xD15C<reason><n>`。**reason=0x13=スマホ側終了**。**F4 判定。**
- **PAIR `0x54`**：`0x5DC0`=bond 発火（成功）／`0xA102`=未発火。
- **FAST EVT[2]conn_cmpl / [5]ltk_req / [6]enc_chg / [7]enc_status / TX[2]tx_smp**：機序補強（app ENC と突き合わせ）。
- ★**まだ失敗なら**：Step 2（snoop 有効化＝BT 再起動を «伴う»→この再起動で消えるか自体が情報→両側観測）。
- ★**もう成功なら**：「C3 も消えた」を app マーカ（＋可能なら snoop）で両側確認し、閉じる方向を提案。

---

## 8. Step 1 実測結果（ユーザー実施後・マーカ読取 2026-07-18）

**ユーザー観測（逐語）**：「**Android : 1回 bond したが，その後bondが解除された，その後はconnectできない**」
⇒ ★**C5 と «違う»：C3 の再接続失敗は «今日もまだ在る»。P_still_fails=55% が «失敗» 側で的中。**

| marker | 値 | 読み |
|---|---|---|
| PAIR 0x54 | **`0x5dc00000`** | bond 発火・**status=00（SMP 成功）**・★**our_sec=0・peer_sec=0（鍵が store に «無い»）**——evidence-c3-09 の `0x5dc00011`(1/1) と «違う» |
| ENC 0x58 | `0x5de00000` | 暗号 status=00＝**初回暗号は成功** |
| CONN 0xC0 | **`0x604e0001`** | ★**接続 «1回だけ»＝再接続はデバイスに «届いていない»** |
| DISC 0xB8 | **`0x00000000`** | ★**切断イベント来ず**（毎起動0＝この起動で切断を受けていない） |
| FAST conn_cmpl / ltk_req / enc_chg / enc_status | 1 / 1 / 1 / 0 | ★**conn_cmpl 修正が効いた**（旧=盲0→今=1・CONN=1 と一致）。**ltk_req=1・enc_chg=1 も発火＝この2計器も «実測で較正»**（初回暗号で非0を確認＝盲でない） |
| FAST put/get/l2cap | 26/26/26 | 暗号後 GATT が流れた（初回セッションは健全に GATT まで到達） |
| TX tx_smp / opcodes | 8 / `0b 02 0c 03 04 0d 08 09` | フル SC ペアリング（SecReq・PairRsp・PubKey・Confirm・Random・DHKey・IdInfo・IdAddr）＝**我々は完全に応答した** |

### 8.1 事前登録の判定（★書き換えない）

| 登録 | 値 | 結果 |
|---|---|---|
| P_still_fails | 55% | ★**的中（失敗側）** |
| 失敗分岐 | 本命 F4=35% | ★**F1（15%）が起きた＝«再接続 CONNECT がデバイスに届かない»（CONN=1 のまま）。本命 F4 は外れ** |
| P_LTK | 70%（成功条件下） | **評価不能**（再接続自体が起きていない＝条件不成立） |
| P_TAG / P_CONNfix / P_TXSMP | 95/85/90% | ★**全的中**（magic 読取・conn_cmpl 修正が接続を数えた・tx_smp=8 opcode 整合） |

### 8.2 ★言えること（実測）

1. **初回 bond は «デバイス側で» 完全に成立**：1 接続・フル SC ペアリング・暗号 status=0・GATT まで到達。
   **我々のスタックは初回セッションを «正しく» こなした**（tx_smp=8 で全 SMP を応答・ltk_req/enc_chg 較正済）。
2. **再接続は «デバイスに 1 度も届いていない»**（CONN=1・conn_cmpl=1・2 度目の接続イベント無し）＝**F1**。
3. **初回接続の «切断» もデバイスは受けていない**（DISC=0）＝**«吊られ» の症状**（evidence-c3-07 の axis-C wedge と
   «同じ DISC=0»＝イベント配送の症状。ただし axis-C は iPhone・本件は Android＝**同一視はしない・症状が同型なだけ**）。
4. ★**新しいデバイス側信号**：PAIRING_COMPLETE は status=0 なのに **bond store our=0/peer=0**
   （c3-09 は 1/1）＝**鍵が store に入っていない**。**再接続で LTK 再開できない機序の «候補»**だが（下記）断定しない。

### 8.3 ★言えないこと（計器の射程外＝正直に）

1. ★**«誰が bond を解除したか»＝答えられない**。デバイス bond ストアは **RAM backed**＝
   «デバイス側の解除» と «真cold» が区別できない（over-determined）／**スマホ側の解除はデバイスに写らない**。
   ⇒ **«誰» は snoop（Step 2）待ち**。
2. ★**F1 の内訳を判別できない**（3 通り・デバイス側では分離不能）：
   (i) **デバイスが wedge して広告停止**（DISC=0＝初回リンクを握ったまま）→ スマホが見つけられない／
   (ii) スマホが **bond解除後に再接続を «試みていない»**（自側で諦めた）／
   (iii) スマホは試みたが **LL 接続がデバイスのログ前に失敗**。
3. ★**our=0/peer=0 が «再接続失敗の原因» か «較正タイミングの artifact» か**＝1 run では決められない
   （c3-09 は 1/1 だった＝計器は 1/1 を出せる＝盲ではないが、**この run で真に 0 だったのか**の再現は未取得）。
4. **初回接続が «今» まだ生きているか（wedge か正常切断か）**＝**未測定**（マーカ読取で download mode に
   落とす前に «広告しているか» の受動スキャンを取り損ねた＝**本ラウンドの取り零し・no silent caps**）。

### 8.4 機序（★答えられる範囲だけ）

**再接続失敗の «局在»＝«再接続 CONNECT がデバイスに届く前»**（デバイスは 2 度目の接続を 1 度も見ていない）。
**その «なぜ»（wedge か・スマホ不再試行か・LL 失敗か）と «誰が bond を解除したか» は «今日の計器では答えられない»。**
初回 bond 自体は健全に成立しており、**病態は «bond の質» ではなく «bond 後の再接続・切断配送»** に局在する
（our=0/peer=0 は候補として記録するが断定しない）。

---

## 9. Step 2 — snoop 両側観測（★準備完了・ユーザー判断・実施はユーザー）

**動機**：失敗が «残っている»＝C5 と違い «撮れる»＝両側観測の価値が高い。snoop があれば
«誰が bond を解除し・スマホは再接続を試みたか・切ったのはどちらか» が決定的に分かる（今日の一次計器では «誰» が測れない）。

★★**観測者効果を «変数として» 設計に組み込む**：snoop 有効化＝**BT 再起動**＝**C5 ではこれで «消えた»**。
⇒ **この BT 再起動で C3 «も» 消えるか自体が結論の一部**（消えたら C5 と同型＝スマホ側現象の傍証）。

### 9.1 事前登録（★実施前 commit・書き換えない）

| # | 予測 | 登録値 | 意味（外れも収穫） |
|---|---|---|---|
| **P2_heal** | snoop 有効化（BT 再起動）で **C3 も再接続成功に «消える»** | **40%** | 消えたら C5 と «同型»＝«スマホ側状態 or スタック再起動» が両チップに効く傍証。C3 は別機序なので C5(実質確実に消えた) より低く見積る |
| **S-cal** | snoop に初回 bond の «較正列» が見える（LE Connection Complete≥1・SMP 交換・Encryption・**Disconnection**） | 90% | 見えなければ測定不成立＝取り直し |
| **S1** | 失敗が残り、snoop に **スマホ→デバイスの再接続（LL connection）試行が «在る»** | 30% | (i)(iii)＝デバイス側が受けない/wedge。★device CONN と突き合わせ（snoop 接続数 vs CONN） |
| **S2** | 失敗が残り、snoop に **再接続試行が «無い»**（スマホが bond解除後に諦めた） | 30% | (ii)＝«誰が解除し・なぜ再試行しないか» が snoop 直前イベントに見える |
| **S-disc** | snoop で **初回リンクの切断を «どちらが» 出したか**（device DISC=0 との突き合わせ） | — | DISC=0 の «吊られ» 仮説を検定：スマホが切ったのに device に届かない＝配送欠落／スマホも切っていない＝両者リンク保持 |

**両側突き合わせ（必ずやる）**：接続数（snoop LE Conn Complete vs device CONN=1）／切断（snoop の Disconnect 有無・向き
vs device DISC=0）／SMP 本数（snoop vs device tx_smp=8）／«誰が bond を解除したか»（snoop の再ペアリング/削除の痕跡）。
**不一致はそのまま報告。**

### 9.2 セルの状態（実測・2026-07-18）

| 項目 | 実測 |
|---|---|
| ビルド | `build/rc_c3_evt`（**verify-flash digest matched**＝焼き直し無し・同一計器） |
| マーカ | RTC STORE 7本＋FAST map（EVT/TX）を **0 クリア→読み戻し ALL ZERO** |
| 1セル1ボード | `-p 3-4`(C5)/`-p 2`(C6) 電源断のまま＝by-id `A7:44`/`5A:9C` GONE＋スキャンで ASP3-C5/C6 不在（較正5デバイス）。hub 1-5 不触 |
| 真cold | `-p 1` off→by-id `BA:BC` GONE 読み戻し→on→**`ASP3-C3-BLE` 広告をスキャン実測** |
| BlueZ | device remove・接続0 |

### 9.3 ★ユーザー手順（Step 2・snoop あり・Fold 7・Android のみ）

1. **開発者向けオプション → 「Bluetooth HCI スヌープログを有効化」を ON** →
   ★**Bluetooth を OFF→ON**（スタック再起動でログ開始。★この再起動で «直る» か自体が観測対象）。
2. **Android・iPhone «両方» で `ASP3-C3-BLE` を forget**（真cold＝鍵ゼロ・片側残留は偽失敗）。
3. **Android で `ASP3-C3-BLE` をペアリング**（初回 bond）→ 観測を逐語で。
4. ★**再接続を試す**：一度 **切断** → もう一度 **接続**。**再接続できたか/できないか・切れたのは «すぐ»か «数秒»か**を逐語で。
5. ★再現後、**BT の OFF/ON を «しない»**（ログを新しくしない）。**スマホを USB で PC に接続**（USB デバッグ許可）。
6. 完了を報告 → こちらで `adb bugreport` で btsnoop を回収（scratchpad へ・repo に入れない・第三者 MAC マスク・peer はアドレスで確定）。
   ★**再現操作より先に snoop ON にしてある**こと（今回撮り損ねない）。

### 9.4 射程・禁則（Step 2）

- ★**C3 «再接続不可»（bond 後）と C5 «bond 未到達»（ENOTCONN）は別病態。同一視しない。**
- ★**«誰が bond を解除したか» は snoop でしか測れない**（デバイス側 RAM backed）。
- **修正コードを書かない。** 矛盾はそのまま。値でなく «目的地到達» で判定。

---

## 10. Step 2 実測結果（snoop 両側観測・2026-07-18）＝★★失敗を «両側で» 撮れた（C5 では取れなかった）

**ユーザー観測（逐語）**：「**Android : connect : ペアリング表示 OK : 直後にペアリングできない : 切断**」
★**Step 1（初回 bond 成功・再接続失敗）と «病態が違う»＝今回は «初回ペアリング» が失敗**（＝**bond «未成立»**。
Step 1 の «解除» と混同しない・別事象）。

### 10.1 デバイス側マーカ（Step 1 と対比）

| marker | Step 2 | Step 1 | 読み |
|---|---|---|---|
| PAIR 0x54 | **`0xa1020805`（未発火）** | `0x5dc00000`（発火） | ★**PAIRING_COMPLETE 来ず＝bond 未成立** |
| ENC 0x58 | `0x5de0000d`（**ETIMEOUT**） | `0x5de00000`（成功） | 暗号未到達・host SM が待って timeout |
| CONN / conn_cmpl | 1 / 1 | 1 / 1 | 接続は 1 回 |
| enc_chg / ltk_req | 0 / 0 | 1 / 1 | ★暗号段に «到達していない» |
| **TX tx_smp / opcodes** | **2 / `0b 02`** | 8 / `0b 02 0c 03 04 0d 08 09` | ★**SecReq＋Pairing Response «まで» で止まった**（Step 1 はフル SC 8本） |
| tx_att / put | 15 / 15 | 18 / 26 | ATT は流れた |
| DISC 0xB8 | **0** | 0 | ★**切断イベントは device に届かず**（両 Step 共通症状） |

### 10.2 snoop（phone 視点・較正 PASS）

records=813・span 10:44:06..10:48:12。calibration `cmd230/acl_tx16/acl_rx192/evt375/att30/smp2`
（**LE Conn Complete・Disconnection・ATT が見える＝SMP 有無を語れる**）。peer は全て `60:55:f9:57:ba:bc`（C3・public）。

| 時刻 | イベント |
|---|---|
| 10:44:21.776 | LE **Enhanced** Connection Complete handle=3 role=central status=0（phone=central・DUT=C3） |
| 10:44:26.367 | **phone RX ← DUT: Security Request(0x0b)**（接続 ~4.6s 後＝我々の 5s tick と整合） |
| 10:44:27.365 | **phone TX → DUT: Pairing Request(0x01)** |
| （以後 SMP 無し） | ★**phone は我々の Pairing Response(0x02) を «一度も受信していない»** |
| （Enc Change 無し） | 暗号化イベント 0 件 |
| 10:44:32.422 | **Disconnection reason=0x08（supervision timeout）**（Pairing Request の ~5s 後） |

### 10.3 ★★両側突き合わせ（独立2計器・本ラウンドの主目的）

| 項目 | snoop（phone） | device（marker） | 判定 |
|---|---|---|---|
| 接続数 | 1（enh・handle=3） | CONN=1・conn_cmpl=1 | ★**一致** |
| 我々の Security Request | phone RX op=0x0b @26.367 | tx_smp opcode 0x0b | ★**一致（電波に出た）** |
| phone の Pairing Request | phone TX op=0x01 @27.365 | 我々が Response を返した＝受信した | 整合 |
| **我々の Pairing Response** | ★**phone «受信せず»**（log 全体で SMP=2・0x02 無し） | **tx_smp opcode 0x02**（host→controller 手渡しは実測） | ★★**乖離＝我々の Pairing Response が phone に «届いていない»** |
| 暗号化 | Enc Change 0 件 | enc_chg=0・ENC=ETIMEOUT | ★一致（暗号未到達） |
| 切断 | reason=0x08 supervision timeout（能動終了でない） | ★**DISC=0（device に届かず）** | ★**乖離＝device は切断イベントを受けていない**（Step 1 と同型の DISC=0） |

### 10.4 ★«ペアリングできない» の正体（言える範囲）

**我々の host は Pairing Response を «送出» した**（`ble_transport_to_ll_acl_impl` で controller へ手渡し＝tx_smp=2 で実測）。
**しかし phone はそれを «受信していない»**（較正済 snoop で SMP=2・0x02 不在）。⇒ **SMP は Pairing Request/Response 段で凍結**し、
**リンクは supervision timeout(0x08) で死んだ**（どちらも能動切断していない）。
⇒ ★**loss は «我々の host より下»（controller / LL / radio の TX 経路）＝host は無罪**（PDU を出している）。
**evidence-c3-04 の «controller/LL 層に真因» と同方向**（ただし別事象＝断定はしない）。

★**厳密な限界**：snoop は phone の HCI＝«phone の host が見た» もの。「我々の無線が出さなかった」と
「出たが phone の controller が落とした（HCI 前）」は snoop «だけ» では分離不能。**どちらでも «host より下の link/radio 損失» で、我々の host は無罪**、が言える範囲。

### 10.5 ★事前登録の判定（★書き換えない・枠外を明記）

| 登録（`4e1b6b0`） | 値 | 結果 |
|---|---|---|
| **P2_heal**（BT 再起動で消える） | 40% | ★**«消えなかった»**（非該当側）。★**むしろ病態が «初回ペアリング失敗» に «変わった»**（Step 1=再接続失敗）＝C5 と «逆方向» |
| S-cal（snoop 較正見える） | 90% | ★**的中**（conn/disc/ATT 可視） |
| **S1 / S2**（再接続試行の有無） | 各30% | ★**枠外**：Step 2 は «再接続» に至らず «初回ペアリング» が失敗＝**登録の前提（再接続を撮る）が崩れた**＝«枠外の観測» として記録（登録は書き換えない・C5-10 §7 の作法） |

### 10.6 ★射程・言えないこと（厳守・正直に）

- ★**«BT 再起動/snoop が病態を «変えた»» と «元々間欠» と «スマホ側状態» は区別できない**（列挙のみ・断定しない）：
  (a) 観測者効果（C5 と逆向き）／(b) 元々間欠で別モードを引いた／(c) スマホ側状態ドリフト。
- ★**Step 1（再接続失敗・F1）と Step 2（初回ペアリング失敗・Pairing Response 損失）を «同じ機序» と決めない**。
  **共通するのは «DISC=0（controller→host 配送欠落）» の症状のみ**＝それ以上の統一は証拠が要る。
- ★**«誰が bond を解除したか»**：Step 2 は **bond «未成立»**＝«解除» 事象は無い（Step 1 の «解除» と別）。
- **1 run の観測**＝«安定再現» は未確認（下記 Step 3 の存在意義）。
- **我々の Pairing Response が «無線に出たか»/«phone controller が落としたか» は snoop だけでは未分離**（§10.4）。

### 10.7 スナップショット（no silent caps・未測定）

- snoop・bugreport は **scratchpad に保管**（`rc_c3_step2_btsnoop.log`・`rc_c3_step2_bugreport.zip`）＝**repo に入れない**。第三者 MAC は出現せず（peer は全て DUT）。
- **device の RX SMP 本数**（对向 Pairing Request が host に届いた «回数»）は CID 分離ミラー無し＝put(15) に ATT と混在。**snoop 側で phone TX=Pairing Request 1本を確認したのが唯一の RX 証拠**。
- Pairing Response 損失の «原因層»（controller TX queue / ATT 輻輳 / RF）は未分離。

---

## 11. Step 3 提案（★実施しない・提案のみ）＝«この失敗は安定に再現するか»＋準備完了

**動機（コーディネータ）**：C3 は «撮れる»＝C5 との決定的差＝機序に踏み込める。**次の鍵＝Step 2 の失敗が «安定再現» するか**
（安定なら «host より下の TX 損失» の強い候補／間欠なら観測者効果・RF を切り分ける材料）。

### 11.1 事前登録（★実施前 commit・書き換えない）

| # | 予測 | 登録値 |
|---|---|---|
| **P3_repro** | 同条件（snoop ON・BT 再起動あり）で **また «初回ペアリング失敗»**（Pairing Response 損失 or 別 SMP 段で停止） | 50% |
| P3_sig | 失敗時、**tx_smp が Step 2 と «同型»（SecReq＋Pairing Response 止まり）** | 40%（失敗条件下） |
| P3_alt | むしろ Step 1 型（初回 bond 成功→再接続失敗）に «戻る» | 25% |
| P3_heal | 今度は成功（C5 型に «消える»） | 25% |

★**4 分岐すべて収穫**：同型再現＝TX 損失候補強化／別 SMP 段＝損失は特定 PDU 非依存／Step 1 型復帰＝間欠の実証／成功＝観測者効果 or 状態依存の実証。

### 11.2 セル状態（実測・準備完了）

| 項目 | 実測 |
|---|---|
| ビルド | `build/rc_c3_evt`（**verify-flash digest matched**＝同一計器・焼き直し無し） |
| マーカ | RTC STORE 7本＋FAST（EVT/TX）を **0 クリア→読み戻し ALL ZERO** |
| 1セル1ボード | C5/C6 電源断のまま＝by-id `A7:44`/`5A:9C` GONE＋スキャン不在（較正5デバイス）・hub 1-5 不触 |
| 真cold | `-p 1` off→by-id `BA:BC` GONE 読み戻し→on→**`ASP3-C3-BLE` 広告実測** |

### 11.3 ユーザー手順（Step 3＝Step 2 の «同条件» 再現）

★snoop は既に ON のはず＝**BT OFF/ON «しない»**（ログ継続・状態を変えない）／両端末 forget →
Android で `ASP3-C3-BLE` ペアリング（＝Step 2 と同じ操作）→ 観測を逐語で（popup→失敗までの体感・切れ方）→
**BT を触らず** USB 接続のまま報告 → こちらで bugreport 追加回収。
（★もし snoop を切ってしまっていたら：snoop ON→BT OFF/ON→forget→ペアリング。その «BT 再起動で消えるか» も記録。）

---

## 12. Step 3 実測結果＋★★3回横断の総合（間欠が実証された・安定な共通項を同定）

**ユーザー観測（逐語）**：「**Android : again ペアリング表示 OK : ペアリングできたが，ちょくごに切断，画面上はBONDED**」

### 12.1 Step 3 デバイス側（★Step 1 と «device 側は同一»）

PAIR=`0x5dc00000`（発火・status0・our=0/peer=0）・ENC=`0x5de00000`（**暗号成功**）・CONN=1・
FAST conn_cmpl=1/ltk_req=1/enc_chg=1/enc_status=0・**tx_smp=8**（フル SC `0b 02 0c 03 04 0d 08 09`）・put=29・**DISC=0**。
⇒ **Step 1 の device マーカと «ほぼ完全一致»**（bond 成立・DISC=0）。**ユーザー体感の差（Step1「解除後 connect 不可」/ Step3「直後切断 BONDED」）は device 側では同じ事象**。

### 12.2 Step 3 snoop（handle=5・较正 PASS・cumulative log に Step2=handle3 も含む）

| 時刻 | イベント |
|---|---|
| 10:58:55.558 | Enhanced Conn Complete handle=5（phone=central） |
| 10:59:00.4〜03.1 | ★**フル SC 交換«成立»**：SecReq(DUT)→PairReq(phone)→**Pairing Response(DUT)«今回は届いた»**→PubKey↔→Confirm(DUT)→Random↔→DHKey↔→**鍵配布両方向** |
| 10:59:03.145 | ★**Encryption Change status=0 enabled=1**＝**暗号成立・bond 完了**（device ENC=`0x5de00000`・snoop 一致） |
| 10:59:08.912 | ★**Disconnection reason=0x08（supervision timeout）**＝暗号成立の ~5.8s 後・**能動終了でない** |

⇒ **両側一致**：bond+暗号は «完全成立»。その後 **リンクが supervision timeout で死ぬ**。**device は DISC=0（切断を受けていない）**。

### 12.3 ★★3回横断の総合表（本ラウンド最大の成果物）

| 信号 | Step 1（snoop無） | Step 2（snoop・BT再起動） | Step 3（snoop） | 安定/変動 |
|---|---|---|---|---|
| ユーザー体感 | bond→解除→再接続不可 | 初回ペアリング失敗 | ペアリング成功→直後切断 BONDED | **毎回違う＝間欠** |
| CONN / conn_cmpl | 1/1 | 1/1 | 1/1 | ★安定（1接続） |
| PAIR（bond） | 発火(0/0) | **未発火** | 発火(0/0) | 変動 |
| tx_smp / SMP 到達 | **8**（フル SC） | **2**（PairRsp 止まり） | **8**（フル SC） | 変動（SMP 段） |
| ENC status | 00 成功 | **0d ETIMEOUT** | 00 成功 | 変動 |
| **DISC（device）** | **0** | **0** | **0** | ★★**安定（3/3 切断未配送）** |
| snoop 切断 reason | （snoop無） | **0x08 supervision timeout** | **0x08 supervision timeout** | ★安定（能動終了でない） |
| **我々 host の TX** | 出している | 出している（Response も） | 出している | ★安定（**host 無罪**） |
| put/get/l2cap | 26 | 15 | 29 | 変動（GATT 量） |

### 12.4 ★安定な共通項＝追うべき機序（間欠の中の不変量）

1. ★★**DISC=0（3/3）＝我々の controller→host «切断イベント配送» の欠落**（`docs/bt-shim.md:2637` が名指したギャップ・PLAN 軸C の wedge 症状と同型）。
2. ★**リンクは «supervision timeout(0x08)» で死ぬ**（snoop で撮れた 2/2）＝**能動切断でない＝リンク維持（LL keepalive）が保てず沈黙→timeout**。両側の controller が timeout を見た＝**RF リンクが実際に沈黙**。暗号成立の ~5〜6s 後（Step2/3 とも）。
3. ★**我々の host は常に «正しく PDU を出す»（tx_smp・Response・鍵配布）＝host 無罪**。

**⇒ 追うべきは «我々の controller 駆動層（shim/OSAL/init・LL）»**：**blob でも host ソフトでもない**（host は 3/3 で正しく振る舞う）。**evidence-c3-04「真因=controller/LL 層」・Step2「Pairing Response が host 下で消失」と同方向。**

**⇒ «変動する部分»（bond 成否・SMP がどこまで進むか・暗号成否）＝間欠のノイズ**（RF/タイミング/ATT 輻輳/観測者効果のいずれか＝**区別できない**）。

### 12.5 事前登録の判定（★書き換えない・枠外明記）

| 登録（`1570cdc`） | 値 | 結果 |
|---|---|---|
| P3_repro（Step2 型=初回失敗） | 50% | 外れ（Step3 は bond 成功） |
| P3_alt（Step1 型=bond 成功後の問題） | 25% | ★**device 側は Step1 と «一致»＝最も近い**（bond 成功・DISC=0） |
| P3_heal（C5 型に «消える»＝安定成功） | 25% | 外れ（bond 直後に切断＝安定成功でない） |
| （総合） | — | ★**«第4の顔»（ペアリング成功→直後切断 BONDED）＝«枠外» と明記**（device 側は Step1 型だが体感は新記述）。**登録は書き換えない** |

### 12.6 ★間欠の «実証»（見出し）

**Step 2→3 は BT 再起動すら挟んでいない（snoop ON のまま・同条件）** のに病態が変わった（初回失敗→bond成功+切断）
＝★**«間欠» が実証された**。**«間欠の駆動要因»（RF/観測者効果/スマホ状態/タイミング）は区別できない（列挙のみ）。**

### 12.7 言えないこと（no silent caps）
- ★**3態を «同一機序» と断定しない**（共通は DISC=0＋«host 下の脆さ»«まで»）。
- **supervision timeout の «根本原因»**（我々の LL がリンク維持を止めたか／RF／conn param）は未分離。
- **DISC=0 が «全切断» か «supervision timeout 限定» か**は未検定（→ Step B で BlueZ 主導の能動切断が DISC を出すか测れる）。
- snoop/bugreport は scratchpad（repo 外・第三者 MAC 出現せず＝peer 全て DUT）。

---

## 13. ★判断表（A/B/C・実施はユーザー判断・推奨に印）

**間欠がスマホ駆動セルで «確定» した今、費用対効果が変わった。**

| 案 | 内容 | 判別力 | 費用 | 何が確定するか | 推奨 |
|---|---|---|---|---|---|
| **A** さらにセルを重ねる | N 回連続で «安定共通項(DISC=0)» の再現率・間欠の分布を測る | 低〜中：間欠は既に実証済＝新規性小。観測者効果が混じる | 大（スマホ操作律速） | 間欠の «統計» のみ（機序でない） | — |
| ★**B** デバイス側だけ深掘り | **controller→host 切断配送（DISC=0・`bt-shim.md:2637`）**と **Pairing Response TX 経路**を **BlueZ 主導で** 計器化。BlueZ は bond 完了しないが «接続/能動切断/初回 SMP» は駆動できる＝**«能動切断で DISC が出るか»** で «全切断欠落» vs «timeout 限定» を判別／**Response TX を無線送出まで追う** | ★高：**観測者効果を排除**し、**安定共通項(DISC=0)に直接迫る**。スマホ不要＝反復自由 | 中（ビルド＋BlueZ・スマホ不要） | **DISC=0 が全切断か supervision 限定か**・**controller→host 配送ギャップの実在**を snoop 無しで | ★**推奨** |
| **C** 閉じる | 成果を畳む（下記 §13.1） | — | ゼロ | 現時点の到達点を確定 | ○（要件充足を優先するなら） |

### 13.1 (C) で畳む場合の «正しい結論»

- **C3 × Android は «間欠的に» 失敗（3態：再接続不可／初回ペアリング失敗／bond 成功後 直後切断）。**
- **両側観測で «初回 Pairing Response が host より下で消える» を1回捕捉**（C5 では «撮れなかった» 失敗記録）。
- **安定な共通項＝DISC=0（controller→host 切断配送の欠落・3/3）＋リンクは supervision timeout で死ぬ（能動終了でない）＋host は無罪。**
- **機序の «層»＝我々の controller 駆動（shim/OSAL/init・LL）＝blob でも host ソフトでもない**（host は 3/3 で正しく PDU を出す）。
- **未決＝原因層の分離（controller 内/RF/conn param）と間欠の駆動要因。**
- **成果物＝両側観測の型（device カウンタ＋phone snoop・本数一致で立証）＋C3 で撮れた失敗記録。**

★**推奨＝B**（観測者効果を排除して安定共通項 DISC=0 に snoop 無しで迫れる）。**要件充足を優先し機序を保険とするなら C**。**A は非推奨**（間欠は実証済＝統計を足すだけ）。

---

## 14. Step B — DISC=0 の特徴づけ（★スマホ無し・BlueZ 主導・観測者効果なし・自走）

**問い**：安定共通項 **DISC=0（controller→host 切断配送の欠落）は «全切断» か «supervision timeout 限定» か «間欠» か。**

**計器（★新規 wrap 不要＝既存が配送鎖をブラケット）**：
controller →[HCI transport]→ `ble_hs_hci_evt_process`（**FAST EVT[3] disc**＝HCI Disconnection Complete 0x05 到達・★0x05 は legacy code＝盲でない）→ host GAP →app（**DISC マーカ 0xB8**＝`0xD15C<reason><count>`・毎起動0）。
- EVT[3]=0 ∧ DISC=0 ⇒ 切断は host HCI にすら届かない＝**controller→host transport 損失**（`bt-shim.md:2637`）。
- EVT[3]>0 ∧ DISC=0 ⇒ HCI までは来たが GAP へ来ない＝host 内部損失。
- EVT[3]>0 ∧ DISC>0 ⇒ 完全配送。

**★正の対照の注意（コーディネータ指摘・実測で確認）**：過去の非0 `DISC=0xd15c1302`（配送された）は
**evidence-c3-09 cell2＝hal 供給**＝**esp-idf 供給（rc_c3_evt）での DISC 配送は «一度も確認されていない»**。
⇒ **rc_c3_evt で «能動切断→DISC≠0» が出るか自体が正の対照の確保**（出れば計器非盲＋配送可能・出なければ構造的損失候補）。

### 14.1 事前登録（★測定前 commit・書き換えない）

| # | 予測 | 登録値 | 意味 |
|---|---|---|---|
| **P_poscontrol** | rc_c3_evt で **«能動切断→DISC≠0»** が «一度でも» 出る（＝計器非盲＋配送可能条件が在る） | **55%** | 出れば「何が配送を分けるか」が問い／出なければ「esp-idf 供給で配送が構造的に死ぬ」候補（hal は配送した＝供給差） |
| **P_active_delivered** | 能動切断（BlueZ Disconnect）で **DISC≠0**（配送率で判定） | **50%** | esp-idf での配送は未確認＝五分。清潔な HCI event なら届く見込み vs Step1-3 の一貫 DISC=0 |
| **P_timeout_lost** | timeout 切断（hci0 down で沈黙）で **DISC=0** | **75%** | Step1-3（supervision timeout）と同型を予想 |

★**分岐（測定前固定）**：(a)能動≠0 ∧ (b)timeout=0 ⇒ **«配送欠落は timeout 限定»**（能動は届く）。
(a)(b) とも 0 ⇒ **«全切断が構造的に届かない»**（esp-idf 供給差の候補）。(a) が間欠 ⇒ **配送自体が間欠**。
★**1回で断定しない＝各 N 回で «配送率» を出す。**

### 14.2 射程
- ★**BlueZ は bond しない＝«初回 SMP まで» しか駆動できない**＝**本実験は «切断配送» に限定**（再接続・bond 後は対象外）。
- ★**過去の非0 は hal 由来**＝**«DISC=0 は普遍» と書かない**（供給・切断型で分ける）。

---

## 15. Step B 実測結果（BlueZ 主導・スマホ無し・観測者効果なし・自走）＝DISC=0 の «正体» を特徴づけた

### 15.1 ★★正の対照＝«能動切断→DISC 配送» を esp-idf ビルドで «初めて» 確保（P_poscontrol/P_active_delivered 的中）

**すべて `rc_c3_evt`（esp-idf・digest matched）・真cold 毎回・BlueZ が能動切断（explicit LL_TERMINATE）**：

| 切断法 | 回数 | device DISC(0xB8) | FAST EVT[3] disc | 配送 |
|---|---|---|---|---|
| BlueZ `Disconnect()` | 5 | `0xd15c1305`（reason 0x13・count=5） | 5 | ★**5/5 配送** |
| `Powered=False`（local power-off terminate） | 1 | `0xd15c1501`（reason **0x15**・count=1） | 1 | ★**1/1 配送** |
| `RemoveDevice`（connected=True 時） | 2 | `0xd15c1301`（reason 0x13・count=1 ×2 boot） | 1 ×2 | ★**2/2 配送** |
| **合計（explicit terminate）** | **8** | — | — | ★★**8/8＝100% 配送** |

⇒ ★★**«DISC=0 は全切断が構造的に届かない» は «偽»**：**esp-idf ビルドで «明示的 terminate（LL_TERMINATE）を受けた切断» は host まで完全配送される（8/8・reason 0x13/0x15 とも）**。**計器は盲でない**（EVT[3]・DISC マーカとも非0 を出せる）。**controller→host 切断配送は «terminate に対しては» 生きている。**

### 15.2 ★では Step1-3 の DISC=0 は何か＝«supervision timeout（silent link death）限定»

- **Step 2/3 の snoop**：切断 reason=**0x08（supervision timeout）**＝**能動終了でない（LL_TERMINATE 無し・リンクが沈黙して死ぬ）**。
- **その supervision timeout は device host に届かない**（DISC=0・EVT[3]=0・3/3・Android）。
- ⇒ ★**配送欠落は «明示 terminate» でなく «silent な supervision timeout» に «限定»**：
  **C3 controller は «LL_TERMINATE を受けた切断» は host へ渡すが、«リンクが沈黙して死んだ（supervision timeout）» 切断イベントは host へ渡さない（or 生成しない）。**

### 15.3 ★事前登録の判定（★書き換えない）

| 登録（`64a9bc7`） | 値 | 結果 |
|---|---|---|
| P_poscontrol（能動切断で DISC≠0 が出る） | 55% | ★**的中**（8/8 配送・計器非盲確定） |
| P_active_delivered（能動切断→DISC≠0） | 50% | ★**的中**（配送率 8/8＝100%） |
| P_timeout_lost（timeout 切断→DISC=0） | 75% | ★**部分的中（枠外注記）**：Android の supervision timeout で DISC=0（3/3）は成立。★**しかし «snoop 無しでの clean な silent timeout» は誘発できず**（下記 §15.4）＝**この leg は Android データ（phone 駆動）に依存＝snoop-free 確認は未達** |

### 15.4 ★言えないこと・未測定（no silent caps）

- ★**«silent supervision timeout» を snoop 無しで誘発できなかった**：`sudo hciconfig hci0 down`（NOPASSWD 外＝password 要求で失敗）以外の BlueZ 経路（`Disconnect`/`Powered=False`/`RemoveDevice`）は **すべて明示 terminate を送る＝配送されてしまう**。⇒ **«timeout→DISC=0» の snoop-free 確認は未達**（Android 3/3 に依存）。
  - 1 例だけ «connect が False に落ちた後 RemoveDevice→DISC=0» を観測したが、**established connection でない可能性**があり clean な silent-timeout の証拠にしない（正直に）。
- ★**«なぜ Step1-3 でリンクが supervision timeout で死ぬのか»（＝実際の間欠失敗の «根» ）は本 Step では未解明**。
  DISC=0 は «timeout で死ぬ» ＋ «timeout を配送しない» の下流症状。**根＝«活動後 ~5-6s でリンクが沈黙する» LL/接続維持の問題**（我々の LL が維持を止めた／phone／RF＝両側が timeout を見た＝RF が実際に沈黙・分離不能）。
- BlueZ の Connect が 1 回 False に落ちた（接続確立自体が時々不安定）＝同じリンク不安定 substrate の可能性（断定しない）。
- 能動切断中に PAIR=`0x5dc00c00`/ENC=`0x5de0000c`（status 0x0c）が付随（cycle が 5s tick を跨いだ際の SecReq→BlueZ 不完了ペアリング）＝DISC の問いに無関係・付随。

### 15.5 ★DISC=0 の «正体»（言える範囲・確定）

**DISC=0（Step1-3）＝«全切断が届かない» のではない。«明示 terminate» は 8/8 配送される（snoop-free 確定）。
DISC=0 は «リンクが supervision timeout で silent に死ぬ» 切断が host へ配送されないこと＝«timeout 限定» の症状**
（timeout leg は Android 3/3 に依存＝snoop-free 未確認）。**間欠なのは «配送» ではなく «リンクが死ぬ／pairing がどこまで進むか»**
（配送は terminate に対し 8/8 決定的）。**残る根本＝«なぜ活動後にリンクが沈黙して supervision timeout で死ぬか»（LL/接続維持・controller 層）＝未解明。**

### 15.6 ★判断表の更新（実施はユーザー判断・推奨に印）

| 案 | 内容 | 判別力 | 費用 | 推奨 |
|---|---|---|---|---|
| **D** «silent timeout» を snoop-free で誘発 | sudo（hciconfig down）が要る or RF 遮蔽治具＝**環境的に不可**（NOPASSWD 外） | timeout leg の snoop-free 確認 | 環境変更要 | △（環境依存） |
| **E** «リンク沈黙の根» を追う | 活動後 ~5-6s の supervision timeout を LL/接続イベントで観測（JTAG or controller トレース）＝**実際の間欠失敗の根に迫る** | ★高（根本） | 大（controller/LL 計装・難度高） | ○（機序を要するなら） |
| ★**C** 閉じる（更新版） | 下記 §15.7＝**DISC=0 の正体まで確定した状態で畳む** | — | ゼロ | ★**推奨**（要件充足優先・DISC=0 は解明済・根は保険） |

### 15.7 (C) 更新版の «正しい結論»

- **C3 × Android は間欠的に失敗（3態）＝共通の下流症状は DISC=0。**
- ★**DISC=0 の正体を snoop-free で確定**：**明示 terminate は 8/8 配送される（計器非盲・配送生存）／DISC=0 は «supervision timeout で silent に死ぬ» 切断に限定**（timeout leg は Android 3/3 依存・snoop-free 未確認）。
- **層＝我々の controller/LL（terminate は配送するが timeout は配送しない＋活動後にリンクが沈黙する）＝blob でも host ソフトでもない**（host は SMP を正しく出す・terminate 配送は生きている）。
- **未解明＝«なぜ活動後 ~5-6s でリンクが supervision timeout で死ぬか»（間欠失敗の根）と «timeout 配送欠落の snoop-free 確認»。**
- **成果物＝両側観測の型＋C3 で撮れた失敗記録＋DISC=0 の切断型依存性（terminate 8/8 配送・timeout 欠落）を snoop-free で特徴づけ。**

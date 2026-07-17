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

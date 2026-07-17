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

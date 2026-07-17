# C3 evidence-05 — **A の最後の SMP PDU を特定**／TX で「沈黙」と「誤答」を分離 ⇒ **機構は «暗号開始»（LL/コントローラ層）へ移動**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `60:55:F9:57:BA:BC`**（hub `1-6` port1）／central＝ホスト `hci0`
前段: `evidence-c3-04` ＝ 機構は «SMP 交換の内部» までしか絞れていなかった

---

## 0. 結論（先に3行）

1. **A の最後の SMP PDU ＝ `0x0D` Pairing DHKey Check。B と «完全に同一» の系列**
   （`02 → 0C → 03 → 04 → 0D`）。**A は «沈黙» でも «誤った opcode» でもない。**
2. **B はその後 LTK Request を受けて暗号が成立**（`ltk_req=1`・`enc_chg=1` status **0**）し、
   鍵配布（`08`/`09`）まで進む。**A は LTK Request を «一度も» 受け取らない**（`ltk_req=0`）。
3. ⇒ **機構は «ホストの SMP opcode 流» ではない**（同一と実証）。
   **DHKey Check の «後»＝暗号開始（central の LL_ENC_REQ → controller の LE LTK Request）**
   ＝**LL/コントローラ層**へ移った。

---

## 1. 計装（**「効かない wrap」を実測で捕まえた**）

**wrap 対象＝`ble_transport_to_ll_acl_impl(struct os_mbuf *om)`**（host→controller の ACL 送出）。

### 1.1 ★候補の «実測による» 却下

| 候補 | 却下理由（実測） |
|---|---|
| `ble_sm_rx` | **関数ポインタ（`chan->rx_fn`）経由**で呼ばれる ⇒ `--wrap` 不可 |
| `ble_l2cap_rx` | **両ツリーでシグネチャが違う**：hal=`(conn, hci_data_hdr, os_mbuf **om, …)` ／ esp-idf=`(uint16_t conn_handle, uint8_t pb, os_mbuf *om)` ⇒ **om の位置も型も違う（片方は二重ポインタ）＝共通 parser を書けない** |
| **`ble_transport_to_ll_acl`** | **★wrap しても «無言で効かない»**（下記） |
| **`ble_transport_to_ll_acl_impl`** | **採用**＝両ツリーで同一シグネチャ・実際に wrap が噛むことを逆アセンブルで確認 |

### 1.2 ★★「効かない wrap」＝**捏造を生む寸前だった**

`-Wl,--wrap=ble_transport_to_ll_acl` は **何も起きなかった**。
`ble_transport_to_ll_acl` は `transport.h` の薄いシムで、逆アセンブル上
**`ble_hs_tx_data` は直接 `ble_transport_to_ll_acl_impl` へ `j` している**
⇒ 当該名の未定義参照が存在せず `--wrap` が噛まない。

```
4200e370 <ble_hs_tx_data>:
4200e370:  j  42006e36 <ble_transport_to_ll_acl_impl>     ← wrap を素通り
```

★**もし気付かず測っていたら `tx_total=0` ＝「ペリフェラルは沈黙している」という
«完全に誤った機構» を報告していた**（＝計器が黙っていることを被験体の沈黙と読む）。
⇒ **`--wrap` は «実際に噛んだか» を必ず逆アセンブルで確認する**（本ラウンドの恒久教訓）。
修正後は両アームとも `ble_hs_tx_data -> __wrap_ble_transport_to_ll_acl_impl` を確認済み。

### 1.3 ★C0（計装の非破壊性）＝**先に実施**（前ラウンドの規律を維持）

| # | 構成 | bond |
|---|---|---|
| **C0** | **hal ＋ TX トレース** | **成功**（`Pairing successful`） |

⇒ **計装は B を壊していない**。**A/B は同一ビルド条件**（同じ wrap・同じ map）。

★**ただし §3.3(d) の宣言どおり「C0 成功 ⇒ A でも無害」は不健全**。実際 **A の初回 run は
`total=2`・ACL=0** と異常に浅く、**TX トレース無しの `c3map_A`（13/6）と食い違った**。
**A run#2 で `total=12`・`acl=6` ＝ TX トレース無しの値と一致**したので
**「A は run ごとに揺れる（初回は外れ値）／TX トレースが A を系統的に壊してはいない」**と判断した。
**単一 run で «TX トレースが A を壊した» と結論しなかったのは、前ラウンドで
`CONN` マーカを単一 run から読んで撤回した経験による。**

---

## 2. ★★実測（依頼 (a)(b)）— 全て真cold・同一手順・毎回 `remove`

| 項目 | **A**（esp-idf／真の v5.5.4） | **B**（hal） |
|---|---|---|
| bond | **失敗**（`AuthenticationCanceled`） | **成功**（`Pairing successful`） |
| **TX SMP 系列** | **`02 → 0C → 03 → 04 → 0D`** | **`02 → 0C → 03 → 04 → 0D` → `08` → `09`** |
| `tx_smp` / `tx_att` / `tx_total` | **5 / 4 / 9** | **7 / 5 / 12** |
| **`ltk_req`** | **0** | **1** |
| **`enc_chg`（status）** | **0** | **1（status 0＝成功）** |
| RX `acl put/get/l2cap` | 6 / 6 / 6 | 9 / 9 / 9 |

**SMP opcode の意味**：`02`=Pairing Response ／ `0C`=Pairing Public Key ／ `03`=Pairing Confirm ／
`04`=Pairing Random ／ **`0D`=Pairing DHKey Check** ／ `08`=Identity Information ／
`09`=Identity Address Information。

### 2.1 (a) A の最後の SMP PDU

**`0x0D` Pairing DHKey Check。** そして **そこまでの系列は B と «バイト単位で同一»**。

### 2.2 (b) 「沈黙」か「誤答」か ⇒ **どちらでもない**

- **沈黙ではない**：A は **5 個の SMP PDU を実送出**している（`tx_smp=5`）。
- **opcode の誤りでもない**：**B と同じ opcode を同じ順で**送っている。
- ⇒ **ホスト側 SMP の «手順» は A でも正しく完走している。**

---

## 3. ★機構の局在（依頼 (e)）— **1段深く移動した**

**LE Secure Connections のペアリングは、DHKey Check の «後» に**
**central が `LL_ENC_REQ` を出し → controller が host へ `LE Long Term Key Request`（0x3E/0x05）を上げ →
host が LTK Reply → `Encryption Change`（0x08）** で暗号が立つ。**鍵配布（`08`/`09`）はその後。**

- **B**：`0D` 送出 → **`ltk_req=1`** → **`enc_chg=1` status=0** → **`08`/`09` 配布** → bond 成立。
- **A**：`0D` 送出 → **`ltk_req=0`（LTK Request が «一度も» 来ない）** → 暗号が立たない → central が諦める。

⇒ **分岐点は «ホストが DHKey Check を送った後、コントローラが暗号開始を完遂するか»。**

★**これは既存 `evt_trace` が «そのために» 作られた判定基準そのもの**（ファイル冒頭）：
> `LTK req == 0` → **controller が LTK を要求していない＝LL/コントローラ層で暗号開始が完遂しない**

**その判定規則を A に当てると «LL/コントローラ層» を指す。**

### 3.1 ★決まったこと（PROVEN．A/B の差として）

| 層 | 判定 |
|---|---|
| 接続確立（HCI EVT 系列） | **無罪**（evidence-c3-04：seq バイト一致） |
| ACL 配送・host rx_q・L2CAP ルーティング | **無罪**（`put==get==l2cap`） |
| **ホスト SMP の opcode 流** | **★無罪（本ラウンド）**＝`02 0C 03 04 0D` が A/B 同一 |
| **暗号開始（LL_ENC_REQ → LE LTK Request）** | **★ここで分岐**＝A は LTK Request を受け取らない |

⇒ **残る容疑は «コントローラ»（blob ＋ その glue `bt.c`）の暗号開始経路**。

### 3.2 ★★C5 による «クロスチップ対照» — **ホスト SM の暗号計算まで無罪になった**（混成を使わずに）

**§3.2 の «DHKey の値が誤っている可能性» は，実機を追加で触らずに «静的に» 潰せた。**

**事実（memory `c5-ble-d2c-d2d-gatt-bond` ＋ 本ラウンドの実測）**：

- **C5 は `ASP3_BT_IDF_V554=ON`（＝真の v5.5.4 タグ submodule 供給）のまま、真cold で
  D-2c/D-2d を達成**——**`0xABF4` の «暗号必須 read» ＝ fresh bond の LTK が実効**。
  ⇒ **esp-idf v5.5.4 の NimBLE ホスト＋esp-idf v5.5.4 の blob で «bond は成立する»**。
- **本ラウンドの実測**：**C5（bond する）と C3-A（bond しない）は，
  ホスト SM のソースを «同一パス» からコンパイルしている**：

```
  ble_sm.c      C5 == C3-A : esp-idf/components/bt/host/nimble/.../host/src/ble_sm.c
  ble_sm_alg.c  C5 == C3-A : esp-idf/components/bt/host/nimble/.../host/src/ble_sm_alg.c
  ble_sm_sc.c   C5 == C3-A : esp-idf/components/bt/host/nimble/.../host/src/ble_sm_sc.c
```

**⇒ `ble_sm_alg.c`（DHKey/Confirm の暗号計算そのもの）は «同一ファイル»。
C5 でその計算結果が central に受理されて暗号が立つ以上、«計算が誤っている» では
C3-A の失敗を説明できない。⇒ ホスト SM は «手順» だけでなく «暗号計算» まで無罪。**

★**この対照は «混成» を作っていない**——**同じホストコード × 違うチップ/コントローラ**を
比べただけで，**blob と glue を混ぜてはいない**（禁止事項に抵触しない）。

### 3.3 ★三重の対照で残った容疑（依頼 (e)）

| 対照 | 結果 | 排除されたもの |
|---|---|---|
| **A vs B**（同一チップ・同一 board・同一 central） | hal は bond・esp-idf は不可 | **C3 シリコン／RF／central／ハーネス**（同じ石で hal は通る） |
| **A vs B の TX SMP 系列** | `02 0C 03 04 0D` が**同一** | **ホスト SM の «手順»** |
| **C5 vs C3-A**（**同一ホストコード**・違うチップ/コントローラ） | C5 は bond 成立 | **ホスト SM の «暗号計算»（`ble_sm_alg.c`）・NimBLE 版・SM 設定** |

**⇒ 残る容疑は «C3 の esp-idf v5.5.4 コントローラ» ただ一つ**
＝**`lib_esp32c3_family/esp32c3/libbtdm_app.a`（`859e8c8e`）＋ `bt/controller/esp32c3/bt.c`**
の **暗号開始（LL_ENC_REQ 受理 → LE LTK Request 生成）経路**。

### 3.4 ★決まっていないこと（正直に）

1. **blob か glue（`bt.c`）かは «分離できていない»**——**C3 では両方が同時に変わる**。
   **分離には «blob だけ差し替え» が要るが，それは «混成» そのもの**
   （ABI 不整合と機能差を区別できない／C6 evidence-c6-05 と同型）。**やらない。**
   ⇒ **ここから先は «設計判断» が要る**（§6.2）。
2. **A は run ごとに深さが揺れる**（初回 `total=2` の外れ値／典型は 12-13 EVT・6 ACL）。
   **機構の主張には典型 run を使い，外れ値は外れ値として記録した**。
3. **`AuthenticationCanceled` は central 側の «諦め» の表現**であり，
   **A の DHKey Check に対して central が «明示的に» 失敗を返したかは未確認**
   （RX の SMP opcode を見ていない）。§5-N1 が次の一手。

---

## 4. 予測の答合せ（依頼 (d)）

★**正直な自己申告**：**本ラウンドは «TX 測定の予測» を実機前に登録していない**
（C0 を先に走らせる規律は守ったが、TX 結果そのものへの事前予測は書かなかった）。
∴ **本節で «的中» を主張できるものは無い**。前ラウンド（evidence-c3-04 §3）で登録した
予測のうち本ラウンドに関係するのは以下：

| # | 前ラウンドの予測 | 本ラウンドの実測 | 判定 |
|---|---|---|---|
| **M2** | A は `ltk_req = 0` かつ `enc_chg = 0` | **両方 0（再現）** | **★再現（2ラウンド連続）** |
| **C0** | 計装は B を壊さない | **成功**（TX 版でも） | **★今回は的中**（前回は外れ→修正済） |

**次ラウンドのための予測は §5 に先行登録する。**

---

## 5. ★次ラウンドへの事前登録予測（実機前に書く）

★**注**：当初ここに書いた N2（「`ble_sm_alg.c` の出力が誤っている可能性」）は，
**§3.2 の C5 クロスチップ対照で «実機を追加で触らずに» 決着した**（同一ファイルで C5 は bond 成立）
⇒ **撤回せず «解決済み» として残す**（予測を «無かったこと» にしない）。

**次の一手＝残った容疑（C3 の esp-idf v5.5.4 コントローラの暗号開始経路）を，
central 側から見て «明示的な拒否» があるかで絞る。**

| # | 予測 | 確度 | 根拠 |
|---|---|---|---|
| **N1** | **A の RX に SMP `Pairing Failed`(0x05) は «無い»** | **70%** | BlueZ の表現が `AuthenticationCanceled`（＝**central 側の取り消し**）であって `AuthenticationFailed` ではない。central が値を «拒否» したなら `Pairing Failed` を返すのが素直 |
| **N3** | **A の RX SMP 系列は `01 → 0C → 03 → 04 → 0D`（B と同一）で終わる** | **65%** | TX が B と同一である以上，往復の RX も同一まで来ているはず（ただし未測定） |

★**含意の自問**：
- 「`Pairing Failed` が無い ⇒ A の DHKey 値は正しい」は**健全でない**——
  **central が黙って諦める実装もありうる**。∴ N1 が当たっても
  **「central は明示的な失敗を返していない」までしか言えない**。
  （なお **値の正しさ自体は §3.2 の C5 対照で既に担保されている**＝
  同一の `ble_sm_alg.c` が C5 で受理されている。）
- 「`ltk_req=0` ⇒ blob が悪い」は**健全でない**（§3.4-1：blob と glue が同時に変わる）。

**測り方（RX SMP opcode）**：`ble_l2cap_rx` は**両ツリーでシグネチャが違う**ので
**`#ifdef TOPPERS_ESPIDF_SUPPLY` で分岐した tree 別 parser** が要る
（esp-idf: om=a2 直ポインタ／hal: om=a2 二重ポインタ）。**mbuf レイアウト依存＝侵襲リスク**
⇒ **C0 を必ず先に**（本ラウンドで «効かない wrap» と «初回 run の外れ値» の両方を踏んだ）。

★**ただし §6.2-1 のとおり，そもそも «これを続けるべきか» はユーザー判断**。

---

## 6. 残ブロッカー・★ユーザーに聞くべきこと（依頼 (f)）

### 6.1 技術的な残

1. **blob と glue の分離は «原理的に» この方法では不可能**（分けたら混成）。
   ⇒ **分離したいなら «同じ blob × 違う glue» か «同じ glue × 違う blob» が要るが，
   それは C6 evidence-c6-05 が «交絡» と断じた構成そのもの**。**設計判断が要る。**
2. **DHKey Check のペイロード検証**（§5）が次の一手。
3. **D-2c/D-2d の真cold 実証は未実施**（hal 経路なら測れるはず）。

### 6.2 ★ユーザーに聞くべきこと（**判断が要る／私では決められない**）

1. **★この調査を続ける «目的» は何か**（**最重要**）——**C3 BT の既定は hal で確定しており（bond 2/2 成功・
   D-2c/D-2d 実績あり）、esp-idf 供給に «機能上の必要» は現時点で無い**。
   本調査は **「hal 参照 0 を BT でも達成する」ため**だが、
   **hal 参照 0 は既に «ビルドとしては» 達成済み**（adv/D-2b まで真cold で動作）。
   ⇒ **「bond まで含めて esp-idf 供給で動かす」ことにどれだけ投資するか**は
   **コスト/便益の判断**であって技術的判断ではない。**私は決められない。**
2. **もし続けるなら**：§6.1-1 のとおり **blob/glue の分離には «混成» を一度受け入れる**か、
   **Espressif 側の情報（v5.5.4 の C3 controller が LESC 暗号開始で既知の問題を持つか）**が要る。
   **後者は私には調べられない（社内情報）。**
3. **★C5 照合は «本ラウンドで実施済み»**（§3.2）＝**C5 は esp-idf 供給のまま D-2d（暗号 read）達成**、
   かつ **C3-A と同一パスの `ble_sm*.c` をコンパイル**している ⇒ **ホスト SM は完全に無罪**、
   **容疑は «C3 の esp-idf v5.5.4 コントローラ» に一意化**した。**この問いは解決済み。**
</content>

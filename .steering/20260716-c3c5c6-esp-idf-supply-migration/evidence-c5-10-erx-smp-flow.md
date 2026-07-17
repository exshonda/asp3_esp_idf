# evidence-c5-10 — E-RX：C5 × Android の失敗時に «SMP PDU はそもそも流れているか»（測るだけ・修正はしない）

**作成**: 2026-07-17 ／ **担当**: E-RX 専任エージェント
**ミッション**: Phase 1 E-RX（`PLAN-next-phase.md`）＝**bond 失敗時の SMP 往復の可視化**。
**修正は書かない。** 他エージェントの evidence/review/PLAN は**読むだけ**。

---

## 1. Step 1：計器の同定（★実測・憶測なし）

### 1.1 `ESP32C5_BT_RXTRACE` の実体（既定 OFF）

**実装**: `asp3/target/esp32c5_espidf/bt/rx_trace.c`（本リポジトリ側＝submodule ではない）
**配線**: `asp3/target/esp32c5_espidf/esp_bt.cmake:505-516`（拡張前）

| wrap 対象 | 呼出し元（クロスTUか） | 数えるもの |
|---|---|---|
| `ble_hs_hci_evt_process` | `ble_hs.c`（クロスTU） | 全 HCI EVT。`0x08`=Encryption Change で enc ゲートを立てる |
| `ble_sm_enc_change_rx` | `ble_hs_hci_evt.c` のテーブル（クロスTU・関数ポインタ） | SM 層への Enc Change dispatch |
| `ble_mqueue_put` | `ble_hs.c:849`＝`ble_hs_rx_data`（クロスTU） | controller→host ACL が rx_q へ届いた数 |
| `ble_sm_tx` | ★下記 1.3 | 我々の SM の SMP 送出（**盲点あり**） |
| `ble_transport_to_ll_acl_impl` | `ble_hs.c:880` の `ble_transport_to_ll_acl` は `transport/monitor.h:57-59` の **inline** で `_impl` を呼ぶ＝各呼出し元 TU から `_impl` への undefined 参照（クロスTU） | host→controller ACL |

**記録先＝STORE3（`0x600B100C`）**：`[31:28]enc_chg [27:24]sm_enc_rx [23:16]put [15:8]sm_tx [7:0]to_ll`。
★**put/sm_tx/to_ll は «enc_chg!=0 でゲートした暗号後デルタ»**（`rx_trace_pack()`：`base = enc_chg!=0`、
base=0 なら 3 フィールドとも 0）。

**C3 の前例との関係**：C3 は `evt_trace.c`（RX 側のみ）。C5 の rx_trace.c はその改良版（双方向）で、
**D-2d（`docs/bt-shim.md:2646-2678`）で実際に非0 を返した実績がある**
（`enc_chg=1・sm_enc_rx=1・put=1・sm_tx=0・to_ll=1`）＝put/to_ll/evt_process/enc_change_rx の
4 wrap は**歴史的に較正済み（非0 を出したことがある）**。

### 1.2 ★そのままでは E-RX に使えない理由（本ラウンドの拡張の根拠）

**現病態は «暗号前»**：`ENC=0x5de00007`＝`ble_sm_connection_broken()`＝HCI Encryption Change は
**来ない**⇒ `enc_chg==0` ⇒ **STORE3 の put/sm_tx/to_ll フィールドは構造的に常に 0**。
⇒ **「0 を読んだ」が «計器のゲートが閉じている» と «本当に流れていない» を区別できない
＝ over-determined**（C5 `0x5DC0`・C6 `0xABF3` と同じ型）。**D-2d の問い（暗号後）用の計器であり、
E-RX の問い（暗号前）には «そのままでは» 盲**。

### 1.3 ★★発見：`--wrap=ble_sm_tx` は «ble_sm.c 内部の 9 呼出し点に噛まない»（現行バグではなく計器の盲点・潜在）

GNU ld の `--wrap` は **undefined 参照のみ**を差し替える。`ble_sm_tx` は `ble_sm.c` で定義され、
**同一 TU 内から 9 箇所**（`ble_sm.c:945`[Pairing Failed]・`:1908`・`:2175`[Pairing Response 系]・
`:2358-2498`[鍵配布 `ble_sm_key_exch_exec`]・`:3384`）で呼ばれる＝**これらは wrap されない**。
噛むのは **クロスTU の 6 箇所のみ**（`ble_sm_lgcy.c:151,215`・`ble_sm_sc.c:408,485,652,839`）。

⇒ **含意（過去記録への影響・事実と推測を分けて書く）**：
- **事実**：D-2d の決定的マーカ「`sm_tx=0`」が測っていたのは «クロスTU 6 箇所» のみで、
  **鍵配布（`ble_sm.c:2358-2498`）はもともと観測不能**だった。
- **事実**：D-2d の結論（PVCY=0 が真因）は `gcc -E` での実効値確認＋PVCY 有効化で bond 成立、
  という**独立証拠で確定しており、揺らがない**。
- **推測**：`sm_tx=0` の読みは「たまたま正しい向きを指した over-determined な 0」だった可能性が高い。
- **本ラウンドの設計への反映**：我々の SMP 送出は SM 層でなく
  **`ble_transport_to_ll_acl_impl`（全 host TX の漏斗・クロスTU 実証済）で L2CAP CID を覗いて数える**。

### 1.4 拡張（E-RX 計器）＝«暗号前» の SMP 生カウンタ・タグ付き

**変更ファイル（全列挙・submodule は 1 行も触っていない）**：

| ファイル | 変更 |
|---|---|
| `asp3/target/esp32c5_espidf/bt/rx_trace.c` | E-RX セクション追加：SMP(CID=0x0006) 判定 `erx_acl_is_smp_first()`（`os_mbuf_copydata` 8B・passive）、カウンタ 3 本、`erx_pack()`→STORE4、新 wrap `__wrap_ble_l2cap_rx` |
| `asp3/target/esp32c5_espidf/esp_bt.cmake` | RXTRACE ブロックへ `-Wl,--wrap=ble_l2cap_rx` 追加＋**RXTRACE×PEND_DIAG 同時 ON を FATAL**（STORE4 二重書込み防止） |
| `apps/ble_host_smoke_c5/ble_host_smoke_c5.c` | `storm_monitor_task` の STORE4 旧ミラー（`int_count[1]`＝常に 0 と実測済）を RXTRACE=ON 時に停止＝**1 レジスタ 1 書き手** |

**覗きの根拠（v5.5.4 submodule ソースで確認・読んだだけ）**：
- `ble_mqueue_put` 時点の om は **HCI ACL ヘッダ付き**（strip は後段 `ble_hs_hci_evt_acl_process`＝
  `ble_hs_hci_evt.c:1943` の `data_hdr_strip`）。RX first PB=2（`BLE_HCI_PB_FIRST_FLUSH`、
  `hci_common.h:2842-2844`）。CID＝byte6|7。
- `ble_transport_to_ll_acl_impl` 時点の om は **HCI ACL ヘッダ付き**（`ble_hs_hci.c:829-859`
  `ble_hs_hci_acl_hdr_prepend` が先に付ける）。TX first PB=0。CID＝byte6|7。
- `ble_l2cap_rx(uint16_t conn_handle, uint8_t pb, struct os_mbuf *om)`（この v5.5.4 tree の
  シグネチャ・`ble_l2cap.c:348`）＝HCI ヘッダ strip 済・CID＝byte2|3（pb=2 のときのみ L2CAP ヘッダ）。
  呼出し元 `ble_hs_hci_evt.c:1968`＝**クロスTU＝wrap 可（逆asmで確認する・§3）**。
- 継続フラグメント（PB=1）は CID を持たない＝数えない＝**カウント単位は «L2CAP PDU 数»**
  （SMP PDU は SC Public Key 含め first に L2CAP ヘッダが乗る）。

**記録先＝STORE4（`0x600B1010`）**：
```
STORE4 = 0xE2 << 24 | smp_rx_put(8) << 16 | smp_rx_l2(8) << 8 | smp_tx_ll(8)   （各飽和 255）
```
- **タグ `0xE2`**＝E-RX（E1 の `0xE1` と判別可能）。**全 wrap から無条件書込み**＝
  HCI evt が 1 個でも流れれば立つ＝**«wrap が噛んで走った» ことの生存証明**（0x00000000 と判別）。
- **STORE4 選定の判断（ミッション要求の明記）**：E1 計器（PEND_DIAG）は `evidence-c5-09` で
  **完結済（used=0 で①死亡）＝上書きしてよい**。同乗は 32bit に収まらないため**上書きを選択**。
  本ビルドは `ESP32C5_BT_PEND_DIAG=OFF`＋cmake FATAL で二重書込みを機械的に排除。
  STORE6 は ENC と共用＝触らない。STORE0/2/5/8/9＝証拠、1＝RTC cal 予約、7＝bt_shim 予約。
- **hot path 負荷**：per-ACL で 8B の `os_mbuf_copydata`＋比較＋LP_AON 書込み 2 本
  （STORE3 既存＋STORE4）。dump なし。20語 dump 事故（evidence-c3-04）とは 2 桁違う軽さ——
  ただし**「軽いはず」は主張しない。C0（§4）で検定する**。

### 1.5 判定表（読み方・測定前に固定）

| smp_rx_put | smp_rx_l2 | smp_tx_ll | 意味 |
|---|---|---|---|
| 0 | 0 | 0（タグあり） | **SMP が電波上に無い**（ただし §2 の R0 の限界を参照） |
| >0 | 0 | — | **rx_q に入ったが host タスクが降ろしていない**＝RX 配送の内側（eventq/OSAL） |
| >0 | >0 | 0 | **受けて dispatch したが我々は 1 個も SMP を出していない**＝SM 実行 or TX 経路 |
| >0 | >0 | >0 | **双方向に流れている**＝カウント値で «どの段で止まったか» を BlueZ 成功プロファイルと比較 |

---

## 2. Step 2：事前登録（★測定前に commit・以後書き換えない）

### 2.1 分岐と確率（PLAN の 3 分岐＋排他的に細分）

前提条件：タグ `0xE2` が読めること（読めなければ**測定不成立＝結果を語らない**）。

| # | 実測パターン | PLAN 分岐 | 登録確率 | 嫌疑（外れた時の意味も含む） |
|---|---|---|---|---|
| **R0** | put=0 ∧ l2=0 ∧ txll=0 | (b) の変種 | **15%** | **SMP 以前に切断**（LL/GAP 層 or Android が SMP 前に降りた）。★この 0 は «計器が盲» と区別が要る＝**C0 較正（BlueZ で 3 カウンタ非0）が成立して初めて「流れていない」と言える**。較正が立たなければ**判定不能と書く** |
| **R1** | put>0 ∧ l2=0 | **(c) 来たが処理していない** | **8%** | **RX 配送の内側（shim/OSAL の eventq drain）**。★iPhone/BlueZ が通る事実と整合させるには「Android 特有の PDU パターン（連発・フラグメント）でのみ露呈」が必要＝それも記録する |
| **R2** | put>0 ∧ l2>0 ∧ txll=0 | **(a) 送っていない** | **12%** | **SM が応答を生成しない or L2CAP→transport 間の TX 経路**。同じく Android 特異性の説明が必要 |
| **R3** | put>0 ∧ l2>0 ∧ txll>0 | (a)(b)(c) の**どれでもない**＝**双方向に流れて途中で死ぬ** | **60%** | **SMP 手順の «段» で死ぬ**（feature 交渉・crypto 値・我々の Pairing Failed 送出等）。カウント値と BlueZ プロファイルの比較で段を局在化。**«即» エラー＋DISC reason=0x13（Android が切った）と最も整合**（待ちの署名なしで能動的に降りるのは、拒絶 PDU を見た側の挙動） |
| **R4** | txll>0 ∧ put=0 | (b) 送ったが応答が来ない | **5%** | 我々が先に送った（app の 5s Security Request）のに peer の SMP が 1 個も来ない＝RX 配送 or 無線側。«即» エラーとは整合しにくい＝低確率 |

**R3 の副予測**：txll==1（Pairing Response のみで peer が降りる）＝20%／txll≥2（もっと進む）＝40%。

**★「iPhone/BlueZ は通るのに Android は落ちる」への自問（各分岐）**：R1/R2 は
「配送機構の全損」では既知（成功セルの存在）と矛盾する＝**成立するなら «Android 特有の負荷・
パターン依存» の形のみ**。R3 は Android 固有の feature 交渉（CTKD/LinkKey bit・IO caps・MITM 等）
が最も自然に説明する。R0/R4 は «即» の観測と噛み合いが悪い＝低確率。

### 2.2 計器そのものの事前予測

| # | 予測 | 登録値 |
|---|---|---|
| **P_C0** | 計装込みビルドで C5 × BlueZ が bond する（＝計装は非破壊） | **80%** |
| **P_TAG** | タグ `0xE2` が読める（wrap 生存） | **90%** |
| **P_CAL** | BlueZ 成功セルで **3 カウンタすべて非0**（＝陽性対照成立） | **85%** |
| **P_BASE** | 失敗セルで STORE6/8/9 が既知ベースライン（`0x5de00007`・CONN ok・DISC 0x13）と一致（＝計装が病態を変えない） | **85%** |

**★E1 の教訓の適用（P_USED 70% を外した前例）**：カウンタごとに「較正が立たなかったらその
カウンタの 0 は語らない」を個別に適用する（3 本まとめて生死判定しない）。

### 2.3 射程（超えて書かない）

- 語れるのは **軸 A（C5 × Android が bond に到達しない）のみ**。軸 B/C へ外挿しない。
- 本計器は **SMP の «個数» のみ**。中身（opcode・reason）・時刻は持たない＝
  「誰が・なぜ切るか」に**個数だけで答えられない場合は «答えられない» と書く**。

---

## 3. 計器の検定（ビルド・逆アセンブル）＝測定前・§2 は書き換えていない

### 3.1 ★★§1.3 の «訂正»（★消さずに残す＝辻褄合わせをしない）

**§1.3 の「`--wrap=ble_sm_tx` は ble_sm.c 内部の 9 呼出し点に噛まない」は
逆アセンブルの実測で «誤り» と判明した。**

| 実測（`build/erx_c5_rxtrace/asp.elf`） | 値 |
|---|---|
| `__wrap_ble_sm_tx` への呼出し | ★**15 箇所**——**ble_sm.c 内部の `ble_sm_key_exch_exec`（5）・`ble_sm_pair_exec`（2）・`ble_sm_sec_req_exec`・`ble_sm_process_result` を含む** |
| 実体 `ble_sm_tx`（`42013b2e`）への jal | ★**1 本のみ＝`__wrap_` 内の `__real_` 戻り（`4200588a`）** |

⇒ **この toolchain（esp-14.2.0 の binutils）では --wrap は同一TU参照にも噛む**。
私の §1.3 は「GNU ld は undefined 参照のみ差し替える」という**文書知識からの推定**で、
**実測が推定を否定した**（「マクロ名/仕様書から推測せず実物で確かめる」の型そのもの）。
- **含意の訂正**：D-2d の歴史的マーカ `sm_tx=0` は**盲ではなく真の測定だった**可能性が高い
  （同系 toolchain なら鍵配布の呼出し点にも噛んでいた）。§1.3 に書いた「たまたま正しい向きを
  指した over-determined な 0」という推測は**撤回**する。
- **設計への影響**：無し（SMP TX の一次計数は元々 `to_ll_acl_impl` の CID 覗きに置いた＝
  SM 層のどこから出ても数える。`ble_sm_tx` wrap は今や «SM 層の全 TX» の副計数として有効）。
- **§2 の事前登録（R0-R4・確率）は 1 文字も書き換えていない**（この訂正は分岐定義に影響しない）。

### 3.2 wrap が «噛んだ» ことの逆アセンブル証明（6/6・全呼出し元の同定つき）

`build/erx_c5_rxtrace/asp.elf`（RXTRACE=ON・PEND_DIAG=OFF・SM=ON・IDF_STD_ISA=ON・
v5.5.4 supply・toolchain `esp-14.2.0_20260121`）：

| wrap | 呼出し元（実測） | real への参照 |
|---|---|---|
| `ble_hs_hci_evt_process` | `ble_hs_event_rx_hci_ev`（`4200c9e0` j `__wrap_`） | `__real_` の 1 本のみ |
| `ble_sm_enc_change_rx` | `ble_hs_hci_evt_encrypt_change`（`4200e1e6` jal `__wrap_`）＝**テーブルではなく直呼び**（rx_trace.c の旧コメント「テーブルから」は不正確）。データ節に real 番地は**ロード対象セクションには 0 件**（一致は全て debug 節） | `__real_` の 1 本のみ |
| `ble_mqueue_put` | `ble_transport_to_hs_acl_impl`（`4200cf0a`・`ble_hs_rx_data` は inline 化で吸収）＝**呼出し元は全 ELF でこの 1 箇所＝rx_q 専用**の傍証 | `__real_` の 1 本のみ |
| ★**`ble_l2cap_rx`（新設）** | `ble_hs_hci_evt_acl_process`（`4200e9be` jal `__wrap_`） | `__real_` の 1 本のみ |
| `ble_sm_tx` | 15 箇所（§3.1） | `__real_` の 1 本のみ |
| `ble_transport_to_ll_acl_impl` | `ble_hs_tx_data`（`4200cdfe` j `__wrap_`） | `__real_` の 1 本のみ |

**wrap 内部の実装確認（逆asm）**：`__wrap_ble_l2cap_rx`／`__wrap_ble_mqueue_put` に
`lui 0x600b1`（STORE4）・`lui 0xe2000`（タグ）・`jal r_os_mbuf_copydata` を確認。
（`os_mbuf_copydata` は porting ヘッダで `r_os_mbuf_copydata` へ #define されており実体は後者＝
リンクエラーで発覚し修正。ELF に `T r_os_mbuf_copydata` 実在。）

### 3.3 ゲートの完全性（陰性対照）

| ビルド | rc | FLASH | `__wrap_`/`g_erx_` シンボル |
|---|---|---|---|
| `build/erx_c5_rxtrace`（ON） | 0 | 418496 B | あり（§3.2） |
| `build/erx_c5_off`（OFF） | 0 | 417600 B | ★**0 個**＝ゲート完全 |

★ON−OFF＝**+896 B**（wrap 6 本＋CID 判定＋pack）。
★**vintage 注記（no silent caps）**：OFF ビルド 417600 B は E1 の OFF（417696 B）と**一致しない**
（本ツリーは e17de26＝app の STORE4 ゲート追加後）。静的一致による非回帰主張はせず **C0 実機で検定**。

## 4. C0（BlueZ・健全セル）＝ ★★**PASS（2/2・真cold）＋陽性対照 3/3 成立**

**ハーネス**：E1 と同一の確立済み D-Bus 直叩き（`ble_c0.py`・`NoInputNoOutput` agent 自己登録・
毎回 `remove` でフレッシュ bond）。**flash**：`build/erx_c5_rxtrace/asp_flash.bin` を
by-id（`D0:CF:13:F0:A7:44`）へ `--chip esp32c5`・`Hash of data verified`。

| run | 真cold の証明 | 開始時 | bond | `0xABF4` |
|---|---|---|---|---|
| 1 | `jtag=GONE cp2102n=GONE`（by-id 読み戻し） | `paired=False bonded=False` | ★**`paired=True bonded=True`** | ★**`b'BT4-OK'`** |
| 2 | `jtag=GONE cp2102n=GONE` | `paired=False bonded=False` | ★**`paired=True bonded=True`** | ★**`b'BT4-OK'`** |

⇒ ★**P_C0 的中（計装は健全セルを壊さない）**。

### 4.1 ★★較正（陽性対照）＝ **3 カウンタすべて非0**（P_CAL 的中）

**マーカー実測（run1・run2 とも «全レジスタ byte 一致»）**：

| reg | 値 | 読み |
|---|---|---|
| STORE0 | `0x5ade51c0` | SYNC 到達 |
| STORE2 | `0xad000000` | adv rc=0 |
| ★**STORE3** | ★**`0x11010203`** | **RXTRACE 原機能も較正**：enc_chg=1・sm_enc_rx=1・暗号後 put=1/sm_tx=2/to_ll=3（鍵配布 2 PDU＝Identity Info/Addr と整合） |
| ★★**STORE4** | ★★**`0xe2040407`** | **tag=0xE2（wrap 生存）・smp_rx_put=4・smp_rx_l2=4・smp_tx_ll=7** |
| STORE5 | `0x00000000` | WRITE 未実施（ハーネスは書かない＝想定どおり） |
| STORE6 | `0x5de00000` | ENC status=0・delta_s=0（暗号成立・即時） |
| STORE8 / STORE9 | `0x604e0001` / `0xd15c1301` | CONNECT ok・切断はハーネス側（0x13） |

★**カウント値は SC responder のプロトコル構造と完全整合**（値の «もっともらしさ» でなく構造で検算）：
- **RX 4 個**＝Pairing Request・Public Key・Pairing Random・DHKey Check（central→peripheral の SC 全 4 PDU）
- **TX 7 個**＝Pairing Response・Public Key・Confirm・Random・DHKey Check（暗号前 5）＋
  Identity Info・Identity Address（暗号後 2＝**STORE3 の sm_tx delta=2 と独立に一致**）
- **put==l2（4=4）**＝rx_q へ入った SMP は全て dispatch された（健全時の基準値）

⇒ ★★**3 本のカウンタは «必ず通る» 経路で全て非0 を実測済**＝**失敗セルの «0» は «盲» ではなく
«流れていない» と読んでよい**（各カウンタ個別に較正成立）。

### 4.2 freshness（残留の制御）

- **E1 計器の旧値 `0xe1000000` → 本ラウンド `0xe2040407`＝«タグの遷移» が今ブートの書込みを直接証明**
  （第9再発の型：証拠は «存在» でなく «遷移»）。
- 真 POR が LP_AON を消すことは同一個体で本日 sentinel 実測済（`evidence-c5-09 §8.4 c1`）。
- カウンタ実体は `.bss`／`g_erx_*`（`408008e0-e8 B`）＝毎ブート 0 初期化＝前ブート値の混入は構造的に不可能。

## 5. 失敗セル（C5 × Android・ユーザー実施）＝ **準備完了・ユーザー待ち**

### 5.1 セルの状態（実測）

| 項目 | 実測 |
|---|---|
| 個体 | `d0:cf:13:f0:a7:44`（MAC で同定・by-id でのみ操作） |
| ビルド | `build/erx_c5_rxtrace`（RXTRACE=ON・PEND_DIAG=OFF・SM=ON・arm B toolchain `esp-14.2.0_20260121`） |
| ★1セル1ボード | C3（port1）・C6（port2）電源断＝`Port 1: 0000 off`/`Port 2: 0000 off` 読み戻し＋**by-id 3 本とも GONE**＋**スキャンで他の `ASP3-*`/`IDFCTL-*` 不在を実測**（スキャナ較正＝5 デバイス見えている） |
| ★真cold | `-p 3-4` off→`jtag=GONE cp2102n=GONE` 読み戻し→on。**広告 `ASP3-C5-BLE` をスキャンで実測** |
| BlueZ | DUT の device object を `remove`・`Discovering: no`・接続 0＝セルを奪わない |
| hub 1-5 | 一切触っていない |

### 5.2 ユーザー手順（★Android のみ・iPhone は触らない）

1. ★**Android と iPhone の «両方» で `ASP3-C5-BLE` を「登録解除（forget）」する**
   （bond は RAM backed＝真cold でデバイス側の鍵は消えている。片側の古い bond が残ると
   9時間型の事故になる。iPhone を忘れさせるのは «勝手に繋いでセルを消費させない» ため）。
   その後 **両方の端末で BT を OFF→ON**。
2. **Android の Bluetooth 設定から `ASP3-C5-BLE` をペアリングする。**
3. **観測を逐語で報告**：ペアリング要求ポップアップが出たか／「登録済み」になったか
   エラーか／切れたのは «すぐ» か «数秒» か «30秒くらい» か（体感で結構）。
4. ★**iPhone は触らない**（カウンタは累積＝2 端末を同一ブートで混ぜない）。

★**エージェントはユーザーが試す «前» にマーカーを読まない**（read-mem は download mode に
落として広告が止まる）。**報告を受けてから** STORE0-9 を読み、**§2 の分岐をそのまま適用**する。

### 5.3 ユーザー観測（逐語・私の解釈と別に記録）

> 「**Android : ペアリング要求がポップアップ -> 即エラー．登録されない**」

★「即」は体感＝計器ではない（秒数として記録しない）。

### 5.4 マーカー実測（全 10 本）

| reg | 値 | 意味 |
|---|---|---|
| STORE0 | `0x5ade51c0` | SYNC 到達 |
| STORE1 | `0x00000000` | （RTC cal 予約） |
| STORE2 | `0xad000000` | adv rc=0 |
| ★STORE3 | ★`0x00000000` | RXTRACE 原パック＝**enc_chg=0・sm_enc_rx=0**（wrap は生存＝STORE4 タグが証明。**HCI Encryption Change は一度も来ていない**）／on_reset 不発 |
| ★★**STORE4** | ★★**`0xe2000003`** | **tag=0xE2（測定有効）・smp_rx_put=0・smp_rx_l2=0・smp_tx_ll=3** |
| STORE5 | `0x00000000` | WRITE 未到達 |
| ★STORE6 | ★`0x5de0ff07` | ENC_CHANGE **status=7**（`BLE_HS_ENOTCONN`）。★byte1=`0xff`＝RXTRACE の `delta_s` フィールド（`g_rx_tick - g_rx_enc_tick` 飽和）。**enc_tick が一度もスナップショットされていない**（=enc 未到来）ことの独立証拠＝STORE3 の enc_chg=0 と 2 系統一致 |
| STORE7 | `0xa1020704` | bt_shim トレース（予約） |
| STORE8 | `0x604e0003` | CONNECT status=0・**count=3**（3 回試行） |
| STORE9 | `0xd15c1303` | DISCONNECT reason=0x13（最終値）・count=3 |

### 5.5 ★失敗側 C0 ＝ **PASS（計装は病態を変えていない）**

| 指標 | 既知ベースライン（`evidence-c5-08 §8.1`） | 本セル | 判定 |
|---|---|---|---|
| ENC status | `0x5de00007`（status=7） | `0x5de0ff07`＝**status=7** | ★**一致**。byte1 の `00`→`ff` は**計装のエンコード差**（RXTRACE=ON のときのみ delta_s が載る・§5.4）＝病態の変化ではない。**この差は隠さず明記する** |
| DISC reason | `0x13` | `0x13`（最終値） | ★一致 |
| CONNECT status | `0x00` | `0x00` | ★一致 |
| WRITE | `0x00000000` | `0x00000000` | ★一致 |
| CONN/DISC count | 2/2（E1 セル） | **3/3** | 差＝セッション依存（試行回数）＝署名ではない |

⇒ **計装込みでも病態は同一＝E-RX の測定は有効**（P_BASE 的中）。
★限界：STORE9 の reason は**後勝ち**＝「3 回とも 0x13」は証明できない（最終回が 0x13・count=3 のみ）。

### 5.6 ★★★一次判定＝**事前登録 R4 が起きた**（登録 5%・本命 R3=60% は外れ）

**`STORE4 = 0xE2_00_00_03` ⇒ smp_tx_ll=3 ∧ smp_rx_put=0 ∧ smp_rx_l2=0 ＝ R4 «txll>0 ∧ put=0»。**

**正規化（ソースで裏取り）**：`bt5_security_tick`（`ble_host_smoke_c5.c:818-838`）は
**接続 5 秒後に 1 回だけ** slave Security Request を送る（`g_sec_initiated` フラグ）。
**TX 3 ÷ 3 接続 = 1/接続**＝**Security Request のみ**。put=0 が「Pairing Response を送った」可能性を
構造的に排除する（Response は Request の受信が前提）。

| | C0（BlueZ・成功） | 失敗セル（Android）・1接続あたり |
|---|---|---|
| 我々の SMP TX（to_ll） | **7**（Pairing Response・PubKey・Confirm・Random・DHKey＋鍵配布2） | **1**（**Security Request のみ**） |
| 対向の SMP → rx_q（put） | **4**（Pairing Request・PubKey・Random・DHKey） | ★**0** |
| rx_q → dispatch（l2） | **4**（=put） | **0**（=put） |
| HCI Enc Change | 1 | **0** |

⇒ **SC 期待系列に対する進行度＝«0 段目»**：**ペアリング手順は 1 PDU も始まっていない。**
**我々は SecReq を（3 接続とも）controller へ渡したが、対向の SMP は 1 個も host に現れなかった。**

★★**副次の含意（カウンタから確実に言える）**：**3 接続とも ≥5 秒生存し、その時点で未暗号だった**
（SecReq は「接続 5 秒後・未暗号」でのみ発火するコードパス）。
＝**「接続が即切れた」のではない**。切断は SecReq 送出**後**。

### 5.7 事前登録予測の全記録（★一つも書き換えていない）

| # | 登録値（`e17de26`） | 結果 |
|---|---|---|
| R0=15%／R1=8%／R2=12%／**R3=60%**／R4=5% | — | ★★**R4（5%）が起きた**＝**私の本命 R3 は外れ** |
| P_C0=80% | — | 的中（§4） |
| P_TAG=90% | — | 的中（0xE2） |
| P_CAL=85% | — | 的中（3 カウンタとも C0 で非0） |
| P_BASE=85% | — | 的中（§5.5・byte1 はエンコード差として説明） |

★**矛盾はそのまま書く**：私は R4 の登録時に「«即» エラーとは整合しにくい＝低確率」と書いた。
**実測は R4 で、ユーザー観測はまた «即»**。⇒ **この緊張は未解決のまま報告する**
（可能な解消は「popup と即エラーは Android 側の内部遷移で、SMP 往復の不在と両立する」だが
**Android 側の記録なしには決められない**＝§5.9 提案 1）。

### 5.8 ★言えること／言えないこと

**言えること（実測）**：
1. **SMP は双方向に流れていない**。我々→対向は Security Request 1 個/接続のみ（controller への
   受け渡しまで実測）。**対向→我々の SMP は 0 個**——rx_q 投入点（`ble_mqueue_put`）にすら現れない。
   **較正済**（同一計器が BlueZ 成功セルで 4/4/7 を数えた＝«盲» ではない）。
2. **我々の host スタック（`ble_mqueue_put` より上）は何も取りこぼしていない**——
   **そもそも何も入っていない**（put=0 ∧ l2=0）。E1 の「pend_ring に何も入らない」と同型の絞り込みで、
   **嫌疑の境界は host ソフトの «下»（controller→host 受け渡し・controller/LL・電波・または Android が
   送っていない）へ移った**。
3. HCI **EVT** 経路は生きている（CONNECT/DISCONNECT イベントは 3 回とも届いた）。
4. 3 接続とも ≥5 秒生存・未暗号（§5.6）。
5. 病態は計装の有無で不変（§5.5）。

**言えないこと（計器の射程外＝正直に）**：
1. ★**「なぜ切れるか」**——opcode/reason 計器なし。**答えられない。**
2. ★**put=0 の内訳**——3 通りを判別できない：
   (i) Android は Pairing Request を**送ったが電波/LL で失われた**／
   (ii) **controller は受けたが host への受け渡し（`host_rcv_pkt`→alloc→`ble_transport_to_hs_acl`）で
   落ちた**（★`docs/bt-shim.md:2637` が名指しした**未計装ギャップそのもの**）／
   (iii) **Android がそもそも送っていない**（popup 後・送信前に自側で abort）。
3. ★**失敗セルで ACL RX が «全く» 無かったのかは不明**——本計器は SMP（CID=6）のみ。
   ATT（GATT discovery）の RX が流れたかは**ミラーしていない**（`g_rx_put` 総数は RAM のみ＝
   read-mem では回収不能）。**設計の抜け＝次ラウンドでミラーすべき**。
4. 我々の SecReq が**電波に出たか**（to_ll は controller への受け渡しまで）。popup が SecReq への
   反応か Android 自発かも不明。

**iPhone/BlueZ との整合（R4 の形で自問）**：「RX 全損」なら iPhone/BlueZ も落ちるはず＝既知と矛盾。
⇒ R4 が成立するのは **«Android 系列でのみ» 対向 SMP が host に届かない**形に限る：
(iii) なら原因は「Android が我々の何かを見て送るのをやめる」＝**我々の統合の観測可能な差
（app/GATT/adv/起動）への反応**として stock との差に帰着し得る。(i)(ii) なら Android 固有の
LL 手順（接続直後の PHY update/DLE 等）との相互作用という**仮説**になる。**どれかは未決**。

### 5.9 ★次の一手（提案のみ・実装しない・ユーザー判断）

| # | 提案 | 費用 | 判別力 |
|---|---|---|---|
| **1** | ★**Android の HCI snoop log**（開発者向けオプション→再現→bugreport で btsnoop 回収） | **最小**（ビルド不要・ユーザー操作のみ） | ★**最大**：(iii)「送っていない」を一発で判別。送っていれば「何を送り・何を受け・なぜ切ったか（Disconnect 理由・SMP Fail の有無）」まで全部見える。**唯一、対向側の視点を与える手** |
| **2** | **RX 入口計器**：`ble_transport_to_hs_acl`（クロスTU 要逆asm確認）を wrap し «全 ACL RX 総数» と «SMP 数» を分離ミラー（STORE4 の再パック or STORE3 相乗り） | ビルド1本＋セル1回 | (ii) host 受け渡し落ち vs (i)(iii) controller から何も来ない、を分離。**§5.8-言えないこと 3 の抜けも同時に閉じる** |
| **3** | stock bleprph × Android で **snoop log を対照採取**（stock は成功する＝成功系列の雛形） | ユーザー操作のみ | 提案 1 と対で「Android が何を見て挙動を変えるか」の差分が出る |

**推奨順**：1（→必要なら 3）→ 2。**修正はまだ書かない**（機序が (i)/(ii)/(iii) のどれかで対処が全く違う）。

### 5.10 未測定・取らなかった対照（no silent caps・最終形）

- **opcode/reason/時刻**＝計器に無い（設計どおり・§2.3 で自認済）。
- ★**ACL RX 総数（CID 不問）を LP_AON にミラーしていない**＝失敗セルで「ATT は流れたか」不明。
  **本ラウンドの設計の抜け**（提案 2 で閉じる）。
- **PB=1（継続フラグメント）は CID 不可視**＝カウント単位は L2CAP PDU 数（SMP は単フラグメント＝影響なし。C0 の 4/4/7 整合が傍証）。
- **iPhone C0 未実施**（BlueZ で成立。RPA セントラルでの計装無害性は失敗側 C0 §5.5 で補完）。
- **STORE9 reason は後勝ち**＝3 回全部が 0x13 かは未証明。
- **Android セルは 1 セッション（3 接続）**。カウンタは 3 接続の合算（1/接続は SecReq コードパスの構造から導出＝直接の per-connection 計測ではない）。
- **我々の SecReq の電波送出**＝未測定（to_ll まで）。

### 5.11 ベンチ復旧（実測）

C3（port1）・C6（port2）再給電・C5 電源サイクル（download mode 解除）→
**3 ボードとも by-id PRESENT・hub 1-6 全ポート `power enable connect` を読み戻し**。hub 1-5 不触。

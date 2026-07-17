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

---

## 6. snoop セル（提案 1 採用・Galaxy Z Fold 7）＝ **準備完了・ユーザー待ち**

### 6.1 セルの状態（実測・2026-07-18）

| 項目 | 実測 |
|---|---|
| ビルド | ★**E-RX ビルドのまま再ビルド/焼き直し «していない»**＝`verify-flash` で **`Verification successful (digest matched)`**（`build/erx_c5_rxtrace/asp_flash.bin` と全 4MB 一致）⇒ **snoop（スマホ視点）とデバイス側カウンタ（我々視点）の «両側観測» セル** |
| マーカー | STORE0,2-9 を 0 クリア→**読み戻しで ALL ZERO 検証**（STORE1=RTC cal は不触） |
| 1セル1ボード | C3/C6 電源断＝`Port 1/2: 0000 off`＋by-id 3 本 GONE 読み戻し＋**スキャンで ASP3-C3/C6 不在を実測**（スキャナ較正=5 デバイス） |
| 真cold | `-p 3-4` off→`jtag=GONE cp2102n=GONE` 読み戻し→on→**`ASP3-C5-BLE` 広告をスキャンで実測** |
| BlueZ | device `remove`・`Discovering: no`・接続 0 |
| PC 側取り出し | ★**adb は導入済みで動作**（`/usr/bin/adb`・v34.0.4-debian。初回 `which adb` が空だったのは誤り＝`dpkg -L`/`type` で訂正）。sudo 不要。**tshark/wireshark は無い＝python 自作パーサで読む**（btsnoop 形式は単純）。**予備経路（adb 不調時）**：Samsung SysDump（`*#9900#`→Copy to sdcard）→ MTP（`gvfsd-mtp` 実在確認済） |

### 6.2 ★事前登録（snoop 判定・★ログ受領 «前» に commit・書き換えない）

**較正（C0 相当）を先に**：SMP の有無を主張する前に、**«必ず在るはず» のものが見えること**を確認する
——(a) 我々の MAC/adv への LE 接続 complete ≥1、(b) Disconnection Complete（reason つき）、
(c) ATT/GATT トラフィック（Android は必ず discovery する）。**これらが見えない snoop で «SMP 不在» を語らない。**

| # | 分岐 | 登録確率 | 意味（外れた時の意味も） |
|---|---|---|---|
| **S1** | snoop に **Android→我々の SMP Pairing Request が在る** | **40%** | **(iii) が死ぬ**＝Android は送っている ⇒ デバイス側 put=0 との**挟み撃ち**で嫌疑は **(i) 電波/LL or (ii) controller→host 受け渡し**に確定。★snoop に «我々からの Pairing Response» まで見えたら**我々の計器（to_ll に Response 無し）と矛盾＝計器衝突として報告**（辻褄を合わせない） |
| **S2a** | Pairing Request 無し・**我々の Security Request は Android に届いている** | **35%** | **(iii) が生きる**＝Android は SecReq を受けて popup を出したのに**送らずに降りた** ⇒ **«なぜ» が snoop の直前イベント（直前の GATT/LL/SM・切断の向きと reason）に見えるはず**。我々の TX 鎖（app→to_ll→電波）は end-to-end 白 |
| **S2b** | Pairing Request 無し・**SecReq も見えない** | **20%** | **我々の TX が電波に出ていない疑い**（to_ll 通過は実測済＝**controller 内 TX/radio**）。★ただし較正 (a)-(c) が見えていることが前提（見えないなら S3） |
| **S3** | snoop が読めない／較正イベントが無い（snoop 有効化前の再現・別セッション混入等） | **5%** | **測定不成立＝結果を語らず取り直し** |

**両側突き合わせ（必ずやる）**：接続回数（snoop の LE Connection Complete 数 vs STORE8 count）／
切断 reason（snoop vs STORE9）／SMP 数（snoop の PairReq 送信数 vs 我々の put）。
**不一致はそのまま報告**（どちらかの計器の異常か、セッション混入の検出器になる）。
**「即」vs「≥5秒生存」の緊張は snoop のタイムスタンプで初めて客観化される**（体感を秒に変換しない）。

### 6.3 ユーザー手順（Galaxy Z Fold 7・Android のみ・iPhone 不触）

1. **開発者向けオプションを有効化**（設定→端末情報→ソフトウェア情報→「ビルド番号」7 回タップ）
   →開発者向けオプションで **「Bluetooth HCI スヌープログを有効化」を ON**
   → ★**Bluetooth を OFF→ON**（スタック再起動でログ開始）。
2. **Android と iPhone の両方**で `ASP3-C5-BLE` を **forget**（残っていれば）→ 両端末 BT OFF/ON
   （Android は手順 1 の OFF/ON と兼ねてよい）。
3. **Android の Bluetooth 設定から `ASP3-C5-BLE` をペアリング**（失敗の再現）。
   観測を逐語でメモ（popup の有無・エラーまでの体感・登録の有無）。**再現は 1 回でよい**
   （複数回でも可＝回数を教えてほしい。デバイス側カウンタと突き合わせる）。
4. ★再現後、**Bluetooth の OFF/ON をしない**（ログを新しくしない）。**スマホを USB で PC に接続**し、
   （初回のみ）「USB デバッグを許可」する（開発者向けオプション→USB デバッグ ON が必要）。
5. 完了を報告 → こちらで `adb bugreport` により回収する（**Fold 7/One UI での btsnoop の実パスは
   取ってから確認する＝決め打ちしない**）。adb が不調なら予備経路（`*#9900#` SysDump→Copy to
   sdcard→MTP）を案内する。

（snoop 受領後にここへ実測を記入）

---

## 7. ★★枠外の観測：snoop セルで «Android : all ok!»（成功）＝**事前登録 S1-S3 の前提（失敗）が崩れた**

★**§6.2 の事前登録は «失敗セルのカウンタ» を前提にしていた＝本セッションは «枠外» として記録する。
登録（S1-S3・確率）は書き換えない。**

### 7.1 凍結時のタイムライン（実タイムスタンプ・正直に）

| 時刻（JST） | 事実 |
|---|---|
| 00:09:30 | commit `409688d`＝E-RX 締め。**この直前に私がベンチを復旧＝3 ボード給電・PRESENT**（C3/C6 も電波に出うる状態にした） |
| ~00:10-00:14 | snoop セル準備：verify-flash・マーカー清掃（**この間 C5 は download mode＝広告停止**。★**C3/C6 はまだ給電中の時間帯があった**＝もしこの窓でユーザーが実施していたら mixup の危険窓だった） |
| ~00:14-00:15 | C3/C6 電源断（読み戻し）→ C5 真cold（by-id GONE 読み戻し）→ **スキャン＝ASP3-C5-BLE のみ在空・BlueZ の Paired=no** |
| 00:16:40 | commit `c90a9e2`＝準備完了 |
| ~00:16-00:19 | **ユーザーが（合図を待たず）実施**：「**Android : all ok!**」・相手の名前は「**ASP3-C5-BLE**」（追加報告・逐語） |
| 00:19-00:20 | 凍結指示受領→現状スキャン（**在空は ASP3-C5-BLE のみ・Port1/2=off 読み戻し**）→ 00:20:06 マーカー読取 |

⇒ **H-mixup はほぼ死んだ**：(a) 名前が C5、(b) 成功時間帯に C3/C6 は電源断（読み戻し済）で
**在空の ASP3 は C5 のみ**を実測、(c) ★決定打＝**成功署名が «私の清掃+真cold の後のブート» のマーカーに在る**（§7.2）。
**snoop の peer アドレスで最終確定させる**（名前でなくアドレス）。

### 7.2 マーカー実測（00:20:06・清掃済+真cold 後のブート＝**このブートの記録であることが構造的に確実**）

| reg | 値 | 読み |
|---|---|---|
| STORE0/2 | `0x5ade51c0`/`0xad000000` | SYNC・adv rc=0 |
| ★**STORE3** | ★**`0x22050005`** | **enc_chg=2・sm_enc_rx=2**（暗号化イベントが 2 回＝2 接続とも暗号化到達）・最終 enc 以降 put=5/sm_tx=0/to_ll=5（**再接続側は SMP 無し＝LTK 再利用の形**・ATT のみ） |
| ★★**STORE4** | ★★**`0xe2060608`** | **tag=0xE2・smp_rx_put=6・smp_rx_l2=6・smp_tx_ll=8**＝**フル SMP 交換** |
| ★**STORE6** | ★**`0x5de00000`** | **ENC_CHANGE status=0＝暗号成立**（失敗署名 `0x5de0ff07` ではない） |
| STORE8/9 | `0x604e0002`/`0xd15c1302` | **CONN=2・DISC=2**（reason 0x13＝スマホ側から終了・セッション終了として正常） |

**構造検算（SC responder＋bond 再接続の期待系列と完全整合）**：
- **TX 8**＝SecReq(1)＋Pairing Response・PubKey・Confirm・Random・DHKey(5)＋鍵配布 Identity×2
- **RX 6**＝Pairing Request・PubKey・Random・DHKey(4)＋**Android 側鍵配布 2**（central key dist）
- **接続 2 回・enc 2 回・2 回目は SMP ゼロ**＝**bond 成立→再接続で LTK 再利用**の形
- 比較：BlueZ C0＝4/4/7（鍵配布が片側のみ）／**失敗セル＝0/0/1(SecReq のみ)**

⇒ ★**成功は «私の真cold の後»＝セルは完全有効**。**デバイス側の証拠は消えていない。
snoop×カウンタの両側観測が «成功系列» で取れた**（設計以上の成果）。

### 7.3 ★矛盾はそのまま（数字で）

**同一ビルド（verify-flash digest 一致）・同一個体・同一スマホ・どちらも真cold** で：
- 昨日 23時台の失敗セル＝**0/0/1×3 接続**（SMP 不流通・ENC=7）
- 今回＝**6/6/8・ENC=0・LTK 再接続まで成立**

「即エラー」vs「≥5 秒生存」の緊張も**未解決のまま**（本セッションの成功はこの緊張を消さない）。

### 7.4 H-real の候補（★列挙のみ・断定しない）

| # | 失敗セルと今回の «差» | 備考 |
|---|---|---|
| 1 | **Android BT スタック再起動**（snoop 有効化に伴う BT OFF/ON） | **forget で消えず・スタック再起動で消える Android 側状態**（例：per-device の解決リスト/キャッシュ/黒リスト）の仮説。★失敗セル手順にも「BT OFF/ON」は書いてあった＝**ユーザーが実際に実施したかは両セルとも未計装** |
| 2 | **開発者向けオプション/HCI snoop ON そのもの** | snoop ON が BT スタックの挙動（タイミング・ログ経路）を変える可能性＝**観測者効果** |
| 3 | 時刻・RF 環境 | 深夜・周囲デバイス数の差 |
| 4 | 私のマーカー清掃・verify-flash | LP_AON 書込みと flash 読取のみ＝機序としては考えにくいが列挙する |
| 5 | 試行のばらつき（元々間欠） | 失敗は 3+2 接続で一貫していたが、**成功確率が 0 という証明は無かった** |

### 7.5 再現実験の提案（★実施しない・ユーザー判断・1 回の成功は 1 回の観測）

| # | 実験 | 判別するもの | 費用 |
|---|---|---|---|
| 1 | **このまま forget→再ペアリング**（snoop ON のまま・BT OFF/ON しない） | 「今の状態では安定して成功する」の確認（ベースライン固定） | スマホ操作のみ |
| 2 | **snoop OFF→BT OFF/ON→forget→ペアリング** | 候補 2（snoop ON 自体）の切り分け | 同上 |
| 3 | **スマホ再起動→forget→ペアリング** | 状態リセットの粒度をもう一段深く | 同上 |
| 4 | **数日内に «BT OFF/ON をしていない長時間運用状態» で再ペアリング** | 候補 1（スタック常駐状態の腐り）＝**失敗条件の再現**（これが再現できて初めて A/B になる） | 時間 |

★**注意**：候補 1 が本物なら「失敗する状態」を再現できるはず。**«成功の再現» と «失敗の再現» の両方が
揃って初めて機序と呼べる**。

### 7.6 snoop 回収と較正（★較正が先）

- 回収：`adb bugreport`（USB 認可後）→ zip 内 **`FS/data/log/bt/btsnoop_hci.log`**（294,585 B・
  実パスは zip を見て確定＝決め打ちしていない）。**bugreport はスクラッチパッドに置き repo には入れない**。
- 形式：btsnoop v1・datalink 1002（H4）。**自作 python パーサ**（tshark 無し）。
- ★**時刻の注記**：ログ内部時刻は標準 epoch 換算で「09:16:xx」と出るが、ベンチ実時刻 00:16 JST と
  **ちょうど +9h**＝端末がローカル時刻をそのまま焼いている形。**照合は相対時刻と系列で行った**。
- **較正 PASS**：records=2428・cmd 655／evt 1022／ACL rx 710・tx 41／**ATT 70**／
  LE Connection Complete（enhanced）**3 件**・Disconnection **3 件**・**LE Start Encryption cmd 2 件**・
  **Encryption Change «v2»（event 0x59）2 件**（★0x08 は 0 件＝新スタックは v2 を使う。
  「enc イベントが無い」と誤読しかけたが**イベントコードのヒストグラムで v2 を発見**＝
  «マーカーの不在は over-determined でないか» の適用）。

### 7.7 snoop 実測（成功系列・**peer はアドレスで C5 確定**・第三者 MAC は出現抜粋に含めていない）

**全 3 接続とも peer=`d0:cf:13:f0:a7:44`（DUT・public）＝ ★H-mixup は snoop でも死んだ。**

| 時刻（ログ内部・-9h でベンチ JST） | イベント |
|---|---|
| 09:16:09.301 | conn#1 (handle=3) 接続 → **09:16:09.484 切断 reason=0x3e**（Connection Failed to be Established・183ms）＝**確立失敗**（RF 過渡） |
| 09:16:13.436 | conn#2 (handle=5) 接続 |
| ★09:16:17.737 | **DUT→phone: Security Request**（接続の ~4.3s 後＝**我々の 5s tick と整合**） |
| 09:16:19.699 | **phone→DUT: Pairing Request**（SecReq の ~2.0s 後＝popup で承認した形）。features: io_cap=0x04(KbdDisp)・authreq=0x2d(Bond+MITM+SC+CT2)・**key_dist=0x0f（LinkKey bit＝CTKD 要求つき）** |
| 09:16:19.7-21.05 | SC 交換：PairRsp／PubKey 両方向／Confirm／Random 両方向／DHKey 両方向 |
| ★09:16:21.201 | **Encryption Change v2 status=0 enabled=1 keysize=16**＝暗号成立 |
| 09:16:21.262 | **鍵配布 両方向**（Identity Info + Identity Addr を双方 2 本ずつ） |
| 09:16:33.832 | conn#2 切断 **reason=0x16**（phone 側が終了） |
| 09:16:36.445 | conn#3 (handle=7) 再接続 → ★**09:16:36.713 Encryption Change v2 status=0**（**SMP 0 本＝LTK 再利用**） |
| 09:16:56.841 | conn#3 切断 reason=0x16 |

### 7.8 ★★両側突き合わせ（独立 2 計器・事前固定項目）＝**全項目一致**

| 項目 | snoop（phone 視点） | デバイス（STORE 視点） | 判定 |
|---|---|---|---|
| phone→DUT の SMP 本数 | **6**（PairReq・PubKey・Random・DHKey・IdInfo・IdAddr） | **smp_rx_put=6・smp_rx_l2=6** | ★**完全一致** |
| DUT→phone の SMP 本数 | **8**（SecReq＋PairRsp・PubKey・Confirm・Random・DHKey＋IdInfo・IdAddr） | **smp_tx_ll=8** | ★**完全一致** |
| 暗号化 | Enc Change v2 ×**2**（status=0） | **enc_chg=2・sm_enc_rx=2**（STORE3）・ENC=`0x5de00000` | ★**完全一致** |
| 接続数 | **3**（うち 1 は 183ms で 0x3e 確立失敗） | **CONN=2** | **差 1＝説明可能**：0x3e の確立失敗は GAP CONNECT に到達しない。★ただし「device が数えなかった」ことの直接証拠は無い＝**推定と明記** |
| 切断 reason | 0x16（phone 側終了）×2 | DISC reason=`0x13` count=2 | ★**表現の差**：phone が local host termination（0x16 は «自分が切った» のログ）→ 対向（我々）には **0x13(Remote User Terminated)** として届く＝**プロトコル上整合** |
| 再接続の SMP | conn#3 は SMP 0 本で暗号成立 | STORE3 の最終 enc 以降 sm_tx=0 | ★**一致**（bond/LTK 再利用） |

⇒ ★★**E-RX 計器は phone 側の独立記録と «本数まで» 一致＝計器の信頼性が両側で立証された。**

### 7.9 ★この snoop に «失敗セッション» は写っていない（差分探索は不成立）

ログの記録開始＝内部 09:14:16（≒ベンチ 00:14）＝**snoop 有効化はの直前**。
**23時台の失敗セル（R4）はログに存在しない**⇒**「失敗と成功の同一ログ内差分」は取れなかった**。
**失敗を snoop ON の状態で再現できたときに初めて差分が取れる**（§7.5 の実験 4 に対応）。

### 7.10 ★成功系列が失敗系列に照らして意味すること（事実と推測を分ける）

- **事実**：成功セルでも**入口は同じ**＝我々の SecReq（~5s tick）→ phone の Pairing Request。
  失敗セルでは（デバイス視点で）**この Pairing Request が一度も host に現れなかった**（put=0）。
- **事実**：今回 phone は SecReq 受領の 2.0s 後に Pairing Request を送った（ユーザー承認込み）。
- **推測（断定しない）**：失敗セルの機序が (iii)「phone が送らなかった」なら、snoop ON／BT 再起動で
  phone 側の «送らない状態» が解消された可能性。 (i)/(ii)（電波/LL・controller→host）なら、
  何が変わって通るようになったかは不明のまま。**どれかは失敗の再現（snoop ON）が取れるまで未決**。

---

## 8. 再現実験 1（forget → 再ペアリング＝成功の安定性）＝ **準備完了・ユーザー待ち**

**実験の定義**：候補①（BT スタック再起動）を**再適用しない**（＝BT OFF/ON 禁止）・snoop ON のまま・
デバイスは真cold＋マーカー分離。**「成功は安定か、一回きりか」**を測る。

### 8.1 セルの状態（実測・2026-07-18 00:3x）

| 項目 | 実測 |
|---|---|
| ビルド | **焼き直し無し**＝`verify-flash` **digest matched**（E-RX ビルドのまま） |
| マーカー | STORE0,2-9 クリア→**ALL ZERO 読み戻し**（前セル 6/6/8 と分離。STORE1 不触） |
| 1セル1ボード | ★**記憶でなく実測**：`Port 1/2: 0000 off` 読み戻し＋C3/C6 の by-id 3 本 GONE＋**スキャンで ASP3-C5-BLE «のみ» 在空**（較正 6 デバイス） |
| 真cold | `-p 3-4` off→`jtag=GONE cp2102n=GONE` 読み戻し→on→広告実測 ⇒ **デバイス側の鍵はゼロ（RAM bond）** |
| BlueZ | device remove・Discovering: no・接続 0 |
| snoop | **端末側そのまま**（ログに追記される。消させない）。USB 接続もそのまま |

### 8.2 ★事前登録（測定前 commit・書き換えない）

| # | 予測 | 登録値 |
|---|---|---|
| **P_repro** | **再ペアリングが成功する**（登録される・エラー無し） | **70%** |
| P_sig | 成功時のカウンタが前セルと同型（RX=6・L2=6・**TX=7 or 8**）※TX=8 は我々の 5s SecReq が phone の Pairing Request より先に出た場合・7 は後の場合 | 75%（成功条件下） |
| P_tag | タグ 0xE2 が読める | 95% |

**分岐の意味（★どちらでも収穫・期待しない）**：
- **成功** ⇒ **現状態は安定**＝「一回きりのまぐれ」を棄却。探索は「**何が状態を変えたか**」（§7.5 実験 2-4）へ。
  ★ただし「安定」は **この直後・この状態** に限る主張＝長期・条件変更への外挿はしない。
- **失敗** ⇒ ★★**snoop ON で失敗が撮れた**＝**§7.9 の A/B が即成立**（成功系列 §7.7 との同一ログ内差分で
  (i)電波/LL／(ii)controller→host／(iii)phone が送らない、を直接判別）＝**情報量最大の分岐**。
  デバイス側カウンタ（クリア済）も失敗側の 0/0/n を独立記録する。
- **P_repro を 70% にした理由**：直前の成功と同一の phone スタック状態（再起動なし）＝有効だった状態が
  持続する見込みが高い。100% にしない理由＝失敗機序が未特定（候補⑤間欠なら再発しうる）・
  forget 直後の再ペアリングという操作差。

### 8.3 ユーザー手順（★BT OFF/ON をしないことが実験の定義）

1. **Android で `ASP3-C5-BLE` を forget（登録解除）**。★**iPhone に古い bond が残っていれば iPhone でも forget**
   （デバイスは真cold＝鍵ゼロ。鍵を持つ端末の自動再接続がセルを汚すため）。
2. ★★**Bluetooth の OFF/ON は «しない»**。**snoop 設定もそのまま**（触らない）。
3. **Android の Bluetooth 設定から `ASP3-C5-BLE` を再ペアリング**（前回と同じ操作）。
4. **観測を逐語で報告**：ポップアップ→結果／登録の有無／（失敗なら）タイミング感（体感で結構）。
5. **USB は繋いだまま**でよい（終了後に bugreport を追加回収する）。

### 8.4 実験 1 の結果＝**成功（ユーザー逐語「Android : all ok」）・P_repro/P_sig/P_tag 全的中**

**マーカー実測（00:35:24・前セルと分離済のブート）**：

| reg | 値 | 読み |
|---|---|---|
| ★STORE4 | ★**`0xe2060608`** | **RX=6・L2=6・TX=8＝前セルと同型**（P_sig 的中・TX=8＝SecReq 先行） |
| STORE3 | `0x110d020d` | enc_chg=1・sm_enc_rx=1・暗号後 put=13/sm_tx=2/to_ll=13（鍵配布 2＋ATT） |
| STORE6 | `0x5de00000` | ENC status=0＝暗号成立 |
| STORE8 | `0x604e0001` | CONNECT=1（単一接続） |
| ★STORE9 | ★`0x00000000` | **DISC=0＝読取時点で «まだ接続中»**。★**私の read-mem が DUT をリセットし接続を落とした**（正直に記録）——snoop 側の切断 `reason=0x08`（supervision timeout・09:35:29.9≒read 00:35:24+5s）と**整合** |

**snoop 突き合わせ（bugreport #2・handle=9・09:34:17≒00:34 JST）**：
接続 → **+5.0s SecReq（DUT→phone）** → +1.5s Pairing Request → SC 交換 → **Enc Change v2 status=0**
（09:34:25.472）→ 鍵配布両方向 ⇒ **phone 視点 6 TX / 8 RX＝デバイス 6/6/8 と本数まで一致（2 セル連続）**。
forget 後の再ペアリング＝**鍵配布からフルにやり直し**＝前回成功系列（§7.7）と同形。

| 事前登録 | 登録値 | 結果 |
|---|---|---|
| P_repro | 70% | ★**的中**（成功） |
| P_sig | 75% | ★**的中**（6/6/**8**） |
| P_tag | 95% | ★的中 |

### 8.5 実験 1 までで «確定した» こと／«未決» のこと

- ★**確定**：**「一回きりのまぐれ」棄却＝成功 2/2**（どちらも真cold・forget 後フレッシュペアリング・
  BT OFF/ON なし・両側計器一致）。★**射程＝«この直後・この状態に限る»**（外挿しない）。
- ★**是正**：**「C5 × Android は bond に到達しない」は «現在の状態では» 成立しない**。
  歴代 NG（2アーム9レジスタ・E1・E-RX 失敗セル）と成功 2/2 の**境界は «snoop 有効化に伴う
  BT スタック再起動» の前後**——★**ただし因果は未確定**（候補①-⑤のまま）。
- ★**未決**：失敗時に Pairing Request がデバイスに現れなかった理由（(i)電波/LL／(ii)controller→host／
  (iii)phone 不送出）は**失敗が snoop ON で再現できるまで判別できない**。

---

## 9. 実験 2（snoop OFF → BT OFF/ON → 再試行）＝ **準備完了・ユーザー待ち**

### 9.1 設計（振る変数と分岐の意味）

**振る変数**：**snoop OFF（候補②除去）＋ BT OFF/ON（候補①再適用）**。

| 結果 | 意味 |
|---|---|
| **成功** | **②（snoop 観測者効果）は «必要条件として» 死ぬ**（snoop 無しでも成立）。①は再適用済で生存＝「snoop は無関係・スタック再起動 or 状態が sticky」へ絞れる |
| **失敗** | ★**極めて重要**＝①を再適用したのに失敗が戻った＝**「スタック再起動が直す」が死に、②が筆頭へ**（または⑤間欠）。★**このセルは snoop 無し＝デバイス側カウンタが唯一の計器**：失敗時は put/l2/txll を読み **E-RX 失敗型（0/0/1×n）と同型か**を判定する |

### 9.2 ★事前登録（実施前 commit・書き換えない）

| # | 予測 | 登録値 |
|---|---|---|
| **P_2** | 再ペアリング成功 | **75%** |
| P_2sig | 成功時カウンタ同型（RX=6・L2=6・TX=7or8） | 75%（成功条件下） |

**計器制約（事前明記）**：snoop OFF＝スマホ視点なし。失敗時の (i)/(ii)/(iii) 判別は**このセルでは
できない**（デバイス側で「型の一致」までを判定し、判別は snoop ON での失敗再現に委ねる）。

### 9.3 セルの状態（実測・00:4x）

| 項目 | 実測 |
|---|---|
| snoop 回収 | ★★**ユーザーが OFF にする «前» に回収・アーカイブ済**（bugreport #1=00:23・#2=00:36 取得済・scratchpad 保管）＝**OFF によるローテート/消失のリスクは既に無い** |
| ビルド | 焼き直し無し＝`verify-flash` digest matched |
| マーカー | STORE0,2-9 クリア→ALL ZERO 読み戻し（実験 1 と分離） |
| 1セル1ボード | `Port 1/2: 0000 off` 読み戻し＋C3/C6 by-id GONE＋スキャンで ASP3-C5-BLE のみ在空 |
| 真cold | `jtag=GONE cp2102n=GONE` 読み戻し→復電→広告実測 |
| BlueZ | device remove・Discovering: no |

### 9.4 ユーザー手順（★順序厳守・snoop 回収は完了済＝すぐ始めてよい）

1. **（回収完了済＝この手順を受け取った時点で開始可）** 開発者向けオプション → **「Bluetooth HCI スヌープログ」を «無効»**。
2. **Bluetooth を OFF → ON**。
3. **`ASP3-C5-BLE` を forget**（Android。★iPhone に残っていれば iPhone も）。
4. **Android の Bluetooth 設定から再ペアリング** → 観測を逐語で（ポップアップ→結果・登録の有無・失敗ならタイミング感）。
5. **USB はそのまま**。

（実施後にここへ実測を記入）

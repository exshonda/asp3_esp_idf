# evidence-06：測定1（SMP op 列）と DISC 実測——**ペアリングは «空中で成功» しているのに ETIMEOUT が出る**

日付：2026-07-21 ／ DUT＝ESP32-C3 rev v0.3（hub port3）／ central＝Android `SM_F966Q`

## 1. 経緯

evidence-05 で「Android 相手だと `ENC_CHANGE status=13`（`BLE_HS_ETIMEOUT`）で bond 不成立」
と記録し、外部AI 2者（Codex / fable）に診断を依頼した。

### 1.1 Codex の診断は**無効**だった（環境問題・記録として残す）

Codex は冒頭で自認：「**情報不足で判断不可。すべてのシェル実行が
`bwrap: loopback: Failed RTM_NEWADDR: Operation not permitted` で失敗**」
＝**リポジトリを1行も読めないまま**上流 NimBLE の一般論だけで推測していた。

- 環境実測：**`kernel.apparmor_restrict_unprivileged_userns = 1`**
  ＝`~/agents_playbook/codex-review.md` が記録済みの既知の罠。
  **`--sandbox danger-full-access` で回避**し、smoke test で実際に読めることを確認してから
  再実行する運用が正しい（playbook の「smoke test を先に打たないとトークンを捨てる」）。
  **依頼側（本エージェント）が playbook を参照せず plugin 経由で投げたのが原因。**
- Codex の仮説（`sm_mitm=1`＋`DISPLAY_YESNO` → Numeric Comparison で
  `ble_sm_inject_io()` 待ち）は**実測で棄却**：
  `apps/ble_host_smoke_c3/ble_host_smoke_c3.c:875-878` は
  **`sm_io_cap=NO_IO`・`sm_mitm=0`**＝Just Works で当該経路に入らない。
  fable も同じ箇所を根拠に「該当せず（`IOACT_NONE`）」と明示的に否定しており、**fable が正しい**。

### 1.2 fable の診断は有効（ただし中心仮説は後に反証された→§3）

## 2. ★測定1：btsnoop の SMP op 列抽出——**ペアリングは完全成功していた**

`btsnoop_verify.py` は SMP を `smp[:14]` で先頭14件に切っており直近セッションが見えないため、
**既存器を壊さず** `snoop/btsnoop_smp.py` を新規追加（時刻範囲指定・opcode 名解決・全件・
鍵配布 PDU の機械判定）。

**positive control を先に実施**（「壊れた検証も成功と同じ顔をする」対策）：
SMP が在ると分かっている区間（`+63530〜63545s`）で完全な LESC フローを検出できることを実演。

**問題のセッション（`+111600s`〜）の実測**：

```
+111693.680s  C->H  op=0x0b Security Request        authreq=0x09(bond=1 mitm=0 sc=1)
+111695.069s  H->C  op=0x01 Pairing Request         io_cap=0x04 authreq=0x2d(bond=1 mitm=1 sc=1)
+111695.159s  C->H  op=0x02 Pairing Response        io_cap=0x03 authreq=0x09(bond=1 mitm=0 sc=1)
+111695.164s  H->C  op=0x0c Pairing Public Key
+111695.430s  C->H  op=0x0c Pairing Public Key
+111695.431s  C->H  op=0x03 Pairing Confirm
+111695.433s  H->C  op=0x04 Pairing Random
+111695.519s  C->H  op=0x04 Pairing Random
+111696.207s  H->C  op=0x0d Pairing DHKey Check
+111696.302s  C->H  op=0x0d Pairing DHKey Check
+111696.512s  C->H  op=0x08/0x09 Identity Information / Address   ★相手の鍵配布
+111696.513s  H->C  op=0x08/0x09 Identity Information / Address   ★★我々の鍵配布
```

**機械判定：我々(H->C)が送った鍵配布 PDU = 2件（`0x08`/`0x09`）**

⇒ **fable が最優先で確定させたかった問い「我々は鍵配布を電波に出したか」の答えは「出している」。**
**LESC ペアリングは空中で完全に成功している。**

切断：
- `+111714.363s reason=0x16`（Local Host＝スマホが切断）＝鍵配布の**約18秒後**
- `+111724.936s reason=0x08`（supervision timeout＝沈黙）

`docs/ble-c3-smp-death-plan.md` rev2 の分類では **Step3 型**（LESC 成功→鍵配布→その後沈黙）に
**該当すると確定**（evidence-05 §7.5 で「断定しない」としていた点が決着）。

## 3. ★★fable の中心仮説は**反証**された（予測を先に固定して測定）

fable の中心的推論：
> `status` が `7`(ENOTCONN) でなく `13`(ETIMEOUT) ＝ **30秒の窓の間、host は
> Disconnection Complete を1件も処理していない**（＝death-plan の「DISC=0」不変量と符合）

**測定前に判定基準を固定**：「`g_gap_disc_count=0` なら仮説実証／`1以上` なら反証」。

実機で再現（スマホで connect → bond 成立 → 切断）させ、**計装ゼロで JTAG 読み出し**
（halt→read→resume＝**リセットを伴わない**ので死んだ状態を保持）：

| 指標 | fable 仮説 | **実測** | |
|---|---|---|---|
| `gap_conn` | 1 | 1 | — |
| **`gap_disc`** | **0** | **1** | ❌ **反証** |
| `conn_handle` | 古い接続を保持 | **`0xffff`（未接続）** | ❌ |
| `msys_1` | 枯渇 | **`free=12/12`（min=10）** | ❌ |
| `notify_sent/fail` | 積み増し・失敗継続 | **0 / 0** | ❌ |

⇒ **切断イベントはホストに正しく配送され、接続も正しく畳まれ、mbuf も健全。**
**「DISC=0」型ではない。**

## 4. 計装の既存バグを2件発見（副産物）

`ESP32C3_BT_EVT_TRACE`（HCI EVT 経路の `--wrap` 計装）を使おうとして判明：

1. **ON にすると必ずリンクエラー**：`undefined reference to
   `__wrap_ble_transport_to_ll_acl_impl'`。実体は `evt_trace.c` の
   `#ifdef TOPPERS_C3_EVT_FAST_MAP` 内にあるのに、cmake がそのマクロを定義せず
   `-Wl,--wrap=` だけ張っていた。**既定 OFF のため気づかれていなかった**。→ 修正済み。
2. **修正後も `--wrap` が inert**：`objdump` 実測で
   `ble_hs_hci_evt_process` を呼ぶ `jal` が**1つも無い**（`__real_` シンボルも生成されない）
   ＝**`ble_hs.c:655` の呼出しが最適化でインライン化**され、リンク時に呼出しが存在しない。
   ⇒ **この計装は現在の最適化設定では機能しない**（playbook「wrap は inert になり得る」の実例）。
   本ラウンドは wrap を諦め、**計装ゼロの JTAG 読み出し**（evidence-rc-c3-P1 と同じ手法）を採った。

## 5. 現時点で確定していること／絞り込まれた謎

**確定**：

| 事実 | 出典 |
|---|---|
| LESC ペアリングは**空中で完全成功**（鍵配布を双方向で交換） | btsnoop（§2） |
| bond はフラッシュに保存され、**真cold を跨いで復元**できる | 実機（evidence-05 §4） |
| **ホストは切断を正しく認識**し接続を畳む。mbuf も健全 | JTAG（§3） |
| それでも **`ENC_CHANGE status=13`** が出る | DUT ログ |
| **BlueZ では成功、Android では失敗** | 両者で実測 |

**絞り込まれた謎**＝「**ペアリングは成功しているのに `ENC_CHANGE` だけが ETIMEOUT を報告する**」。
＝`ENC_CHANGE` の «報告» と実際の SMP 完了が**乖離**している。

**次の測定（未実施）**：ペアリング成功直後（ETIMEOUT が出る **«前»**）に `sec_state` が
`enc=1 bond=1` になっているかを見る。なっていれば
「**bond は実は成功していて、アプリが遅れて偽の失敗報告を受け取っているだけ**」が確定する
（`ble_sm_proc` が完了後も回収されず 30 秒で期限切れ発火、という機序が候補。**未検証**）。

---

## 6. 追加測定：**症状は 1 つではない**（Android 相手で 2 種類の経過を観測）

`ENC_CHANGE` ハンドラでしか `sec_state` を出していなかったため、**ETIMEOUT が出る «前»** の
状態が見えなかった。そこで `PAIRING_COMPLETE` 時点にも `sec_state` を出すログを 1 行足した
（`ble_host_smoke_c3.c`．**JTAG 案は断念**——観測手段が測定対象を壊す事故を 3 度踏んだため。
§6.3 に列挙）。

### 6.1 観測された 2 種類の経過（同一ビルド・同一 central）

| # | 経過 | DUT ログ | フラッシュ |
|---|---|---|---|
| **A** | **bond 成立 → 数十秒後に切断** | `PAIRING_COMPLETE status=0` が出る | **`magic=0x42535331` で保存**（peer = 実機の identity アドレス） |
| **B** | **ペアリング要求が来ない → 切断** | `PAIRING_COMPLETE` が **一度も出ない** | 未保存 |

**⇒ 同じ手順でも A と B が起きる＝単一の決定論的バグではない**（レース／タイミング依存の疑い）。
evidence-05 §7 で「ペアリング要求は来るが bond しない」と記録した経過も含めると、
**Android 相手の挙動は少なくとも 3 通り**ある。

### 6.2 経過 B の実測ログ（今回・接続前から読み続けて捕獲）

```
ble_host_smoke: GAP CONNECT status=0 handle=1
esp_shim: pend path engaged (dtqid=2)          ← ★D-2c 型の救済経路が発動
ble_host_smoke: BT5 security_initiate(slave SecReq) rc=0
ble_host_smoke: ss t=60s conn=1 disc=0 ntf=0/0
ble_host_smoke: GAP ENC_CHANGE status=13
ble_host_smoke: sec_state enc=0 auth=0 bond=0 keysz=0
```

**`esp_shim: pend path engaged (dtqid=2)`** は `esp_shim_core.c:781`。
**ブロッキング API が BT のクリティカルセクション内で `E_CTX` を返し、救済の pending リングへ
落ちた**ときに（初回のみ）出る＝memory `c3-ble-d2c-gatt-conn` が記録する **D-2c 型の機構が
«接続直後に» 発動している**。

★**ただし因果は未確定**：`shim_que_pend_used` は増分のみの累計カウンタで、
evidence-rc-c3-P1 §1-1 が「**71 は «救済経路が累計 71 回発動し、すべて流れた» の意**」と
明記しているとおり、**発動＝詰まりではない**。**「pend が出た → だから SMP が失敗した」と
断定してはならない**（相関を因果と早合点しない）。**A/B 対照が必要**：
経過 A（bond 成功）のときにも `pend path engaged` が出ているかを確認すれば、
**この行が失敗の弁別指標になるか否か**が決まる。**本ラウンドでは未取得。**

### 6.3 ★観測手段が測定対象を壊した事故（4 件・手法の記録）

| 手段 | 何が起きたか |
|---|---|
| `tmp/rts_boot_capture.py` | **RTS でリセット**＝観測したい BLE セッションを切る。さらに**先にリセットしてから採取**するので **リセット «前» のログが失われる** |
| `tmp/c5_cold_passive_capture.py` | **開く瞬間に一度リセットが入る**（スクリプト冒頭に明記）。測定開始が対象を再起動する |
| `--wrap` 計装（`ESP32C3_BT_EVT_TRACE`） | ①`TOPPERS_C3_EVT_FAST_MAP` 未定義で**必ずリンクエラー**（修正済）②修正後も**最適化で inert**（§4） |
| **自作 GDB スクリプト** | `continue &`＋`detach` が効かず **DUT を halt したまま放置**＝広告停止。ユーザーの試験を 1 回無駄にした |

**結論＝この DUT でライブ BLE を観測する唯一の安全な方法は**
「**接続を始める «前» から、DTR/RTS を触らずシリアルを読み続ける**」。
（`c3_pool_watch.sh` の halt→read→**resume** は例外的に安全だが `sec_state` は読めない。）

### 6.4 現時点の到達点

**確定**：
- ペアリングが成立する経過（A）では **空中で LESC 完全成功・鍵配布双方向**（§2）、
  **フラッシュに bond 保存**、**真cold を跨いで復元**（evidence-05 §4）。
- 切断は**ホストに正しく配送**される（§3．fable 仮説の反証）。
- **同一条件で A/B の経過が分岐する**＝レース／タイミング依存（§6.1）。

**未確定（次の測定）**：
1. 経過 A でも `pend path engaged` が出るか（＝この行が弁別指標か）。**A/B 対照が必須**。
2. 経過 A の `PAIRING_COMPLETE` 時点で `encrypted=1` か
   （＝ETIMEOUT が «偽の失敗報告» か否か）。**ログは仕込み済み・未捕獲**。

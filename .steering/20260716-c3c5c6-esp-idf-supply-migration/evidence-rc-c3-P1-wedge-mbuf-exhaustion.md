# evidence-rc-c3 P1 — «wedge» の機構を実機で特定：mbuf(msys_1) 枯渇＝未解放の notify 送信キュー

2026-07-18。計画＝`docs/ble-c3-smp-death-plan.md`(rev2) の **P1**。
計器＝`p1-tools/c3_postmortem.sh`＋`c3_postmortem_decode.py`（**出荷ビルドに計装ゼロ・JTAG 読み出しのみ**）。
生ダンプ＝`p1-tools/dump_DEAD_wedge.txt`・`dump_DEAD_leaked_mbufs.txt`、対照＝`sample_healthy_dump.txt`。

## 0. 経過（実機・Android・同日）

同一 build（本セッションの修正入り＝`t2c5_c3_ble`／esp-14.2.0_20260121／SM=ON）で：
**ペアリング成功 → 再接続も成功 → 切断/再接続を «何回か» 繰り返した後、connect できなくなった
（スマホ側にエラーは出ない）**。この «死んだ状態» を触らずに JTAG で捕獲した。

## 1. 事実（実測値・healthy 対照つき）

### 1-1 RTOS / shim 層は **完全に健全**
| 項目 | healthy(adv中) | **DEAD(wedge)** |
|---|---|---|
| タスク状態13個 | （下記） | **完全に同一** |
| `dspflg` / `p_runtsk==p_schedtsk` | 1 / 一致 | **1 / 一致** |
| `esp_shim_crit_nest` | 0 | **0** |
| `shim_que_pend_total`（現在保留数） | 0 | **0**（＝全て flush 済） |
| `shim_que_debt_conflict` | 0 | 0 |
| `shim_sem_pend_total` / `sem_take_ectx_total` / `sem_ectx_total` | 0 | 0 |
| `shim_que_pend_used`（**累計利用実績**） | 0 | 71 |

★`shim_que_pend_used` は **increment のみの累計カウンタ**（`esp_shim_core.c:763,778`。初回 syslog 用）で
«現在詰まっている数» ではない。**現在値は `que_pend_total=0`＝保留リングは空**。
⇒ **«E_CTX 救済が詰まった» ではない。71 は «救済経路が累計71回発動し、すべて流れた» の意。**

⇒ ★**計画 rev2 の H5「我々の統合が SMP 処理経路で恒久ブロック（RTOS デッドロック）」は反証**。
タスクは全て正常な待ち状態、スケジューラ整合、クリティカルセクション外。

### 1-2 BLE ホストは **生きていて、動き続けている**
| 値 | 実測 | 意味 |
|---|---|---|
| `g_gap_conn_count` / `g_gap_disc_count` | **5 / 4** | ★**5回接続・切断は4回しか届いていない＝最後の1回だけ欠落**（systemic な欠落ではない） |
| `g_conn_handle` / `ble_hs_conns` | **0x0001** / **0x3fcb201c**(非NULL) | host は**古い接続を保持**＝wedge |
| `g_adv_active` | **1** | アプリは広告中のつもり。**しかし電波上は出ていない**（受動スキャン 0 回検出） |
| `g_notify_sent` / `g_notify_fail` / `g_notify_last_rc` | **24 / 117 / -1** | notify ループは**回り続け、失敗し続けている** |
| `g_notify_counter` | 141 | 24+117=141 ✓ 整合 |
| `g_conn_secs` | 159 | |

★`g_notify_last_rc = -1` は**アプリ実装上「`ble_hs_mbuf_from_flat()` が NULL を返した」場合に限る**
（`ble_host_smoke.c:266-268`）＝**mbuf 確保失敗**。`rc` を格納する別経路（279行）とは区別される。

### 1-3 ★★ mbuf プール：**msys_1 が完全枯渇**
| プール | block_size | num_blocks | **num_free** | **min_free** | free-list |
|---|---|---|---|---|---|
| **msys_1** (`os_msys_init_1_mempool` @3fcb16ec) | 256 | 12 | **0** | **0** | **NULL** |
| msys_2 (@3fcb1714) | 320 | 24 | 24 | 24 | 非NULL |

### 1-4 ★★★ 枯渇した12ブロックの正体＝**未解放の notify 送信キュー**
12ブロック全部が**完全に規則的な対**（`dump_DEAD_leaked_mbufs.txt`）：
- 偶数ブロック：`om_len=4`・`om_next=0`・pool=msys_1 ＝ **notify の4バイト・ペイロード**
- 奇数ブロック：`om_pkthdr_len=8`・`om_len=7`・`om_next`→直前の偶数ブロック・pkthdr 総長 `0x0b=11`
  ＝ **ATT Handle-Value-Notification パケット**（L2CAP 4 + ATT 7 = 11B）

⇒ **msys_1 の全12ブロック＝ちょうど «notify 6パケット»（ヘッダ mbuf＋ペイロード mbuf の対）が丸ごと残留。
リークは 100% «送信キューに積まれた notify» に帰属する。**

`notify_tick` は **1秒周期**（`ble_host_smoke.c:248`）＝**リンク死から約6秒で12ブロックを使い切る**。

## 2. 解釈（★事実と推測を分ける）

### 2-1 **wedge の機構は特定できた**（確信度：高い＝上記実測の直結）
1. リンクが死ぬ（supervision timeout）
2. **切断イベントが host に配送されない（DISC=0）**——今回 5接続/4切断で**最後の1回だけ**欠落
3. host は接続の後片付けをしない ⇒ **送信キューの mbuf が解放されない**
4. `notify_tick` が1秒毎に積み増し、**約6秒で msys_1(12ブロック) を使い切る**
5. 以後 host は**何も確保できない**（notify 117連続失敗）⇒ **広告も再開できない**
   （`g_adv_active=1` なのに電波が出ていない）⇒ **スマホから見えない＝「connect できない・エラーも出ない」**

★**「エラーが出ない」というユーザー観察は、この機構の予測どおり**（デバイスが広告しないので、
スマホは «見つからない» だけでエラー事象が発生しない）。

### 2-2 **まだ分かっていないこと**（正直に）
- ★**最初にリンクが死んだ理由は未解明**。今回の枯渇は «リンク死の後» に notify が積んだ結果であり、
  **リンク死の原因ではない**（12ブロック＝6秒分は死後に消費されている）。
- ただし**「前サイクルの残留で入口から枯渇していた」可能性は排除できない**
  （今回は最後の1接続だけで枯渇を説明できるので、**cross-cycle 累積は不要**だが、否定もしていない）。
- `shim_que_pend_used=71` の内訳（どの DTQ か）は未取得。

## 3. ★出荷可能な成果（計画 §8 のオフランプが現実になった）

**wedge の «reset 不要化» が設計可能**：host が «接続していると信じているのに送れない» 状態を
アプリ側で検出し（例：notify 連続失敗 N 回 or 一定時間 notify 成功なし）、`ble_gap_terminate` を
撃って接続を畳めば、**NimBLE が接続の後片付けで送信キューの mbuf を解放する**はず。
⇒ 広告が再開し、スマホから再び見えるようになる（**reset 不要**）。
※「はず」＝**未検証**。次段で実測する（P1-3b）。

## 4. 次の一手

1. **リーク/枯渇の因果を分離**：リセット後、接続/切断サイクルごとに `mp_num_free` を JTAG で読む。
   - 単調減少して戻らない ⇒ サイクル毎リーク（＝入口枯渇もあり得る）
   - 各サイクルで戻る ⇒ 今回のように «死んだ接続» でのみ滞留
2. **P1-3b（wedge 解消の実証）**：watchdog で `ble_gap_terminate` → `num_free` が回復し広告が戻るか。
   戻れば**出荷可能な回避策**が確定（根治前でも UX が直る）。
3. **リンク死の原因**（残る本丸）は P2（stock 成功 vs 我々 失敗の同一スマホ btsnoop 差分）へ。

## 5. 計画への反映
- **H5（RTOS/shim デッドロック）は反証** ⇒ 計画 §3 を更新すること。
- **DISC=0 は «独立軸» ではなく wedge の «起点»** と確定（fable 指摘6の読みが実測で支持された）。
- 計器（`p1-tools/`）は**計装ゼロで再利用可能**＝以後の全ラウンドで同じ手が使える。

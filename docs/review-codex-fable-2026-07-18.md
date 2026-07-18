# 外部レビュー（Codex + fable）findings と検証結果 — 2026-07-18

対象＝本リポジトリ自作コード（`asp3/target/esp32c{3,5,6}_espidf/`・`asp3/arch/`・cmake・
apps）。ベンダ submodule（esp-idf/hal/lwip/asp3_core）は送信/レビュー対象外。
2レビュアに同一範囲・同一既知課題・同一出力形式で依頼し、**全指摘をコードで独立検証**した。

- **Codex**（gpt-5.5 / `--sandbox danger-full-access` / 423k tok / 無改変）：5件。真4・**誤検出1**。
- **fable**（general-purpose fresh context / 397k tok / 読取専用）：7件＋Low 9件。**誤検出0**。
  twai_sem 契約を asp3_core カーネルソースで、番地を esp-idf ヘッダで自己裏取り。潜在/現行の
  区別・blob 依存項目の留保も適切。**この repo では fable の方が Codex より深く正確だった**。

## 検証済み findings（優先順）と対応

| # | 出所 | 指摘 | 判定 | 現/潜 | 対応 |
|---|---|---|---|---|---|
| 1 | Fable#2 | C6 `wifi/esp_shim.c:1481` 「実施59」診断が無条件で intno==1 割込み毎に MAC reg 読み+RTC 書き（C3 はガード・C5 は無し） | 真 | 現行(C6) | ✅ 修正 `1de29c7` |
| 2 | Fable#5 | C3 `wifi/esp_wifi_adapter.c` slowclk_cal が「STORE1」称し STORE4(`0x600080B8`) 誤読／BT診断が STORE1 破壊 | 真(番地) | 現行だがマスク | ✅ 修正 `1de29c7` |
| 3 | Codex#2 + Fable#1 | タイマリスト無ロック走査（Wi-Fi `shim_timer_find`／BT `bt_timer_task`）→ UAF・再arm取りこぼし・int64 torn read | 真 | 潜在(負荷時) | ✅ 修正 `1de29c7` |
| 4 | Codex#1=Fable#4 | FromISR 取得系（`take_from_isr`/`ReceiveFromISR`/`ref_dtq`）が `twai_sem` 等 task 専用へ落ち ISR で必ず E_CTX 失敗 | 真(構造) | 潜在(blob依存) | ⏸ 保留（下記） |
| 5 | Fable#3 | タイマ起床 `sig_sem` が critical 内 E_CTX で消え、give/queue の保留救済がタイマ未適用 | 真(構造) | 潜在 | ⏸ 保留 |
| 6 | Fable#6 | セマフォ delete/再利用で `shim_sem_pend` 未清算 → 再利用先へ亡霊 give | 真(構造) | 潜在(deinit/reinit) | ⏸ 保留 |
| 7 | Codex#5 | C5 `wifi_v8/esp_wifi_adapter.c` LPCON=`0x7`(代入) が bit3(LP_TIMER_EN=BT正当) 破壊。C6 は `\|=` 済 | 真 | 潜在(C5 WiFi+BT共存) | ✅ 修正 `1de29c7` |
| 8 | Codex#4 | プール外全タスクが単一 `shim_main_thread_sem` 共有 → 多タスクで esp_wifi 同期化け | 真(構造) | 潜在(多タスク) | ⏸ 保留（要設計） |
| 9 | Fable#7 | queue `sem_debt` 窓でブロッキング送信が「空き無し」失敗 | 妥当 | 潜在(高負荷) | ⏸ 保留（要設計） |
| — | **Codex#3** | `sys_arch_sem_wait/mbox_fetch` が `1U` 固定 → lwIP タイマ399msドリフト | **✗ 誤検出** | — | 棄却 |

Fable Low 9件（消し忘れ診断・syslog dangling ポインタ・C5 に残る C3 番地診断・CLIC 内部線0-15
未初期化・トレース重複で multiple-definition・TMO オーバフロー busy-spin・SYSTIMER torn read・
タスクハンドル型混在・DEF_INH 線カバレッジ）は妥当だが優先度低・未着手。

## 誤検出の根拠（Codex#3 棄却）

Codex は「lwIP 契約=成功時に経過ms を返す」と主張。しかし lwip submodule は **2.2**（`init.h`）で、
契約 doc（`sys.h:217/334`）は「タイムアウトなら `SYS_ARCH_TIMEOUT`、成功なら _任意の値_」。
`tcpip.c:113` と `sockets.c` の全 `waitres` 使用箇所（2168/2183/2451/2466）は `== SYS_ARCH_TIMEOUT`
の真偽比較のみで、経過時間として一切使われていない。タイマは `sys_now()` 絶対時刻で計算。→
`1U` は契約準拠でドリフトは起きない。Codex は lwIP 1.x の古い契約を適用した。fable は同じ誤りを犯していない。

## 適用した修正（commit `1de29c7`）

9ファイル・+219/−63。全対象を C3/C5/C6 の wifi/BT ビルドで実コンパイル・リンク確認
（環境の toolchain 版ドリフト `esp-14.2.0_20241119` vs submodule 期待 `20260121` を回避して
新規 build dir で確認）。**実機/QEMU 動作確認は未実施**（潜在系＝要実機反証）。

- #1 C3 同様 `esp_shim_isr_storm_probe`(既定0) でガード → 既定 no-op。
- #2 STORE 読みを廃し公称値(150kHz RC)直返し。従来も STORE=0 でこの値にフォールバック＝実効不変。
  副次効果：BT 診断の STORE1 書きは «誰も STORE1 を較正値として読まない» ため無害化（移設不要）。
- #3 Wi-Fi `shim_timer_find` は走査をロック下＋確保はロック外＋再走査。BT `bt_timer_task` は
  Wi-Fi 側 `esp_shim_timer_task` と同型（ロック下で判定・更新，cb はロック外，1個ずつ continue 再走査）。
  ※ `c6/bt/bt_shim.c` は現行どの構成でも非コンパイルの休眠コード（ライブ＝`bt_shim_idf61.c`）だが
  整合のため同修正。
- #7 `= 0x7` → `\|= 0x7`（C6 evidence-c6-13 と整合）。

## 保留した findings（#4/#5/#6/#8/#9）と «なぜ保留か»

いずれも潜在＋（blob 依存 or bond-critical 機構 or 要設計判断）で、投機的に手を入れると
D-2c/D-2d の bond 修正等を壊すリスクがある。プロジェクトの調査規律
（`memory/feedback_hardware_investigation_rigor.md`：未確認を確認扱いしない・反証を先に）に従い、
実機での反証を挟んでから着手する。

- **#4 FromISR 取得系** — ★**Codex の提案「`pol_sem` を使え」はカーネルで裏取りした結果 _無効_**：
  `pol_sem` も `CHECK_TSKCTX_UNL`＝`sense_context()`（ISR で失敗）で弾かれる **タスク文脈専用**
  （`asp3_core/kernel/semaphore.c` / `check.h`）。**ASP3 には ISR から使えるセマフォ非ブロック取得の
  サービスコールが存在しない**。真の修正は ISR 経路の再設計。blob が本当に ISR からこれを呼ぶかは
  未確認（両レビュアとも留保）。→ **まず到達性を計装で確認してから**（`sense_context()` を踏んだら
  記録）着手すべき。安直な pol_sem 置換は誤修正。
- **#5 タイマ起床 sig_sem の E_CTX 救済** / **#6 セマフォ delete/再利用の亡霊 give** — 真の機構欠陥
  だが、D-2c/D-2d の bond-critical な «E_CTX 保留救済機構»（`esp_shim_exit_critical` の
  `queue_flush_pending`/`sem_flush_pending`）とセマフォプール会計に手を入れる必要があり回帰リスクが高い。
- **#8 thread sem 共有** / **#9 queue sem_debt** — 現構成で発火しない前提条件（多タスク esp_wifi /
  高負荷）。設計判断が要る。

## 手順メモ（再利用可）

- Codex 実践知＝`~/agents_playbook/codex-review.md`・`memory/reference_codex_review.md`。
  このPCは `apparmor_restrict_unprivileged_userns=1` で `--sandbox read-only` が動かず
  `danger-full-access` 必須。stdin は `< /dev/null`。smoke test を先に打つ。
- ★**外部ツール/他エージェントの確信度は自己申告＝根拠にしない。全件独立検証。潜在/現行を区別。**
  今回 Codex は誤検出1（#3・古い lwIP 契約）＋無効な修正提案1（#4・pol_sem）。fable の方が正確だった。

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
| 4 | Codex#1=Fable#4 | FromISR 取得系（`take_from_isr`/`ReceiveFromISR`/`ref_dtq`）が `twai_sem` 等 task 専用へ落ち ISR で必ず E_CTX 失敗 | 真(構造) | 潜在(blob依存) | ✅ 計装+明文化 `0867aba` |
| 5 | Fable#3 | タイマ起床 `sig_sem` が critical 内 E_CTX で消え、give/queue の保留救済がタイマ未適用 | 真(構造) | 潜在 | ✅ 修正 `0867aba` |
| 6 | Fable#6 | セマフォ delete/再利用で `shim_sem_pend` 未清算 → 再利用先へ亡霊 give | 真(構造) | 潜在(deinit/reinit) | ✅ 修正 `0867aba` |
| 7 | Codex#5 | C5 `wifi_v8/esp_wifi_adapter.c` LPCON=`0x7`(代入) が bit3(LP_TIMER_EN=BT正当) 破壊。C6 は `\|=` 済 | 真 | 潜在(C5 WiFi+BT共存) | ✅ 修正 `1de29c7` |
| 8 | Codex#4 | プール外全タスクが単一 `shim_main_thread_sem` 共有 → 多タスクで esp_wifi 同期化け | 真(構造) | 潜在(多タスク) | ✅ アサート計装 `8f8b0a2` |
| 9 | Fable#7 | queue `sem_debt` 窓でブロッキング送信が「空き無し」失敗 | 妥当 | 潜在(高負荷) | ✅ アサート計装 `ac0f319` |
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

## 適用した修正 第2弾（commit `0867aba`）＝ #4/#5/#6

第1弾（`1de29c7`）に続き #4/#5/#6 を実装（#5/#6 は Opus サブエージェントで実装→親が diff
レビュー＋独立ビルド再検証の上コミット）。全チップ wifi/BT で実コンパイル・リンク確認済み。
実機/QEMU 動作確認は未実施（潜在系＝要実機反証）。

- **#4 FromISR 取得系** — ★**Codex の提案「`pol_sem` を使え」はカーネルで裏取りした結果 _無効_**：
  `pol_sem` も `CHECK_TSKCTX_UNL`＝`sense_context()`（ISR で失敗）で弾かれる **タスク文脈専用**。
  **ASP3 には ISR から使えるセマフォ非ブロック取得のサービスコールが存在しない**。真ISRからの
  take は E_CTX 失敗を返すしかなく、give 側と違い «取得» は延期不能で保留救済もできない。
  ⇒ `esp_shim_sem_take` で E_CTX を検出し `shim_sem_take_ectx_total` で計数（give 側
  `shim_sem_ectx_total` と対称）＝blob が実際に ISR から take するかを実機で観測可能化
  （0 のまま＝死経路・無害／非0＝要再設計）。coex `take_from_isr` ラッパにも制約を明記。
- **#5 タイマ起床 sig_sem の critical外必達 救済** — D-2d の give 救済と同型に、E_CTX の起床要求を
  semID で保留（`esp_shim_signal_or_pend`）し `esp_shim_exit_critical` で精算
  （`esp_shim_wakeup_flush_pending`）。★真ISRからは sig_sem 自体が成立（CHECK_UNL）し E_CTX に
  ならないため救済対象は critical 由来のみ＝漏れ無し。rescue 実体は wifi/esp_shim.c、BT からは
  共有 esp_shim.h 経由で呼ぶ（C5/C6 は専用 esp_shim.h を持たず C3 のを共有＝target.cmake）。
- **#6 セマフォ delete/再利用の亡霊 give** — `esp_shim_sem_delete`/`_create` で当該スロットの
  `shim_sem_pend[i]` を清算（`_total` から先に引いてから 0＝アンダーフロー回避）。

## 報告のみ（#8/#9）＝実装せず・要実機反証

現構成で発火しない前提条件（多タスク esp_wifi / 高負荷）＋修正はいずれも設計判断／bond-critical
機構への介入を伴うため、**まず #4 同型の計装で到達性を確認してから着手**が安全（Opus サブエージェント
の分析結論と一致）。

- **#8 thread sem 共有**（★**計装実装済み `8f8b0a2`**）：`esp_shim_task_get_current()` がプール外タスクに
  一律 sentinel `&shim_main_thread_sem` を返し、`esp_shim_thread_semphr_get()` が単一共有 sem を返す。
  複数プール外タスクが esp_wifi 同期 API を叩くと give/take を奪い合う。**現デモは単一タスク＝発火不能**。
  真の修正（tid→専用 thread_sem の側テーブル）は共有 CRE_SEM プール枯渇リスク＝要設計判断なので見送り、
  **「2つ目の相異なるプール外 tid が sentinel に解決した」瞬間を検出**して `shim_main_thread_conflict++`＋
  `LOG_EMERG`（halt はしない）＝**沈黙化けを «大声化»**。複数タスク WiFi 制御を設計するなら本当の
  per-tid sem 化を検討する指標にもなる。C3/C5/C6。
- **#9 queue sem_debt**（★**計装実装済み `ac0f319`**）：ISR/E_CTX 送信がトークン未消費でスロット確保し
  `sem_debt++`。不変式 `token = free_slots + sem_debt`。`free_slots==0 && sem_debt>0` で task 側 `twai_sem`
  が成功→`slot_alloc` 失敗→`return(0)`＝portMAX_DELAY のブロッキング契約違反（near-full×ISR-debt×task送信
  の三重条件＝高負荷限定）。真の修正（案A=残 tmo で再ブロック＝過剰待ち懸念／案B=debt 相殺で token を
  実空きに再設計＝D-2c bond-critical 会計の再設計＝回帰大）は見送り、**当該パスで `shim_que_debt_conflict++`
  ＋ブロッキング時のみ `LOG_WARNING`**（戻り値/挙動は不変=非回帰）。実機高負荷で本窓が踏まれるか
  （カウンタ非0）を観測可能にする指標＝非0 なら案A の実装を評価。C3/C5/C6。

## 手順メモ（再利用可）

- Codex 実践知＝`~/agents_playbook/codex-review.md`・`memory/reference_codex_review.md`。
  このPCは `apparmor_restrict_unprivileged_userns=1` で `--sandbox read-only` が動かず
  `danger-full-access` 必須。stdin は `< /dev/null`。smoke test を先に打つ。
- ★**外部ツール/他エージェントの確信度は自己申告＝根拠にしない。全件独立検証。潜在/現行を区別。**
  今回 Codex は誤検出1（#3・古い lwIP 契約）＋無効な修正提案1（#4・pol_sem）。fable の方が正確だった。

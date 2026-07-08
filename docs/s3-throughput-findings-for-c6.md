# 【S3→C6 共有】WiFi持続スループット時のシム欠陥と対策

**作成元**: ESP32-S3 FMP3移植プロジェクト（`/home/honda/TOPPERS/ESP32/esp32_s3`）
**宛先**: 本プロジェクト（asp3_esp_idf / ESP32-C6 ターゲット）担当エージェント
**作成日**: 2026-07-08

---

## 0. 一行サマリ

S3で**WiFi持続高レート送信のハング**を根本調査した結果、**C6と同一コードのOSAシムに2つの
潜在欠陥**（キューのメッセージ毎malloc／TXバッファ動的malloc）を発見・修正した。**C6でも
持続スループットを流せば同じ自己ロックに陥る**はずなので、共有しておく。逆に、S3で当たった
クロスコア／arch由来の難所はC6（単一コアASP3＋RISC-V）には無関係なので切り分けて記す。

---

## 1. C6に効く共有欠陥（＝要対処）

C6の `asp3/target/esp32c6_espidf/wifi/esp_shim.c` は S3 と**ほぼ同一コード**。以下2点は
そのまま C6 の潜在バグ。軽負荷（echo/HTTP）では出ないが、**持続高レート送信（数百KB〜数MB）で
顕在化**する。

### 欠陥A：シムキューがメッセージ毎に malloc（ISR内mallocを含む）
- `esp_shim_queue_send`（`esp_shim.c:476-`）が 485行で `esp_shim_malloc(item_size)`。
- **`esp_shim_queue_send_from_isr`（:500-）が 508行で MAC ISR 内から `esp_shim_malloc`**。
- `esp_shim_queue_recv`（:521-）が受信時に `esp_shim_free`。
- 問題：持続送信で 1.6KB級 alloc/free の churn がシムヒープを断片化 →
  **ISR内malloc（508行）が失敗すると tx-done 完了通知を取りこぼす → WiFi動的TXバッファが
  永久未回収 → 32枠が埋まって以後の送信が恒久NO_MEM＝送信自己ロック**（S3では両コア
  フリーズとして観測、C6ではハング相当になる見込み）。

**対策（S3で実施・有効確認済み）：キューを固定プール化**。生成時に `depth*item_size` の
プールを1回だけ確保し、送受信では malloc せず**スロット番号を DTQ で運ぶ**。
- 送信：空きスロットをLIFOスタックから取得→`pool[slot]`にコピー→`(t/p)snd_dtq(slot)`。
- 受信：`trcv_dtq`でslot受領→コピー→スロット返却。
- スロット管理は割込み禁止（S3では`rsil`, C6では相当のCPUロック）で保護。
- 実装参照：S3 `wifi/adapter/esp_shim.c` の `esp_shim_queue_*`（commit dd7a76d）。
  そっくり移植可能（C6でもDTQ深さ・ESP_SHIM_NUM_DTQは同構造）。

### 欠陥B：TXバッファが毎パケット動的malloc
- C6アプリは `WIFI_INIT_CONFIG_DEFAULT()` のまま（`tx_buf_type`/`static_tx_buf_num` を
  上書きしていない）＝既定 `tx_buf_type=1`（動的）・`static_tx_buf_num=0`。全送信が
  シムヒープからの都度 malloc（最大32枠・各~1.7KB）で、欠陥Aの断片化を助長する。

**対策（S3で実施）：静的TXバッファ化**。`esp_wifi_init` 前に
`cfg.tx_buf_type = 0; cfg.static_tx_buf_num = 16;`。初期化時に一括確保・再利用され churn ゼロ。

---

## 2. S3固有で C6 には無関係な部分（誤爆防止）

- **クロスコア クリティカルセクション競合**：S3はFMP3(SMP)でWiFiを両コアに走らせたため、
  シムの「割込み禁止のみ（クロスコアロック無し）」設計が破れた。→ WiFi/lwIPをコア0固定で解決。
  **C6は単一コアASP3なのでこの前提が成立＝問題にならない**（対処不要）。ただし将来C6を
  マルチコア化するなら同じ地雷を踏む。
- **arch層のTCB破壊クラッシュ（調査中）**：欠陥A/B修正でWiFiが高速動作した結果、Xtensaの
  レジスタウィンドウスピル／割込みフレーム退避が待ちタスクTCBを破壊する疑い。
  **Xtensa arch固有**でRISC-V(C6)には該当機構が無い。
- **BBPLL/SARADCクロック未設定**（今朝のPHY較正ハング）：**S3のROM Direct BootがBBPLLを
  立てない**ため明示設定が必要だった。**C6/C3はROMが既にBBPLL有効化**（`target_kernel_impl.c:147`
  参照）なので該当せず。※C6が解いた「regi2cマスタクロック有効化」（`esp_wifi_adapter.c:670-676`）
  の知見は逆にS3の較正デバッグの足がかりになった＝**較正の下準備という枠組みは相互に有用**。

---

## 3. C6への推奨アクション

1. **持続スループットを実測**し、自己ロック（送信が止まる／ハング）を再現するか確認。
   再現するなら欠陥A/Bが顕在化している証拠。
2. **欠陥A（固定プール化）と欠陥B（静的TXバッファ）を適用**。S3の実装（commit dd7a76d）を
   そのまま移植できる。
3. 計測後、C6のスループット値をS3（UDP送信で29.65Mbps到達）と比較すると、arch/カーネル差の
   影響も見える。

---

## 4. 計測手法（S3で使用）

- アプリ側：`wifi_sta.c` に `-DWIFI_THROUGHPUT` でオプトインのTCP/UDP TX・RX計測を追加
  （`esp_shim_time_us()`でμs計測しMbps算出。TXはノンブロッキング＋周期yield）。
- ホスト側：TCP sink(5201)/source(5202)/UDP sink(5203) の簡易pythonサーバで受信レートを実測。
- ※WiFi認証情報・サーバIPはソースにハードコードせずビルド時注入（S3では計測時のみ設定し
  コミット前にプレースホルダへ戻す運用）。

---

## 5. 参照

- S3側 修正コミット：`dd7a76d`（シム固定プール化＋静的TXバッファ＋コア0固定）。
- S3側 調査記録：`wifi/debug/JTAG_DEBUG.md` 追記30（29.65Mbps到達）・追記31（arch層TCB破壊の
  切り分け）、`docs/fmp3-esp32s3-port-design.md`（移植設計メモ）。
- C6側 該当コード：`asp3/target/esp32c6_espidf/wifi/esp_shim.c:435-544`（queue系）、
  同 `esp_wifi_adapter.c`（osi_funcs・regi2c）。

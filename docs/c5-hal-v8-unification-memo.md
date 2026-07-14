# メモ：C5 Wi-Fi/BLEのhal(v8)統一検討——「v8 blob非互換」判定の再検証（未実施・後日実施）

記録日：2026-07-14。発案＝ユーザー。「C5の真因はlibphy/libppの
バージョンではなくデバイスレジスタのアクセス権（APM，実施42/43）
だったのだから，C3/C6と同じhal(esp-hal-3rdparty)版に戻せるのでは」。

## 論拠：実施09の「hal v8 blobはeco2非互換」判定は交絡していた

実施09がv8→v9移行（実施10）を決めた時点のASP3環境には，後に発見
された根本欠陥が少なくとも2つ共存していた：

1. **クロック鎖の根本欠陥**（実施32〜34で発見・修正）：ROMがBBPLLを
   40MHzプロファイルで較正し576MHzに誤ロック＋CPUルートクロック
   未切替。IQ推定較正が収束しないことはblob世代と無関係に説明可能。
2. **APM遮断**（実施42で発見・実施43で恒久化）：`bootloader_init_mem()`
   未実行によりMODEMマスタ（APM master id=4）がREE2のまま，HP_APM
   ctrlフィルタがモデムのHP SRAM（TX/RXパケットバッファ）アクセスを
   恒常ブロック。

当時の対照は「ASP3(v8) vs stock(v9)」でOS/ブート列とblob世代の
2変数が同時に変化しており，後のハンドオフ実験群（実施26-30/39）が
証明した真の差はブート列だった。実施09の逆アセンブル所見（v8/v9で
`phy_iq_est_enable_new`のアルゴリズムが異なる）は「コードが違う」
ことの証明であって「v8がeco2で動かない」ことの証明ではない。
**v8 blobは正しく構成された環境（クロック鎖修正＋APM解除後）で
一度も試されていない。**

## ただし保証はない（事前に明記しておく反証可能性）

- v9のアルゴリズム差が**eco2適応そのもの**である可能性は残る
  （実施08のeco3.ld騒動＝halスナップショットのC5対応がシリコンrev
  面で未成熟だった傍証）。
- 5GHz（v9では実施45で実証済み，ch48/RSSI-61）がv8 blobで動くかは
  未知。
- どちらに転んでも価値がある：v8で較正が再びハングすれば「v8は真に
  eco2非互換」が今度こそクリーンに確定し，v6.1続行の根拠が固まる。

## 実施計画（後日）

1. **v8統合の復元**：実施10直前（コミット`45f7532`の親）の
   `asp3/target/esp32c5_espidf/esp_wifi.cmake`＋`wifi/`一式を
   git履歴から変種として復元（ビルドオプションで切替できる形を推奨。
   例：`ESP32C5_WIFI_HAL_V8=ON`）。
2. **後続修正の仕分け・再適用**：
   - arch/target共有層の修正（クロック鎖＝bootloader_clock_configure
     相当・BBPLL 48MHzプロファイル再較正・SWDTキー・CLIC修正群・
     APM解除`esp32c5_r42_apm_unblock`）は**そのまま生きる**（変更不要）。
   - v9ファイル側に入った修正の再適用が必要：`g_misc_nvs`のNULL保護
     （実施45）・送信毎malloc→固定プール化（実施46，S3欠陥A）・
     putchar/printf系スタブの現状версия等。v8復元ファイルとの差分を
     `git diff`で棚卸しして1件ずつ移す。
   - v9専用の追加（CONFIG_SOC_*ミラーのv9必要分・`_wifi_pm_sleep_lock_*`
     等のv9フィールド・freertos_stub）はv8では不要のはず——ただし
     削除ではなく変種ガードで分離。
3. **ビルド検証**：halはsubmoduleなので**クラウド環境/CIでもビルド可**
   （IDF v6.1ローカルパス依存が消える——統一の最大の利点）。
   wifi_scan＋lwIP系＋BLE（bt_smoke_c5/ble_host_smoke_c5——BLEも
   halの`lib_esp32c5`へ戻せるか要調査）の全構成でエラー0を確認。
4. **実機判定**（判定点3つ・各2ブート再現）：
   (a) PHY較正が収束するか（旧IQ-estハング点＝`phy_iq_est_enable_new`
       done bit）——ここで再ハングならv8非互換が確定，v6.1続行
   (b) scanでAP検出（2.4GHz）
   (c) connect→DHCP→ping→TCP/UDP＋**5GHz**（ch48等）
5. **判定後**：
   - 成功→C3/C6/C5をhal一本に統一。`esp_wifi.cmake:46`のローカル
     パス直書き（`/home/honda/tools/esp-idf-v6.1`）を撤去。
     docs（c5-bringup.md・README）へ実施NNとして記録。
   - 失敗→v6.1依存を「ローカルパス」から「pinned submodule化
     （esp-idf v6.1-beta1）」へ変えて依存を明示化するフォールバックを
     検討。実施09の結論を「正しく構成された環境での再確認済み」に
     格上げして記録。

## 注意

- 実施記録の作法どおり，事前予測を固定してから実機判定を行う
  （予測：候補=クロック＋APM修正済み環境ならv8較正は収束する。
  外れたらそれ自体が実施09の追認として価値がある）。
- BLE側（C5はIDF v6.1のbt.c/ble.c/libble_app.a/NimBLEを使用中，
  D-2b達成済み）をhalの`lib_esp32c5`（esp32c5-bt-lib submodule）へ
  戻せるかは別途調査（wifi統一が成功した場合の第2段）。
- 本メモ作成時点でコード変更なし。

# 【依頼】C3実機2台による BLE `emi.c:164` ブロッカーの真因特定・解決

**依頼元**: ESP32-S3 FMP3移植プロジェクト（`/home/honda/TOPPERS/ESP32/esp32_s3`, ブランチ `feat/xtensa-esp32s3-arch`）
**宛先**: 本プロジェクト（asp3_esp_idf / ESP32-C3 ターゲット）担当エージェント
**作成日**: 2026-07-08

---

## 0. 一行サマリ

**C3実機2台の side-by-side 比較**で、`esp_bt_controller_enable()` が blob 内部アサート
`BLE assert emi.c 164`（EMページ所有権テーブル不整合）で停止する**真因を特定・解決**してほしい。
併せて **openocd を版上げ**（Linux USB-JTAG 安定化）してから着手すること。

---

## 1. 背景・なぜこの依頼か

- S3 側では WiFi が完全動作（スキャン→WPA2→lwIP→DHCP→TCP/UDP/HTTP/HTTPS、デュアルコア動作）。
  次のターゲットは **BLE 対応**だが、その最大リスクが `emi.c:164` ブロッカー。
- **ESP32-S3 と ESP32-C3 は同一の BLE コントローラ系列**（`libbtdm_app.a`、
  `controller/esp32c3/bt.c`、S3もこれを使用）。よって:
  - **C3 で `emi.c:164` の真因を特定・修正できれば、その修正はほぼそのまま S3 に効く**
    （ROM 実アドレスのみ C3/S3 で差、修正内容＝呼び出し側シーケンスはチップ非依存の公算大）。
  - C3 は **既に BT シムが動き、`emi.c:164` を再現でき、JTAG 調査記録もある**
    （`docs/bt-shim.md`）→ S3 の arch 移植を待たずに blocker 調査へ即着手できる。
- WiFi の解決手法（レジスタ書き漏れを実機レジスタ差分で発見）と違い、`emi.c:164` は
  **「enable 前後で real ESP-IDF が踏む初期化シーケンスの欠落」**の公算が高い
  （`bt-shim.md` 有力仮説）。Direct Boot が ESP-IDF 標準ブートローダの較正シーケンスを
  スキップする差分が本命候補。→ **比較対象を「レジスタ差分」から「初期化シーケンス差分
  ＋EMテーブル状態差分＋較正データ」へ拡張**するのがこの2台比較の肝。

---

## 2. ★ボード・ポート番号（ユーザー記入欄）

**使用ボード: M5Stamp C3U Mate ×2（native USB Serial/JTAG 内蔵）**
現状 **未接続**。ユーザーが接続後に下表のポート番号を記入する。
（接続を指示されたらユーザーが挿すので、判明したポートをここに追記してもらうこと。）

| 役割 | ボード | ポート（/dev/ttyACM*） | 備考 |
|---|---|---|---|
| 基準機A（real ESP-IDF + NimBLE, enable成功構成） | M5Stamp C3U Mate #1 | `______________` ←記入 | |
| 被験機B（FMP3 + bt_smoke, emi.c:164再現） | M5Stamp C3U Mate #2 | `______________` ←記入 | |

> 補足: M5Stamp C3U Mate は native USB（USB Serial/JTAG）。コンソール＝書込みと同じ
> `/dev/ttyACM*` に 115200bps。2台同時接続時はポート番号が別々に割り当たるので、
> `ls -l /dev/serial/by-id/` 等で個体を識別して上表に対応付けること。

---

## 3. 事前準備

### 3-1. openocd の版上げ（必須・最初に実施）
S3 側で Linux の USB-JTAG が不安定な問題を **openocd-esp32 の版上げで解消**した実績あり。
C3 でも同版に更新してから着手すること（2台同時 JTAG 接続の安定性のため特に重要）。

- 推奨版: **`openocd-esp32 v0.12.0-esp32-20260703`**（S3側 `docs/environment-setup.md` と同一）
- 入手: Espressif の openocd-esp32 リリース（`github.com/espressif/openocd-esp32` のprebuilt）
- C3 は `esp32c3-builtin.cfg` 相当の設定で内蔵 USB-JTAG に接続（外付けプローブ不要）。
- 版上げ後、旧版で発生していたハングアップ頻度が下がることを軽く記録しておくと良い。

### 3-2. 2台の役割セットアップ
- **基準機A**: 素の ESP-IDF **v6.1.0**（`bt_smoke` と同一 sdkconfig・可能なら同一 blob）で
  NimBLE の最小 example（`esp_bt_controller_enable(BLE)` が **成功**する構成）を書き込む。
- **被験機B**: 本プロジェクトの **FMP3 + `apps/bt_smoke`**（`emi.c:164` で停止する既知状態）。
  - ビルド: `-DESP32C3_BT=ON`（`ESP32C3_WIFI` とは同時ON禁止）。`docs/bt-shim.md`・
    `esp_bt.cmake` 参照。

---

## 4. 調査手順（side-by-side 比較）

`bt-shim.md` の残課題に沿って、以下を 2台同時 JTAG で実施する。

1. **両機の同一ブレーク点で状態 dump ＆ diff**
   両機に JTAG を同時接続し、既に特定済みのアサート直前
   （`r_emi_em_base_init` / `r_emi_get_mem_addr_by_offset+166` 付近＝`emi.c:164` 直前）に
   `hbreak` を張り、ブレーク時点で以下を dump して A/B 差分を取る:
   - **EMページ所有権テーブル**（`0x60031220` 起点10エントリ 他、`bt-shim.md` で追跡済みのMMIO領域）。
     特に**ページ4（オフセット0x1000）のテーブルエントリ**（不整合の当該箇所）。
   - `r_emi_em_base_init` 内の**7回の内部malloc結果アドレス**と EM 基底。
   - enable 直前の **PHY/RF・クロック・`PERIPH_BT_MODULE`** 関連レジスタ。
2. **基準機Aの関数トレース（本命）**
   基準機Aで `esp_bt_controller_init` → `enable` の間に呼ばれる**ROM関数トレース**
   （ROMシンボル解決）を取り、被験機Bの `bt_smoke` シーケンスと突き合わせて
   **「Aだけが呼んでいる初期化」＝ページ4記述子を埋める犯人関数**を洗い出す。
   - 参考深掘り対象: `r_emi_init` / `r_lld_core_init` 等（`bt-shim.md` 記載）。
   - Direct Boot がスキップする**PHY較正データのNVS読込・クロック較正・RTC** 等が
     enable 前提になっていないかを重点確認。
3. **修正 → enable 成功 → VHCI 往復**
   欠落シーケンスを `bt_shim.c` / `bt.cfg` 側に補完し、`esp_bt_controller_enable(BLE)` を
   成功させる。続けて **HCI Reset 送信 → Command Complete 受信**（`bt_smoke` の VHCI
   ループバック＝Phase D-1 完了基準）を確認する。

---

## 5. 成果物・完了基準

- [ ] openocd 版上げ完了（Linux 安定化を軽く記録）
- [ ] A/B の EMテーブル・malloc・レジスタ差分の記録
- [ ] `emi.c:164` の**真因（欠落初期化ステップ）の特定**
- [ ] 被験機Bで `esp_bt_controller_enable(BLE)` 成功 → **VHCI で HCI Reset 往復**（Phase D-1完了）
- [ ] `docs/bt-shim.md` を更新（真因・修正内容・A/B比較手順を追記）
- [ ] **S3 移植向けメモ**: 修正のうちチップ非依存部分（＝S3へ流用可能な部分）と、
      C3/S3 で差が出る部分（ROMアドレス等）を明記して S3 側へ引き継げる形にする。

---

## 6. 参考

- `docs/bt-shim.md` … C3 の BT シム実装・`emi.c:164` 調査の全記録（本依頼の一次情報）。
- `asp3/target/esp32c3_espidf/bt/`（`bt_shim.c` / `bt.cfg` / `stub/include/freertos/*.h`）
- `asp3/target/esp32c3_espidf/esp_bt.cmake`、`apps/bt_smoke/`
- `README.md` の Phase D-1 行（現状 = enable 開始まで到達・`emi.c:164` でブロック）。
- S3側の関連知見（`/home/honda/TOPPERS/ESP32/esp32_s3/wifi/debug/JTAG_DEBUG.md`）:
  WiFi blob の RNG レジスタ誤り（C3アドレス流用の罠）等、C3資産流用時の注意例あり。

---

## 7. 注意

- クローズドソース blob（`libbtdm_app.a`）依存のため、最終手段は実機JTAG＋ROM逆アセンブル。
- **原因がシーケンス欠落なら C3 の修正はほぼそのまま S3 に効く**が、**S3 実機での再現有無の
  最終確認は必須**（ROM が C3/S3 で別物のため）。この点を成果物メモに明記すること。
- ホストスタックは軽量な **NimBLE 推奨**（Bluedroid は重RAMで不適）。ただし本依頼の範囲は
  **Phase D-1（コントローラ enable + VHCI 往復）まで**。NimBLE ホスト統合（D-2）は次段。

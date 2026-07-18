# コードレビュー依頼：TOPPERS/ASP3 × esp-hal — WiFi/BT blob の v5.5.4 統一ほか

あなたはこのリポジトリ全体を閲覧できる上級レビュアです。ベアメタル組込み
（TOPPERS/ASP3 カーネル）と ESP32 無線 blob 統合の変更を、**実機物証と
コードの両面から**批判的にレビューしてください。日本語で回答してください。

## 0. リポジトリの前提（重要）

- **TOPPERS/ASP3 カーネル**（RTOS）＋ **esp-hal-3rdparty**（`hal/` submodule）を
  ESP32-C3/C5/C6 上で動かす統合リポジトリ。**Direct Boot**（ESP-IDF の
  startup・FreeRTOS は不使用。ASP3 自前ブート）。
- 正本：`README.md`、`asp3/asp3_core/docs/dev/esp-idf-integration.md`、`CLAUDE.md`。
- **禁則（レビュー時も遵守が正しいか確認してほしい）**：
  1. `asp3/asp3_core/`（submodule）を直接編集しない。カーネル/共通arch/
     チップ依存部の変更は asp3_core 側で行い submodule bump する。
  2. `hal/`（submodule）を直接編集しない。差分はラッパ/シムを本リポジトリ側
     （`asp3/target/`）に置く。
  3. カーネル内で動的メモリ確保を使わない（WiFi/BT blob 要求のヒープは
     カーネル外＝アプリ/ライブラリ層）。
- **ボード事情**：無線（PHY/PLL）は真の電源断（cold）と RTS リセット（warm）で
  挙動が異なる。C5/C6 は cold で register_chipv7_phy の PLL ロックが問題になる。

## 1. レビュー対象

- ブランチ：`claude/blob-unify-v5.5.4`（本体）＋ submodule `asp3/asp3_core`
  ブランチ `feat/esp32c6`（先端 `9904a44`）。
- 変更範囲：`git log --oneline main..HEAD`（20 commit）／`git diff --stat main..HEAD`
  （29 ファイル・+2471/-119 行）。
- **正本ドキュメント（先に読むべき）**：
  - `docs/blob-unify-v554.md` … WiFi/BT の v5.5.4 統一の全記録（§1-12）。**最重要**。
  - `docs/config-audit.md` … 手書き CONFIG_XX vs 動作 ESP-IDF の監査（PVCY 型欠落）。
  - `docs/wifi-shim-c6.md` 実施92 … C6 WiFi の pc=0 crash 帰属（double-reset 由来）。
  - `docs/c5-toolchain.md` … esp-15.2 ツールチェーン検証。

## 2. 変更の要旨（各々の主張と根拠）

1. **WiFi blob 統一（C3/C5/C6：hal(v8)→ESP-IDF v5.5.4(v8)）**。可逆オプション
   `ASP3_WIFI_BLOB_HAL`（既定 OFF=v5.5.4）。ABI 差（`CONFIG_SOC_WIFI_HE_SUPPORT`
   起因の `wifi_os_adapter.h` フィールド差）を **verbatim コピーの override
   ヘッダ**（`.../idf_v554_override/esp_private/wifi_os_adapter.h`, 196 行）で
   サロゲート差替え。実機：C3/C5/C6 とも scan 完走を主張。
2. **BT blob 統一（C5/C6：v6.1→v5.5.4）**。可逆オプション `ASP3_BT_IDF_V554`
   （**既定 ON=v5.5.4 へ flip 済**）。根拠＝v5.5.4 の `libble_app.a`/`libphy.a`/
   `libbtbb.a` が v6.1 と **md5 バイト同一**（相違は `libcoexist.a` のみ）＋
   C5 で cold full-BLE adv 実証。
3. **C3 BT の v5.5.4 化＝実装済だが実機 bond 失敗 → hal 維持**（`ASP3_BT_IDF_V554`
   既定 **OFF=hal**）。詳細は §4 の重点項目。
4. **SM_SIGN_CNT 定義**（`CONFIG_BT_NIMBLE_SM_SIGN_CNT=1` を全4 BT 変種へ）。
   config 監査で見つけた PVCY 型欠落（ESP-IDF Kconfig 既定 y に対し未定義）の解消。
5. **CLIC 出口正規化**（asp3_core `arch/riscv_gcc/common` + C5 `chip_support.S`）＝
   割込み出口の mret 非経由2経路を FMP3 同様の出口正規化型へ変更、C5 の
   synthetic mret（実施28）撤去。test_porting 6/6→8/8。
6. **C6 WiFi 实施92**＝blob 統一後の pc=0 crash は v5.5.4 回帰でなく調査ハーネスの
   double-reset 由来アーティファクトと帰属。

## 3. 実機検証済み／未検証の区別（前提として扱ってよい物証）

- WiFi：C3 19-43 AP・C5 20 AP・C6 20 AP（single-reset）scan 完走を実機で確認。
- BT：C5 は v5.5.4 で cold の full-BLE adv（`ASP3-C5-BLE`）実機確認。C5/C6 warm 動作。
- C3 BT：**hal で full bond 成功／v5.5.4 で AuthenticationTimeout**（同一 board・
  back-to-back A/B・複数回再現）。
- C6 BT は cold で register_chipv7_phy hang（**本レビュー対象外の既知 deferred 壁**）。

## 4. 重点レビュー項目（優先度順・具体的な問い）

### ★A（最重要）C3 BT v5.5.4 の SMP bond 失敗の根因
`asp3/target/esp32c3_espidf/esp_bt.cmake`（`ASP3_BT_IDF_V554`）で controller
（`bt.c`, osi `0x0001000A→0B`・新 field `_malloc_retention`）＋PHY＋4 blob
（`libbtdm_app.a` hal=`dfdadb9d` → v5.5.4=`d9753a31`）を v5.5.4 へ替えると、
adv/connect は成功するが **SMP が ~30s で AuthenticationTimeout**（`Paired/Bonded no`）。
NimBLE host・`bt/stub`・`wifi/esp_shim.c` は hal のまま（HCI が ABI 境界）。
- 我々の局在化：PVCY=1 は compile 済＋**stock ESP-IDF は同 `d9753a31` blob で
  bond 成功** → 残差は「d9753a31 blob を我々の統合（bt.c config / esp_shim /
  HCI flow control）に載せた非互換」に局在（因果未確定）。
- **問い**：コードを読んで、暗号確立後の SMP 鍵配布フェーズで ACL/HCI が
  詰まる箇所を特定できるか？ 特に (1) `bt.c` が blob へ渡す `esp_bt_controller_config_t`
  の `CONFIG_BT_CTRL_BLE_*` 既定と v5.5.4 blob の期待値の齟齬、(2) esp_shim の
  HCI flow control（`host_num_completed_packets` 等）と新 blob のバッファ管理の
  相互作用、(3) osi table の field 齟齬（`_malloc_retention` 以外の並び）、を
  file:line で検証 or 反証してほしい。stock IDF（`~/tools/esp-idf`）の bt.c/config と
  我々の供給値を突合するのが有効。

### ★B 既定 flip の安全性（WiFi=v5.5.4・BT=v5.5.4 for C5/C6）
`ASP3_WIFI_BLOB_HAL` 既定 OFF・`ASP3_BT_IDF_V554`（C5/C6）既定 ON。
- **問い**：md5 バイト同一（BT）＋実機 scan/cold-adv（WiFi/BT）を根拠に既定を
  v5.5.4 へ倒したのは妥当か？ 見落としている回帰面（`libcoexist.a` 差の影響、
  WiFi+BT coexist 未着手時の前提、可逆性の穴）はないか？ reversibility
  （`-DASP3_*` で戻せる）が実際に全経路で担保されているか cmake を確認してほしい。

### ★C CLIC 出口正規化（カーネルレベル・高リスク）
asp3_core `feat/esp32c6` の `a888d48`（`arch/riscv_gcc/common/{riscv.h,core_support.S}`）
＋本体 `7ab2590`（`asp3/arch/riscv_gcc/esp32c5/chip_support.S`）。
- **問い**：割込み出口の mret 非経由2経路（idle-return / delayed-dispatch）を
  `mepc=label; MPP=M; MPIE clear; mret` ＋チップ層 `mcause.mpil=0` 強制へ変更した
  設計は、CLIC の割込みネスト/優先度復帰の観点で正しいか？ mpil クリアの
  マスク（`0xFF00FFFF`）や MPP/MPIE の扱いに競合・取りこぼしはないか？
  QEMU 8/8 と実機の差（mie 有無）を踏まえた見落としはないか？

### ★D blob 統一機構の正しさ
`.../idf_v554_override/esp_private/wifi_os_adapter.h`（196 行 verbatim override）と
各 `esp_shim_blobglue.c`・`esp_wifi_adapter.c`・`target.cmake` の no-op stub。
- **問い**：override ヘッダのサロゲート差替えは ABI 的に正しいか（構造体
  レイアウト・関数ポインタ順）？ no-op stub 化した wpa_supplicant 3 関数は
  scan 経路で本当に不要か？

### ★E C6 WiFi 实施92 の帰属（因果の妥当性）
`docs/wifi-shim-c6.md` 実施92。
- **問い**：pc=0 crash を「double-reset 由来アーティファクト（blob 非依存）」と
  帰属した反証実験（single-reset 15/15 clean vs double-reset 8/8 crash）は、
  rigor（1実験1機構・N反復・両 blob）を満たすか？ 別解釈（例：特定 reset cause・
  タイミング窓）を排除できているか？

### ★F SM_SIGN_CNT・CONFIG 監査
- **問い**：`CONFIG_BT_NIMBLE_SM_SIGN_CNT=1` の有効化に副作用はないか
  （`#if` 文脈のみ使用は確認済）？ `docs/config-audit.md` が **見落としている**
  同型（chip 横断の共通欠落）の CONFIG_XX 差分は他にないか？

## 5. 期待する成果物

- 各重点項目（A-F）ごとに：**結論（同意/要修正/要追加検証）**＋**根拠（file:line）**
  ＋**反証条件**。相関を因果と早合点しないこと。
- 特に **★A（C3 bond 失敗の根因）** は、最小の修正候補があれば file:line で提案。
- 「実機物証に反する指摘」をする場合は、その物証（§3）とどう両立するかも述べる。
- read-only（コード読解と診断のみ。ビルド/実機操作は不要）。

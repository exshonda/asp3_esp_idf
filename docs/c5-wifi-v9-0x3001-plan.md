# esp_wifi_init_internal 0x3001 失敗の解決計画（C5・IDF v6.1/v9移行後）

対象：`docs/c5-bringup.md` 実施10の申し送り＝v9 blob の
`esp_wifi_init_internal` がエラー 0x3001（`ESP_ERR_WIFI_NOT_INIT`）を
返して失敗→rollback→RTC_SWDTリセットする問題。

本計画はコードレビュー（静的解析のみ・実機/ビルドなし。レポート全文＝
`tmp/c5_review_v9_0x3001.md`）に基づく。**レビュー環境にはIDF v6.1が
無い**ため、v9ヘッダ実物に依存する判定は「実機PC側での確認手順」として
明示してある。修正はいずれも本リポジトリのエディタブル層のみで完結する。

## 候補順位（レビュー結果）

### 候補1（確度高・最優先）：`CONFIG_SOC_WIFI_HE_SUPPORT` 未定義による `wifi_osi_funcs_t` のサイズ／`_magic` オフセットずれ

- `wifi/esp_wifi_adapter.c` の v9 構造体初期化に
  `#if CONFIG_SOC_WIFI_HE_SUPPORT` ガード付きフィールド
  （`_wifi_disable_ac_ax` 等）があるが、このマクロは**ビルドのどこにも
  定義されていない**（`sdkconfig_stub/sdkconfig.h` には `CONFIG_SOC_`
  プレフィックスのマクロが1件も無い）。
- C5 は実際には HE（Wi-Fi 6）対応（hal `soc/esp32c5/include/soc/
  soc_caps.h` に `SOC_WIFI_HE_SUPPORT (1)`）。本物のIDFビルドでは
  Kconfig が `SOC_*` を `CONFIG_SOC_*` へ自動ミラーするため
  （実例：`hal/nuttx/esp32c6/include/sdkconfig.h` に
  `CONFIG_SOC_WIFI_HE_SUPPORT 1`）、**Espressif 製 blob は
  「フィールドあり」の広い構造体を期待**している。
- v9 ヘッダ（`wifi_os_adapter.h`）側も同名マクロで当該フィールドを
  ガードしている場合、当方のコンパイル結果は関数ポインタ分だけ短い
  構造体になり、**末尾の `_magic` が誤ったオフセットで読まれて整合性
  チェックに落ちる**。0x3001 という「ソフトな拒否」と、失敗地点
  （rom version ログ後〜supplicant/phy_enable 前）に整合する。
- 機構の前提＝「v9 ヘッダの構造体定義自体が `CONFIG_SOC_WIFI_HE_SUPPORT`
  でガードされている」ことは、この環境では未確認（実機PCの
  IDF v6.1 実物で要確認）。ヘッダがガード無し（フィールド常在）なら
  サイズずれは起きず、当該フィールドが NULL 初期化になるだけ
  （その場合は候補1を棄却して候補2へ）。

**確認手順（修正より先に・反証を先に）**：
1. 実機PCで `grep -rn 'CONFIG_SOC_' <IDF>/components/esp_wifi/include/`
   を実行し、v9 ヘッダが参照する `CONFIG_SOC_*` を全列挙する。
   `wifi_os_adapter.h` の当該フィールドが本当に同マクロでガードされて
   いるかを確認。
2. サイズの直接確認：現行 `asp.elf` で
   `nm -S build/c5_idf61/asp.elf | grep g_wifi_osi_funcs` のサイズと、
   `CONFIG_SOC_WIFI_HE_SUPPORT=1` を定義した再ビルドのサイズを比較
   （4バイト以上増えれば機構成立）。
3. （任意・JTAG）`_magic` 直読み：`g_wifi_osi_funcs` の
   アドレス+サイズ-4 を `mdw` し、`ESP_WIFI_OS_ADAPTER_MAGIC`
   （ヘッダ記載値）と一致するオフセットが blob 期待と合うか、
   stock 参照機（C5#2）の同名シンボルと比較する。

**修正**：`sdkconfig_stub/sdkconfig.h` に、手順1で列挙された
`CONFIG_SOC_*`（少なくとも `CONFIG_SOC_WIFI_HE_SUPPORT 1`）を
soc_caps.h の実値どおりに追加する。恒久対策として、esp_wifi/esp_phy/
esp_coex の v9 ヘッダが参照する `CONFIG_SOC_*` 全件を soc_caps.h と
突合せてミラーしておくこと（今回の欠落はミラー機構自体が無いことが
根本原因のため、1件だけ足して終わりにしない）。

### 候補2（確度中）：`esp_coex_adapter` の v9 追随漏れ

候補1と同型の問題が coex 側（`coex_adapter_funcs_t`）にも起こりうる。
C3/C6 共有の coex アダプタが v8 のままなら、バージョン/magic/サイズの
いずれかで blob に拒否される。
- 確認：IDF v6.1 の `esp_coex/include/private/esp_coexist_adapter.h` の
  `COEX_ADAPTER_VERSION`・構造体フィールドと、当方の coex アダプタ実装を
  突合せ。
- JTAG判別：診断用グローバル `esp_shim_coex_pre_init_entered/done/ret`
  を直読みし、coex_pre_init が失敗を返していないか確認。

### 候補3（確度中〜低）：sleep-retention 系 no-op スタブの戻り値

新設4フィールド（`_wifi_bb/mac_sleep_retention_attach/detach`）と
no-op スタブ（`_regdma_link_set_write_wait_content`・
`_sleep_retention_find_link_by_id`）。レビューでは「0固定＝esp_err_t の
成功」でおそらく無害と評価。候補1・2で解消しない場合のみ、
stock（C5#2）に JTAG breakpoint を張って実際の呼出し有無・戻り値を対比。

### 反証済み（追わない）

- `wifi_init_config_t` のヘッダ混入（v8/v9混在）：ビルドは単一の
  IDF ヘッダ経由で一貫しており混入なし。
- NVS スタブ：`nvs_enable=0` 固定のため 0x3001 経路に到達しない。

## 副次修正：エラーテキストの可読化（どの候補でも先にやると効率が上がる）

1. **実装バグ**：`wifi/esp_shim_blobglue.c` の `printf()` weak スタブが
   コメント（syslogへ折り返す）に反して**何もせず `return 0`**して
   いる。`vsnprintf`＋syslog 出力（`esp_shim_log_write` 相当の既存経路）
   に修正する。blob が init 失敗理由を裸 `printf` で出している場合、
   これだけで原因が読める可能性がある。
2. **JTAGでの原文復元**：コンソールの化け（割込み禁止区間の
   ポーリング送信＋文字単位ドロップ）に依存せず、`syssvc/syslog.c` の
   リングバッファ `syslog_buffer[]` を JTAG で直読みすればログ原文を
   復元できる（コンソール状態非依存。アドレスは `nm` で取得）。

## 実施順（推奨）

1. 副次修正1（printf スタブ）＋候補1の確認手順1〜2（いずれも安価）。
2. 候補1が成立→`CONFIG_SOC_*` ミラー追加→再ビルド→実機で
   `esp_wifi_init` 完走確認。不成立→候補2の突合せへ。
3. `esp_wifi_init` 完走後は scan→AP 判定＝deaf-RX 本丸
   （`docs/c5-bringup.md` 実施09の確定事項により、stock では同一チップで
   AP 受信済み。ASP3統合下で AP=0 なら初めて「shim 環境要因」の議論に
   入る）。
4. 各ラウンドは `docs/c5-bringup.md` に実施NNとして追記し、本計画の
   候補の成立/棄却を明記する。

## 注意（調査の鉄則）

- 修正を入れる前に確認手順（サイズ比較・grep）で機構の成立を確かめる
  こと。「定義を足したら直った」だけでは candidates 1 と 2 の切り分けが
  つかない（両方まとめて直す変更を入れない）。
- stock 参照機（C5#2）は読み取り比較のみに使い、書換えはしない。

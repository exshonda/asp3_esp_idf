# hal（esp-hal-3rdparty）か ESP-IDF か — 依存基盤の判断メモ

> ✅ **実行完了（2026-07-21）**。本メモの結論＝**ESP-IDF 単一供給への移行は
> 実施され、完了している**。`hal`／`lwip` submodule は撤廃され、3チップ・
> 全構成で hal 参照 0 を `ninja -t deps` で実測済み。実行計画は
> `tmp/plan-espidf-only-hal-removal.md`（同じく実行完了）、到達点は
> `README.md`、証跡は `.steering/20260716-c3c5c6-esp-idf-supply-migration/`。
> 本メモは**判断の根拠と当時の比較検討**の記録として保存する
> （＝これから判断するための文書ではない）。

本リポジトリのチップ依存基盤を **esp-hal-3rdparty（`hal` submodule）** に置くか
**ESP-IDF** に置くかの議論を集約する。関連する実測の詳細は
`docs/hal-nuttx-version-map.md`（版管理マップ）を参照。調査は全て read-only。
作成日 2026-07-15。

## 0. 結論（先出し）

- **v5.5.4 blob 統一との整合・来歴の明確さ・将来の M5Stack/ESP32-S3 対応**を同時に
  満たす方向は、**esp-idf を `v5.5.4` タグで submodule 化し、共通基盤に据える**こと。
- hal は「esp-idf を非IDFビルド向けに平坦化した抽出物」であり、機能面で
  esp-idf の真部分集合。**hal に一切依存せず IDF のみに依存することは可能**
  （BT が既に `${IDF}` から直接コンパイルして実証済み）。
- 直ちに全面移行しない場合の最小策：現状の hal pin の**来歴を1行記録**する
  （再現性は SHA pin で既に確保済み。曖昧なのは人間可読な版札だけ）。

## 1. 現状の依存インベントリ（実測）

- **blob は既定で hal から採っていない**：WiFi は `ASP3_WIFI_BLOB_HAL` 既定 OFF →
  `IDF_V554=~/tools/esp-idf`（v5.5.4）から。BT は `${IDF}` から `bt.c`/`ble.c`/phy を
  直接コンパイル。hal blob は fallback のみ。
- **残る hal 依存は3カテゴリ**（v5.5.4 既定パスでも使用）：
  1. SoC/レジスタ/リンカ（`soc`/`hal`/`esp_rom/*/ld`）
  2. support 層ソース（`mbedtls` 4.0・`wpa_supplicant`）
  3. 低レベルC（`esp_hw_support`・`esp_hal_*`・`efuse`・`esp_phy`）
- 参照規模：約290が stock IDF に同名で存在（移行容易）、約69が hal 固有の
  再編（`esp_hal_clock/gpio/pmu/timg/rtc_timer/usb/security/ana_conv`）、
  `nuttx/` グルー4本（`include/mbedtls`・`src/esp_event.c`・`<chip>/include`）。

## 2. hal の来歴問題（`hal-nuttx-version-map.md` の要約）

- esp-hal-3rdparty は**リリースタグを持たない**。上流 NuttX も **tag でなく生 SHA**で
  pin（`arch/risc-v/src/common/espressif/Make.defs` の `ESP_HAL_3RDPARTY_VERSION`）。
- NuttX 安定版は全て **IDF 5.1.x** に張り付き、12.11 で **5.1.4→6.0.0 へ直接跳躍**、
  5.2〜5.5 を完全スキップ。∴**IDF 5.5.x に対応する hal スナップショットは上流に
  存在しない**＝「hal を v5.5.4 相当に pin」は不可能。
- 本 repo の hal `b90b183` は **NuttX master 現行 pin（=release/master.c=IDF 6.1.0）**と
  一致。既に「NuttX 方式（SHA pin）」で運用中で、たまたま最先端を引き継いだ状態。

## 3. mbedTLS のバージョン

- hal 同梱は **mbedTLS 4.0.0 + tf-psa-crypto 1.0.0**（IDF 6.x 世代）。
- WiFi の mbedtls は hal ソースからビルド（`esp_wifi.cmake` `MBEDTLS_DIR=hal/...`）、
  wpa_supplicant とペアの matched set。**blob は mbedtls を直接呼ばず crypto ラッパの
  C ABI 越し**＝blob 世代と mbedtls 版は独立。「blob=v5.5.4 だから mbedtls=3.x」は
  誤った結合。
- ∴ mbedtls 版は**基盤ツリーが決める**：hal のままなら 4.0、esp-idf v5.5.4 submodule
  なら 3.6系。IDF タグ化すれば版が自動決定し 3.x/4.0 論争は消える。

## 4. 開発環境構築の手間

- mbedtls（hal 供給）は `git submodule update` で揃い**追加 setup ゼロ**。3.x へ
  下げるだけなら別ツリーが要り手間が増える（＝4.0 維持が setup 上は有利）。
- 本当の手間は **BT の `${IDF}` ハードコードパス**（`esp_bt_idf61.cmake:50/52`＝
  `/home/honda/tools/esp-idf*`）。新開発機は同じパスに clone が要る（ToDo-2）。
- **esp-idf を submodule 化するとこのハードコードパスが消え、dual-tree（hal＋外部
  IDF）が single-tree になり自己完結**＝トータルの手間はむしろ減る。

## 5. 将来の M5Stack / ESP32-S3 対応

- ESP32無印/S3 は **Xtensa**。現 asp3_core は RISC-V のみ＝**Xtensa カーネルポート**が
  前提（IDF/hal 選択とは独立の最大の関門）。
- M5 ライブラリ（M5Unified/M5GFX＋各 Unit ドライバ）は **IDF ネイティブ**（ESP
  Component Registry 配布・Arduino-ESP32 も IDF コンポーネント）。**hal には M5 が
  必要とする `driver/`・`esp_lcd`・コンポーネントレジストリ・Arduino core が無い**
  （NuttX 向けに削がれている）。∴ M5 案件は **ESP-IDF 基盤一択**。
- M5 が前提とする IDF 版（実測）：
  - M5Unified `0.2.18`/M5GFX `0.2.25` とも **`idf_component.yml` に版制約なし**
    （frameworks=arduino/espidf/`*`）＝**v4.4〜v5.x を広くサポート**、特定版非強制。
  - 実効版は Arduino-ESP32 core 経由で決まる（core→IDF：2.0.x=4.4／3.0.x=5.1／
    3.1.x=5.3／3.2.x=5.4／**3.3.x=5.5**。core→IDF 対応は Arduino リリースノート由来、
    patch レベルは要突合）。
  - ∴**現行 M5/Arduino スタック（core 3.3）は IDF v5.5**＝**我々の v5.5.4 統一と整合**。
- M5 は FreeRTOS/app_main 前提のため、ASP3 Direct Boot 上では **FreeRTOS 互換シム**が
  別途必要（基盤が hal か IDF かに関わらず要るが、IDF なら FreeRTOS/driver 層が
  揃う分シムが楽）。

## 6. 「hal に一切依存せず IDF のみ」は可能か — 可能（移行面と代償）

- **可能**：hal は esp-idf の再抽出なので機能の欠落なし。BT の `${IDF}` 直接
  コンパイルで idf.py 無しの IDF コンポーネント利用は実証済み。
- **移行面**：約290参照は `${ESP_HAL_DIR}`→`${IDF}` 向け替えで解決。約69の
  `esp_hal_*` は stock IDF の `components/hal`・`esp_hw_support` へリマップ。
  `nuttx/` 4本は置換（mbedtls config は自前、esp_event は IDF 版 or shim）。
- **代償（hal がやってくれていた前処理を自前化）**：sdkconfig.h stub 提供・
  `idf_component_register` 相当のソース列挙・推移的ヘッダ通し・FreeRTOS 前提箇所の
  shim。BT で確立済みパターンの横展開。
- **むしろ楽になる点**：hal の mbedtls シンボル prefix patch は我々には不要
  （競合ホスト mbedtls が無い）。

## 7. 推奨と選択肢

- **推奨（長期）**：**esp-idf `v5.5.4` タグを共通 submodule 化**。来歴・mbedtls 版・
  setup 手間・M5 将来対応を一挙に解決し、C3/C5/C6 の blob 統一と ESP32/S3+M5 を
  同一 IDF タグで両立。hal は RISC-V 側レガシー/fallback として残置。
- **段階移行**：①C6 BT の `${IDF}` を submodule へ向ける最小 pilot（可逆）→
  ②WiFi support 層（mbedtls/wpa_supplicant/phy/hw_support）を IDF へ→③soc/hal/ld を
  IDF へ寄せ hal 撤去。
- **C 系も同じ IDF ベースへ寄せる（開発効率）**：到達点は「全チップを単一 esp-idf
  submodule（v5.5.4）に統一」。**駆動理由は M5 ではなく S3 開発との単一コードベース化**
  （M5 は displayless の C3/C6 基盤を強制しない＝§5 の別軸）。統一の利得は主に
  ①統合パターン・シム規約・sdkconfig/cmake 規約の一本化、②WiFi/BT シム
  （`esp_shim.c`・npl・`esp_intr_alloc` 等）が同一 IDF レイアウトを相手に再利用可、
  ③単一 submodule が RISC-V(C系)/Xtensa(S3系) 両アーキを賄う（アーキ差は asp3_core と
  linker/startup 側で、IDF コンポーネント・ソースは共通）。ただし条件：
  - **順序＝S3 先行でパターン確立 → C 系が後追い移行**（submodule レイアウト・シム規約・
    sdkconfig 扱い・ブートハンドオフを S3 で実証してから C 系を同じ土台へ）。
  - **可逆・漸進**＝C 系は現に動作中（C5 connect/DHCP/5GHz・bt_smoke 実績）。統一のため
    だけに一気に剥がさず hal 経路を fallback に残し、コンポーネント単位で移して回帰させ
    ない。BT の `${IDF}` 直コンパイル実証済みパターンの横展開。
  - 移行は新機能を足さない refactor＝S3 と共有する範囲の分だけ元が取れる。段階移行①の
    pilot を **S3/C 系の共通土台の最初の一歩**として S3 担当と共有設計する。
- **最小策（当面現状維持）**：`.gitmodules`/README に「hal `b90b183` =
  esp-hal-3rdparty `release/master.c` = NuttX master pin(2026-07-15) = IDF 6.1.0・
  mbedtls 4.0」と記録。曖昧さの実害を消す。

## 8. ブート方式（ESP32-S3/無印 は C3 と topology が変わる）

M5Stack 案件（ESP32-S3/無印）で「app_main 等のブートを可能な限り IDF に寄せるか」
への回答。「ブート」を分離して考える：

- **(a) ハードウェア立ち上げ**：二段ブートローダ、クロック/PLL、フラッシュ MMU・
  キャッシュ、**PSRAM 初期化**、brownout、efuse——スケジューラ起動前の OS 非依存部。
- **(b) OS スケジューラ起動**：`main_task` 生成 → FreeRTOS scheduler start → `app_main`。

**方針：(a) は IDF を最大限使う／(b) app_main は使わない。**

- **(b) app_main / FreeRTOS＝不使用**：app_main は FreeRTOS スケジューラ起動後に走る
  main_task であり、採用＝FreeRTOS をカーネルにすること。ASP3 がカーネルという前提と
  矛盾（2スケジューラが CPU を同時所有不可）。**ASP3 のカーネル起動（sta_ker）が
  app_main の位置を置き換える**。
- **(a) HW 立ち上げ＝IDF を使う（S3 では事実上必須）**：ESP32-S3/無印は **Xtensa で
  ROM Direct Boot を持たない**（S3 公式 startup ドキュメントに Direct Boot 記述なし＝
  通常の二段ブートローダ経由。一次確認済み）。∴ C3 の「二段ブートローダ無しの自前
  Direct Boot」は移植不可。フラッシュ MMU/キャッシュ・クロック・PSRAM を立ち上げる
  二段ブートローダが必要で、M5 は PSRAM にフレームバッファを置く（M5GFX）ため S3 の
  PSRAM/キャッシュ初期化は非自明＝成熟した **IDF ブートローダ＋スケジューラ前の
  `esp_system` HW init を借りる**のが合理的。C3 より IDF ブート依存が増えるのは正しい。
- **ハンドオフ線**：ROM → IDF 二段ブートローダ → `esp_system` HW init まで IDF、
  **IDF が `esp_startup_start_scheduler`（main_task 生成＋FreeRTOS 起動＋app_main）に
  入る直前で分岐し ASP3 カーネル起動へ渡す**。app_main の手前で止める。
- **却下済み代替**：IDF で app_main まで起動し ASP3 を FreeRTOS タスクとして動かす
  ホステッド方式＝二重スケジューラ・tick 競合・割込み配送衝突でハード RT が壊れる。
  「FreeRTOS 不使用」で排除済み。
- **混同禁止**：M5/Arduino ライブラリの FreeRTOS 呼出し（task/queue/`vTaskDelay`）は
  **実行時**の話＝ASP3 上の FreeRTOS 互換シムで解く。app_main とは別レイヤ。

∴ topology は C3 と変わる：C3＝ブートローダ無し・HW init も自前 → S3/無印＝**IDF 二段
ブートローダで HW/PSRAM を立ち上げ、app_main 直前で ASP3 へ渡す**。「可能な限り IDF を
使う」は **(a) HW 立ち上げは Yes、(b) app_main/スケジューラは No**。

担当エージェント向けの作業ブリーフ＝`docs/esp32s3-integration-brief.md`。

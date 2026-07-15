# ESP32-S3（/無印）統合ブリーフ — 担当エージェント向け

ESP32-S3（および同じ Xtensa 制約を共有する ESP32 無印）のチップ依存部を開発する
担当エージェントへ渡す方針と着手ガードレール。背景の正本は
`docs/hal-vs-espidf-decision.md`（特に §5 M5Stack・§6 IDF-only・§7 推奨・§8 ブート方式）、
`docs/hal-nuttx-version-map.md`、C3 実装は `asp3/asp3_core/docs/dev/esp-idf-integration.md`、
禁則は `CLAUDE.md`。

## A. 依存基盤の方針

1. **S3/無印の基盤は ESP-IDF（タグ付き submodule、`v5.5.4` を第一候補）。hal
   （esp-hal-3rdparty）は使わない。** M5 ライブラリ（M5Unified/M5GFX＋各 Unit
   ドライバ）は IDF ネイティブで、hal には M5 が要る `driver/`・`esp_lcd`・
   コンポーネントレジストリ・Arduino core が無い（NuttX 向けに削がれている）。
   hal を v5.5.4 相当に pin する案は不可能（上流に 5.5.x の hal が存在しない）＝検討不要。
2. **IDF 版の整合**：M5 本体は IDF 版を固定しない（v4.4〜v5.x）。実効版は Arduino-ESP32
   core 経由（core 3.3 = IDF v5.5）。`v5.5.4` を選べば M5 と既存 C3/C5/C6 の v5.5.4 blob
   統一の両方と整合。別版を選ぶ場合はコーディネータに要確認。
3. **統合モデルは ASP3 Direct Boot（＝ASP3 がカーネル）を維持。IDF の startup/FreeRTOS を
   丸ごと採用しない。** IDF コンポーネントは選択的に引く（既存 BT が `${IDF}` から
   `bt.c`/`ble.c`/phy を idf.py 無しで直接コンパイルするのと同じ流儀）。

## B. ブート方式（S3 は C3 と topology が変わる — 最重要）

「ブート」を2つに分けて設計。**(a) は IDF を最大限使う／(b) app_main は使わない。**

- **(a) HW 立ち上げ＝IDF を使う（S3 では事実上必須）**：二段ブートローダ、クロック/PLL、
  フラッシュ MMU・キャッシュ、**PSRAM 初期化**、brownout、efuse。
  - 重要事実：ESP32-S3/無印は **Xtensa で ROM Direct Boot を持たない**（S3 公式 startup
    ドキュメントに Direct Boot 記述なし＝通常の二段ブートローダ経由。一次確認済み）。
    ∴ C3 の「二段ブートローダ無しの自前 Direct Boot」は移植不可。二段ブートローダが要る。
  - M5 は PSRAM にフレームバッファを置く（M5GFX）ため S3 の PSRAM/キャッシュ初期化は
    非自明＝成熟した IDF ブートローダ＋スケジューラ前の `esp_system` HW init を借りる。
- **(b) app_main / FreeRTOS スケジューラ＝使わない**：app_main は FreeRTOS スケジューラ
  起動後の main_task。採用＝FreeRTOS をカーネルにすること＝ASP3 前提と矛盾。**ASP3 の
  カーネル起動（sta_ker）が app_main の位置を置き換える**。
- **ハンドオフ線**：ROM → IDF 二段ブートローダ → `esp_system` HW init まで IDF、**IDF が
  `esp_startup_start_scheduler`（main_task 生成＋FreeRTOS 起動＋app_main）に入る直前で
  分岐し ASP3 カーネル起動へ渡す**。app_main の手前で止める。
- **却下済み代替**：IDF で app_main まで起動し ASP3 を FreeRTOS タスクとして動かす
  ホステッド方式＝二重スケジューラ・tick 競合・割込み配送衝突でハード RT が壊れる。排除。
- **混同禁止**：M5/Arduino の FreeRTOS 呼出し（task/queue/`vTaskDelay`）は**実行時**の話
  ＝ASP3 上の FreeRTOS 互換シムで解く。app_main とは別レイヤ。

## C. 直視すべき長ポール

- **Xtensa カーネルポート（最大の関門）**：S3=LX7・無印=LX6。現 asp3_core は RISC-V のみ。
  arch/カーネル変更は asp3_core リポジトリ側で行い submodule bump（禁則①）。チップ依存部
  `asp3/target/esp32s3_espidf/` 等は本リポジトリ側に置く。
- **FreeRTOS 互換シム**：M5 実行時依存を ASP3 へマップ。カーネル内で動的メモリ確保を
  使わない（禁則③。ヒープはカーネル外）。IDF 基盤なら FreeRTOS/driver 層が揃う分シムが楽。

## D. 進め方（推奨順）

1. **設計メモを先に書く**：`docs/` に S3 統合計画（Xtensa ポート前提、IDF submodule 化の
   範囲、ブート(a)/(b)分離とハンドオフ実装、PSRAM/キャッシュ立ち上げ、FreeRTOS シム、
   M5 を載せる最小構成）をまとめ、コーディネータ確認を得てから実装へ。
2. **可逆・小さく**：cmake のパス定義で切替わる粒度を保ち、C3/C5/C6 の既存ビルドを
   回帰させない。IDF submodule 追加・パス規約は進行中の pilot と重複しないよう調整。
3. 各ラウンドは該当 docs に実施NN（背景/事前予測/結果/変更ファイル/検証）で追記。
   ビルドが通ることを確認してから commit・push。

## E. 確認が要る分岐（勝手に決めず必ず相談）

- 採用 IDF タグ（`v5.5.4` 以外を検討する場合）
- IDF submodule の配置・命名（既存 hal/lwip submodule 構成との整合）
- 二段ブートローダを IDF 純正で使うか、trim した自前にするか
- ハンドオフ点の実装（IDF startup のどこで分岐し ASP3 へ渡すか）
- FreeRTOS シムを自前実装するか、IDF の FreeRTOS を条件付きで載せるか
- Xtensa ポートを asp3_core 側でどう進めるか（別リポジトリの作業計画）

推測を因果と早合点せず、必要なら一次情報（esp-idf のタグ・S3 startup/bootloader
ドキュメント・M5 の idf_component.yml）を read-only で当たること。

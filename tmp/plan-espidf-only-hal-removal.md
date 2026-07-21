# 計画：asp3_esp_idf を esp-idf submodule(v5.5.4) 単一供給へ移行し ./hal と ./lwip を撤廃

> ✅ **実行完了（2026-07-21）**。本計画は**実施済み**——`hal`／`lwip` submodule は
> 撤廃され、供給元は `esp-idf` submodule 一本。3チップ・全構成で hal 参照 0 を
> `ninja -t deps` で実測し、C3/C5/C6 とも W1（Wi-Fi＋DHCP＋ping）・W2（BLE GATT）を
> **真cold** で達成済み。到達点は `README.md`、証跡は
> `.steering/20260716-c3c5c6-esp-idf-supply-migration/`、判断の背景は
> `docs/hal-vs-espidf-decision.md`。
> 本書は**これから実行する計画ではなく**、実行された計画の記録として保存する。

ローカルエージェント向けの実行計画。FMP3/Xtensa の esp32_s3 repo が
「esp-idf-only 化＋hal 撤廃＋lwip を esp-idf から」を実機GREEN で完遂済みで、その
実証手法を asp3_esp_idf(ASP3/RISC-V, C3/C5/C6)へ翻訳する。本 repo 側の背景正本は
`docs/hal-vs-espidf-decision.md`（§6 IDF-only・§7 段階移行・§8 ブート）、
`docs/hal-nuttx-version-map.md`。

## 1. 先行事例（esp32_s3 repo のファイルを直接読むこと）

- `.steering/20260716-esp-idf-only-milestone-summary.md`（全体到達点・段階の型）
- `.steering/20260716-esp-idf-submodule/design.md`（submodule 設計）
- `.steering/20260716-lx6-esp-idf-supply-migration/plan.md` + `evidence-stage1〜5`
- `.steering/20260716-lx6-esp-idf-supply-migration/evidence-stage4-lwip-and-hal-removal.txt`
- `.steering/20260716-seam-s3-lx7-port/evidence-s3-supply-migration-02-lwip-espidf.txt`
- `esp/boot/build_lwip_lib_espidf_esp32(s3).sh`（lwip ビルドの実レシピ）
- `esp/wifi/net/{netif_*.c, port/sys_arch.c, port/include/lwipopts.h}`（lwip ポート）
- `esp/config/esp32/hal_stub_include/`（hal_stub の vendor 実例）
- `esp/build_incflags_esp32s3_espidf.txt`（incflags と @IDF@/@ESPIDF@/@REPO@ 置換規約）

## 2. S3 repo との差分（翻訳時に効く）

- 本 repo は ASP3(RISC-V C3/C5/C6)、S3 repo は FMP3(Xtensa LX6/LX7)。**供給移行
  （ソースの出所を hal→esp-idf submodule に替える）はアーキ/カーネル非依存**でそのまま翻訳可。
- **C 系は ROM Direct Boot があるので seam(IDF bootloader ハンドオフ)は不要**。ブートは
  現行 Direct Boot 維持、変えるのは「供給」だけ＝S3 より小さいサブセット。
- toolchain は riscv32-esp-elf（S3 の xtensa-esp32s3-elf を置換）。

## 3. 段階（各段：可逆・チップ別・実機TAP検証・非回帰厳守）

- **段階0**：**esp-idf を submodule 追加**し外部ハードコードパスを置換（規約の具体は §3a）。
  既存の `IDF_V554=/home/honda/tools/esp-idf`・`${IDF}=…/esp-idf-v6.1`・`esp_bt_idf61.cmake:50/52`
  等（＝ToDo-2）を submodule 参照へ。
- **段階1**：**lwip を esp-idf から**。`$ESPIDF/components/lwip/lwip` をソースビルドし
  `liblwip.a` 化（`build_lwip_lib_espidf_esp32s3.sh` を riscv へ翻訳）。`ESP32Cx_LWIP` の
  `LWIP_DIR` を `./lwip` → esp-idf へ repoint。落とし穴＝INC 生成の `@IDF@` 置換漏れで
  hal_stub flat `errno.h` が外れ `__getreent` 未定義リンクエラー（S3 evidence-02 の教訓）。
  `ping_stop()` 版差は no-op を TOPPERS ガードで供給。**実機 C3 で GOT IP + ping**。
- **段階2**：**mbedtls/wpa を hal(4.0/tf-psa) → esp-idf(3.6.5)**（最難関、S3 stage3 相当）。
  config を esp-idf port の `esp_config.h` へ。**実機 WPA2 GOT IP + HTTPS 200**。
- **段階3**：**WiFi blob/PHY/coex**。`-L` を `$ESPIDF/components/{esp_wifi,esp_coex,esp_phy}/lib/<chip>`
  へ（本 repo は既に IDF_V554 参照＝ほぼ済。submodule パスへ寄せるだけ）。
- **段階4**：**BT(NimBLE + BT blob) を esp-idf へ**。★重点＝**osi_funcs_t の欠落フィールド確認**：
  S3 で v5.5.4 blob が `_malloc_retention` を要求し vendored `bt.c` 欠落→全フィールド1ポインタ
  ズレ→controller enable 復帰せず、を発見・修正済み。**C3 の bond 失敗(★A)も同型の疑い**。
  esp-idf submodule 自身の `bt/controller/<chip>/bt.c` と vendored `bt.c` を diff し欠落補完。
  **実機 C3 で bond**（★A の決着を兼ねる）。
- **段階5**：**hal 撤去の締め**。残る hal 由来ヘッダ(soc/hal/esp_rom 等)を esp-idf へ寄せ、
  hal_stub(nuttx/config.h+libc stub)を repo に vendor（S3 の `esp/config/esp32/hal_stub_include/`
  に倣う）。incflags を清掃し **.d 依存で esp-hal-3rdparty 参照ゼロを実測確認**。
- **段階6**：**./hal submodule 撤廃 + ./lwip submodule 撤廃**（lwip は esp-idf 供給に一本化）。
  `.gitmodules` 更新。撤廃後にドキュメント整備（decision メモのポインタ更新・
  開発者オンボーディング doc 作成）をまとめて実施。

## 3a. 段階0 の submodule 規約（具体・S3 repo `esp-idf-submodule/design.md` 準拠）

1. **フル公式 ESP-IDF を submodule 追加**：
   `git submodule add https://github.com/espressif/esp-idf.git esp-idf`（https）。
   ディレクトリは**版なし `esp-idf`**（版をパス名・変数名に混入させない）。軽量化
   （sparse/自前mirror）は後回し。`.gitmodules` に `shallow = true` 可。
2. **版は submodule の固定 commit に一元化**：当面 pin＝**v5.5.4**
   ＝commit `735507283d5b2f9fb363a1901172dbd9e847945d`（annotated tag `v5.5.4`）。
   更新は `cd esp-idf && git checkout <tag>` → 親で gitlink を commit するだけ（パス/変数不変）。
   v6.1 フォールバックは**同 submodule を v6.1 タグへ再 pin**（並置が要れば後日 2 本目を追加）。
3. **単一インタフェース変数 `ESPIDF`**：`ESPIDF="${ESPIDF:-$REPO/esp-idf}"`（env 上書き可・
   機外絶対既定なし。`REPO` は既存の env 上書き可変数を踏襲）。消費者は submodule 内部構造で
   なく本変数のみに依存させる。
4. **全チップ単一 `esp-idf` submodule を共有**（C3/C5/C6＋将来 S3/無印）。blob は esp-idf 自身の
   **再帰 submodule** に入るので、clone 後に**必要分だけ** `git submodule update --init`：
   - Wi-Fi/PHY/coex：`components/esp_wifi/lib`・`components/esp_phy/lib`・`components/esp_coex/lib`
   - BT controller blob：C3/C5/C6 は `components/bt/controller/lib_esp32c3_family`
     （実パスは esp-idf tree で確認）。
   - 親が pin するのは esp-idf の commit のみ（blob の pin は esp-idf ツリーが保持）。
     再帰 submodule 全23個は init しない（軽量化余地）。clone 手順は README/env に明記。
5. **パス供給の置換原則（非破壊・消費者単位・リンク駆動）**：
   - ヘッダ：`-I$ESPIDF/components/<c>/include …`
   - blob：`-L$ESPIDF/components/{esp_wifi,esp_phy,esp_coex,bt}/lib/<chip>`
     （旧 `IDF_V554=/home/honda/tools/esp-idf`・`${IDF}=…/esp-idf-v6.1` 機外パスを置換）
   - 触れた箇所の `/home/honda` 絶対リテラル（incflags・スクリプト・`.ld`・`target_timer.h` 等）を
     `$REPO`/`$ESPIDF` 相対トークンへ。
   - 既存の hal/外部IDF 既定は**当面フォールバックで温存**し、ESP-IDF 等価供給を確認できた
     消費者から旧参照を除去。
6. **toolchain**：riscv32-esp-elf **esp-14(GCC 14.2.0)** 固定（esp-15 は使わない。S3 repo の
   esp-14 固定に整合）。
7. **代替不能資産の棚卸し**：hal/外部IDF にしか無い include/資産（NuttX シム・hal_stub・
   `nuttx/config.h`・`esp_event` glue 等）を洗い出し記録（S3 の `evidence-asp3-only-inventory.txt`
   に相当）。段階5 の vendor 対象として先に列挙する。
8. **include サニティ**：最小ビルド（`wifi_scan` or `bt_smoke`）が esp-idf v5.5.4 ヘッダで解決
   してコンパイル成立することを実ブート前に静的確認（未解決シンボル一覧を証跡化）。

## 4. ガードレール（本 repo の鉄則）

- 各段は可逆：hal 経路を fallback に残したまま切替え、実機 GREEN 確認後に次段。
  C3/C5/C6 の既存ビルド/実機を回帰させない（TAP で機械判定）。
- 静的解析で結論しない（S3 教訓：nm シンボル数で「機能不可」と誤判断→実機で覆った）。
  相関を因果と早合点せず、反証実験を先に。
- submodule(asp3_core/hal/lwip)を直接編集しない。差分は本 repo 側のシム/vendor で。
  カーネル内で動的メモリ確保しない。
- 各段を docs に実施NN（背景/事前予測/結果/変更ファイル/実機ログ）で記録し、
  ビルド成立を確認してから commit・push。

## 5. 最初の一手（推奨）

段階0(submodule 追加)→段階1(lwip)を C3 で通す。これが S3/C 系共通土台の入口。
あるいは★A 優先なら段階4の osi_funcs_t 突合を先行スパイクしてよい（bond 決着が近い）。

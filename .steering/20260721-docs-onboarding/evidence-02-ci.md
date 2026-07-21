# evidence-02：GitHub Actions による CI（ビルド腐敗の検出）

日付：2026-07-21 ／ ブランチ：`main`
対象＝残課題B。計画＝`tmp/plan-github-actions-ci.md`。
実行環境は**GitHub-hosted 無料枠でまず試す**方針をユーザーに確認済み。

## 1. 計画 §4「着手前に潰すべき未確認事項」の解決

| # | 未確認事項 | 解決 |
|---|---|---|
| 4-1 | 必要な submodule の集合を実測で確定 | ✅ **evidence-01 で実測済み**（`ninja -t deps`＋リンク行）。下記 §2 |
| 4-2 | リポジトリが public か | ✅ **public**（未認証 GET で `visibility: public` を確認）＝Actions 無料無制限。push/PR トリガでも費用問題なし |
| 4-3 | `idf_tools.py install` が CI で通るか | ⚠️ **部分的に判明**——`riscv32-esp-elf` と `qemu-riscv32` は tool として存在するが、**`esptool` は tool ではない**（§3） |
| 4-4 | seam は esptool を要求する | ✅ 解決＝`install-python-env` で入れる（§3） |

## 2. submodule の最小集合（実測）

`esp-idf` 配下の再帰 submodule は **23個あるが 9個で足りる**（全構成合計）。

| 用途 | 必要な submodule |
|---|---|
| Wi-Fi＋lwIP（3チップ共通） | `components/esp_wifi/lib`・`esp_phy/lib`・`esp_coex/lib`・`lwip/lwip`・`mbedtls/mbedtls` |
| BLE（共通） | `components/bt/host/nimble/nimble`・`esp_phy/lib`・`esp_coex/lib` |
| BLE（チップ固有 controller blob） | C3=`bt/controller/lib_esp32c3_family`／C5=`bt/controller/lib_esp32c5/esp32c5-bt-lib`／C6=`bt/controller/lib_esp32c6/esp32c6-bt-lib` |

## 3. ★`esptool` は `idf_tools.py` の «tool» ではなかった（CI が初回で落ちる箇所）

計画 §4-4 は「`idf_tools.py install esptool` を追加するか」と書いていたが、**実測すると
`idf_tools.py list` に `esptool` は現れない**（出るのは `riscv32-esp-elf`・
`riscv32-esp-elf-gdb`・`qemu-riscv32`）。そのまま書いていれば **CI の初回実行で落ちていた**。

正しい入手経路は **ESP-IDF の python venv**：

- `asp3/cmake/esp_find_esptool.cmake:26-40` は
  `$ENV{IDF_TOOLS_PATH}/python_env/*/bin` を `HINTS` に `find_program` する設計。
- ローカル実機環境で seam を通している実体もこの venv のものだった（実測）：
  `ESP32C5_ESPTOOL=<IDF_TOOLS_PATH>/python_env/idf6.1_py3.12_env/bin/esptool`
- ∴ CI では **`idf_tools.py install-python-env`** を実行する（サブコマンドの存在も実測確認）。

## 4. ★workflow を書く «前» に10構成をローカルで全て通した

計画 §6 が「**いきなり push トリガで有効化しない**（壊れた CI が常時赤いのは
『テストが弱い』より悪い＝誰も見なくなる）」と釘を刺しているため、
**赤い CI を作らないよう先にローカルで全構成の成立を実測**した。

toolchain＝`asp3/cmake/toolchain-esp32-riscv32.cmake`（esp-14.2.0_20260121）、
判定＝configure・build の rc に加え **`asp.elf` の実在**まで確認。

| # | 構成 | 結果 |
|---|---|---|
| 1-3 | C3/C5/C6 素構成（Wi-Fi/BT 両OFF・`sample1`） | ✅ 全て build 0・elf 生成 |
| 4-6 | C3/C5/C6 Wi-Fi＋lwIP（`wifi_dhcp`） | ✅ 同上 |
| 7-9 | C3/C5/C6 BLE（`ble_host_smoke_c*`＋SMP） | ✅ 同上 |
| 10 | **C5 seam**（`ASP3_SEAM_BOOT=ON`） | ✅ `asp_seam.bin` 生成 |
| 別 | C3 `test_porting`（QEMU） | ✅ **`# 8/8 passed`** |

## 5. ★この過程で自分のドキュメントの誤りを発見・修正した

10構成を通す際、**`apps/ble_host_smoke` が存在しない**（正しくは `ble_host_smoke_c3`）
ことが判明した。**evidence-01 で作成した `docs/onboarding.md` に、この誤ったパスを
書いていた**（2箇所）。

原因＝**検証が不十分だった**。BLE 構成は当初 `configure` の rc しか見ておらず、
**存在しない `ASP3_APPLDIR` を渡しても configure は rc=0 で通ってしまう**
（`build` して初めて失敗する）。「記載コマンドは全て実行して確認済み」と書きながら、
BLE だけは実質未検証だった。

- 修正済み（`docs/onboarding.md` の2箇所）。**README は元から正しかった**＝
  私の転記ミスであり、README 由来の誤りではない。
- 教訓＝**configure の rc は検証にならない**。`ASP3_APPLDIR` の誤りは build まで
  進めないと出ない。以後、ビルド検証は**必ず生成物（`asp.elf`）の実在まで見る**
  （本 CI の判定にも同じ基準を入れた＝§6）。

## 6. workflow の設計（`.github/workflows/build.yml`）

- **単一ジョブで10構成をループ**（マトリクスにしない）。submodule 込みのクローンが
  重いため、並列化すると同じクローンを10回やることになる（計画 §2）。
- **判定は「ビルド成立」＋「`asp.elf` が実在」の両方**。過去に「`.o` の個数だけで
  成否判定」「オプションを黙殺してバイト同一を吐く」ビルドスクリプトの実例が
  あったため、成果物の実在まで見る（計画 §3）。
- **toolchain キャッシュのキーは esp-idf の pin**（`tools.json` が版を決めるため、
  esp-idf が変われば自動で入れ直る）。
- **toolchain の実体をログに出す**（過去に汎用GCCへ黙って落ちた事故があるため。
  `esp_toolchain_check.cmake` が FATAL で守るが、ログにも残す）。
- **QEMU の `test_porting` は best-effort**（`continue-on-error`）。Espressif fork の
  QEMU が CI で取れるかは未確認のため、**取れなければスキップし CI は落とさない**
  ——このステップの不成立で「ビルド腐敗検出」という主目的を潰さないため。
  取れた場合は **TAP で `# 8/8 passed` を機械判定**する。
- **トリガは当面 `workflow_dispatch` のみ**（計画 §6-4）。push/PR はコメントアウトして
  あり、手動実行でグリーンを数回確認してから外す。

## 7. 検証できたこと／できていないこと（正直な線引き）

**できた**：
- YAML の構文妥当性（`yaml.safe_load`）・ステップ数・トリガ
- workflow が参照する全パスの実在
- **10構成すべてがローカルで build 成立**（CI が実行する内容と同じ cmake 引数）
- `idf_tools.py` のサブコマンドと tool 一覧（`esptool` が tool でないこと）

**できていない（要ユーザー実行）**：
- **GitHub Actions 上での実行そのもの**。`gh` が未認証のため私からは起動も確認も
  できない。**初回は `workflow_dispatch` で手動実行し、グリーンを確認してから
  push/PR トリガを外すこと**。
- 特に**未確認なのは CI 環境固有の部分**＝(a) `install-python-env` の所要時間と成否、
  (b) `qemu-riscv32` が取得できるか、(c) キャッシュキーの `hashFiles` が
  `.git/modules/esp-idf/HEAD` を拾えるか（submodule のためパスが特殊）。
  (c) が外れてもキャッシュミスになるだけで CI は通る（毎回 toolchain を入れ直すので遅くなる）。

## 8. 計画 §5 の「費用対効果についての正直な所見」への同意

計画自身が「**今回の esptool 腐敗を CI が検出できたかは微妙**」（CI は常に esptool を
入れるので常に通ってしまう）と書いており、これに同意する。
**再発防止は既に構造側で達成済み**（`asp3_require_esptool()` が configure 時に FATAL）。
本 CI の価値は「**新規の**腐敗を早期に見つける」ことにある、という整理が正確。

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

---

## 9. ★初回 CI 実行の結果——**本物の潜在バグを検出**（CI の目的が果たされた）

run `29796831795`（`workflow_dispatch`）。**9構成は成功、`c5_seam` のみ失敗**。

### 9.1 検出された不整合

```
esptool.py --chip esp32c5 elf2image --flash-mode dio --flash-freq 80m --flash-size 2MB …
esptool: error: unrecognized arguments: --flash-mode --flash-freq 80m --flash-size 2MB
```

CI が解決した esptool＝`.espressif/python_env/idf5.5_py3.12_env/bin/esptool.py`。

| 環境 | esptool | 構文 |
|---|---|---|
| **IDF v5.5.4 の venv（＝本repoが pin している版）** | **v4.12.0** | `--flash_mode`（アンダースコア）・`image_info` |
| IDF v6.1 の venv（開発機にたまたま同居） | v5.3.1 | `--flash-mode`（ハイフン）・`image-info` |
| `run.cmake:42-47`（修正前） | — | **ハイフン形＝v5 専用** |

∴ **pin している v5.5.4 だけの clean 環境では seam がビルドできなかった**。
開発機で通っていたのは **IDF v6.1 の venv が偶然存在し、`esp_find_esptool.cmake` の
glob（`python_env/*/bin`）がそちらを拾っていた**ため＝典型的な "works on my machine"。

**これは CI のバグではなくリポジトリのバグ**であり、**CI を作った目的そのものを果たした**
（計画 §5 は「今回の esptool 腐敗を CI が検出できたかは微妙」と正直に書いていたが、
**別の esptool 起因の腐敗を実際に検出した**）。

### 9.2 修正と検証

`--flash_mode`／`image_info`（アンダースコア形）へ変更。**v4.12.0 と v5.3.1 の両方が
アンダースコア形を受け付ける**ことを実測確認した上での選択（ハイフン形は v5 のみ）。

ローカルで **両版を明示指定して A/B**：

| esptool | build | `asp_seam.bin` |
|---|---|---|
| v4.12.0（`-DESP32C5_ESPTOOL=…idf5.5…/esptool.py`）＝CI環境の再現 | rc=0 | 516,928 bytes |
| v5.3.1（`…idf6.1…/esptool`）＝非回帰確認 | rc=0 | 516,928 bytes（**バイト数一致**） |

### 9.3 併せて判明したこと

- **evidence-02 §7 の未確認(a)＝`install-python-env` は CI で成功**した（所要時間込みで
  ジョブ全体 2m46s）。`idf_tools.py install esptool` と書いていれば初回で落ちていた
  という §3 の判断も、結果的に正しかった。
- `gh run watch --exit-status` が **exit 0 を返したのに run の結論は `failure`** だった。
  **成功指標を鵜呑みにせず `gh run view --json conclusion` で確認すること**
  （このリポジトリの「ビルドスクリプトの成功判定を信用しない」という教訓と同型）。
- ジョブログは `gh run view --log` では空になり、`gh api …/actions/jobs/<id>/logs` で
  取得できた（fine-grained PAT の権限差と思われる）。

## 10. 修正後の再実行＝**グリーン**（run `29797193149`）

全10構成 OK（3m12s）。生成物サイズも記録：

```
c3_plain 328,728  c5_plain 327,756  c6_plain 326,804
c3_w1  3,263,072  c5_w1  3,451,028  c6_w1  3,569,736
c3_ble 1,770,764  c5_ble 2,352,308  c6_ble 2,373,088
c5_seam 2,928,284
```

### 10.1 §7 の未確認(b)＝QEMU は **取得できなかった**（真因も判明）

```
ERROR: tool qemu-riscv32 … is installed, but getting error: non-zero exit code (127)
  with message: …/qemu-system-riscv32: error while loading shared libraries:
  libSDL2-2.0.so.0: cannot open shared object file
→ qemu-riscv32 を取得できないためスキップ
```

`continue-on-error` ＋ フォールバックが**設計どおり働き CI は落ちなかった**が、
結果として **CI が「ビルドだけ」になっていた**。真因は runner に `libSDL2` が無いこと
なので、`libsdl2-2.0-0` を apt で入れれば **TAP `# 8/8 passed` の実テストまで CI で回る**。
→ 対応済み（次回実行で検証）。

### 10.2 ★自分が仕込んだ警告が誤検出だった

`show toolchain` ステップが `esptool が見つからない（seam が落ちる）` を出したが、
**seam は成功していた**＝誤検出。原因＝実体名が版で違う：

| venv | esptool の実体名 |
|---|---|
| IDF v5.5.4（CI が使う） | **`esptool.py`**（v4系） |
| IDF v6.1（開発機） | `esptool`（v5系） |

`ls …/bin/esptool` だけを見ていたため。cmake 側（`esp_find_esptool.cmake`）は
`NAMES esptool esptool.py` で両方を探すので実害は無かったが、
**「通っているのに警告が出る」CI は警告を無視する癖をつける**ので修正した
（両方を許容し、見つかった実体の `version` をログに出す）。

## 11. ★QEMU の 6/8 は «移植バグではなく QEMU 版差» と決定的に切り分けた

§10.1 で QEMU を有効化したところ、**`not ok 6 - alarm_handler`／`not ok 7 -
isr_delayed_dispatch`** で落ち、#8 に到達せずハングした（ローカルでは 8/8）。

**測定前に予測を固定**：「CI と同じ `esp_develop_9.2.2_20250817` をローカルで使えば
6/8 が再現する（＝版差）。再現しなければ版差ではなく CI 環境要因」。

### 11.1 A/B（変数は QEMU 版のみ・**同一ビルドツリー**）

| QEMU 版 | 結果 |
|---|---|
| `esp_develop_9.2.2_**20250817**`（CI が入れる版＝IDF v5.5.4 の tools.json 指定） | **`not ok 6`／`not ok 7`**・以降ハング（rc=124） |
| `esp_develop_9.2.2_**20260417**`（開発機の版） | **`# 8/8 passed`**（rc=0） |

**同じ `asp.elf` を、cmake の `-DQEMU_SYSTEM_RISCV32_ESP` だけ差し替えて実行**した
（ビルドし直していない＝バイナリ差の混入なし）。**予測は的中**し、
**移植側ではなく QEMU の割込みエミュレーションの版差**と確定した。
落ちた2項目が #6 タイマ割込み経路・#7 割込み出口ディスパッチという
「割込み配送」に集中していることとも整合する（CLAUDE.md の既知の罠と同じ領域）。

> ⚠️ これは「新しい QEMU が正しい」ことの証明ではなく、**「この ELF は 20260417 で 8/8、
> 20250817 で 6/8」という事実**。ただし実機では割込み系も動作実証済み（README 到達点）
> のため、移植側の問題である可能性は低いと判断する。

### 11.2 制約：`idf_tools.py` では新しい版を入れられない

`idf_tools.py list` の `qemu-riscv32` は **`esp_develop_9.2.2_20250817 (recommended)` のみ**
＝pin している esp-idf v5.5.4 の `tools.json` がその版しか知らない。
`<tool>@<version>` 構文はあるが、tools.json に無い版は指定できない。
⇒ **GitHub リリースアセットから直接取得して版を固定**する
（`esp-develop-9.2.2-20260417` の x86_64 アセット実在を HTTP 200 で確認済み）。

### 11.3 併せて直した2件

1. **`continue-on-error` の握り潰しを解消**。step 全体に付けていたため
   **テストが 6/8 で落ちているのに run の結論が `success`** になっていた
   ＝**「壊れた検証も成功と同じ顔をする」状態**。
   「QEMU を取得できない＝スキップ（緑）」と「走って落ちた＝赤」に分離した。
2. **esptool 検出の誤検出**（§10.2 の修正が不十分だった）。
   `ls A B` は **片方が欠けるだけで非ゼロ**を返すが、実体は版により
   `esptool`(v5系) か `esptool.py`(v4系) の**どちらか一方しか無い**ため必ず非ゼロになる。
   `find` で列挙して「1件以上あるか」で判定する方式へ変更し、ローカルで誤検出しないことを確認。

## 12. キャッシュヒット経路の検証（§7 の未確認をすべて解消）＋ push/PR トリガ有効化

**測定前に予測を固定**：「キャッシュヒットし install 系はスキップされる。それでも
キャッシュに `python_env` が含まれるため esptool は検出でき、seam も 8/8 も通る。
外れる（esptool 未検出／seam 失敗）なら、キャッシュに `python_env` が入っていない」。

**結果＝予測どおり**（run `29798199437`）：

| 確認項目 | 結果 |
|---|---|
| キャッシュ | **ヒット**（`Cache restored from key: espressif-tools-`・824MB） |
| `install toolchain`／`install-python-env` | **スキップ**（設計どおり） |
| esptool 検出 | キャッシュ内の `esptool.py v4.12.0` を検出（**誤検出警告 0件**） |
| `c5_seam` | OK（2,928,284 bytes）＝**復元した esptool で seam が通る** |
| 全10構成 | 全て OK |
| QEMU 実テスト | **`# 8/8 passed`** |

⇒ **§7 に残していた未確認3点をすべて解消**：
(a) `install-python-env` の成否＝成功（§9.3）／(b) `qemu-riscv32` の取得＝
`idf_tools` 経由は不可だがリリースアセット直取得で解決（§11.2）／
(c) キャッシュキーの `hashFiles`＝**キー算出・保存・復元とも成功**。

### 12.1 push/PR トリガを有効化した

計画 §6-4「いきなり push トリガで有効化しない」に従い、**手動実行5回で確認してから**
有効化した（実績は workflow 冒頭コメントに記録）。特筆すべきは、
**この5回のうち2回は «赤にすべき問題» を実際に捕まえている**こと：

- 1回目＝`c5_seam` の潜在バグ（pin した IDF では seam がビルドできない）を**検出して赤**
- 3回目＝**緑だったが実はテストが 6/8 で落ちていた**（`continue-on-error` の握り潰し）
  ＝**「壊れた検証も成功と同じ顔をする」を CI 自身で踏んだ**。これを直したことで、
  以後は「走って落ちたら赤」になる。

∴ 「常時赤い CI」ではなく「**壊れたときに赤くなる CI**」であることを実証した上での有効化。

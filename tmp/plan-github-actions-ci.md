# 計画：GitHub Actions による CI（ビルド腐敗の検出）

2026-07-21。**未着手・計画のみ**。動機＝残課題1「seam 構成のビルドを定期的に
通す仕組みが無い＝また黙って腐る」（実際に esptool の PATH 依存で腐っていた）。

## 0. 結論（先に）

**実現可能。** ただし下記 §5 のとおり **「今回の腐敗を CI が検出できたか」は微妙**で、
費用対効果は「seam を通常のビルドマトリクスに入れること」の方が確実。
CI はそれを**自動で回す手段**として位置づけるのが正しい。

## 1. 実現可能性（実測で確認済みの前提）

| 論点 | 実測 | CI での扱い |
|---|---|---|
| toolchain が絶対パス固定 | `asp3/cmake/toolchain-esp32-riscv32.cmake:65-72` が **`IDF_TOOLS_PATH` を尊重**する設計。`-DESP_TOOLCHAIN_ROOT` / `-DESP_TOOLCHAIN_PREFIX` でも上書き可 | esp-idf submodule 同梱の `tools/idf_tools.py install` で導入。**指定版と一致することが構造的に保証される**（版ズレ事故の再発防止にもなる） |
| esp-idf の巨大さ | `.git/modules/esp-idf` ＝ **3.0GB**（全履歴） | `--depth 1` の shallow clone |
| blob 再帰 submodule | **必要分の作業ツリー合計 118MB**（全23個は不要） | 選択的に `submodule update --init --depth 1` |
| ビルド依存 | `cfg.py` は**標準ライブラリのみ**（`gen_file`/`srecord` はリポジトリ内）＝**pip 不要**。`cmake_minimum_required(VERSION 3.16)` | ubuntu-latest で充足 |
| 既存ひな型 | asp3_core に `.github/workflows/{ci,container,nightly}.yml` | 構造を踏襲（`paths-ignore`・`concurrency`・`workflow_dispatch`） |

## 2. 構成（★マトリクスにしない）

10構成をマトリクス並列にすると **531MB のクローンが10回**走って割に合わない。
**1回クローンして10構成をループ**する単一ジョブを推奨。

```
1. checkout（submodules: false ＝ 手動制御）
2. asp3/asp3_core を init（小さい）
3. esp-idf を --depth 1 で init
4. 必要な再帰 submodule のみ --depth 1 で init（集合は §4-1 で要確定）
5. toolchain を actions/cache（キー＝esp-idf の pin commit）
   キャッシュミス時のみ `idf_tools.py install riscv32-esp-elf`
6. 10構成をループでビルド：
     C3/C5/C6 × (plain / WiFi+lwIP / BLE)          = 9
     + C5 seam（-DASP3_SEAM_BOOT=ON）  ★本CIの主目的
```

トリガは asp3_core に倣う：`push`(main) / `pull_request` / `workflow_dispatch`、
`paths-ignore: ['**.md','docs/**','LICENSE']`、`concurrency` でキャンセル。

**判定はビルド成立のみ**（実機は当然不可）。今回腐ったのはビルド段階なので目的は満たす。

## 3. 判定を «成立» で終わらせない工夫（このリポジトリの事故履歴を踏まえて）

過去に「`.o` の個数だけで成否判定」「`EXTRA_OFLAGS` を黙殺してバイト同一を吐く」
ビルドスクリプトの実例があった。CI でも同型を避ける：

- **toolchain の実体を表示・検証**（`esp_toolchain_check.cmake` が既に FATAL で守るが、
  ログに解決結果を出す）
- **成果物の存在とサイズ**を確認（`asp.elf`・`asp_flash.bin`）
- ★**C3 は `csrw mie` が 0 であること**を検査（`-DESP32C3_QEMU=OFF` の実機安全性。
  既存の `tmp/c3ble.sh` が既に持っている検査＝CI へ移植する価値がある）

## 4. 着手前に潰すべき未確認事項（★推測で書くと CI で初めて壊れる）

### 4-1. 必要な submodule の集合を **実測で確定**する
本計画作成時に `build.ninja` からの抽出を試みて**空振り**した（抽出方法が不適切）。
`ninja -t deps` かリンク行（`link.txt` / `-L` 引数）から取り直すこと。
候補（未確定）：`components/{esp_wifi,esp_phy,esp_coex}/lib`・`components/mbedtls/mbedtls`・
`components/lwip/lwip`・`components/bt/host/nimble/nimble`・
`components/bt/controller/lib_esp32c3_family`・`lib_esp32c5/esp32c5-bt-lib`・
`lib_esp32c6/esp32c6-bt-lib`。

### 4-2. リポジトリが public か
`gh` 未認証で未確認。**public なら Actions 無料無制限／private なら分単位課金**で
実行頻度の設計が変わる（private なら push 毎をやめて nightly + dispatch に寄せる）。

### 4-3. `idf_tools.py install` が CI で通るか
Python 依存・ダウンロード可否・所要時間。キャッシュが効けば2回目以降は無視できる。

### 4-4. seam は esptool を要求する
POST_BUILD で `esptool elf2image` を呼ぶ（`esp/../run.cmake`）。CI では
`idf_tools.py install esptool` を追加するか、seam だけ configure 止まりにするか要判断。

## 5. ★費用対効果についての正直な所見

**主目的に対して有効だが、「今回の腐敗を CI が検出できたか」は微妙。**

今回の esptool 腐敗は「**開発者のローカル環境に esptool が PATH にあるか**」に依存していた。
CI は常に esptool を導入するので **常に通ってしまう**可能性が高い。
＝**CI があっても今回の件は見逃した公算が大きい。**

より確実なのは：
1. **seam を9構成と同じ扱いで通常のビルドマトリクスに入れる**（人手でも CI でも同じ手順で回る）
2. 今回の修正で `asp3_require_esptool()` が **configure 時に FATAL** するようにしたので、
   esptool が無い環境では**明示的に落ちる**（黙って腐らない）＝**構造側の対策は既に入っている**

∴ CI の価値は「新規の腐敗を早期に見つける」ことであって、
**今回の件の再発防止は既に構造で達成済み**という整理が正確。

## 6. 進め方

1. §4-1（必要 submodule の実測確定）— これが無いと workflow が書けない
2. §4-2（public/private の確認）— 実行頻度の設計に効く
3. workflow 作成 → `workflow_dispatch` で手動実行して通す
4. 通ってから push/PR トリガを有効化

★**いきなり push トリガで有効化しない**（壊れた CI が常時赤いのは、
「テストが弱い」より悪い＝誰も見なくなる）。

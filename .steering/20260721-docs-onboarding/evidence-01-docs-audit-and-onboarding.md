# evidence-01：ドキュメント棚卸し＋開発者オンボーディング doc

日付：2026-07-21 ／ ブランチ：`main`
対象＝残課題A（ドキュメント整備＋オンボーディング doc）。判断が要る分岐は事前に確認し、
**対象読者＝外部公開品質**という回答を得てから着手した。

## 1. 背景

esp-idf 単一供給への全面移行・`hal`/`lwip` 撤廃・`asp3/`↔`esp/` レイアウト再編が完了した
直後で、**移行前を前提にした記述が各所に残っている**状態だった。CI が無いため
「腐っていても気づけない」構造でもある（残課題B の動機）。

## 2. 事前予測（着手前に固定）

| # | 予測 | 結果 |
|---|---|---|
| P1 | 現ツリーは健全（移行直後で腐っていない）＝主要構成がビルドできる | ✅ 的中（§3） |
| P2 | 「6/6 passed」の残存は**大半が歴史的記録**で、機械的置換は履歴の捏造になる | ✅ 的中（§4） |
| P3 | README のリンク切れ等、記述と実体の不一致が他にもある | ✅ 的中（§5） |

## 3. ベースライン確認（触る前にビルドが通ることを実測）

`IDF_TOOLS_PATH=$HOME/tools/espressif`、toolchain＝`asp3/cmake/toolchain-esp32-riscv32.cmake`
（esp-14.2.0_20260121）。

| 構成 | configure | build | 生成物 |
|---|---|---|---|
| C3 Wi-Fi+lwIP（`wifi_dhcp`） | 0 | 0 | ✓ |
| C5 BLE（`ble_host_smoke_c5`） | 0 | 0 | ✓ |
| C6 Wi-Fi+lwIP（`wifi_dhcp`） | 0 | 0 | ✓ |
| **C5 seam**（`ASP3_SEAM_BOOT=ON`） | 0 | 0 | `asp_seam.bin` ✓ |
| C3 `test_porting`（QEMU） | 0 | 0 | ✓ |

実エラー 0 件。**seam 構成も含めて腐っていない**ことを確認（過去に esptool の
PATH 依存で黙って壊れた前歴があるため、明示的に確認した）。

QEMU 実行（TAP 機械判定）：

```
1..8
ok 1 - syslog_output      ok 5 - eventflag_set_wait
ok 2 - tick_timer_basic   ok 6 - alarm_handler
ok 3 - task_create_activate  ok 7 - isr_delayed_dispatch
ok 4 - semaphore_signal_wait ok 8 - wake_from_idle
# 8/8 passed
```

## 4. ★「6/6 passed」の扱い＝**書き換えない**と判断した

`docs/` に 30箇所以上あるが、**分類したところほぼ全てが「当時の実測ログ」**
（実施NN の記録・回帰確認の記録）だった。`test_porting` は asp3_core 側で
6項目→8項目に増えたが、**当時のランは実際に 6/6 だった**のであり、
これを 8/8 に置換すると**履歴の捏造**になる。

したがって：

- **規範的な記述（今の手順として読まれるもの）のみ修正**
  → `CLAUDE.md` の検証の鉄則（`# 6/6 passed` → `# 8/8 passed` ＋経緯注記）。
- **歴史的記録は数値をそのまま保存**し、必要なら**冒頭に状態バナー**を付けて
  「今の正本ではない」ことを示す（本文は書き換えない）。
- README には既に注記があり（「docs内に残る『6/6 passed』は6項目時代の記録」）、
  `docs/c5-bringup.md:10623` にも 6→8 の遷移が記録済み。追加の一括置換は行わない。

## 5. 実施した修正

| 対象 | 内容 |
|---|---|
| `CLAUDE.md` | 全面更新。禁則②を「撤廃済み `hal/`」→**`esp-idf/` submodule**へ。構成を `asp3/`↔`esp/` 分離に。toolchain 固定（esp-14／esp-15 禁止）を明記。TAP を `# 8/8 passed` に。**リポジトリ外を指していた dangling ref**（`memory/…`）を廃し、実機調査の規律を**本文に内包**して自己完結化 |
| `docs/hal-integration.md` | 冒頭に**歴史文書バナー**（前提の `hal/` submodule は撤廃済み・パスは現行に当てはまらない・数値は当時の記録として保存） |
| `docs/hal-vs-espidf-decision.md` | 冒頭に**実行完了バナー**（判断は実施済み＝これから判断する文書ではない） |
| `tmp/plan-espidf-only-hal-removal.md` | 同上（実行完了） |
| `docs/ble-c3-smp-death-plan.md` ほか | **README のリンク切れを解消**（§6） |
| `README.md` | ビルド節冒頭に onboarding への導線 |
| `docs/onboarding.md` | **新規**（§7） |

## 6. README のリンク切れ（発見と対処）

README が参照する `docs/ble-c3-smp-death-plan.md` が **main に存在しなかった**。
調べると、この調査（C3×スマホ BLE の `DISC=0`）の**コード修正は既に main へ移植済み**
（`2612f11` の `ESP32C3_BT_CONN_WD`）だが、**doc と証跡だけがブランチ
`claude/c3-smp-death-plan` に取り残されて**いた。

対処＝**doc・証跡・調査ツールのみ** main へ取得（**コードは取らない**＝既に移植済みで
二重適用になるため）。取得後に `git status` で**コード変更の混入 0 件**を確認：

- `docs/ble-c3-smp-death-plan.md`
- `.steering/…/evidence-rc-c3-P1-wedge-mbuf-exhaustion.md`
- `.steering/…/snoop/`（btsnoop 一次証拠＋再解析器）
- `.steering/…/p1-tools/`（post-mortem ダンパ等）

## 7. `docs/onboarding.md`（新規・外部公開品質）

README と重複させず、**「手を動かす順序」と「最初に踏む罠」**に絞った。
**記載コマンドは全て実際に実行して確認済み**。

特筆：

- **submodule の最小集合を実測して記載**した。「使う分だけ init」と書かれていても
  集合が示されていなければ役に立たないため、`ninja -t deps` とリンク行から採取：
  - W1（3チップ共通）＝`esp_wifi/lib`・`esp_phy/lib`・`esp_coex/lib`・`lwip/lwip`・`mbedtls/mbedtls`
  - W2＝`bt/host/nimble/nimble`・`esp_phy/lib`・`esp_coex/lib` ＋ チップ固有の
    コントローラ blob（C3=`lib_esp32c3_family` / C5=`lib_esp32c5/esp32c5-bt-lib` /
    C6=`lib_esp32c6/esp32c6-bt-lib`）
  - ＝**23個中5〜6個**で足りる。
- **`ASP3_EXTRA_APP_C_FILES` で `tap.c` を足す必要がある**ことを記載。
  検証中に実際に踏んだ（忘れると `undefined reference to 'tap_plan'`）。
  **手順を実行して確かめたからこそ書けた項目**で、書かなければ新規開発者は同じ所で詰まる。
- 外部公開品質の機械チェック：**内部絶対パス 0・ボードMAC 0・台帳参照 0・
  認証情報 0・エージェント内部参照 0**。

## 8. 残り（本ラウンド外）

- **既存 docs には内部絶対パス・ボードMACが多数残る**。新規 doc は外部公開品質にしたが、
  **リポジトリ全体を外部公開するならスクラブが別途必要**（本ラウンドでは新規分のみ担保）。
- 残課題B（CI）＝`tmp/plan-github-actions-ci.md`。実行環境は
  **GitHub-hosted 無料枠でまず試す**方針をユーザーに確認済み。
- 残課題C（bondストアのNVS化）・D（Android/RPA・`DISC=0`）は実機・スマホが要る。
  D の正本は本ラウンドで main へ取得した `docs/ble-c3-smp-death-plan.md`。

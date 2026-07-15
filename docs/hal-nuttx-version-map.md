# esp-hal-3rdparty 版管理マップ（NuttX pin ↔ hal SHA ↔ IDF version）

## 目的

esp-hal-3rdparty（本リポジトリの `hal` submodule）はリリースタグを持たず、
「どの版か」が曖昧という課題への回答。上流の主要消費者である **NuttX が hal を
どう版管理しているか**を実測し、`NuttX release ↔ hal commit ↔ ESP-IDF version` の
対応表を作成した。これにより「IDF リリースに対応する hal リビジョンを特定して
submodule pin する」逆引きが可能かを判定する。

調査は全て read-only の Web 取得（NuttX の `Make.defs` と esp-hal-3rdparty の
`esp_idf_version.h`）。取得日 2026-07-15。

## NuttX の版管理機構（実測で確定）

NuttX は hal を **タグでもブランチでもなく生コミット SHA で pin** する。
`arch/risc-v/src/common/espressif/Make.defs`（xtensa 側にも同等物）：

```
ESP_HAL_3RDPARTY_URL     = https://github.com/espressif/esp-hal-3rdparty.git
ESP_HAL_3RDPARTY_VERSION = <40-hex commit SHA>
```

ビルド時に clone → `git -C chip/$(ESP_HAL_3RDPARTY_REPO) checkout --quiet
$(ESP_HAL_3RDPARTY_VERSION)`。∴**版管理台帳は NuttX 自身の git 履歴**であり、
esp-hal-3rdparty 側のタグ（`nuttx-X.Y`、2023年で停止＝レガシー）は現行 NuttX は
使っていない。hal の IDF 対応は各スナップショットの
`components/esp_common/include/esp_idf_version.h` からのみ復元できる。

補足（設計思想）：NuttX は mbedtls submodule を直接編集せず `nuttx/patches/` の
patch（シンボル prefix 付与でホスト mbedtls との衝突回避）で当てる。本リポジトリの
禁則②「hal を直接編集しない・差分はシムで」と同じ。

## 対応表（NuttX release → hal SHA → IDF stamp）

| NuttX release | pin する hal SHA | IDF stamp | 備考 |
|---|---|---|---|
| ≤ 12.4 | （`common/espressif` 統一前・チップ毎 pin） | ~5.1 | 統一パス未導入（12.4 は 404） |
| 12.5 | `09219a01…e381` | **5.1.0** | |
| 12.6 | `9bc2c738…1fce` | **5.1.0** | |
| 12.7 | `20690e67…e94d` | **5.1.4** | |
| 12.8 | `e3899a23…5b92` | **5.1.4** | |
| 12.9 | `e23f3131…ce65` | **5.1.4** | |
| 12.10 | `0b15e1a6…8f5f` | **5.1.4** | 5.x 系の最後 |
| 12.11 | `8d7f4177…aec0` | **6.0.0** | ← ここで一気に 6.0 へ跳躍 |
| 12.12 | `bb255ca4…b669` | **6.0.0** | `= release/master.a` |
| 12.13 | `bb255ca4…b669` | **6.0.0** | 12.12 と同一 |
| **master** | **`b90b1837…6ce5`** | **6.1.0** | `= release/master.c` ＝**本リポジトリの hal pin** |

（SHA は先頭8桁＋末尾4桁を表示。完全形は本文中／Make.defs 参照。）

## 結論

1. **NuttX 安定版は全て IDF 5.1.x に張り付いている**（12.5 の 5.1.0 → 12.10 の
   5.1.4）。**12.11 で 5.1.4 から 6.0.0 へ直接跳躍**し、5.2/5.3/5.4/**5.5** を
   完全にスキップしている。
2. ∴**IDF 5.5.x に対応する hal スナップショットは、上流 NuttX の pin 系列にも
   esp-hal-3rdparty のブランチ系列（前調査：`release/v5.1` の次が `release/master.*`
   ＝6.x）にも存在しない**。「hal を v5.5.4 相当に pin する」は物理的に不可能と確定。
3. **本リポジトリの hal `b90b183` は NuttX master 現行 pin と完全一致**（＝
   `release/master.c` ＝ IDF 6.1.0）。つまり asp3_esp_idf は既に「NuttX 方式
   （SHA pin）」で運用しており、たまたま NuttX 最先端（未リリースの 6.1.0）を
   引き継いでいる状態。

## 版管理戦略への含意

- **タグ由来のクリーンな来歴で hal を pin したい場合**、現実的なアンカーは：
  - **NuttX 12.10**（hal `0b15e1a6` / IDF **5.1.4**）＝安定版で最も新しい 5.x。
  - **NuttX 12.12 or 12.13**（hal `bb255ca4` / IDF **6.0.0**）＝安定版で最新の 6.x。
  - **NuttX master**（hal `b90b183` / IDF 6.1.0）＝現状。ただし master は動くので
    参照するなら SHA を固定＋この表で「NuttX master @2026-07-15」と明記する。
- **v5.5.4 blob との整合が目的なら hal 側の逆引きは打ち切り**：上流にも 5.5 の
  hal が無い以上、hal を弄っても目標に届かない。**esp-idf の `v5.5.4` タグを
  submodule 化**するのが唯一クリーンな道（タグ有り・blob と mbedtls 3.6 まで一致）。
- **現状維持で来歴の曖昧さだけ消す最小策**：submodule は既に SHA pin 済み＝再現性は
  ある。`.gitmodules`／README に「hal `b90b183` = esp-hal-3rdparty `release/master.c`
  = NuttX master pin（2026-07-15 時点）= ESP-IDF 6.1.0 相当・mbedtls 4.0」と
  記録すれば、人間可読な札が付き実害は消える。

## 再現手順（この表の更新方法）

```
# 1. NuttX の pin を読む（release タグを releases/12.N に変える）
curl -s https://raw.githubusercontent.com/apache/nuttx/releases/12.N/\
arch/risc-v/src/common/espressif/Make.defs | grep ESP_HAL_3RDPARTY_VERSION

# 2. その hal SHA の IDF stamp を読む
curl -s https://raw.githubusercontent.com/espressif/esp-hal-3rdparty/<SHA>/\
components/esp_common/include/esp_idf_version.h | grep ESP_IDF_VERSION_
```

# evidence-c3-13 — 段階5：hal参照ゼロを9構成で実測確認＋lwip既定をONへ反転

2026-07-20。ブランチ `claude/espidf-only-hal-removal`。evidence-c3-12 の続き。

## 1. 段階5の核心確認：3チップ×3構成＝9通り全てで `./hal` submodule 参照ゼロ

`ninja -t deps` で `./hal`（esp-hal-3rdparty submodule）への実参照数を計測
（esp-idf自身の同名コンポーネント`components/hal/`・repo資産`hal_stub/`を
誤カウントしないよう `asp3_esp_idf/hal/` パスで厳密フィルタ。grepでなく
実ビルドの依存グラフで検証——静的解析だけで結論しない、の規律に従う）。

| チップ | 素カーネル | WiFi+lwIP(ASP3_LWIP_ESPIDF=ON) | BLE(NimBLE+controller) |
|---|---|---|---|
| C3 | 0 | 0（evidence-c3-12で確認済み） | 0（evidence-c3-12で確認済み） |
| C5 | 0 | 0 | 0 |
| C6 | 0 | 0 | 0 |

⇒ **既定設定（`ASP3_ESPIDF_SUPPLY=ON`・`ASP3_BT_IDF_V554=ON`・
`ASP3_LWIP_ESPIDF=ON`）では3チップ・全アプリ種別で `./hal` submoduleに
一切依存しない**ことを実測確認。段階5の目標（「.d依存でesp-hal-3rdparty
参照ゼロを実測確認」）は達成。

なお元の計画にあった「incflagsを清掃」「hal_stubをrepoにvendor」は
本repoには該当しない：incflagsファイル自体が存在せず（S3 repo固有の
仕組み）、hal_stub（`asp3/target/esp32c3_espidf/hal_stub/include/`）は
そもそも本repo自身が出自（S3側がここから借用した経緯がevidence-c3-11の
先行調査で判明済み）。

## 2. `ASP3_LWIP_ESPIDF` の既定を OFF→ON へ反転

evidence-c3-11/12で3チップとも実機GREEN（GOT IP + gateway ping継続成功）
を確認済みのため、他の主要供給トグル（`ASP3_ESPIDF_SUPPLY`・
`ASP3_WIFI_BLOB_HAL`・`ASP3_BT_IDF_V554`）と同じく既定ONへ反転した：

- `asp3/target/esp32c3_espidf/target.cmake`
- `asp3/target/esp32c5_espidf/target.cmake`
- `asp3/target/esp32c6_espidf/target.cmake`

可逆性は維持（`-DASP3_LWIP_ESPIDF=OFF`で`./lwip`submoduleへ完全復帰）。

### 非回帰確認

既定反転後、明示フラグ無しで3チップとも：
- 素カーネル：ビルド成立（サイズ不変）。
- `wifi_dhcp`：ビルド・リンク成立。`ninja -t deps`で
  `esp-idf/components/lwip`への参照1050件・旧`./lwip`submoduleへの参照0件
  を確認＝既定が正しくesp-idf供給に切り替わったことを実測で確認。

## 3. 現在の状態（3チップ共通）

| オプション | 既定 | fallback |
|---|---|---|
| `ASP3_ESPIDF_SUPPLY` | ON | OFF＝hal |
| `ASP3_WIFI_BLOB_HAL` | OFF（＝esp-idfが既定） | ON＝hal |
| `ASP3_BT_IDF_V554` | ON（C3はASP3_ESPIDF_SUPPLY追従） | OFF＝hal(C3)／v6.1(C5/C6) |
| `ASP3_LWIP_ESPIDF` | **ON（本ラウンドで反転）** | OFF＝./lwip submodule |

★全ての主要供給トグルが esp-idf 既定・hal/./lwip はreversible fallbackの
位置づけに揃った。

## 4. 段階6（submodule撤廃）の判断材料

`./hal`・`./lwip` submoduleは now **既定経路では一切参照されない**。
撤廃の是非は「fallbackオプション（`ASP3_ESPIDF_SUPPLY=OFF`等）を今後も
維持する必要があるか」というポリシー判断に帰着する：

- **残す場合**：A/B比較・不具合切り分けの手段として`.hal`/`./lwip`を
  保持し続ける（現状維持）。段階6は見送り。
- **撤廃する場合**：`ASP3_ESPIDF_SUPPLY=OFF`等のfallbackオプション自体を
  削除するコード変更が先に必要（現状はoption自体が生きているため、
  submoduleだけ消すとfallback選択時にビルドが壊れる）。

いずれもユーザー判断が必要（本ラウンドでは未決定・見送り）。

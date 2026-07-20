# evidence-c3-14 — 段階6：`./hal`・`./lwip` submodule 撤廃

2026-07-20。ブランチ `claude/espidf-only-hal-removal`。evidence-c3-13 の続き。
計画（`tmp/plan-espidf-only-hal-removal.md` 段階6）の最終段。

## 1. 前提（evidence-c3-13で確立）

C3/C5/C6 × (素カーネル/WiFi+lwIP/BLE) の9構成すべてで、既定設定
（`ASP3_ESPIDF_SUPPLY=ON`・`ASP3_BT_IDF_V554`既定・`ASP3_LWIP_ESPIDF=ON`）
下では `./hal` submoduleへの参照がゼロであることを実測確認済み。

## 2. fallbackガードの追加（撤廃「前」に実施）

`./hal`・`./lwip` submoduleを削除すると、以下の**明示的なfallback
オプション**（reversible指定のもの）が壊れたパス参照になる：

| オプション | 参照先 | チップ |
|---|---|---|
| `ASP3_ESPIDF_SUPPLY=OFF` | `./hal`(ESP_HAL_DIR) | C3/C5/C6共通 |
| `ASP3_WIFI_BLOB_HAL=ON` | `./hal`(ESP_HAL_DIR) | C3/C5/C6共通 |
| `ASP3_BT_IDF_V554=OFF` | `./hal`(ESP_HAL_DIR) | **C3のみ**（C5/C6のBT fallbackは外部v6.1ツリーで`./hal`と無関係） |
| `ASP3_LWIP_ESPIDF=OFF` | `./lwip` | C3/C5/C6共通 |

これらを黙って壊す（分かりにくいfile-not-foundエラーにする）のではなく、
`asp3/cmake/esp_supply_default_check.cmake` に共有関数
`asp3_require_removed_submodule(<dir> <option-name> <desc>)` を追加し、
各fallback分岐の入口で呼ぶことで、**明示的なFATAL_ERRORと復元手順**に
変えた（可逆性を「コードで即座に」ではなく「gitで明示的に」保つ形へ変更）。

## 3. 撤廃手順・検証

```
git submodule deinit -f hal lwip
git rm hal lwip
```

`.gitmodules`から該当2エントリも自動的に消去された（残るは
`asp3/asp3_core`・`esp-idf`の2つ）。

### 非回帰確認（実ビルド、撤廃前後で比較）

| 構成 | 撤廃前サイズ | 撤廃後サイズ |
|---|---|---|
| C3 wifi_dhcp | IROM 423664/DROM 501168/RAM 311380 | 同一 |
| C5 wifi_dhcp | FLASH 527728/RAM 319672 | 同一 |
| C6 wifi_dhcp | FLASH 581824/RAM 316904 | 同一 |
| C3 ble_host_smoke | IROM 254208/DROM 284608/RAM 306508 | 同一 |
| C5 ble_host_smoke_c5 | FLASH 418368/RAM 305872 | 同一 |
| C6 ble_host_smoke_c6 | FLASH 402240/RAM 305172 | 同一 |

⇒ **撤廃後も既定ビルド6種すべて非回帰（サイズ完全一致）**。

### fallbackガードの実地確認（撤廃「後」に実施）

`-DASP3_ESPIDF_SUPPLY=OFF`（C3/C5/C6）・`-DASP3_WIFI_BLOB_HAL=ON`（C3）・
`-DASP3_LWIP_ESPIDF=OFF`（C3）を明示指定してconfigureし、いずれも
「このオプションはstage6でsubmodule撤廃済みにつき使えない。復元手順」
という明示FATAL_ERRORで**意図どおりに落ちる**ことを確認（cryptic file-not-
foundエラーにならない）。

## 4. README.md の更新（範囲限定）

ディレクトリツリー（`hal/`・`lwip/`行）を削除。★**なお `README.md` の
「供給元」節（47-90行目）には、C3のBT既定がesp-idf供給で bond 失敗する
という**現在のコードと矛盾する古い記述**が残っている（実際は
`target.cmake:159` で `_asp3_espidf_supply_default=ON`固定＝BTも含め
全構成で既定esp-idf。toolchain修正[memory:
toolchain-idf-standard-alignment]でbondは成功するようになったため）。
**本ラウンドではスコープ外として触れていない**——次の一手として別途
ドキュメント整備が必要。

## 5. 結論

計画の段階0〜6が全て完了：
- 段階0：esp-idf submodule追加（v5.5.4 pin）
- 段階1：lwip→esp-idf供給（3チップ実機GREEN）
- 段階2：mbedtls/wpa→esp-idf供給（実測でC3 hal参照0を確認）
- 段階3：WiFi/PHY/coex blob→esp-idf供給
- 段階4：BT→esp-idf供給
- 段階5：hal参照ゼロを9構成で実測確認
- 段階6：`./hal`・`./lwip` submodule撤廃（fallbackオプションは
  明示的ガード付きで温存＝コードは削除せずgit revertで復元可能）

★fallbackオプション自体（`ASP3_ESPIDF_SUPPLY`等）のコードは削除して
いない。「使うと明示的に落ちる」状態で温存しており、真に不要と判断
すればコード自体の削除は別タスクとして扱う。

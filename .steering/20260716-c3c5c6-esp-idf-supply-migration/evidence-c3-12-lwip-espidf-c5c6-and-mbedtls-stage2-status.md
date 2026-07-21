# evidence-c3-12 — lwIP esp-idf供給をC5/C6へ横展開（実機GREEN）＋段階2状態の訂正

2026-07-20。ブランチ `claude/espidf-only-hal-removal`。evidence-c3-11 の続き。

## 1. 段階2（mbedtls/wpa）の訂正 — ★誤報告のリカバリ

前段で「`MBEDTLS_PORT_DIR` が常に `ESP_HAL_DIR` 固定＝段階2は部分的」と報告したが、
これは grep 結果の if/else 分岐を読み違えた**誤り**。実際には：

- `asp3/target/esp32c3_espidf/esp_wifi.cmake:481` の
  `set(MBEDTLS_PORT_DIR ${ESP_SUP_DIR}/components/mbedtls/port)` は
  `if(ASP3_ESPIDF_SUPPLY)` ブロック内（401-494行）にあり、`ESP_HAL_DIR` 直書きの
  `:560` は **別の `else() # ASP3_ESPIDF_SUPPLY`** ブロック（495-575行、hal fallback用）。
  両方とも設計どおりの可逆分岐であって欠陥ではない。

### 実測による確定（`ninja -t deps`、grepでなく実ビルドで検証）

C3 で以下3構成を実際に configure+build し、`./hal` submodule（`esp-hal-3rdparty`）への
依存を `ninja -t deps` で数えた（`hal_stub`・esp-idf自身の `components/hal/` という
同名コンポーネントを誤カウントしないよう `asp3_esp_idf/hal/` パスで厳密フィルタ）：

| 構成 | app | オプション | `./hal` submodule参照数 |
|---|---|---|---|
| 素カーネル | (無し) | `ASP3_ESPIDF_SUPPLY=ON`（既定） | **0** |
| WiFi+lwIP+mbedtls+wpa | wifi_dhcp | 既定 | **0** |
| BLE(NimBLE+controller) | ble_host_smoke | `ASP3_BT_IDF_V554=ON`（既定） | **0** |

⇒ **C3 は既定設定で mbedtls/wpa を含め全面的に hal-free**。段階2はC3について
**完了**。C5/C6 の同種オプションのコメント（`target.cmake` 内、`ASP3_ESPIDF_SUPPLY`
の説明文）にも既に「hal 7357/0」「hal 7181/0」のような実測値が記録されており、
本確認と整合する。

## 2. lwIP esp-idf供給をC5/C6へ横展開

evidence-c3-11 で C3 に導入した `ASP3_LWIP_ESPIDF`（既定OFF・可逆）を、同一パターンで
C5・C6 の `target.cmake` へ移植：

- `asp3/target/esp32c5_espidf/target.cmake`
- `asp3/target/esp32c6_espidf/target.cmake`

いずれも ON で `LWIP_DIR = ${IDF_V554}/components/lwip/lwip` ＋
`TOPPERS_LWIP_ESPIDF_SUPPLY=1`、OFF で従来の `./lwip` submodule。
`ping_stop()` no-op shim は `net/netif_esp32c3.c`（3チップ共有）に既に実装済み
（evidence-c3-11）のため追加変更不要。

### 静的検証

`apps/wifi_dhcp` を C5・C6 それぞれ `ASP3_LWIP_ESPIDF=ON` でビルド、両方リンク成功。

### 実機検証（★実機GREEN・GOT IP + ping、3チップとも）

| チップ | DUT | IP取得 | ping |
|---|---|---|---|
| C3 | `<MAC-21>` | 192.168.1.77 | gateway ping 継続成功（evidence-c3-11） |
| C6 | `<MAC-06>` | 192.168.1.78 | gateway ping 継続成功 |
| C5 | `<MAC-39>` | 192.168.1.79 | gateway ping 継続成功 |

★C5 (`…c8:94`) は旧台帳で「stock v9参照機・書換え禁止」と記録されていた個体。
本ラウンドはユーザーに確認の上、書換えの許可を得て実施した。

C6 の `csrw mie` 命令数=2（C3の安全性チェックと異なり、C6は実機で mie/mip
アクセスが正常＝asp3_core AGENTS.md記載のC3/C6差分どおり。安全性上の問題ではない）。

非関連の既知警告（3チップとも共通・本変更と無関係）：
`esp_shim: #8 UNSUPPORTED: esp_wifi sync API from 2nd non-pool task` — wifi_dhcp
アプリのタスク構成起因の既存shim警告。

## 3. 結論・次の一手

- ★**段階2（mbedtls/wpa）はC3について実測で完了確認**（誤報告を訂正）。
- ★**段階1（lwip→esp-idf）はC3/C5/C6の3チップとも実機GREEN**。既定はまだOFF
  （3チップ揃って初めて安全に既定反転を検討できるため、当面据え置き）。
- 残る作業：
  - 段階5：hal_stub/nuttx sdkconfigの最終清掃（fallback経路は残す）。
  - 段階6：`./hal`・`./lwip` submodule 撤廃は、C5/C6のBT/WiFi等が
    fallbackとしてまだhalに依存し得るため時期尚早。まず3チップ全構成
    （WiFi/BT/plain）で `ASP3_ESPIDF_SUPPLY=OFF` 系オプションを実際に使う
    ケースが本当に必要か（＝fallbackを永続的に残すか、削除して良いか）を
    ユーザーと確認する必要がある。

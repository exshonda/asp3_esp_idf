# WiFi blob統一 — hal(v8) から ESP-IDF v5.5.4(v8) へ

C3/C5/C6のWiFi/PHY/coexist blob（`.a`）を，hal（`asp3/hal`＝
esp-hal-3rdparty submodule，NuttX同期のos_adapter v8）から実ESP-IDF
v5.5.4（`~/tools/esp-idf`，`ESP_IDF_VERSION`=5.5.4，os_adapter v8）へ
切替える。カーネル・BT・アプリ層は不変（WiFiのみ）。禁則（hal/asp3_core
非編集）は全チップで遵守——差分はすべて`asp3/target/`側の新規ファイル・
既存targetファイルの追記のみ。

調査時点：2026-07-15，tree `cb02d61`（ブランチ`claude/blob-unify-v5.5.4`）。

## 0. 結論サマリ

| チップ | build | ABI対処 | 実機検証 | 判定 |
|---|---|---|---|---|
| C3 | 0エラー | 不要（後述§2） | wifi_scan起動せず（既存hal blobでも同一クラッシュ＝board固有・非回帰） | ブロブ統一は完了。実機scanは別問題で保留 |
| C5 | 0エラー | wifi_os_adapter.h override必要 | ★**scan完走**（20 APs found, err=0，実SSID確認，再scan安定） | **完全達成** |
| C6 | 0エラー | wifi_os_adapter.h override必要 | esp_wifi_init->0到達後，esp_shim_c6の既存Illegal instructionクラッシュ（toolchain非依存の既知事象と同一シグネチャ） | 合格（既知の別問題） |

## 1. 前提：os_adapter versionは同じv8だが構造体は完全一致ではない

`ESP_WIFI_OS_ADAPTER_VERSION`はhal・v5.5.4とも`0x00000008`（v8）で一致
するが，`wifi_os_adapter.h`の`wifi_osi_funcs_t`構造体はバイト単位で
必ずしも同一ではない。実測（`diff`）：

```
158a159,161
> #if CONFIG_SOC_WIFI_HE_SUPPORT
>     bool (*_wifi_disable_ac_ax)(void);
> #endif
```

v5.5.4のみ`_wifi_disable_ac_ax`フィールドを`_magic`の直前に持つ
（`CONFIG_SOC_WIFI_HE_SUPPORT`ガード）。halの当該ヘッダにはこの
ブロック自体が存在しない（マクロの値に関係なくフィールドが無い）。

- **C3**：`soc_caps.h`（hal・v5.5.4とも）に`SOC_WIFI_HE_SUPPORT`定義
  自体が存在しない＝`CONFIG_SOC_WIFI_HE_SUPPORT`は両者とも未定義
  （`#if`は0扱い）＝**構造体は事実上バイト同一**。ヘッダ差し替え不要。
- **C5/C6**：`soc_caps.h`が`SOC_WIFI_HE_SUPPORT (1)`を定義し，かつ
  ASP3ビルドは`CONFIG_SOC_WIFI_HE_SUPPORT=1`を既に定義済み
  （C5=`sdkconfig_stub/sdkconfig.h`実施11．C6=`hal/nuttx/esp32c6/
  include/sdkconfig.h`）。v5.5.4のblob（`libnet80211.a`他）は本フィールド
  込みでビルドされている一方，halヘッダで構造体を組み立てると
  フィールドが欠落し`_magic`のオフセットがずれる——過去の同種事象
  （`memory/c5-wifi-osi-abi-he-field.md`．v9混在時に実機で
  `0x102`/`0x3001`エラーとして確認済み）と同一パターン。

対処（C5/C6共通）：v5.5.4の`wifi_os_adapter.h`を**verbatimコピー**した
override headerを新規作成し（`wifi_v8/idf_v554_override/esp_private/
wifi_os_adapter.h`・`wifi/idf_v554_override/esp_private/
wifi_os_adapter.h`），halの`esp_wifi/include`より前のインクルードパスに
追加——このファイル1つだけをシャドウし，他のヘッダはhal側にフォール
バックする（サロゲート最小差し替え）。`esp_wifi_adapter.c`に
`wifi_disable_ac_ax_wrapper`（実ESP-IDFの`esp_adapter.c`と同じ
`return false`）を追加してフィールドを埋める。

他のヘッダ（`esp_wifi.h`・`esp_wifi_types_generic.h`・`esp_now.h`等）も
diffを取ったが，構造体フィールドの追加・削除は無く（enum値のエイリアス
化・doc追記・新API追加のみ），`wifi_init_config_t`・`wifi_scan_config_t`
等のscan/init経路で使う構造体はhal/v5.5.4間でバイト同一——ヘッダ
差し替えは`wifi_os_adapter.h`の1ファイルのみで足りる。

`esp_phy/include`・`esp_coex/include`はバイト完全一致（`esp_private/
phy.h`のcopyright年表記のみ差分・関数追加のみで既存API不変）——
差し替え不要。

## 2. blobそのものの世代差：hal blobにしか無い3関数

hal（新しめのNuttX同期スナップショット）のnet80211.a/pp.aは，
wpa_supplicantの比較的新しい機能（WPA3互換モード・RSNXE override・
PMKキャッシュskip制御）に対応する3関数を提供するが，v5.5.4のblob
（古い世代）には**そもそも存在しない**（`nm`実測：hal側`T`定義あり，
v5.5.4側は完全に不在）：

- `esp_wifi_skip_supp_pmkcaching`
- `esp_wifi_sta_get_ie`
- `esp_wifi_is_wpa3_compatible_mode_enabled`

hal同梱のwpa_supplicantソース（`wpa.c`・`esp_hostap.c`・`esp_wpa3.c`．
差し替えていない＝hal禁則）はこれらを無条件に呼ぶため，v5.5.4 blobへの
リンクだけでは未定義参照になる。3チップとも`esp_shim_blobglue.c`に
`ASP3_WIFI_BLOB_V554`ガード付きの機能無効化スタブを追加した（open
scanでは実行時未到達．WPA3互換モードやRSNXE override自体が「存在し
なかった旧世代のblobと同じ挙動」に落ちるだけ）。

## 3. cmakeの変更（reversible）

3チップとも同じパターン：

```cmake
option(ASP3_WIFI_BLOB_HAL "Use hal(v8) blob instead of v5.5.4 unification" OFF)
set(IDF_V554 /home/honda/tools/esp-idf)   # 未定義なら既定値
if(ASP3_WIFI_BLOB_HAL)
    set(ASP3_WIFI_BLOB_SRC ${ESP_HAL_DIR})     # 従来のhal
else()
    set(ASP3_WIFI_BLOB_SRC ${IDF_V554})        # 既定＝v5.5.4統一後
    list(APPEND ASP3_COMPILE_DEFS ASP3_WIFI_BLOB_V554=1)
    list(PREPEND ASP3_INCLUDE_DIRS .../idf_v554_override)   # C5/C6のみ
endif()
list(APPEND ASP3_LINK_OPTIONS
    -L${ASP3_WIFI_BLOB_SRC}/components/esp_wifi/lib/${WIFI_CHIP_SERIES}
    -L${ASP3_WIFI_BLOB_SRC}/components/esp_phy/lib/${WIFI_CHIP_SERIES}
    -L${ASP3_WIFI_BLOB_SRC}/components/esp_coex/lib/${WIFI_CHIP_SERIES}
)
```

`-DASP3_WIFI_BLOB_HAL=ON`でいつでもhal(v8)へ戻せる（reversible。
C6の旧`ASP3_WIFI_BLOB_IDF`診断枝——`docs/blob-inventory.md`§7で
「未検証のまま」と記録されていた一時計装——はこの統一で廃止し，
C3/C5と同じ`ASP3_WIFI_BLOB_HAL`へ一本化した）。

変更ファイル一覧：

| チップ | cmake | adapter | blobglue | override header（新規） |
|---|---|---|---|---|
| C3 | `esp_wifi.cmake` | （不要） | `wifi/esp_shim_blobglue.c` | （不要） |
| C5 | `esp_wifi_v8.cmake` | `wifi_v8/esp_wifi_adapter.c` | `wifi_v8/esp_shim_blobglue.c` | `wifi_v8/idf_v554_override/esp_private/wifi_os_adapter.h` |
| C6 | `esp_wifi.cmake` | `wifi/esp_wifi_adapter.c` | `wifi/esp_shim_blobglue.c` | `wifi/idf_v554_override/esp_private/wifi_os_adapter.h` |

## 4. 実機検証

各chipとも`build.ninja`の`-L`実体で`/home/honda/tools/esp-idf/...`が
実際にリンクされていることを機械確認済み（パス推測ではない）。

### C3（`60:55:F9:57:BA:BC`，usbjtag console）

- build：0エラー，RAM 88.74%（hal版88.76%と同水準・非回帰）。
- 実機：`wifi_scan`（hal blob・v5.5.4 blob**双方**）で，flash書込み・
  ハッシュ検証後の起動が早期ROM段階（`Saved PC≈0x42000d8c`台，
  ESP-ROM文字列出力直後）で`Illegal instruction`→`Breakpoint`ループに
  入り，アプリのバナーにすら到達しない。**同一board・同一手順で
  `ble_host_smoke`は正常起動**（バナー・BLE広告まで到達）——WiFi
  ビルド固有の事象。
- **判定**：hal blob（変更前の既存ベースライン）でも**バイト単位で
  同一のクラッシュ**を確認したため，本v5.5.4統一が原因ではない。
  board固有（or 既存の別問題）と判断し，非回帰は成立。この特定board
  （BA:BC）でのwifi_scan実機scan成功は別タスクとして持ち越し
  （電源再投入等の要人手対応が必要な可能性——最終報告に明記）。

### C5（`D0:CF:13:F0:C8:94`，UART0 console＝`/dev/ttyUSB4`）

- build：0エラー，RAM 76.05%（hal版と同水準）。
- 実機：★**scan完走**
  ```
  wifi_scan: esp_wifi_init -> 0
  wifi_scan: esp_wifi_start -> 0
  wifi_scan: esp_wifi_scan_start -> 0
  wifi_scan: 20 APs found (err=0)
    [0] <SSID-INST> (rssi=-46 ch=116)
    [1] <SSID-INST-1X> (5GHz) (rssi=-46 ch=116)
    [2] <SSID-EDU> (rssi=-46 ch=116)
  ```
  実SSID確認・再scanループも継続動作・WDT/panicなし。
- **判定**：v5.5.4 blobでWiFi完全動作を実証。

### C6（`14:C1:9F:E0:5A:9C`，usbjtag console）

- build：0エラー，RAM 89.47%（hal版89.48%と同水準・非回帰）。
- 実機：
  ```
  wifi_scan: esp_wifi_init -> 0
  wifi_scan: DIAG post-init ...
  esp_event: WIFI_EVENT id=43
  esp_shim: set_isr intno=1 handler=42063a96
  Illegal instruction.
  ```
  `esp_wifi_init`完走（★到達）後，`set_isr intno=1`直後に
  `Illegal instruction`で停止。
- **判定**：`docs/c5-toolchain.md`のesp-15.2実測（xpack/esp両
  toolchainでバイト単位同一のクラッシュ．「`set_isr intno=1
  handler=...`直後・`pc=0x00000000`のnullジャンプ」）で確認済みの
  toolchain非依存の既存事象と**同一シグネチャ**（handler番地は
  blob差替えで変わるが発生パターンは同一）。本blob統一が原因の新規
  回帰ではなく，既知の別問題（本タスクのスコープ外）。タスクの合格
  基準「build壁ゼロ＋esp_wifi_init到達」を満たす。

## 5. reversibility（hal(v8)へ戻す方法）

各チップのビルドに`-DASP3_WIFI_BLOB_HAL=ON`を追加するだけで，
`ASP3_WIFI_BLOB_SRC`が`ESP_HAL_DIR`に戻り，`ASP3_WIFI_BLOB_V554`も
未定義になる（blobglueのスタブ3関数・C5/C6のwifi_disable_ac_ax_wrapper
はコンパイルアウトされ，hal blob自身の実装のみが使われる＝二重定義
にならない）。override headerディレクトリはhalが先に見つかるパス
順序上，`ASP3_WIFI_BLOB_HAL=ON`時は`ASP3_INCLUDE_DIRS`に追加され
ないため無害。

## 6. 未検証・今後の課題

- C3実機（BA:BC）でのwifi_scan完走——board固有の早期クラッシュの
  原因調査（電源再投入含む要人手対応の可能性）。
- C5#1（`A7:44`．latch中）・C3の別board（`60:55:F9:57:C2:60`，
  `docs/wifi-scan-c3-crash.md`でwifi_scan実機成功実績あり）での
  再検証は本ラウンド未実施（範囲外・board競合）。
- BT/BLEのblob統一は本タスクの対象外（`docs/wifi-blob-generation-todo.md`
  の結論「単独WiFiは現状維持」はWiFi単体の話であり，本タスクは
  そのWiFi blobをv5.5.4へ進めた形——BT統一は別タスク）。

## 7. 関連ドキュメント

- `docs/blob-inventory.md`——hal/v6.1のblob世代対応表（本タスクの
  前身調査．v5.5.4は本ドキュメントで新たに対象に追加）。
- `docs/c5-toolchain.md`——esp-15.2 toolchain統一実測（C6の既知
  クラッシュシグネチャの一次資料）。
- `memory/c5-wifi-osi-abi-he-field.md`——C5でのCONFIG_SOC_WIFI_HE_SUPPORT
  ／`_wifi_disable_ac_ax`問題の初出（v9混在時．本タスクで同一パターン
  がv5.5.4でも再発することを確認）。

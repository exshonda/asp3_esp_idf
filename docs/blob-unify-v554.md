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
| C3 | 0エラー | 不要（後述§2） | ★**scan完走**（19 APs found, err=0，実SSID確認，再scan安定，複数回43 APsまで観測） | **完全達成** |
| C5 | 0エラー | wifi_os_adapter.h override必要 | ★**scan完走**（20 APs found, err=0，実SSID確認，再scan安定） | **完全達成** |
| C6 | 0エラー | wifi_os_adapter.h override必要 | esp_wifi_init->0到達後，esp_shim_c6の既存Illegal instructionクラッシュ（toolchain非依存の既知事象と同一シグネチャ） | 合格（既知の別問題） |

**C3の途中経過について**：本ラウンド前半で「board固有の早期クラッシュ」と
誤判定した記録が残っていたが，これは**調査ミス**——手動cmake configureで
`-DESP32C3_QEMU=OFF`を渡し忘れていたことが原因（既定`ON`＝
`TOPPERS_USE_QEMU`定義→`chip_initialize()`が実機に存在しない`mie` CSRへ
`csrw`命令を出し，実機で即Illegal instruction。`tmp/c3ble.sh`が明記して
いる既知の罠）。クラッシュPC（`addr2line`で`_kernel_chip_initialize`
→`csrw mie,a5`と確認）を突き止めて修正后，同じhal blob・v5.5.4 blob
双方で**scan完走**を確認した（§4参照）。「board固有の問題」という
結論は誤りで撤回する——教訓として本節に残す。

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
- **判明した調査ミスと修正**：手動cmake configureで
  `-DESP32C3_QEMU=OFF`を渡し忘れていたため（既定`ON`），`wifi_scan`
  （hal blob・v5.5.4 blob双方）が実機で早期に`Illegal instruction`
  →`Breakpoint`ループに陥った。`riscv-none-elf-addr2line`でクラッシュ
  PC（`0x42025e6c`）を特定したところ`_kernel_chip_initialize`
  （`arch/riscv_gcc/esp32c3/chip_kernel_impl.c:129`）内の
  `csrw mie,a5`——これは`#ifdef TOPPERS_USE_QEMU`ガード下のQEMU専用
  命令で，実機ESP32-C3は`mie` CSR自体を実装しないため書込みで
  Illegal instructionになる（`tmp/c3ble.sh`が明記する既知の罠と
  同一）。`-DESP32C3_QEMU=OFF`を付けて再ビルドし
  （`objdump -d | grep -c "csrw.*mie"` = 0を確認），再実機検証した
  ところ両blobともscan完走した。
- 実機（修正後）：★**scan完走**
  ```
  wifi_scan: esp_wifi_init -> 0
  wifi_scan: esp_wifi_start -> 0
  wifi_scan: esp_wifi_scan_start -> 0
  wifi_scan: 19 APs found (err=0)
    [0] <SSID-INST-1X> (rssi=-38 ch=1)
    [1] <SSID-INST-G> (rssi=-38 ch=1)
    [2] <SSID-INST> (rssi=-39 ch=1)
    ...
  wifi_scan: RESCAN 16/43/43/34/35 APs (err=0)
  ```
  実SSID確認・再scanループ継続（複数回43 APsまで観測）・WDT/panic
  なし。**hal blob（`-DASP3_WIFI_BLOB_HAL=ON`）でも同一手順で
  scan完走**（19→19→19→17 APs，err=0）——非回帰も正しく実証。
- **判定**：v5.5.4 blobでC3もWiFi完全動作を実証。「board固有の問題」
  という前半の判定は誤り（§0参照）——実際はテストハーネス側の
  フラグ漏れで，blob統一そのものとは無関係だった。

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

- C6の既存Illegal instructionクラッシュ自体の根本原因調査（本タスクの
  スコープ外．`docs/wifi-shim-c6.md`の実施系列で継続）。
- C5#1（`A7:44`．latch中）での再検証は本ラウンド未実施（latch中の
  ため不接触）。
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

## 8. BTの v5.5.4 実現性判定（2026-07-15，tree `39357dd`〜）

C5/C6のBTは現状ESP-IDF v6.1（`libble_app.a`新世代）を使う——理由は
「hal(v8)のlibphyがBTで`register_chipv7_phy`を収束させずハングした」
（C5実施09・C6 §11-13）。v5.5.4も同じos_adapter ABIマクロ`v8`を
名乗るため「v5.5.4も同様にハングする公算が高い」という懸念が本タスクの
出発点だった。**実測でこの懸念は否定された**——以下，判定手順と結論。

### 8.1 事前md5実測：BT関連blobはv5.5.4/v6.1間でバイト完全一致

「v5.5.4はv8＝hal同様ハングするかもしれない」という懸念は，WiFiの
os_adapter ABIマクロ（`ESP_WIFI_OS_ADAPTER_VERSION`＝0x08/0x09）を
BTの物理libphy blobの代理指標にした早合点だった。実測（`md5sum`）：

| チップ | blob | v5.5.4（`~/tools/esp-idf`） | v6.1（`~/tools/esp-idf-v6.1`） | 判定 |
|---|---|---|---|---|
| C5 | libble_app.a | `c2785c98f3231f74c825da6162be60bc` | `c2785c98f3231f74c825da6162be60bc` | **バイト完全一致** |
| C5 | libphy.a | `4ccdbdbe1faf04a84b4059c882febe0f` | `4ccdbdbe1faf04a84b4059c882febe0f` | **バイト完全一致**（`register_chipv7_phy`含む） |
| C5 | libbtbb.a | `f553ddd33805f6380fe103f37fe185c1` | `f553ddd33805f6380fe103f37fe185c1` | **バイト完全一致** |
| C5 | libcoexist.a | `8400ad430c6719fc9ddf67310c4eb59a` | `53b3f95021fe43caaff9dd0bf72203ca` | 別物（唯一の不一致） |
| C6 | libble_app.a | `c28653df7553ac7b9932a84b235b166b` | `c28653df7553ac7b9932a84b235b166b` | **バイト完全一致** |
| C6 | libphy.a | `3fea07086717f1c7c18f58e2d3815721` | `3fea07086717f1c7c18f58e2d3815721` | **バイト完全一致** |
| C6 | libbtbb.a | `d31c8865a4c1230bd65711847638f244` | `d31c8865a4c1230bd65711847638f244` | **バイト完全一致** |
| C6 | libcoexist.a | `10f5cb6c42fe65771d57535d9aa12094` | `68ebb703fd26b29c1a53094a3157c3cb` | 別物（唯一の不一致） |

つまりC5/C6とも，**controller・PHY・btbb（BTが実際にPHY較正／HCIで
使うblob）はv5.5.4とv6.1で1バイトも違わない同一ファイル**——「別blob
だから収束するかも」という期待も「v8系列だから必ずハングする」という
懸念も，どちらも実測前の憶測に過ぎず，物理的には**同じバイナリを
リンクし直すだけ**という結論になった。不一致は`libcoexist.a`のみ
（WiFi/BT間の無線調停ロジック．BT単体ビルドでは非活性経路が濃厚）。

bt.c/ble.c（controller層ソース，blobではない）はv5.5.4/v6.1間で
差分283行（C5）——差分の大半はコントローラ組込みSM（legacy pairing，
`CONFIG_BT_LE_SM_LEGACY`/`CONFIG_BT_LE_SM_SC`）のmbedTLS/tinycrypt
実装差で，両マクロとも本ビルドでは未定義（0）＝**両バージョンとも
コンパイル対象外の死コード**——実働に影響しない（NimBLEホストのSMP
はホスト側`ble_sm*.c`が別途担当，本差分とは無関係）。

### 8.2 cmake変更（reversible）

C5（`esp_bt.cmake`）・C6（`esp_bt_idf61.cmake`）に共通で
`ASP3_BT_IDF_V554`オプション（既定OFF＝v6.1のまま）を追加。ONで
`IDF`変数を`~/tools/esp-idf`（v5.5.4，WiFi統一と同じツリー）へ切替える
——WiFiの`ASP3_WIFI_BLOB_HAL`と対称的な設計（BTは既定=v6.1のまま，
v5.5.4はopt-inの実現性確認用）。

```cmake
option(ASP3_BT_IDF_V554 "Use ESP-IDF v5.5.4 BT ... instead of v6.1" OFF)
if(ASP3_BT_IDF_V554)
    set(IDF /home/honda/tools/esp-idf)
else()
    set(IDF /home/honda/tools/esp-idf-v6.1)
endif()
```

**build壁が1つ**（C5・C6共通）：`phy_init.c`が`esp_private/wifi.h`→
`esp_wifi_types.h`→`esp_wifi_types_generic.h`と辿る際，v5.5.4の当該
ヘッダは`"esp_interface.h"`を直接includeする（v6.1のヘッダはこの
includeが無い＝この一点だけ書き方が変わっていた）。実体は
`${IDF}/components/esp_hw_support/include/esp_interface.h`——`ASP3_
BT_IDF_V554=ON`時のみこのディレクトリを`ASP3_INCLUDE_DIRS`へ追加して
解決（hal側に同名ファイルが無いため衝突なし）。この1点を除き，
build壁ゼロ。

なお`register_chipv7_phy`を実際に呼ぶ`phy_init.c`自体もv5.5.4/v6.1間で
差分58行あるが（`diff`実測），全て`SOC_PM_SUPPORT_PMU_MODEM_STATE &&
CONFIG_ESP_WIFI_ENHANCED_LIGHT_SLEEP`ガード内（light-sleep時のPHY
retention．本ビルドは`CONFIG_ESP_WIFI_ENHANCED_LIGHT_SLEEP`を定義
しないため両バージョンともコンパイル対象外の死コード）——
`register_chipv7_phy`呼出しパス自体は無傷。

### 8.3 実機検証（warm．cold未実施＝§8.4参照）

**C5（`bt_smoke_c5`，D-1コントローラのみ．board C5#2 `D0:CF:13:F0:C8:94`）**：
build成功（RAM 70.83%）。RTSリセットによるwarm boot **3回連続で完全
成功**（`esp_bt_controller_init OK`→`esp_bt_controller_enable(BLE)`→
`phy: libbtbb version...`出力→`esp_bt_controller_enable OK`→HCI Reset
送出→Command Complete受信→`Phase D-1 milestone reached`→`done`）。
`register_chipv7_phy`によるPHY較正がwarmでは**確実に収束**することを
実証。

**C5（`ble_host_smoke_c5`，NimBLEホスト＋SM．同board）**：build成功
（RAM 77.80%）。フラッシュ後の初回bootでTG0 WDTリブートループ
（`Core0 Saved PC:0x40038598`）が発生——**ただし直後に直前まで動作して
いた`bt_smoke_c5`の同一バイナリ（フラッシュ済みasp_flash.bin）を
再書込みして再テストしたところ同一のWDTループが再現**——これは
`memory/c5-latched-board-state.md`が既述する「C5はCPU_LOCKUP/WDT
ループを繰り返すとsoft/hard resetを生き延びるラッチ状態に陥る」既知の
ボード現象と一致するシグネチャ（`0x40038598`は「赤herring」ROM PCと
memoryに明記済み）。**差分再テストにより「同一の既知良品バイナリが
直前と後で結果が変わった」ことを実証**——ble_host_smoke_c5固有の
v5.5.4リグレッションではなく，ボードのラッチが原因である公算が高い
（が，ラッチ解除＝物理電源再投入until確定できない．次段の課題＝
§8.4）。

**C6（`bt_smoke_c6`，`ESP32C6_BT_IDF61=ON`のD-1．board C6 port1
`14:C1:9F:E0:5A:9C`）**：build成功（RAM 65.86%）。RTSリセットによる
warm boot **2/2回連続で完全成功**（C5と同一の判定基準：
`esp_bt_controller_enable OK`→HCI Reset→Command Complete→
`Phase D-1 milestone reached`→`done`）。★C6はcoldでは別問題（アナログ
PLLがwarm handoff依存．`docs/ble-c5c6.md`実施91等）が既知のため，本
warm結果はv5.5.4のcold収束を意味しない——C6のcold判定は本ラウンドでは
未実施（deferred，既存のcold-PLL課題と交絡するため）。

### 8.4 結論と残課題

- **C5**：controller-only（D-1）はwarmで3回連続完全成功——v5.5.4への
  BT統一は「PHYハング」という当初の主要リスクに関しては**実現可能**と
  判定できる強い実測的根拠が揃った（md5一致＋warm実機収束）。ただし
  タスク原本が要求する「真の cold 電源投入での判定」は，C5#2ボードが
  検証中盤でラッチ状態に入ったため**未達**——ボードの物理電源断
  （port3 off→on，30秒以上）を経ないと再開できない。**要人手**：
  ユーザーに「C5#2（port3）を物理的にoff→onしてほしい」と依頼する
  必要がある（usbhub経由のon/off操作はエージェントから権限拒否済み）。
  ★**さらに重要な留保**：`docs/ble-c5c6-plan.md` §17.1で「C5の«真
  cold»（物理電源断）健全性は検証できなかった」と明記されている
  通り，**現行v6.1版のC5 BTも真coldでは未検証**——「クリーンな判定台」
  という位置付けはwarm/BlueZ放射の実績を指すもので，v6.1自体のcold
  実績があるわけではない。したがって電源再投入後にやるべきことは
  「v5.5.4を再検証」ではなく，**v6.1とv5.5.4を同一手順・同一boardで
  cold A/Bする**こと：(a) v6.1版`bt_smoke_c5`をcold 1発目で検証（今回
  初めてv6.1のcold実績を確定），(b) v5.5.4版`bt_smoke_c5`を同様にcold
  検証，(c)（時間があれば）`ble_host_smoke_c5`（NimBLE，adv到達）を
  v5.5.4でcold検証——libphy/controller/btbbはmd5一致だが`libcoexist.a`
  とbt.c/ble.cソースはv5.5.4/v6.1間で異なる（§8.1）ため，このA/Bが
  「両方成功」なら実現性確定＋v6.1のcold実績も同時に確定，「v5.5.4のみ
  cold失敗」ならcoexist/ソース差分に原因を局在化できる——この1実験で
  両方の未決着が閉じる。`ble_host_smoke_c5`のWDTループは，直後の
  差分再テスト（直前まで動作していた`bt_smoke_c5`同一バイナリの再
  フラッシュ）で同一ループが再現したことから**board latchの可能性が
  高い**が，latch発生がNimBLEビルドのフラッシュ直後だった以上
  「v5.5.4+NimBLE固有の不具合」の可能性も排除できていない——
  **現時点の判定は「未検証（inconclusive）」であり「問題なし」ではない**。
  cold再検証で確定させること。
- **C6**：D-1はwarmで2回連続完全成功——v5.5.4のBT controller/phy/btbb
  がwarmでは収束することを実証。ただしC6は元々cold PLLの別課題が
  あるため，本タスクの「PHYハング再発」懸念とは別に，cold判定は
  そもそも意味を持ちにくい（v6.1版も同じcold課題を抱える）。C6の
  cold判定はdeferred＝既存のcold-PLL調査（`docs/ble-c5c6.md`実施系列）
  待ち。
- **C3**：タスク原本は「v5.5.4にC3のBT libが無い（submodule pathspec
  無し）」という前提で対象外としていたが，これは実測で**誤り**だった
  ——`~/tools/esp-idf/components/bt/controller/lib_esp32c3_family/
  esp32c3/libbtdm_app.a`・`controller/esp32c3/bt.c`とも実在する
  （訂正として記録）。ただしmd5実測では，v5.5.4のC3 libbtdm_app.a
  （`d9753a31a8eeac9da8f3718cdfdb4938`）はv6.1と同一だが，**現在C3が
  実際に使っているのはhal版**（`dfdadb9ddc12eeeab85edfb5d26eb4bf`＝
  別物）——つまりC3をv5.5.4へ切替えるのはC5/C6と違い「既に実績のある
  バイナリへの回帰」ではなく「未検証の別blobへの新規切替」で，リスク
  性質が異なる。本ラウンドではC5（クリーンな判定台）とC6を優先し，
  C3は**このバイト差分の存在を記録するに留め，実装・実機検証は次ラウンド
  以降に持ち越す**（hal版で現状動作中＝D-2d bond達成済みのため急ぐ理由が
  薄い）。
- **暫定的な設計判断**：cold判定が未完了のため，`ASP3_BT_IDF_V554`は
  **既定OFF（v6.1のまま）を維持**——本ラウンドはreversibleな
  opt-inオプションの追加とwarm実機実証までに留め，デフォルト切替は
  cold判定完了後の判断とする。

## 9. 関連ドキュメント（BT実現性判定，追補）

- `memory/c5-latched-board-state.md`——C5のWDTループ・ラッチ現象の
  既存記録（本ラウンドのble_host_smoke_c5 WDTループが同一シグネチャ
  であることの一次資料）。
- `docs/ble-c5c6.md`——C6のcold PLL課題（実施89〜91系列）の一次資料。

## §10 ★親による cold 確認：v5.5.4 BT が C5 で cold 収束（D-1）＝BT-v5.5.4 統一の «安全性» 確定
BT feasibility subagent の要請どおり、親が C5#2(port3) を電源再投入して latch 解除→
`build/c5bt_v554`（bt_smoke_c5・`ASP3_BT_IDF_V554=ON`＝v5.5.4 blob）を flash→**真の電源断
（off 10s→on）で cold 一発目**を rts_boot_capture（氾濫根治済＝clean）で採取：
- `esp_bt_controller_init OK` → `I (881) phy: libbtbb version: 92325d6`（PHY 較正 run）→
  **`esp_bt_controller_enable OK`（register_chipv7_phy 収束・cold）** → HCI Reset →
  `VHCI recv 7 bytes` → **`HCI Command Complete`（controller alive・VHCI loopback OK）**。
∴**v5.5.4 BT は C5 で «cold から» D-1 収束**。§9 の md5 実測（v5.5.4 の libble_app/libphy/
libbtbb は v6.1 と «バイト同一»、libcoexist のみ相違）と合わせ、**BT-v5.5.4 統一は «回帰なし・
cold 動作» で安全性確定**。§17.1 の「v6.1 自体も cold 未検証」は «同一 blob が cold 収束» で
実質解消。**cold-PLL ハングは «C6 固有»**（C5 BT は cold 収束＝C6 の cold-PLL hard wall は
blob 非依存の C6 silicon/analog 固有）も裏付け。
**残＝`ASP3_BT_IDF_V554` の既定を «ON（v5.5.4）» にして BT 統一を完了するか（WiFi は既に
v5.5.4 既定）はユーザー判断**（blob バイト同一＝低リスク・可逆）。C6 は cold-PLL 別壁につき
cold 判定は保留（warm では bt_smoke_c6 2/2 動作）。

## §11 ★full BLE cold 実証→`ASP3_BT_IDF_V554` 既定を ON（v5.5.4）へ＝BT 統一完了
§10 の D-1 に続き、**full BLE（NimBLE host＋v5.5.4 controller/phy/coexist）が cold から
adv 放射**を実証：`build/c5ble_v554`（ble_host_smoke_c5・`ASP3_BT_IDF_V554=ON`）を C5#2 へ
flash→真の電源断（off 10s→on）→BlueZ scan で **`ASP3-C5-BLE`（D0:CF:13:F0:C8:94）が cold
から NEW**。subagent の「latch で inconclusive」は board artifact（電源再投入で解消）と確定。
∴ v5.5.4 の libcoexist（v6.1 と «唯一相違» する blob）込みでも full BLE が cold 動作。

**→ 既定を flip**：`esp32c5_espidf/esp_bt.cmake` と `esp32c6_espidf/esp_bt_idf61.cmake` の
`option(ASP3_BT_IDF_V554 ... OFF)` を **`ON` へ**。WiFi（`ASP3_WIFI_BLOB_HAL` 既定 OFF＝
v5.5.4）と揃い、**WiFi・BT 双方が既定 v5.5.4＝blob 統一完了**。
- 検証：C5 BLE clean build＝rc=0・RAM 77.80%・**tools/esp-idf(v5.5.4) 参照 334 / esp-idf-v6.1
  参照 0**（bt/controller/esp32c5・esp_phy/esp32c5・esp_coex/lib/esp32c5・esp32c5-bt-lib）。
- 同 C6 BLE clean build＝rc=0・RAM 72.36%・v5.5.4 参照 334 / v6.1 参照 0。
- 可逆：`-DASP3_BT_IDF_V554=OFF` で v6.1 tree に戻る。
- C6 は cold-PLL 別壁（blob 非依存の C6 silicon 固有）につき cold BLE は未収束のまま（warm
  bt_smoke_c6 2/2 は不変）。統一は «同一 blob へ揃える» 目的を達成、C6 cold は独立課題。

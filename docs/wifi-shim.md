# Wi-Fi os_adapter shim（Phase B-2）

## 課題

Wi-Fiバイナリblob（libnet80211.a／libpp.a／libcore.a等）は
`wifi_osi_funcs_t`（osi関数テーブル・約120エントリ）を通じてOS機能を
要求する。要求はFreeRTOS流の**動的生成**（task/queue/semaphore/mutex/
timer/malloc）だが，ASP3は**全カーネルオブジェクト静的生成**
（cfgによるCRE_*）である。

## 設計方針：静的プールからの動的割当て

カーネル（asp3_core）は無改変のまま，shim層（本リポジトリ）で
「静的に生成したオブジェクトのプール」から動的にみえる割当てを行う。
AGENTS.md禁則②（カーネル内動的メモリ禁止）はカーネル内の規定であり，
shim層＝アプリ/ライブラリ層のヒープは対象外（ただし静的配列上に実装）。

| osi要求 | ASP3での実現 |
|---|---|
| malloc系（4系統：通常/internal/DMA/wifi専用） | 静的配列上の簡易ヒープ（first-fit＋結合）へ一本化。ESP32-C3はDRAM/IRAM共有・PSRAMなしのため系統の区別は不要 |
| semaphore（binary/counting・timeout付き） | `CRE_SEM`×Nのプール＋割当て管理。timeoutは`twai_sem` |
| mutex（通常/再帰） | `CRE_MTX`×Mのプール。再帰はshimで所有者＋カウント管理のラッパ |
| queue（任意サイズitem・timeout・ISR送信） | shim実装（ヒープ上リングバッファ＋空き/データの2セマフォ）。ASP3のdtqはintptr固定のため使わない |
| event group | shim実装（`CRE_FLG`プール or 独自） |
| task create/delete/delay | `CRE_TSK`×Kのプール（共通エントリ＋関数ポインタ渡し）。優先度はFreeRTOS(0-25,大=高)→ASP3(小=高)へ写像。delayは`dly_tsk` |
| timer（ets_timer系：arm/disarm/done） | shim専用タイマタスク＋ソート済みリスト（発火はタスク文脈＝コールバックがblocking可） |
| 割込み（set_isr/ints_on/off） | Wi-Fi系ソースをCPU割込み線へ事前route＋cfgで共通ディスパッチャをDEF_INH登録し，shimの関数ポインタ表で動的にみせる |
| critical section | `loc_cpu`/`unl_cpu`（ネストはshimカウンタ） |
| NVS | 無効化（スタブ）。較正データの永続化は後続課題 |
| 乱数・時刻 | HW RNGレジスタ／SYSTIMER |
| esp_event（SCAN_DONE等の通知） | 最小shim（登録コールバックの直接呼出し） |
| log（printf系） | vsnprintf相当の小実装→`syslog("%s")` 折返し |

## スコープ

- **B-2a**：shim実装＋blobリンク＋`esp_wifi_init()`成功＋**scan**
  （SSID一覧表示）——実機で検証
- **B-2b**：**AP接続**（WPA2＝wpa_supplicantのビルドが必要）
- TCP/IPスタックはスコープ外（接続確認まで）

## 参照

- 設計テンプレート：NuttXの `esp_wifi_adapter.c`（osi約123エントリの
  NuttX実装）・`Wireless.mk`（ビルド配線）＝`ref-esp32c3/nuttx/`に取得
- blob：`hal/components/esp_wifi/lib`（esp32-wifi-lib submodule）・
  `hal/components/esp_phy/lib`

## 実施結果

### B-2b 達成：実機WPA2接続成立（2026-07-04）

接続デモ `apps/wifi_connect/`（-DWIFI_SSID/-DWIFI_PASSWORDで接続先指定）で
**実機ESP32-C3がWPA2 APへL2接続成立**（STA_CONNECTED・複数回再現）：

```
event: STA_CONNECTED
wifi_connect: CONNECTED to '<SSID>' rssi=-56 ch=1
```

AP発見→認証→アソシエーションまでは容易に到達したが，**WPA2 4-way
ハンドシェイクがタイムアウト（reason=15）**する壁があった．実機JTAG
（wpa_supplicant状態機械にbrえ）で「msg1受信→PTK導出→msg2送信→msg1
再受信」の無限ループ＝AP側がmsg2を黙って拒否（bad MIC）と判明．原因は
Direct Boot（ESP-IDF起動シーケンス非経由）に起因する2つのshimバグ：

1. **HW RNGレジスタのアドレス誤り**（esp_shim.c esp_shim_random）：
   0x6002607C（常時0を返す別レジスタ）を読んでいた．正しくは
   SYSCON_RND_DATA_REG=0x600260B0．SNonceが常時全ゼロになり，AP側が
   nonce再利用/リプレイとみなしmsg1を再送し続けていた．
2. **PSA Crypto未初期化**（esp_shim.c esp_shim_initialize）：
   esp_supplicantのhmac_vector（sha1_prf経由のPTK/MIC導出）はPSA Crypto
   API（psa_import_key等）を使い，psa_crypto_init()が前提．ESP-IDFは
   コンポーネント初期化セクション（ESP_SYSTEM_INIT_FN）で自動実行するが，
   本ポートのASP3独自起動は通らず未呼出しだった＝PTKに未初期化スタック
   のゴミが入りMICが必ず不一致．esp_shim_initialize()で明示的に
   psa_crypto_init()を呼ぶよう修正（PBKDF2のPMK導出は旧mbedtls_md経路で
   PSA非依存のため正しく，PTK側だけが壊れていた）．

較正はNVS永続化なし（毎回フル較正）．IP/DHCPはスコープ外（L2まで）．

### B-2a 達成：実機Wi-Fiスキャン成功（2026-07-03）

実機ESP32-C3で **esp_wifi_init→start→scan→SSID一覧取得** が完動．周囲の
AP（16〜17個）のSSID・RSSI・チャンネルを実受信（RF較正も機能）．

実機JTAG（OpenOCD-esp32＋riscv32-esp-elf-gdb）デバッグで解明・解決した
3つの起動ブロッカー（いずれもDirect Boot＝ESP-IDF起動シーケンス非経由に
起因）：

1. **モデムクロック未初期化**（hal_initハング）：ESP-IDFの
   esp_perip_clk_init()が起動時にSYSTEM_WIFI_CLK_EN_REG（0x60026014）へ
   SYSTEM_WIFI_CLK_EN（0x00FB9FCF）をセットしてモデム系クロックを起こす．
   Direct Bootではこれが走らずMAC MMIO（0x60033D14）が無応答．
   → hardware_init_hookで同値をセット（TOPPERS_ESP32C3_WIFI時のみ）．
   C3のperiph_ll_wifi_module_enable_clk_clear_rstのマスクは0（no-op）で，
   モデムクロックはこの起動時設定に依存する点が非自明．
2. **coex os_adapter未登録**：g_coex_adapter_funcs（coex_adapter_funcs_t）を
   実装し起動時にesp_coex_adapter_register（NuttXのbringup相当）．
3. **coexist_funcs（ROM常駐グローバル・0x3fcdf83c）がNULL**：ROMのcoexist
   ラッパー群がこのポインタ経由でメソッドを呼ぶが，WiFi単独のDirect Boot
   では誰も設定せず，WiFi PM（pm_disconnected_start）がNULLメソッドで
   クラッシュ．→ coexist非アクティブ（全メソッド0返し）のダミーテーブルを
   指させて回避（esp_coex_adapter.c）．NuttXはWiFi単独時coex osiを全スタブ
   化するが，pp lib内部のPM経路は直接ROM coexist_funcsを読むため本対処が要る．

### 到達段階

init／start／scan／done すべて到達（**B-2a完了**）．次段（B-2b）はAP接続
（WPA2）＝wpa_supplicantの本経路．較正は毎回フル（NVS永続化なし）．

### 旧・実装ログ

### shim・adapter実装（2026-07-03）

- `wifi/esp_shim.[ch]`＋`esp_shim.cfg`＋`esp_shim_cfg.h`：基盤プリミティブ
  （静的プール：セマフォ24・ミューテックス8・DTQ4×64・タスク6×8KB＋
  タイマタスク．ヒープ112KB＝静的配列上first-fit．キューはDTQ＋ヒープ
  確保itemのポインタ渡し＝正しいブロッキングと非タスク文脈送信）
- `wifi/esp_wifi_adapter.c`：wifi_osi_funcs_t全エントリのASP3実装
  （ABI version 0x8．event group／NVSはNuttXと同じくスタブ・coexは
  libcoexistへパススルー・割込みはblob指定の線番号を尊重し1〜15を開放
  ＝ターゲットのペリフェラルは線16〜18へ退避）
- `wifi/esp_event_shim.c`：イベント通知の最小実装（登録ハンドラの直接
  呼出し）・`wifi/esp_shim_libc.c`：malloc系グローバル→shimヒープ・
  ファイルI/Oスタブ・esp_log系→syslog折返し
- `apps/wifi_scan/`：スキャンデモ（init→start→scan→SSID一覧）


### コンパイル通過（2026-07-03）

`cmake --build build/wifi`（wpa_supplicant/mbedtls/esp_wifi 全123ファイル）で
コンパイルエラー0を達成。変更内容：

- **インクルードパス追加**（`asp3/target/esp32c3_espidf/esp_wifi.cmake` §1b）：
  `esp_hw_support`／`esp_wifi`／`heap`／`efuse`／`esp_event`／`log`／
  `riscv`／`esp_hal_security`／`esp_security`／`esp_hal_ana_conv`／
  `esp_hal_gpio`／`esp_rom/esp32c3/include/esp32c3`／`esp_pm`／`esp_phy`
  （すべてesp-hal-3rdparty内に実在するヘッダをたどって特定）。
- **libc系スタブ追加**（`hal_stub/include/`）：`errno.h`・`unistd.h`・
  `endian.h`・`machine/endian.h`・`sys/types.h`・`sys/time.h`・
  `sys/queue.h`（空スタブ）・`sys/param.h`・`sys/lock.h`（`_lock_t`）。
  既存`string.h`/`stdlib.h`/`time.h`/`assert.h`にも不足関数
  （strcmp/strncmp/strstr/strerror・strtol・struct tm/gmtime/mktime・
  static_assert）を追加。
- **プロジェクト固有スタブ**：`platform/os.h`（ESP-IDF版はFreeRTOS，
  NuttX版はNuttXカーネルヘッダに依存し両方採用不可のため独自に作成。
  OS_PASS/OS_FAIL・esp_os_*型・TickType_t/UBaseType_t/BaseType_tのみ）、
  `esp_netif.h`（esp-hal-3rdpartyに実体が無いコンポーネント。
  `esp_netif_t`等の不完全型宣言のみ＝実際に呼び出すソースは無い）。
- **`nuttx/config.h`にCONFIG_*追加**：`CONFIG_ESPRESSIF_LOG_LEVEL`・
  `CONFIG_ESPRESSIF_WIRELESS`・`CONFIG_ESPRESSIF_WIFI`・
  `CONFIG_ESPRESSIF_WIFI_*`（RX/TXバッファ数等，ESP-IDF Kconfig既定値）・
  `CONFIG_WPA_WAPI_PSK=0`（WAPI無効固定）。
- **除外ソースなし**：不足ヘッダはすべて実在パス追加かスタブ追加で解決でき，
  ソース自体をコメントアウトする必要は無かった（`esp_psa_crypto_init.c`
  含め123ファイル全て採用）。
- **`__NuttX__`は定義しない**：`esp_event.h`のUBaseType_t等は
  `platform/os.h`スタブ側で肩代わりし，`__NuttX__`を定義すると
  `esp_mbedtls.h`のシンボルリネーム（未パッチのmbedtls本体と不整合）や
  `nuttx/kmalloc.h`等の本物のNuttXカーネルヘッダ依存を誘発するため。

### リンク結果

現状の`build/wifi`（サンプルアプリはWi-Fi APIを一切呼ばない）は
**リンクも成功**する。`-ffunction-sections`+`--gc-sections`により，
未参照のWi-Fiエントリポイント（`esp_wifi_init()`等）ごと未実装シンボルへの
参照も削除されるため。os_adapter実装後，実際にWi-Fi APIを呼ぶアプリコードを
書いた時点で初めて未定義シンボルがリンクエラーとして顕在化する。

参考として，wifi関連123オブジェクトファイルを対象に`nm -A -u`相当で
集めた「真の未定義シンボル」（同オブジェクト群の中で定義されないもの）は
210個．内訳：

| 分類 | 件数 | 代表例 |
|---|---:|---|
| libc系 | 40 | malloc/free/calloc/realloc・str*・mem*・printf系・fopen等ファイルI/O・gettimeofday/gmtime/mktime |
| コンパイラ組込み(`__*`) | 5 | `__bswapsi2`・`__clzsi2`・`__ctzsi2`・`__ffssi2`・`__udivdi3`（libgccが解決予定） |
| `esp_wifi_*`（osi/private API） | 87 | `esp_wifi_connect_internal`・`esp_wifi_get_config`・`g_wifi_osi_funcs`等。osi_funcs shim・wifi_init.c内部実装が対象 |
| `esp_event*` | 1 | `esp_event_post` |
| `esp_log*` | 3 | `esp_log`・`esp_log_level_set`・`esp_log_timestamp` |
| osi/OS抽象 | 2 | `current_task_is_wifi_task`・`g_wifi_osi_funcs` |
| esp_phy/esp_rom/esp_hw/efuse/hmac/random | 9 | `esp_phy_modem_init/deinit`・`esp_rom_md5_*`・`esp_efuse_get_key_purpose`・`esp_hmac_calculate`・`esp_random`/`esp_fill_random` |
| mbedtls（未コンパイルのssl/x509/psa_aead/psa_pake/base64） | 44 | `mbedtls_ssl_*`・`mbedtls_x509_crt_*`・`mbedtls_psa_aead_*`（TLS/X.509モジュール自体は本ビルド対象外＝eap_tls.c等が参照するが未着手） |
| WPS | 14 | `wps_init`・`wps_enrollee_process_msg`等（`esp_supplicant/src/esp_wps.c`が呼ぶ`wps.c`本体は本ビルド対象に未採用） |
| heap | 1 | `heap_caps_free` |
| その他 | 4 | `adc2_cal_include`・`pm_beacon_offset_funcs_empty_init`・`uuid_gen_mac_addr`・`wifi_sta_get_enterprise_disable_time_check` |

このうち`esp_wifi_*`(87)・osi/OS抽象(2)・`esp_event*`(1)・`esp_log*`(3)・
libc系(40)がos_adapter shim（本ドキュメント上部の設計）の主対象。
mbedtls(44)とWPS(14)は「未コンパイルの追加ソース」が必要な既知ギャップ
（コンパイル通過そのものには影響しない＝現行ソースリストで閉じている）。

### 検証コマンド（コンパイルまで．2026-07-03時点）

```bash
cmake --build build/wifi 2>&1 | grep -c "error:"   # → 0
cmake --build build/wifi                            # → Linking C executable asp.elf まで到達
```

### リンク成功（wifi_scanデモ．2026-07-03）

`apps/wifi_scan`（`esp_wifi_init()`等を実際に呼ぶ最小デモ）を
`ASP3_APPLDIR`/`ASP3_APPLNAME`で指定してビルドし，39個の未定義シンボル
すべてを解消，リンクまで成功した（`asp.elf`／`asp_flash.bin`生成）。

```bash
cmake -S asp3/asp3_core -B build/wifi_scan \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$(pwd)/asp3/target/esp32c3_espidf \
  -DCMAKE_TOOLCHAIN_FILE=$(pwd)/asp3/asp3_core/cmake/toolchain-riscv64.cmake \
  -DESP32C3_CONSOLE=uart0 -DESP32C3_QEMU=ON -DESP32C3_WIFI=ON \
  -DASP3_APPLDIR=$(pwd)/apps/wifi_scan -DASP3_APPLNAME=wifi_scan
cmake --build build/wifi_scan
# → [100%] Built target asp（asp.elf・asp_flash.bin生成）
```

#### 解決方法（分類別）

| 分類 | 件数 | 解決方法 | 対象シンボル |
|---|---:|---|---|
| ROMリンカスクリプト追加 | 4 | `esp32c3.rom.libc-suboptimal_for_misaligned_mem.ld`をリンク（esp_wifi.cmake §2） | `strcmp`・`strcpy`・`strncmp`・`strncpy`（`memcmp`/`memcpy`/`memmove`も同ldでPROVIDEされるが既存定義が優先） |
| esp-halソース追加（実装採用） | 2 | `esp_hw_support/periph_ctrl.c`（esp_wifi.cmake §6）＋`platform/os.h`にクリティカルセクションマクロ（loc_cpu/unl_cpu委譲）を追加 | `periph_module_reset`・`wifi_module_enable`・`wifi_module_disable`（+ `phy_module_enable`/`phy_module_disable`をesp_shim_blobglue.cが利用） |
| esp-halソース追加（同名別ファイル） | 1 | `esp_phy/src/lib_printf.c`を追加（wifi版lib_printf.cとは別実体） | `phy_printf` |
| libcシム実装（esp_shim_libc.c） | 12 | 自前vsnprintfコア実装＋各種委譲 | `vsnprintf`・`snprintf`・`sprintf`・`puts`・`setbuf`・`remove`・`rename`・`usleep`・`sleep`・`gettimeofday`・`esp_fill_random`・`heap_caps_free` |
| バグ修正（リネーム） | 1 | `coex_register_start_callback`→`coex_register_start_cb`（blob実シンボル名に合わせ訂正．nm確認） | `coex_register_start_cb` |
| blobglueシム新規（esp_shim_blobglue.c） | 19 | 新規ファイル．詳細は下表 | 下表参照 |
| リンカスクリプト修正（副作用対応） | - | `esp32c3.ld`に`.iram1`/`.coexiram`/`.wifi*iram`系（IRAM_ATTR）を.textへ，`.dram1`系を.dataへ，`.rodata_wlog_*`系を.rodataへ追加収容するルールを追加（periph_ctrl.c・blob追加で新たに現れたセクション名．overlapsエラーの解消） | - |

`esp_shim_blobglue.c`（新規ファイル）の内訳：

| シンボル | 実装内容 | 妥協点 |
|---|---|---|
| `g_misc_nvs` | `void *`固定NULL | NVS未実装．blobがNULLチェックせず参照する経路があれば実機でクラッシュしうる（未検証） |
| `misc_nvs_init`/`misc_nvs_deinit` | 常時成功／no-op | 同上 |
| `g_espnow_user_oui[3]` | 全ゼロ固定 | ESP-NOW自体がスコープ外．`esp_now_set_user_oui()`相当の設定APIは未実装 |
| `mesh_sta_auth_expire_time` | `0`固定 | ESP-MESH未使用（通常STA遷移では影響しない想定） |
| `g_log_level` | `0`固定 | blob側冗長ログを最小化するのみ．ASP3側syslogは別経路 |
| `adc2_cal_include` | no-op（意図通り） | 妥協ではない（較正コンストラクタをリンクさせないだけの目的のため空実装が正）|
| `esp_efuse_get_key_purpose` | `ESP_EFUSE_KEY_PURPOSE_USER`(0)固定 | eFuse本体コンポーネント未採用．鍵未プロビジョニングとして正しく振る舞う |
| `esp_hmac_calculate` | 常時失敗(-1) | 同上．ハードウェアHMAC鍵は使わない前提 |
| `esp_read_mac` | `EFUSE_RD_MAC_SPI_SYS_{0,1}_REG`を直読み | typeパラメータ（STA/AP/BT/ETH）無視で同一MACを返す。mac_addr.c本体（esp_efuse依存）は不採用 |
| `esp_wifi_power_domain_on`/`off` | ～～2026-07-03時点．**以降はesp_phy/src/phy_init.c本体（`esp_wifi_bt_power_domain_on`/`off`のエイリアス）に置き換え．下記「PHY本実装」節参照** | （履歴．旧blobglue簡易移植は削除済み） |
| `esp_phy_enable`/`esp_phy_disable` | ～～2026-07-03時点．**以降はesp_phy/src/phy_init.c本体（`register_chipv7_phy()`によるフル較正込み）に置き換え．下記「PHY本実装」節参照** | （履歴．旧「主要な妥協点」＝キャリブレーション未実装は解消．ただし新たな課題が判明＝下記節参照） |
| `esp_phy_modem_init`/`esp_phy_modem_deinit` | ～～2026-07-03時点．**以降はesp_phy/src/phy_init.c本体に置き換え** | （履歴．heap_caps_malloc実体化＝下記節参照） |
| `esp_phy_update_country_info` | ～～2026-07-03時点．**以降はesp_phy/src/phy_init.c本体に置き換え（`CONFIG_ESP_PHY_MULTIPLE_INIT_DATA_BIN`未定義のため常時成功のみ＝実質同じ振る舞い）** | （履歴） |
| `coex_condition_set` | no-op | libcoexist.aに対応する実体が無い（nm確認済み＝このesp-hal-3rdpartyスナップショットでは未実装／将来予約フィールドと判断）。BLE非統合のため実質未使用想定 |

#### 追加したesp-halソース一覧（esp_wifi.cmake §6）

- `hal/components/esp_hw_support/periph_ctrl.c`（実ソース採用．クロック
  ゲート管理．依存が軽い＝`esp_private/critical_section.h`は
  `platform/os.h`スタブにマクロ追加するだけで閉じ，`hal/clk_gate_ll.h`は
  header-only）
- `hal/components/esp_phy/src/lib_printf.c`（`esp_wifi/src/lib_printf.c`
  とは別ファイル．`phy_printf`/`rtc_printf`を提供）

不採用（簡易シムで代替した理由）：

- `hal/components/esp_hw_support/mac_addr.c`（`esp_read_mac`本体）：
  `esp_efuse`本体（`esp_efuse_api.c`等）・`esp_efuse_table`に依存が及ぶ
  ため不採用．レジスタ直読みで代替。
- `hal/components/esp_phy/src/phy_init.c`（`esp_phy_enable`等本体）：
  `nvs_flash`・`esp_partition`・`esp_timer`本体・`esp_private/
  sleep_modem.h`・`esp_private/esp_modem_clock.h`等，チップ全体の
  ブート基盤（NuttXのhal_esp32c3.cmakeでは常時コンパイル対象だが，
  ASP3側は独自のチップ起動＝target_kernel_impl.c等を既に持つため
  重複・競合のリスクが大きい）に依存が及ぶため不採用．クロックゲート
  部分のみ`periph_ctrl.c`経由で正しく実施し，RFキャリブレーション部分
  は上表の妥協点として明記。

#### asp.elfサイズ・メモリ使用量（`--print-memory-usage`．2026-07-03）

```
Memory region         Used Size  Region Size  %age Used
            IROM:      361840 B         4 MB      8.63%
            DROM:      424400 B         4 MB     10.12%
             RAM:      201764 B       320 KB     61.57%
```

`riscv64-unknown-elf-size`：`text=424400  data=5032  bss=196724  dec=626156`
（`asp.elf`＝約2.83MB，`bss`のうちheap_area静的配列が112KB
（`ESP_SHIM_HEAP_SIZE`）＋shimタスクスタック6本×8KBが主要因）。

#### 非Wi-Fiビルドへの影響

`build/esp32c3`（ESP32C3_WIFI=OFF）・`build/esp32c3-qemu`・
`build/tp-qemu`・`build/wifi`（ESP32C3_WIFI=ON・アプリはWi-Fi API未使用）
とも本変更後にビルドし直し，エラー0件を確認（`esp32c3.ld`の追加ルールは
既存の`.text*`/`.rodata.*`/`.data*`パターンへの追記のみで，既存セクション
の扱いは変えていない）。

### PHY本実装（register_chipv7_phy．2026-07-03）

#### 課題

前節の「主要な妥協点」どおり，従来の`esp_phy_enable`（esp_shim_blobglue.c
簡易版）はクロックゲートのみでlibphy.a内部の関数テーブル初期化
（`register_chipv7_phy()`）を一切呼んでおらず，実機で
`esp_wifi_start()`→`esp_wifi_scan_start()`実行時にPHYの関数テーブル
（`set_chanfreq`等）がNULLのまま呼び出されクラッシュしていた。

#### 実施内容

- **esp_phy/src/phy_init.c・phy_common.c・esp32c3/phy_init_data.c**
  （`esp_wifi.cmake` §1c）を採用し，`esp_phy_enable`/`esp_phy_disable`/
  `esp_phy_modem_init`/`esp_phy_modem_deinit`/`esp_phy_update_country_info`/
  `esp_wifi_bt_power_domain_on`/`off`の**簡易版をesp_shim_blobglue.cから
  削除**し，本体へ完全移行した。
- **較正モード**：本ビルドのsdkconfig.h（`hal/nuttx/esp32c3/include/
  sdkconfig.h`）は`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`を定義
  しないため，`register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL)`
  を無条件に呼ぶ経路のみが実行される＝**毎回フル較正・NVS永続化なし**。
  NVS関連関数（`esp_phy_load_cal_data_from_nvs`等）はコンパイルはされる
  が到達しない．`nvs.h`/`nvs_flash.h`は`hal_stub/include`に新設した
  スタブ（`nvs_open`は常に`ESP_ERR_NVS_NOT_INITIALIZED`を返す）で解決。
- **CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1**（意図的な妥協点）：実機PLL
  温度ドリフト追従（`phy_track_pll`系）はesp_timer周期タイマ
  （`esp_timer_create`/`esp_timer_start_periodic`．Phase B-2 shim未対象）
  を要するため無効化。`hal_stub/include/esp_timer.h`は新設スタブ
  （`esp_timer_get_time()`のみ実装＝`esp_shim_time_us()`委譲，周期タイマ
  APIは宣言のみ＝到達不能コード）。
- 新設スタブ：`driver/gpio.h`（`phy_common.c`のアンテナ切替API＝到達
  不能コード用の最小型定義），`nvs.h`/`nvs_flash.h`。
- 新規実装（`esp_shim_blobglue.c`）：`esp_efuse_mac_get_default`
  （既存`esp_read_mac`へ委譲），`esp_deep_sleep_register_phy_hook`
  （no-op＝ASP3はディープスリープ経路を持たない），
  `_esp_error_check_failed`（`ESP_ERROR_CHECK`マクロの実体．
  syslog+`abort()`）。
- 新規実装（`esp_shim_libc.c`）：`heap_caps_malloc`（`esp_phy_modem_init`
  が要求．`esp_shim_malloc`へ委譲），`esp_timer_get_time`，newlib
  retargetable locking一式（`_lock_init`/`_lock_acquire`/`_lock_release`
  等．`esp_shim_mutex_*`＝タスクブロッキング可能な本物のミューテックスへ
  委譲．PHY較正は数百ms要しうるため`loc_cpu`ベースでなくブロッキング
  ミューテックスが必要）。
- `hal_stub/include/platform/os.h`：`OS_IN_ISR()`追加（`sns_ctx()`委譲）。
  また`<kernel.h>`（`assert()`をTOPPERS版へ再定義する）取り込み前後で
  `assert`マクロを`#pragma push_macro`/`pop_macro`により保存・復元する
  修正を追加（`t_syslog.h`を#includeしないTU＝`esp_phy/src/phy_init.c`
  等でTOPPERS_assert_failが実体を持たずリンクエラーになる問題の解消）。
- `esp_shim_cfg.h`：`ESP_SHIM_HEAP_SIZE`を112KB→192KB（実測で不足の
  兆候は見られなかったが，PHY本実装後の余裕を持たせるため）。
- `esp_wifi.cmake`：`TCNT_SYSLOG_BUFFER=128`（既定32．診断ログの起動
  時バーストで溢れていたため増量。`kernel/`/`syssvc/`本体は編集禁止
  のためコンパイル定義で上書き）。

#### 検証結果

- **コンパイル**：`cmake --build build/wifi_scan`でerror 0件。
- **非Wi-Fiビルドへの影響**：`build/esp32c3`・`build/esp32c3-qemu`・
  `build/tp-qemu`・`build/tp-hw`・`build/wifi`とも本変更後に再ビルドし
  エラー0件（既存ターゲット無影響）。
- **実機（ESP32-C3-DevKit・`/dev/ttyACM1`）**：フラッシュ書込み→リセット
  後，`wifi_scan: initializing shim`→`wifi_scan: esp_wifi_init`まで到達
  （従来クラッシュしていた`esp_wifi_scan_start()`のNULL call箇所は
  未到達＝той箇所のクラッシュは解消したと判断できる段階まで進行）。
  ただし**新たなハング**を発見：`esp_wifi_start()`内部（`wifi_start_
  process`→`wifi_hw_start`→`hal_init`）で，レジスタ`0x60033D14`
  （WIFI_MAC内部・非公開レジスタ，`hal/`ヘッダに定義なし）のbit0を
  無限ポーリングし続けたまま先へ進まない（OpenOCD＋GDB実機アタッチで
  バックトレース確認済み．`riscv32-esp-elf-gdb`＋
  `openocd-esp32/v0.12.0-esp32-20250422`の`esp32c3-builtin.cfg`使用）。
  150秒待っても進行しないため恒久ハングと判断。
  - 原因調査：`_wifi_clock_enable`/`_wifi_reset_mac`等のosi_funcs配線は
    NuttXリファレンス（`ref-esp32c3/nuttx/.../esp_wifi_adapter.c`）と
    完全一致（`wifi_module_enable()`/`periph_module_reset()`呼び出しは
    従来から実装済み・今回変更なし）。クロックゲート自体
    （`phy_module_has_clock_bits`アサーション）は通過している。
    切り分け未完了＝**PHY本実装（`register_chipv7_phy`によるフル較正）
    自体が原因かは未確定**（クロックゲート経路は新旧で同一のため，
    較正の有無がなぜMAC側レジスタに影響するかは要調査）。
  - 副次的に確認した既知の別課題（今回の変更が原因ではない可能性が
    高い．参考記録）：(1) `esp_shim: queue len 200 > pool depth 64`
    ＝wifi driver taskが要求するキュー長がshimの`ESP_SHIM_DTQ_CNT`
    上限を超過。(2) `net80211_printf`経由の`%s`ログ（`"config nano
    formatting: %s"`）が文字化けする＝`esp_shim_libc.c`の`vsnprintf`
    自体は目視レビューで問題なし，blob側が渡す文字列ポインタが
    不正な可能性がある．いずれも未解決＝follow-up課題。
  - **次のアクション（未着手）**：`hal_init`の呼び出し元
    （`wifi_hw_start`．blob内部のためソース不可視）が期待する
    追加のクロック/リセット手順（例：MAC専用リセットパルス，
    タイミング要件）の特定。実機JTAGでのステップ実行や，較正を
    一時的にスキップした場合との比較（差分切り分け）が有効と思われる。

#### 実施結果サマリ（到達段階）

`init`（esp_wifi_init成功）は到達．`start`（`esp_wifi_start()`内部で
恒久ハング）は未到達．`scan`/`done`は未到達。**タスクで要求された
「set_chanfreqのNULL callクラッシュ」自体は，そのクラッシュ箇所へ到達
する前の段階（wifi_hw_start）で新たなブロッカーに当たったため直接の
再現・解消確認はできていないが，PHY本実装（register_chipv7_phy呼出し）
は正しく組み込まれ，クラッシュ原因だった「関数テーブル未初期化」状態
は解消されている（libphy.aの内部初期化コードパスへ到達している事実
から判断）。**

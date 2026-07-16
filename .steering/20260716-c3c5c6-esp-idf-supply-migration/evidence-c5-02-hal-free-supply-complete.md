# C5 evidence-02 — **HAL依存ゼロ達成**（供給を全面 esp-idf v5.5.4 へ）＋ W1/scan 実機非回帰

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration` ／ commit: `cbe94d8`
DUT: **ESP32-C5 #2**（BASE MAC `d0:cf:13:f0:c8:94`, hub **port5**, `ttyACM5`）
toolchain: Espressif `riscv32-esp-elf` esp-15.2.0

---

## 0. 結論

**完了条件（`ninja -t deps` に esp-hal-3rdparty 参照ゼロ）を達成**し，
**scan（20 AP）・W1（GOT IP + ping×30）とも実機で非回帰を確認**した。

| ビルド | hal 参照 | esp-idf 参照 | 外部`~/tools/esp-idf` | build |
|---|---|---|---|---|
| wifi_scan（前＝hal供給） | **7357** | 0 | 0 | rc=0 |
| wifi_scan（後＝esp-idf供給） | **0** | 6849 | 0 | rc=0 |
| wifi_dhcp/W1（前） | **7370** | 0 | 0 | rc=0 |
| wifi_dhcp/W1（後） | **0** | 6863 | 0 | rc=0 |
| 非回帰 `-DASP3_ESPIDF_SUPPLY=OFF -DASP3_WIFI_BLOB_HAL=ON` | 7357 | 0 | 0 | **rc=0**（可逆） |

> 計測は **`ninja -t deps`**（`find -name '*.d'` は CMake+Ninja の `deps=gcc`
> により誤測する＝evidence-01 §4 の注意を踏襲）。

---

## 1. 供給元の構造（実測）— 「hal は ESP-IDF の再パッケージ」

本ターゲットが参照する **hal ルートの 75 パス**を機械的に esp-idf v5.5.4 へ
写像した結果，**72 パスが 1:1 で対応**した。対応しなかったのは 3 つだけで，
いずれも mbedtls/NuttX 固有：

```
components/mbedtls/port/psa_driver/include   ← tf-psa(4.0.0)専用．3.6.5に無い
nuttx/include/mbedtls                        ← NuttXシムconfig．esp-idfに無い
nuttx/src/esp_event.c                        ← 参考コメント内の記載のみ（未使用）
```

### 1.1 差分は本質的に2点だけ

| # | 差 | 吸収方法 |
|---|---|---|
| 1 | hal は IDF の単体 `hal` コンポーネントを **`esp_hal_*` 8個へ分割**（clock/timg/rtc_timer/pmu/gpio/security/ana_conv/usb）。**esp-idf にこの8個は1つも存在しない** | `ESP_SUP_HAL_<x>` 変数で吸収（esp-idf では全て `components/hal` を指す） |
| 2 | mbedtls **4.0.0（tf-psa-crypto分離）→ 3.6.5（classic）** の**版ダウン** | §3 |

### 1.2 ★esp-hal-3rdparty のリネームは「揃えて移せば消滅する」

hal と esp-idf の **同一ソース**（`esp_hw_support/port/esp32c5/rtc_time.c`）を
diff した実測：中身は同一で，**include するヘッダ名だけがリネームされている**。

```
hal:      #include "hal/rtc_timer_hal.h"   #include "hal/timg_ll.h"
esp-idf:  #include "hal/lp_timer_hal.h"    #include "hal/timer_ll.h"
```
（他に `hal/clkout_channel.h`(hal) ≡ `soc/clkout_channel.h`(esp-idf)）

⇒ **ヘッダだけ移してソースを hal に残すとリネームが未解決参照として噴出する**が，
**ソースも揃えて移せば問題自体が消滅する**。これが前ラウンドの段階2a（ヘッダのみ
移行）が行き詰まった構造的な理由。**「ヘッダとソースは必ず同じ供給元から揃えて取る」**。

---

## 2. 段階2a「残り壁」3件の決着 — **2件は引き継ぎ記述が誤り**

| # | 引き継ぎの記述 | 実測による決着 |
|---|---|---|
| (3) | `wifi_nan_sync_config_t`→`wifi_nan_config_t` 改名が壁 | **消滅**。ヘッダのみ移行が原因。`wifi_init.c` も esp-idf 供給に揃えれば両側一致（§1.2の一般則の一例） |
| (4) | `adc2_cal_include()` は C5 に実装が無く**空スタブが要る** | **誤り・対処不要**。`esp_private/adc_share_hw_ctrl.h:149-163` が `#if CONFIG_IDF_TARGET_ESP32S2 \|\| CONFIG_IDF_TARGET_ESP32C3` の時のみ関数宣言し，**それ以外（C5含む）は `#define adc2_cal_include()` ＝空マクロ**に展開。C5 に `adc2_init_cal.c` が無いのは**正常**（存在するのは S2/C3 のみ＝設計どおり） |
| (5) | `esp_netif.h` の include（呼出し0件） | そのとおり。`esp_netif/include` をパス追加のみで解決 |

> 教訓の再確認：evidence-01 が「コーディネータの引き継ぎ『中身同一』は誤りだった」と
> 記録したのと**同じ型の誤り**が，段階2aの引き継ぎメモ自身にも 2/3 含まれていた。
> **引き継がれた「壁」も、設計の土台にするなら自分で実測して裏を取ること。**

---

## 3. mbedtls **4.0.0 → 3.6.5**（最難関）

### 3.1 版の実測（引き継ぎを鵜呑みにせず自分で確認）

```
esp-idf submodule : git describe = v5.5.4（735507283d）＝**タグそのもの**
  mbedtls         : MBEDTLS_VERSION_STRING "3.6.5" / tf-psa-crypto ディレクトリ **無し**
                    library/*.c = 108本（classic＝暗号もTLSも一括）
hal (esp-hal-3rdparty)
  mbedtls         : git describe = mbedtls-4.0.0-22-g582ff4820 / "4.0.0"
                    tf-psa-crypto **有り** ／ library/*.c = 33本（TLS層のみ）
```

### 3.2 ソースの写像

tf-psa `builtin/src` 44 + `core` 8 → 3.6.5 `library/` へ 1:1 写像。実測差分：

- **4.0.0 のみ（除外）**：`pk_rsa.c`／`tf_psa_crypto_config.c`／`tf_psa_crypto_version.c`
  （version は 3.6.5 の `version.c` で代替＝`CONFIG_MBEDTLS_VERSION_C=1` が要求）
- **`pk_ecc.c` は 3.6.5 にも実在**＝そのまま採用
- **3.6.5 で追加が要るもの**：`psa_crypto_aead.c`・`entropy_poll.c`・
  `bignum_mod.c`・`bignum_mod_raw.c`
- 残り 41 本は同名で 3.6.5 に実在

### 3.3 config 配線

- `MBEDTLS_CONFIG_FILE=<mbedtls/esp_config.h>`（**ESP-IDF本家 port**）へ寄せ，
  hal の NuttX シム config（`hal/nuttx/include/mbedtls`）と
  `TF_PSA_CRYPTO_USER_CONFIG_FILE` を**廃止**。
- **ソフト暗号のまま**（PROMPT.md の指示どおり）：`sdkconfig_stub` に
  `CONFIG_MBEDTLS_HARDWARE_*` が無い ⇒ `esp_config.h` が `*_ALT` を立てない
  ⇒ builtin C 実装。
- `CONFIG_MBEDTLS_ROM_MD5=1` が `MBEDTLS_MD5_ALT` を立てる（`esp_config.h:188`）
  ため `port/md/esp_md.c` を追加（**本家 `components/mbedtls/CMakeLists.txt:337-339`
  と同一条件**）。
- **`-Wl,-u,mbedtls_psa_crypto_init_include_impl` は付けない**：当該シンボルを供給する
  `port/esp_psa_crypto_init.c` は **esp-hal-3rdparty 独自**（NuttX向け）で
  **esp-idf に存在しない**（実測）。

### 3.4 ★`common.h` shadow（**版ダウン固有の新規リスク**）

- 3.6.5 の `library/common.h` は wpa の `src/utils/common.h` と**同名**。
  **4.0.0 には `library/common.h` が存在しない**ため hal 構成ではこの衝突は
  **発生し得なかった**＝移行で初めて生じるリスク（S3段階3の既出の罠と同一）。
- mbedtls が公開する全ヘッダ名（`port/include`・`mbedtls/include`・`library`）×
  wpa の `src/utils`・`src/crypto` で照合した実測の重なりは **`common.h` ただ1つ**。
- **正しい解決先は wpa 側**：本家 esp-idf は wpa を
  `PRIV_INCLUDE_DIRS src src/utils …` ＋ `PRIV_REQUIRES mbedtls` で登録し
  （`wpa_supplicant/CMakeLists.txt:246-250`），コンポーネント自身の include が
  requirements より**前**に来る。実際 `crypto_mbedtls.c` は冒頭で既に
  `utils/common.h` を include 済みで，後続の bare `#include "common.h"` は
  同一ファイルの再include（ガードで無害）。
- ⇒ **`mbedtls/library` を wpa の後ろに置く**ことで解決。
  `library` 自体は wpa の `crypto_mbedtls.c`(`common.h`) と
  `tls_mbedtls.c`(`ssl_misc.h`) が mbedtls 内部ヘッダを直接 include するため必要
  （本家も mbedtls コンポーネントの公開 include に `mbedtls/library` を含む＝
  `CMakeLists.txt:30`）。

---

## 4. ABI skew の解消（本移行の主目的）

blob は自身がビルドされたヘッダの md5 先頭7桁を埋め込む（evidence-01 §1.3 の手法）。
**hal ヘッダ × v5.5.4タグ blob の混成では 3/5 が不一致**だったが，esp-idf 供給で **5/5 一致**：

| ヘッダ | hal | **esp-idf（現行）** | v5.5.4タグ blob の要求 | 判定 |
|---|---|---|---|---|
| `wifi_os_adapter.h` | 6eaa5ad | **6eaa5ad** | 6eaa5ad | 元から一致 |
| `esp_wifi.h` | 9f7e672 | **a78adff** | a78adff | **解消** |
| `esp_wifi_types_generic.h` | 6773bf5 | **dae1625** | dae1625 | **解消** |
| `esp_wifi_types_native.h` | ce069d3 | **ce069d3** | ce069d3 | 元から一致 |
| `esp_wifi_driver.h` | 50fc486 | **2331a76** | 2331a76 | **解消** |

### 4.1 osi ABI（0x102 回帰が無いことの静的証明）

```
$ riscv32-esp-elf-nm -S build/idf-scan/asp.elf | grep g_wifi_osi_funcs
40800004 000001e8 D g_wifi_osi_funcs          ← size 0x1e8 = 488

$ objdump -s -j .data (g_wifi_osi_funcs+480 .. +492)
 408001e4 4e8b0242 afbeadde 02000000
          ^offset480 ^offset484 = afbeadde = 0xdeadbeaf（LE）
```
⇒ `_magic` は **offset 484** ＝ v5.5.4タグ blob が読む位置と一致
（evidence-01 §1.2）。`ASP3_WIFI_OSI_HAS_DISABLE_AC_AX` は既定 OFF のまま。

---

## 5. 実機結果（生ログ．**SSIDはマスク**）

手続き：`read-mac` で `d0:cf:13:f0:c8:94` 照合 → `--after no-reset` で
`write-flash 0x0`（Hash of data verified）→ **真の cold**
（`off 5` → **読み戻しで `C5 device count = 0` / `Vbus 0.01V` / `no device` を確認** → `on 5`）
→ `rts_boot_capture.py`（**single-reset**）。

### 5.1 W1 — `wifi_dhcp`（esp-idf供給・cold・**5GHz ch44**）

```
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 (Jul 16 2026, 19:11:08)
wifi_dhcp: esp_wifi_init
wifi_dhcp: esp_wifi_connect -> 0
event: STA_CONNECTED
net: link up, starting DHCP
wifi_dhcp: AP info: channel=44 rssi=-75
net: DHCP bound ip=192.168.100.21 gw=192.168.100.1
wifi_dhcp: IP acquired: 192.168.100.21
wifi_dhcp: done (ping result logged by net_task)
net: ping gateway -> OK      ← ★30回連続 OK（NG 0件）
```
異常マーカー（Illegal instruction / Guru / TG0_WDT / panic / access fault）＝**0件**。

> ★**mbedtls 3.6.5 が実際に走行経路で使われている証拠**：WPA2 の 4-way handshake
> （PTK/MIC 導出＝`crypto_mbedtls*.c` 経由）を通らないと `STA_CONNECTED` →
> `DHCP bound` に到達しない。＝**リンクが通っただけではなく暗号が機能している**
> （S3 段階3 evidence-01 と同じ判定基準）。

### 5.2 一里塚1 — `wifi_scan`（esp-idf供給）

```
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
wifi_scan: esp_wifi_init
I (47) pp: pp rom version: 78a72e9d5
wifi_scan: DIAG set_promiscuous(true) -> 0
wii_scan: esp_wifi_scan_start -> 0
wifi_scan: 20 APs found (err=0)
  [0] <SSID-EDU> (rssi=-44 ch=6)
  [3] <SSID-INST-1X> (rssi=-46 ch=6)
  [10] <SSID-INST-1X> (5GHz) (rssi=-51 ch=116)
  [11] <SSID-EDU> (rssi=-51 ch=116)
wifi_scan: done
```
**20 AP**（2.4GHz ch6 ＋ 5GHz ch116 の両バンド）＝hal ベースラインと同数。異常マーカー **0件**。

### 5.3 メモリ（参考）

| ビルド | FLASH | RAM |
|---|---|---|
| wifi_scan hal供給 | 480,784 B (11.46%) | 299,016 B (76.04%) |
| wifi_scan **esp-idf供給** | **464,432 B (11.07%)** | **299,816 B (76.25%)** |
| W1 esp-idf供給 | 524,784 B (12.51%) | 319,640 B (81.29%) |

FLASH が約 16KB 減るのは mbedtls 4.0.0→3.6.5 の版差（tf-psa の PSA レイヤ縮小）。

---

## 6. 実装（可逆性）

- 新option **`ASP3_ESPIDF_SUPPLY`（既定 ON＝HAL-free）**。`OFF` で従来の hal 供給へ
  完全に戻る（実測：build rc=0／hal 7357／esp-idf 0）。
- `ESP_SUP_DIR`（供給ルート）と `ESP_SUP_HAL_<x>`（hal 分割コンポーネントの写像）で
  `target.cmake`／`esp_wifi_v8.cmake` の全パスを供給元非依存化。
- 版差の吸収は **`TOPPERS_ESPIDF_SUPPLY`** ガード（S3 と同名・同役割）：
  `esp_event_post()` の `event_data` が hal=`void *` / esp-idf v5.5.4=`const void *`。
  esp-idf 供給では `esp_private/wifi.h` が `esp_event.h` を引くため，C3/C5/C6 共通
  パターンの局所 extern が `conflicting types` になる。C5 のみガードで本物の宣言を
  使い，共有の `esp_event_shim.c` の定義も揃えた。**C3/C6 は無改変**（ガード未定義）。
- 撤去：WIP option `ASP3_WIFI_INC_IDF`（本移行に吸収され不要）。

## 7. 副次効果：**外部レビュー ★D5 が解消**（実測で確認）

引き継ぎ仮説「wpa を esp-idf へ移せば `esp_wifi_sta_get_ie` の参照ごと消え，
`esp_shim_blobglue.c` の no-op stub が不要になり ★D5（`esp_wifi_sta_get_ie` の
no-op 化＝**RSN IE 検証の恒常無効化**）も解消する」を**実測で確認した**：

| 測定 | hal 供給 | **esp-idf 供給** |
|---|---|---|
| `esp_wifi_sta_get_ie` を参照する wpa ソース | **3 ファイル** | **0 ファイル** |
| 最終 elf 内の `esp_wifi_sta_get_ie` シンボル | **有り（1）** ＝no-op stub がリンクされ**実際に使われている** | **無し（0）** ＝参照ごと消滅・stub は gc される |
| 同 `esp_wifi_is_wpa3_compatible_mode_enabled` | 無し（0．元々未参照） | 無し（0） |

⇒ **`esp_wifi_sta_get_ie` は hal(esp-hal-3rdparty) の wpa_supplicant だけが呼ぶ
hal 独自API**であり，esp-idf v5.5.4 の wpa_supplicant はそもそも呼ばない。
esp-idf 供給では **RSN IE 検証を無効化する no-op stub が走行経路から消滅**した
＝`docs/blob-unify-v554-review.md` ★D5 は本移行で**解消**（既定 ON の構成において）。

> 注：`esp_shim_blobglue.c` の stub 定義自体は**残置**する（`ASP3_ESPIDF_SUPPLY=OFF`
> ＝hal fallback で今も必要なため）。esp-idf 供給時は未参照＝`--gc-sections` で脱落する
> （上表の elf 実測が根拠）。**hal fallback を使う限り ★D5 は残る**点は変わらない。

---

## 8. ブート方式の判断 — **本ラウンドでは «決定しない»（判断材料のみ記録）**

PROMPT.md §「ブート方式は『あなたが決める』」に対する回答。

### 8.1 本ラウンドで実測により**確定した**こと

1. **供給移行とブート方式は直交**する。C5 は **Direct Boot のまま**，
   供給を全面 esp-idf 化して **scan・W1 の両里塚を実機達成**した（§5）。
   ＝「HAL依存撤去のためにブート方式を変える必要は無い」ことが実証された。
2. 実機の生ログは `boot:0x18 (SPI_FAST_FLASH_BOOT)`＝ROM が flash を
   セルフマップして ASP3 のエントリへ直接ジャンプしている（2nd-stage 無し）。
3. ASP3 の Direct Boot は **esptool 標準 image header を使わない独自規約**
   （`flash_header.S`）。C5 でも C3/C6 と同じマジックで動作している。

### 8.2 **決定しない**理由（＝根拠が無いまま決めない）

Xtensa 側は最終的に seam 一本化＋Direct Boot 退役という判断をしたが，
その根拠（実 bootloader 経由の flash cache/MMU 挙動・`esp_app_desc` を
segment#0 に置く必要・efuse blk rev チェック通過・image size 依存の
flash オフセットに2パスリンクで対応）は **いずれも C5 実機で未検証**である。
PROMPT.md 自身が「C系 bootloader の flash cache/MMU 挙動を**実機で見極めた上で**
判断せよ」と条件付けており，**本ラウンドではその実験を一つも行っていない**。

⇒ 相関（Xtensaがseamを選んだ）から因果（C5もseamが妥当）を早合点しないため，
**判断はコーディネータへ差し戻す**。現時点の既定は **Direct Boot 継続**
（＝両里塚を実機達成済みの，唯一エビデンスのある構成）。

### 8.3 判断に必要な次の実験（提案）

1. esp-idf v5.5.4 の C5 2nd-stage bootloader（`components/bootloader`，
   C5 ポート実在を確認済み）をビルドし，`bootloader@0x0 / ptable@0x8000 /
   app@0x10000` レイアウトで ASP3 を起動できるか（seam）を実機で試す。
2. その際 `esp_app_desc` / efuse blk rev チェックを通す必要が実際に生じるか
   （C系は「esp32 classic」ではないため PROMPT.md は生じる側に賭けている＝**要実測**）。
3. Direct Boot で現に動いている以上，seam 化の**便益**（実運用フローとの一致・
   OTA/partition 連携）と**コスト**（2パスリンク等）を C5 の要件で秤にかける。
</content>
</invoke>

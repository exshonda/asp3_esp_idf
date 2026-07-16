# C5 evidence-01 — `esp_wifi_init 0x102` 真因確定 → 一里塚1(scan) / W1(GOT IP+ping) 実機達成

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #2**（BASE MAC `d0:cf:13:f0:c8:94`, rev v1.0/eco2, hub **port5**）
toolchain: Espressif `riscv32-esp-elf` esp-15.2.0（`memory/c5-canonical-compiler.md`：xpack15が正典，esp-15.2も実績あり）

---

## 1. 真因（file:line・すべて実測）

**結論＝`idf_v554_override/esp_private/wifi_os_adapter.h`（＋1169版のコピー）そのものが原因。**
osiテーブルのABIずれでblobが登録を拒否していた。

### 1.1 blob側の検査は md5 ではなく「オフセット直読み」

`esp-idf/components/esp_wifi/lib/esp32c5/libnet80211.a(ieee80211_api.o)`
`wifi_osi_funcs_register`（逆アセンブル実測）:

```
00000000 <wifi_osi_funcs_register>:
   0:  lui   a5,0x0            ; g_osi_funcs_p
   4:  lw    a4,0(a5)
   8:  bnez  a4,68             ; 既に登録済みなら return 0
   e:  bnez  a0,2a             ; osi_funcs==NULL -> puts() -> return 258
  20:  li    a0,258            ; ★258 = 0x102 = ESP_ERR_INVALID_ARG
.L354:
  2a:  lw    a2,0(a0)          ; _version  (offset 0)
  2c:  li    a4,8              ; ==8 でなければ net80211_printf -> return 258
.L355:
  46:  lw    a2,484(a0)        ; ★_magic を **offset 484** で直読み
  4a:  lui   a1,0xdeadc
  4e:  addi  a1,a1,-337        ; 0xdeadbeaf
  52:  beq   a2,a1,60          ; 不一致 -> net80211_printf -> return 258
.L356:
  60:  sw    a0,0(a5)          ; g_osi_funcs_p = osi_funcs
  64:  li    a0,0              ; return ESP_OK
```

`esp_wifi_init`（`hal/components/esp_wifi/src/wifi_init.c`）は
`result = esp_wifi_init_internal(config)` の戻り値をそのまま返す
＝ **0x102 の出どころは osi 登録検査**。

> 補足：`esp_wifi_internal_osi_funcs_md5_check()` 等の md5 系APIは
> **どこからも呼ばれない**（実測：esp-idf/hal 全ツリーで呼出し0件。
> `hal/components/esp_wifi/test_md5/test_md5.sh` 専用）。
> ＝**md5は実行時チェックではない**。ただし後述のとおり「blobがどのヘッダで
> ビルドされたか」を知る**測定手段**として極めて有用。

### 1.2 blobが期待する `_magic` オフセット（実測）

| blob | `_magic` 読み出しオフセット |
|---|---|
| **esp-idf v5.5.4タグ（submodule）** | **484** |
| `~/tools/esp-idf`（＝v5.5.4-**1169**-gbb2188bf） | **488** |
| hal（esp-hal-3rdparty） | **484** |

（`_version` は3者とも offset 0 / `==8` 要求で同一）

### 1.3 blob埋込みmd5 vs ヘッダ実md5（＝どの版のヘッダを要求しているか）

blobは自身がビルドされたヘッダのmd5先頭7桁を `g_wifi_osi_funcs_md5` 等に埋込む。
リンク＋シンボル読み出しで実測：

| ヘッダ | blob要求(v5.5.4タグ) | blob要求(+1169) | blob要求(hal) | 実ファイルmd5: hal / v5.5.4タグ / +1169 |
|---|---|---|---|---|
| `wifi_os_adapter.h` | `6eaa5ad` | `8651e5d` | `6eaa5ad` | **6eaa5ad / 6eaa5ad / 8651e5d** |
| `esp_wifi.h` | `a78adff` | `b552543` | `9f7e672` | 9f7e672 / a78adff / b552543 |
| `esp_wifi_types_generic.h` | `dae1625` | `317a246` | `6773bf5` | 6773bf5 / dae1625 / 317a246 |
| `esp_wifi_types_native.h` | `ce069d3` | `ea82efe` | `ce069d3` | ce069d3 / ce069d3 / ea82efe |
| `esp_wifi_driver.h`(supplicant) | `2331a76` | `427c683` | `5845330` | 50fc486 / 2331a76 / 427c683 |

⇒ **各blobは自分のツリーのヘッダと完全一致**（測定手法の妥当性検証＝self-consistent）。
⇒ **`wifi_os_adapter.h` は hal と v5.5.4タグで «バイト同一»（6eaa5ad）**。

### 1.4 ABI差の実体＝1フィールド

`diff esp-idf(tag) vs ~/tools/esp-idf(+1169) wifi_os_adapter.h`:

```c
     void * (*_coex_schm_get_phase_by_idx)(int);
+#if CONFIG_SOC_WIFI_HE_SUPPORT
+    bool (*_wifi_disable_ac_ax)(void);      /* ← +1169 のみ。_magic の直前 */
+#endif
     int32_t _magic;
```

`CONFIG_SOC_WIFI_HE_SUPPORT=1`（C5）で +1169 版ヘッダを使うと
`_magic` が **484→488** へ4バイト後退する。

### 1.5 我々の構成（実測・修正前後）

| 構成 | `nm -S g_wifi_osi_funcs` | 0xdeadbeaf の位置 | v5.5.4タグblob(484)と |
|---|---|---|---|
| 修正前（override有＝+1169 ABI） | size `0x1ec` (492) | **488** | **不一致 → 0x102** |
| 修正後（override撤去） | size `0x1e8` (488) | **484** | **一致 → OK** |

### 1.6 引き継ぎ事項の訂正（重要）

引き継ぎ「override と submodule v5.5.4タグ版の diff はコメント28行のみ＝中身同一」は
**誤り**。コメントを除去して比較した実測：

```
override != IDF-tag
override == IDF+1169 (modulo comments)   ← ★
override != HAL
```

override は **`~/tools/esp-idf`（+1169）からの verbatim コピー**であった
（override 自身の冒頭コメントにも「`~/tools/esp-idf/...` からの verbatim コピー」と明記）。
＝**provenance の罠が二重に効いていた**。override の存在理由として書かれていた
「halのヘッダは`_wifi_disable_ac_ax`を欠く古い版」も、+1169との比較に基づく誤りで、
**halは v5.5.4タグと同一**（§1.3）。

**ruled out（非原因と実測で確定）**
- osi md5 不一致 … md5は実行時に検査されない（§1.1補足）
- `wifi_init_config_t` の版差 … `esp_wifi.h` の差分は **730行目以降**のみで、
  `wifi_init_config_t`(〜124行)・`WIFI_INIT_CONFIG_MAGIC`(189行)・
  `WIFI_INIT_CONFIG_DEFAULT`(334行) は hal/v5.5.4タグで**同一**
- `_version` … 3blobとも8要求／我々も8（実測）
- board/latch … §3 のとおり cold で正常動作

---

## 2. 修正

- `asp3/target/esp32c5_espidf/wifi_v8/idf_v554_override/` を**撤去**
  （halヘッダ＝v5.5.4タグと同一内容がそのまま正しい）。
- `_wifi_disable_ac_ax` の充填を新option **`ASP3_WIFI_OSI_HAS_DISABLE_AC_AX`（既定OFF）**へ。
  `-DIDF_V554=~/tools/esp-idf`（+1169 ABI）へ差し戻す時のみ ON。
- 可逆性（実測）：`-DASP3_WIFI_BLOB_HAL=ON` → build rc=0・hal blobを`-L`・
  osi size `0x1e8`（＝484のまま。ヘッダ同一のためABI不変）。

---

## 3. 実機結果（生ログ）

**手続き**：`esptool read-mac` で `d0:cf:13:f0:c8:94` 照合 → `--after no-reset` で
`write-flash 0x0`（フル4MB, Hash of data verified）→ **真のcold**
（`usbhub3c_ctl.py off 5` → **読み戻しでデバイス消滅を確認**（count 1→0, Vbus 0.02V,
"no device"）→ `on 5`）→ `rts_boot_capture.py`（**single-reset**）。

### 3.1 一里塚1 — `wifi_scan`（v5.5.4タグblob供給・cold）

```
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5
wifi_scan: esp_wifi_init          ← 修正前はここで "-> 258"
I (37) pp: pp rom version: 78a72e9d5
wifi driver task: 40803258, prio:23, stack:6656, core=0
wifi_scan: esp_wifi_start -> 0
wifi_scan: DIAG set_promiscuous(true) -> 0
wii_scan: esp_wifi_scan_start -> 0
esp_event: WIFI_EVENT id=1
wifi_scan: 20 APs found (err=0)
  [0] <SSID-INST> (rssi=-40 ch=11)
  [4] <SSID-EDU> (rssi=-41 ch=11)
  [10] <SSID-EDU> (rssi=-52 ch=116)
  [14] <SSID-INST-1X> (5GHz) (rssi=-53 ch=116)
  [19] <SSID-EDU> (rssi=-57 ch=100)
wifi_scan: done
```
（2.4GHz/5GHz 両方。アプリ側の表示上限は20件＝`wifi_scan.c:856 num = 20`）

### 3.2 W1 — `wifi_dhcp`（GOT IP + ping）

`-DESP32C5_LWIP=ON` が必要（既定OFF。無いと `netif_esp32c3.h` 未検出でビルド不可）。
認証情報は **cmake注入のみ**（`-DASP3_EXTRA_COMPILE_DEFS='WIFI_SSID=...;WIFI_PASSWORD=...'`）。
本ファイルにSSID/パスワードは記録しない。

```
net: DHCP bound ip=192.168.100.21 gw=192.168.100.1
wifi_dhcp: IP acquired: 192.168.100.21
wifi_dhcp: done (ping result logged by net_task)
net: ping gateway -> OK      （以降 30回以上連続 OK）
```

### 3.3 latch 再現（教訓の再確認）

W1 初回投入時、`rst:0x7 (TG0_WDT_HPSYS)` + `Core0 Saved PC:0x40038598` の
**無限リブートループ**を観測。**コードを疑わず先に電源再投入**
（off 5 → デバイス消滅を読み戻し確認 → on 5）→ **一発で §3.2 の正常動作**。
＝`memory/c5-latched-board-state` の再現・再確認（`0x40038598` は red-herring な ROM PC）。

---

## 4. 供給状況（`.d` 依存の実測）

**注意**：CMake+Ninja は `deps = gcc` で `.d` を `.ninja_deps` に取り込んで削除するため、
`find -name '*.d'` では測れない（1件しか出ない）。**`ninja -t deps` を使うこと**。

W1ビルド（`ninja -t deps`，全15465行）:

| 供給元 | 参照数 |
|---|---|
| `hal/`（esp-hal-3rdparty） | **7370** |
| `esp-idf/`（submodule） | **0** |
| `~/tools/esp-idf`（外部絶対パス） | **0** ← 撤去済 |

内訳（hal component別）:
```
2996 mbedtls      947 wpa_supplicant   770 log        744 soc
 613 esp_wifi     548 esp_common       195 esp_rom    173 hal
 122 esp_event    121 esp_hw_support    40 riscv       20 esp_hal_clock
  19 esp_hal_pmu   18 esp_phy           13 esp_hal_gpio  9 esp_system
   6 heap           4 efuse              2 esp_pm       2 esp_hal_rtc_timer
   2 esp_coex       1 esp_security       1 esp_hal_timg  1 esp_hal_security
   1 esp_hal_ana_conv                    2 hal/nuttx（mbedtls NuttXシムconfig）
```

⇒ **blob供給（`-L`）は esp-idf submodule へ移行済み**（3本：esp_wifi/esp_phy/esp_coex）。
**ヘッダ供給（`-I`）は全てhalのまま**＝ここが残作業の本体。

---

## 5. 段階2a（Wi-Fi経路ヘッダのesp-idf化）— 着手・**未完**

`ASP3_WIFI_INC_IDF`（**既定OFF＝ONでは現状ビルド不可**）として着地。潰した壁／残り壁：

| # | 壁 | 状態 |
|---|---|---|
| 1 | esp-idf `esp_wifi_types_generic.h` → `esp_interface.h` 欠落（**halに当該ファイル自体が無い**） | 済：`esp_hw_support/include` をAPPEND |
| 2 | esp-idf `esp_private/wifi.h` → `freertos/FreeRTOS.h` 欠落（hal版はOS非依存の`platform/os.h`＝NuttX向けにFreeRTOS依存を剥離）。**必要な型は`QueueHandle_t`1つだけ**、要求ヘッダも2本のみ | 済：既存BT用FreeRTOSスタブ（`esp32c3_espidf/bt/stub/include`，実体はesp_shimへ委譲。C5 esp_bt.cmakeが既に再利用＝チップ非依存）をAPPEND |
| 3 | hal版`wifi_init.c:716`が`wifi_nan_sync_config_t`を使用。esp-idf v5.5.4では`wifi_nan_config_t`へ**改名**（`CONFIG_ESP_WIFI_NAN_SYNC_ENABLE`→`CONFIG_ESP_WIFI_NAN_ENABLE`） | **残**：正攻法は`wifi_init.c`もesp-idf供給へ移すこと |
| 4 | esp-idf版`wifi_init.c:500`は`adc2_cal_include()`を**無条件**呼出（hal版は`#ifndef __NuttX__`で除外）。C5に`adc2_init_cal.c`が**無い**（実測：esp32c3/esp32s2のみ存在） | **残**：空スタブが要る |
| 5 | esp-idf版`wifi_init.c`は`esp_netif.h`をinclude。ただし**`esp_netif_*`の呼出しは0件**（実測） | **残**：ヘッダパス追加のみで足りる見込み |

**スコープ判断の根拠**：一里塚1/W1は**mbedtls/wpaに触れずに達成**できた（§3）＝
コーディネータの示唆どおり「mbedtls最難関を後回しにして先にscan/0x102を片付ける」順序が
**実測で正当化された**。段階2aはABI整合上の価値はあるが（§1.3で
`esp_wifi.h`/`esp_wifi_types_generic.h`/`esp_wifi_driver.h` が hal↔v5.5.4タグblobで
**不一致**＝潜在skewが残る）、一里塚達成には不要のため次ラウンドへ。

---

## 6. 次ラウンドの前提整備（済）

- `esp-idf/components/mbedtls/mbedtls` submodule を init（**未initだった**）。
  実測：**esp-idf v5.5.4 = mbedtls 3.6.5（classic、tf-psa-crypto分離なし）**／
  **hal = 4.0.0（tf-psa-crypto分離）** ＝版ダウン。S3雛形の段階3と同型。
- hal固有8コンポーネントは **esp-idf v5.5.4 に全て存在しない**（実測）：
  `esp_hal_clock` `esp_hal_timg` `esp_hal_rtc_timer` `esp_hal_pmu`
  `esp_hal_gpio` `esp_hal_security` `esp_hal_ana_conv` `esp_hal_usb`
  ⇒ esp-idf の `hal/`・`soc/`・`esp_hw_support/` 相当物へ読み替え or `asp3/target/` へshim。

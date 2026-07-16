# C5 evidence-05 — BT 供給の esp-idf submodule 移行（ビルド破綻の修復）

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #2**（BASE MAC `d0:cf:13:f0:c8:94`, hub **port5**, `ttyACM5` / `ttyUSB2`）
toolchain: Espressif `riscv32-esp-elf` esp-15.2.0

evidence-04 §6.2 が「pre-existing・供給移行の残作業」として記録した
`-DESP32C5_BT=ON` のビルド不能への回答。

---

## 1. 破綻の真因 — **コーディネータ診断を一部訂正**

### 1.1 追認した部分

`-DESP32C5_BT=ON -DASP3_APPLNAME=bt_smoke_c5` は configure rc=0 / build rc=1。
エラーは実測で再現（`scratchpad/build_before.log:65`）：

```
In file included from /home/honda/tools/esp-idf/components/bt/include/esp32c5/include/esp_bt.h:19:
hal/components/esp_hw_support/include/esp_private/esp_modem_clock.h:46:32:
  error: unknown type name 'shared_periph_module_t'; did you mean 'periph_module_t'?
```

「**外部 `~/tools/esp-idf` の `esp_bt.h` × hal の `esp_modem_clock.h` の混成**」
という構図の診断は**正しい**。`esp_bt.cmake:61` の `set(IDF /home/honda/tools/esp-idf)`
＝外部絶対パス、`esp_bt.cmake:190-195` の `${ESP_HAL_DIR}/components/esp_hw_support/include`
が混成を作っているという指摘も**正しい**。

### 1.2 ★訂正した部分（引き継ぎの実測による裏取り＝evidence-02 §2 と同じ型の誤り）

> 診断：「`shared_periph_module_t` は **hal 内でも `soc/esp32c61` と `soc/esp32s31`
> にしか定義が無く、`soc/esp32c5` には無い**（実測）」

**これは誤り。** `hal/components/soc/esp32c5/include/soc/periph_defs.h:35` に
**C5 の定義は実在する**（hal 内の 13 チップすべてが定義を持つ）。

∴ 真因は「hal に C5 用の型が無い」ではなく、**同名ヘッダ `soc/periph_defs.h` の
版差（リネーム）**である：

| 供給元 | `soc/periph_defs.h` | `esp_modem_clock.h` の宣言 |
|---|---|---|
| **hal**（新しい＝v6.x 相当の再パッケージ） | `shared_periph_module_t` を定義（`esp32c5/include/soc/periph_defs.h:35`） | `modem_clock_module_enable(shared_periph_module_t)` |
| **esp-idf v5.5.4**（submodule） | `periph_module_t` のみ（`shared_periph_module_t` **不在**） | `modem_clock_module_enable(periph_module_t)` |

**両者は内部で整合している。壊れているのは «混ぜたこと» そのもの。**
`target.cmake:118` が `${ESP_SUP_DIR}/components/soc/esp32c5/include`（＝esp-idf の
`periph_defs.h`）を**先に**積むため、hal の `esp_modem_clock.h` が要求する
`shared_periph_module_t` が解決できない。

⇒ これは PROMPT.md §5 が「版差の吸収パターン」として予告していた
**`periph_module` リネーム**そのもの。そして evidence-02 §1.2 の構造的知見
「**ヘッダとソースを揃えて移せばリネーム問題は消滅する**」が**そのまま適用できる**
（片方＝soc だけが esp-idf へ移り、esp_modem_clock.h が hal に残った状態＝
まさに「揃えずに移した」形）。

### 1.3 本ラウンドの回帰ではないことの確認

`ASP3_C5_PMU_INIT=OFF`（既定）でも同一エラー＝evidence-04 の記述どおり
**pmu_init ラウンドの回帰ではない**。WiFi 供給移行（evidence-02）で共通 include が
esp-idf 側へ動いた際、**BT パスだけが取り残された**ことに起因する。

---

## 2. ★provenance の実測 — **記録済みの結論を反証する重大な発見**

タスクが警告した「provenance の罠」を BT blob で実測した。**罠は実在し、
しかも `memory/c5c6-bt-blob-v554-feasibility.md` の中心的主張を反証する。**

```
esp-idf submodule      : git describe = v5.5.4 (735507283d)  ＝**タグそのもの**
~/tools/esp-idf (外部) : git describe = v5.5.4-1169-gbb2188bf ＝release/v5.5 の先端
```

### 2.1 BT blob md5 の突合（4 ライブラリ × 4 ツリー）

| lib | **submodule v5.5.4タグ** | **外部 +1169** | **hal** | **外部 v6.1** |
|---|---|---|---|---|
| `libble_app.a` | `015db3db` | `c2785c98` | **`015db3db`** | **`c2785c98`** |
| `libphy.a` | `51166fb6` | `4ccdbdbe` | **`51166fb6`** | **`4ccdbdbe`** |
| `libbtbb.a` | `211553eb` | `f553ddd3` | **`211553eb`** | **`f553ddd3`** |
| `libcoexist.a` | `c516e24e` | `8400ad43` | **`c516e24e`** | `53b3f950` |

### 2.2 ★この表が意味すること（2つとも記録の訂正）

**(a) 「v5.5.4 blob ＝ v6.1 blob とバイト完全一致」は誤り。**
memory `c5c6-bt-blob-v554-feasibility.md` の中心的発見
「libble_app.a・libphy.a・libbtbb.a は v5.5.4/v6.1 間でバイト完全一致」は、
**`~/tools/esp-idf`(+1169) を「v5.5.4」と誤認して測った結果**である。
実際は **+1169 ≡ v6.1**（3/4 が一致）であって、**真の v5.5.4 タグは 4/4 とも別物**。

**(b) 「BT 統一を v5.5.4 へ完了した」も誤り。実際は v6.1 blob のまま。**
`ASP3_BT_IDF_V554=ON`（既定）は `~/tools/esp-idf`＝+1169 を指すので、
現在の C5 BT が実際にリンクしているのは **v6.1 と同一の blob**。
∴ 「WiFi・BT 双方 v5.5.4＝blob 統一完了」という記録は**成立していない**
（WiFi は submodule＝真の v5.5.4 タグ、BT は +1169≡v6.1）。
**本ラウンドの移行によって初めて実際に統一される。**

**(c) 真の v5.5.4 タグ blob ＝ hal blob（4/4 バイト一致）。**
⇒ 本移行は「パスの付け替え」では**なく**、**BT の blob 世代が実際に変わる機能変更**
（+1169/v6.1 → v5.5.4タグ/hal）。実機再検証が必須。

---

## 3. ★予測（**実機を触る前に固定。測定後に改竄しない**）

§2.2(c) より、本移行は「hal と同一の blob へ切替える」ことと等価。
ここで記録済みの **実施09「hal(v8) の libphy は eco2 C5 の BT 経路で
`register_chipv7_phy` が収束せずハングする」** が正面から効く可能性がある
（それが v6.1 を採用した当初理由そのもの）。

### 3.1 予測：**ハングしない（BT は submodule blob で動く）**

理由（いずれも既存の実測に基づく。相関ではなく同一バイナリの同定）：

1. **`libphy.a = 51166fb6`（submodule）は、この eco2 C5 実機で既に cold 動作実証済み**。
   WiFi の供給は evidence-02 で submodule へ全面移行済＝**WiFi が現に使っている
   libphy.a はこの 51166fb6 そのもの**であり、cold で scan 20AP・W1（GOT IP＋ping 30/30）
   を達成している。`register_chipv7_phy` は libphy 内にあり、**WiFi 経路も BT 経路も
   同じ関数を通る**（esp_bt.cmake 冒頭コメント自身がそう述べている）。
   ⇒ 「51166fb6 の register_chipv7_phy は本個体で収束する」は**実証済みの事実**。
2. **`libble_app.a = 015db3db` は C5 で BLE bond 成功の実績がある**
   （memory `c3-ble-d2d-gatt-notify-sm.md`：「C5(blob 015db3db)」で
   sm_tx=2／ENC status=0／bond 成功を実機実証）。これは hal era の記録だが、
   §2.1 より **hal の libble_app ≡ submodule v5.5.4タグの libble_app** ＝同一バイナリ。
3. 実施09 の「hal libphy が BT でハング」は、**実施48-52 が WiFi について
   「hal(v8) 非互換」を相関≠因果として反証**した経緯があり、かつ evidence-04 が
   「当時の残壁＝RX IQ 較正ハングは実施42/43 の APM/HP-TEE 解除で別の真因が
   特定され解決済み」と記録している。⇒ 実施09 の観測は **blob 起因ではなかった**
   可能性が高い。

### 3.2 外れた場合に何を意味するか（＝反証条件を先に書く）

`bt_smoke_c5`(submodule blob) が cold で `esp_bt_controller_enable` を通らず
`register_chipv7_phy` でハングしたら：
- **予測は外れ**＝実施09 の観測は blob 起因で正しかったことになり、
  「同じ libphy が WiFi では収束するが BT では収束しない」＝**PHY 較正モードの
  差（`PHY_MODEM_BT` vs `PHY_MODEM_WIFI`）に真因が局在**する、という
  重要な新情報になる。その場合は **既定を +1169 へ戻し（可逆）**、
  「BT は submodule へ移行できない／こういう条件が要る」を結論として報告する
  （＝無理に成功を作らない）。

### 3.3 ビルドについての予測

`shared_periph_module_t` は §1.2 のとおり**版差**なので、
**BT の include/ソースを ESP_SUP_DIR へ揃えれば消滅**する（evidence-02 §1.2 の一般則）。
その先に、WiFi 移行時と同種の未解決参照が数件出ると予測する
（WiFi は `esp_interface.h` 欠落・`esp_clk_tree` 連鎖などを踏んだ）。

---

## 4. 実機測定（2026-07-17）— **★予測 §3.1 は的中：ハングせず D-1 達成**

### 4.0 ★測定条件の変更点（前節までとの差 ＝ 交絡として明示）

| 項目 | §1-3 執筆時の前提 | **本測定の実際** |
|---|---|---|
| **DUT** | ESP32-C5 **#2**（`d0:cf:13:f0:c8:94`） | **ESP32-C5 #1（`d0:cf:13:f0:a7:44`）** ★別個体 |
| PC | 別PC | 本PC（deskmini） |
| 電源制御 | `usbhub3c_ctl.py off 5` | `sudo uhubctl -l 1-6 -p 3-4 -a off/on`（**両ポート断**） |
| コンソール | usbjtag | **uart0（CP2102N）** ★下記 4.1 |
| toolchain | esp-15.2.0 | 同左（`~/.espressif/tools/…`） |

**★個体差の交絡（最重要）**：§3.1 の予測根拠1「libphy `51166fb6` はこの eco2 C5 実機で
cold 動作実証済み」の「この実機」は **C5#2**。本測定は **C5#1**＝**別個体**であり、その根拠は
そのままでは移送できない。本プロジェクトには「board B＝個体難聴＋個体ハングで引退」という
**個体差が実測を誤らせた実績**がある。
⇒ **だから §6-1 の決定的対照（同一個体・同一 libphy を WiFi 経路で先に実証）を省かずに実行した**（4.2）。

### 4.1 ★方法論の訂正：usbjtag コンソールでは「真の cold」を観測できない

最初の採取（usbjtag コンソール・`tmp/c5_cold_passive_capture.py`）は、**両ポート電源断→復電**の
直後にもかかわらず：

```
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
```

＝**POWERON ではない**。ACM ポートを open する行為自体が DUT をリセットするため、
usbjtag 経路では観測対象の cold boot が open で潰れる。
evidence-01/02/04 の「真の cold」ログが**すべて `rst:0x15`** であるのに対し、
**UART0 を使った evidence-03 だけが `rst:0x1 (POWERON)` を得ている**——この非対称は本ラウンドの
実測と整合する。⇒ **本ラウンドは全アームを UART0（CP2102N）コンソールで採取**し、
`rst:0x1 (POWERON)` を毎回確認した。

> これは HANDOFF §5-2（「`rts_boot_capture.py` は cold を warm に変える」）と**同型だが別経路**の
> ハーネス由来汚染。`c5_cold_passive_capture.py` は RTS を打たないので §5-2 は回避できるが、
> **usbjtag では open 自体がリセット源**なので受動採取でも足りない。

電源断は毎回**読み戻しで実証**（`ls /dev/serial/by-id/ | grep -c 'A7:44\|b04e3bcf'` → **0**）。

### 4.2 ★決定的対照（§6-1）：**同一個体・同一 libphy が WiFi 経路で収束する** — PASS

`build/c5_wifiscan_uart`（submodule 供給・`ninja -t deps` の hal 参照 **0**）、**真の cold**：

```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
wifi_scan: esp_wifi_scan_start -> 0
wifi_scan: 20 APs found (err=0)
  [0] <SSID masked> (rssi=-57 ch=1)
wifi_scan: RESCAN 30 APs (err=0)
```

⇒ **C5#1 において libphy `51166fb6` は WiFi 経路で収束する**（＝§3.1 根拠1 を、C5#2 からの
移送ではなく**本個体で直接**確立）。これで「BT でハングしたら個体か blob か永遠に決まらない」
という §6-1 の懸念を先回りで潰した。

### 4.3 ★本題：`bt_smoke_c5` D-1 — **真の cold で 2/2 達成**

`build/c5_btsmoke_uart`（`ESP32C5_BT=ON`・`ASP3_BT_IDF_V554=ON`・`ASP3_C5_PMU_INIT=OFF`）。
**独立した真cold 2 ブート**（rigor 標準＝プラットフォーム主張には ≥2 ブート）：

```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
bt_smoke_c5: esp_bt_controller_init OK (heap free=179944)
bt_smoke_c5: esp_bt_controller_enable(BLE)
I (657) phy: libbtbb version: 980c3ee, Dec  2 2025, 11:38:24     ← ★PHY init 通過（ハングせず）
esp_shim: set_isr intno=1 handler=420412a4
esp_shim: task 'ble_ll_task' -> tskid 1 (prio 23)
bt_smoke_c5: esp_bt_controller_enable OK
bt_smoke_c5: intr rate/1s line1=0 line2=0 (storm threshold ~ >>1000/s)   ← storm 無し
bt_smoke_c5: controller enabled, sending HCI Reset
bt_smoke_c5: Phase D-1 milestone reached                          ← ★D-1 達成
bt_smoke_c5: HP_APM M0-M3 exception latch clear (BT path OK)
```

**★`Phase D-1 milestone reached` が Command Complete の物証である根拠**（コード側で確認）：
`bt_smoke_c5.c:199` は `if (hci_reset_done)` でのみ当該行を出力し、`hci_reset_done` は
`bt_smoke_c5.c:75-79` の **`len>=2 && data[0]==0x04 && data[1]==0x0E`（HCI Command Complete）**
成立時にしか立たない。不成立時は `FAILED (no HCI Command Complete)` を出す別分岐。
⇒ milestone 行 ≡ Command Complete 受信。

**正直な限定**：`VHCI recv` のバイトダンプ行（`[0]=0x04` `[1]=0x0e` と
`HCI Command Complete received` の明示行）は、**2ブートとも同じ位置で文字レベルに寸断**され
コンソール上で判読できない。これは VHCI コールバックからの syslog バースト＝
**既知の ASP3 syslog バースト欠落/交錯**（memory: 実施20／C5実施04「コンソール氾濫が成功を隠す」）
であり、DUT の異常ではない。milestone 行はコード上で Command Complete に gate されているため
判定は成立するが、**「生バイト 04 0e を直接目視した」わけではない**ことを明記する。

### 4.4 W2（`ble_host_smoke_c5`）— **真の cold で over-the-air GATT まで達成**

デバイス側（UART0・真cold）：
```
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
ble_host_smoke_c5: esp_bt_controller_enable OK (heap free=167944)
ble_host_smoke_c5: esp_nimble_init (host)
ble_host_smoke_c5: nimble host task start
ble_host_smoke_c5: advertising started as 'ASP3-C5-BLE' (own_addr_type=0)
ble_host_smoke_c5: g_adv_rc=0 g_adv_active=1
```

**★ホスト側（BlueZ `hci0`）＝「現在放射している」ことの独立物証**：

```
[NEW] Device D0:CF:13:F0:A7:44 ASP3-C5-BLE          ← adv 検出。BD addr = DUT の BASE MAC と一致
[CHG] Device D0:CF:13:F0:A7:44 Connected: yes       ← GAP 接続確立
[NEW] Primary Service .../service0006  Generic Attribute Profile
[NEW] Characteristic .../service0006/char0007  Service Changed
[CHG] Device D0:CF:13:F0:A7:44 UUIDs: 00001800-0000-1000-8000-00805f9b34fb   (GAP)
[CHG] Device D0:CF:13:F0:A7:44 UUIDs: 00001801-0000-1000-8000-00805f9b34fb   (GATT)
[CHG] Device D0:CF:13:F0:A7:44 ServicesResolved: yes ← ★GATT サービス探索完了
```

> **★方法論**：memory の申し送り「LP_AON の adv-rc マーカは reset-survive ゆえ
> 『過去のブートが adv_start に到達した』しか証明せず**現在放射の証明にならない**」に従い、
> **ホスト側 OTA 検出を主物証**とした。`g_adv_rc=0` は補助。BD アドレスが DUT の BASE MAC と
> **完全一致**するので他個体の混入もない。
> **限定**：本 app は独自 GATT サービス（C3/C6 の `0xABF0-4` 相当）を**持たない**——検証できたのは
> 標準 GAP/GATT サービスの探索完了までで、**独自 characteristic の read/write/notify や
> bond/暗号（D-2d）は本 app のスコープ外**。

### 4.5 hal 参照の実測（`ninja -t deps`。★`find -name '*.d'` は 0 と誤測する）

| ビルド | hal 参照数 | 外部 `~/tools/esp-idf` 参照 |
|---|---|---|
| `c5_wifiscan_uart`（WiFi 供給の非回帰再測） | **0** | 0 |
| `c5_btsmoke_uart`（BT・D-1） | **0** | 0 |
| `c5_blehost_uart`（BLE・W2） | **0** | 0 |

BT ビルドの `-L` は **4本とも submodule のみ**（外部絶対パス `/home/honda/tools/esp-idf` は消滅）：
```
-L …/asp3_esp_idf/esp-idf/components/bt/controller/lib_esp32c5/esp32c5-bt-lib
-L …/asp3_esp_idf/esp-idf/components/esp_coex/lib/esp32c5
-L …/asp3_esp_idf/esp-idf/components/esp_phy/lib/esp32c5
-L …/asp3_esp_idf/esp-idf/components/soc/esp32c5/ld
```
NimBLE が submodule 由来で**実リンクされている**ことの tripwire（nm）＝
`ble_gap_adv_start` / `ble_hs_init` / `ble_svc_gap_device_name_set` の 3 シンボルが実体を持つ
（`ESP32C5_BT_NIMBLE:BOOL=OFF` は option 既定値の表示に過ぎず、**実リンクが正**）。

### 4.6 blob provenance の独立再実測（§2.1 の追認）

```
esp-idf submodule: git describe = v5.5.4 / 73550728   ＝タグそのもの
libble_app.a  submodule=015db3db  hal=015db3db  IDENTICAL
libphy.a      submodule=51166fb6  hal=51166fb6  IDENTICAL
libbtbb.a     submodule=211553eb  hal=211553eb  IDENTICAL
libcoexist.a  submodule=c516e24e  hal=c516e24e  IDENTICAL
```
⇒ **4/4 バイト一致**。本移行が「パスの付け替え」ではなく **BT blob 世代の実変更
（+1169≡v6.1 → v5.5.4タグ≡hal）** であることを追認。

---

## 5. 意味づけ — **記録済み「実施09」を C5#1 で反証**

### 5.1 予測は的中（§3.1）

`bt_smoke_c5` は **真の cold（`rst:0x1 POWERON`）で 2/2 ハングせず** D-1 到達。
§3.2 の反証条件（`register_chipv7_phy` でハング）は**発生しなかった**。
⇒ **既定を +1169 へ戻す必要はない**。

### 5.2 実施09「hal(v8) libphy は eco2 C5 の BT 経路で収束しない」は **本個体で成立しない**

本ラウンドは、実施09 の主張に対する**同一個体内の決定的対照**になっている：

| 経路 | libphy | 個体 | 電源 | 結果 |
|---|---|---|---|---|
| WiFi | `51166fb6`（≡hal） | C5#1 | 真cold | **収束**（20/30 AP） |
| **BT** | **`51166fb6`（≡hal・同一バイト）** | **C5#1** | 真cold | **収束**（libbtbb 版表示→enable OK→HCI CC→adv→GATT） |

⇒ 「同じ libphy が WiFi では収束するが BT では収束しない」という **§3.2 が想定した
『PHY 較正モード（`PHY_MODEM_BT` vs `PHY_MODEM_WIFI`）への真因局在』は、C5#1 では観測されない**。
実施09 の観測は **blob 起因ではなかった**という §3.1 根拠3（実施48-52 が相関≠因果として反証した経緯）
の側を支持する。

**★主張の射程を限定する（rigor）**：
- 本結果は **C5#1・eco2・本 AP 環境・この toolchain** での実測。**C5#2 では未再現**
  （このPCに C5#2 は接続されていない）。∴「実施09 は誤りだった」と**チップレベルで断定はしない**。
  正確には「**実施09 の症状は C5#1 では再現しない／blob だけでは説明されない**」。
- 逆に、**§3.1 根拠1・2 が依拠していた C5#2 由来の実績も、本測定の根拠にはしていない**
  （4.2 で本個体の対照を取り直した）。個体差の交絡は**両方向で**遮断した。
- C6 の前例（memory 実施90-91）は「**warm 残留に暗黙依存し cold で壊れる**」型だった。
  本ラウンドは全アーム `rst:0x1 (POWERON)` なので、**その型の隠れ依存は無い**ことも同時に示している。

### 5.3 `ASP3_C5_PMU_INIT` について（HANDOFF §4-2）

本ラウンドの BT アームは全て **`ASP3_C5_PMU_INIT=OFF`（既定）** で D-1／W2 とも達成した。
⇒ **BT でも pmu_init の機能上の便益は「必要ない」側の実測**（WiFi の 4アーム全 20AP と同じ結論）。
**「OFF のまま」が正しい**という HANDOFF §4-2 の選択肢を支持する。
（※本ラウンドは ON アームの A/B を**走らせていない**ので、「ON にすると壊れる」も
「ON に便益がある」も**主張しない**。OFF で足りることのみを示した。）

---

## 6. 結論・申し送り

### 6.1 結論

- **C5 の BT 供給は esp-idf submodule（真の v5.5.4 タグ）へ移行できる**。
  D-1（HCI Command Complete）・W2（OTA adv → connect → GATT ServicesResolved）とも
  **真の cold で達成**。hal 参照 **0**・外部 `~/tools/esp-idf` 参照 **0**。
- ∴ **evidence-05 §2.2(b) が指摘した「BT だけ +1169≡v6.1 のままで統一未達」は、本ラウンドで実際に解消**
  ＝ C5 は WiFi・BT とも **真の v5.5.4 タグ供給で統一**され、実機で動作する。
- **`ASP3_BT_IDF_V554=ON`（既定）を維持してよい**。fallback（`=OFF`→外部 v6.1）は可逆のまま温存。

### 6.2 未確認・申し送り

1. **C5#2 では未検証**（本PC未接続）。本結論の射程は C5#1。
2. **D-2c/D-2d（bond/暗号/独自 GATT characteristic）は未達**＝`ble_host_smoke_c5` に
   独自サービス（C3/C6 の `0xABF0-4` 相当）が**無い**ため。C3 は同日フル達成済み
   （`docs/bt-shim.md`）なので、**C3/C6 の app 設計を C5 へ転写すれば到達見込み**。
3. **usbjtag コンソールでの cold 観測は不可**（4.1）。以後 C5 の cold 判定は **UART0 固定**。
   evidence-01/02/04 の「真のcold」ログは実際には `rst:0x15`（USB リセット後）である点に注意
   ——**それらの結論を cold 依存の文脈で再利用するときは要再測**。
4. **syslog バースト交錯で VHCI 生バイトが読めない**（4.3）。C5 で HCI バイト列の直接物証が
   要るラウンドでは、LP_AON STORE マーカ（app が既に持つ）か採取レートの見直しが要る。
5. C3 / C6 への横展開は未着手（HANDOFF §4-3）。

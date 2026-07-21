# C5 evidence-05 — BT 供給の esp-idf submodule 移行（ビルド破綻の修復）

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #2**（BASE MAC `<MAC-39>`, hub **port5**, `ttyACM5` / `ttyUSB2`）
toolchain: Espressif `riscv32-esp-elf` esp-15.2.0

evidence-04 §6.2 が「pre-existing・供給移行の残作業」として記録した
`-DESP32C5_BT=ON` のビルド不能への回答。

---

## 1. 破綻の真因 — **コーディネータ診断を一部訂正**

### 1.1 追認した部分

`-DESP32C5_BT=ON -DASP3_APPLNAME=bt_smoke_c5` は configure rc=0 / build rc=1。
エラーは実測で再現（`scratchpad/build_before.log:65`）：

```
In file included from $HOME/tools/esp-idf/components/bt/include/esp32c5/include/esp_bt.h:19:
hal/components/esp_hw_support/include/esp_private/esp_modem_clock.h:46:32:
  error: unknown type name 'shared_periph_module_t'; did you mean 'periph_module_t'?
```

「**外部 `~/tools/esp-idf` の `esp_bt.h` × hal の `esp_modem_clock.h` の混成**」
という構図の診断は**正しい**。`esp_bt.cmake:61` の `set(IDF $HOME/tools/esp-idf)`
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
| **DUT** | ESP32-C5 **#2**（`<MAC-39>`） | **ESP32-C5 #1（`<MAC-37>`）** ★別個体 |
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
[NEW] Device <MAC-37> ASP3-C5-BLE          ← adv 検出。BD addr = DUT の BASE MAC と一致
[CHG] Device <MAC-37> Connected: yes       ← GAP 接続確立
[NEW] Primary Service .../service0006  Generic Attribute Profile
[NEW] Characteristic .../service0006/char0007  Service Changed
[CHG] Device <MAC-37> UUIDs: 00001800-0000-1000-8000-00805f9b34fb   (GAP)
[CHG] Device <MAC-37> UUIDs: 00001801-0000-1000-8000-00805f9b34fb   (GATT)
[CHG] Device <MAC-37> ServicesResolved: yes ← ★GATT サービス探索完了
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

BT ビルドの `-L` は **4本とも submodule のみ**（外部絶対パス `$HOME/tools/esp-idf` は消滅）：
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

---

## 7. ★D-2c/D-2d 予測（**ビルド・実機を触る前に固定。測定後に改竄しない**）

ユーザー判断で C3/C6 横展開より先に **C5 の D-2c/D-2d** を実施する（6.2-2 の申し送りを埋める）。
コーディネータの訂正を受け、**W2＝connect＋`ServicesResolved`（標準 GAP/GATT の探索完了）までであり、
D-2c/D-2d 水準ではない**ことを前提とする。

### 7.0 着手前に実測で確定した事実（＝予測の土台。これ自体は測定済み）

| 事実 | 実測値 |
|---|---|
| `apps/ble_host_smoke_c5/ble_host_smoke_c5.c` に `0xABF0`〜`0xABF4` | **0 件**（608行・notify 0・READ_ENC 0） |
| `ESP32C5_BT_SM` option | **既定 ON**（`esp_bt.cmake:445`）＝`TOPPERS_ESP32C5_BT_SM` 付与 |
| **W2 ビルドで SM/tinycrypt が実リンク済みか** | **リンク済**：`ble_store_config_init`/`ble_sm_pair_initiate`/`ble_sm_alg_encrypt`/`tc_aes_encrypt`/`ble_gap_security_initiate` 各1・`uECC_*` **15** |
| SM 配管（`sm_bonding=1`・`ble_store_config_init`・`bt5_security_tick`・ENC/PAIRING マーカ） | app に**既存** |

⇒ **欠けているのは app の GATT surface だけ**＝**C6 §15 と同型**（C6 は app 完備で cmake トグルのみ／
C5 は cmake 完備で app のみ）。**新規設計はせず C3 `ble_host_smoke.c:206-313` を逐語転写する**。

### 7.1 予測（ビルド）

- **予測 B1：cmake 変更は不要**。SM/tinycrypt/`ble_store_config` は既にリンク済（7.0 実測）
  ⇒ **app ソース追加のみで通る**。C3 の `ESP32C3_BT_PVCY` 相当の追加トグルも不要。
- **予測 B2：壁は 0〜2 件**。候補は (i) `host/ble_gatt.h`/`host/ble_uuid.h`/`os/os_mbuf.h` の
  include 追加漏れ、(ii) **`bt_nimble_config.h` の GATT リソース上限**で `ble_gatts_count_cfg` が
  非0 を返す可能性（C6 §14 は同ファイルを stub 側へ隔離した経緯あり）。
- **予測 B3：`g_conn_handle` の ifdef 移動が要る**。現状 `TOPPERS_ESP32C5_BT_SM` 配下だが
  `notify_tick` は SM 非依存で必要 ⇒ 無条件化する（SM=OFF ビルドの非回帰を壊さないこと）。

### 7.2 予測（実機 D-2c）

**予測 C1：`0xABF1` READ / `0xABF2` NOTIFY / `0xABF3` WRITE の3方向とも通る。**
根拠＝同一の NimBLE（submodule v5.5.4 タグ）で C3 が全4特性を OTA 実証済み、かつ C5 は既に
connect＋`ServicesResolved` まで到達している（§4.4）＝ATT サーバは現に動いている。

**予測 C2：`0xABF0` は BlueZ から見える**（＝C3 で起きた「不可視」は再発しない）。
根拠＝C3 の不可視は **100% central 側 GATT キャッシュ**と実測断定済（`0x5eed8309`＝f=1/svc=3/chr=9・
`add_svcs rc=0`）で**デバイス側登録失敗ではなかった**。今回の central は BlueZ で、
**本セッションで `bluetoothctl devices` が空＝キャッシュ無し**を実測済み。
- ★ただし**一度接続するとキャッシュが生まれる**ため、**特性を変更した後は毎回 `remove` してから
  再探索する**（憶測で「キャッシュだろう」と片付けないための手順化）。

### 7.3 予測（実機 D-2d）

**予測 D1：`0xABF4` の暗号必須 READ は bond 後に通る**（`"BT4-OK"` が返る）。
根拠＝(i) SM/tinycrypt/uECC が**実リンク済**（7.0）、(ii) C3 が同一設計で OTA フル実証済、
(iii) C5 は `sm_sc=1`/`sm_io_cap=NO_IO`＝Just Works で BlueZ 側に入力 UI が要らない。

**予測 D2：未ペア READ は insufficient-authentication で弾かれ、BlueZ が pairing を自動開始する。**

### 7.4 ★外れた場合に何を意味するか（反証条件を先に書く）

| 観測 | 意味づけ |
|---|---|
| `0xABF0` が BlueZ から**不可視** | **憶測で「キャッシュ」と言わない**。`TOPPERS_C5_GATTS_REGDIAG`（C3 の `TOPPERS_C3_GATTS_REGDIAG` を転写・既定OFF・非回帰）で `gatts_register_cb` の登録結果を LP_AON へ出し、**f（OP_SVC で UUID==0xABF0 到達）＋svc 数＋chr 数**を読む。f=1 なら **central 側キャッシュ**、f=0 なら **svc 定義が `ble_gatts_start` で丸ごと弾かれた**（C3 で `0xABF4` が容疑になった型）＝**非依存に決定**する。 |
| `0xABF1` READ は通るが `0xABF2` NOTIFY だけ来ない | CCCD/subscribe 経路（`BLE_GAP_EVENT_SUBSCRIBE` の `attr_handle==val_handle` 照合）を疑う。`g_notify_sent`/`g_notify_fail`/`g_notify_last_rc` で **device 側の送出可否を先に**切り分ける（central 側表示の問題と混同しない）。 |
| `0xABF4` READ で **bond は成立するのに暗号 read が返らない** | SM 最終段（鍵配布→bond 登録）の問題。`PAIRING_COMPLETE` の status と `our_sec`/`peer_sec` を STORE6 で読む（C5 は STORE6 が ENC/PAIRING 共用＝last-wins：成功なら `0x5DC0…`／失敗なら `0x5DE0<status>` が残る）。 |
| **connect＋bond が「成功」したように見える** | **古い bond の再利用でも起こる**（C3 の実績）。**`remove` 後のフレッシュ試行**でのみ真の可否を測る。 |
| ビルドが `count_cfg` 非0 | 予測 B2(ii) 的中＝`bt_nimble_config.h` のリソース上限。**上限を上げるのが正しい**（app 側で特性を削らない）。 |

### 7.5 変えないもの（非回帰の約束）

- **BT 供給は現状維持**＝`ASP3_BT_IDF_V554=ON`（真の v5.5.4 タグ submodule）。D-1/W2 が通った構成を壊さない。
  **hal 参照 0・外部 `~/tools/esp-idf` 参照 0 を各ビルドで再測**する。
- **全アーム UART0（CP2102N）採取で `rst:0x1 (POWERON)` を毎回確認**（4.1 の自己発見を守る）。
- 電源は `sudo uhubctl -l 1-6 -p 3-4` のみ・毎サイクル **S3-B 在席確認**・書込前 **MAC 照合**。

### 7.6 LP_AON STORE の割当（C5 は **STORE0-9 のみ実在**＝全8+2 が使用中）
<!-- ↓§8 は測定後に追記。§7 は測定前に commit 8312122 で固定済み・以降無改変 -->


新規に要るのは **write マーカ（`0xABF3`）**。C3 は D-2c で「storm probe を無効化して 2reg を接続観測へ
明け渡す」ことで解決した。**C5 も同じ判断**を採る：`storm_monitor_task` の **STORE5（線2累積ミラー）を
write マーカへ転用**する（STORE4＝線1ミラーは残す）。妥当性＝**storm 非発生は §4.3/§4.4 で
`line1=0 line2=0` を live 実測済＝線2 ミラーの情報価値は既に尽きている**。`report_intr_rate()` は
両線ともコンソールへ出し続けるので観測能力は落ちない。

---

## 8. D-2c/D-2d 実機測定（2026-07-17）— **★D-2c・D-2d とも達成。予測は概ね的中**

測定条件は §7.5 の約束どおり：BT 供給＝`ASP3_BT_IDF_V554=ON`（真の v5.5.4 タグ submodule）のまま、
**全アーム UART0 採取で `rst:0x1 (POWERON)` を確認**、電源は `-l 1-6 -p 3-4` のみ・毎サイクル S3-B 在席確認・
書込前 MAC 照合（`<MAC-37>`）。

### 8.1 実装＝C3 からの逐語転写（新規設計なし）

`apps/ble_host_smoke_c5/ble_host_smoke_c5.c` へ C3 `ble_host_smoke.c:206-313` を転写：
`gatt_read_access`／`gatt_notify_access`／`gatt_write_access`／`custom_chrs[]`／`custom_svcs[]`／
`notify_tick()`／`BLE_GAP_EVENT_SUBSCRIBE`・`_MTU` 処理／`ble_gatts_count_cfg`＋`ble_gatts_add_svcs`。
`TOPPERS_C5_GATTS_REGDIAG`（C3 の判別計装・**既定 OFF**）も併せて転写した。

### 8.2 予測の答合せ（§7）

| 予測 | 結果 |
|---|---|
| **B1** cmake 変更不要 | **的中**。app ソース追加のみ。cmake は 1 行も触っていない |
| **B2** 壁 0〜2 件 | **的中（下限）＝壁 0 件**。configure rc=0／build rc=0 が**一発**（C6 §15 と同型） |
| **B3** `g_conn_handle` の ifdef 無条件化が要る | **的中**。SM 配下から出した。**SM=OFF ビルドの非回帰も実測**（8.5） |
| **C1** READ/NOTIFY/WRITE 3方向とも通る | **的中**（8.3） |
| **C2** `0xABF0` は BlueZ から可視 | **的中**（8.3）。C3 の「不可視」は再発せず |
| **D1** `0xABF4` は bond 後に通る | **的中**（8.4） |
| **D2** 未ペア READ が pairing を起動 | **的中**（8.4） |

### 8.3 ★D-2c＝READ／NOTIFY／WRITE の3方向が実際に流れた

**キャッシュ罠の手当**：本 app は特性を変更したため、**毎試行 `remove` してフレッシュ discovery**
（§7.2 の手順化どおり）。`remove` 直後の再探索で `0xABF0`〜`0xABF4` が**全て再列挙**された
（`service000e` ＝ `0000abf0-…`、`char000f/0011/0014/0016` ＝ `abf1/abf2/abf3/abf4`、
`0xABF2` の CCCD `desc0013` ＝ `00002902` も出る）。⇒ **C2 的中・キャッシュ問題は発生せず**。

ホスト側（BlueZ／D-Bus 直叩き）と**デバイス側 UART0 の突合せ**：

```
[D-2c] ABF1 0xABF1 READ -> 42 54 34 2d 4f 4b  (b'BT4-OK')        ← 固定値 read
[D-2c] ABF2 0xABF2 READ -> 00 00 00 00                            ← カウンタ現在値
       ABF2 NOTIFY -> 01 00 00 00 / 02 00 00 00 … 09 00 00 00     ← 8s で 9 通知（≒1/s）
[D-2c] ABF3 WRITE 0x5a 0x01 -> ok
```
```
（デバイス側 UART0・同一セッション live）
ble_host_smoke_c5: GAP CONNECT status=0 handle=0
ble_host_smoke_c5: GAP MTU value=256
ble_host_smoke_c5: GAP SUBSCRIBE attr=18 cur_notify=1 reason=1    ← 0xABF2 の val_handle(0x12)
ble_host_smoke_c5: GATT WRITE len=2 first=0x5a count=1            ← ★書いた値と完全一致
```

**独立物証（RTC マーカ・コンソール非依存）**：**cold boot 直後に「1接続＋1回だけ write」**という
統制条件で採取（`esptool --before usb-reset --after no-reset read-mem`）：

```
0x600B1014 (STORE5) = 0x7717015a   ← ★write マーカ：count=1・先頭バイト=0x5a（予測どおり）
0x600B1020 (STORE8) = 0x604e0001   ← CONNECT status=0 count=1
0x600B1010 (STORE4) = 0x00000000   ← 線1ミラー＝0（コンソールの line1=0 と整合＝正常値）
```
⇒ §7.6 の **STORE5 転用は妥当**（app 自身のコメントが「STORE5 の reset 生存は未確認」と
留保していた点も、本ラウンドで**実測により解消**）。

### 8.4 ★D-2d＝`0xABF4` 暗号必須 READ が bond 後に通った（**フレッシュ 2/2**）

**★「古い bond の再利用」を排除**（§7.4）：毎試行 **`remove`（forget）** してから接続し、
リンク開始時点で **`paired=0 bonded=0`** を実測してから測った。

```
（trial 1・trial 2 とも同一）
connected=1 resolved=1 paired=0 bonded=0                     ← ★フレッシュ（古いbondではない）
[D-2d] 0xABF4 を UNPAIRED のまま READ
  → 値が返らない（BlueZ が pairing を開始）                   ← D2 的中：暗号ゲートが効いている
（デバイス側 UART0）
  ble_host_smoke_c5: GAP PAIRING_COMPLETE status=0            ← SMP 成功
  ble_host_smoke_c5: GAP ENC_CHANGE status=0                  ← 暗号確立
  bond settled: paired=1 bonded=1
[D-2d] bond 確立後に 0xABF4 を再 READ
  ABF4 0xABF4 READ -> 42 54 34 2d 4f 4b  (b'BT4-OK')          ← ★D-2d 達成
[D-2c control] ABF1 0xABF1 READ -> 42 54 34 2d 4f 4b          ← 同一リンクで平文readも健全
```

**A/B が揃っている**＝同一特性が **未ペアでは返らず／bond+暗号後には返る** ⇒
**bond/LTK 暗号が end-to-end で実効**であることの物証（C3 と同じ判定基準）。
RTC マーカ側も `0x600B1018 (STORE6) = 0x5de00000` ＝ `0x5DE0` タグ＋**status=0**（ENC_CHANGE 成功）
でコンソールと一致（C5 は STORE6 が ENC/PAIRING 共用の last-wins。本ラウンドは
PAIRING_COMPLETE → ENC_CHANGE の順に発火したため **ENC 側が残る**＝app コメントの
想定順序とは逆だが、どちらも status=0 で成功）。

**★正直な限定**：`PAIRING_COMPLETE status=0` と同時に出る `bonds our=0 peer=0` は **0 のまま**。
C3 の判定基準（`status=0 かつ our_sec>=1`）のうち **our_sec>=1 は満たしていない**。
`ble_store_util_count` を PAIRING_COMPLETE の瞬間に読む timing の問題か、`ble_store_config`
（PERSIST=0＝RAM）の数え方かは**未確定**。ただし**機能的な証明は独立に立っている**
（未ペアで弾かれ、bond 後に READ_ENC が実データを返した＝NimBLE が暗号リンクでしか許可しない経路）。
⇒ **「暗号が実効」は証明済み／「bond が device 側 store に登録され再接続で再利用される」は未検証**。

### 8.5 非回帰の実測（§7.5 の約束）

| ビルド | 結果 | hal 参照 | 備考 |
|---|---|---|---|
| `c5_ble_d2cd`（SM=ON 既定・本番） | build rc=0 | **0** | 外部 `~/tools/esp-idf` 参照も **0** |
| `c5_ble_smoff`（`ESP32C5_BT_SM=OFF`） | build rc=0 | **0** | **`0xABF4` の UUID が ELF から消える**＝SM ゲート完全 |
| `c5_ble_regdiag`（`TOPPERS_C5_GATTS_REGDIAG`） | build rc=0 | **0** | `gatts_regdiag_cb` 実リンク＝判別計装は**いつでも使える** |
| `c5_btsmoke_uart`（D-1） | build rc=0 | **0** | D-1 非回帰 |

**★静的 tripwire（nm のシンボル数で判断しない＝rigor 標準）**：ELF のバイト直読みで
GATT テーブルの**中身**まで確認した（`custom_chrs` @0x420621e8）：

| chr | UUID リテラル | flags | access_cb |
|---|---|---|---|
| `0xABF1` | `1000f1ab` | `0x0002` READ | `gatt_read_access` |
| `0xABF2` | `1000f2ab` | `0x0012` READ\|NOTIFY（+val_handle） | `gatt_notify_access` |
| `0xABF3` | `1000f3ab` | `0x0008` WRITE | `gatt_write_access` |
| `0xABF4` | `1000f4ab` | **`0x0202` READ\|READ_ENC** | `gatt_read_access` |
| svc | `1000f0ab`＝`0xABF0` PRIMARY | → `custom_chrs` | |

（`notify_tick`／`os_mbuf_append` が nm に出ないのは **inline 化**と NimBLE の
**`r_os_mbuf_append` リネーム**が理由＝逆アセンブルで `jal <ble_gatts_notify_custom>` の実在を確認済。
**シンボル不在を「機能不在」と誤読しない**）。

### 8.6 ★方法論：ハーネスを先に疑って正解だった（rigor 標準の実例）

D-2d は**最初 2 回失敗した**が、**どちらも DUT ではなくハーネスが原因**だった：

1. **`bluetoothctl` へのパイプ入力は競合する**。agent の
   `Accept pairing (yes/no):` プロンプトと service-discovery の出力バーストが
   **後続コマンドを黙って食う**（`menu gatt` が消え、以降 全 GATT コマンドが
   `Invalid command in menu main` になった）。`--- ラベル ---` の echo すら
   bluetoothctl へのコマンドとして解釈された。
2. **D-Bus 直叩きへ移行したが agent 未登録**だったため BlueZ が pairing を認可できず
   `0xABF4` が NoReply。**この時点で「デバイスが SM を通せない」と誤断する誘惑があった**が、
   直前の bluetoothctl 実行が `Bonded: yes/Paired: yes` に到達していた実測があったため
   **ハーネス側を疑って正解**（HANDOFF §5-7「ログが途中で切れたら DUT より先にハーネスを疑え」の同型）。
   → 自動承認 agent（`NoInputNoOutput`）を Python で登録して解決＝**無人で決定論的**に。
3. 残る `NoReply` は **BlueZ が pairing 中に元の ReadValue を自動リトライしない**ため。
   **bond 確立を待って再 READ すれば返る**（8.4）＝これも DUT の問題ではない。

**★もう一つの自己訂正**：`STORE5=0` を見て一度「write マーカが動いていない」と疑い、
さらに「`--before usb-reset` が app を再起動してマーカを消しているのでは」と仮説を立てたが、
**device を `remove` して2連続 read しても `STORE8` が保持された**ことで**その仮説は反証**
（＝`usb-reset` は ROM download モードに留め、app は走らない＝マーカは凍結される）。
真相は「**長時間セッション中に app が一度再起動しており**（`STORE8` の connect count が
~7 回接続したのに **1** だった）、その後の trial は `0xABF3` write をしないスクリプトだった」。
**cold boot 直後に 1接続＋1write だけを行う統制条件**で測り直したら `0x7717015a` が出た（8.3）。
⇒ **憶測で結論を書かず、統制した再測定で決着させた**。何が app を再起動したかは**未特定**（非ブロッキング）。

---

## 9. 結論（D-2c/D-2d）

- **C5 は D-2c／D-2d とも実機達成**。`ASP3_BT_IDF_V554=ON`（**真の v5.5.4 タグ submodule 供給**）のまま、
  **真の cold（`rst:0x1 POWERON`）**で
  **`0xABF1` READ="BT4-OK"／`0xABF2` NOTIFY（LEカウンタ）／`0xABF3` WRITE（デバイス側 `first=0x5a` 一致）／
  `0xABF4` 暗号必須 READ（フレッシュ bond 後に "BT4-OK"、未ペアでは弾かれる）**を OTA 実証。
- **hal 参照 0・外部 esp-idf 参照 0** を全ビルドで維持（D-1/W2 の構成を壊していない）。
- ∴ **C5 の BLE は C3 と同水準（connect＋bond/暗号＋フル GATT）に到達**。
  §6.2-2 の申し送り「D-2c/D-2d 未達」は**解消**。

### 9.1 残課題・申し送り

1. ~~**`bonds our=0 peer=0` の未確定**（8.4）~~ → **§11 で決着**：
   - **`our=0` は artifact**（NimBLE は PAIRING_COMPLETE を鍵保存より «前» に発火する。
     ソース `ble_sm.c:1114-1121` ＋ 後から数え直す直接測定 `our=1 peer=1` の2点で確定）。
   - **device 側 bond store への登録は «成立»**（STORE3 `0xb0d50101`）＝§8.4 の「未証明」は解消。
   - **★ただし「再接続での LTK 再利用」は FAIL＝新たに判明した実バグ**（§11.2/§11.6-1）。
   - bond が再起動を跨がないのは **RAM backed ＝設計どおり**（§11.4）。
2. **長時間 OTA セッション中の app 再起動**（8.6）＝原因未特定・非ブロッキング。
3. C3 / C6 への供給移行の横展開は依然未着手（HANDOFF §4-3）。
4. **`bluetoothctl` パイプ駆動は使わない**こと（8.6-1）。本ラウンドの
   D-Bus スクリプト（agent 登録込み）が再利用可能な正しい型。

---

## 10. ★bond 永続化・LTK 再利用 予測（**実機を触る前に固定。測定後に改竄しない**）

§9.1-1 の残課題（`PAIRING_COMPLETE status=0` なのに `bonds our=0 peer=0`）を潰す。
コーディネータ指摘に従い **「bond 永続化」を2つに分離**して測る（混同しない）。

### 10.0 着手前に**静的に実測**した事実（予測ではなく測定済み。§10.1 の土台）

**(a) C5 の bond store は RAM backed＝NVS 不使用（3点で確定）**

1. `esp_bt.cmake:626` は SM=ON で **`ble_store_config.c` のみ**を積む
   （`ble_store_nvs.c`・`ble_store_config_conf.c` は**リンク対象外**）。
2. `esp_nimble_cfg.h:1097` は `MYNEWT_VAL_BLE_STORE_CONFIG_PERSIST` を
   **`#ifdef CONFIG_BT_NIMBLE_NVS_PERSIST`** で決めるが、**同マクロは C5 ビルドのどこにも未定義**
   ⇒ **`PERSIST = 0`**（upstream `syscfg.yml` の既定は 1 だが、本ビルドでは 0）。
3. **ELF 実測**：`ble_store_config_persist_our_secs` / `ble_store_config_conf_init` /
   `ble_store_nvs_*` / `nvs_open` / `nvs_set_blob` は **全て 0 個**。
   リンクされているのは `ble_store_config_init/read/write` のみ。

⇒ **P2（再起動を跨ぐ bond）は「残らないのが設計どおり」**である公算が高い。
**勝手に NVS 化はしない**（スコープ外＝ユーザー判断事項として申し送る）。

**(b) C3 と C5 は store backend も count 箇所も «同一»＝「C3 との唯一の差分」は存在しない**

| | C3 | C5 |
|---|---|---|
| store | `ble_store_config.c`（PERSIST=0＝RAM。`esp_bt.cmake:489`） | 同左（`:626`） |
| count 箇所 | `ble_host_smoke.c:627-628`（PAIRING_COMPLETE ハンドラ内） | `ble_host_smoke_c5.c:589-590`（同じ） |

⇒ **仮説3（C3/C5 の差分を静的同定）は「差分なし」で決着**。∴ C5 固有の異常ではない。

**(c) ★「C3 では `our_sec>=1` が満たされた」という前提自体が疑わしい**

- `docs/`・`.steering/` を全文検索しても **`our=<非0>` の実測記録は1件も存在しない**
  （`our_sec>=1` の出現は**私が §8.4 で criterion を引用した箇所のみ**）。
- 逆に `docs/bt-shim.md:2434` は C3 で
  **「`PAIRING_COMPLETE status=0`（SMP完了！）だが phone 未 bond・**bond件数0**」** を記録している。
⇒ **`our_sec>=1` は C3 のソースコメント由来の «期待値» であって、実測された事実ではない**。
  §8.4 で私が「C3 の判定基準を満たしていない」と書いたのは、
  **満たされた実績のない基準を引き合いに出していた**ことになる（本節で訂正する）。

**(d) 「count が ENOTSUP で殺されている」仮説は **反証済**（憶測で書かない）**

`ble_store_iterate` は `#if NIMBLE_BLE_CONNECT && MYNEWT_VAL(BLE_SM_SC)`、偽なら
`return BLE_HS_ENOTSUP`＝`out_count` は 0 のまま。`bt_nimble_config.h:139` が
`CONFIG_BT_NIMBLE_SM_SC` を **0 に define** しているので一見成立しそうに見えるが——
`esp_nimble_cfg.h:1029` は **`#if` ではなく `#ifdef`**。**0 でも «定義されている» ので真**
⇒ **`MYNEWT_VAL_BLE_SM_SC = 1`**。
**ELF 実測で裏取り**：`ble_store_iterate` は **0xdc バイトの実体**（ENOTSUP スタブなら数バイト）、
`ble_store_config_write_our_sec` = 0xbc バイト、`uECC_*` 15 個。
⇒ **カウント機構は完全に有効。∴ `our=0` はコンパイル時 artifact ではない**。

### 10.1 予測

- **予測 P1：PASS**＝bond 確立 → 切断 → **`remove` せず再接続** → **再ペアリング無しで
  `0xABF4` が `"BT4-OK"` を返す**（LTK 再利用）。
  根拠＝§8.4 で暗号が実際に確立した以上、device 側は LTK を保持していたはず。
- **予測 P2：FAIL（＝bond は残らない）。ただしこれは «設計どおり» であって欠陥ではない**。
  根拠＝10.0(a)。真cold で RAM が消える ⇒ 再接続時に**再ペアリングが発生**する。
- **予測 A：`our=0` は «カウントのタイミング» による artifact**（keys が store に書かれる前に
  PAIRING_COMPLETE ハンドラが数えている）。
  ⇒ **P1 が PASS すれば、それ自体が「LTK は device 側に在った」の物証**になり、
  **P1 PASS ＋ `our=0` の同居 ⇒ `our=0` は artifact と «非依存に» 決まる**（これが最も安い決着）。

### 10.2 ★外れた場合に何を意味するか（反証条件を先に書く）

| 観測 | 意味づけ |
|---|---|
| **P1 が FAIL**（再接続時に**新しい `PAIRING_COMPLETE` が発火**して初めて `0xABF4` が読める） | **予測 A も外れ**＝`our=0` は **artifact ではなく実体**＝**device 側に bond が保存されていない**。その場合 §8.4/§9 の「bond」表現は**過大**であり、実態は「**接続ごとに毎回ペアリングし直しているだけ**」＝**D-2d の主張を「暗号は実効／bond は不成立」へ格下げする**。無理に成功を作らない。 |
| **P2 が PASS**（真cold 後に再ペアリング無しで `0xABF4` が読める） | 10.0(a) の静的結論（RAM backed）と**矛盾**する ⇒ 測定か静的読みのどちらかが誤り。**BlueZ 側が黙って再ペアリングしていないか**（device console の新規 `PAIRING_COMPLETE` の有無）を必ず確認してから結論する。 |
| P1/P2 とも「`0xABF4` が読める」 | **「読めた」だけでは判別できない**。**判定は「device console に新しい `PAIRING_COMPLETE` が出たか否か」で行う**（出なければ LTK 再利用／出れば再ペアリング）。 |

### 10.3 測定設計（`remove` の有無が本質なので明示的に設計する）

| アーム | 手順 | `remove` | 判定 |
|---|---|---|---|
| **P0（土台）** | 真cold → `remove`（clean slate）→ connect → `0xABF4` READ → pairing 発生 → bond 確立 | **する**（開始時のみ） | `PAIRING_COMPLETE status=0` |
| **P1** | P0 の直後 → **disconnect** → **再 connect** → `0xABF4` READ | **しない**（本質） | **新 `PAIRING_COMPLETE` 無し**で `"BT4-OK"` なら PASS |
| **P2** | P1 の状態から → **真cold**（`-l 1-6 -p 3-4`）→ 再 connect → `0xABF4` READ | **しない**（本質） | **新 `PAIRING_COMPLETE` の有無**で判定 |

- **既存バイナリ（`build/c5_ble_d2cd`＝§8 で検証済み）をそのまま使う**＝app 改変ゼロで測る。
  改変が要ると判明した場合のみ、**既定 OFF のガード付き**で追加し非回帰を実測する。
- **全アーム UART0 採取で `rst:0x1 (POWERON)`**（cold アームのみ）。ハーネスは **D-Bus ＋ 自動承認 agent**
  （`bluetoothctl` パイプ駆動は禁止＝§8.6）。`NoReply` は bond 確立を待って再試行。
- **BT 供給は不変**（`ASP3_BT_IDF_V554=ON`）。hal 参照 0 を再測。

---

## 11. bond 永続化・LTK 再利用 実機測定（2026-07-17）— **P1 は FAIL・`our=0` は artifact（予測は一部外れ）**

測定条件：§10.3 の設計どおり。BT 供給不変（`ASP3_BT_IDF_V554=ON`）、全 cold アームで
`rst:0x1 (POWERON)` を UART0 で確認、ハーネスは D-Bus ＋自動承認 agent、
電源は `-l 1-6 -p 3-4` のみ、書込前 MAC 照合。

> **★測定環境の変化（記録）**：本ラウンド中に**ユーザーが hub 1-6 の port1/2 の個体を
> 2度入替えた**（S3-B 抜去 → C6 装着 → C6 抜去）。当初の「S3-B 在席確認」ガードが2度発火したが、
> **いずれも私の電源操作による巻き込みではない**（`-p 3-4` はポート1/2 を駆動できず、
> 実際 port1/2 は `power enable connect` を保ったまま**占有個体の MAC だけが変わった**＝物理入替）。
> 最終的な読み戻し条件は **`off` 後に `grep -c A7:44` = 0 のみ**（コーディネータ指示）。
> **DUT の C5#1 は全ラウンドを通じて無事**。

### 11.1 予測の答合せ（§10.1）

| 予測 | 結果 |
|---|---|
| **P1：PASS**（LTK 再利用） | **★外れ＝FAIL**（11.2） |
| **P2：FAIL だが設計どおり** | **的中**（11.4） |
| **A：`our=0` は カウントのタイミング artifact** | **★的中**（11.3）——**しかも これが本丸だった** |

### 11.2 ★P1＝FAIL：LTK は再利用されない（2ビルドで再現）

> **★★2026-07-17 追記＝本節は §13 で «撤回» された。** (1) `reason=517` は
> **`PIN or Key Missing` ではなく `Authentication Failure`**（§12.0(a) で自己訂正）。
> (2) **P1 の FAIL は device のバグではなく «採取ハーネス» が原因**——`c5_cold_passive_capture.py`
> の port-open が DUT をリセットし RAM の bond store を消していた（§13.1/§13.3）。
> **capture を開かなければ P1 は PASS**（`ABF4 → "BT4-OK"`・device 側 `store_read rc=0`）。
> 以下の記述は «当時の観測» としてそのまま残すが、**結論は §13 が正**。


bond 確立後 → 切断 → **`remove` せず**再接続：

```
[p1 before connect] connected=0 resolved=0 paired=1 bonded=1   ← BlueZ 側は bond 保持
[p1 after connect]  connected=0 ...                            ← 接続が維持できない
  ABF4 0xABF4 READ -> org.bluez.Error.Failed
（デバイス側 UART0）
  GAP CONNECT status=0 handle=0
  GAP ENC_CHANGE status=7          ← BLE_HS_ENOTCONN
  GAP DISCONNECT reason=517        ← 512+5 = BLE_HS_HCI_ERR(0x05) = «PIN or Key Missing»
  ★NEW PAIRING_COMPLETE count = 0  ← «再ペアリングすらしていない»
```

⇒ central が保存済み LTK で暗号化を要求 → **device 側が LTK を «照合» できず** → 切断。
**§10.2 の「P1 FAIL なら新しい PAIRING_COMPLETE が発火して初めて読める」よりも悪い**
（再ペアリング自体が起きず、リンクが落ちる）。

### 11.3 ★`bonds our=0` は **artifact** — 2つの独立な方法で決着（憶測なし）

**(1) ソース上の構造（NimBLE `ble_sm.c:1114-1121`）**
```c
if (res->enc_cb && res->app_status != BLE_HS_ENOTCONN) {
    ble_gap_pairing_complete_event(conn_handle, res->sm_err);  /* ← 我々の PAIRING_COMPLETE */
}
/* Persist keys if bonding has successfully completed. */
if (res->app_status == 0 && rm && (proc->flags & BLE_SM_PROC_F_BONDING)) {
    ble_sm_persist_keys(proc);                                  /* ← 保存は «その後» */
}
```
⇒ **PAIRING_COMPLETE ハンドラ内での計数は構造的に «保存前» を見ている**＝0 が出るのは当然。

**(2) 直接測定＝«後から» 数え直す**（`TOPPERS_C5_BOND_COUNT_DIAG`・**既定 OFF**・非回帰）

```
GAP PAIRING_COMPLETE status=0 bonds our=0 peer=0    ← ハンドラ内（早すぎる）
GAP ENC_CHANGE status=0
BONDDIAG bonds our=1 peer=1 (late count)           ← ★1秒後：鍵は «在る»
```

**★syslog 非依存の物証**（C5 の既知 syslog バースト欠落で «変化した瞬間の1行» が消えるため、
STORE3 へ毎秒無条件ミラー `0xB0D5<our:8><peer:8>` を追加し `esptool read-mem` で回収）：

| アーム | 条件 | STORE3 実測 | 意味 |
|---|---|---|---|
| **A** | 真cold・**BLE 操作を一切しない** | `0xb0d50000` | `our=0 peer=0`＝**store は空** |
| **B** | フレッシュ pairing 直後 | **`0xb0d50101`** | **`our=1 peer=1`＝鍵は保存されている** |
| **C** | bond 後に**真cold**（`remove` せず） | `0xb0d50000` | **bond 消滅**（＝P2） |

⇒ **`our=0` は artifact と «非依存に» 確定**（予測 A 的中）。

**★私の §10.2 の推論自体が誤りだったことを明記する**：
§10.2 は「**P1 が FAIL なら 予測 A も外れ＝`our=0` は実体＝bond が保存されていない**」と
書いたが、実測は **P1 FAIL かつ `our=1`（保存されている）** で、**この含意は成立しない**。
つまり私が測定前に固定した反証条件のうち **「P1 FAIL ⇒ A も外れ」という結合が invalid** だった。
**後から数え直す直接測定（ARM B）が無ければ、P1 の FAIL を根拠に
「bond は保存されていない」と誤結論していた**（§10.2 の指示どおりに機械適用すると誤る）。

### 11.4 P2＝**bond は再起動を跨がない。ただし «設計どおり» で欠陥ではない**

- **静的**（§10.0(a)）：`PERSIST = 0`（`CONFIG_BT_NIMBLE_NVS_PERSIST` 未定義）＋
  `ble_store_nvs.c`/`ble_store_config_conf.c` は**リンク対象外**＋ELF に `nvs_open` 等 **0 個**。
- **実機**（ARM A/C）：真cold のたびに `STORE3 = 0xb0d50000`＝**store は毎回空から始まる**。
⇒ **RAM backed の当然の帰結**。**「残らないのが正常」**。NVS 化は**スコープ外＝ユーザー判断事項**（§11.6）。

**★正直な限定（交絡）**：**P2 の «実機での» 切り分けは P1 の故障に交絡されている**——
P1（鍵が在っても再接続が 517 で落ちる）が壊れている以上、**P2 の 517 は「鍵が消えたから」とは
断定できない**（同じ症状が両方で出る）。**P2 を «鍵の有無» で判定できたのは
STORE3 の直接測定（ARM C の `our=0`）のおかげ**であって、再接続の可否では判定していない。

### 11.5 ★D-2c/D-2d の主張の «正確な» 射程（§8/§9 の訂正）

§10.2 は「P1 FAIL なら D-2d を『暗号は実効／bond は不成立』へ格下げする」と定めたが、
**その前提（bond 不成立）は実測で否定された**（ARM B：`our=1 peer=1`）。よって
**機械的な格下げはせず、射程を精密化する**：

| 主張 | 状態 |
|---|---|
| `0xABF1`/`0xABF2`/`0xABF3`（D-2c） | **成立**（§8.3・本ラウンドでも再現） |
| `0xABF4` 暗号必須 READ が **fresh bond 後に** 通る（D-2d） | **成立**（§8.4・本ラウンド ARM B でも再現） |
| **bond（LTK）が device 側に保存される** | **成立**（ARM B `our=1 peer=1`）——§8.4 の「未証明」を**解消** |
| **保存された bond が «次の接続» で再利用される** | **★不成立（P1 FAIL）＝本ラウンドで新たに判明した実バグ** |
| bond が再起動を跨いで残る | **不成立。ただし RAM backed ＝設計どおり**（欠陥ではない） |

∴ **D-2d（暗号必須 READ＋bond 生成）は引き続き成立**。新たに分かったのは
**「bond の «再利用» ができない」**という **別の未解決欠陥**（§11.6-1）。

### 11.6 残課題・ユーザー判断事項

1. ~~**★実バグ：bond があるのに再接続で LTK を照合できない（P1 FAIL・reason=517）**~~
   → **★§13 で撤回＝実バグではなかった**。真因は **採取ハーネスの port-open による
   DUT リセット**（RAM の bond store が消える）。**capture を開かなければ P1 は PASS**。
   H1（RPA/IRK）も実測で死んだ（peer は public `<MAC-33>`）＝
   **C3 の PVCY ドラフトを C5 に当てる根拠は無い**。以下は当時の «仮説» の記録：
   **原因は未特定**（本ラウンドでは «鍵は在る» ところまでしか確定していない）。
   **有力な «仮説»（未検証・因果主張しない）**＝**アドレス解決/プライバシ（IRK・RPA）系**：
   `ble_sm_persist_keys()` は「identity address が得られればそれを鍵の保存キーにする」実装で、
   再接続時に central が RPA を使うと device 側は IRK で解決できないと照合に失敗しうる。
   C3 は同型の PVCY 系調査（`0x202D LE Set Addr Resolution Enable`／`0x204E LE Set Privacy Mode`、
   `docs/bt-shim.md`）と、**`their=no-ID / our=ID` で「PAIRING_COMPLETE status=0 だが bond件数0」**
   （`bt-shim.md:2434`）を記録しており、**C5 も同じ族の可能性**。
   **次の一手（反証実験を先に）**＝(i) 再接続時に device が受けている peer アドレス種別
   （RPA か identity か）を採取、(ii) `ble_store_config_find_sec` が何を照合して失敗するかを見る、
   (iii) C3 の PVCY ドラフト（`on_sync` で resolve 無効化）を C5 で A/B。
2. **NVS 永続化（P2 を PASS させる）はスコープ外＝ユーザー判断事項**。
   実装するなら `CONFIG_BT_NIMBLE_NVS_PERSIST` 定義＋`ble_store_config_conf.c`/`ble_store_nvs.c`＋
   `nvs_port.c` のリンク＋NVS パーティション。**ただし 1 が未解決のままでは
   「再起動を跨いだ bond」も使えない**（再利用そのものが壊れているため）＝**先に 1 を潰すべき**。
3. `TOPPERS_C5_BOND_COUNT_DIAG` は**既定 OFF**で温存（STORE3 へ `0xB0D5<our><peer>` を毎秒ミラー）。
   ★**教訓**：C5 では「変化時だけ syslog」する計装は**バースト欠落で消える**
   （実測：cold 直後の `our=0` 行が `done (host task continues in backgrou` の途中切れと同じ
   burst で失われ、以後 «変化なし» で無言になった）。**LP_AON へ毎周期ミラーする方が確実**。

### 11.7 非回帰（実測）

| ビルド | 結果 | hal 参照 | 備考 |
|---|---|---|---|
| `c5_ble_d2cd`（**既定＝DIAG OFF**） | build rc=0 | **0** | ELF に `BONDDIAG` 文字列 **0 個**＝計装は完全に消える |
| `c5_ble_bonddiag`（`TOPPERS_C5_BOND_COUNT_DIAG`） | build rc=0 | **0** | `BONDDIAG` 実在 |

BT 供給は不変（`ASP3_BT_IDF_V554=ON`＝真の v5.5.4 タグ submodule）。`asp3_core`/`hal` の改変ゼロ。

---

## 12. ★`reason=517` 真因追跡 予測（**実機を触る前に固定。測定後に改竄しない**）

> 節番号：コーディネータ指示は「§11 として追記」だが **§11 は既に bond 永続化の測定結果で使用済**の
> ため **§12** とする（内容＝指示どおり §11.6-1 の実バグ追跡）。

### 12.0 ★着手前の «無料» の実測（予測ではない。§12.1 の土台）

**(a) ★私の `517` のデコードが誤っていた（自己訂正）**

`esp-idf/.../nimble/include/nimble/ble.h:202-203` 実測：
```
BLE_ERR_AUTH_FAIL      = 0x05      ← 517 = BLE_HS_HCI_ERR(0x05) = «Authentication Failure»
BLE_ERR_PINKEY_MISSING = 0x06      ← これは 518。今回観測していない
```
⇒ **§11.2／memory／commit `112c7ee` に書いた「517 = PIN or Key Missing」は誤り**。
正しくは **「Authentication Failure」**。**意味が変わる**：「鍵が無い」ではなく
**「認証／暗号化手続きが失敗した」**＝`our=1`（鍵は在る）と**むしろ整合する**。
（`BLE_HS_ERR_HCI_BASE=0x200`・`BLE_HS_HCI_ERR(x)=0x200+x`＝`ble_hs.h:171,174` で確認）

**(b) H4（517 の発信元）＝ ほぼ静的に決着：**「**central（BlueZ）が切った**」

NimBLE 全ソースを検索すると `BLE_ERR_AUTH_FAIL` は **`ble_hs_hci.c:42`（定義）と
`:153`（ログ用の文字列テーブル）にしか出現しない**＝**NimBLE 側に 0x05 で terminate する
コードパスは存在しない**。∴ device が見た `DISCONNECT reason=0x05` は
**peer が送った LL_TERMINATE_IND の理由**＝**central が「認証失敗」と判断して切った**。
（実機で «device が terminate していない» ことの傍証も併せて採る）

**(c) 再接続時の LTK 照合パス（`ble_sm.c:1396-1416, 1590-1600`）**
```c
ble_sm_retrieve_ltk(ediv, rand, addrs.peer_id_addr.type, peer_id_addr, &value_sec)
  key_sec.peer_addr = {type, val}          ← ★identity address «で索く»
  rc = ble_store_read_our_sec(&key_sec, value_sec);
  if (rc != 0) return rc;
  if (value_sec->ediv != ediv || value_sec->rand_num != rand) return BLE_HS_ENOENT;  ← ★第2の関門
```
⇒ 失敗しうる箇所は **①`ble_store_read_our_sec` の照合（peer_addr 不一致）** と
**②`ediv`/`rand` 不一致** の2つ。失敗すると LTK nack → central が暗号化失敗 → 切断。

**(d) H1（RPA/IRK）は «host 側だけで» 既に強い逆風**

| | 実測 |
|---|---|
| BlueZ adapter | `Address=<MAC-33>` / **`AddressType = public`** |
| DUT の device object | `Address=<MAC-37>` / **`AddressType = public`** / `Paired=1 Bonded=1` |

⇒ **両側とも public＝RPA が登場しない**なら「RPA を IRK で解決できず不一致」は起こり得ない。
**ただし host 側プロパティだけでは «device が実際に受け取った peer アドレス» を証明しない**
（BlueZ が privacy 有効で RPA を使う可能性は `btmgmt` が root 必須で未確認）。**device 側で実測する**。

### 12.1 予測（★安い順に潰す＝H3/H4 → H1 → H2）

- **予測 H3：死（device は再起動していない）**。根拠＝store は RAM backed なので、再起動すれば
  `our` は 0 に戻る。**再接続失敗の «直後» に STORE3 を読んで `0xb0d50101`（our=1）なら、
  同一ブート内であることと鍵の存在が同時に示される**（＝H3 反証）。
- **予測 H4：death of "device が切った"＝central が切った**（12.0(b) の静的根拠）。
- **予測 H1：死**（12.0(d)）。device が受け取る peer アドレスは **public `<MAC-33>`**
  であり RPA ではない、と予測する。
- **予測 H2：生存＝真因はここ**。`ble_store_read_our_sec` が
  **(H2a) そもそも呼ばれない** か **(H2b) 呼ばれるが rc≠0（peer_addr 不一致）** か
  **(H2c) rc=0 だが ediv/rand 不一致で `retrieve_ltk` が ENOENT** のいずれか。
  **どれかは «測ってから» 言う**（現時点で1つに賭けない）。

### 12.2 ★反証条件（＝この含意が本当に成立するかを自問してから書く。§10.2 の失敗を繰り返さない）

前回 §10.2 は「P1 FAIL ⇒ `our=0` は実体」という **成立しない含意**を書いた。今回は
**各仮説の «直接の観測量» だけで判定し、仮説間の含意を連鎖させない**：

| 仮説 | 判定に使う直接の観測量 | 死ぬ条件 | 生きる条件 |
|---|---|---|---|
| H3 | 失敗直後の **STORE3** | `0xb0d50101`（our=1）＝同一ブート・鍵在り | `0xb0d50000` なら **H3 生存**（再起動していた＝鍵喪失は当然） |
| H4 | NimBLE の terminate 呼出の有無（静的）＋ device 側 disconnect reason | 0x05 の emitter が NimBLE に無い | — |
| H1 | **device 側で受けた peer アドレスの «type»** | `type=public(0)` かつ addr=BlueZ の public | `type=random(1)` かつ上位2bit=0b01（RPA）なら **H1 生存** |
| H2 | **store_read(OUR_SEC) の «呼出有無・照合キー・rc»** | 呼ばれて rc=0 かつ ediv/rand 一致 | 呼ばれない／rc≠0／ediv-rand 不一致 なら **H2 生存**（どの枝かも同時に判る） |

★**「H1 が死んだら H2 が真因」とは書かない**（H1/H2 以外＝未知の H5 が残りうる）。
**H2 の観測量が «正常» だったら、真因は未特定として報告する**（無理に成功を作らない）。

### 12.3 計装（`TOPPERS_C5_LTK_DIAG`・**既定 OFF**・非回帰を実測）

**submodule（`esp-idf`）は編集しない**（CLAUDE.md 禁則）。**app 層だけで測る**：
`ble_store_config_init()` が `ble_hs_cfg.store_read_cb = ble_store_config_read` を張った «後» に
**アプリ側ラッパで包む**（＝NimBLE の照合キーと rc を非侵襲に覗ける唯一の合法な窓）。

- **STORE9**＝直近の `store_read(OUR_SEC)`：`0x5A<<24 | count(4) | addr_type(4) | rc(8) | addr[0](8)`
- **STORE8**＝**保存済** our_sec[0] の素性：`0x57<<24 | type(4) | addr[0](8) | addr[5](8) | ediv/rand が非0 か(2)`
- **STORE3**＝bond 件数（§11 の `TOPPERS_C5_BOND_COUNT_DIAG` を流用）
- **GAP CONNECT 時の peer アドレス**（`ble_gap_conn_find` の `peer_id_addr`/`peer_ota_addr`）も記録

★**LP_AON へ無条件ミラーを主判定**にする（syslog は補助）。§11.6-3 の教訓＝C5 では
「変化時だけ syslog」はバースト欠落で消える。

---

## 13. `reason=517` 実機測定（2026-07-17）— **★真因＝私の «採取ハーネス»。§11 の「実バグ」を撤回する**

### 13.1 結論（先に書く）

**`reason=517`（Authentication Failure）に device 側のバグは無い。**
真因は **`tmp/c5_cold_passive_capture.py` が CP2102N(UART0) を open する行為そのものが
DUT を «リセット» すること**。連鎖：

```
capture の port-open → CP2102N の DTR/RTS が EN を駆動 → C5 リブート
  → RAM backed の bond store が消える（our=1 → 0）
  → 再接続で LTK 照合が ENOENT → device が LTK negative reply
  → central(BlueZ) が «Authentication Failure(0x05)» でリンクを切る ＝ reason=517
```

⇒ **§11.2／§11.6-1 の「bond の再利用ができない＝新たに判明した実バグ」は撤回する。**
**P1（LTK 再利用）は実際には PASS**（13.3）。

### 13.2 予測の答合せ（§12.1）

| 予測 | 結果 |
|---|---|
| **H3：死（device は再起動していない）** | **★外れ＝H3 は生存し、しかも «全部» の原因だった** |
| **H4：central が切った** | **的中**（NimBLE に `BLE_ERR_AUTH_FAIL` の emitter が無い＝静的） |
| **H1：死（peer は public）** | **的中**（13.4） |
| **H2：生存＝真因** | **★外れ＝照合機構は «健全»**（13.4） |

**私が「安い」と判断して先に置いた H3 が正解だった**——が、**§11 の時点では H3 を潰さずに
「実バグ」と書いてしまっていた**（コーディネータが H3 を挙げたのは正しかった）。

### 13.3 ★決定的 A/B：唯一の差分は «capture を open したか» だけ

同一 bond・同一ブート・同一コードで、**capture の open だけを反転**：

| アーム | 差分 | P1（再接続して `0xABF4` を読む） |
|---|---|---|
| **X（capture 無し）** | — | **PASS 2/2**：`connected=1 resolved=1 paired=1 bonded=1`／`ABF4 → 42 54 34 2d 4f 4b ("BT4-OK")` |
| **Y（capture を 4秒 open しただけ）** | **port-open のみ** | **FAIL**：`connected=0`／`ABF4 → org.bluez.Error.Failed` |

**★ARM Y の capture 自身が «リセットの瞬間» を録画していた**（これが物証）：
```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
```
＝**bond 済みで走っている DUT に capture を当てた «だけ» で ROM から再起動している**。

**ハーネスを直した確認**（capture を **P0 の «前» に1回だけ open して P0→P1 を通して保持**）：
```
[P0 after pairing] paired=1 bonded=1 ／ ABF4 → "BT4-OK"
[P1 after connect] connected=1 resolved=1 paired=1 bonded=1
ABF4 0xABF4 READ -> 42 54 34 2d 4f 4b  (b'BT4-OK')      ← ★再接続で LTK 再利用 成功
（device 側）
LTKDIAG read OUR_SEC n=47 type=0 addr0=0xbd rc=0        ← ★rc=0＝鍵が «見つかって» 再利用された
boot banners in window: 1                                ← リセットは capture-open の1回だけ
```
⇒ **P1 = PASS**。**`n=47`**（カウンタが伸びている＝無再起動）と **`rc=0`** が
壊れていた側の **`n=1` / `rc=5`**（カウンタ 1 へ巻戻り＝再起動、ENOENT）と**真逆**。

### 13.4 H1／H2 の «直接の観測量» による判定（§12.2 の表どおり・含意を連鎖させない）

**H1＝死**：device 側で受け取った peer アドレスを実測（app 層 `ble_gap_conn_find`）：
```
LTKDIAG conn id_addr type=0 [0]=0xbd [5]=0x8c
```
＝**type=0（public）**・`<MAC-33>`（BlueZ の public アドレス。BLE は little-endian
なので val[0]=0xBD／val[5]=0x8C）。**RPA ではない**⇒「RPA を IRK で解決できず不一致」は成立しない。
∴ **C3 の PVCY ドラフトを C5 に当てる根拠は無い**（§12 の「H1 が生き残ってから」に従い、当てない）。

**H2＝死（照合機構は健全）**：`store_read(OUR_SEC)` の «照合キーと rc» を app 層ラッパで実測：

| 状況 | 実測 | 解釈 |
|---|---|---|
| 再接続（リセット無し） | `n=47 type=0 addr0=0xbd **rc=0**` | **正しいキーで索き、鍵が見つかる**＝機構は正常 |
| 再接続（capture がリセット後） | `n=1 type=0 addr0=0xbd **rc=5**` | キーは «正しい» が store が空＝ENOENT |
| `ble_store_util_count` の iterate | `type=0 addr0=0x00 rc=5` | `BLE_ADDR_ANY` での列挙終端＝**正常なノイズ**（誤読しない） |

⇒ **照合キー（`type=0`/`addr0=0xbd`）は保存側と一致しており、H2a/H2b/H2c いずれも «バグではない»**。
`rc=5` は **store が空だったことの帰結**であって原因ではない。

**H3＝生存＝全部の原因**。物証は4つ：(i) `LTKDIAG` の読み出しカウンタが **7→1 に巻戻る**、
(ii) `BONDDIAG bonds our=0`（保存済みが消えている）、(iii) ブート時にしか出ない
`done (host task continues in background)` が P1 の窓に現れる、(iv) 13.3 の A/B と ROM バナー録画。

**H4＝central が切った**：NimBLE 全ソースで `BLE_ERR_AUTH_FAIL` は
`ble_hs_hci.c:42`(定義) と `:153`(ログ文字列) のみ＝**0x05 で terminate するコードパスが無い**。
∴ device が見た `reason=0x05` は **peer(BlueZ) が送った LL_TERMINATE_IND の理由**。
device が LTK nack を返した結果として central が «認証失敗» と判断した——13.1 の連鎖と整合。

### 13.5 ★P2 の «交絡を除いた» 再測定＝FAIL。ただし設計どおり

§11.4 では「P2 は P1 の故障に交絡されて実機判定できない」と書いたが、**P1 が harness 由来と
判明した今、capture 無しで P2 を «清潔に» 測れる**：

```
（bond 確立：paired=1 bonded=1・ABF4 → "BT4-OK"）
→ 真の電源断（-l 1-6 -p 3-4 off／読み戻し C5#1=0 で power 断を実証）→ 復電
→ 再接続（remove せず・capture 無し）
[p2 after connect] connected=0 resolved=0 paired=1 bonded=1
ABF4 0xABF4 READ -> org.bluez.Error.Failed
```
⇒ **P2 = FAIL＝bond は真の電源断を跨がない**。**RAM backed（§10.0(a)：`PERSIST=0`・
`nvs_open` 等 0 個）の当然の帰結＝「残らないのが正常」**。§11.4 の結論は維持（交絡は解消）。

### 13.6 ★★方法論の重大な訂正（§4.1 の自己発見を，自分で上書きする）

**`rst:0x1 (POWERON)` は «真の電源断» の証明にならない。**
13.3 の ARM Y が示すとおり、**capture の port-open による EN リセットも
`rst:0x1 (POWERON)` を出す**（ESP32 では EN リセットと電源投入が同一シグネチャ）。

- §4.1 で私は「usbjtag は open が DUT をリセットするから cold を観測できない。UART0 なら
  `rst:0x1 (POWERON)` が取れる」と書いたが、**正確には UART0 も open で DUT をリセットする**。
  **違うのは «リセットの signature» だけ**（UART0＝`rst:0x1 POWERON` ／ usbjtag＝`rst:0x15 USB_UART_HPSYS`）。
  **UART0 の方が «電源断と区別がつかない» ぶん、むしろ質が悪い**。
- **では過去の「真cold」主張は無効か → 無効ではない**。私は毎回
  **`uhubctl off` → `ls /dev/serial/by-id | grep -c A7:44` = 0 の読み戻し**で
  **電源が実際に落ちたこと自体を実証**している。**cold の証明はこの読み戻しであって、
  バナーではない**。バナーは整合的だが証明力を持たない（＝今後はそう扱う）。
- ⇒ **D-1／W2／D-2c／D-2d の «真cold 達成» の結論は維持**（電源断は読み戻しで実証済み。
  capture-open による追加の EN リセットは «電源断の後» に起きるので warm 残留を持ち込まない）。

### 13.7 ★§8.6 の «原因未特定» が特定された

§8.6 で「長時間 OTA セッション中に app が一度再起動した（STORE8 の connect count が
~7 接続に対し 1）。**原因未特定・非ブロッキング**」と書いた件は、**本ラウンドで原因確定**＝
**同じ capture port-open のリセット**。当時「非ブロッキング」と判断したが、**実際には
その後の §11 の結論を丸ごと汚染していた**（＝«非ブロッキング» の見立てが誤りだった）。

### 13.8 恒久対策（ハーネス側。product コードの修正は不要）

**device 側に修正は要らない**（バグが無いため）。直すべきは採取手順：

1. **capture は «測定対象の状態が始まる前» に1回だけ open し、測定を通して保持する**
   （13.3 で実証：P0 の前に open → P0→P1 を通して保持 → P1 PASS ＋ device ログも取れる）。
2. **状態を跨ぐ測定（bond 再利用・カウンタ・RAM 保持）の «途中» で capture を開閉しない**。
3. `tmp/c5_cold_passive_capture.py` の docstring に上記を明記した（下記 13.9）。
4. どうしても途中で読みたい場合は **LP_AON マーカ＋`esptool read-mem`**（ただしこれも
   `--before usb-reset` で ROM download へ落とすので **app は止まる**＝測定の «最後» に使う）。

### 13.9 非回帰（実測）

| ビルド | 結果 | hal 参照 | 備考 |
|---|---|---|---|
| `c5_ble_d2cd`（**既定＝全 diag OFF**） | build rc=0 | **0** | ELF に `LTKDIAG` **0 個**・`BONDDIAG` **0 個** |
| `c5_ble_ltkdiag`（`TOPPERS_C5_LTK_DIAG`＋`TOPPERS_C5_BOND_COUNT_DIAG`） | build rc=0 | **0** | `LTKDIAG` 実在 |

BT 供給は不変（`ASP3_BT_IDF_V554=ON`）。`asp3/asp3_core`・`hal`・`esp-idf`（submodule）の改変ゼロ
——**NimBLE の照合キーは submodule を触らず app 層で `ble_hs_cfg.store_read_cb` を包んで覗いた**。

### 13.10 最終状態（D-2c/D-2d の射程・§11.5 を更新）

| 主張 | 状態 |
|---|---|
| D-2c（`0xABF1` READ／`0xABF2` NOTIFY／`0xABF3` WRITE） | **成立** |
| D-2d（`0xABF4` 暗号必須 READ が fresh bond 後に通る） | **成立** |
| device 側 bond store への鍵の保存 | **成立**（`our=1`） |
| **保存された bond が «次の接続» で再利用される** | **★成立（P1 PASS）**——§11 の「不成立」を**撤回** |
| bond が **真の電源断** を跨いで残る | **不成立。RAM backed ＝設計どおり**（NVS 化はユーザー判断事項） |

⇒ **C5 BLE に既知の未解決バグは «無い»**（残るのは「NVS 永続化を入れるか」という設計選択のみ）。

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

（以下、測定結果は §4 以降に追記）

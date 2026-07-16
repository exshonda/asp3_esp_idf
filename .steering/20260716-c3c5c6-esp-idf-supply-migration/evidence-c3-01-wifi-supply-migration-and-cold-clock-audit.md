# C3 evidence-01 — WiFi供給を esp-idf submodule へ移行（**hal参照0達成**）／★真cold クロック監査＝**C6型の罠は «該当しない»**（静的確定）

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
toolchain: Espressif `riscv32-esp-elf` **esp-15.2.0**（`~/.espressif/tools/...`）
**実機には一切触れていない**（別エージェントがC6実機測定中のため。実機検証は次ラウンド）

---

## 0. 結論（先に4行）

1. **C3 WiFi は供給移行完了＝`ninja -t deps` の hal 参照 **0**、かつ**リンク行（`-L`/`-T`＝blob・ROM ld）も 0**。可逆性・非回帰も実測（7構成）。
2. **`~/tools/esp-idf`（版が不定の外部tree）への参照は全7ビルドで 0**。C3の外部絶対パス2箇所を撤去。
3. **★§2（真cold クロック）＝C3は C6型の «stock がやっているのを我々がやめた» 罠に «該当しない»**。C6のバグは「ASP3が何も書かずwarm残留に依存」だったが、**C3はmuxを毎ブート無条件に書く**（`target_kernel_impl.c:91-94`）＝構造的に成立しない。**ただしC3のコメントは «理由» が事実として誤り**（ROMはBBPLLに一切触れない）＝**コメントのみ訂正**（バイナリはbyte-identicalで実証）。
4. **★新規の壁はゼロ**——踏んだ版差3件はすべて C5/C6 が既に解いた型の転写で解決。

---

## 1. ★provenance：`~/tools/esp-idf` は **v5.5.4ではない**（親の警告を実測で追認）

```
$ git -C esp-idf describe --tags            → v5.5.4   （735507283d）★真のv5.5.4
$ git -C ~/tools/esp-idf describe --tags    → v5.5     （8c750b08＝v5.5.0）
$ git -C ~/tools/esp-idf-v6.1 describe      → v6.1-beta1
```

| tree | 実体 |
|---|---|
| repo submodule `esp-idf/` | **v5.5.4 タグ＝`735507283d`**（＝真のv5.5.4） |
| `~/tools/esp-idf`（変数名 `IDF_V554`） | **v5.5(=v5.5.0, `8c750b08`)** |
| `hal/` | `b90b1837` |

C3 が持っていた外部絶対パスは**2箇所**（いずれも「v5.5.4」を名乗る）：
- `esp_wifi.cmake:222` `set(IDF_V554 /home/honda/tools/esp-idf)`
- `esp_bt.cmake:53` `set(BT_IDF /home/honda/tools/esp-idf)`

⇒ **同じパス名がPCごとに別版**。両方とも撤去し、`IDF_V554` は `target.cmake` で
**submodule 相対**（`${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf`）に定義（C5と同型）。

### 1.1 ★C3固有の朗報：`idf_v554_override/` は **存在しない**（C6の罠は無い）

- C6 で有害だった `idf_v554_override/`（自称 verbatim だが実体は `_wifi_disable_ac_ax` を足したもの＝`_magic` 484→488 ずれ）は、**C3には存在しない**（実測＝該当ディレクトリ0件・`wifi_os_adapter.h` のシャドウも0件）。
- 構造的な理由も実測で確認：**C3 は `CONFIG_SOC_WIFI_HE_SUPPORT` を持たない**ため、HEフィールド差の罠が**発生しようがない**。
- 実測（`c3_idf` の elf）：`g_wifi_osi_funcs` **size=0x1e0=480**・`_magic`(0xdeadbeaf) は **offset 476**。
  （C5/C6 は 488／magic 484＝**C3は構造そのものが別**＝HE分がそもそも無い。）

---

## 2. ★★§2 静的調査：C3に「ROMが設定済みだから触らない」判断は在るか（依頼 (d)）

**結論：在った。そして «判断（コード）» は正しく、«理由（コメント）» が誤っていた。**
**C6型の罠には該当しない**（＝深追いせず、コメント訂正のみに留めた）。

### 2.1 該当箇所（原文）

`asp3/target/esp32c3_espidf/target_kernel_impl.c:83-89`（訂正前）：

> CPUクロックをPLL（160MHz）へ切り替える／Direct Boot では CPU はリセット既定の
> XTAL/2＝20MHz のまま起動する（**実機dlynse計測で確認**）．**BBPLL（480MHz）は
> ROMブートローダがブート時に有効化したものを流用する**（SPI_FAST_FLASH_BOOT
> 経路ではROMがPLLを使用している）．

★**C6 evidence-c6-04 のコメントが「C3のBBPLLと同様，SPI_FAST_FLASH_BOOT経路でROMが
既に有効化・設定したものを流用」と C3 を引き合いに出していた＝C3が «原型» だった**
という親の見立ては**当たっていた**（同じ文言・同じ論法）。

### 2.2 ★しかし C6 と違い、C3 は構造的に安全（**2つの独立な理由**）

| | C6（バグ有り） | **C3** |
|---|---|---|
| CPU clk mux（`SOC_CLK_SEL`）を書くか | **書かない**（「ROMが設定済み」と決め打ち）→ warm残留に暗黙依存 | **毎ブート無条件に書く**（`:91-94`）＝**残留に依存する余地が無い** |
| 依存している «残留» の中身 | PCR `SOC_CLK_SEL`（POR既定＝XTAL＝**不都合な側**） | BBPLL enable（OPTIONS0．POR既定＝**有効＝好都合な側**） |

⇒ **C6のバグは「ASP3が何も書かなかった」こと。C3は書いている。** ∴同型ではない。

### 2.3 stock C3 `rtc_clk_init()` vs ASP3-C3 の差分（依頼 (d)．**PROVEN/UNKNOWN で分類**）

stock＝`esp-idf/components/esp_hw_support/port/esp32c3/rtc_clk_init.c` ＋ `rtc_clk.c`。
ASP3＝`target_kernel_impl.c:62-131`。**ASP3は stock の関数を1つも呼ばない**（実測：
`regi2c_ctrl_ll_i2c_reset`／`regi2c_ctrl_ll_i2c_bbpll_enable`／`rtc_clk_cpu_freq_set_config`／
`rtc_clk_bbpll_configure`／`rtc_clk_bbpll_enable`／`rtc_clk_init` すべて参照0）＝
**レジスタmuxを直書きしているだけ**。

| # | stock の段 | ASP3-C3 | 真coldで効くか | класс |
|---|---|---|---|---|
| 1-3 | `SCK_DCAP`/`CK8M_DFREQ`/各divider | 無し | **No**＝RTC_SLOW/RC_FAST精度のみ。ASP3はSYSTIMER(XTAL系)基準 | LIKELY |
| 4 | `regi2c_ctrl_ll_i2c_reset()` | 無し | **No**＝PORではi2cバスは定義上リセット状態 | LIKELY |
| 5 | `regi2c_ctrl_ll_i2c_bbpll_enable()`（ANA_CONFIG bit17） | 無し | **UNKNOWN**＝当該bitのPOR既定が手書きヘッダに未記載。BBPLLの«再設定»を阻むだけで«走行»は阻まない | **UNKNOWN** |
| 6 | `rtc_clk_xtal_freq_update(40)`（STORE4） | 無し | **No**＝getterは壊れ値なら40M既定へfallback（`rtc_clk.c:358-365`）。C3 blobに参照者無し | LIKELY |
| 7 | `rtc_clk_apb_freq_update()`（STORE5） | 無し | **cold/warm差は無い**が全ブートでstale（ROMが20MHz値を書く）。**blobに`rtc_clk_apb_freq_get`未定義参照は無い**＝消費者不在＝潜在のみ | LIKELY |
| **8** | **`rtc_clk_bbpll_enable()`** | 無し | **No＝PROVEN**：`BBPLL_FORCE_PD`(bit10)/`BBPLL_I2C_FORCE_PD`(bit8)/`BB_I2C_FORCE_PD`(bit6) は **すべて `default: 1'b0`**（`soc/rtc_cntl_reg.h:101,113,125`．**私が原文を直接確認**）。`clk_ll_bbpll_enable()` はこの3bitを**クリアするだけ**（`hal/esp32c3/clk_tree_ll.h:67-71`）＝**POR既定では no-op** | **PROVEN** |
| **9/11** | BBPLL周波数(480M)＋CPUPERIOD_SEL | **有り**（`:91-92`） | n/a．`SYSTEM_PLL_FREQ_SEL`(bit2) は **`default: 1'b1`＝480M**（`soc/system_reg.h:55`．原文確認） | **PROVEN** |
| **10** | `rtc_clk_bbpll_configure()` の**アナログ側**＝regi2c 8本（div_ref/dcur/dbias等） | **無し** | **UNKNOWN＝唯一の実質的残件**。BBPLLは**工場アナログ既定**で走る。起動実績はあるが温度/電圧マージン未検証。**cold/warmの問題ではない** | **UNKNOWN** |
| 12 | `clk_ll_cpu_set_divider(1)`（PRE_DIV_CNT=0） | 無し | **No（低risk）**＝PRE_DIVはXTAL/RC_FAST経路用。PLL経路はCPUPERIOD_SELから導出（`rtc_clk.c:275-280`） | LIKELY |
| **13** | **`clk_ll_cpu_set_src(PLL)`（`SOC_CLK_SEL=1`）** | **有り**（`:93-94`） | n/a．**★これの欠落こそがC6のバグだった。C3は毎ブート書く** | **PROVEN** |
| 14 | `esp_rom_set_cpu_ticks_per_us(160)` | **有り**（`:114`） | n/a（既に修正済） | **PROVEN** |
| 15-17 | ccount rescale／`rtc_clk_8m_enable`／`fast/slow_src_set`／`32k_enable` | 無し | **No**＝ASP3はRTC fast/slow・sleepを使わない | LIKELY |

**ROM側のPROVEN**（`esp32c3_rev3_rom.elf` を nm／逆アセンブル）：
- ROMは **`rtc_clk_*` のクロックAPIを1つも持たない**（`rtc_clk_bbpll_enable`/`rtc_clk_cpu_freq_set*` **不在**。在るのは `rtc_boot_control`/`rtc_get_reset_reason`/`rtc_select_apb_bridge` 等のみ＝**私がnmで直接確認**）。
- ブート経路 `main→boot_prepare→ets_run_flash_bootloader→ets_run_direct_boot` はcache/MMU初期化＋ジャンプのみ＝**regi2cトランザクションを1回も行わない**。
- （`rom_phy_bbpll_cal` シンボルは**存在する**が、ROM内に呼出し元は無い＝**PHY blob向けのexport**。）
- ⇒ **「ROMがBBPLLを有効化した」は事実として偽**。実体は**シリコンのリセット既定**。

**独立クロスチェック**（コメントの「XTAL/2=20MHz・`s_ticks_per_us`=0x14」を裏から検算）：
`SYSTEM_SOC_CLK_SEL` 既定 `2'd0`(XTAL) ＋ `SYSTEM_PRE_DIV_CNT` 既定 `10'h1` ⇒ 40MHz/2 = **20MHz** ⇒ `s_ticks_per_us`=20=0x14＝**実機JTAG実測値と一致**。

### 2.4 ★実施した修正＝**コメントのみ**（コードは無変更）

- `target_kernel_impl.c:83-89` の「ROMが有効化したものを流用」を、上記のPROVEN/UNKNOWN付きの
  正確な記述へ差し替えた。**`asp_flash.bin` が byte-identical（`93cad396…`）であることで
  «コメントのみ» を実証**。
- ★**C6の `cold_clk_init_c6.c` を反射的に移植してはならない**と明記した：
  C3のBBPLLは既定で上がっているため無益で、かつ **C6が真coldで実際に踏んだ
  «BBPLL較正のregi2c完了待ち無限スピン»（evidence-c6-04 #9）を持ち込むriskがある**。
  ＝**「効く保証のない修正を予防的に入れる」ことこそが新しいバグの源**。

---

## 3. ★移行前の既定は **本PCで壊れていた**（C6 §3 と同型・pre-existing）

移行前の C3 既定 `-DESP32C3_WIFI=ON` は**リンク失敗**（私の変更前のbaselineで実測）：

```
ld: multiple definition of `esp_wifi_skip_supp_pmkcaching';
    asp3/target/esp32c3_espidf/wifi/esp_shim_blobglue.c:411: first defined here
```

供給が**混成**だったため（ヘッダ=hal／blob=`~/tools/esp-idf`＝本PCでは**v5.5.0**）。

真因（nm 実測）＝`#if ASP3_WIFI_BLOB_V554` が守っていた3関数スタブの前提が、
**本PCのどのツリーにも当てはまらない**：

| tree | `skip_supp_pmkcaching` | `sta_get_ie` | `wpa3_compat` |
|---|---|---|---|
| submodule（**真のv5.5.4タグ**） | **定義あり** | なし | なし |
| hal | 定義あり | あり | あり |
| **v6.1** | **なし** | あり | あり |
| `~/tools/esp-idf`（v5.5.0） | **定義あり** | なし | なし |

⇒ 「3関数すべてを欠く」ツリーは **+1169（≡v6.1系）＝元PCの `~/tools/esp-idf`** だけ。
**「v5.5.4」という名前の嘘が、ソースの `#if` ガードにまで伝播していた**（C6 §3 と同一結論）。

**対処**＝`esp_wifi_skip_supp_pmkcaching` のスタブを**撤去**（C6が先行実施した同一修正の転写）。
残り2関数はガード内に残置（hal fallback用。esp-idf供給では未参照→`--gc-sections`で脱落＝★D5解消）。

---

## 4. ★blob md5 実測表（C3）— 依頼 (a)

```
file          submodule(v5.5.4tag)              hal(b90b1837)                     v6.1-beta1                        ~/tools/esp-idf(v5.5.0)
--- WiFi ---
libnet80211.a 81a3ec5bbc40d9a739110cd2b7d82f84  79e5066fbb9ba8aa16fec42083e6ad7d  4428d5aa768913b2d2a62b135c92ead0  42220ba40ccf5e912a5ca2f66245e5fb
libpp.a       164aa99906586c95faa9021af76f4aca  96fc359838938220b141b118cc8598cc  9b90cc76f8746add853f92c997acee05  901e28f5d14f7cc3f13c3a82e56ff51e
libespnow.a   6255fe8ec4bb87a8a14c894e1e113d72  9cc396a338e2187f55102fce23c414e3  69f8c7b53c3b33b8978beaa4a624bcb8  4aee80a50e25c1e4377da9d41abf5fe0
libmesh.a     32a75dd4ebd1ce55afac338aee4e5b57  f1dda0cb1efade91ea6d862dce845008  b56298f4c201c1ebb170b3405e02bb62  a0e748d43f77a8159068e6dd273354ec
libsmartconf. ebf0fd54edfc1fe267857a5dfdf005b6  4437536d39ed77267e956bc3b7c9736d  785c896e1e8050aebedbcd378b9fe42e  fe157d3649b9324b64a87bec8446ea8c
libwapi.a     16c72d3df6603d79b99e356d5f231324  aa4fbee89f68755fb0ae93e965e0184a  e7bd2fb3bd4689d81747c86e8482e117  af5a426b808f43452805890dcbfc0bb0
libcore.a     0958841f674baa275a6a6dc29865696a  952499526ce64f2a296733ed2838a5b3  643b90498ebed0132aa609d6859ad85b  b092720dfdc6cc8a6c29c3c46b43f2d3
--- PHY/coex/BT ---
libphy.a      a401b5bbcbd619a15d77f8d936c9cfe0  a401b5bbcbd619a15d77f8d936c9cfe0  a51adbdc7ad18a2411061c37b915383f  7aa5591e0de5a0caface02735093ae53
libbtbb.a     463253285ae81adeb942455c7f08f86f  463253285ae81adeb942455c7f08f86f  daa743ddae3ba5793944f70b740e3cfc  dcae9e14ee8b05d26c8cf09e98770856
libcoexist.a  e854dd6a17e7eb9d056441af30fd7a21  e854dd6a17e7eb9d056441af30fd7a21  9f44a45c3da1eb057f098ddf23e4ca42  ecb81e9127e01a2553bed0a7c152986c
libbtdm_app.a 859e8c8edd8906a53b4116817cd3af8a  dfdadb9ddc12eeeab85edfb5d26eb4bf  d9753a31a8eeac9da8f3718cdfdb4938  93abf3c70f84c82347dd900069f031d3
```

### 4.1 そこから言えること（依頼 (a)）

1. **WiFi：submodule ≠ hal（7/7 相違）**。⇒ WiFi の移行は**実質的な版統一**であり意味がある（C6と同じ）。
2. **PHY/btbb/coex：真のv5.5.4タグ ≡ hal（3/3 バイト完全一致）**（C5/C6と同じ構造）。
3. **★BT controller は C6 と構造が違う**：`libbtdm_app.a` は **submodule ≠ hal ≠ v6.1 の3つ巴**。
   ⇒ **C6では「BT を v5.5.4 へ移す」＝「hal へ戻す」と同義**（4/4一致）だったが、
   **C3では真の A/B が成立する**＝「v5.5.4統一」に**実体がある**。
4. **★★旧記録（`esp_bt.cmake:37-38`）の「決定的事実（md5実測）」を反証**——
   **旧記録自身の数値がprovenance の罠を証明している**：

   | | 旧記録の主張 | 本ラウンドの実測 |
   |---|---|---|
   | `libbtdm_app.a` hal | `dfdadb9d…` | `dfdadb9d…` **一致**（旧記録のhal測定は正しい） |
   | `libbtdm_app.a` 「v5.5.4」 | `d9753a31…`（＝v6.1と同一と記載） | **`d9753a31…` は本PCの v6.1 の値そのもの** |
   | `libbtdm_app.a` 真のv5.5.4タグ | （記載なし） | **`859e8c8e…`＝旧記録に一度も現れない第3の値** |
   | 「libphy/libbtbb も hal相違」 | hal相違 | **偽**＝真のタグは **hal とバイト一致** |

   ⇒ 旧記録が「v5.5.4」と呼んでいたツリーは **+1169（≡v6.1系）**。
   **C6 evidence-c6-01 §2.1-3 と完全に同じ結論が、C3側でも独立に再現した。**

> ★nm のシンボル数で機能可否を判断していない（PROMPT §6・rigor）。md5 は「同一バイナリか」の同定にのみ使用。

### 4.2 ★測定上の注意（誤読しかけたもの・正直に残す）

`libcore.a` は **submodule の esp32c3 と esp32c6 で md5 が完全一致**（`0958841f…`）。
一瞬「測り間違い（symlink等）か」と疑ったが、**実体は 4108 B の準空アーカイブで
中身は `misc_nvs.o` 1本のみ**（`ar t` で実測）＝**チップ非依存だから一致して当然**。
⇒ **md5一致を「同じblobだ」と意味づける前に、そのファイルが実質空でないか確認すること**。

---

## 5. 踏んだ版差と解き方（依頼 (c)）— **新規の壁はゼロ（3件すべてC5/C6の転写）**

| # | 壁（実測） | 解き方 | 出典 |
|---|---|---|---|
| 1 | `esp_event.h:12` が `freertos/FreeRTOS.h` を要求（hal は `platform/os.h` に置換＝NuttX向けにFreeRTOS依存を剥がしてある） | 既存のBT用FreeRTOS互換スタブ `bt/stub/include` を**後ろに**追加（同居する `bt_nimble_config.h`/`esp_partition.h` を誤シャドウしないため）。**★C3のスタブは元々ここに在る**（C5/C6が参照していた本体） | C6 §4-4 |
| 2 | `esp_event_post()` の `event_data` が hal=`void *` / esp-idf=**`const void *`** → `conflicting types` | `esp_wifi_adapter.c` の局所 extern を `#ifndef TOPPERS_ESPIDF_SUPPLY` で止め、esp-idf供給時は本物の `esp_event.h` を使う | C5 §6／C6 §4-5 |
| 3 | **`shared_periph_module_t` / `soc_root_clk_circuit_t` 未定義**（BTビルド）＝hal の `esp_hw_support` ヘッダが esp-idf の `soc/` 定義を見る | **「混ぜない」ことで回避**＝`ASP3_ESPIDF_SUPPLY` の既定を **`ESP32C3_BT=ON` のときだけ OFF**（検証済みの全hal構成を維持） | C6 §6／HANDOFF §4-3-5 |
| — | mbedtls **4.0.0(tf-psa) → 3.6.5(classic)** の版ダウン | C6の写像を1:1転写（`pk_rsa.c`/`tf_psa_crypto_{config,version}.c` 除外、`version.c`/`psa_crypto_aead.c`/`entropy_poll.c`/`bignum_mod{,_raw}.c` 追加、port は `md/esp_md.c` 追加＋`-Wl,-u,mbedtls_psa_crypto_init_include_impl` を外す） | C5 §3／C6 §4-2 |
| — | `common.h` shadow（3.6.5 の `library/common.h` が wpa の `src/utils/common.h` と同名） | `mbedtls/library` を **wpa の後ろ**へ | C5 §3.4 |
| — | `sdkconfig.h` は Kconfig生成物で esp-idf に**存在しない** | hal の `nuttx/esp32c3/include/sdkconfig.h` を `sdkconfig_stub/` へ **verbatim vendor**（**CONFIG_* は1ビットも変えていない**＝body diff 0・720個で一致を実測） | C6 §4-1 |

> ★一般則の再確認：**ヘッダとソースを揃えて同じ供給元から取ればリネーム/版差問題は消滅する。片方だけ移すと詰む。**
> C3では `esp_hal_*` 8分割の吸収（`ESP_SUP_HAL_<x>`）で 6箇所を写像。

### 5.1 ★ABI skew の解消（本移行の主目的）

C3 v5.5.4タグ blob が要求する md5 集合（`strings` で実測．**C6と同一＝WiFiヘッダはチップ非依存**）：
`0947e8b 2331a76 47fd1b7 56b5fed 6253add 6eaa5ad 7937614 80e5949 a78adff ce069d3 dae1625`

| ヘッダ | hal | **esp-idf（移行後）** | blobの要求集合に居るか | 判定 |
|---|---|---|---|---|
| `wifi_os_adapter.h` | 6eaa5ad | **6eaa5ad** | 両方 IN | 元から一致 |
| `esp_wifi.h` | 9f7e672 | **a78adff** | hal=**not-in** / idf=IN | **解消** |
| `esp_wifi_types_generic.h` | 6773bf5 | **dae1625** | hal=**not-in** / idf=IN | **解消** |
| `esp_wifi_types_native.h` | ce069d3 | **ce069d3** | 両方 IN | 元から一致 |
| `esp_wifi_driver.h` | 50fc486 | **2331a76** | hal=**not-in** / idf=IN | **解消** |

⇒ **esp-idf 供給で 5/5 一致**（hal ヘッダでは **3/5 不一致**だった）。

**★これは `esp_wifi.cmake:213-218` の旧コメントを反証する**：
> 「os_adapter ABI（wifi_os_adapter.h）はC3の場合 hal/v5.5.4 でバイト同一…
>  ＝**ヘッダ差し替え不要で blob(.a)のみ差し替えれば足りる**」

**`wifi_os_adapter.h` 1本については正しかった**が、**他の3本は skew していた**＝
「ヘッダ差し替え不要」は**成立していなかった**（C3の混成供給には**気づかれていない
実ABI skew が 3/5 あった**）。本移行でそれが解消した。

---

## 6. ビルド実測（依頼 (b)）— 全7構成

`.d`＝`ninja -t deps`／`L`＝**リンク行の `-L`/`-T`**（★`ninja -t deps` は `-L`/`-T` を見ないため別測．C6ラウンドの知見）

| build | 構成 | cmake | build | **.d hal** | .d idf | .d ~tools | **L hal** | L idf | L ~tools |
|---|---|---|---|---|---|---|---|---|---|
| （baseline） | 移行前の既定 `-DESP32C3_WIFI=ON` | 0 | **1（リンク失敗）** | 7000+ | 0 | blob(`-L`) | — | — | — |
| `c3_idf` | **既定**＝esp-idf供給 WiFi(`wifi_scan`) | 0 | **0** | **0** | 6255 | **0** | **0** | 188 | **0** |
| `c3_halback` | `-DASP3_ESPIDF_SUPPLY=OFF -DASP3_WIFI_BLOB_HAL=ON` | 0 | **0** | 6855 | 0 | **0** | 194 | 0 | **0** |
| `c3_dhcp` | WiFi+lwIP（W1相当 `wifi_dhcp`） | 0 | **0** | **0** | 6269 | **0** | **0** | 188 | **0** |
| `c3_sample1` | WiFi/BT 両OFF（素） | 0 | **0** | **0** | 246 | **0** | **0** | 12 | **0** |
| `c3_bt_hal` | **既定BT**＝hal（`bt_smoke`） | 0 | **0** | 890 | 0 | **0** | 62 | 0 | **0** |
| `c3_ble_hal` | **既定BLE**＝hal（`ble_host_smoke`） | 0 | **0** | 5690 | 0 | **0** | 158 | 0 | **0** |
| `c3_bt_v554` | `-DASP3_BT_IDF_V554=ON`（**真のv5.5.4タグ**） | 0 | **0** | 844 | 33 | **0** | 49 | 14 | **0** |

**リンク行の供給元（`c3_idf`）**：`-L` は `esp-idf/components/{esp_wifi,esp_phy,esp_coex}/lib/esp32c3`＋
`esp-idf/components/soc/esp32c3/ld`、`-T` は `esp-idf/components/esp_rom/esp32c3/ld`×7＋
`esp-idf/components/riscv/ld/rom.api.ld`
＝**blob・ROM ld も含めて hal 参照 0**。

- **`~/tools/esp-idf` 参照は全7構成で 0**（＝外部絶対パス撤去の実証）。
- **BT の hal 参照が残るのは設計どおり**（§5-3：BTは検証済みの全hal構成を維持）。
- **toolchain**：memory の「C3ビルドはGCC13.2必須（esp-14.2は`phy_init.c`の`heap_caps_malloc`暗黙宣言でhard error）」は
  **本PC・本供給では不要と実測**＝**esp-15.2.0 で全7構成が通る**（`-Wno-error=implicit-function-declaration` 併用．C5/C6と同じ）。
  ＝**別PC・別供給時代の記録**であり、鵜呑みにしなくて正しかった。

---

## 7. ★実機ラウンドへの予測と「測ってほしい決定的対照」（依頼 (e)）

### 7.1 最優先＝**WiFi scan（既定＝esp-idf供給）**

**予測＝通る（85%）**。根拠＝(i) ABI skew が 3/5 不一致→**5/5一致**へ解消（§5.1）、
(ii) `_magic` offset 476 が構造的に整合（§1.1）、(iii) C5/C6 が同じ移行で scan を実機達成。
★**移行前の既定はそもそもリンクできなかった**（§3）＝実機比較の「前」は
`-DASP3_ESPIDF_SUPPLY=OFF -DASP3_WIFI_BLOB_HAL=ON`（＝`c3_halback`＝hal純正）を使うこと。

### 7.2 ★§2 の決着＝**OPTIONS0 の cold/warm 対照**（この静的結論を実機で殺せる唯一の測定）

**測る対象＝`RTC_CNTL_OPTIONS0_REG`（`0x60008000`）を `hardware_init_hook()` の
«ASP3が何か書く前» にSTOREへラッチし、真POR と warm を比較する。**

- なぜこのレジスタか＝**C3で「BBPLLのenable状態」と「warm保持・POR既定復帰」を
  同時に満たす唯一のレジスタ**＝**C6の PCR `SOC_CLK_SEL` の構造的アナログ**。
  `SYSCLK_CONF`(0x600C0058) を後から読んでも**ASP3自身が書いた値**が見えるだけで無意味。
- 真coldの証明＝**センチネル法**（STORE3へ `0xCA5E0058` → 電源断 → **0 なら真POR**）。
  ★STORE5(`0x600080BC`)は**ROMが上書きする**ので使わない（本ラウンドのROM逆アセンブルでも
  `boot_prepare` が 0x600080BC を書くのを確認＝この既知事実の独立な裏付け）。

| STORE0（＝OPTIONS0）の bit10/8/6 | 判定 |
|---|---|
| **cold・warm とも 0** | **予測どおり**＝C6型のバグ無し・コメント訂正で完了（§2.4） |
| **cold で 1（warm で 0）** | **★予測は反証**＝PLL切替前に `bbpll_enable`＋`bbpll_configure` が要る |
| bit13(`XTAL_FORCE_PU`, 既定1)が両方 1 | 「この読みが生きている」無料のsanity check |

**私の予測（先に固定する）**：**cold・warm とも 0（＝85%）**。
残り15%は §2.3 の **行5・行10（POR既定が文献で取れなかったアナログ2件）** に集中する。

★**含意の自問**（「A⇒B」を書く前に成立を確認する規律）：
- 「**bit が cold で 0 ⇒ C3 は真coldで動く**」は **健全でない**。0 は「強制電源断されていない」
  ことしか示さず、**BBPLLが480MHzでロックしている**ことまでは示さない（アナログ既定の話＝行10）。
  ⇒ **機能側の対照を別に置く**：`mcycle` を SYSTIMER（XTAL系＝PLL非依存）で割った比を
  ラッチする。**≈10.0 ⇒ CPU 160MHz（＝BBPLL 480M）** ／ **≈6.67 ⇒ BBPLL が実は 320M** ／
  **値が出ない ⇒ cold で BBPLL 死**。
- 「**予測が外れた ⇒ §2 の静的事実も撤回**」も **健全でない**。§2.3 行8/9/13 は
  **ヘッダ原文とROM nmで直接確認したPROVEN**であり、実機結果と独立に生き残る
  （外れた場合に崩れるのは «行5/行10 のUNKNOWNが無害» という部分だけ）。

### 7.2.1 ★★C3 の実機実績は **すべて warm**＝本予測を支持する既存データは «無い»

**一次情報**（コーディネータの訂正を受け、C3 の正本 docs を «この個体の記録» として読み直した）：

- `docs/bt-shim.md:2791`：C3 は **D-2c/D-2d フル達成**（connect+bond+暗号+フルGATT）だが、
  末尾に **「物理電源断 cold boot（warm 残留判別・**依然ユーザー保留**）」** と明記されている。
  ⇒ **C3 の実機実績は D-1〜D-2d まで含めて «すべて warm»。真cold は一度も行われていない。**
- `docs/bt-shim.md:2741-2743`：**プロジェクトは既にこの実験を C3 の宿題として登録済み**で、
  しかも正しく枠組み化していた——
  > 「★C6 前例は naive 予測を «反転» させる…C3 が同型なら物理電源断（cold）は改善ではなく
  > «悪化» もあり得る＝この実験は «warm残留への依存 vs warm残留による害» の判別であって
  > **一方向の確認ではない**」

**⇒ 経験的な状況は C6 と «同一»（warm実績のみ・真cold未検証）。にもかかわらず §2 で
«C3は該当しない» と結論できたのは、差が «経験» ではなく «構造» の側にあるからである**
（C6＝muxを書かない×POR既定が不都合／C3＝muxを毎ブート書く×POR既定が好都合）。
∴ **本予測(85%)を支持する «既存の実機データ» は存在しない**——支えているのは
**レジスタ既定値の原文とROM逆アセンブルだけ**。だからこそ §7.2 の対照を測る価値がある。
（★これは本ラウンドの成果を弱める注記ではなく、**成果の «種類» を正確にする注記**＝
静的PROVENであって実機corroborationではない。）

### 7.2.2 ★交絡候補として «個体差» を筆頭に置かない（コーディネータ訂正の反映）

- 本ラウンドの DUT 候補 `60:55:F9:57:BA:BC` は **台帳未知の新個体ではなく**、
  `docs/ble-c5c6.md`／`docs/blob-unify-v554.md`／`docs/c5-toolchain.md`／`memory/reference_hw_bench_deskmini.md`
  等に記録のある **既知の個体**（全文検索で実測）。**過去記録はこの個体の一次情報として有効**。
- ★**「USBデバイス台帳に無い＝新個体」という推論を使わない**（コーディネータが同型の誤りで
  二度事故り、C6 では存在しない «rev v0.2 vs v0.3» 仮説まで生んだ）。**個体同定は MAC の全文検索で行う。**
- ★**個体差を交絡の筆頭に置かない**：C6 で «個体差» は誤った前提が生んだ幻であり、
  **本物の交絡は cold/warm の区別だった**。C3 でも同じ轍を踏まないこと
  （本ラウンドの §2 も、個体ではなく **cold/warm × レジスタ既定** で切っている）。

### 7.3 ★BT の申し送り（**未測定の新構成が生まれた**）

`-DASP3_BT_IDF_V554=ON` は**パス変更により意味が変わった**：

| | 従来（元PC） | **本ラウンド以降** |
|---|---|---|
| 指す先 | `~/tools/esp-idf`＝**+1169（≡v6.1系）**／本PCでは**v5.5.0** | **submodule＝真のv5.5.4タグ** |
| `libbtdm_app.a` | `d9753a31`(=v6.1) ／本PCなら `93abf3c7`(v5.5.0) | **`859e8c8e`（実測．リンク行で確認）** |
| `bt.c` OSI_VERSION | `0x0001000B`／本PCなら **`0x0001000A`（＝説明と矛盾）** | `0x0001000B`＋`_malloc_retention`（＝旧説明と整合） |

⇒ **HANDOFF §4-4 の「C3 BT の v5.5.4 切替は実機 bond 失敗（AuthenticationTimeout）」という
エビデンスは «+1169（≡v6.1系）» に対するものであり、«真のv5.5.4タグ» は一度も測っていない。**
∴ 現在の `-DASP3_BT_IDF_V554=ON` は「失敗すると分かっている構成」**ではなく**、
**未測定の新しい構成**（ビルドは通る＝§6）。**既定は OFF のまま**＝実機挙動は不変。
**C3では libbtdm_app.a のみ hal と相違＝真の A/B が成立する**（C6と違い実体がある）。

---

## 8. 変更ファイル（すべてガード付き・可逆。`asp3_core`/`hal`/`esp-idf` は**無編集**）

| file | 内容 |
|---|---|
| `target.cmake` | 供給ブロック（`IDF_V554` submodule相対／`ASP3_ESPIDF_SUPPLY`／`ESP_SUP_DIR`／`ESP_SUP_HAL_<x>`×8／`ESP_SUP_SDKCONFIG_DIR`）。BT時のみ既定OFF |
| `esp_wifi.cmake` | 全パスを供給元非依存化。mbedtls 4.0.0/3.6.5 の if/else。`common.h` shadow 対策。外部絶対パス撤去 |
| `esp_bt.cmake` | 外部絶対パス撤去→`ASP3_BT_IDF_V554_DIR`（既定=submodule・不在ならFATAL_ERROR）。旧「決定的事実」md5記述の訂正 |
| `wifi/esp_shim_blobglue.c` | `esp_wifi_skip_supp_pmkcaching` スタブ撤去（§3） |
| `wifi/esp_wifi_adapter.c` | `esp_event_post` の `TOPPERS_ESPIDF_SUPPLY` ガード |
| `target_kernel_impl.c` | **コメントのみ訂正**（§2.4．`asp_flash.bin` byte-identical で実証） |
| `sdkconfig_stub/sdkconfig.h` | **新規**＝hal `nuttx/esp32c3/include/sdkconfig.h` の verbatim vendor（CONFIG_* 720個・body diff 0） |

**option（すべて可逆）**：

| option | 既定 | 意味 |
|---|---|---|
| `ASP3_ESPIDF_SUPPLY` | **ON**（BT時のみ OFF） | esp-idf submodule 供給（OFF で hal 供給へ完全復帰＝実測 rc=0） |
| `ASP3_WIFI_BLOB_HAL` | OFF | WiFi blob を hal へ戻す |
| `IDF_V554` | submodule | `-DIDF_V554=<path>` で外部treeへ A/B |
| `ASP3_BT_IDF_V554` | **OFF**（不変） | BT を v5.5.4 タグへ（**未測定**＝§7.3） |
| `ASP3_BT_IDF_V554_DIR` | submodule | 上記の供給tree（**新規**．外部絶対パスの置換） |

---

## 9. 残ブロッカー（依頼 (f)）

1. **★実機未検証**（本ラウンドは実機に一切触れていない＝親の指示）。§7 の予測を先に固定した。
2. **C3 BT は「submodule供給」に到達していない**（§5-3・§7.3）。到達には
   `esp_bt.cmake` の `ESP_HAL_DIR` 群（35箇所）を同時移行し、`shared_periph_module_t` を
   「混ぜない」形で解く必要がある＝**別ラウンド**。
   **ただしC3はC6と違い「v5.5.4統一」に実体がある**（blobが3つ巴）＝やる価値がある。
3. **§2.3 の行5・行10（UNKNOWN）**：BBPLLの**アナログ較正が未適用**＝工場既定で走行。
   起動実績はあるが**温度/電圧マージンは未検証**。cold/warm の問題ではないため本ラウンドでは
   触らない（**反射的なC6移植はむしろ危険**＝§2.4）。
4. **`~/tools/esp-idf` を指す記述が docs 側に残る**：`docs/blob-unify-v554.md` 等は
   「v5.5.4＝`~/tools/esp-idf`」を前提に書かれており、§1/§4.1 の実測と食い違う。**次ラウンドで訂正が要る**。
5. **STORE4/STORE5 の潜在事項**（§2.3 行6/7）：`rtc_clk_apb_freq_get()` は全ブートで stale な
   20MHz を返しうる（消費者は現時点で**不在**と実測）。`s_ticks_per_us` と同族の潜在バグとして申し送り。
</content>
</invoke>

# C6 evidence-01 — WiFi供給を esp-idf submodule へ移行（**hal参照0達成**）／BTは v6.1 据置きと判断＋**決定的対照**の用意

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
commit: WiFi移行＝`3cc1f63`（※後述の帰属注記あり）／BT修正＝`35c37ac`
toolchain: Espressif `riscv32-esp-elf` **esp-15.2.0**（`~/.espressif/tools/...`）
**実機には一切触れていない**（別エージェントがC5実機測定中のため。実機検証は次ラウンド）

---

## 0. 結論（先に3行）

1. **C6 WiFi は供給移行完了＝`ninja -t deps` の hal 参照 **0**（ヘッダ/ソース・blob・ROM ld すべて）。可逆性・非回帰も実測。
2. **C6 BT は v6.1 据置きが正しい**——実測で **真のv5.5.4タグのBT blob ≡ hal（4/4バイト一致）**＝「v5.5.4統一」は C6 では「halへ戻す」と同義で、§10-12 のハングを持ち込む側。
3. ただし §13 の A/B は **blob と グルーを同時に替えた交絡**であり「blobの版が原因」は未分離。**4アーム目（C3型グルー×hal同一blob）をビルド可能にした**＝実機ラウンドの最優先対照。

---

## 1. ★provenance：`~/tools/esp-idf` は **v5.5.4ではない**（親の警告を実測で追認・かつ更新）

親プロンプトは「`~/tools/esp-idf` は `v5.5` の shallow clone＝release/v5.5 の先端」と述べたが、
**本PCでの実測はさらに違った**：

```
$ git -C ~/tools/esp-idf log -1 --format='%H %ci %s'
8c750b088c7cd857d079c0eeb495da199b359461  2025-07-18  change(version): Update version to 5.5.0
$ git -C ~/tools/esp-idf describe --tags   → v5.5      （shallow, 1 root）
$ git -C ~/tools/esp-idf cat-file -t bb2188bf → fatal: Not a valid object name
$ git -C ~/tools/esp-idf reflog            → clone: from https://github.com/espressif/esp-idf.git （移動していない）
```

| tree | 実体 |
|---|---|
| repo submodule `esp-idf/` | **v5.5.4 タグ＝`735507283d`**（＝真のv5.5.4） |
| `~/tools/esp-idf`（変数名 `IDF_V554`） | **v5.5(=v5.5.0, `8c750b08`) の shallow clone** |
| `~/tools/esp-idf-v6.1` | `v6.1-beta1` |
| `hal/` | `b90b1837` |

- HANDOFF §3-1 が記録した `v5.5.4-1169-gbb2188bf` は**本PCには存在しない**（別PCの状態）。
- ⇒ **同じパス名 `~/tools/esp-idf` が PC ごとに別の版を指す**。これが provenance の罠の本体であり、
  「外部絶対パスを撤去して submodule 相対にする」ことの実証的な根拠。
- ★**HANDOFF §3-3 の「+1169 ≡ v6.1（3/4一致）」は本PCでは検証不能**（当該treeが無い）。
  本PCで再現できるのは「**真のv5.5.4タグ ≡ hal**」の方（§2）。

---

## 2. ★blob md5 実測表（C6）— 依頼 (a)

```
file          submodule(v5.5.4tag)              hal(b90b1837)                     v6.1-beta1                        ~/tools/esp-idf(v5.5.0)
libble_app.a  75db98e5139162fa60583becb38ea0a1  75db98e5139162fa60583becb38ea0a1  c28653df7553ac7b9932a84b235b166b  54cb6f5f2348e68539b771184f673544
libphy.a      cb429107787d88023983668c9b161b56  cb429107787d88023983668c9b161b56  3fea07086717f1c7c18f58e2d3815721  6b62ea91d9af51b9beb46385911db3bb
libbtbb.a     cbe3022fd34cffb613ce81a15b207a95  cbe3022fd34cffb613ce81a15b207a95  d31c8865a4c1230bd65711847638f244  1037b470aba2988454ba3c47c502adf2
libcoexist.a  553448620fbc7f65fa559eef312d2d0e  553448620fbc7f65fa559eef312d2d0e  797d4daf5005bbc1d7d7288ef60a5e14  cd3c5cff89994a0b5291722d81a658fc
--- WiFi ---
libnet80211.a bfdbbdaf127366732e98c2b43333ec8e  b465395be10138180ffb4bad4e96927f  e3d3ecb4ccc91b35ff36b716fe68c8c6  e1aa75584fed856af0da978d9696305c
libpp.a       cfdbb703b346935ce9f178d30ccb7fa7  22b025da9d9f367268c705eaac716676  700f6842af5066077c2cef33fd66fadc  d0274ae589b292c7d1edef86db3771dd
libcore.a     0958841f674baa275a6a6dc29865696a  952499526ce64f2a296733ed2838a5b3  643b90498ebed0132aa609d6859ad85b  b092720dfdc6cc8a6c29c3c46b43f2d3
（libespnow/libmesh/libsmartconfig/libwapi も submodule≠hal．全て相違）
```
> パス注意：C6 の libble_app.a は `lib_esp32c6/esp32c6-bt-lib/**esp32c6/**libble_app.a`（C5より1階層深い）。

### 2.1 そこから言えること

1. **BT/PHY/coex：真のv5.5.4タグ ≡ hal（4/4 バイト完全一致）**（C5と同じ構造）。
   ⇒ **C6 で「BTをsubmodule(v5.5.4)へ移す」＝「hal blob に戻す」と等価**。
   §10-12 が `register_chipv7_phy` RFシンセ非ロックでハングと記録した blob そのもの。
2. **WiFi：submodule ≠ hal（7/7 相違）**。⇒ WiFi の移行は**実質的な版統一**であり、意味がある。
3. **v5.5.4タグ vs v6.1 は BT 4/4 すべて相違**。
   ⇒ `esp_bt_idf61.cmake:48` の旧コメント「libble_app/libphy/libbtbb は v5.5.4/v6.1 間で
   **C6もバイト完全一致**——差はlibcoexist.aのみ」は **C6では成立しない＝実測で反証**。
   旧主張が成立していたのは当時の `~/tools/esp-idf` が **+1169** で、それを「v5.5.4」と
   **名前で誤認**したため。＝本質は「v5.5.4≡v6.1」ではなく「**+1169≡v6.1**」。
4. **傍証（構造）**：v5.5.4タグには `components/bt/porting/mem/os_mempool.c` が**無い**
   （v6.1 で bt/porting へ移設。v5.5.4は `bt/host/nimble/nimble/porting/nimble/src/` の旧レイアウト）。
   旧 `ASP3_BT_IDF_V554=ON` はこのパスを直参照していたので、**真のタグを指したら configure すら通らない**
   ＝**旧トグルは一度も v5.5.4 タグを指していなかった**ことの独立な裏付け。

> ★nm のシンボル数で機能可否を判断していない（PROMPT §6・rigor）。md5 は「同一バイナリか」の同定にのみ使用。

---

## 3. ★C6 WiFi の移行前の姿＝**混成供給**で、本PCでは**そもそも壊れていた**

移行前（私の変更前）の C6 既定 `-DESP32C6_WIFI=ON`：

| 要素 | 供給元 |
|---|---|
| ヘッダ・ソース | **hal** |
| WiFi/PHY/coex **blob** | `${IDF_V554}` = **`~/tools/esp-idf`**（本PCでは v5.5.0） |
| `wifi_os_adapter.h` | `wifi/idf_v554_override/` が hal をシャドウ |

実測：**リンク失敗**（pre-existing。私の変更前の baseline で確認）

```
ld: ~/tools/esp-idf/.../libnet80211.a(ieee80211_supplicant.o):
    multiple definition of `esp_wifi_skip_supp_pmkcaching';
    .../wifi/esp_shim_blobglue.c:495: first defined here
```

原因（nm 実測）：

| tree の libnet80211.a | `esp_wifi_skip_supp_pmkcaching` |
|---|---|
| hal | **定義あり** |
| esp-idf(v5.5.4タグ) | **定義あり** |
| `~/tools/esp-idf`(v5.5.0) | **定義あり** |
| v6.1 | **なし** |

⇒ `#if ASP3_WIFI_BLOB_V554` のスタブは「当該シンボルを持たない tree」＝**+1169／v6.1 系**に対してのみ正しかった。
**「v5.5.4」という名前の嘘が、ソースの `#if` ガードにまで伝播していた**。

### 3.1 ★`idf_v554_override/` は誤りであり**有害**だった（＝親の §6-1「引き継がれた事実も疑え」の実例）

override は「v5.5.4 からの verbatim コピー」と自称していたが、**hal・esp-idf・v6.1・`~/tools/esp-idf` の
どれとも一致しない**（実測）。実体は「v5.5.4 のヘッダに `_wifi_disable_ac_ax` を**足したもの**」：

```diff
--- idf_v554_override/esp_private/wifi_os_adapter.h
+++ esp-idf(v5.5.4タグ)/components/esp_wifi/include/esp_private/wifi_os_adapter.h
-#if CONFIG_SOC_WIFI_HE_SUPPORT
-    bool (*_wifi_disable_ac_ax)(void);
-#endif
```

- **真のv5.5.4タグの `wifi_os_adapter.h` は当該フィールドを持たず、hal のものと md5 一致（`6eaa5ad`）**。
- C6 は `CONFIG_SOC_WIFI_HE_SUPPORT=1` なので override を被せると構造体が4バイト伸び、
  `_magic` が **484→488** へずれて blob の整合性チェックが落ちる
  （＝memory `c5-wifi-osi-abi-he-field` が C5 で確定済みの罠と**同一**）。
- ⇒ **override をディレクトリごと撤去**。ガードは供給元の版を表す
  `ASP3_WIFI_OSI_HAS_DISABLE_AC_AX`（既定OFF）へ改めた（C5 と同一）。

**静的検証（0x102 回帰が無いことの証明）**：
```
$ nm -S build/c6_idf/asp.elf | grep g_wifi_osi_funcs
40819004 000001e8 D g_wifi_osi_funcs          ← size 0x1e8 = 488
$ objdump -s -j .data (+480..+492)
 408191e4 b2ab0242 afbeadde 02000000
                   ^+484 = afbeadde = 0xdeadbeaf(LE)
```
⇒ `_magic` は **offset 484**＝v5.5.4タグ blob が読む位置と一致（C5 evidence-02 §4.1 と同じ判定）。

### 3.2 ABI skew の解消（blob 埋込み md5 × 供給ヘッダ）

blob が「自分がビルドされたヘッダの md5 先頭7桁」を埋め込む性質を利用（HANDOFF §5-9 の手法）。
C6 v5.5.4タグ blob が要求する md5 集合：
`0947e8b 2331a76 47fd1b7 56b5fed 6253add 6eaa5ad 7937614 80e5949 a78adff ce069d3 dae1625`

| ヘッダ | hal | **esp-idf（移行後）** | blob の要求 | 判定 |
|---|---|---|---|---|
| `wifi_os_adapter.h` | 6eaa5ad | **6eaa5ad** | 6eaa5ad | 元から一致 |
| `esp_wifi.h` | 9f7e672 | **a78adff** | a78adff | **解消** |
| `esp_wifi_types_generic.h` | 6773bf5 | **dae1625** | dae1625 | **解消** |
| `esp_wifi_types_native.h` | ce069d3 | **ce069d3** | ce069d3 | 元から一致 |
| `esp_wifi_driver.h` | 50fc486 | **2331a76** | 2331a76 | **解消** |

⇒ **esp-idf 供給で 5/5 一致**（hal ヘッダでは 3/5 不一致だった）。C5 evidence-02 §4 と同型の結果。

---

## 4. 踏んだ版差と解き方（依頼 (c)）— **新規の壁はゼロ・すべてC5が既に解いていた**

| # | 版差（実測） | 解き方 |
|---|---|---|
| 1 | `sdkconfig.h` は Kconfig 生成物で **esp-idf のチェックアウトに存在しない** | hal の `nuttx/esp32c6/include/sdkconfig.h`(1155行) を `sdkconfig_stub/` へ **verbatim vendor**（PROMPT §4）。**CONFIG_* は1ビットも変えていない**＝供給移行で設定を同時に動かさない |
| 2 | mbedtls **4.0.0(tf-psa分離) → 3.6.5(classic)** の版ダウン | config は本家 port の `esp_config.h`。`library/` 一括レイアウトへ1:1写像。`pk_rsa.c`/`tf_psa_crypto_{config,version}.c` 除外、`version.c`/`psa_crypto_aead.c`/`entropy_poll.c`/`bignum_mod{,_raw}.c` 追加 |
| 3 | `common.h` shadow（3.6.5 の `library/common.h` が wpa の `src/utils/common.h` と同名。**4.0.0 には無いので移行で初めて生じる**） | `mbedtls/library` を **wpa の後ろ**に置く（本家 esp-idf の登録順と同じ） |
| 4 | esp-idf の `esp_event.h`/`esp_private/wifi.h` は `freertos/*.h` を要求（**hal は `platform/os.h` に置換**＝NuttX向けにFreeRTOS依存を剥がしてある。実測：hal `esp_event.h:12`=platform/os.h ／ esp-idf `:12-15`=freertos×4） | C3 の `bt/stub/include`（FreeRTOS互換スタブ）を**後ろに**追加（同居する `bt_nimble_config.h`/`esp_partition.h` を誤シャドウしないため） |
| 5 | `esp_event_post()` の `event_data` が hal=`void *` / esp-idf=**`const void *`** | `TOPPERS_ESPIDF_SUPPLY` ガードで局所 extern を止め、本物の宣言を使う |
| 6 | `wifi_init.c` が `esp_netif.h` を include（呼出し0件） | `esp_netif/include` をパス追加のみ |
| 7 | **`periph_module` リネーム**：hal の `esp_private/esp_modem_clock.h` が要求する `shared_periph_module_t` は **esp-idf の `soc/periph_defs.h` に存在しない**（実測：hal は `soc/esp32/…` 等に定義／esp-idf は**どこにも無い**） | **BT側で顕在化**。§6 のとおり「混ぜない」ことで回避（HANDOFF §4-3-5 が予告した罠と一致） |

> ★一般則の再確認（evidence-c5-02 §1.2）：**ヘッダとソースを揃えて同じ供給元から取ればリネーム/版差問題は消滅する。片方だけ移すと詰む。**

### 4.1 副次：★D5（`esp_wifi_sta_get_ie` の no-op 化＝RSN IE 検証の恒常無効化）は C6 でも解消

| 測定 | hal 供給 | **esp-idf 供給** |
|---|---|---|
| `esp_wifi_sta_get_ie` を参照する wpa ソース | **3 ファイル** | **0 ファイル** |
| `esp_wifi_is_wpa3_compatible_mode_enabled` を参照する wpa ソース | **4 ファイル** | **0 ファイル** |

⇒ これらは **hal の wpa_supplicant だけが呼ぶ hal 独自 API**。esp-idf 供給では参照ごと消滅し
`--gc-sections` で脱落＝走行経路から消える（C5 evidence-02 §7 と同じ結論）。
スタブ定義自体は hal fallback 用に残置（**hal fallback を使う限り ★D5 は残る**）。

---

## 5. ★B1 / ★B2 / ★B3(iii) の真因と対処（依頼 (d)）

### 5.1 ★B1 の真因（レビューの記述より**深刻**だった）

- 構造：`esp_bt.cmake:31` `option(ESP32C6_BT_IDF61 … OFF)` が**上位ゲート**、
  `ASP3_BT_IDF_V554`（既定ON）は `esp_bt_idf61.cmake:48`＝`if(ESP32C6_BT_IDF61)` の**内側**。
  ⇒ 上位が OFF である限り**下位flipは一度も評価されない**。既定flipコミット `e1e965c` は
  下位の既定2箇所しか変えておらず、上位に触れていない（レビューの指摘どおり）。
- ★**さらに実測で判明**：§20（`2c39cad`, 2026-07-15）が `esp_shim_bt_pmu_init()` の呼出しを
  `#ifdef TOPPERS_ESP32C6_BT` で**無条件化**した一方、実体 `bt/bt_pmu_init_c6.c` を
  **`esp_bt_idf61.cmake` にしか追加しなかった**（静的確認：`esp_bt.cmake` の
  `bt_pmu_init_c6` 参照数＝**0**）。
  ⇒ **hal 経路は 2026-07-15 以降そもそもリンク不能**：
  `undefined reference to 'esp_shim_bt_pmu_init'`（実測）。
  ∴ 「素の `-DESP32C6_BT=ON` が hal(v8) に着地」は正確には
  **«ハングする構成に着地» ではなく «ビルドできない»**。
- **対処**：`ESP32C6_BT_IDF61` の既定を **OFF→ON**。根拠＝v6.1 は §13 D-1／§14 D-2a/D-2b／
  §15 D-2c/D-2d を **board C 実機 2/2** で達成した**唯一**の構成（board C 最終flashもこの経路）。
  ＝「既定＝実機エビデンスのある構成」。**憶測ではなく真因特定後の変更**。

### 5.2 ★B2／★B3(iii)（可逆性の穴）

- 呼出しガードを `TOPPERS_ESP32C6_BT` → **`TOPPERS_ESP32C6_BT_PMU_INIT`** へ変更し、
  **実体を積む `esp_bt_idf61.cmake` だけが定義**する。
  - v6.1／v5.5.4 経路：定義されるので**挙動不変**（非回帰）。
  - hal 経路：呼出しが消え §20 以前の挙動へ戻り**リンク可能**に。
- ⇒ `-DESP32C6_BT_IDF61=OFF` の**単一ノブ**で hal へ完全復帰＝**B2 の穴を解消**。

### 5.3 外部絶対パスの撤去（3箇所）

| 旧 | 新 |
|---|---|
| `esp_wifi.cmake:318` `set(IDF_V554 /home/honda/tools/esp-idf)` | **submodule 相対**（`${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf`） |
| `esp_bt_idf61.cmake:50` `set(IDF /home/honda/tools/esp-idf)`（"v5.5.4"を名乗る） | **`${IDF_V554}`＝submodule（真のv5.5.4タグ）** |
| `esp_bt_idf61.cmake:52` `set(IDF /home/honda/tools/esp-idf-v6.1)` | **`ESP_IDF61_DIR` キャッシュ変数**（既定=従来パス／不在なら FATAL_ERROR で明示） |

⇒ 既定ビルドの `~/tools/esp-idf`（版が不定の外部tree）参照は **全ビルドで 0**（実測）。
v6.1 は submodule 化されていないため `ESP_IDF61_DIR` は依然として外部treeを指す＝
**「hal参照0」は満たすが「submodule供給」には到達しない**（§6・§8 の残ブロッカー）。

---

## 6. ★C6 BT の供給移行は「**行わない**」と判断した（無理に成功を作らない）

> ## ★★【2026-07-17 追記＝evidence-c6-05 実機で本節の判断は «撤回» された】
> **4アーム目（`ASP3_BT_IDF_V554=ON`）を実機で回した結果、根拠1と3は反証された。**
>
> | 本節の根拠 | evidence-c6-05 の実測 | 判定 |
> |---|---|---|
> | 1.「v5.5.4統一＝**§10-12 のハング構成に戻すこと**と同義」 | **v554 は warm・真cold とも D-1 到達。★hal 対照すら D-1 到達**（＝前提の «ハング» が本個体で再現しない） | **★反証** |
> | 2.「基盤を esp-idf へ移すと供給混成で `shared_periph_module_t` 未定義が噴出」 | **再現した**（`-DASP3_ESPIDF_SUPPLY=ON` で同一エラー＋`clk_hal` 系ドリフト） | **★維持** |
> | 3.「C6 BT が要求する blob は **v6.1**（submodule に無い）」 | **★反証＝submodule の v5.5.4 blob（`75db98e5`/`cb429107`）で D-1 到達** | **★反証** |
>
> ⇒ **「BT だけ外部 v6.1 tree が必須」は撤回**（＝外部 `/home/honda/tools/esp-idf-v6.1` への
> 依存を外せる）。**根拠2（基盤の混成）は別問題として残る**＝「hal 参照 0」は未達
> （`esp_bt*.cmake` の `ESP_HAL_DIR` 計122箇所の載せ替え＋clk/periph API ドリフト吸収が要る）。
> **正本＝`evidence-c6-05-blob-vs-glue-4th-arm.md` §5.5／§5.6。**
> ★**ただし §10-12 の hal ハング（board C・rev v0.3）が何だったかは未解決**
> （本DUT＝rev v0.2 では hal が動く＝**個体/rev 仮説が復活**・board C 非接続で分離不能）。


理由（すべて実測）：

1. **§2.1-1**：真のv5.5.4タグの BT blob ≡ hal（4/4）＝「v5.5.4統一」は C6 では
   **§10-12 のハング構成に戻すこと**と同義。
2. **実機エビデンスは全て「hal基盤 ＋ BTだけv6.1」**で得られている（§13-15）。
   基盤を esp-idf v5.5.4 へ移すと、BT側cmake（`esp_hw_support` 等を hal から採る）との間で
   **供給元の混成**が生じ、実測で `shared_periph_module_t` 未定義が噴出して破綻する
   （hal内・esp-idf内はそれぞれ整合。**壊れているのは «混ぜたこと» そのもの**＝HANDOFF §4-3-5 と一致）。
3. BT を移すなら `esp_bt*.cmake` の `ESP_HAL_DIR`（**計122箇所**）も同時に移す必要があり、
   かつ C6 BT が要求する blob は **v6.1（submodule に無い）**なので、
   どのみち「**submodule 供給**」というミッションのゴールには到達しない。

**実装**：`ASP3_ESPIDF_SUPPLY` の既定を `ESP32C6_BT=ON` のときだけ **OFF**（＝検証済み構成を維持）、
それ以外（WiFi／素のビルド）は **ON**（HAL-free）。明示的に `-DASP3_ESPIDF_SUPPLY=ON` で上書き可＝可逆。

---

## 7. ★実機ラウンドへの予測と「測ってほしい決定的対照」（依頼 (e)）

### 7.1 ★最優先＝**4アーム目**（§13 の交絡を解く）

**§13 の A/B は 2変数を同時に替えていた**（＝HANDOFF §5-1「決定的対照を省くと偽の成功譚になる」の型）：

| アーム | グルー（bt.c／シム） | blob | 結果 |
|---|---|---|---|
| hal（§10-12） | **esp_os_* / platform-os.h 型**（hal 独自） | hal | **synth-lock ハング（D-1未達）** |
| v6.1（§13-15） | **C3型**（FreeRTOS+esp_intr_alloc、`bt_shim_idf61.c`） | v6.1 | **D-1/D-2b/D-2c 達成** |
| **★v5.5.4タグ（本ラウンドで用意）** | **C3型**（＝v6.1 と同型。実測） | **hal とバイト同一** | **未測定＝これが4アーム目** |

実測の裏付け：
- C6 `bt.c` の型：**hal のみ `esp_os_*`/platform-os.h 型**（10 hits）、
  **esp-idf v5.5.4タグ・v6.1 はともに C3型**（FreeRTOS/esp_intr_alloc 9 hits, esp_os_* 0）。
- ビルド済み `c6bt_idf554` がリンクする `libble_app.a` の md5 ＝ **`75db98e5` ＝ hal と完全一致**（実測）。

⇒ **`-DASP3_BT_IDF_V554=ON` で `bt_smoke_c6`（D-1）を実機で回すと帰属が一意に決まる**：

| 実機結果 | 結論 |
|---|---|
| **ハングする**（`register_chipv7_phy` synth-lock 再現） | 原因は **blob の版** → **v6.1 必須／C6 の v5.5.4統一は不可能**（§6 の判断が確定） |
| **動作する**（controller enable→HCI Reset→Command Complete） | 原因は **hal のグルー/シム**（blob は無罪） → **v5.5.4統一が可能**になり、§13 の帰属（memory の「hal matched set の BT enable サブパス版問題に局在」）は**誤り**と判明 |

> ## ★★【2026-07-17 追記＝evidence-c6-05 実機で本項の予測は «反証» された】
> **実機（本DUT `14:C1:9F:E0:5A:9C`・rev v0.2）：v554 は warm・真cold とも D-1 到達。**
> **∴ 下の「ハングする側に賭ける」は外れ＝「原因は blob の版」は反証された。**
> ★**しかし «2択表» の «前提» も崩れた**：**hal 対照も両条件で D-1 到達**したため、
> 表の「hal＝synth-lock ハング（D-1未達）」という所与が**本個体では成立しない**。
> ⇒ **2択のどちらでもない第3の答＝「blob もグルーも無罪」**（§10-12 の現象自体が再現しない）。
> **∴「動作する ⇒ 原因は hal のグルー/シム」も «健全でない»**（グルーを替えなくても動くので）。
> **正本＝`evidence-c6-05` §5.2／§5.4。**

**私の予測（先に固定する）**：**ハングする側に賭ける**（確率としては blob 起因を優勢と見る）。
根拠＝§12 が「BT/WiFi で `register_chipv7_phy` の入力（a0-a3/init_data/cal_data）は3/3一致、
発散点は関数**内部**」と実測しており、内部分岐は blob のコードそのものだから。
**ただし C5 の反例（hal相当blobでBT動作）があるため断定はしない**——だから測る。
※ 反証されたら §6 の判断ごと撤回すべき、という意味で**この予測は捨て身の対照**である。

### 7.2 その他の実機確認（優先順）

1. **C6 WiFi `wifi_scan`（既定＝esp-idf供給）で scan が通るか**。
   予測＝**通る**（AP数は環境依存）。根拠＝(i) ABI skew が 3/5 不一致→**5/5一致**へ解消（§3.2）、
   (ii) `_magic` offset 484 が静的に一致（§3.1）、(iii) C5 が同じ移行で scan 20AP／W1 を実機達成。
   ★**移行前の既定はそもそもリンクできなかった**（§3）＝実機比較の「前」は
   `-DASP3_ESPIDF_SUPPLY=OFF -DASP3_WIFI_BLOB_HAL=ON`（hal純正）を使うこと。
2. **`-DESP32C6_BT_IDF61=OFF`（hal純正BT）が D-1 でハングするかの再確認**。
   §10-12 の再現性確認＝7.1 の対照の「片翼」。**これも 2026-07-15 以降ビルド不能だったので、
   今回リンク可能になって初めて再測できる**。
3. `ble_host_smoke_c6`（既定=v6.1）で §14 D-2b／§15 D-2c-d の**非回帰**。
   ★注意：私の既定変更で v6.1 blob の実体が `~/tools/esp-idf`(本PC=**v5.5.0**) → **本物の v6.1** に変わる。
   元PCでは両者が BT blob 同一（+1169≡v6.1）だったので**実質同一**のはずだが、
   **本PCで過去ビルドと比較するなら注意**（＝本PCの過去ビルドは v5.5.0 blob だった可能性がある）。

### 7.3 ★測定の作法（この repo で実際に事故ったもの）

- `.d` は **`ninja -t deps`**（`find -name '*.d'` は 0 と誤測）。
- ★**`ninja -t deps` は `-L`/`-T` を見ない**＝**blob と ROM ld の供給元はリンク行から別途測ること**
  （本ラウンドで両方測った。§8 の表）。
- LP_AON等の**残存マーカを「現在の状態」の証拠に使わない**（memory の C3/C6 の教訓）。

---

## 8. ビルド実測（依頼 (b)）— 全アーム

| build | 構成 | cmake | build | **hal** | esp-idf(submod) | v6.1 | `~/tools/esp-idf` |
|---|---|---|---|---|---|---|---|
| （baseline） | 移行前の既定 `-DESP32C6_WIFI=ON` | 0 | **1（リンク失敗）** | 7054 | 0 | 0 | blob のみ（`-L`） |
| `c6_idf` | **既定**＝esp-idf供給 WiFi | 0 | **0** | **0** | 6464 | 0 | **0** |
| `c6_halback` | `-DASP3_ESPIDF_SUPPLY=OFF -DASP3_WIFI_BLOB_HAL=ON` | 0 | **0** | 7114 | 0 | 0 | **0** |
| `c6_sample1` | WiFi/BT 両OFF（素） | 0 | **0** | **0** | 145 | 0 | **0** |
| `c6bt_default` | **既定BT**＝v6.1 | 0 | **0** | 1889 | 0 | 130 | **0** |
| `c6bt_hal` | `-DESP32C6_BT_IDF61=OFF`（★B2回復） | 0 | **0** | 2067 | 0 | 0 | **0** |
| `c6bt_idf554` | `-DASP3_BT_IDF_V554=ON`（★4アーム目） | 0 | **0** | 1880 | 119 | 0 | **0** |
| `c6ble_nimble` | `ble_host_smoke_c6`（§14 D-2b） | 0 | **0** | 3127 | 0 | 3273 | **0** |
| `c6bt_sm` | `-DESP32C6_BT_IDF61_SM=ON`（§15 D-2c/d） | 0 | **0** | — | — | — | — |

**リンク行の供給元（`c6_idf`）**：`-L` は `esp-idf/components/{esp_wifi,esp_phy,esp_coex}/lib/esp32c6`＋
`esp-idf/components/soc/esp32c6/ld`、`-T` は `esp-idf/components/esp_rom/esp32c6/ld`＋`esp-idf/components/riscv/ld`
＝**blob・ROM ld も含めて hal 参照 0**。

**BT の hal 参照が残るのは設計どおり**（§6：BTは hal 基盤を維持）。
BT 側で消えたのは **`~/tools/esp-idf`（版が不定の外部tree）＝0**。

---

## 9. 残ブロッカー（依頼 (f)）

1. **★実機未検証**（本ラウンドは実機に一切触れていない＝親の指示）。§7 の予測を先に固定した。
2. **C6 BT は「submodule供給」に到達していない**（§6）。到達するには
   (a) 7.1 の4アーム目が「動作する」側に出て v5.5.4 統一が可能になるか、
   (b) v6.1 を submodule 化する（＝ユーザー判断。現状 `ESP_IDF61_DIR` は外部tree）
   のどちらかが要る。**BT の hal 離脱は v6.1 参照で達成されるが、それは submodule ではない**。
3. **BT基盤（`esp_bt*.cmake` の `ESP_HAL_DIR` 122箇所）は未移行**。移すなら §4-7 の
   `shared_periph_module_t` を「混ぜない」形で解く必要（＝BT一式を同一treeへ）。
4. **`~/tools/esp-idf-v6.1` への依存が残る**（`ESP_IDF61_DIR`）。不在環境では BT 既定がビルド不可
   （FATAL_ERROR で明示する実装にした）。
5. **docs の更新漏れ**：`docs/blob-inventory.md`／`docs/wifi-blob-generation-todo.md` は
   「C6 BT 既定＝hal（`ESP32C6_BT_IDF61=OFF`）」と記載しており、本ラウンドの既定変更と食い違う。
   §2.1-3 の md5 主張（v5.5.4≡v6.1）も誤りのまま残っている。**次ラウンドで訂正が要る**。

---

## 10. ★記録の帰属についての注記（正直に残す）

WiFi 移行の6ファイルは、私が `git add` した直後に**別エージェント**（同一worktree・共有index）の
`git commit` に巻き込まれ、**commit `3cc1f63`（"docs(c5-ble): reason=517 …"）に含まれてしまった**。
内容は無傷（検証済み）だが、**コミットメッセージは C6 の作業を説明していない**。
当該ブランチは相手が能動的に commit 中であり、履歴書換え（amend/rebase）は相手の作業を壊すため
**行っていない**。本ファイルが WiFi 移行の実質的な記録である。
以降の私の commit（`35c37ac`）は `git commit -- <paths>` でパス限定・アトミックに実行した。
> 教訓：**共有worktreeでは `git add` と `git commit` を分けない**（間に他エージェントの commit が入る）。
</content>

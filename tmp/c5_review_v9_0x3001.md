# ESP32-C5 Wi-Fi v9移行（コミット45f7532）静的コードレビュー — `esp_wifi_init_internal` 0x3001 (ESP_ERR_WIFI_NOT_INIT) 候補洗い出し

対象ブランチ: `claude/c6-wifi-c5-dev-5vc6x9`（`git show 45f7532` 全文精読・実施10まで・
「別PC再開メモ」まで精読済み）。実機・ビルドは使用していない（環境にIDF v6.1が
無いため）。**v9版 `esp_private/wifi_os_adapter.h`／`esp_wifi.h` の実物はこの環境に
存在しない**ため，v8ヘッダ（`hal/components/esp_wifi/include/`）・関連コミット
コメント・`esp_wifi.cmake`内の推定注記から差分を**推定**した箇所は都度明記する。

---

## 総合ランキング（結論を先に）

| 順位 | 候補 | 確度 | 一言 |
|---|---|---|---|
| **1** | `CONFIG_SOC_WIFI_HE_SUPPORT` 未定義による `wifi_osi_funcs_t` の**構造体サイズ・`_magic`オフセットずれ**（ABI崩壊） | **高** | 唯一「静的解析だけで矛盾が確定する」候補。0x3001とタイミング整合も良い |
| 2 | `esp_coex_adapter.c`（C3共有・未改修）が v9 `coex_adapter_funcs_t` に追随していない可能性 | 中 | v8→v9でフィールド追加があれば同種のオフセットずれが再発しうる |
| 3 | sleep-retention no-opスタブの戻り値意味論（`0`固定）の不確実性 | 中〜低 | 実体（hal `sleep_retention.c`）を根拠に「おそらく`esp_err_t`系で0=成功」と評価。要検証マーク |
| 4 | syslog FIFO詰まり／エラーテキスト消失（副次課題6） | （副次） | `printf()`スタブが実際には何もフォーマットしていない実装バグを発見。加えてJTAGでの根本回避策を提示 |
| 5 | `event_post_wrapper` の `const void*`/`void*` 不一致 | 低 | `-Wno-error=incompatible-pointer-types`で警告化されているだけで実害は考えにくい（この経路は0x3001到達点より後） |
| 6 | wifi_init_config_t のヘッダ混入（v8/v9混在） | **低（反証済み）** | 実際に確認した結果，混入は無い（後述） |
| 7 | NVS関連スタブの戻り値 | **低（反証済み）** | `nvs_enable=0`固定のため到達しない設定になっている |
| 8 | mbedtls/wpa_supplicantをhal(v8)に据え置く設計判断 | 情報提供のみ | 今回のクラッシュ地点（supplicant手前）には無関係。次段の壁になりうる点を申し送り |

---

## 候補1（最有力・高確度）：`CONFIG_SOC_WIFI_HE_SUPPORT` が未定義のため `wifi_osi_funcs_t` の末尾フィールド（`_magic`直前）がコンパイラごとにズレている

### 該当箇所
- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c:1103-1108`
  ```c
  #if CONFIG_SOC_WIFI_HE_SUPPORT
  static bool wifi_disable_ac_ax_wrapper(void)
  {
      return false;
  }
  #endif
  ```
- 同ファイル `:1245-1248`（osiテーブル初期化子の末尾，`._magic`の直前）
  ```c
  #if CONFIG_SOC_WIFI_HE_SUPPORT
      /*  【実施10】v9 で追加された HE(AX) 無効化フィールド  */
      ._wifi_disable_ac_ax = wifi_disable_ac_ax_wrapper,
  #endif
      ._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
  };
  ```

### 根拠
1. `CONFIG_SOC_WIFI_HE_SUPPORT` は **C5向けビルドのどこにも定義されていない**。
   - `asp3/target/esp32c5_espidf/sdkconfig_stub/sdkconfig.h` を全文grepしても
     `CONFIG_SOC_` プレフィックスのマクロが**1件も存在しない**。
   - `esp_wifi.cmake` の `ASP3_COMPILE_DEFS` にも同名マクロの追加は無い
     （`CONFIG_IDF_TARGET_ESP32C5=1` は明示的にAPPENDされている＝
     `esp_wifi.cmake:284`。対して `CONFIG_SOC_WIFI_HE_SUPPORT` は無い＝
     **同じ「Kconfig相当を手で埋める」作業が1箇所だけ漏れている**）。
2. C5は実際にはWi-Fi 6 (HE) をサポートするチップである：
   `hal/components/soc/esp32c5/include/soc/soc_caps.h:628`
   ```c
   #define SOC_WIFI_HE_SUPPORT                 (1)    /*!< Support Wi-Fi 6 */
   ```
3. `CONFIG_SOC_WIFI_HE_SUPPORT`（`CONFIG_`付き）という**Kconfig生成後の名前**が
   実在のパターンであることは，同じリポジトリ内の証拠で確認できる：
   `hal/nuttx/esp32c6/include/sdkconfig.h:329`
   ```c
   #define CONFIG_SOC_WIFI_HE_SUPPORT 1
   ```
   （C6のNuttXポートは本物のKconfig出力を持ち込んでおり，`soc_caps.h`のcapability
   が `CONFIG_SOC_*` として自動的にミラーされることを裏付けている）。
   ＝ **本物のESP-IDF v6.1をC5ターゲットでビルドすれば`CONFIG_SOC_WIFI_HE_SUPPORT=1`が
   必ず立つ**（Kconfigのチップcapability由来のシンボルはユーザー設定に依らない）。
4. v9の`wifi_os_adapter.h`（本環境に実体は無いが，v8ヘッダの同型ゲート
   `#if CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61`
   ＝`hal/.../wifi_os_adapter.h:152`が実在する事実と，コミットメッセージが
   「v9でC5の5フィールドが追加された」と明言していることから），v9ヘッダも
   同じ「Kconfig capabilityでフィールドをガードする」流儀を踏襲していると
   考えるのが自然）。すなわち：
   - **v9ヘッダ自身も `#if CONFIG_SOC_WIFI_HE_SUPPORT` でこのフィールドを
     ガードしている可能性が高い**。
   - この場合，v9ヘッダを我々の（`CONFIG_SOC_WIFI_HE_SUPPORT`未定義の）
     `sdkconfig.h`と一緒にコンパイルすると，**ヘッダ側でもフィールドが
     消える**ため，こちら（`esp_wifi_adapter.c`）の初期化子と「見た目は」
     整合してコンパイルは通る。
   - しかし，**prebuiltのv9 blob（Espressifが本物のKconfig＝HE_SUPPORT=1で
     ビルド済み）はフィールドが存在する前提の固定オフセットで
     `g_wifi_osi_funcs`をアクセスする**。
   - 結果：**我々がコンパイルした`g_wifi_osi_funcs`は，blobが期待するより
     関数ポインタ1個分（RV32で4バイト）短い**。特に，`_magic`は
     「常に最後のフィールド」という規約（`wifi_init_config_t`の
     `magic`フィールドにも同じ規約が明記されている：
     `hal/components/esp_wifi/include/esp_wifi.h:123` 
     `int magic; /**< WiFi init magic number, it should be the last field */`）
     に従って配置されるため，**blobが読みに行く`_magic`のオフセットに，
     こちらの構造体では別のバイト（パディングかリンカが後続に置いた
     何らかのデータ）が乗っている**。
5. `esp_wifi_init_internal`は「pp/net80211 rom versionログの直後・
   `esp_supplicant_init`/`esp_phy_enable`より前」で失敗している（実施10）。
   OSアダプタテーブルの整合性検査（マジック値チェック）は，まさに
   blobの初期化シーケンスの**最初期**（他のサブシステムを触る前）に
   行われるのが自然な設計であり，タイミング的に矛盾しない。
   `ESP_ERR_WIFI_NOT_INIT`（0x3001）は「osi/初期化状態がまだ整っていない」
   という**汎用フォールバックコード**として使われることが多く，
   マジック不一致時の代表的な返り値として非常に自然。

### 確度
**高**。他の候補と異なり，これは「実機での挙動」ではなく**この環境だけで
機械的に矛盾が確定できる**（`grep`3回で完結：`CONFIG_SOC_WIFI_HE_SUPPORT`が
どこにも定義されていない事実／soc_caps.hでC5がHE_SUPPORT=1である事実／
同名の`CONFIG_`付きマクロが実在するパターンである証拠）。唯一の不確定要素は
「v9ヘッダが本当に同じ名前でこのフィールドをゲートしているか」という
ヘッダ内部の推定部分のみ。

### 実機での安価な判別方法
1. **最も安価（ビルドのみ・実機不要）**：
   `asp3/target/esp32c5_espidf/sdkconfig_stub/sdkconfig.h` に
   `#define CONFIG_SOC_WIFI_HE_SUPPORT 1` を追加して再ビルドし，
   `nm -S build/.../asp.elf | grep g_wifi_osi_funcs` で**構造体サイズが
   4バイト増えるか**を確認する。増えた状態で実機投入し，0x3001が消えれば
   **確定**（このリポジトリの制約上，本レビューでは実施しない。次段の
   担当者へのそのまま使える手順として記載）。
2. **JTAGでの確認（ビルド変更なしで検証したい場合）**：
   - `nm build/.../asp.elf | grep g_wifi_osi_funcs` で現在のシンボルサイズ
     `S`（バイト数）を取得。
   - `objdump -d`または disassembly で，blobの`esp_wifi_init_internal`
     （libnet80211.a/libpp.aどちらか，nmでシンボル解決）が
     `g_wifi_osi_funcs`ベースアドレスから**どのオフセットを読みに行くか**
     を確認する。もし読み出しオフセットが現在の構造体サイズ`S`より**外側**
     （`S`未満で`_magic`のはずの位置＝`S-4`より後ろの，本来存在しないはず
     の位置）を指していれば確定。
   - 実行時：JTAGでhalt→`g_wifi_osi_funcs`の**先頭アドレス+ (S-4)**（＝
     現在のコンパイルでの`_magic`のはずの位置）を読み，`0xDEADBEAF`
     （`ESP_WIFI_OS_ADAPTER_MAGIC`）になっているか確認する。**なっていれば
     この候補は反証**（構造体は一致している）。**なっていなければ**，
     どこにゴミが入っているか（次の4バイトに実際の0xDEADBEAFがあるか）を
     確認する＝「1フィールド分ずれている」ことの直接証拠になる。
   - stock C5#2（IDF v6.1本家ビルド）で同じシンボルを`nm -S`し，サイズを
     比較するのが最も簡単な対照実験（本家はCONFIG_SOC_WIFI_HE_SUPPORT=1で
     ビルドされているはずなので，サイズが我々のASP3ビルドと**4バイト
     違う**はず）。

---

## 候補2（中確度）：`esp_coex_adapter.c`（C3共有・v9移行で無改修）が新しい`coex_adapter_funcs_t`に追随していない可能性

### 該当箇所
- `asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c`（全体，特に構造体
  初期化 `:172-193`）— 45f7532では**一切変更されていない**。
- 対して `esp_wifi.cmake` は coexのインクルード・リンクパスを
  `${ESP_HAL_DIR}` → `${IDF}` に切り替え済み（`:120`, `:313-315`）。
  すなわち `private/esp_coexist_adapter.h` は **IDF v6.1版**が使われる。

### 根拠
- v8の`coex_adapter_funcs_t`（`hal/components/esp_coex/include/private/esp_coexist_adapter.h:16`）
  は`COEX_ADAPTER_VERSION 0x00000002`。wifi_osi側がv8→v9で5フィールド
  追加された前例（実施10の変更内容そのもの）を踏まえると，coex側も
  同時に拡張されていておかしくない（Espressifは通常，`esp_wifi`/`esp_phy`/
  `esp_coex`の3コンポーネントをセットでバージョンアップする）。
- もし v9の`coex_adapter_funcs_t`が末尾（`_magic`直前）にフィールドを
  追加していた場合，候補1と**全く同型のバグ**が再発する：未初期化
  フィールドはゼロ初期化されるため**コンパイルは通る**が，構造体サイズが
  ずれ，`_magic`（`private/esp_coexist_adapter.h`にも同名の末尾マジック
  規約がある）が誤ったオフセットで読まれる。
- ただし実施10の観測（クラッシュではなく「エラーコードを返してrollback」）
  から見て，`coex_pre_init()`/`esp_coex_adapter_register()`呼び出し自体は
  `esp_wifi_init`より**前**（`wifi_scan.c:135` `esp_shim_coex_adapter_register()`）
  に実行されすでに通過している可能性が高く，仮にcoex側の構造体もズレて
  いたとしても，それがそのまま「NULL関数ポインタ呼び出しによるクラッシュ」
  ではなく「値チェックによるソフトエラー」として現れるかは未検証。

### 確度：中
候補1ほど確度は高くない（v9でcoex側が本当に拡張されたかは未確認＝
docs/コミットメッセージに明記が無い）。ただし「同じ移行作業で片方だけ
更新され，もう片方が置き去りになる」というミスのパターンとして，
候補1が正しければ姉妹バグとして疑う価値が高い。

### 実機での安価な判別方法
- `esp_shim_coex_pre_init_entered`/`esp_shim_coex_pre_init_done`/
  `esp_shim_coex_pre_init_ret`（`esp_coex_adapter.c:203-207`で定義済みの
  診断用グローバル）を**JTAGでクラッシュ直後に読む**。
  `entered=1・done=1・ret=0`であれば，coexアダプタ登録は正常に完了して
  おり，この候補はその時点では否定される（0x3001の原因はcoex呼び出し
  そのものではなく，そのあとのwifi_osi_funcs整合性チェック側にある可能性
  が高まる＝候補1を優先すべき根拠が増える）。
  `ret!=0`ならば「なぜ失敗したか」の切り分けに直結する（syslogに
  "coex_pre_init -> %d" が出るはずだが，これも副次課題6のFIFO問題で
  読めない可能性があるため，構造化ログバッファ直読み＝下記候補4の
  「安価な判別方法」を援用するとよい）。
- `nm -S`で`g_coex_adapter_funcs`のサイズを`nm -S build/.../asp.elf`
  （C5ビルド）とstock C5#2ビルドで比較。

---

## 候補3（中〜低確度）：sleep-retention no-opスタブの戻り値意味論

### 該当箇所
`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c:1078-1108`
```c
static void regdma_link_set_write_wait_content_wrapper(void *link, uint32_t value, uint32_t mask)
{ (void)link; (void)value; (void)mask; }
static void *sleep_retention_find_link_by_id_wrapper(int id)
{ (void)id; return NULL; }
static int32_t wifi_bb_sleep_retention_attach_wrapper(void)  { return 0; }
static int32_t wifi_bb_sleep_retention_detach_wrapper(void)  { return 0; }
static int32_t wifi_mac_sleep_retention_attach_wrapper(void) { return 0; }
static int32_t wifi_mac_sleep_retention_detach_wrapper(void) { return 0; }
```

### 検討
- `_regdma_link_set_write_wait_content`（戻り値void）・
  `_sleep_retention_find_link_by_id`（NULL返し）はv8でも既に存在する
  フィールド（`hal/.../wifi_os_adapter.h:152-155`）で，本ポートの**実体**
  （`hal/components/esp_hw_support/sleep_retention.c:354`
  `void * sleep_retention_find_link_by_id(int id)`，
  `hal/components/esp_hw_support/port/regdma_link.c:535`
  `void regdma_link_set_write_wait_content(...)`）を見ると，
  どちらも「（内部リストが空なら）NULLを返す／no-op」という実装が
  **正規の初期状態としてあり得る**（sleep retentionモジュールが
  一つも登録されていない状態のシステムでは`sleep_retention_find_link_by_id`
  はNULLを返すのが正常動作）。**この2つは確度が低い**（元々v8でもNULL
  のまま未設定だったとコメントされており，C6ではこれで動いている）。
- 新設4フィールド（`_wifi_bb/mac_sleep_retention_attach/detach`）は
  v9固有で実体が不明。真のESP-IDF実装（本環境には無い）は，恐らく
  `sleep_retention_module_init()`/`sleep_retention_module_allocate()`
  相当を呼び出し `esp_err_t`（`ESP_OK=0`が成功）を返す**か**，
  「アタッチ成功したか」を`bool`（`true`=成功）で返す設計か，
  ヘッダが無いため断定できない。
  - hal内の類似ドメイン関数（`esp_hw_support/lowpower/port/esp32c5/
    sleep_modem_state.c`の`sleep_modem_state_phy_link_init`）は
    軒並み`esp_err_t`（0=ESP_OK成功）を返す規約なので，**「0=成功」で
    ある可能性の方がやや高い**と評価する（＝現状の実装は恐らく正しい）。
  - ただし`wifi_osi_funcs_t`内の他のFreeRTOS由来フィールド
    （`_mutex_lock`・`_task_create`・`_semphr_take`等）は軒並り
    「1=成功／0=失敗（pdPASS/pdFAIL）」規約であり，命名パターンだけでは
    どちらの流儀に従うか断定できない。

### 確度：中〜低
ユーザー提示の最優先観点だが，実体調査の結果「0固定」は**恐らく無害
（0=ESP_OK系）**という結論に傾く。ただし断定はできないため中確度で残す。

### 実機での安価な判別方法
- これらの関数は**scan専用パス（PM/sleep無効）では呼ばれない想定**と
  コメントされている（`:1071-1076`）。実際に呼ばれているかをJTAGで
  安価に確認できる：各wrapperの先頭に**到達すること自体が既に情報**
  なので，OpenOCDで4関数それぞれに`bp`を張り，`esp_wifi_init`実行中に
  ヒットするかを見る。**1回もヒットしなければ本候補は完全に容疑から
  外れる**（最も安く候補1本命を裏付ける反証実験になる）。
- ヒットした場合は，戻り値レジスタ（`a0`）をセットする`li a0,0`直後の
  `ret`命令にbpを置き，**呼び出し元（blob側）の直後の分岐命令**
  （`beqz`/`bnez` a0, ...）をディスアセンブルして「0のときどちらに
  飛ぶか」を確認すれば，意味論を実機disassemblyだけで確定できる
  （ハードウェアの実際の状態は不要＝安価）。

---

## 副次課題6：syslog FIFO詰まり／エラーテキスト消失

### 発見1（実装バグ）：新設`printf()`weakスタブが「コメントの主張」と実装が食い違う
`asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c:502-530`
```c
/*
 * (2) printf：IDF v6.1 の libcoexist.a が...
 *     診断可視化のため syslog へ折り返す。
 */
#include <stdarg.h>
extern int vsnprintf(char *buf, size_t size, const char *format, va_list ap);

int __attribute__((weak))
esp_wifi_skip_supp_pmkcaching(void) { return(0); }

int __attribute__((weak))
printf(const char *format, ...)
{
	(void)format;
	return(0);	/* coex ログは破棄（C6 wifi_trace.c と同じ扱い） */
}
```
コメントは「syslogへ折り返す」と書いているが，**実装は`format`も
可変引数も一切使わず即`return(0)`しているだけ**（`vsnprintf`のextern宣言
も本体で一度も使われていない＝完全に死んでいる）。libcoexist.aが
`coex_pre_init`/`coex_rom_osi_funcs_init`/`esp_coex_adapter_register`内で
出す診断出力は**このスタブによって100%握りつぶされている**。0x3001の
直接原因ではない可能性が高い（コード上は昔からこうだったC6 wifi_trace.c
のno-op踏襲でもあるが，コメントに反して実装されていない点は実際の
バグ＝修正候補）。

### 発見2：低レベルコンソール出力は「割込み禁止のまま」ポーリングし，
ホスト未ドレイン時は最大5000回×100nsのリトライ後に**文字単位で破棄**する
`asp3/arch/riscv_gcc/esp32c5/chip_serial.c:180-199`（`esp32c5_sio_fput`）。
これは`asp3/asp3_core/syssvc/syslog.c:121-157`（`syslog_wri_log`）の
`SIL_LOC_INT()`〜`SIL_UNL_INT()`の**割込み禁止区間内**で呼ばれる
（syslog.cはsubmodule＝編集不可）。ホストがUSB CDCを速やかにドレインし
ていない状況で高頻度ログが出ると，1文字あたり最大500us，1行（数十文字）
で最大数十msの割込み禁止が生じうる（実施06での「usbjtagドレイン有無で
挙動が変わる」という既出の実測とも整合する）。**文字単位で無応答なら
黙って捨てる仕様のため，メッセージの一部だけが欠落する＝「文字化け」に
見える動作として説明可能**。

### 提案（安価・実装コスト小・カーネル非改変）
1. **`printf()`スタブを本来の意図通りに直す**（`vsnprintf`→`syslog(LOG_NOTICE,"%s",buf)`
   へ実際に転送する。1行の変更で済み，libcoexist内部の診断が見えるようになる
   可能性がある）。
2. **JTAGでの構造化ログ直読み（最も安価・確実・console/USB状態に非依存）**：
   `asp3/asp3_core/syssvc/syslog.c`の`syslog_buffer[]`（静的配列，
   `TCNT_SYSLOG_BUFFER=128`個・`SYSLOG{logtype,logtim,logpar[6]}`）は，
   **コンソールへの出力が失敗・欠落しても記録自体は残っている**
   （`syslog_wri_log`はバッファへの記録→低レベル出力の順で，バッファ
   記録は無条件に先に完了する）。クラッシュ直前にJTAGで
   `syslog_buffer`・`syslog_head`・`syslog_tail`・`syslog_count`・
   `syslog_lost`をダンプすれば，**画面に出なかった/化けたメッセージの
   原文（フォーマット文字列ポインタ＝`logpar[0]`とその引数）をそのまま
   読める**。`logpar[0]`は`.rodata`内の文字列アドレスなのでELFと突き合わ
   せれば人間可読化できる。RTC_SWDTリセットは（実施04-07で確認済みの
   CPU_LOCKUPと違い）比較的緩やかなため，OpenOCDでの非同期halt猶予も
   期待でき，実行可能性が高い。
3. `esp_shim.cfg`側の`syslog_logmask`/`syslog_lowmask_not`設定
   （target_kernel.cfg等）を確認し，低レベル即時出力とリングバッファの
   両方が同一優先度で有効になっていないか確認する。両方有効だと
   同一メッセージが「即時ポーリング出力」と「logtaskの非同期出力」で
   二重に，かつ非同期にインターリーブして出力されうる（文字化けの
   もう一つの説明候補）。immediate出力をERROR以上に絞り，INFO/NOTICE
   はリングバッファ＋logtask単独出力に一本化すると，出力が二重化せず
   まとまった行として出るようになる可能性が高い。

---

## 反証済み（確認したが問題なしと判断した項目）

### a. `wifi_init_config_t` のヘッダ混入（v8/v9混在）— 確度低・ほぼ否定
`esp_wifi.cmake`全文を確認した結果，`esp_wifi/include`パスは
**C5ビルドでは`${IDF}/components/esp_wifi/include`（:147）の1箇所のみ**
追加されている（`${ESP_HAL_DIR}/components/esp_wifi/include`は削除済み
＝旧C6版の`esp_wifi.cmake:100`と対比して確認）。`wifi_init_config_t`
（`apps/wifi_scan/wifi_scan.c:78`）も`esp_wifi_init_internal`本体
（`${IDF}/components/esp_wifi/src/wifi_init.c`）も**同一のIDF v6.1
ヘッダ経由**でコンパイルされるため，`magic`/`feature_caps`/`osi_funcs`
ポインタの型不整合は起きない。

### b. NVS関連スタブの戻り値 — 確度低・ほぼ否定
`WIFI_INIT_CONFIG_DEFAULT()`の`nvs_enable`フィールドは
`CONFIG_ESP_WIFI_NVS_ENABLED`依存（`hal/.../esp_wifi.h:174-178,323`）。
C5の`sdkconfig_stub/sdkconfig.h:58`は明示的に
`#define CONFIG_ESP_WIFI_NVS_ENABLED 0`としており，`nvs_enable=0`で
初期化される。blobはこのフラグを見てNVS経路を最初から使わない設計の
はずなので，`esp_shim_blobglue.c`のNVSスタブ群（すべて`ESP_ERR_NVS_
NOT_INITIALIZED`固定）が0x3001の原因になっている可能性は低い。

---

## その他の申し送り（`git show 45f7532`全文レビューより）

- **mbedtls/wpa_supplicantをhal(v8/4.0.0)に据え置く判断**（`esp_wifi.cmake:383-399`）
  は，今回のクラッシュ地点（`esp_supplicant_init`より**前**）には無関係。
  ただし候補1（あるいは候補2）を解消して`esp_wifi_init`が完走した場合，
  次に`esp_supplicant_init`〜WPA2ハンドシェイクの段階で，v9 blob（`esp_wifi`
  ネームスペース）とhal(v8)の`wpa_supplicant`/`mbedtls`が**再び**別世代の
  ABIを共有する箇所が無いか（特に`esp_wifi_types_generic.h`のような
  wifi↔supplicant境界の構造体）は，次段の壁として事前に注意喚起して
  おく価値がある。
- **`freertos_stub/`のヘッダ群**は型・マクロの供給のみが目的で，実行時
  の意味論に影響する箇所は`portmacro.h`の`portENTER/EXIT_CRITICAL`系
  のみ。これは`esp_shim_int_disable/restore`へ委譲されており，PHY/coex
  スピンロックとしては（シングルコアという前提の下で）意味的に正しい。
  0x3001とは無関係と判断。
- **ROM ldから`systimer.ld`を除外した判断**（`esp_wifi.cmake:359-361`，
  「IDF v6.1のesp32c5 ldセットには存在しない」）は，実機でリンクが
  成功している以上（実施10でリンク成功を確認済み）結果として妥当と
  見てよい。ただしこれも実物のIDF v6.1 ldディレクトリで裏取りできて
  いない推定である旨は明記しておく。

---

## 付録：主要ファイルパス一覧
- `$HOME/asp3_esp_idf/asp3/target/esp32c5_espidf/esp_wifi.cmake`
- `$HOME/asp3_esp_idf/asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`
- `$HOME/asp3_esp_idf/asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c`
- `$HOME/asp3_esp_idf/asp3/target/esp32c5_espidf/wifi/esp_shim.c`
- `$HOME/asp3_esp_idf/asp3/target/esp32c5_espidf/sdkconfig_stub/sdkconfig.h`
- `$HOME/asp3_esp_idf/asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c`（C5と共有）
- `$HOME/asp3_esp_idf/asp3/target/esp32c3_espidf/wifi/esp_shim_cfg.h`（C5と共有）
- `$HOME/asp3_esp_idf/asp3/arch/riscv_gcc/esp32c5/chip_serial.c`
- `$HOME/asp3_esp_idf/asp3/asp3_core/syssvc/syslog.c`（submodule・参照のみ）
- `$HOME/asp3_esp_idf/hal/components/esp_wifi/include/esp_private/wifi_os_adapter.h`（v8参照用）
- `$HOME/asp3_esp_idf/hal/components/soc/esp32c5/include/soc/soc_caps.h`
- `$HOME/asp3_esp_idf/hal/nuttx/esp32c6/include/sdkconfig.h`
- `$HOME/asp3_esp_idf/docs/c5-bringup.md`（実施04〜10・別PC再開メモ）

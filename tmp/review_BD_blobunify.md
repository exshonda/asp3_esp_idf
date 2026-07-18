# レビュー：★B（既定flipの安全性）／★D（blob統一機構の正しさ）

対象：`claude/blob-unify-v5.5.4`（+ submodule `feat/esp32c6`@9904a44）。
read-only。参照した一次資料：`docs/blob-unify-v554.md`、
`tmp/review-request.md`、`asp3/target/{esp32c3,esp32c5,esp32c6}_espidf/`
配下の target.cmake・esp_wifi*.cmake・esp_bt*.cmake・
`wifi_v8|wifi/idf_v554_override/esp_private/wifi_os_adapter.h`・
`esp_shim_blobglue.c`・`hal/components/wpa_supplicant/src/rsn_supp/wpa.c`。

---

## 問い1：既定flip（WiFi=v5.5.4 OFF・BT=v5.5.4 ON for C5/C6）の妥当性

**結論：要修正（premise自体が誤り）。C5は妥当。C6は「既定flipされていない」——
レビュー依頼文および`docs/blob-unify-v554.md`§11の記述が実際のコードと食い違う。**

### 根拠（file:line）

C6のBT既定経路は二段ゲートになっている：

1. `asp3/target/esp32c6_espidf/esp_bt.cmake:31`
   `option(ESP32C6_BT_IDF61 "..." OFF)` — **既定OFF**。
2. `esp_bt.cmake:32-34`：
   ```
   if(ESP32C6_BT_IDF61)
       include(${CMAKE_CURRENT_LIST_DIR}/esp_bt_idf61.cmake)
   else()
   ... (以下560行、hal submodule版のBT実装がそのまま続く)
   ```
3. `ASP3_BT_IDF_V554`（既定ON＝v5.5.4）が定義されているのは
   `esp_bt_idf61.cmake:48`のみ——つまり**`ESP32C6_BT_IDF61=ON`を明示指定
   しない限りこのファイル自体がincludeされず、`ASP3_BT_IDF_V554`という
   変数は存在しない**。

`git log`で確認した経緯（`git show e1e965c`）：
- `option(ESP32C6_BT_IDF61 ... OFF)`は commit `210b6db`（「IDF v6.1
  matched-set swap をトグル化して実装」）で導入された、本v5.5.4統一とは
  別目的（C6のRFシンセ非ロック調査用）の既存トグルで、既定OFFのまま
  一度も触られていない。
- v5.5.4統一の「既定flip」コミット`e1e965c`（コミットメッセージ：
  「BT blobをv5.5.4へ統一完了…WiFiと揃い双方v5.5.4既定」）が実際に変更
  したのは2行だけ：`esp32c5_espidf/esp_bt.cmake`の`ASP3_BT_IDF_V554`
  既定OFF→ON、および`esp32c6_espidf/esp_bt_idf61.cmake`（＝
  `ESP32C6_BT_IDF61=ON`のときしか読まれないファイル）内の同名オプション
  既定OFF→ON。**`ESP32C6_BT_IDF61`自体の既定は変更していない**。

実機ヘルパスクリプト（`tmp/c6ble.sh:7,44`）も
`既定 app = ble_host_smoke_c6, v6.1(idf61) matched-set + SM
（-DESP32C6_BT=ON -DESP32C6_BT_IDF61=ON -DESP32C6_BT_IDF61_SM=ON）`と
明記——開発者自身、C6のBLEを試すときは常に`ESP32C6_BT_IDF61=ON`を
**手で追加する**運用になっている＝cmakeの素の既定では届かないことを
実質的に裏付けている。

**帰結**：`-DESP32C6_BT=ON`のみ（他のオプション無指定）でビルドすると、
C6のBTは**hal submodule版**（esp_os_*/`platform/os.h`シム・
`bt/controller/esp32c6/bt.c`・hal同梱`libble_app.a`）——v5.5.4でも
v6.1でもない**第3の世代**——になる。これは
`docs/blob-unify-v554.md`§8のmd5実測（v5.5.4≡v6.1バイト同一，
libcoexist.aのみ相違）が立てた「安全性の論理」の対象にすら入っていない
コードパスであり，かつ§8既述の「hal(v8)のlibphyはPHY較正が収束せず
ハングする」という**この統一作業の出発点そのものの懸念が当てはまる
世代**である。

### C5については妥当

`asp3/target/esp32c5_espidf/esp_bt.cmake:59`：
```
option(ASP3_BT_IDF_V554 "... (default ON=blob統一. v5.5.4 blob=v6.1バイト
同一・C5でcold full-BLE adv実証. OFFでv6.1へ可逆)" ON)
```
ここはゲートが1段のみで、既定ONが直ちに効く。§10-11（cold実機D-1/
full BLE adv実証）の物証もこの経路に対応しており、C5に関する限り
「md5同一＋実機cold実証」の論理は成立している。

### WiFiは3チップとも一貫

`esp32c3_espidf/esp_wifi.cmake:220`・`esp32c5_espidf/esp_wifi_v8.cmake:295`・
`esp32c6_espidf/esp_wifi.cmake:316`いずれも
`option(ASP3_WIFI_BLOB_HAL ... OFF)`（既定OFF=v5.5.4）で一段ゲート。
WiFiに二重ゲート構造は無く、この点はチップ間で整合している。

### 「md5同一なら安全」の論理が成立する範囲（後半設問への回答）

- C5：libble_app.a／libphy.a／libbtbb.aがv5.5.4≡v6.1（md5一致）＋
  cold実機（D-1・full BLE adv）で確認——**成立**。
- C6：md5同一の議論自体は物理的に正しい（実測値は同じ）が、**その安全な
  経路（v6.1 or v5.5.4）が既定で選ばれていない**ため、既定ビルドの
  安全性論証としては無意味（対象外の経路の話をしている）。
- libcoexist.a差の影響（WiFi+BT同時使用未着手）は、現状WiFi/BT排他
  （RAM予算のFATAL_ERROR）のため今は非活性——ただしこれは「今回のflip」
  固有の限界ではなく、統合全体の既存制約。§3で問う。

### 反証条件
`ESP32C6_BT_IDF61`の既定がこの後の別コミットでONへ変更されていれば
（本レビュー時点のHEADには無い）、本指摘の前段は解消する。その場合も
「戻し方（reversible既定に戻すのに2オプション必要）」の複雑さは残るため
問い2で継続指摘。

---

## 問い2：可逆性（reversibility）の全経路担保

**結論：WiFiと C5-BTは可逆機構として正しく設計されている。C6-BTは
「二段オプション」の存在そのものが可逆性の見通しを損ねており、
実務上の「戻し忘れ」リスクを作っている（問い1の帰結と表裏）。**

### WiFi（3チップ共通パターン）：正しい
`esp32c3_espidf/esp_wifi.cmake:220-233`・
`esp32c5_espidf/esp_wifi_v8.cmake:295-311`・
`esp32c6_espidf/esp_wifi.cmake:316-328`：
`ASP3_WIFI_BLOB_HAL`単一オプションが`ASP3_WIFI_BLOB_SRC`
（blob探索先）・`ASP3_WIFI_BLOB_V554`（コンパイル定義）・override
headerディレクトリのPREPEND有無を一括で切替える。全て単一の
`if/else`に閉じており、「片方だけ戻す」不整合の余地がない。

補強確認：
- override headerでシャドウされるのは`wifi_os_adapter.h`1ファイルのみ、
  かつhal版ヘッダには対象フィールド自体が存在しないため、
  `ASP3_WIFI_BLOB_HAL=ON`時にoverrideディレクトリをASP3_INCLUDE_DIRSへ
  追加しない（`esp_wifi_v8.cmake:299-311`のif/else排他）。
- `esp_wifi_adapter.c`・`esp_shim_blobglue.c`のv5.5.4専用コード
  （`wifi_disable_ac_ax_wrapper`・3 stub関数）はいずれも
  `#if ASP3_WIFI_BLOB_V554`でガードされており
  （`esp32c5_espidf/wifi_v8/esp_wifi_adapter.c:1087,1220`、
  `esp32c6_espidf/wifi/esp_wifi_adapter.c:1010,1143`、
  C3/C5/C6の`esp_shim_blobglue.c`各406-426/541-561/490-510行）、
  `ASP3_WIFI_BLOB_HAL=ON`時は二重定義にもならず消える。設計通り
  reversible。
- `sdkconfig_stub/sdkconfig.h`の`CONFIG_SOC_WIFI_HE_SUPPORT=1`
  （C5：`sdkconfig_stub/sdkconfig.h:115`）はhal/v5.5.4どちらの経路でも
  「有効なチップ機能フラグ」として使われる値であり、hal側ヘッダは
  そもそもこのマクロを見ないブロック非存在設計（§blob-unify-v554.md §1）
  なので「戻し忘れて残る」類の危険はない——確認済み。

### C3-BT（既定OFF=hal・opt-in ON=v5.5.4）：可逆機構自体は正しい
`esp32c3_espidf/esp_bt.cmake:52-56`：単一`if/else`で`BT_IDF`を切替え、
149行目のif一段でIDF側追加include（`esp_interface.h`）も同じ変数で
ガード。§12.3の実機A/Bで両方向rc=0を確認済み（doc記載）。ここは設計・
実機とも整合。

### C6-BT：二段オプションが生む「戻し忘れ」構造
`esp32c6_espidf/esp_bt.cmake:31-34`（`ESP32C6_BT_IDF61`）と
`esp_bt_idf61.cmake:48`（`ASP3_BT_IDF_V554`）の**2つの独立オプション**
が直列している。可逆性という観点で問題になる組合せ：

| ESP32C6_BT_IDF61 | ASP3_BT_IDF_V554（idf61内） | 実際に使われるBT実装 |
|---|---|---|
| OFF（既定） | （評価されない） | **hal submodule**（v8, esp_os_*） |
| ON | ON（既定） | v5.5.4 |
| ON | OFF | v6.1 |

「v5.5.4からhalへ完全に戻す」つもりで`-DASP3_BT_IDF_V554=OFF`だけ渡すと
（`ESP32C6_BT_IDF61=ON`を維持したまま）**v6.1に着地し、halには戻らない**
——これは「片方だけ戻すと不整合になる組合せ」の実例であり、依頼文
問い2が懸念した事象そのものが実在する。逆に「素の既定に戻す」つもり
なら両方OFFにする必要があるが、`ASP3_BT_IDF_V554`はそもそも
`ESP32C6_BT_IDF61=OFF`时点で無評価（値を見ても何も起きない）ため、
「今どの経路でビルドされているか」をコード側からは`ESP32C6_BT_IDF61`
一段しか見ずに誤認しやすい。

### 反証条件
運用上、C6のBLEテストは常に`tmp/c6ble.sh`のような固定オプション集合
（`ESP32C6_BT_IDF61=ON`込み）経由でのみ行われ、「素の`-DESP32C6_BT=ON`」
という組合せが実際にテスト・出荷対象から除外されているなら実害は
限定的——ただし、それは「ドキュメントされた既定」と「実際にテストされる
経路」が乖離していることの追認にしかならず、cmakeのoption()既定値を
見て「安全な既定」と信じる次の開発者・レビュアを誤誘導するリスクは
残る。

---

## 問い3：見落とし回帰面

**結論：3点とも実在。特に1点目（C6既定不統一）は問い1の帰結で
最重要、2点目・3点目はdoc既述だが影響範囲の記述が甘い。**

1. **chip横断の既定不統一（依頼文が名指ししたC3-BTだけでなく、
   実質C6-BTも）**：C3のBT既定OFF=halは`docs/blob-unify-v554.md`§12で
   明示され、実機bond失敗という具体的根拠がある「意図された」不統一。
   一方C6-BTの既定が事実上hal（v8）のままなのは問い1で示した通り
   **未文書化・意図せぬ**不統一——「C5/C6はBTもv5.5.4に統一完了」という
   docsの結論（§11見出し）を信じたユーザーが素朴に`-DESP32C6_BT=ON`
   すると、md5同一性の議論もcold実機D-1実証も及んでいないhal(v8)経路
   に着地する。しかもhal(v8)のBTは、この統一作業の出発点（§8冒頭）で
   「libphyがPHY較正で収束せずハングする」と名指しされていた懸念の
   対象そのものであり、C6-BT既定ビルドの信頼性はこのPRのどの実機物証
   にも支えられていない。

2. **libcoexist.a差・coex未着手の前提**：`docs/blob-unify-v554.md`§8.1で
   「相違はlibcoexist.aのみ（BT単体では非活性の濃厚）」と書かれている
   通り、現状はWiFi/BT排他ビルド（`esp32c3_espidf/target.cmake:220`・
   `esp32c5_espidf/target.cmake:178-180`・
   `esp32c6_espidf/esp_bt.cmake`各所のFATAL_ERROR）なので実害は無い。
   ただし「WiFi+BT同時使用」はASP3のロードマップ上いずれ扱う対象で
   あり、その時点でlibcoexist.aの相違（v5.5.4≠v6.1）が新規リスク軸に
   なることは今回のflip判断のドキュメントに一切明記されていない
   （§8.1が言及するのは「今は非活性」までで、「将来coex実装時に
   再監査が必要」という運用上のTODOが立っていない）。

3. **coldとwarmの非対称性の伝播**：C6のcold-PLL問題は「blob非依存の
   C6 silicon/analog固有」（§10）という整理は筋が通るが、C6-BTの
   既定が実際にはhal(v8)である（問い1）ため、「C6はcold PLLの別課題
   さえ解決すればv5.5.4 BTは動く」という含意も同様に**対象外の経路の
   議論**になっている——cold-PLL修正（`esp_bt_idf61.cmake`内の
   `pmu_init.c`移植・§20）はESP32C6_BT_IDF61=ON経路にしか実装されて
   いない（289-296行目）ため、既定経路（hal submodule版）はこの修正の
   恩恵も受けていない。

---

## 問い4：override headerのABI正しさ

**結論：同意（正しい）。verbatimコピー＋designated initializerの組合せ
により、フィールド順序非依存でABIが一致する設計になっている。**

### 根拠

`wifi_v8/idf_v554_override/esp_private/wifi_os_adapter.h`（C5版・196行）
と`wifi/idf_v554_override/esp_private/wifi_os_adapter.h`（C6版・196行）は
**構造体本体（35-196行目）が1バイトも違わずbyte-identical**（コメント
のみ相違。`diff`実測済み）——同一のv5.5.4ソースツリーから取った
`components/esp_wifi/include/esp_private/wifi_os_adapter.h`なのでチップ
非依存の妥当な設計。

- `ESP_WIFI_OS_ADAPTER_VERSION 0x00000008`・`ESP_WIFI_OS_ADAPTER_MAGIC
  0xDEADBEAF`（同ファイル45-46行目）はhal/v5.5.4間で共通（v8系列で一致、
  docs §1既述の前提と整合）。
- 構造体末尾は`int32_t _magic`（187行目）で終端——blobが末尾magicで
  構造体サイズ・レイアウトの整合性チェック
  （`esp_wifi_internal_osi_funcs_md5_check`、コメント19行目に言及）を
  行う設計に対応する終端フィールドが正しく残っている。
- `_wifi_disable_ac_ax`（184-186行目）は`#if CONFIG_SOC_WIFI_HE_SUPPORT`
  ガードで、`_magic`の直前という正しい位置（docs §1のdiff実測と一致）。
- 埋める側（`esp32c5_espidf/wifi_v8/esp_wifi_adapter.c:1098-1224`、
  `esp32c6_espidf/wifi/esp_wifi_adapter.c`同型）は**C99 designated
  initializer**（`._field = value`）で初期化しているため、構造体
  フィールドの実際のオフセットはコンパイラが現在有効なヘッダ定義
  （override時はv5.5.4版、hal時はhal版）から自動的に解決する——ソース
  内での記述順序（`_wifi_disable_ac_ax`が2行目の`_env_is_chip`直後にある
  等）はABIに一切影響しない。これは「手書きの初期化子順序ミス」という
  古典的なABIバグクラスを構造的に排除しており、正しい設計判断。

### C5版とC6版の差異
コメントのみ（C5固有の言及＝「CONFIG_IDF_TARGET_ESP32C5=1はsdkconfig
stub由来」、C6固有の言及＝「hal/nuttx/esp32c6/include/sdkconfig.h由来」
——実装の事実関係を反映した正しい差分）。構造体本体・マクロ定義は
同一で、**あるべき姿と一致**（両チップともSOC_WIFI_HE_SUPPORT=1・
os_adapter v8のため差がある理由がない）。

### サロゲート差替えの範囲確認
`esp_wifi_v8.cmake:308-310`／`esp32c6_espidf/esp_wifi.cmake:325-327`の
`list(PREPEND ASP3_INCLUDE_DIRS ${...}/idf_v554_override)`は
`wifi_os_adapter.h`という同名ヘッダ「だけ」がoverrideディレクトリに
存在し、他の全ヘッダはhal側にフォールバックする設計（docs §1の
「サロゲート最小差し替え」）。ディレクトリ内を確認したところ実際に
`esp_private/wifi_os_adapter.h`1本のみが置かれており（`find`で確認）、
記述通り。

### 反証条件
本評価はESP-IDF v5.5.4の実ヘッダとの逐語比較を「docsの記述」に
依拠している（stock v5.5.4ツリーは本環境に無いため直接diffできない）。
本番環境で`~/tools/esp-idf/components/esp_wifi/include/esp_private/
wifi_os_adapter.h`と本ファイルを`diff`し完全一致することを一度は
確認すべき（docsの「verbatimコピー」という自己申告の検証）。

---

## 問い5：no-op stubの妥当性

**結論：要修正。3関数のうち`esp_wifi_sta_get_ie`は「scanで不要」という
枠を超え、**STA接続（4-way handshake）の`wpa_set_bss()`から無条件に
呼ばれ、RSN IE整合性チェック（downgrade/IE-swap検出）を実質的に
無効化する**——依頼文の懸念（「connect/bond経路で呼ばれないか」）が
的中している。**

### 根拠（file:line）

呼び出し元は`hal/components/wpa_supplicant/src/rsn_supp/wpa.c`
（hal side・編集禁止・v5.5.4統一とは無関係に既存のソース）：

- `wpa.c:2634`：`wpa_set_bss()`冒頭で
  `bool use_pmk_cache = !esp_wifi_skip_supp_pmkcaching();`
  ——**STA接続のたびに無条件で呼ばれる**関数（`wpa_set_bss`はSTA
  connect時の鍵管理設定エントリポイント。scan専用処理ではない）。
- `wpa.c:2761-2764`（`#ifdef`無し・無条件）：
  ```c
  ie = esp_wifi_sta_get_ie(bssid, WLAN_EID_RSN);
  wpa_sm_set_ap_rsn_ie(sm, ie, ie ? (ie[1] + 2) : 0);
  ie = esp_wifi_sta_get_ie(bssid, WLAN_EID_RSNX);
  wpa_sm_set_ap_rsnxe(sm, ie, ie ? (ie[1] + 2) : 0);
  ```
  スタブは常にNULLを返すため、`sm->ap_rsn_ie`は**常にNULL**（接続の
  たびに）になる。
- `wpa.c:1171-1230`（`wpa_supplicant_validate_ie`）：EAPOL鍵交換
  message 3/4のRSN IEを、`sm->ap_rsn_ie`（beacon/ProbeResp時点で
  保存したはずのRSN IE）と突き合わせる整合性チェック。特に
  `wpa.c:1193-1196`：
  ```c
  (ie->rsn_ie && sm->ap_rsn_ie &&
   wpa_compare_rsn_ie(..., sm->ap_rsn_ie, sm->ap_rsn_ie_len,
                       ie->rsn_ie, ie->rsn_ie_len))
  ```
  および`wpa.c:1210-1224`（downgrade攻撃検出コメント「Possible
  downgrade attack detected」）——**`sm->ap_rsn_ie`が常にNULLだと、
  これらの一致検証は`sm->ap_rsn_ie`が真になる条件を満たさず常に
  スキップされる**。つまりRSN IE偽装・ダウングレード検知の一部が
  この統合ビルドでは**常時無効化**されている。

依頼文の想定は「WPA3互換モードやRSNXE overrideが無効化されるだけ」
（`docs/blob-unify-v554.md` §2の記述）だが、コードを実際に追うと
`esp_wifi_sta_get_ie(bssid, WLAN_EID_RSN)`という**無条件の**呼び出し
（`#ifdef CONFIG_WPA3_COMPAT`の外）が同じ関数で使われており、WPA3
固有機能に留まらずWPA2の標準セキュリティチェックにまで影響が及ぶ。

### 実害の程度（限定要因）
- 実機実証は「open scan」のみ（`docs/blob-unify-v554.md` §4：C3/C5/C6
  いずれも認証無しのAPスキャン）——STA接続（WPA2/WPA3 4-way
  handshake）は本ブランチで一度も実機検証されていない。したがって
  「connectで壊れるかどうか」自体はまだ観測されておらず、今回発見した
  のは静的コード解析による**未検証の回帰面**であって「実機で確認された
  障害」ではない（依頼文の実機物証§3と矛盾しない——§3はscanのみを
  範囲としており，本指摘はその範囲外＝STA connectについて）。
- この無効化は「接続を失敗させる」方向ではなく「本来弾くべき
  IE偽装・ダウングレード攻撃を見逃す」方向のセキュリティ低下であり、
  機能的な疎通（連続テストのスコープ）には影響しない可能性が高い
  ため、これまでの「scan完走」実証では検出されない。

### 他のstubの戻り値契約
`esp_wifi_skip_supp_pmkcaching() → false`と
`esp_wifi_is_wpa3_compatible_mode_enabled() → false`は、
コメント通り「その機能が存在しなかった旧世代の暗黙動作」に相当し、
`esp_wpa3.c:50-58`・`esp_hostap.c:112,210,225`（AP側，本ビルドでは
未使用のsoftAP機能）を含め、falseで安全側に倒れる設計——これらは
契約として問題ない。問題は`esp_wifi_sta_get_ie`のNULL戻り値が
「機能拡張の無効化」ではなく「既存のセキュリティ検証ロジックの入力
そのものを潰す」点にある。

### 反証条件
- `sm->rsn_enabled`が本ビルドの構成（PSK/SAE運用）で実際に真になる
  条件、および`sm->proto`の実際の値を実機ログ・app設定から確認して、
  1210-1224行の分岐に実際に到達するかどうかは未確認（コードパス上は
  到達しうることのみ確認）。
- 実際にWPA2-PSK等でSTA connectを1回実機実行し、（a）接続自体が
  成功するか，（b）`wpa_report_ie_mismatch`が呼ばれないことを以て
  「チェックが素通りしている」ことを確認できれば、本指摘を実機で
  裏付けられる（現時点は静的解析のみ）。
- この無効化がhal blob使用時（`ASP3_WIFI_BLOB_HAL=ON`）にも起きるかは
  未確認——hal blobは3関数自体を提供する（nm確認済みとdocsに記載）ため、
  hal blob使用時は本物の実装が呼ばれ、この問題は**v5.5.4選択時
  （既定）にのみ存在する**回帰のはずである。

---

## 総括（呼び出し元向けサマリ用メモ）

- **既定flip妥当性**：C5は物証（md5一致＋cold実機）に支えられ妥当。
  **C6はコード上「既定flipされていない」**——`esp32c6_espidf/
  esp_bt.cmake:31`の`ESP32C6_BT_IDF61`（既定OFF）が上位ゲートとして
  残っており、素の`-DESP32C6_BT=ON`はhal submodule版（v8）に着地する。
  依頼文・docsの「C5/C6ともBT既定v5.5.4」という前提はC6については誤り。
- **可逆性の穴**：C6-BTは二段オプション構造のため、
  `-DASP3_BT_IDF_V554=OFF`だけでは halへ戻らずv6.1に着地する
  ——「片方だけ戻すと不整合」の実例。WiFi・C3-BT・C5-BTの可逆機構自体は
  単一if/elseで正しく設計されている。
- **override ABI**：正しい。verbatimコピー＋designated initializerで
  フィールド順序非依存、C5/C6で構造体本体byte-identical。
- **no-op stub**：`esp_wifi_sta_get_ie`はscan限定ではなくSTA connect
  （`wpa_set_bss`）から無条件に呼ばれ、RSN IEダウングレード/偽装検知を
  無効化する（`wpa.c:1171-1230`）——実機未検証（open scanのみ）の
  回帰面として要フォローアップ。

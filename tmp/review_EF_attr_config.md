# ★E・★F レビュー（帰属の妥当性 / SM_SIGN_CNT・CONFIG監査）

読解範囲：`tmp/review-request.md`（全文）、`docs/wifi-shim-c6.md` 実施90-92
（`docs/wifi-shim-c6.md:13784-14441`）、`docs/config-audit.md`（全文）、
`asp3/target/{esp32c3,esp32c5,esp32c6}_espidf/bt/stub*/include/bt_nimble_config.h`
（4変種）、`hal/components/bt/host/nimble/`（NimBLEソース・Kconfig.in）、
`hal/components/esp_wifi/Kconfig`・`hal/components/esp_phy/Kconfig`。
read-only。ビルド・実機操作なし。

---

## 問い1・2（★E：実施92の帰属のrigor／別解釈の排除）

### 結論：**要追加検証**（「結論は妥当・理由づけの手続きに穴」という実施92自身の
実施91への評価と同型の指摘が、実施92自身にも当てはまる）

実験デザインを表にすると：

| セル | 操作 | 結果 |
|---|---|---|
| cell1（`docs/wifi-shim-c6.md:14305-14324`） | v5.5.4、1回flash→5回連続RTS | 5/5 clean（boot1含む） |
| cell2（`:14330-14337`） | hal(v8)、1回flash→5回連続RTS | boot1のみcrash、boot2-5 clean |
| firstboot（`:14357-14364`） | v5.5.4、各ブート前に**再flash**→RTS 1回 | 8/8 crash |
| 決定実験（`:14366-14377`） | 両blob、`--after no-reset` flash→RTS 1回のみ | v5.5.4 9/9・hal 6/6、計15/15 clean |

**指摘1：cell1のboot1とfirstbootは名目上「同一の反応構造」のはずが結果が逆**。
cell1のboot1（v5.5.4，flash直後の最初のRTS）とfirstbootの各試行（v5.5.4，
reflash直後の最初のRTS）は、書き方の上では両方とも「flashのauto-boot→
直後にRTSで捕捉」という同一シーケンスに見える。しかしcell1のboot1は
**1/1 clean**、firstbootは**8/8 crash**。文書はこの矛盾を明示的に検討して
いない。唯一の弁別変数として提示された「direct-reset＝走行中アプリを
2度目のリセットで中断したか」（`:14284-14287`, `:14363-14364`）という
枠組みでは、cell1のboot1もfirstbootの各試行も「flash起因の初回起動を
RTSで中断した」という同一条件のはずで、この理論単独では両者の結果差を
説明できない。考えられる交絡（未排除）：
  - cell1とfirstbootで実際に使用したesptoolのflashオプション
    （`--after`挙動）が異なっていた可能性（決定実験でのみ明示的に
    `--after no-reset`と記載、cell1/2やfirstbootのflash呼び出しの
    正確なフラグはdoc本文に記載がない。§末尾「変更ファイル」
    （`:14432-14440`）に挙げられた`cap.py`／`firstboot.py`／`single.py`
    はスクラッチ（本リポジトリ非管理）であり、本レビューでは実体を
    確認できない＝**再現性検証不能**）。
  - cell1の「5回連続RTS」の各RTS間隔（アプリがrescanループに達してから
    次のRTSを打つまでの経過時間）と、firstbootの「flashの内部reset→
    再flashツールの終了→即RTS」の間隔が異なる可能性＝「2度目のリセット
    である」という2値変数ではなく「割り込まれた時点でのWiFi HW状態
    （PHY/MAC初期化のどのフェーズか）」という連続変数が真の弁別因子
    かもしれない。実施92自身がcrash機序として「直前ブートのWiFi HW
    （MAC/modem/PHY）が稼働/設定途中でRTSリセットに中断され」
    （`:14381-14383`）と述べており、まさに「タイミング窓」仮説と
    表裏一体。これは§9で「一次特定は未実施」（`:14425-14427`）と
    留保されているが、留保するなら結論部（先出し結論3・4，`:14284-14294`）
    でも「弁別変数＝2度目のリセットという事実」と言い切るのではなく
    「2度目のリセットの発生（またはタイミング窓の存在）」と条件を
    緩めるべきだった。

**指摘2：hal(v8)側は「double-reset」条件でN=1のみ**。v5.5.4は
firstboot（真の二重リセットハーネス）で8/8という十分なNで再現している
一方、halは同じ`firstboot.py`では試験されておらず（doc中に記載なし）、
cell2の「連続RTS」条件下でのboot1一回のクラッシュ（`:14330-14337`）のみが
根拠。プロジェクトのrigor原則（1実験1機構・N反復・両blob）に照らすと、
**「blob非依存」の主張はv5.5.4側はN=8、hal側はN=1という非対称なエビデンス
の上に成り立っている**。halがfirstboot条件下でも真に8/8等の水準で
再現するかは未検証＝反証可能性が残る（もしhalがfirstboot条件で
クラッシュ頻度がv5.5.4と有意に異なれば、「blob非依存」ではなく
「blob依存の頻度差」という第三の解釈が生き残る）。

**指摘3：過去ラウンドとの不整合が未解消のまま先祖返りしている**。
本ラウンドは実施91 §5（`docs/wifi-shim-c6.md:14141-14164`）の
「JTAG_CPUリセット(0x18)限定で決定的」という帰属を「RTSでも
再現するため不完全」と正しく修正した（`:14292-14294`）。しかし
**実施91 §6（`:14166-14190`）で報告済みの、同一signature系統の
単発クラッシュ**（`:14178-14190`＝再flashなし・RTSリセットのみの
2回目起動でGuru Meditation発生、直後の3回目RTSリセットで正常復帰）
は、構造的には本ラウンドの「double-reset（走行中アプリをRTSで
中断→次ブートがWiFi初期化中にnull跳躍）」仮説と**整合するはずの
先行事例**であるにもかかわらず、実施92はこれを再検討・再分類
していない。実施91 §6はこの事象を「実施90の«単発の未帰属クラッシュ»
と同系統・本ラウンドの主目的とは別の予存事象」（`:14181-14184`）
として片付けており、実施92もこの整理を追認したまま踏襲している
（実施92本文に実施91 §6への言及なし＝§5にのみ言及）。
「相関→因果の早合点」を疑って過去の帰属を洗い直す本ラウンドの
姿勢からすると、**§6も同じ洗い直しの対象にすべきだった**——もし
§6の事象も「直前ブートが走行中にRTSで中断された」という条件に
実際に合致するなら、それは3ラウンド（90/91/92）にまたがって
同一現象を「別の予存バグ」「JTAG限定」「double-reset」と
繰り返しラベルを貼り替えてきたことになり、むしろ本ラウンドの
理論の**追加の支持証拠**になり得たはずである。逆に合致しないなら、
「double-reset」だけでは説明できない第4の発生条件がまだ残っている
ことになる。どちらであれ、**未検討のまま「blob非依存・double-reset
由来で確定」と言い切るのは早い**。

**指摘4：実施90の原初事象（`:13956-13961`）は「観測外のため帰属不能」
と明記されており、本ラウンドの理論を検証する材料にも反証する材料にも
使えない**——これ自体は文書が正直に「観測外」と書いている点は良いが、
実施92の結論部がこの「未解決の一件」に触れずに「pc=0 crashは調査
ハーネスのdouble-resetアーティファクト」と一般化している点は、
実施90由来の事例まで含めて片付けたように読める書き方になっている。

### 反証条件（このレビューが妥当性を撤回する条件）
- `cap.py`／`firstboot.py`／`single.py`の実際のesptool呼び出し
  （`--after`引数・RTS間隔のタイミング）を確認し、cell1のboot1と
  firstbootが真に同一シーケンスだったと確認できれば、指摘1は解消する。
- halをfirstboot条件（真の再flash型double-reset）でN≧8程度試験し、
  v5.5.4と同等の頻度でクラッシュすれば指摘2は解消し「blob非依存」の
  主張が強化される。
- 実施91 §6・実施90の単発事象を再現条件付きで再現実験し、
  double-reset条件と整合する（または整合しない）ことを明示すれば
  指摘3は解消する。

### 実機物証との両立性
§3の実機物証（v5.5.4がsingle-resetで20 AP完走）はこのレビューでも
否定していない——**「v5.5.4既定は出荷経路で健全」という実務判断
（結論4・6）自体は本レビューでも支持できる**。疑義があるのは
「crashの機序をdouble-resetのみに完全に帰属した」という**説明の
確実性の水準**であり、実務判断（既定maintain）を覆すものではない。

---

## 問い3（★F：SM_SIGN_CNTの副作用）

### 結論：**同意**（`#if`文脈以外での副作用は確認されず、4変種とも整合）

`hal/components/bt/host/nimble/` 内で`SM_SIGN_CNT`を参照する箇所は
以下の3箇所のみ（grep確認・網羅）：

1. `hal/components/bt/host/nimble/port/include/esp_nimble_cfg.h:2272-2276`
   ```
   #ifndef MYNEWT_VAL_BLE_SM_SIGN_CNT
   #ifdef CONFIG_BT_NIMBLE_SM_SIGN_CNT
   #define MYNEWT_VAL_BLE_SM_SIGN_CNT  CONFIG_BT_NIMBLE_SM_SIGN_CNT
   #else
   #define MYNEWT_VAL_BLE_SM_SIGN_CNT (0)
   ```
   `#ifdef`ガード付きfallback＝安全（未定義でも展開エラーなし）。
2. `hal/components/bt/host/nimble/nimble/nimble/host/src/ble_sm.c:2721`
   （`#if MYNEWT_VAL(BLE_SM_SIGN_CNT)`）＝
   `ble_sm_incr_our_sign_counter`/`ble_sm_incr_peer_sign_counter`の
   **関数定義全体**を囲むガード。
3. `hal/components/bt/host/nimble/nimble/nimble/host/src/ble_att_svr.c:2548`
   （`#if MYNEWT_VAL(BLE_SM_SIGN_CNT)`）＝
   `ble_sm_incr_peer_sign_counter`呼び出し1箇所のみを囲む。

呼び出し元プロトタイプ／マクロ定義側
（`ble_sm_priv.h:399-400`が宣言、`:418-419`が代替マクロ
`#define ble_sm_incr_our_sign_counter(conn_handle) BLE_HS_ENOTSUP`等）は
**別の親ガード（`NIMBLE_BLE_SM`）**で管理されており、`SM_SIGN_CNT`とは
独立＝プロトタイプは常に存在するため宣言と定義の整合性に問題はない。
呼び出し側は`ble_att_svr.c:2548`（受信側signed write）と
`ble_att_clt.c:845`（送信側，`ble_sm_incr_our_sign_counter`）のみで、
いずれも自身の`#if MYNEWT_VAL(BLE_SM_SIGN_CNT)`で自己完結してガード
されている（`ble_att_clt.c`側も同様のガードを確認）。
**配列サイズ・構造体レイアウト・sizeof等、`#if`以外の文脈での使用は
0件**。したがって`=1`化によるABI変化・構造体レイアウト変化は無い
（config-audit.md §3.4で指摘された「ガード無しマクロ」の危険パターン
＝`SM_SC_DEBUG_KEYS`型とは異なり、`SM_SIGN_CNT`は`esp_nimble_cfg.h`側で
`#ifdef`ガードが明示的に存在するため、その点でもより安全）。

4変種の値・文脈は完全に整合している（全て`#define CONFIG_BT_NIMBLE_SM_SIGN_CNT 1`，
コメントも同一文言＝`docs/config-audit.md`への相互参照込み）：
- `asp3/target/esp32c3_espidf/bt/stub/include/bt_nimble_config.h:204`
- `asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h:196`
- `asp3/target/esp32c6_espidf/bt/stub/include/bt_nimble_config.h:192`
- `asp3/target/esp32c6_espidf/bt/stub_idf61/include/bt_nimble_config.h:200`

Kconfig側の`default y`根拠も本サンドボックスの`hal/`submodule内
`Kconfig.in`で独立に再確認できた：
`hal/components/bt/host/nimble/Kconfig.in:1298-1304`
```
config BT_NIMBLE_SM_SIGN_CNT
    bool "Enable Sign counter operations"
    default y
    depends on BT_NIMBLE_ENABLED
```
（config-audit.md は `~/tools/esp-idf-v6.1` 版Kconfig.inを一次参照
としているが、本環境にそのツリーは無い＝**stock ESP-IDF不在**。
上記はhal submodule同梱版での代替確認であり、config-audit.mdの
記載と数値・行番号は一致する）。

### 反証条件
実機でSigned Write（CSRK使用のATT Signed Write Command）を実際に
送信するセントラル（BlueZ設定変更等）でreplay耐性の実機テストを
行い、sign counter更新が機能することを確認すれば「副作用なし」の
確証が完全になる（現状は静的解析のみ・doc自身も§3.11.2で
「未確認」と明記，`docs/config-audit.md:371-373`）。

---

## 問い4（★F：config-audit.mdが見落としている同型CONFIG差分）

### 結論：**1件、方法論上の見落としを検出**（`CONFIG_BT_NIMBLE_GATT_CLIENT`）。
ただし実害の検証まで行った結果、**現状は機能的リスクなし**（下記）。
SM/HS系統では他に新規の見落としなし。WiFi HE/PHY系統でも新規の
「静かな機能欠落」は検出されなかった（既知の設計判断／自己防御パターン
に帰着）。

### 4.1 検出手順（config-audit.mdと同じ機械的突合を再実施）

`esp_nimble_cfg.h`が条件参照する`CONFIG_BT_NIMBLE_*`（117件，
config-audit.md §3.12と同数を確認）と、`Kconfig.in`の`default y`
（54件）の積集合＝34件を抽出し、4変種すべてで未定義のものを
洗い出した（config-audit.mdの§3.11と同一手法）。結果、
config-audit.md §3.11が検討した6件
（`CHK_HOST_STATUS`／`RECONFIG_MTU`／`SM_SIGN_CNT`／
`HIGH_DUTY_ADV_ITVL`／`HANDLE_REPEAT_PAIRING_DELETION`／
`HOST_QUEUE_CONG_CHECK`）を含む34件中、以下を除き大半は
「マスタースイッチ側も未定義＝機能自体使用外」（`HS_FLOW_CTRL`系，
`config-audit.md:235-246`で既出）か「Mesh/ISO等，3チップとも
未実装」（`config-audit.md:380-397`で既出，網羅実証はしないと
明記済み）に帰着し新規性なし。

### 4.2 新規検出：`CONFIG_BT_NIMBLE_GATT_CLIENT`

- Kconfig既定：`hal/components/bt/host/nimble/Kconfig.in`に
  `BT_NIMBLE_GATT_CLIENT`のエントリを確認（`default y`，
  config-audit.mdの`defaulty`抽出と一致）。
- fallback（`esp_nimble_cfg.h:263-267`）：
  ```
  #ifndef CONFIG_BT_NIMBLE_GATT_CLIENT
  #define MYNEWT_VAL_BLE_GATTC     (0)
  #else
  #define MYNEWT_VAL_BLE_GATTC     (CONFIG_BT_NIMBLE_GATT_CLIENT)
  #endif
  ```
- **4変種すべてで`CONFIG_BT_NIMBLE_GATT_CLIENT`未定義**（C3
  `bt/stub/include/bt_nimble_config.h`、C5同、C6hal同、C6idf61同、
  いずれもgrep 0件）＝`MYNEWT_VAL(BLE_GATTC)=0`にfallback。
- **消費側`.c`（`ble_gattc.c`）は4変種すべてで実際にリンクされている**
  ことを確認：
  - `asp3/target/esp32c3_espidf/esp_bt.cmake:442`
  - `asp3/target/esp32c5_espidf/esp_bt.cmake:503`
  - `asp3/target/esp32c6_espidf/esp_bt.cmake:523`
  - `asp3/target/esp32c6_espidf/esp_bt_idf61.cmake:492`

  これはconfig-audit.md §3.11自身が挙げた抽出条件
  （「117件のうち未定義かつ消費側`.c`が実際にリンクされているもの」，
  `docs/config-audit.md:304-311`）に**形式的に合致する**にもかかわらず、
  §3.11の候補表（`config-audit.md:313-320`，6件）に含まれていない。
  すなわち**config-audit.mdが自ら定めた抽出基準を、この1件については
  適用し漏らしている**（要修正候補: config-audit.md自体の網羅性の穴）。

- **実害の検証**（config-audit.mdの判定手順§3.0に倣って実施）：
  `ble_gattc.c`内の`MYNEWT_VAL(BLE_GATTC)`使用は全て`#if`ガード
  （`hal/components/bt/host/nimble/nimble/nimble/host/src/ble_gattc.c:71`
  他多数、ファイル冒頭から末尾`:5620`の`#endif`まで一貫して
  `#if MYNEWT_VAL(BLE_GATTC)` [単独] または
  `#if MYNEWT_VAL(BLE_GATTC) || MYNEWT_VAL(BLE_GATTS)` [OR]
  の形でガードされている＝GATTC=0でも該当関数は単に
  コンパイルアウトされるだけで、GATTS側（サーバ機能）には影響しない
  ことをソースで確認）。
  さらに本リポジトリの`asp3/`配下（`hal/`・vendored nimbleを除く）を
  全文grepしたが、`ble_gattc_disc`／`ble_gattc_write`／`ble_gattc_read`
  等のGATTクライアントAPI、および接続を開始する`ble_gap_connect`の
  呼び出しは**0件**＝アプリ層はセントラル接続／GATTクライアント手続きを
  一切使用していない。
  さらに4変種の`bt_nimble_config.h`はいずれも
  「GATTCは未使用（本ビルドはPERIPHERALロールのみ）のため0のまま」
  という趣旨のコメントを既に持つ（例：
  `asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h:70-78`）
  ＝**開発者自身は既にこの欠落を認識し、意図的判断として記録済み**。

  **結論：`GATTC`未定義はconfig-audit.md（§3.11候補表）の網羅性としては
  抜けだが、PVCY型（silent機能消失で実害あり）には該当しない**
  ——PVCYは「今後有効化される予定の機能が静かに壊れていた」ケースで
  実害があったが、GATTCは「そもそも設計上使わない機能」であり、
  ロール構成（`CONFIG_BT_NIMBLE_ROLE_CENTRAL=1`は4変種とも設定済みだが、
  実際にセントラル接続を開始するアプリコードが存在しない）とも矛盾
  しない。ただし今後central/GATTクライアント機能を追加する開発者は、
  `CONFIG_BT_NIMBLE_GATT_CLIENT`を明示的に1にしないと
  `ble_gattc.c`が事実上空コンパイルされたままになる点は申し送り事項。

  【留保】config-audit.mdはS3姉妹プロジェクト（本レビュー環境では
  参照不可＝`/home/honda/TOPPERS/esp32_s3`は本サンドボックス外）を
  含む5変種比較を謳っている。S3が`GATT_CLIENT`を定義していれば
  config-audit.mdの「5変種共通欠落」という抽出基準からは外れる
  （4/5変種欠落は基準未達）ため、除外は意図的だった可能性もある。
  ただし**この4変種（本リポジトリの対象そのもの）に限れば挙動は同じ**
  であり、レビュー対象範囲では指摘の実益は変わらない。

### 4.3 その他チェック済み・新規性なしと判定した項目

- **WiFi SAE/OWE系**（`CONFIG_ESP_WIFI_ENABLE_SAE_H2E`/`_SAE_PK`/
  `_SOFTAP_SAE_SUPPORT`/`_ENABLE_WPA3_OWE_STA`、いずれもKconfig
  `default y`：`hal/components/esp_wifi/Kconfig:310-337`）は
  C3/C6 `hal_stub/include/nuttx/config.h:61-65`およびC5
  `sdkconfig_stub/sdkconfig.h:78-82`で**明示的に0固定**されており、
  「SAE_PK／SAE_H2E／SoftAP SAE／OWE STAは未対応（0固定）とする方針」
  という趣旨のコメントが両ファイルに存在＝**未定義によるsilent
  fallbackではなく明示的なポリシー決定**。config-audit.md §6の
  表（`config-audit.md:451-463`）は「C3/C6とC5の値が一致」の確認に
  留まりKconfig既定との突合はしていないが、実体は「意図的に0へ
  override」であって「定義し忘れ」ではないためPVCY型には該当しない。
  （なお、C3/C6側は`CONFIG_ESPRESSIF_WIFI_*`という別名前空間で供給し、
  vendor提供の`hal/nuttx/esp32c3(または c6)/include/sdkconfig.h`側で
  `#define CONFIG_ESP_WIFI_ENABLE_SAE_H2E CONFIG_ESPRESSIF_WIFI_ENABLE_SAE_H2E`
  のような転写マクロを経て実際のESP-IDF名前空間へブリッジされている
  ことも確認済み＝名前空間の齟齬による見せかけの欠落ではない）。
- **WiFi ENTERPRISE/FTM/RRM/WNM/WPA3_COMPATIBLE系**
  （`hal/components/esp_wifi/Kconfig`で`default y`）は4変種とも
  完全未定義（vendor sdkconfig.hにも無し）だが、消費箇所
  （`hal/components/esp_wifi/include/esp_wifi.h:233-283`）は
  `WIFI_FTM_INITIATOR`等の**定数マクロ定義そのもの**を`#if`で
  ガードするだけで、これらの定数を参照するアプリコードは
  `asp3/`に存在しない（grep 0件）＝もし将来コードが参照すれば
  「未定義識別子」でハードなビルドエラーになる自己防御パターン
  （config-audit.md §3.0が「ビルドが割れるものは低優先」とした
  基準と同型）。silentな機能消失ではないため新規指摘としない。
- **PHY較正データ永続化**
  （`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`，Kconfig
  `default`行なし＝実質n寄りだが多くのターゲットでy相当。
  `hal/components/esp_phy/Kconfig`参照）は3チップとも意図的に
  未定義とし、`esp_shim_blobglue.c`（例：
  `asp3/target/esp32c3_espidf/wifi/esp_shim_blobglue.c:220-231`）で
  「本ビルドはCONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGEを定義しない
  ため実行経路には現れない＝毎回フル較正PHY_RF_CAL_FULL固定」と
  明記済み＝**既知・文書化済みのトレードオフ**（`docs/wifi-shim.md`にも
  相互参照あり）。config-audit.md本体には明記されていないが、
  ソース内で既に十分に監査・説明されているため「見落とし」には
  該当しない。

### 反証条件
- GATT_CLIENTについて：将来central/GATTクライアント機能を要求する
  アプリコードが追加された時点で`CONFIG_BT_NIMBLE_GATT_CLIENT`が
  未定義のままだと機能が静かに欠落する。追加時にこのマクロを
  明示することを申し送る（config-audit.md §8の要修正候補リストに
  追加することを推奨）。
- 本レビューはstock ESP-IDFビルド環境を持たない
  （`~/tools/esp-idf-v6.1`は本サンドボックスに不在）ため、
  Kconfig記載の`default y`と実際のsdkconfig生成値が完全一致するかは
  ソースの`Kconfig.in`記載のみに基づく（config-audit.mdと同じ制約）。
  stock IDFでのフルビルド比較ができれば、この監査の確度はさらに
  上がる。

---

## 要約（最終メッセージ用）

- **★E（実施92の帰属）**：v5.5.4既定を「維持してよい」という実務判断は
  支持できるが、「pc=0 crash＝double-reset由来（blob非依存）」という
  **説明の確からしさには複数の穴**がある。(1) cell1のboot1（v5.5.4，
  flash直後の初回RTS）が1/1 cleanだったのに対し、firstboot（同じ
  「flash直後の初回RTS」のはずの条件）はv5.5.4で8/8 crash——同一条件の
  はずが結果が逆転しており、実際のflash/RTSタイミングの違い（交絡）
  が未排除。(2) hal(v8)の「double-resetでもクラッシュする」根拠は
  cell2のboot1一回のみ（N=1）で、v5.5.4のN=8（firstboot）と非対称。
  (3) 実施91 §6（`docs/wifi-shim-c6.md:14166-14190`）で報告済みの
  同系統単発クラッシュを、本ラウンドのdouble-reset理論と照合せずに
  放置している——理論の反証機会を見送っている。実務判断は変えなくてよいが、
  「確定」と言い切る書きぶりは過剰。
- **★F（SM_SIGN_CNT・config監査）**：SM_SIGN_CNT=1化はソース上
  `#if`文脈2箇所のみで使用され副作用なし・4変種で値も文脈も整合＝**同意**。
  config監査の網羅性については、audit自身の抽出基準
  （117件×Kconfig default y×4変種未定義×消費.c実リンク）を
  そのまま再実行した結果、**`CONFIG_BT_NIMBLE_GATT_CLIENT`が
  基準に合致するのに候補表から漏れている**ことを検出した
  （config-audit.md自体の見落とし）。ただし実害を検証したところ、
  `ble_gattc.c`は内部で適切に`#if`ガードされ、かつアプリ層がGATT
  クライアントAPIを一切使用していないため**現状は機能的リスクなし**
  ——将来central機能を追加する際の申し送り事項として記録すべき。
  WiFi HE/PHY系・SM/HS系の他の候補は、明示的ポリシー決定
  （SAE/OWE系）またはビルド自己防御パターン（FTM/RRM/WNM/ENTERPRISE）
  または既存文書化済みトレードオフ（PHY較正データ永続化）に帰着し、
  新規のsilent-loss型ミスは検出されなかった。

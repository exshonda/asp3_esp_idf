# ★A レビュー：C3 BT v5.5.4 の SMP bond失敗の根因

対象: `asp3/target/esp32c3_espidf/esp_bt.cmake`（`ASP3_BT_IDF_V554`）、
`apps/bt_smoke/bt_smoke.c`・`apps/ble_host_smoke/ble_host_smoke.c`、
`hal/components/bt/controller/esp32c3/bt.c`・`hal/components/bt/include/esp32c3/include/esp_bt.h`、
`asp3/target/esp32c3_espidf/wifi/esp_shim.c`・`bt/bt_shim.c`、
`docs/blob-unify-v554.md` §12、`docs/bt-shim.md`（D-2d bond診断一式）、`docs/config-audit.md`。

**前提の確認（本env）**：`~/tools/esp-idf`（stock v5.5.4）は本環境に存在しない
（`ls /home/honda/tools/esp-idf` → No such file or directory）。よって
v5.5.4版`bt.c`/`esp_bt.h`の実体は本レビューでは直接読めない。以下は
(i) hal版の現物、(ii) 本リポジトリのコミット済みコメント・過去セッションの
実測記録（md5・diff行数などdocsに記載済みの一次情報）、(iii) 実機物証、
の3つから言える範囲の結論。v5.5.4現物との直接突合が必要な箇所は
「要stock比較・本環境で未確認」と明記する。

## まず押さえるべき非対称性（★重要・見落とされがちな前提）

C5/C6の「v5.5.4への統一」とC3のそれは**リスクの性質が違う**。
`docs/blob-unify-v554.md:256-271`の実測md5表で確認できる通り、C5/C6の
`libble_app.a`/`libphy.a`/`libbtbb.a`はv5.5.4とv6.1で**バイト完全一致**
（差はcoexistのみ）。つまりC5/C6の「v5.5.4化」は実質「同一バイナリの
再リンク」であり、esp_shim/os_adapter統合への新規負荷はゼロに近い。

対してC3は`docs/blob-unify-v554.md:389-401`と`esp_bt.cmake:36-38`が明記する
通り、`libbtdm_app.a`が **hal=`dfdadb9d…` と v5.5.4=`d9753a31…` でバイト不一致**
＝別バイナリへの本物の切替である。しかも`libphy.a`/`libbtbb.a`/`libcoexist.a`
についてはC3独自のhal側hash比較がdocsに存在せず（`blob-unify-v554.md`の
md5表はC5/C6のみ）、C3側は「v5.5.4のhashを記録した」だけで hal との
異同は未検証（`esp_bt.cmake:465-466`はv5.5.4側hashの列挙のみ）。

＝「C5/C6は同じバイナリをv5.5.4のビルドツリーで揃えただけで安全側に倒せた
再検証だが、C3は正真正銘の新しいバイナリを我々のos_adapterに繋ぐ最初の
実験」という非対称性がある。これは§3の実機物証（C5 cold full-BLE adv成功・
C3のみbond失敗）と矛盾しない——**C5の成功は「新しいバイナリでのbond耐性」を
何も検証していない**ため。実際、C5のbond成功実機テスト（`docs/bt-shim.md:2561-2583`、
2026-07-14、「別blob `015db3db`」）は、C5がv5.5.4へ切り替わった
`docs/blob-unify-v554.md`§8-11（日付2026-07-15）**より前**に行われており、
その時点のC5 BTはまだ旧世代blobだったと解される。つまり**PVCY修正込みの
NimBLEホストがv5.5.4世代のBT blob（d9753a31系）相手にend-to-endでbondした
実機確認は、C3・C5・C6のいずれについても本リポジトリ中に存在しない**。
C3の「v5.5.4でbond失敗」は孤立した謎ではなく、「新blob×我々の統合」という
未踏の組み合わせで最初に得られた結果、と位置づけるのが最も整合的。

## 問い1：`esp_bt_controller_config_t`（bt.c config）とv5.5.4 blobの期待値の齟齬

**結論：要追加検証（ただしリスクは限定的と判断）**

- `apps/bt_smoke/bt_smoke.c:90-94`・`apps/ble_host_smoke/ble_host_smoke.c:750-753`
  のコメントが明記する通り、両アプリとも **`BT_CONTROLLER_INIT_CONFIG_DEFAULT()`
  マクロを使わず**、`esp_bt_controller_config_t cfg` を `memset(&cfg, 0, sizeof(cfg))`
  してからフィールド名で1つずつ手書き代入している
  （`bt_smoke.c:95-160`／`ble_host_smoke.c:754-799`）。
- ABI面：`esp_bt.cmake:114-159`で `ASP3_BT_IDF_V554` に応じ
  `${BT_IDF}/components/bt/include/${BT_CHIP_SERIES}/include`（＝esp_bt.h）が
  切り替わる。この切替はアプリ／bt.c／blobの**全TUに一貫して効く**ため、
  「アプリはhal版ヘッダでコンパイルしたのにblobはv5.5.4」という取り違えは
  cmake構造上起きない。加えて`memset`＋フィールド名代入という書き方は、
  v5.5.4で新フィールドが追加されていても**それは0初期化される**（ゼロで
  安全に倒れるかは別問題だが、少なくとも未定義動作にはならない）。
  ＝候補(a)（config構造体の齟齬）はABIレイアウトの観点では**構造的に
  低リスク**と判断する。
- ただし2点、未確認のまま残る：
  1. v5.5.4で追加されたフィールドが**非ゼロを要求する**機能フラグである
     可能性（例：SC/暗号関連の新設フィールド）。hal版`esp_bt.h:410-506`の
     構造体定義を見ても、既に`ble_50_feat_supp`等かなり新しい世代のフィールドを
     含んでおり、v5.5.4との差分がどこまで残っているか本env単独では確定できない
     （**要stock比較・本環境で未確認**）。
  2. **`bt.c`自体が`esp_bt_controller_config_t`の値を経由せず、直接
     `CONFIG_BT_CTRL_*`マクロを`#if`で参照する箇所**（機能コンパイルの
     gate。値ではなくビルド時定数）。これは本リポジトリの
     `asp3/target/esp32c3_espidf/bt/stub/include/bt_nimble_config.h:127-169`
     （`CONFIG_BT_CTRL_MODE_EFF`等）が供給しているが、このヘッダ自体
     「hal/nuttx側sdkconfig.hのCONFIG_ESPRESSIF_BLEブロックの手書き写し」
     （同ファイル:1-20のコメント）であり、**実際のv5.5.4 Kconfigデフォルトから
     独立して手で保守されている**。`docs/config-audit.md:143-147`は
     C3のcontroller一式`CONFIG_BT_CTRL_*`/`CONFIG_BT_BLE_*`について
     「cmakeの-Dで別供給されている構造差でありヘッダの欠落ではない＝
     リスク対象外」と明記して**監査対象から明示的に除外**している。
     つまり「v5.5.4のbt.cが新規に`#if CONFIG_BT_CTRL_XXX`で参照する
     マクロを、本リポジトリのbt_nimble_config.hがまだ持っていない」
     というPVCY型（chip横断で共通に欠落するパターン）の齟齬は、
     **一度も体系的に監査されていない**——これがconfig-audit.mdの
     既知の穴であり、問い1で最も具体的に検証すべき残作業と判断する。

**反証条件**：v5.5.4 bt.cの実体を取得し(1)構造体定義に新規必須フィールドが
無いこと、(2)ソース中の`#if CONFIG_BT_CTRL_*`/`#ifdef CONFIG_BT_CTRL_*`の
全出現をgrepし、`bt_nimble_config.h`に対応定義があるか突合すれば確定する
（本envでは実施不可）。

## 問い2：esp_shimのHCI flow control（`host_num_completed_packets`等）と新blobのバッファ管理

**結論：同意（ただし理由はレビュー案とは逆＝この経路は既に反証済みで無罪）**

- リポジトリ全体を`host_num_completed_packets`でgrepしても**該当シンボルは
  存在しない**（Bluedroid由来の関数名で、NimBLE経路では使われない）。
  NimBLEのhost-side flow control本体は`MYNEWT_VAL(BLE_HS_FLOW_CTRL)`で、
  `docs/bt-shim.md:2605-2615`が実機調査で**プリプロセッサ実値を直接確認
  済み＝実効0（無効）**と記録している。C5 sdkconfig_stubにもNuttX C3
  （同blob）にも`CONFIG_BT_NIMBLE_HS_FLOW_CTRL`定義なし＝host は
  controller→host のflow controlを有効化していない。
- これはNimBLEホスト側（blob非依存）の設定であり、C3のv5.5.4切替でも
  変わらない——`esp_bt.cmake`のBT_IDF切替はcontroller/phy/blobのみで、
  NimBLEホスト（`bt/stub`含む）はhalのまま（`esp_bt.cmake:31`のコメント通り）。
  ＝「新blobでflow control credit計算が変わり詰まる」という機序はNimBLE側
  が最初からcreditのやりとりに参加していないため**成立しない**。
- ただし、**esp_shimレベルの「give取りこぼし」機序**（レビュー案の(2)に近いが
  中身が違う）は実在し、かつ本ラウンドで再検証されていない。
  `asp3/target/esp32c3_espidf/wifi/esp_shim.c:354-451`
  （`esp_shim_sem_give`のE_CTX保留機構）は、コメント（:354-362）が明記する
  通り「controller blobがosiの`_semphr_give_from_isr`をMIE==0（クリティカル
  セクション/ISR）文脈から叩く」タイミングを前提にした**hal blob
  （`dfdadb9d`）の実測ISR挙動に対して事後にチューニングされた救済策**。
  v5.5.4 blob（`d9753a31`）はバイナリが別物（問い0参照）である以上、
  「暗号後2個目のACLに対応するgiveがISR文脈から来るか・何回来るか・
  どのタイミングか」が**同一である保証はない**。既存のpend_ring/
  sem_flush機構が新blobの実際のgiveパターンを取りこぼす経路が残っていても
  不思議ではなく、これは**未検証**。

**反証条件**：`ESP32C3_BT_EVT_TRACE`（`esp_bt.cmake:542-552`）を
`ASP3_BT_IDF_V554=ON`ビルドで有効化し、`enc_chg`/`sm_enc_rx`/`put`/`sm_tx`/
`to_ll`のRTCマーカ（`docs/bt-shim.md:2648`の記法）を実機で採取。
hal blobで確認済みの「put=1・sm_tx=0」（PVCY起因）と**同一パターン**なら
既知のPVCY型ではない別要因（host側は既にPVCY=1でリンク確認済みのため
矛盾）と分かり、**put=0または`to_ll`不一致**が出れば「新blobのgive/ACL
配送そのものが我々のosi実装と噛み合っていない」ことが実証できる。

## 問い3：osi table（`_malloc_retention`以外の並び）のfield齟齬

**結論：同意はできない・要追加検証だがリスクは低いと推定**

- hal版`hal/components/bt/controller/esp32c3/bt.c:178-241`の`osi_funcs_t`は
  全フィールドが無条件（`#if`ガード無し）で並んでおり、`osi_funcs_ro`の
  初期化（:423行以降）も`._magic = ...`のような**指示付き初期化子**
  （フィールド名指定）。指示付き初期化子である以上、**ソースコード上の
  記述順序はABIに影響しない**——ABIを決めるのは構造体「定義」の
  フィールド宣言順であり、これは`bt.c`が丸ごとv5.5.4版に差し替わる
  （`esp_bt.cmake:172`）ため、bt.c（osi構造体定義）とblob（osi構造体を
  期待する側）は常に**同一世代のペア**になる。ゆえに「新フィールドを
  途中に挿入されて既存フィールドの位置がずれる」古典的ABI事故のリスクは
  構造的に低い。
- `esp_bt.cmake:39-46`（コメント）が記す通り、実際に確認されている差分は
  「OSI_VERSION 0x0001000A→0x0001000B」と「末尾に近い新規`_malloc_retention`
  フィールド追加」の2点のみで、bt.c全体の差分は91行と報告されている
  （`docs/blob-unify-v554.md`該当節。**この91行diffの実測自体は過去セッションの
  ~/tools/esp-idf参照時に行われたもので、本環境では再現・検証不可**）。
  91行という小さな差分規模から見て、osi構造体そのものへの他の変更は
  無かった可能性が高いが、断定はできない。
- `_malloc_retention`の実害は低いと判断する：blobから見れば単なる
  `void *(*)(size_t)`関数ポインタで、実体は`heap_caps_malloc(size, MALLOC_CAP_RETENTION)`
  へ委譲されるだけ（`esp_bt.cmake:74-79`のコメント）。本リポジトリの
  `esp_shim_libc.c:612-620`の`heap_caps_malloc`は**capsを無視して
  esp_shim_mallocへ委譲する**実装であり、リテンション専用メモリ領域としての
  特別なセマンティクスを持たない。sleep_mode=0（modem sleep無効、
  `bt_smoke.c`/`ble_host_smoke.c`のcfg.sleep_mode=0）である以上、
  このwrapperが実行時に呼ばれる経路（light/modem sleep関連）はそもそも
  非活性の可能性が高い。
- **osi以外で見つけた、より具体的な"field/count齟齬"のリスク箇所**：
  `asp3/target/esp32c3_espidf/bt/bt_shim.c:398-499`の`esp_intr_alloc`実装。
  同ファイルのコメント（:398-412）が明記する通り、この実装は
  「bt.cはesp_intr_allocを**厳密に2回**（source8=RWBLE→source5=BT_BB）
  呼ぶ」という**S3実機で確定した経験則**を前提に`BT_INTR_MAX_SLOT=2`を
  ハードコードし、3回目以降の呼び出しは「想定外＝最終スロットへ上書き」
  （:471-478）としてハンドラを黙って誤配線する。v5.5.4 bt.cが割込み登録の
  回数・順序をhal版と変えていないかは**未確認**（91行diffの内訳に
  esp_intr_alloc呼び出し追加の言及はなく、可能性としては低いが、
  file:line裏取りには v5.5.4 bt.c 現物が要る）。もし3回目の登録が
  実際に発生していれば、2個目以降の暗号化ACL処理に関わる割込みの
  一部が誤ハンドラへルーティングされ、"1個目は通るが2個目以降が
  詰まる"という観測済みシグネチャと整合し得る。

**反証条件**：v5.5.4 bt.cで`esp_intr_alloc`（または`intr_alloc`委譲）の
呼び出し箇所数をgrepし2箇所（RWBLE/BT_BB相当）であることを確認、または
実機で`BT_INTR_TRACE_REG`（`bt_shim.c:437`、アドレス`0x60008054`）を
`ASP3_BT_IDF_V554=ON`ビルドの起動直後にesptoolで読み、
`0xA1020805`相当（2回・source8→5）と一致するかを見れば即座に切り分けられる
（ビルド不要な追加計装で、既存機構の読み出しのみ）。

## 問い4：暗号確立後のSMP鍵配布フェーズでACL/HCIが詰まる最有力箇所

**結論：単一箇所には確定できない（rigor上、因果は未確定のまま）。ただし
優先順位付きの検証パスは提示できる。**

`docs/bt-shim.md`のD-2d調査履歴が示す通り、hal blobでの本物の bond 不成立は
段階的に多くの仮説（トリガ経路・HCIフロー制御・pend_ring欠落・NPLタイマ・
RXソース差分・controller/LL説）を**反証**した末に、最終的に
**host側の`#if MYNEWT_VAL(BLE_HS_PVCY)`によるIdentity鍵送出のコンパイル
アウト**（`docs/bt-shim.md:2646-2677`、`ble_sm.c:2365-2426`相当）に
確定した。v5.5.4ビルドはこのPVCY修正を**リンク段階で確認済み**
（`docs/blob-unify-v554.md:502-506`：`ble_hs_pvcy_set_our_irk`等が
link済み）。つまり**既知のPVCY型バグの再発ではない**——症状（30秒
AuthenticationTimeout）が同型に見えるのは、SM実装のタイムアウトが
「鍵配布が完了しない」という共通の外部症状に収束するため（bt-shim.mdの
教訓：相関を因果と早合点しない）。

優先順位（テスト容易性×説明力で採点）：
1. **既存計装の転用**（最優先・追加実装ほぼ不要）：
   `ESP32C3_BT_EVT_TRACE`/`ESP32C3_BT_ACL_TRACE`/`ESP32C3_BT_APIERR_TRACE`
   （`esp_bt.cmake:509-552`）は全てNimBLEホスト側（hal のまま）のシンボルを
   `--wrap`しており、`ASP3_BT_IDF_V554=ON`でも**そのままリンク可能**。
   hal blobで確立済みの基準パターン（enc_chg=1/sm_enc_rx=1/put=1/sm_tx>0
   のはずが実際は0、というPVCY修正前の記録）と、v5.5.4ビルドでの実測を
   突合すれば「host側は今回も鍵配布を試みているのに blob 側の配送で
   止まっているか」を機械的に判別できる。
2. **`BT_INTR_TRACE_REG`（`bt_shim.c:437`）読み出し**：上記問い3のとおり、
   割込み登録回数・sourceを見るだけで済む安価な反証実験。
3. **esp_shimのgive/pend_ring会計**（`esp_shim.c:354-451`、
   `shim_sem_ectx_total`・`shim_sem_pend_total`）を`ASP3_BT_IDF_V554=ON`で
   採取し、hal blobでの既知の収支（"debt=0に復帰"、`docs/bt-shim.md:2626`）
   と比較。
4. blob自体のLL/CCM差（bt-shim.mdの「iOS再接続MIC failureと同族」説、
   `docs/bt-shim.md:2627-2636`）——これは silicon/blob 内部の話でコード
   読解では確定できず、実機計装に頼るしかない。

**最小の修正候補（file:line）**：現時点でコードから確定的な bug と
言えるものは無い（＝PVCY型のような「file:lineで直せる一発修正」は
見つからなかった）。強いて挙げるなら:
- `asp3/target/esp32c3_espidf/bt/bt_shim.c:471-478`
  （`esp_intr_alloc`の3回目呼び出しに対する黙示的スロット上書き）を
  「想定外」ログ＋アボートまたはRTCマーカ確定出力に変更し、
  v5.5.4ビルドで**実際に3回以上呼ばれていないか**を可視化する
  （修正というより診断強化。ただし実装コストは小さく非回帰）。
- `docs/config-audit.md:143-147`で明示的に対象外とされた
  C3 controller一式`CONFIG_BT_CTRL_*`（`bt/stub/include/bt_nimble_config.h:127-169`）
  について、v5.5.4 bt.c 現物とのgrep突合を次回のconfig監査ラウンドに追加する。

## 順位付き要約

1. **最重要の見落とし**：C5/C6の「v5.5.4統一成功」はC3の失敗と本質的に
   非対称（blobがバイト同一 vs バイト不一致）。C3のみが「新バイナリ×
   我々の統合」という初めての組み合わせであり、かつPVCY修正込みの
   NimBLEホストが新世代BT blobとend-to-endでbondした実機確認はこの
   リポジトリのどこにも存在しない。“謎の非互換”ではなく“単に一度も
   検証されていない組み合わせ”という説明が最も整合的（要追加検証）。
2. **問い2（HCI flow control／host_num_completed_packets）は同意できない
   前提を含む**：該当機構はNimBLEでは無効化されており（実測済み）、
   その意味では無罪。だが類似の脆弱ポイントとして esp_shim の
   E_CTX give救済（pend_ring/sem_flush）が hal blob の実測タイミングに
   チューニングされている点は未検証のまま残る（要追加検証）。
3. **問い1・3（config/osi構造体のABI）は構造的リスクは低いと判断**
   （designated initializer・memset・BT_IDF一貫切替のため）が、
   config-audit.mdが明示的に対象外とした「bt.cが直接参照する
   CONFIG_BT_CTRL_*マクロの完全性」は未監査であり、ここに
   PVCY型（chip横断で共通に欠落する)の残存バグが潜んでいる可能性は
   否定できない（要追加検証）。
4. **問い4（決定打）**：既存の診断計装（EVT_TRACE/ACL_TRACE/APIERR_TRACE/
   BT_INTR_TRACE_REG）はすべてASP3_BT_IDF_V554=ONでもそのまま使えるため、
   追加実装なしで「host側は正常に鍵配布を試みているのにblobが配送しない」
   のか「host側の何か（esp_intr_alloc回数・give会計）が新blobで壊れている」
   のかを1〜2回の実機計装ラウンドで切り分け可能。次の一手はコード修正では
   なく、この既存計装をv5.5.4ビルドに対して回すこと。

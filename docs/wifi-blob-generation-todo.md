# WiFi blobをIDF v6.1へ揃えるべきか——机上レビュー＋ToDo

本ラウンドの成果物。**読み取り専用調査＋本ファイルのみ新規作成**（コード変更・
ビルド・実機なし）。ブランチ`claude/c6-wifi-c5-dev-5vc6x9`（2cf9022時点）。

## 発端（ユーザーの問い）

「BLEをIDF v6.1にしたのだから，WiFiもv6.1にした方が良いのではないか。
良いならToDoとして記録せよ」。本ファイルはこの是非を，コード（cmake／
リンクライブラリパス／submodule実体）とdocs（実施記録）の一次情報で
裏取りしながら評価し，結論としてのToDoを末尾に記録する。

**先に結論を要約すると**：単独WiFiの現状維持を推奨する（下記D）。
根拠は，(1)WiFi(hal/v8)はC5/C6とも実機実証済みで健全に動いている，
(2)v6.1への移行はCLAUDE.mdの禁則に触れないが，C5が実施48-52で
まさに「v6.1のローカルパス依存を撤去してポータビリティを取り戻す」
ために苦労してhal(v8)へ寄せた経緯を逆走することになる，(3)WiFi単独では
v6.1化の実益（§12非対称の解消）が発生しない——非対称はBT側のhal libphy
固有の問題であり，WiFiを動かしても消えない，という3点。v6.1化に価値が
出るのはWiFi+BLE coexistに実際に着手する時（1つのlibphy/libcoexへ揃える
「必要」が生じた時）であり，それまでは判断を保留してよい。

## 0. blob世代対応表（現状，2026-07-15時点のツリー実体で確認）

| チップ | 機能 | 採用blob世代（既定） | 動作状態（実機） | 根拠（docs／コード） |
|---|---|---|---|---|
| C3 | WiFi | hal（esp-hal-3rdparty，旧世代） | 既存資産，動作実績あり（本レビューでは深追いせず） | `docs/ble-c5c6-plan.md` 1.2節（C3欄） |
| C3 | BLE | hal 旧世代`libbtdm_app.a`＋`btbb`（C6/C5の「BLE embedded controller V1」とは別系統．v6.1化の選択肢自体が存在しない） | D-2c/D-2d完全達成——4特性PASS，`0xABF4`暗号readでbond LTK実効を実証 | `docs/bt-shim.md` L2054-2280他，commit `801939a` |
| C5 | WiFi | **hal(v8) 単一実装**（実施52でIDF v6.1版=v9を完全削除） | scan（実施48）／2.4GHz connect+DHCP（実施50）／**5GHz connect+DHCP**（実施51）まで実機実証済み | `docs/c5-bringup.md` 実施48・50・51・52（L10339-10560）／`asp3/target/esp32c5_espidf/esp_wifi_v8.cmake` |
| C5 | BLE | **IDF v6.1**（`esp_bt.cmake`が`~/tools/esp-idf-v6.1`から bt.c/ble.c/libble_app.a/esp_phy/esp_coexを採用．halの`lib_esp32c5`へ戻す件は「wifi統一が成功した場合の第2段」として**未着手のまま残っている**） | D-1〜D-2d到達．PVCY修正（`CONFIG_BT_NIMBLE_HS_PVCY=1`）後にbond成功実証済み | `docs/ble-c5c6-plan.md` 1.2節／`docs/c5-hal-v8-unification-memo.md`末尾／`docs/bt-shim.md` L2544-2688（特にL2688「C5はbond成功済」）／`asp3/target/esp32c5_espidf/esp_bt.cmake` L1-46 |
| C6 | WiFi | **hal(v8)**（`esp_wifi.cmake`既定．`ASP3_WIFI_BLOB_IDF`は実行されなかった/結果未記録の診断フックのみで実運用には未使用【未確認＝実施記録に結果なし】） | scan＋connect＋DHCP＋ping＋TCP/UDPまで実機で安定動作（実施90/91，APM恒久化＋modem ICG再確立後） | `docs/wifi-shim-c6.md` 実施90/91（末尾） |
| C6 | BLE（既定＝`ESP32C6_BT_IDF61=OFF`） | hal(v8) `libble_app.a` | D-2b到達済みだが**cold起動でregister_chipv7_phyがRFシンセ非ロック＝非収束**（§10-12） | `docs/ble-c5c6-plan.md` §10-12（L1130-1229） |
| C6 | BLE（トグルON＝`ESP32C6_BT_IDF61=ON`） | **IDF v6.1 matched-set**（bt/phy/coex/libble_app.aを`~/tools/esp-idf-v6.1`から一式転写） | D-1（§13）→D-2a/D-2b（§14）→D-2c/D-2dビルド＋device-side非回帰（§15）まで実機到達．**board C最終flashはこちら**（`build/c6bt_idf61_sm`） | `docs/ble-c5c6-plan.md` §13-15（L1232-1481） |

**留意点（表の読み方）**：
- C6のBLEは「hal既定・v6.1はオプトイン」という**cmakeオプションの既定値**と，
  「実際に実機で収束し稼働中のビルド世代」が**逆転している**——オプション名の
  既定はhalだが，動いているのはv6.1版という状態。これは「WiFiがv6.1になれば
  自然に揃う」という単純な話ではなく，**BLE側で既にv6.1が"動く方"として
  定着している**ことを意味する（§2で後述）。
- C5は「WiFi=hal，BLE=v6.1」という**現時点で既に世代が割れた状態**にある
  （BT+WiFi同時ON非対応のため実害は出ていないが，単一バイナリに揃っている
  わけではないことは把握しておく必要がある）。

## 1. §12の事実（BT vs WiFiの非対称）——要約

`docs/ble-c5c6-plan.md` §12（L1130-1229）がJTAG HWブレークポイントで実測した
内容：

- C6の同一hal `libphy.a`（`register_chipv7_phy`）に対し，BTビルド
  （`build/c6bt_fix`）とWiFiビルド（`build/c6_wifiscan_works`）から呼び出した
  ときの**入力3要素（`a0`=init_data 128B・`a1`=cal_data・`a2`=mode）はバイト単位で
  完全一致**，呼出し元（`ra`）も同一ソース関数（`esp_phy_load_cal_and_init`），
  エントリ時のMMIO 11レジスタもLP_TIMERのビット1つ（良性差と判定）を除き一致。
- にもかかわらず，**WiFiは`register_chipv7_phy`実行中にsynth-lockビット
  （`0x600a00cc` bit8）が立って収束し，BTは立たずに無限スピンする**。
- 結論：発散点は`register_chipv7_phy`**関数内部**（hal libphyのBT依存
  サブパス）にある。内部機構（`phy_get_modem_flag`によるBT/WiFi分岐という
  仮説）は**未計装・未実測＝【未確認】**（同docsが明記，rigor基準に従い
  因果として主張しない）。
- §13で，bt/phy/coex/`libble_app.a`をIDF v6.1のmatched setへ丸ごと
  swapしたところ収束した（C6 D-1〜D-2d到達）。ただし同docsが明記する通り，
  **これは「hal libphyが全体としてBTと非互換」という意味ではない**——WiFiは
  同じhal libphyで収束しているため，非互換はBT enable経路（`libble_app.a`の
  enable前セットアップ側）にローカライズされている可能性が高いとしつつも，
  「v6.1側の何が効いたか」の内部機構自体は未解明のまま。

**重要な非対称性の性質**：C6のAPM/クロック系の恒久修正（実施87/88/90/91）は
BT/WiFi非依存の無条件修正であり，§10-12のhal BT検証は**それらの修正が
既に適用された状態**で行われている。つまりC6のhal BT非収束は，C5のWiFi
（実施09時点）のように「クロック鎖・APM未修正という別要因に交絡していた」
という構図では説明できない——**hal libphyのBT依存サブパスに真に局在した
問題**である可能性が高い（C5 WiFiの旧v8非互換判定とは性質が異なる，
`docs/ble-c5c6-plan.md` §12末尾・§13.6の記述と整合）。

## 2. 問いA：libphyは共有か——リンクライブラリパスの実測対応

`grep`でcmakeファイルの`-L`/リンクライブラリ実体を突合せた結果：

| ファイル | 対象 | libphy/libcoex/libble_appの参照元 |
|---|---|---|
| `asp3/target/esp32c5_espidf/esp_wifi_v8.cmake` L285-287 | C5 WiFi(既定) | `${ESP_HAL_DIR}/components/{esp_wifi,esp_phy,esp_coex}/lib/esp32c5`（hal submodule） |
| `asp3/target/esp32c5_espidf/esp_bt.cmake` L297-299 | C5 BLE(既定・唯一) | `${IDF}/components/{bt/controller/lib_esp32c5,esp_phy/lib,esp_coex/lib}/esp32c5`（`IDF=/home/honda/tools/esp-idf-v6.1`） |
| `asp3/target/esp32c6_espidf/esp_wifi.cmake` L307-319 | C6 WiFi(既定) | `else()`枝＝`${ESP_HAL_DIR}/components/{esp_wifi,esp_phy,esp_coex}/lib/esp32c6`（hal submodule）。`if(DEFINED ASP3_WIFI_BLOB_IDF)`枝は診断用の未実行フック【未確認】 |
| `asp3/target/esp32c6_espidf/esp_bt.cmake` L336-338 | C6 BLE(`ESP32C6_BT_IDF61=OFF`) | `${ESP_HAL_DIR}/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6`＋`esp_phy,esp_coex/lib/esp32c6`（hal submodule） |
| `asp3/target/esp32c6_espidf/esp_bt_idf61.cmake` L279-281 | C6 BLE(`ESP32C6_BT_IDF61=ON`，**実機稼働中の版**) | `${IDF}/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6`＋`esp_phy,esp_coex/lib/esp32c6`（`IDF=/home/honda/tools/esp-idf-v6.1`） |

**結論（実測）**：

- **WiFi（C5/C6とも）とBLE（既定＝hal版）は，同一hal submodule内の
  同一`libphy.a`／`libcoex(esp_coex).a`パスを参照している**——ファイル実体を
  md5sumで確認（下記）。
- **BLE（トグルON＝v6.1版）は，物理的に別ファイルの`libphy.a`／
  `libble_app.a`を参照している**（`/home/honda/tools/esp-idf-v6.1`配下）。
  本環境（クラウド/CIサンドボックス）にはこのパスは**存在しない**
  （`ls /home/honda/tools/esp-idf-v6.1` → No such file or directory，
  本レビューで実測確認）。**つまりC5のBLEビルドおよびC6のv6.1版BLE
  ビルドは，現在この環境では再現ビルドできない**——ユーザーのローカル
  マシン（`honda@i.nagoya-u.ac.jp`）でのみ成立するビルドであることが，
  今回の調査で直接裏付けられた。

hal側とv6.1側の`libphy.a`が同一物かの直接バイト比較は，v6.1側の実体が
本環境に無いため実施不可（【未確認】）。ただし`docs/ble-c5c6-plan.md`
§13.4のnm実測（`register_chipv7_phy`のアドレスがhal版`0x42032590`から
v6.1版`0x42032abe`へ移動）は，「同名シンボルだが物理的に別バイナリが
リンクされた」ことの直接証拠であり，**hal版とv6.1版は同名APIを持つ
別blobである**ことは実測レベルで確定している。

md5実測（本環境のhal submodule内。C6/C5のWiFiとBLE既定版が同一ファイルを
指すことの確認）：

```
libphy.a (esp32c6, hal)      cb429107787d88023983668c9b161b56
libble_app.a (esp32c6, hal)  75db98e5139162fa60583becb38ea0a1
libphy.a (esp32c5, hal)      51166fb6f054a9e57211dfcfc1af62e9
libble_app.a (esp32c5, hal)  015db3db5a44be084b44b3579c900a5b
```

**A への回答**：WiFiとBLE（既定hal構成）は同一`libphy.a`をリンクする
（構造上・実測上ともに共有）。v6.1版BLEは別blob。C5のBLEは常時v6.1，
C6のBLEはトグルでhal/v6.1どちらも選べるが実機で収束するのはv6.1側のみ。

## 3. 問いB：WiFi+BLE coexistenceの前提

- **現状は排他**：`asp3/target/esp32c5_espidf/target.cmake` L178-179，
  `asp3/target/esp32c6_espidf/target.cmake` L171-172がそれぞれ
  `ESP32C{5,6}_BT AND ESP32C{5,6}_WIFI`で`FATAL_ERROR`（RAM予算，
  C3の前例踏襲）。両チップとも実測RAM/FLASHはBT単体で72-77%程度
  （`docs/bt-shim.md` L2557「C5 BT+NimBLE: FLASH 9.21%/RAM 77.44%」，
  `docs/ble-c5c6-plan.md` §15.1「C6 BT+NimBLE+SM: FLASH 9.73%/RAM 72.35%」）
  であり，WiFi単体も同程度（C5 wifi_scan RAM 76.05%，`docs/bt-shim.md`
  L2557）を要するため，単純合算では確実にオーバーする——排他は現時点で
  妥当な判断と評価できる。
- **技術的な意味での「揃える必要」**：WiFi/BLE同時ONを実装する場合，
  1つの最終ELFに両方の機能をリンクすることになる。`esp_bt.cmake`の
  コメント（C5版，L19-22）は「hal世代のBT blobとv6.1世代のPHY blobを
  手で混ぜる『ハイブリッド』構成は，Espressifが実際には検証していない
  blob-ABI境界を新規に作ることになるため採らない」と明記しており，
  これは**単一チップ内でBT用/WiFi用のPHYを世代違いで同時リンクする
  ことへの明確な忌避**として既に文書化されている。coexistでは
  register_chipv7_phy／coex_init等のPHY・coex制御が単一の状態機械
  （較正データ・coexスケジューラ）を共有する必要があるため，**世代の
  異なる2つのlibphy/libcoexを同一バイナリに混在させる設計は，リンクは
  通っても意味論（calibration state・coex arbitration）が壊れるリスクが
  高い**——「揃える必要がある」というユーザーの直感は，coexistという
  条件下では技術的に妥当と評価する。
- **「WiFiもv6.1側のlibphyに引きずられるか」**：現状の実測事実だけからは
  **半分正しい**。C6のBLEはv6.1でのみ収束することが実機実証済み
  （§13-15）。一方**C6のWiFiはhal(v8)で既に実機実証済み**（実施90/91，
  scan+connect+DHCP+ping+TCP/UDP）であり，v6.1版WiFiがC6で収束するかは
  **本ラウンドでは検証していない＝【未確認】**（`ASP3_WIFI_BLOB_IDF`
  診断フックはコード上存在するが実行結果の記録が見当たらない）。
  したがって「coexistのために揃えるなら，収束実績のある側＝BLEはv6.1・
  WiFiはhal，のどちらへ揃えるべきか」は，**hal版WiFi+v6.1版BLEを混ぜず
  一方に統一する，という要請自体は成り立つが，統一先がv6.1と決まって
  いるわけではない**（hal側へBLEを寄せる方向＝C5の「第2段」相当も
  理論上ある，4節で後述）。「引きずられる」は，現状の実機実証の
  非対称（BLE=v6.1のみ収束確認済み，WiFi=hal・v6.1双方とも収束するか
  不明）を踏まえると，**当面はv6.1へ寄せる方が実証コストが低い
  （BLE側で既に収束実績があるため）**という限定的な意味では妥当。

## 4. 問いC：単独WiFiにv6.1移行するメリット/デメリット

### メリット

1. **blob世代の統一**（BLEと同じmatched set）——C6限定では意味がある
   （C6のBLEはv6.1でのみ収束実績があるため，WiFiも同じmatched setに
   揃えれば「両方ともv6.1で動作実績あり」という状態は作れる）。ただし
   これは**WiFi単体の動作を改善するものではない**——WiFiは既にhalで
   動いている（実施90/91）。統一のメリットは専ら「BLEとの将来的な
   coexist時の一貫性」であり，単独WiFiの価値ではない。
2. **§12非対称の"解消"にはならない**——これは重要な誤解ポイントとして
   明記する。§12の非対称はBTのhal libphy内部の問題であり，**WiFiを
   v6.1にしてもBT側がhalのままなら非対称は残る**（そもそも現状C6の
   BTはトグルでv6.1が使えるため，非対称は既に「回避可能」な状態にある。
   WiFiのv6.1化は非対称の「解消」ではなく，両者を同じv6.1へ「追随」
   させるだけ）。
3. **将来coexistへの布石**——3節で述べた通りcoexist着手時にはいずれ
   一方へ揃える必要が生じる可能性が高く，その時点でv6.1へ揃えるので
   あれば，先にWiFiだけでもv6.1移植を済ませておく，という順序上の
   意味はある。

### デメリット

1. **C5がまさに撤去した依存の再導入**——`docs/c5-bringup.md`実施52は
   「`esp_wifi.cmake:85`の`/home/honda/tools/esp-idf-v6.1`ハードコード
   依存でポータビリティ（＝統一の主目的）を損ねる」ことを理由に
   v9（IDF v6.1版WiFi）を**完全削除**した。本レビューでも
   `/home/honda/tools/esp-idf-v6.1`が本環境（クラウド/CIサンドボックス）
   に**実在しない**ことを直接確認しており，「WiFiをv6.1へ戻す」ことは
   実施52が解決した「クラウド/CIでビルドできない」問題をWiFi側にも
   再導入することを意味する。CLAUDE.mdはCI/QEMU/実機3系統でのビルド
   確認を鉄則としており，ローカルパス依存はこの鉄則と相性が悪い。
2. **動作実績のあるhal(v8) WiFiを触るリスク**——C5は5GHz実証済み
   （実施51），C6はconnect+DHCP+ping+TCP/UDPまで安定動作（実施91）。
   両方とも「動いているものを壊すリスク」対「まだ得られていない利益」
   のトレードオフになり，coexistという具体的な必要が無い現時点では
   割に合わない。
3. **再検証コスト**——5GHz・APM・クロック系はいずれもhal(v8)向けに
   個別にチューニングされた実機修正の積み重ね（実施42/43のAPM，
   実施32-34のクロック鎖，実施90/91のICG）であり，v6.1版WiFiで
   これらが同様に必要かは未検証。C5のBT（既にv6.1）はC3型の別
   プログラミングモデル（直接FreeRTOS API）であり，WiFi側もv6.1へ
   移行するなら`esp_wifi_adapter.c`のosi登録テーブル配線を含め
   大幅な作り直しが必要になる可能性が高い（BTのhal→v6.1移行が
   「bt.c/ble.cのモデルごと変わる」大改造だったのと同型の負荷が
   WiFi側でも見込まれる，`docs/ble-c5c6-plan.md` 1.2節）。

### 中間案（v6.1のpinned submodule化）

`docs/c5-hal-v8-unification-memo.md`末尾が既に「失敗時のフォールバック」
として記録している案——**v6.1依存を「ローカルパス」から「pinned
submodule化（esp-idf v6.1-beta1等のタグ）」へ変え，依存を明示化する**。
これは技術的には現実的（`.gitmodules`に`hal`と同様の形式で
`https://github.com/espressif/esp-idf.git`をタグ固定追加するだけ）。
ただし：

- この案は**WiFiのv6.1化とは独立に検討できる**——現状のBLE（C5常時，
  C6トグルON時）が既にこのローカルパス問題を抱えているため，
  「BLEのv6.1依存をpinned submodule化する」こと自体が，WiFiの扱いを
  決める前に単独で価値のあるToDoである（本ファイルのToDoに含める）。
- WiFiもv6.1へ揃えるかどうかは，pinned submodule化の後でも独立に
  判断できる（submodule化はポータビリティ問題を解決するが，
  「hal(v8)で動いているWiFiをわざわざ触るか」という3節の判断とは別軸）。

## 5. 問いD：結論と推奨

**(1) 単独WiFiは現状維持でよい**——を推奨する。

根拠（1行）：WiFi(hal/v8)はC5(5GHz含む)・C6(scan〜TCP/UDP)とも実機実証済みで
健全に動作しており，§12の非対称はBT側のhal libphy内部問題であって
WiFiをv6.1化しても解消しない一方，v6.1化はC5が実施52でまさに撤去した
ローカルパス依存（本環境に実在しないことを実測確認）をWiFi側にも
再導入し，動作実績のある資産に不要なリスクを負わせるだけになるため。

**(3)「coexistに着手する時」に判断，をToDoとして保留**——を採用する
（(2)即v6.1移行は推奨しない）。

## 6. ToDo

### ToDo-1（保留・トリガ待ち）：WiFiのblob世代をBLEに揃えるか再検討する

- **着手条件／トリガ**：ASP3がESP32-C5/C6でWiFi+BLE **coexistに実際に
  着手する**時。現状は`target.cmake`のFATAL_ERRORでWiFi/BLE同時ONが
  そもそも不可能なため，この制約を外す（RAM予算の実機再検証を含む）
  作業に着手するタイミングが自然なトリガになる。
- **着手時に最初にやること**：(a) coexist対象チップでhal版WiFi＋hal版
  BLEの組合せが既に収束するか再検証する（C6はhal BLEがcold非収束のため
  現状NG，C5はBLEがhal未検証のため要確認）。(b) (a)がNGならv6.1版WiFiが
  収束するか実機で確認してから，v6.1へ揃える方針を確定する。「WiFiを
  v6.1にする」は前提条件（coexist着手）が満たされてから，かつ実機検証
  してから決める——本ラウンドの推奨に基づき，前倒しでの着手はしない。
- **代替方向も棄却せず並記**：C5については「BLEをhalの`lib_esp32c5`へ
  戻せるか」という**逆方向の統一**が`docs/c5-hal-v8-unification-memo.md`
  末尾で既に「wifi統一が成功した場合の第2段」として予告されたまま
  **未着手**になっている。coexist着手時はこちら（BLE→hal統一）も
  同列の選択肢として検討すること（WiFi→v6.1と比べてローカルパス依存を
  増やさずに済む利点がある）。

### ToDo-2（独立・トリガ不要）：BLEのv6.1ローカルパス依存をpinned submoduleへ

- 現状C5のBLE（常時）とC6のBLE（`ESP32C6_BT_IDF61=ON`，実機で実際に
  稼働中のビルド）は`/home/honda/tools/esp-idf-v6.1`というローカルパスに
  依存しており，**本レビュー時点でクラウド/CI環境に同パスが存在しない
  ことを実測確認した**——つまりC5のBLEビルド，C6のv6.1版BLEビルドは
  現状このクラウド環境では再現できない。
- `docs/c5-hal-v8-unification-memo.md`が既に提案している
  「pinned submodule化（esp-idf v6.1-beta1等のタグ）」を実施し，
  `.gitmodules`に`hal`と同型のエントリを追加してローカルパス依存を
  解消することを推奨する。これはWiFiの世代選択（ToDo-1）とは独立に
  進められ，CLAUDE.mdの「CI/QEMU/実機3系統で検証」という鉄則との
  整合性も改善する。
- 優先度はToDo-1より高いと考える——現に動いている資産（C5 BLE D-2d
  bond・C6 BLE D-2c/D-2dビルド）が本レビューで発覚した通りこの
  クラウド環境では追試・回帰確認すらできない状態にあるため。

## 7. 参照した一次資料（本ラウンドで実際に確認したファイル）

- `docs/ble-c5c6-plan.md`（§1.2・§12・§13・§14・§15，全読）
- `docs/c5-hal-v8-unification-memo.md`（全読）
- `docs/c5-bringup.md`（実施50・51・52，該当節全読）
- `docs/wifi-shim-c6.md`（実施90/91該当節，末尾）
- `docs/bt-shim.md`（L2054-2280「D-2c/D-2d」節，L2508-2680「C5 BLEビルド回帰
  修正」〜「PVCY真因確定」節）
- `asp3/target/esp32c5_espidf/esp_wifi_v8.cmake`（`-L`/リンクライブラリ節）
- `asp3/target/esp32c5_espidf/esp_bt.cmake`（冒頭コメント・`-L`節）
- `asp3/target/esp32c5_espidf/target.cmake`（`ESP32C5_BT`/`ESP32C5_WIFI`排他節）
- `asp3/target/esp32c6_espidf/esp_wifi.cmake`（`-L`節・`ASP3_WIFI_BLOB_IDF`診断枝）
- `asp3/target/esp32c6_espidf/esp_bt.cmake`（トグル定義・`-L`節）
- `asp3/target/esp32c6_espidf/esp_bt_idf61.cmake`（冒頭コメント・`-L`節）
- `asp3/target/esp32c6_espidf/target.cmake`（`ESP32C6_BT`/`ESP32C6_WIFI`排他節）
- `.gitmodules`（submodule一覧）
- `hal/components/esp_phy/lib/{esp32c6,esp32c5}/*.a`・
  `hal/components/bt/controller/lib_{esp32c6,esp32c5}/**/*.a`（md5sum実測）
- `/home/honda/tools/esp-idf-v6.1`の非存在を`ls`で実測確認（本環境）
- git log（`git log --oneline -5`，コミット`2cf9022`まで確認）

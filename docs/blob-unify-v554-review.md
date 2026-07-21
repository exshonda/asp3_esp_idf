# コードレビュー結果：WiFi/BT blob v5.5.4統一ほか（`tmp/review-request.md`への回答）

依頼＝`tmp/review-request.md`（★A〜★F）。レビュアはリポジトリ全体をread-onlyで
精読（サブエージェント4本を次元別に並列実行し本ファイルで統合）。ブランチ
`claude/blob-unify-v5.5.4`（`bcffac4`）＋submodule `asp3_core`＝`feat/esp32c6`
（`9904a44`）。各項目の詳細生レポート（`tmp/review_{A_c3bond,C_clic,BD_blobunify,
EF_attr_config}.md`）は本ファイルへ統合後に削除済み（履歴には残存）。
**stock ESP-IDF（`~/tools/esp-idf`）は本環境に不在**のため、
stock現物との直接突合が要る箇所は「要stock比較・本環境で未確認」と明示した。

## 総括（優先度順）

| # | 項目 | 判定 | 一言 |
|---|---|---|---|
| ★B1 | C6 BT既定flip | **要修正（前提の誤り）** | 素の`-DESP32C6_BT=ON`はv5.5.4でもv6.1でもなく**hal(v8)**に着地。既定flipはC6では効いていない |
| ★A | C3 bond失敗の根因 | **要追加検証（局在は妥当・因果未確定）** | C3の失敗は孤立した謎でなく「PVCY修正込みホスト×新世代blob bond」が3チップとも実機未確認＝C3が初の組合せ |
| ★D5 | no-op stubの範囲 | **要修正（未検証の回帰面）** | `esp_wifi_sta_get_ie`はscan外（STA接続時に無条件呼出し）＝RSN IE検証が恒常無効化。実機はopen scanのみ |
| ★E | 实施92帰属 | **要追加検証** | 「double-reset由来・blob非依存」は同一条件の結果逆転・hal側N=1・先行事例未照合でrigor未充足 |
| ★B2 | 可逆性 | **一部穴（C6-BT）** | WiFi/C3-BT/C5-BTは可逆。C6-BTは二段オプションで`ASP3_BT_IDF_V554=OFF`単独ではhalに戻らずv6.1着地 |
| ★C | CLIC出口正規化 | **同意（設計正しい）** | mpilマスク・MPP/MPIE・非CLIC回帰すべて安全。留意は検証範囲（実機C5 1台・QEMU非対応）の狭さ |
| ★D4 | override header ABI | **同意** | 構造体byte一致・designated initializerで順序非依存＝堅牢 |
| ★F | SM_SIGN_CNT・config監査 | **概ね同意（1件申し送り）** | SM_SIGN_CNT副作用なし。監査漏れ`CONFIG_BT_NIMBLE_GATT_CLIENT`1件（現状実害なし・将来central時の申し送り） |

---

## ★A：C3 BT v5.5.4のSMP bond失敗の根因 — 要追加検証

**結論**：局在化（「d9753a31 blobを我々の統合に載せた非互換」）は妥当だが、
単一箇所への因果は未確定。**最大の発見は「非対称の再解釈」**：

- C5/C6のBT blob（libble_app/libphy/libbtbb）はv5.5.4とv6.1で**md5バイト完全一致**
  （`docs/blob-unify-v554.md:256-271`）＝「同一バイナリの再リンク」。一方C3の
  `libbtdm_app.a`は`dfdadb9d`(hal)→`d9753a31`(v5.5.4)で**バイト不一致**＝真の
  新バイナリ切替（`esp_bt.cmake:36-38`）。
- 時系列：C5のD-2d bond成功（`bt-shim.md:2561`, 2026-07-14, 別blob`015db3db`）は
  C5のv5.5.4化（§8-11, 07-15）**より前**。∴**PVCY修正込みNimBLEホストが新世代
  BT blobとend-to-endでbondした実機確認は、C3/C5/C6いずれも本リポジトリに存在
  しない**。C3の失敗は「初めて試された組合せ」。
- 反証済みの経路：`host_num_completed_packets`等のシンボルは不在、NimBLEの
  `BLE_HS_FLOW_CTRL`実効0（`bt-shim.md:2605-2615`実機確認）＝HCIフロー制御は無罪。
- 未検証の具体的リスク（file:line）：
  1. `esp_shim.c:354-451`のE_CTX give救済（pend_ring/sem_flush）は**hal blobの
     実測ISRタイミングにチューニングされた事後対応**＝別バイナリで同giveパターンが
     再現する保証なし。
  2. `bt/bt_shim.c:398-499`の`esp_intr_alloc`が「bt.cは割込みを厳密に2回登録」を
     ハードコード（3回目以降は黙ってスロット上書き`:471-478`）。v5.5.4 bt.cが登録
     回数を変えていれば「1個目は通るが2個目以降が詰まる」症状と整合し得る（未確認）。
  3. `docs/config-audit.md:143-147`がC3の`CONFIG_BT_CTRL_*`群を**明示的に監査対象外**。
     bt.cが直接参照する新規`#if CONFIG_BT_CTRL_XXX`欠落（PVCY型）は未検証。
- **次の一手（コード修正でなく計装）**：`ESP32C3_BT_EVT_TRACE`/`ACL_TRACE`/
  `APIERR_TRACE`＋`BT_INTR_TRACE_REG`（`bt_shim.c:437`）は`ASP3_BT_IDF_V554=ON`でも
  追加実装なしで使える既存計装。v5.5.4ビルドに対しこれらを実機で回し、鍵配布
  フェーズのどのACL/割込みが詰まるかを実測するのが最短。

---

## ★B：既定flipの安全性 — 要修正（C6の前提誤り）＋可逆性に一部穴

**B1（既定flip）＝要修正**：依頼文と`blob-unify-v554.md`§11の「C5/C6ともBT既定
v5.5.4」は**C6について誤り**（コーディネータが`esp_bt.cmake`で裏取り済み）：
- `esp_bt.cmake:31` `option(ESP32C6_BT_IDF61 ... OFF)`が上位ゲート。
  `ASP3_BT_IDF_V554`（既定ON）は`esp_bt_idf61.cmake:48`にあり、`if(ESP32C6_BT_IDF61)`
  （`:32`〜`:581`）の内側でしか評価されない。
- ∴素の`-DESP32C6_BT=ON`は`ESP32C6_BT_IDF61=OFF`→**hal(v8)経路**＝この統一作業の
  出発点で「cold PHY較正ハング」と名指しされた世代に着地。既定flipコミット
  （`e1e965c`）は`ASP3_BT_IDF_V554`の既定2箇所しか変えておらず`ESP32C6_BT_IDF61`の
  既定に触れていない。実機ヘルパ`tmp/c6ble.sh`が常に`-DESP32C6_BT_IDF61=ON`を手動
  付加する運用がこれを裏付ける。
- なお`esp_bt_idf61.cmake:52`はv5.5.4経路でも`set(IDF /home/honda/tools/esp-idf-v6.1)`
  のローカルパス直書きに依存（`wifi-blob-generation-todo.md`のToDo-2と同根）。

**B2（可逆性）**：WiFi（3チップ共通）・C3-BT・C5-BTは単一if/elseで可逆（override
二重定義排除・guard整合を確認）。**C6-BTのみ二段オプションで穴**：
`-DASP3_BT_IDF_V554=OFF`単独ではhalに戻らずv6.1着地。

**B3（見落とし回帰面）**：(i)上記C6既定不統一（最重要）、(ii)libcoexist.a差の
「coex実装時に再監査」がTODO未文書化、(iii)cold-PLL修正（§20）が
`ESP32C6_BT_IDF61=ON`経路にしか実装されていない。

---

## ★C：CLIC出口正規化 — 同意（設計正しい）

5問すべて同意。`core_support.S:556-577`（idle復帰）・`:638-656`（遅延ディスパッチ）を
`mepc設定+MPP=M+MPIEクリア+mret`へ変換、チップ層`irc_end_int`（`chip_support.S:288-291`）
が全出口分岐より先にmcause.mpil=0を強制＝どのmretもmil=0へ一貫帰着。
- mpilマスク`0xFF00FFFF`：bits[31:24]/[15:0]保持・bits[23:16]のみクリア＝競合なし。
- MPP=M（OR埋め）・MPIEクリア（AND-NOT単一ビット）＝到達時MIE=0既定のため取りこぼしなし。
- 非CLIC回帰：C3/C6/polarfire/rp2350の`irc_end_int`が`csrw mcause`を持たない（C5のみ）を
  ソース確認＝mret化は非CLICでは「plain jump代替」に縮退し安全。
- **唯一の留意＝検証範囲**：CLIC経路はQEMU非対応で実機C5 1台のみ。例外系出口未変換の
  正しさ（CLIC仕様「例外はmil非昇格」）の一次仕様書裏取りは未実施（反証条件）。

---

## ★D：blob統一機構の正しさ — override ABIは同意／no-op stubは要修正

**D4（override header）＝同意**：C5/C6の`idf_v554_override/esp_private/
wifi_os_adapter.h`（196行）は構造体本体byte一致（コメントのみ相違）、
`_wifi_disable_ac_ax`位置も正しく、埋める`esp_wifi_adapter.c`がC99 designated
initializer＝記述順非依存で堅牢。

**D5（no-op stub）＝要修正（未検証の回帰面）**：`esp_wifi_sta_get_ie`は「scanで
不要」の想定を超え、`wpa.c:2634`（`wpa_set_bss`＝STA接続の度に無条件）・
`wpa.c:2761-2764`（`#ifdef`無し無条件）からRSN IE取得に使われる。常にNULL返却で
`sm->ap_rsn_ie`が恒常NULL→`wpa.c:1171-1230`のIE整合性検証（ダウングレード/偽装
検出）が事実上無効化。実機実証はopen scanのみでSTA connect未検証＝§3の物証とは
矛盾しない検証範囲外の指摘。**WPA2接続を使うなら要対応**。

---

## ★E：C6 WiFi 实施92の帰属 — 要追加検証

実務判断（v5.5.4既定維持）は支持できるが、「pc=0 crash＝double-reset由来・
blob非依存」の説明にrigorの穴：
1. **同一条件の結果逆転**：`wifi-shim-c6.md:14305-14324`（v5.5.4 flash直後初回RTS＝
   1/1 clean）と`:14357-14364`（v5.5.4同種条件＝8/8 crash）が名目同一で正反対。
   esptool flashオプション/RTS間隔の交絡未排除（検証スクリプトはスクラッチのみ＝
   再現性検証不能）。
2. **hal側証拠が非対称**：v5.5.4はN=8、halは`cap.py`のboot1一回（N=1）のみ＝
   「blob非依存」の裏付けが片側だけ薄い。
3. **先行事例未照合**：実施91§6（`:14166-14190`）の同系統単発クラッシュを新理論と
   照合せず放置＝反証機会の見送り。

---

## ★F：SM_SIGN_CNT・config監査 — 概ね同意（1件申し送り）

- **SM_SIGN_CNT=1**：`ble_sm.c:2721`・`ble_att_svr.c:2548`の2箇所のみ`#if`文脈で
  使用、副作用なし。4変種とも値・文脈整合＝同意。
- **監査網羅性**：監査自身の基準（Kconfig `default y`×4変種未定義×実リンク）を
  独立再実行し、**`CONFIG_BT_NIMBLE_GATT_CLIENT`が基準合致なのに`config-audit.md`
  §3.11の候補表から漏れ**を検出（`ble_gattc.c`は4変種でリンク済＝`esp_bt.cmake:442/
  503/523`・`esp_bt_idf61.cmake:492`）。ただし`ble_gattc.c`は`#if MYNEWT_VAL(BLE_GATTC)`
  で内部ガード＋アプリ層のGATTクライアントAPI呼出し0件＝**現状実害なし**（将来
  central機能追加時の申し送り）。他（WiFi SAE/OWE・FTM/RRM/WNM・PHY較正永続化）は
  明示ポリシー/ビルド自己防御/既文書化トレードオフに帰着＝新規silent-lossなし。

---

## 推奨アクション（優先順）

1. **C6 BT既定の是正（★B1）**：意図が「C6もv5.5.4を既定」なら、`ESP32C6_BT_IDF61`の
   既定ONまたはオプション構造の一段化を行い、素の`-DESP32C6_BT=ON`がv5.5.4に着地する
   ようにする。意図が「C6はhal既定のまま」なら`blob-unify-v554.md`§11とreview-request
   の記述を訂正（現状は文書と実装が食い違い）。あわせてC6-BTの可逆性（★B2）を一段化で解消。
2. **★A/C3 bondの実測**：新規修正の前に既存計装（EVT/ACL/APIERR_TRACE＋
   BT_INTR_TRACE_REG）をv5.5.4ビルドで実機実行し、鍵配布フェーズの詰まり箇所を特定。
   併せて「PVCY修正込みホスト×新世代blob bond」をC5/C6でも1回実証し、C3の失敗が
   本当にC3固有かを切り分ける。
3. **★D5/no-op stub**：WPA2 STA接続をスコープに入れる前に`esp_wifi_sta_get_ie`の
   NULL返却がRSN IE検証を無効化する件を手当て（scan専用に留めるなら明示的に文書化）。
4. **★E/实施92**：両blob対称のN反復＋検証スクリプトのリポジトリ取り込みで帰属を
   rigorに再確認（実務判断＝v5.5.4維持は据え置き可）。
5. **★F**：`CONFIG_BT_NIMBLE_GATT_CLIENT`をcentral機能着手時の申し送りに記録。

（★C＝カーネル変更は設計として承認可。統一機構のコア＝override ABIも承認可。
要修正はいずれも「blob差替えそのもの」でなく周辺の既定値/stub範囲/帰属rigorに集中。）

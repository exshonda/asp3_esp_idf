# C3 BLE「central からconnectできない」静的コードレビュー

対象: リポジトリ `/home/user/asp3_esp_idf`、ブランチ `claude/c6-wifi-c5-dev-5vc6x9`（HEAD=`36c99f7`）。
比較区間: `f9dae7d`（C3実機でconnect/GATTディスカバリが動いていた最終確認点）〜`HEAD`。
方式: ソース変更なし、実機/ビルドなしの読解のみ。

## 0. 結論（先出し）

対象区間でC3のBLEビルドに影響する実ソース変更は **6ファイルのみ**（`ble_host_smoke.c` /
`evt_trace.c`(新規) / `bt_nimble_config.h` / `esp_bt.cmake` / `esp_shim.c` / `esp_shim.h`）。
このうち診断計装3点（`ESP32C3_BT_ACL_TRACE`/`ESP32C3_BT_EVT_TRACE`/`ESP32C3_BT_APIERR_TRACE`）は
CMakeオプションで**既定OFF**であり、実機再テストに使った `tmp/c3ble.sh` も
`-DESP32C3_BT=ON -DESP32C3_BT_SM=ON` のみを渡す＝**診断計装は今回の「connect不可」ビルドに含まれていない**。

残る実質差分は2つ：
1. `CONFIG_BT_NIMBLE_HS_PVCY=1`（bt_nimble_config.h、ae21e7a）
2. `esp_shim_sem_give()` の E_CTX/E_QOVR 挙動変更（esp_shim.c、1b8e028）

**両方とも、C5の「bond成功済み」実機ビルドに（世代は違えど）同一ロジックとして含まれている**
ことをgit historyで確認した（下記4節）。C5は別blob（`015db3db`）でconnect＋暗号＋bond全て
成功しており、これは両差分がconnectを壊さないことの実証に近い反証データである。
そのため「コード側候補あり」と強く言い切れる材料は無いが、**PVCY追加が呼び出す
コントローラ向け新規HCIコマンド列（起動時1回・resolving list操作）はC3のみ検証されておらず
（C3実機再テストはこの状態で中断）、C3固有のblob（`libbtdm_app`、C5/S3より旧世代）が
その新規コマンド列にどう応答するかは未確認**という一点だけが、唯一の未反証コード仮説として残る。

→ 総合判定：**「コード側候補は弱い（ほぼ環境要因が最有力）」だが、断定前に安価なA/Bテスト
（PVCYを一時的に0へ戻して同一central/同一環境で再試験）で完全に切り分けられるので、
それを次の一手として先に行うことを推奨する。**

---

## 1. 対象コミット棚卸し表

`git log --oneline f9dae7d..HEAD -- apps/ble_host_smoke/ asp3/target/esp32c3_espidf/`（時刻昇順）

| commit | 概要 | C3ビルドへの影響 | connect経路への関与 |
|---|---|---|---|
| `1190be9` | 自前GATTサービス(0xABF0-3)＋SM初期有効化(ESP32C3_BT_SM既定ON) | `ble_host_smoke.c`+320行、`esp_bt.cmake`+54行。adv開始コード自体(`ble_gap_adv_start`呼出し・`conn_mode`/`disc_mode`)は不変 | **なし**（GATT/SMはconnect後の処理。adv paramは無変更を確認） |
| `830a194` | bond確認マーカ計装＋実機ヘルパ`tmp/c3ble.sh` | `ble_host_smoke.c`+50行（RTCマーカのみ）、新規0xABF4(READ_ENC)特性追加 | **なし**（マーカ書込み・暗号必須特性はconnect後のGATT/SM専用） |
| `9335414` | 接続5秒後のslave Security Request移植 | `ble_host_smoke.c`+49行 | **なし**（`bt5_security_tick`は`gap_event_cb`のCONNECT後にのみ計時開始） |
| `9c31c96` | HCI EVT `--wrap`計装 `evt_trace.c`新規 | 既定OFF(`ESP32C3_BT_EVT_TRACE`) | **なし**（オプション既定OFF、`tmp/c3ble.sh`もON化しない） |
| `b6be1be` | ENC→ETIMEOUT実秒計装＋pend_ring 100ms周期flushをmain_taskへ追加 | `esp_shim_queue_flush_pending`/`sem_flush_pending`呼出しが**常時**(build時option無し)main_taskループに追加 | **理論上わずかにあり**だが、pend件数0の高速パス（volatile読み1回で即return、ロック無し）のみが常時実行区間＝オーバーヘッド無視できる（4節末で評価） |
| `57ab52a` | RXパイプライン計装＋key_dist実験（最終的にspec準拠ENC\|IDへ復帰） | `evt_trace.c`拡張（OFF既定） | **なし**（既定ビルドに影響する`ble_hs_cfg.sm_*_key_dist`は現在ENC\|IDで確認済み＝実験前と同一） |
| `1b8e028` | `esp_shim_sem_give`のE_CTX/E_QOVR処理変更＋SVC_PERROR診断 | `esp_shim.c`/`.h`（**常時有効**・オプション非依存） | **要検討→4節で詳細評価。C5実証あり** |
| `da5d02d` | PVCY=0が原因と確定（docsのみ、対象パス外） | ドキュメントのみ | - |
| `ae21e7a` | `CONFIG_BT_NIMBLE_HS_PVCY=1`追加 | `bt_nimble_config.h`+13行のみ | **要検討→2節で詳細評価。本レポートの最重要候補** |
| `36c99f7` | 中断メモ（docsのみ） | ドキュメントのみ | - |

---

## 2. 候補①（唯一の未反証コード仮説）：PVCY起動時resolving-list HCIバーストがC3コントローラ(blob)を壊す可能性

### 機構

`CONFIG_BT_NIMBLE_HS_PVCY=1` は `ble_hs_startup.c` の起動シーケンス（`ble_hs_sync`→
`ble_hs_startup_go`、すなわちアプリの`on_sync`/`start_advertising`より**前**に一度だけ走る）
に新しい分岐を追加する：

`hal/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_startup.c:567-585`
```c
if (ble_hs_cfg.store_gen_key_cb) {
    ...
#if MYNEWT_VAL(BLE_HS_PVCY)
    if (rc == 0) { ble_hs_pvcy_set_our_irk(gen_key.irk); }
#endif
} else {
    rc = -1;
}
#if MYNEWT_VAL(BLE_HS_PVCY)
if (rc != 0) {
    ble_hs_pvcy_set_default_irk();
    ble_hs_pvcy_set_our_irk(NULL);   /* ★戻り値は捨てられる */
}
#endif
```

`apps/ble_host_smoke/ble_host_smoke.c` は `ble_hs_cfg.store_gen_key_cb` を一切設定しない
（grep済み・確認）。したがって **PVCY=1では毎回必ず** `ble_hs_pvcy_set_default_irk()` →
`ble_hs_pvcy_set_our_irk(NULL)` に入る。C3/C5とも `CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY` は
未定義/0なので、`ble_hs_pvcy.c` の非HOST_BASED分岐（`ble_hs_pvcy.c:342-388`）が実行される：

```c
rc = ble_hs_pvcy_set_resolve_enabled(0);   /* HCI LE_Set_Address_Resolution_Enable(0) */
rc = ble_hs_pvcy_clear_entries();          /* HCI LE_Clear_Resolving_List */
rc = ble_hs_pvcy_set_resolve_enabled(1);   /* HCI LE_Set_Address_Resolution_Enable(1) ★有効化 */
...
rc = ble_hs_pvcy_add_entry(tmp_addr=0, addr_type=0, irk=0); /* HCI LE_Add_Device_To_Resolving_List */
```

**これはPVCY=0（D-2c時点＝connect動作実績あり）では一切発行されなかった、新規のコントローラ
向けHCIコマンド4連発である。** advは`ble_gap_adv_start()`（`ble_gap.c:4081`。全文grep済み、
PVCY関連分岐は皆無）そのものはPVCY非依存だが、**この起動時バーストが「advは出るがconnectだけ
失敗する」症状の唯一の新規要因になり得る**：Address Resolutionをコントローラ内で有効化すると、
以後のLL層（アドバタイズ・接続受理を含む）は resolving list を参照した処理に切り替わる
（BT Core Spec Vol 6 Part B）。ホスト側は`ble_hs_pvcy_set_our_irk()`の戻り値を握り潰しており
（`ble_hs_startup.c:584`で代入なし）、**途中のHCIコマンドが失敗/異常応答してもble_hs_syncは
失敗せず、advは正常に開始される**——これは実測（広告 `ASP3-C3-BLE` 確認）と完全に整合する。
「広告は出るがconnectだけ失敗」という非対称な症状を、**ホスト側では検出されない、コントローラ
内部状態のサイレントな異常**として説明できる数少ない機構がこれである。

own_addr_type自体は `ble_hs_id_infer_auto(0, ...)` （app側 `privacy=0`固定・
`ble_host_smoke.c:376`）でRPA系を一切使わないため、この起動時バースト以外の経路
（`ble_hs_id_use_addr`のRPA分岐＝`ble_hs_id.c:378`、`ble_gap_set_priv_mode`等）はPVCYの影響を
受けない（コード上確認済み）。

### C5との対比（重要な反証データ）

- C5のPVCYコンフィグ（`asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h:113,127`）も
  `CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY 0` + `CONFIG_BT_NIMBLE_HS_PVCY 1` で**全く同じ非HOST_BASED
  分岐**を通る。C5は実機でconnect＋暗号＋PAIRING_COMPLETEまで成功済み（`ae21e7a`本文）。
- つまり**この起動時HCIバースト自体はC5のblob（`015db3db`, `libble_app`系）では実害が無い**こと
  が実証されている。C3のblobは`dfdadb9d`＝`libbtdm_app`系で世代が異なる（CLAUDE.mdでも言及の
  通りconfigもblobも別物）。**同一ホストコードが「新しいコントローラ世代では無害・古い世代では
  未検証」という構図**になっており、C3固有のリグレッションだとすれば「C3のblobがこの新規コマンド
  列に対して何かしら異なる応答をする」という仮説になる（未反証・未確認）。

### 確度・判別方法

確度：**中〜低**（機構は具体的で新規性があるが、C5の反証データがあるため強く支持はできない）。

**最も安価な判別テスト（環境要因3件の切り分け後、最初にやるべきこと）**：
1. まず環境要因の切り分けを完了する（メモ記載の手順どおり）：
   a. BlueZ側で `bluetoothctl remove 60:55:F9:57:C2:60` → 電源再投入（DUT）→ `scan on` →
      `connect`。
   b. (a)がダメなら**別central**（スマホ/nRF Connect等）で同一DUTへ試行——ここで central側
      キャッシュ/DTR-RTSリセット/ttyACMドリフトの3件を切り分ける。
2. (1)がいずれも失敗＝真にDUT側でconnectが成立しないなら、**PVCYのA/Bテスト**を行う：
   - `asp3/target/esp32c3_espidf/bt/stub/include/bt_nimble_config.h` の
     `#define CONFIG_BT_NIMBLE_HS_PVCY 1` を**一時的に**（コミットせず）`0`へ戻してビルド・
     flashし、**全く同じcentral・同じ操作**で再試行する。
   - PVCY=0で connect が成功 → 本候補が実証される。次段は「起動時4HCIコマンドのどれが原因か」
     の二分探索（`ble_hs_pvcy_set_our_irk()`の中身をコメントアウトしながら1個ずつ切り戻す）。
   - PVCY=0でも connect が失敗 → **本候補は棄却**。コード側要因は実質消滅し、環境要因
     （メモの3件）が最有力という結論を確定してよい。
3. 可能なら（PC側sudo権限が使えるようになった場合）`btmon`/nRF Sniffer で**CONNECT_REQ自体が
   central から出ているか・DUTがCONNECT_INDに応答しているか**をHCI/OTAレベルで直接見る。これは
   host/コントローラどちらの問題かを一発で切り分けられる最終手段（現状PC制約でできないと
   `docs/bt-shim.md`に記載あり）。

---

## 3. 候補②（低確度・参考）：esp_shim_sem_give の E_CTX/E_QOVR 挙動変更

`asp3/target/esp32c3_espidf/wifi/esp_shim.c`（1b8e028、esp_shim.c:454-476付近）で
`esp_shim_sem_give()` の意味論が変わった：

- 旧: `sig_sem()==E_OK` のときのみ成功(1)、それ以外は失敗(0)。
- 新: `E_OK`/`E_QOVR`を成功(1)扱いに拡大し、`E_CTX`（MIE==0＝クリティカルセクション/ISR文脈）
  時は保留カウントへ退避して**成功(1)を返しつつ**後で`esp_shim_sem_flush_pending()`が精算する。

この関数はBTコントローラのOS adapter層（`_semphr_give_from_isr`等）から常時呼ばれる、
diagnosticオプションに依存しない**常時有効コード**であり、connect受理シーケンス中にコントローラが
セマフォgiveを発行する経路が存在すれば理論上影響し得る。

**しかしgit historyを辿ると、C5の"bond成功実証済み"ビルド（`ae21e7a`時点のC5）はこの変更を
byte単位で含んでいる**：
- `e139a30`（C5 esp_shim.c をC3版から再生成、2026-07-14 20:59）は`1b8e028`（同日20:36）の
  **後**にコミットされており（`git log --oneline`の表示順は厳密な時系列ではなくトポロジ順である
  ため、`--format='%ai'`でタイムスタンプを直接確認して判明）、再生成元のC3 `esp_shim.c` には
  既に`shim_sem_pend`/`E_QOVR`ロジックが入っていた。
- 実際 `asp3/target/esp32c5_espidf/wifi_v8/esp_shim.c` に同一ロジック
  （`shim_sem_pend[]`, `E_QOVR`分岐, `esp_shim_sem_flush_pending()`）が存在することを確認済み。
- C5はこの状態でconnect＋暗号＋bond成功（`2884922`, `ae21e7a`）。

→ **この変更もC5では実害なしと実証済み**であり、候補①と同様「共通コードでC5では無害」という
帰結になる。確度：**低**（C3固有のblob差で顕在化する可能性はゼロではないが、①より状況証拠が薄い）。
判別法：候補①のA/Bテストで connect が直っても直らなくても、追加で`esp_shim_sem_give()`を旧実装
（`E_OK`のみ成功）に一時的に戻して同様のA/Bを行えば独立に切り分けられる。

---

## 4. 検討したが却下した観点（task記載の確認項目）

- **own_addr_type / IRK未設定によるRPA生成失敗・adv-connectアドレス不一致**：
  `ble_host_smoke.c:376` で `ble_hs_id_infer_auto(0, ...)`（privacy=0固定）を使用しており、
  advの own_addr_typeは常にPUBLIC/RANDOM staticでRPAを使わない。`ble_hs_id_use_addr`の
  PVCY分岐（`ble_hs_id.c:378`）はown_addr_typeがRPA系のときにしか到達しないため**非該当**。
  advアドレスとconnect時のアドレス解決が不一致になる経路はコード上存在しない。
- **SM関連CONFIG／メモリ予算（RAM 92%台）でヒープ/プール枯渇→connectイベントが黙って失敗**：
  対象区間でMSYS/ACLバッファ等のプールサイズ変更は無い（`bt_nimble_config.h`の差分はPVCY追加
  13行のみ）。PVCY=1自体が追加するBSSは`ble_hs_pvcy.c`の非HOST_BASEDブランチで
  `static uint8_t[16]*2 + uint16_t`程度(約34バイト)で無視できる。RAM逼迫はf9dae7d以降の
  SM/計装追加で段階的に93.4x%→93.54%まで上昇しているが、リンクは全コミットで成功しており
  （オーバーフローなし）、これは「connect特有の失敗」を説明する機構ではない
  （RAM逼迫が真因ならconnect前のadv/sync自体も不安定になりやすいはずだが、advは安定して確認
  できている）。確度は最低ながら、実機再現性が悪い（flaky）場合の背景要因として完全には
  排除しない。
- **D-2d診断計装（evt_trace.c／RXパイプライン計装／pend_ring flush）の残留副作用**：
  `ESP32C3_BT_ACL_TRACE`/`ESP32C3_BT_EVT_TRACE`/`ESP32C3_BT_APIERR_TRACE`は全てCMakeオプション
  既定OFFであり、`tmp/c3ble.sh`のbuildコマンドも有効化していない。**今回の「connect不可」
  ビルドには一切含まれていない**（1節参照）。ただし`b6be1be`で追加された
  `esp_shim_queue_flush_pending()`/`esp_shim_sem_flush_pending()`のmain_taskからの100ms周期
  呼出しは常時ビルドに含まれる（オプション非依存）。ただし両関数とも保留件数0のときはロック無し
  の1回のvolatile読みで即returnする高速パスがあり（`esp_shim.c`のflush実装参照）、advertising
  中もconnect試行中も保留は基本0のはずなのでオーバーヘッドは無視できる。
- **C3のadvパラメータ（connectable/scannable）がD-2d変更で変わっていないか**：
  `git log -p f9dae7d..HEAD -- apps/ble_host_smoke/ble_host_smoke.c | grep conn_mode/disc_mode/
  adv_params/ble_gap_adv_start(`は**ヒットなし**＝f9dae7d以降、adv開始コードは1バイトも変わって
  いない。`adv_params.conn_mode = BLE_GAP_CONN_MODE_UND` / `disc_mode = BLE_GAP_DISC_MODE_GEN`
  のまま。**この経路は完全に無罪。**
- **bond store callbacks（PVCY=1が要求するstore APIの未実装/スタブでconnect段階の失敗）**：
  `ble_hs_pvcy_set_default_irk()`が使う`ble_store_read_local_irk`/`ble_store_write_local_irk`
  は`ble_store_config.c`（`ESP32C3_BT_SM`時のstore実装、C5と共通）に完全実装されている
  （`ble_store_config_read_local_irk`/`write_local_irk`関数を確認済み）。スタブ未実装による
  即時失敗の経路は無い。またconnect自体（LL接続確立）はSM/bond処理より前の段階であり、
  bond store云々はそもそもconnect失敗の説明にはならない（bondはconnect後の話）。

---

## 5. 候補の優先順位付きリスト（最終）

| 順位 | 候補 | 機構要約 | 根拠(file:line) | 確度 | 安価な判別法 |
|---|---|---|---|---|---|
| 1 | PVCY起動時resolving-list HCIバースト | `ble_hs_startup.c`が起動時（sync前）に`LE_Set_Address_Resolution_Enable(0/1)`+`LE_Clear_Resolving_List`+`LE_Add_Device_To_Resolving_List`をC3コントローラへ新規発行。戻り値は握り潰されるためhost側は失敗に気付かず advは正常開始。C3のみ旧世代blob(`libbtdm_app`)で未検証 | `ble_hs_startup.c:567-585`, `ble_hs_pvcy.c:342-388`, `bt_nimble_config.h`(ae21e7a diff) | 中〜低（C5で同一コードが無害と実証済みのため） | `CONFIG_BT_NIMBLE_HS_PVCY`を一時的に0に戻しA/B再試験 |
| 2 | `esp_shim_sem_give`のE_CTX/E_QOVR挙動変更 | コントローラOS adapterからの常時呼出し経路の意味論変更。connect受理中にsem giveが絡む経路があれば理論上影響し得る | `esp_shim.c`(1b8e028 diff、行454-476相当) | 低（C5で同一実装が無害と実証済み） | `esp_shim_sem_give`を旧実装に戻しA/B再試験（候補1と独立に） |
| 3 | RAM逼迫（93.4x%→93.54%の段階的上昇） | 直接connectを壊す機構は特定できないが、flaky動作の背景要因として排除しきれない | 各commit本文のRAM%記載 | 最低（advが安定している時点で説明力が弱い） | ビルドログでlink後RAM%を確認、必要なら診断計装を全OFFにして再ビルドしRAM%比較 |

**「コード側候補なし」とまでは言い切らない**——候補1は具体的で新規かつC3で未検証という一点が
残るため。ただしC5の反証データにより確度は控えめにしか主張できない。**次の一手は2節に記載の
A/Bテスト**（PVCYオンオフでのconnect比較）であり、これだけで「コード側候補が実在するか／
環境要因のみか」を実機1〜2回のflashで機械的に決着できる。

---

## 6. 参照ファイル一覧（このレビューで実読したソース）

- `/home/user/asp3_esp_idf/apps/ble_host_smoke/ble_host_smoke.c`
- `/home/user/asp3_esp_idf/apps/ble_host_smoke/ble_host_smoke.cfg`
- `/home/user/asp3_esp_idf/asp3/target/esp32c3_espidf/esp_bt.cmake`
- `/home/user/asp3_esp_idf/asp3/target/esp32c3_espidf/bt/stub/include/bt_nimble_config.h`
- `/home/user/asp3_esp_idf/asp3/target/esp32c3_espidf/wifi/esp_shim.c`(+`.h`)
- `/home/user/asp3_esp_idf/asp3/target/esp32c5_espidf/bt/stub/include/bt_nimble_config.h`
- `/home/user/asp3_esp_idf/asp3/target/esp32c5_espidf/wifi_v8/esp_shim.c`
- `/home/user/asp3_esp_idf/hal/components/bt/host/nimble/nimble/nimble/host/src/ble_gap.c`
- `/home/user/asp3_esp_idf/hal/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_id.c`
- `/home/user/asp3_esp_idf/hal/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_pvcy.c`
- `/home/user/asp3_esp_idf/hal/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_resolv.c`
- `/home/user/asp3_esp_idf/hal/components/bt/host/nimble/nimble/nimble/host/src/ble_hs_startup.c`
- `/home/user/asp3_esp_idf/hal/components/bt/host/nimble/nimble/nimble/host/store/config/src/ble_store_config.c`
- `/home/user/asp3_esp_idf/docs/bt-shim.md`（該当区間の記録全て）

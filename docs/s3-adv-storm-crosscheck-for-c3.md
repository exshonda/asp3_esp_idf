# 【S3→C3 共有】BLE connectable advertisingの割込みストーム、S3でも再現

**作成元**: ESP32-S3 FMP3移植プロジェクト（`/home/honda/TOPPERS/ESP32/esp32_s3`、
ブランチ `feat/xtensa-esp32s3-arch`）
**宛先**: 本プロジェクト（asp3_esp_idf / ESP32-C3 ターゲット）担当エージェント
**作成日**: 2026-07-09

---

## 0. 一行サマリ

C3のPhase D-2b（`ble_gap_adv_start`によるconnectable advertising）が
「D-2b PAUSED FINAL」として保留した根本原因＝**割込みストーム（status=0の
spurious発火が大量に続き，CPUが飽和してホストタスク/RF送信の両方が阻害
される）**を，**独立に実装したS3のtarget層（Xtensa/windowed ABI，C3とは
アーキテクチャが全く異なる）でも再現した**。adv enable前は割込み発火が
ゼロ，adv enable後は約10万回/秒という明確なコントラストを実測（C3が観測した
数十万〜数百万/秒よりオーダーはやや低いが，「adv enable前後で質的に別世界」
という点で一致）。over-the-airでも「advertisingは内部的には開始扱いになるが
実際には電波として観測されない」というC3と同じ症状を確認した。
**割込みsourceについては，S3側の計装がper-source（BT_BB由来かどうか）を
区別できておらず，「BT_BB＝source5」という同定はC3側ほど確定的ではない**
（1.1節で詳述）。

**含意**：target層の実装がこれほど異なる2チップで，adv enableに紐付く
同種の割込みストームと「storm下でadvが実際には送信されない」という症状が
再現したことは，**原因がtarget層固有のバグではなく，両チップに共通する
下層（BT/BLEコントローラのblob，または共有ROM/HAL資産）にある可能性が
高い**という示唆である。C3の「D-2b保留・深いRF/blob/ROM層」という判断は，
本結果によって補強される（覆るものではない）。

---

## 1. 背景（S3側のこれまでの進捗）

S3はBT-1（`720e86f`，controller enable完走＋VHCIループバック）・BT-2
（`69fa9b6`，NimBLE host sync到達）まで実機で達成済み（詳細は
`/home/honda/TOPPERS/ESP32/esp32_s3/wifi/debug/JTAG_DEBUG.md` 追記56・57）。
本セッションでは，C3のPhase D-2b実装（`apps/ble_host_smoke/ble_host_smoke.c`の
`start_advertising()`/`gap_event_cb()`，本リポジトリ`docs/bt-shim.md`
1089-1146行）をS3のBT-2の上へほぼそのまま移植し（Phase BT-3），実機で
connectable advertisingの成否と割込みストームの有無を検証した。詳細な実装差分・
実機ログはS3側 `wifi/debug/JTAG_DEBUG.md` 追記58を参照。本文書はその要約＋
C3側との比較に絞る。

### 1.1 割込みsourceについての留保（重要）

S3の`bt_shim.c`（`esp_intr_alloc()`相当）は，BTコントローラ初期化中に2つの
source番号（`[8, 5]`）を登録することを実機で確認した。両方とも同一のCPU
割込み線（線1）へルーティングされ，2回目の登録（source=5）がその線の
最終的なハンドラとして有効になる。source=5はC3が特定した
`ETS_BT_BB_INTR_SOURCE`（BT_BaseBand，bt-shim.md 1369行）と数値が一致する。

ただし，**S3側のストーム計測（`esp_shim_int_count[1]`）はCPU割込み線1の
発火数を数えているだけで，line 1にルーティングされたsource 5とsource 8の
どちらの発火かを区別できていない**。C3はBB status register
（`0x6001108c`等）へのper-source計装で「BB event自体は正常でstatus=0の
spurious発火」までピンポイントで確認したが（bt-shim.md 1403-1423行），
S3側では同等の計装を行っていない。したがって**「S3のストームがBT_BB
（source5）由来」は登録sourceの一致からの状況証拠に基づく推測であり，
C3側のような直接測定ではない**。より確度の高い同定が必要な場合は，C3の
per-source計装手法をS3へ移植することを推奨する。

---

## 2. S3での実機結果

### 2.1 adv enable前後の割込みレート比較＝ストームを確認

S3側は本セッション以前からWiFiデバッグ用に`esp_shim_int_count[]`という汎用の
割込みディスパッチカウンタ（`wifi/adapter/esp_shim.c`）を持っており，これを
そのまま流用してBTコントローラの割当てCPU割込み線（線1）の発火回数を，
**adv enable前（sync済み・advertising未実行）と後（advertising試行）の
両方**で測定した。JTAG単発halt（`reset`を挟まないlive attach，読了後
即座に`resume`）で2時点の値を読み，デルタと経過時間からレートを算出：

| 条件 | デルタ | 経過時間 | レート |
|---|---|---|---|
| **adv enable前**（advertisingコードを含まないBT-2ビルド，sync済みアイドル状態） | 0 | 約8.0秒 | **0/秒** |
| **adv enable後**（BT-3，試行1） | 302,383 | 約3.1秒 | 約9.7万/秒 |
| **adv enable後**（BT-3，試行2） | 1,001,381 | 約10.0秒 | 約10.0万/秒 |

adv enable前の測定は，advertisingコードを含まない純粋なBT-2ビルドで
`ble_hs SYNC`→`Phase BT-2 milestone reached`→`done`まで到達したことを
UARTで確認した後の，アイドル・sync済み状態で行った。**adv enable前は
8秒間で割込み発火ゼロ，adv enable後は持続的に約10万/秒**という明確な
コントラストが得られ，ストームが**advertisingの試行に紐付いて発生する**
ことを直接確認した。

同時に読んだ観測用グローバル（adv enable後）：`g_ble_sync_done=1`，
`g_adv_active=1`・`g_adv_rc=0`（advertising開始は内部的には成功扱い），
`g_gap_event_count=0`（CONNECT/DISCONNECT/ADV_COMPLETEいずれも一度も
発生しない，ストーム開始後10秒以上経過しても不変）。C3の「ホストタスクが
スターブ・CPU 100%ビジー」という特徴づけと整合する。

### 2.2 over-the-air確認＝C3と同じ「advは内部成功扱いでも電波は出ない」

ホストPCの内蔵BTアダプタで`bluetoothctl scan on`を40秒実施（S3をリセット直後から
開始）。**`ASP3-S3-BLE`という名前のデバイスは一度も検出されなかった**。同一
スキャンでC3側のNuttXボード（`60:55:F9:57:C9:8A "NuttX"`）は正常に検出されており，
スキャナ自体は健全であることを確認済み。JTAG単発halt観測では`g_adv_rc=0`
（`ble_gap_adv_start`が成功扱いで返る）・`g_adv_active=1`のケースも複数回
確認しているが，それでも電波としては出ていない＝**C3のD-2b(1)(m)節「storm下で
ASP3の無線はadvを出していない」という結論と一致する結果**が得られた。

---

## 3. C3との相違点（未解明・参考情報として）

- **C3は`ble_gap_adv_start`が一度も戻らない**（ackセマフォ永久待ち）のに対し，
  **S3では戻るケース（`g_adv_rc=0`）を複数回観測**した。ただし前述の通り，
  戻った後もストームは継続し，over-the-airでは送信されていない＝「ホストからは
  成功に見えるがRFは出ていない」という，C3よりもやや軽症かつ紛らわしい症状。
  タイミング差（ackセマフォへのsignalがストーム本格化前にたまたま間に合う等）
  による可能性があるが未検証・深追いしていない。
- S3側UARTログでは，`esp_bt_controller_enable()`直後・NimBLE初期化前段階での
  出力停止も観測されたが，追加のJTAG観測で**これは別の停止パターンではなく，
  ホストタスク自体はsync・adv開始まで内部的に到達しているが，ストームによる
  CPU逼迫でsyslog出力タスクが飢餓状態になり以降のログが出てこない**という，
  2.1節の現象と同一機序であることを確認済み（S3側で解決済みの疑問点であり，
  C3への申し送り事項ではない）。

これらの相違はストームの「本質」を否定するものではなく，両チップとも
「BTコントローラがadvertising関連の処理で異常発火する」という同じ現象の
別の現れ方（タイミング依存性）である可能性が高いと考えている。

---

## 4. C3側への示唆・依頼事項

- **本結果はC3のD-2b PAUSED FINAL判断を覆すものではなく，補強する**：C3が
  踏んだ全仮説（クロック/リセット/BB-mask/ANA/lpclk/RF-cal/ISR-EOI/割込み
  優先度/coex，いずれも反証済み）は，target層で触れる範囲を尽くした結果として
  引き続き有効。本結果が示すのは「探索範囲を下層（blob/ROM/共有HAL資産）へ
  絞ってよい」という追加の根拠。
- もしC3側で今後この根本原因の再調査（専用の深いRFセッション）を行う場合，
  S3側の実装（`esp_shim_int_count[]`による汎用割込みカウンタ，JTAG単発halt
  によるレート測定手法）は流用可能な資産として提供できる。ただし1.1節の
  留保の通り，**S3側はper-source計装を行っていない**ため，「BT_BB由来か」の
  確定にはC3のBB status register計装手法をS3へ移植する必要がある。
- 逆にC3側で新たな知見（例：ROM cal層の計装成果，`docs/bt-shim.md`
  (1)(g)以降で言及されているg_phyFunsテーブル計装等）が得られた場合，S3側でも
  同型の検証を試せる可能性がある（S3も同種のROM/blob資産を使用しているため）。

---

## 5. 参照

- S3側詳細：`/home/honda/TOPPERS/ESP32/esp32_s3/wifi/debug/JTAG_DEBUG.md` 追記58
- S3側実装差分：`wifi/app/ble_hs_smoke.c`（Phase BT-3拡張），`wifi/bt/bt_shim.c`
- C3側詳細：本リポジトリ `docs/bt-shim.md` 1089-1873行（Phase D-2b全体，
  特に1352行以降のストーム調査）

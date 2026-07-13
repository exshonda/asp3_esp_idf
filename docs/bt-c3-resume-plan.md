# C3 BLE D-2b（connectable advertising）再開計画

**作成日**: 2026-07-13
**担当**: 再開計画策定タスク（読み取り専用・机上分析のみ。実機/ビルド未実施）
**入力**: `docs/s3-bt-intr-source-overwrite-fix-for-c3.md`・`docs/bt-shim.md`・
`docs/s3-adv-storm-crosscheck-for-c3.md`・`docs/c5c6-lessons-for-s31.md`・
`docs/c5-bringup.md`実施42-43・`docs/wifi-shim-c6.md`実施88・
`asp3/target/esp32c3_espidf/bt/bt_shim.c`・`hal/components/bt/controller/esp32c3/bt.c`

---

## 1. 現状整理

C3のPhase D-2b（`ble_gap_adv_start`）は，advertising enable（HCI `0x200a`）直後に
BT_BB (source5) 由来と見えるが実体はstatus=0のspurious割込みストーム（〜1-3.8M/s）が
発生しCPU飽和・ホストタスク/コントローラadv送信の両方を阻害する，として
「D-2b PAUSED FINAL」（`docs/bt-shim.md` 1823-1873行）で保留になった。クロック/
リセット/ANA/BB-mask/lpclk/RF-cal/ISR-EOI/割込み優先度/coexの9角度を全て反証した
上での保留であり，探索は「深いRF/BB/blob層」へ絞られていた。その後2件の材料が
増えた：(1) S3（Xtensa，別target層実装）が独立にBT/BLE移植を進め，同型ストームの
**真因をtarget層の実装バグ（`esp_intr_alloc`の多重登録によるsource取り違え）と
確定・修正済み**にした（`docs/s3-bt-intr-source-overwrite-fix-for-c3.md`）。
(2) 本セッションのC5/C6 WiFi調査で「Direct Bootのブート列欠落バグファミリー」
（APM/TEE等）という新しい方法論が確立された（`docs/c5c6-lessons-for-s31.md`）。
本書はこの2件を判定材料に，D-2b再開の実行計画を立てる。

---

## 2. S3真因のC3への適用性判定（机上）——**結論：バグは実在する（高確度）**

### 2.1 S3が確定した真因（要約）

BTコントローラblob（`libbtdm_app.a`）は`bt.c`の`interrupt_alloc_wrapper`経由で
`esp_intr_alloc()`を**source 8→source 5の順に2回**呼ぶ。target層の実装（S3の
修正前・C3の現行）は

- 単一の静的`intr_handle_data_t`（配列でない）
- 全呼出しを無条件で固定CPU割込み線1へ配線
- `esp_shim_set_isr(固定線1, handler, arg)`を呼出しごとに実行

という構造のため，**2回目（source5）の登録が1回目（source8）のhandler/argを
上書き**する。結果，本来source8で発火すべき割込みがsource5用handlerで処理され，
そのhandlerはsource8のstatus/clearレジスタに一切触れないため，CPUレベルで
deassert→即re-assertを繰り返す（真の再トリガ型ストーム）。S3はこれを
`intr_handle_data_t`の配列化＋呼出し順でのCPU線分離で修正し（コミット`5e6d4b3`），
ストームは完全に消滅してPhase BT-3（connectable advertising）が完了した。

### 2.2 C3の`bt_shim.c`にこのパターンが実在するかの直接確認

`asp3/target/esp32c3_espidf/bt/bt_shim.c` 393-447行を実際に読んだ。**S3の修正前と
構造的に同一**であることを確認した：

```c
#define BT_INTR_CPU_LINE      1

struct intr_handle_data_t {
    int source;
};
static struct intr_handle_data_t  bt_intr_handle;   /* 単一・配列でない (407-411行) */

esp_err_t
esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg,
               intr_handle_t *ret_handle)
{
    (void) flags;                                   /* 417行：flags無視 */
    bt_intr_handle.source = source;                  /* 418行：2回目呼出しで上書き */
    ...
    sil_wrw_mem(...(BT_INTMTX_BASE_ADDR + source*4), BT_INTR_CPU_LINE); /* 427-428行：常に固定線1 */
    ...
    esp_shim_set_isr(BT_INTR_CPU_LINE, (void *) handler, arg);  /* 440行：2回目呼出しでhandler/arg上書き */
    ...
}
```

397行のコメント「BTコントローラは単一のISRソースしか要求しないため（実測：
esp_intr_alloc呼出しは1箇所）」は，**S3側で全く同じ文言のコメントが後にstaleと
判明したのと同じ形**である。

### 2.3 「実測：1箇所」という既存の確信は再検証が必要——決定的な理由

`docs/bt-shim.md` 1369-1371行・1400行によれば，C3側は既に「blobが登録するsource
番号をRTCへ記録して確定」し，**常にsource=5（ETS_BT_BB_INTR_SOURCE）**という
結果を得ている。しかし，この記録機構は`esp_intr_alloc`の呼出しごとに**同一の
RTCレジスタ（0xC0）へ上書き記録**する実装であり（bt_shim.c 411行の単一静的変数
と同型の設計）——**もしS3と同じくsource8→source5の順に2回呼ばれていた場合，
RTCに残るのは最後の書込み（source=5）のみで，1回目のsource=8は記録の時点で
既に消えている**。つまり「常に5だった」という既存の観測は，(a)本当に1回しか
呼ばれていない，(b)2回呼ばれ最後がたまたま5だった，のどちらでも同じ見え方に
なる——**既存データは呼出し回数を判別できない**（S3文書5節が指摘する通り）。

### 2.4 ソースコードレベルの補強証拠——C3とS3は**文字通り同じ`bt.c`を使う**

`hal/components/bt/controller/`を確認したところ，**`esp32s3`専用の`bt.c`は
存在せず**（`Kconfig.in`のみ），BTコントローラソースは`esp32c3/bt.c`を**S3も
そのまま使う**構成になっている（blobライブラリも`lib_esp32c3_family/{esp32c3,
esp32s3}/libbtdm_app.a`という「c3ファミリ」ディレクトリ配下）。`interrupt_alloc_wrapper`
（`hal/components/bt/controller/esp32c3/bt.c` 351,426,856行）はS3・C3で**同一の
コンパイル単位**であり，実際に何回・どのsourceで呼ばれるかはこの中の
`._interrupt_alloc`関数ポインタ経由でblob（`libbtdm_app.a`）内部が決める。S3側で
実機観測により「source8→source5の2回」が確定している以上，**同じ`bt.c`と
同系列のblobを使うC3も同じ2回呼出しパターンを持つ可能性が非常に高い**——
これは「構造が似ている」という類推ではなく，「同一ソースファイル・同系列blob」
という一次情報に基づく強い状況証拠である。

**判定2（結論）**: S3が確定したバグパターンは，C3の`bt_shim.c`に**コード上
実在する**（393-447行，構造完全一致）。既存のC3側「source=5確定」という観測は
バグの有無を判別できておらず（2.3節），むしろ`bt.c`共有という一次情報（2.4節）は
バグ実在を支持する方向に働く。**未検証だが高確度**——次段は3.1節の低コスト
検証で白黒つけられる。

**★既存のC3計装データそのものが，上書き仮説でより単純に説明できる**：
`docs/bt-shim.md` 1373-1423行の実測（(1)(a)節）——①ストームの99.997%が
BB status(`0x6001108c`)=0のspurious，②EIP_STATUS(0x600C2110)は毎回sticky-OR=0
（＝線1は毎回確実にdeassertされ即再アサート，真の再トリガ），③enableビット
（bit10+bit16）から出得るのは最大でも~113回のはずが3.8M回発火——は，
上書き仮説（source8の割込みがsource5用handlerに配送され続ける）で**過不足なく
再現できる**：source5用handler（`r_bt_bb_isr`/`r_bt_bb_isr_hack`）が走る度に
`0x6001108c`を読めば当然status=0（本当に発火しているのはsource8側であり
BB eventレジスタに原因bitは無い）；handlerは`r_plf_funcs_p[11](5)`でsource5の
CPU側EOIだけを毎回確実に叩く（③の「deassertは効くが即再発火」と一致）；
しかしsource8自身のstatus/clearには一切触れないため，本当の発火元
（source8＝RWBLE）は放置されたまま連続assertを続ける。C3側の調査記録は
この同じデータを「深いRF/BB層でBBハードウェアが連続assertしている」
（1434-1438行）と読んだが，**上書き仮説はそれより1階層浅い場所（ソフトウェアの
割込みルーティング）で同じ観測を全て説明できる**——これは既踏の再調査ではなく，
既存データに対する新しい・より単純な説明を持つ**未検証の空白**である。

### 2.5 C3とS3の割込み配線の違い（適用時の設計調整）

- **S3（Xtensa）**：CPU割込み番号がハードウェア的にレベル固定（0-10は概ね
  レベル1固定，レベル3を使うには専用のCPU割込み番号23・27を新設する必要が
  あった）。
- **C3（RISC-V, INTMTX）**：`bt_shim.c`に既に`BT_INTMTX_PRI_REG(n)`（404行）が
  存在し，**優先度はCPU番号ではなくレジスタ書込みで可変**。S3のような
  「専用CPU番号の新設」は不要——2本目のCPU割込み線（例：線2）へ配線し
  優先度レジスタを設定するだけで足りる可能性が高い（S3より軽い対応）。
- **既存インフラの確認**：`asp3/target/esp32c3_espidf/wifi/esp_shim.cfg`
  103行に`DEF_INH(2, { TA_NULL, esp_shim_inthdr_2 })`が**既に存在**し，
  この`.cfg`は`esp_bt.cmake`経由でBTビルドにも取り込まれる（WiFi専用ではない）。
  ⇒ **カーネルコンフィグ変更なしで，`bt_shim.c`側のロジック変更だけで
  2本目のCPU割込み線を使える**——実装コストはさらに低い。
- `flags`引数無視（bt_shim.c 417行）についても，S3の副次発見（Level3要求）と
  同型のリスクがあるが，2.5節の通りC3はレジスタ可変優先度のため軽微。

---

## 3. 本セッションの新知見の適用

### 3.1 APM/TEE（PMS/esp_memprot/SENSITIVE系）の有無——**机上確認：C3/S3世代には存在しない**

`hal/components/soc/`を確認したところ，**`esp32c3`・`esp32s3`ディレクトリ配下には
APM/PMS/TEE関連ヘッダが一切存在しない**（`apm`/`pms`/`tee`/`memprot`をキーワードに
grepしても0件）。一方，APM機構を実装した`esp32c5`・`esp32c6`・`esp32c61`・`esp32s31`
配下には`hp_apm_reg.h`・`lp_apm_reg.h`・`cpu_apm_reg.h`等が確認できる。
⇒ **C5/C6で確定した「APM/TEE未初期化でモデムのバスマスタアクセスが遮断される」
という機構は，C3/S3にはハードウェア自体が存在しないため適用不可**（「未初期化」
ではなく「そもそも無い」）。これは「Direct Bootが省略した」話ではなく世代差であり，
C3のBTストームの説明にはならない。

この判定は，C3のWiFiが実際に正常動作している事実（`docs/wifi-scan-c3-crash.md`，
scan/DHCP実績あり）とも整合する：BT_BB(0x60011xxx)とWiFi用MAC/BB(emi.c関連の
0x60031xxx)はC3では同一モデムHW（時分割共有無線）の一部であり，もしAPM相当の
バスマスタ遮断機構が存在してモデムのHPメモリアクセスを止めるなら，WiFiの
TX/RXも同様に死ぬはずである。WiFiが機能している以上，モデムのバスマスタ経路
自体は開通しており，**BTストームの原因は「バスアクセス遮断」系ではなく，
2章で特定した割込みルーティング（source取り違え）系である蓋然性がさらに
高まる**。

### 3.2 クロスカーネル・ハンドオフのC3 BLE版設計

C5/C6で最強のツールだった「stockブート→ASP3ジャンプ」ハンドオフ手法は，C3 BLEでは
**より軽量な代替**が既に用意されている：`docs/bt-shim.md`の「基準機A」＝
NuttX-C3-BLE参照バイナリ（`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c3ble/
nuttx/nuttx.bin`，2026-07-13時点でファイル存在を確認済み）が既に board A
（`60:55:F9:57:C9:88`）向けに用意されており，NuttXは自動advertisingを起動する
参照実装として何度も使われてきた（1440-1445行・1452-1483行等）。優先すべきは
フルのstock→ASP3ジャンプ・ハンドオフではなく，**S3が使った手法（CPU割込み線の
発火数カウンタ＋JTAG単発halt読み，または既存のRTC STORE計装）をそのまま流用し，
NuttX（正常side）とASP3（storm側）で`esp_intr_alloc`相当の呼出し回数・source値の
**時系列**（現状のような「最後の値だけ」ではなく）を比較する**設計に絞るのが
費用対効果が高い。フルハンドオフはstorm根絶後，D-2b以降（接続確立等）の
比較調査で必要になった時点で導入すれば十分。

### 3.3 ブート列欠落ファミリー監査リスト（`docs/c5c6-lessons-for-s31.md` §1・§4）のC3 BLE適用

§4のチェックリストをC3 BLE文脈で機械的に当てはめると：

| # | 項目 | C3 BLEでの状況 |
|---|---|---|
| 1 | APM/TEE | 3.1節の通り**該当ハードウェアなし＝N/A** |
| 2 | TX/RX 2×2 | **実施済み**（`docs/bt-shim.md` 1804-1821行，storm下でTX電波なしを確認）。RXは未測定（advさえ出ない現状ではRX側は後回しで妥当） |
| 3 | CPU実周波数実測 | BT単体では未実施だが，WiFi側で同一`hardware_init_hook`を使い回しており（`docs/s3-bt-intr-source-overwrite-fix-for-c3.md` 187行「target_kernel_impl.c 90-124行」既に一致確認済み），BT固有の疑いは薄い＝優先度低 |
| 4 | PLL較正プロファイル | 同上，WiFi側で実績のある経路を共有＝優先度低 |
| 5 | クロック/電源ゲート（ICG等） | `docs/bt-shim.md`で**emi.c:164真因として既に発見・修正済み**（`esp_shim_bt_clock_init`，255-330行）。BT_BB自体のクロックも(1)(i)実験で反証済み（1380-1386行） |
| 6 | WDTキー/PMA/PMP/ROMグローバル | WiFi側で解決済みの資産を共有。BT固有の再監査は優先度低 |
| 7 | 割込み系（CLIC mil固着等） | **C3はCLICチップではない**（RISC-V標準割込みコントローラ＋INTMTX）。ただし**割込み"配送先"の齟齬という同系統の問題が2章のバグそのもの**——項目7は「CLIC mil固着」ではなく「INTMTXルーティングの多重登録上書き」という**C3版の同型問題**として読み替えられる |

⇒ このチェックリストを機械的に流した結果，**未消化・有望なのは実質2章のバグ
（項目7のC3版）のみ**。他の項目は既に反証済みか，該当ハードウェアなしか，
WiFi側の実績で優先度が低い。これは「storm根本は割込みルーティング系のみが
未踏」という2.1-2.4節の判定と整合する。

### 3.4 ホストBLEアダプタ（hci0）でのover-the-air観測＝2×2判定のBLE版

ホスト側`bluetoothctl`（`/usr/bin/bluetoothctl`，動作確認済み）は`docs/bt-shim.md`
1804-1821行で既に「storm下でASP3のadvは一切検出されない・NuttXは検出される」
という2×2判定の**TX軸**を確定済み。RX軸（ASP3側でscanner役をやらせて外部
advertiserが見えるか）は未実施。3章の再開計画では，2章の修正適用後の
**回帰確認**として同じ`bluetoothctl scan le`手順をそのまま再利用する
（新規ツール開発不要）。

---

## 4. 再開実行計画（優先順位付きラウンド列）

### ラウンド1（最優先・低コスト・非破壊）：呼出し回数計装＋S3パターンの修正を同一パッチで投入

2.4節の通り，修正自体は「呼出しが1回でも2回でも安全」（1回だけなら配列の
1要素目だけが使われ，現行動作と完全に同じ）という設計にできるため，
**診断（呼出し回数カウンタ）と修正（配列化＋2本目CPU線）を分けず，1パッチに
まとめてよい**（`docs/c5c6-lessons-for-s31.md` §3.5「1実験1機構」の精神には，
今回は「機構」が単一（=INTMTXルーティング）なので反しない）：

1. **事前予測**：`esp_intr_alloc`は2回（source8→source5）呼ばれる。現行実装は
   2回目が1回目を上書きするため，source8向けの割込みがsource5 handlerに配送され，
   status=0 spuriousストームが発生している。修正（配列化＋線分離）で
   ストームは消滅し，`LE Set Advertising Enable`(0x200a)のcomplete到達→
   `ble_gap_adv_start`が戻る→over-the-airでadvが観測される，まで到達すると予測する。

   **診断と修正を同一パッチにまとめる根拠**（`memory/feedback_hardware_investigation_rigor.md`
   の「前提を確認してから注入」原則との整合）：単に「機構が単一だから」ではなく，
   **無修正・ストーム有りのベースラインは既に`docs/bt-shim.md`に確定記録済み**
   （1364-1368行，~100万〜390万/秒）であるため，1回のビルドで(a)呼出し回数
   （前提の直接証拠）と(b)ストーム消滅の有無（結果）を，この既存ベースラインに
   対して同時に測定でき，追加のビルド往復を挟むコストを払わずに前提と結果の
   両方が手に入る。慎重を期すなら，まず計装のみ（配列化なし）でカウンタだけ
   確認する最小ラウンドを先に挟む選択肢もあるが，それを既定にはしない
   （下記5.でread-back検証を必須にすることでリスクは十分下がる）。
   **注入の着弾確認（read-back，必須）**：修正後，(i) 2個目のスロットに
   実際に`source=8`が記録されたことをRTC計装で確認する，(ii) 2個目のCPU線の
   INTMTXマップレジスタ（`BT_INTMTX_BASE_ADDR + 8*4`）に実際に線番号2が
   書き込まれたことを事後読みで確認する——「書いたつもり」の偽陰性
   （`docs/c5c6-lessons-for-s31.md` §3.5既出）を避けるため，配線が本当に
   反映されたことを見てから，ストーム消滅の有無を評価する。
2. **実装**（`asp3/target/esp32c3_espidf/bt/bt_shim.c` 407-477行のみ変更，
   `.cfg`変更不要——2.5節の通り線2は既にDEF_INH済み）：
   - `bt_intr_handle`を`bt_intr_slot[BT_INTR_MAX_SLOT]`（S3同様2要素で足りる
     見込み）の配列化
   - `esp_intr_alloc`呼出し順で空きスロットに`source`を記録し，1個目→線1，
     2個目→線2へ配線（`BT_INTMTX_PRI_REG`もスロットごとに設定）
   - `esp_intr_free/enable/disable`を，渡された`handle`（配列要素へのポインタ）
     からスロット番号を逆算してper-handle操作に変更（現行は固定線1決め打ち）
   - 診断計装として，各`esp_intr_alloc`呼出しでインクリメントする静的カウンタを
     生存確認済みのRTC STOREレジスタ（0xB8/0xC0/0xC4のうち，現行diagが使って
     いないもの——現行は0xC0/0xC4をBBステータス計装で使用中のため，例えば
     未使用の0x50-5c(STORE0-3)系から1本選ぶ）へ記録し，source値も呼出し順で
     別レジスタへ記録（1回目・2回目を区別できる形。現行の「上書きのみ」設計を
     解消）
3. **判定基準**：
   - カウンタが2以上→S3と同型バグが**確定**（呼出し回数の直接証拠）
   - ストームカウント（既存の`esp_shim_int_count[1]`／新設する線2側カウンタ）が
     修正後に激減し，正常なBLE advイベント頻度（〜10-数百/秒オーダー，S3実績と
     同程度）に収まれば**修正成功**
   - `docs/bt-shim.md`のadv-return marker(0xC4系，実装が競合する場合は計装
     レジスタの再割当てが必要）が非0になり`g_adv_rc=0`へ遷移すれば**ホスト側
     ブロック解消**を確認
4. **反証時の扱い**：カウンタが1のまま（=1回しか呼ばれていない）なら，
   2.4節の状況証拠は覆り，このバグはC3では非該当と判明する。その場合は
   ラウンド2（分岐B）へ進む。
5. **実装前提の事前確認（済）**：線2の配送経路が「カウントするだけ」で
   終わっていないことを本計画書作成時に`esp_shim.c`のソースで確認済み——
   `shim_int_dispatch(int intno)`は`intno`非依存の共通関数で，線1・線2いずれも
   最終的に`shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg)`を呼ぶ
   （`esp_shim.c` 1018-1019行／1026-1028行の2経路とも同じ呼出しパターン）。
   `esp_shim_inthdr_2`（1035行）は`DEF_INH(2, ...)`（`esp_shim.cfg` 103行）で
   既にカーネルへ登録済み。⇒ **線2は「カウンタが増えるだけ」ではなく，
   登録したhandlerが実際にend-to-endで起動される経路が既に存在する**——
   ラウンド1の修正はこの既存経路へ2個目のsourceを配線するだけでよく，
   ディスパッチ機構自体の追加実装は不要。

### ラウンド2：分岐（ラウンド1の結果で経路が変わる）

**分岐A（ラウンド1でストーム消滅・advが観測された場合）**：
- over-the-air確認（3.4節の`bluetoothctl scan le`）で`ASP3-C3-BLE`を実際に検出。
- 非回帰確認：`bt_smoke_hw`（D-1）・`ble_host_smoke`のsync到達（D-2a）が
  引き続き0エラーで通ることを確認。
- `docs/bt-shim.md`のD-2b節を「保留解除・修正確定」として更新し，1352-1873行
  （深いRF/BB/blob層の調査ログ）に「原因は割込みルーティングであり，RF/AGC/coex
  層の調査は誤誘導の副産物だった」旨の訂正注記を追加（調査資産としては
  引き続き有用——計装コード`phy_cal_trace.c`等は温存）。
- 次段：3.1.26行で触れたGATTサーバのNULL関数ポインタ問題（`docs/bt-shim.md`
  1126-1133行，advには必須でないため後回しにされていた別の既知バグ）の調査へ
  進み，D-2c（GATT/接続確立）へ。

**分岐B（ラウンド1で呼出しが1回のみ・またはストーム不変の場合）**：
- 2章の判定が外れたことになるため，`docs/c5c6-lessons-for-s31.md` §3.5の
  「予測が外れたら注入に進まず棄却」原則に従い，このバグ仮説を明示的に
  棄却してdocs記録する。
- 次候補：`docs/bt-shim.md`が最終的に示唆していた「INTMTXレベルの割込み配線
  精査」（1870行）——具体的にはINTMTX優先度レジスタ以外の設定（トリガ種別・
  マスク）や，coex/RF-cal以外でまだ触れていない低レベルHW要因（例：BT_BB自体の
  ハード的な連続assert条件をROM/blob逆アセンブルで追う，(1)(g)-(k)で確立した
  `phy_cal_trace.c`パターンのBT_BB版への転用）。この経路はC6 deaf-RX同様の
  深いROM/blob層セッションが必要になる可能性が高く，コストは大きく上がる。

### ラウンド3（分岐Aの場合のみ・低優先）：flags/優先度要求の精査

S3の副次発見（`ESP_INTR_FLAG_LEVEL3`要求）に対応する呼出し引数がC3側でも
実際に何を要求しているか（`bt_shim.c` 417行の`(void) flags`を外して実測）を
確認し，2.5節の「レジスタ可変優先度で軽微」という予測を検証する。ストームが
既に解消していれば緊急性は低く，接続安定性・スループット等の品質向上項目として
扱う。

---

## 5. 必要リソース

| リソース | 所在・状態 |
|---|---|
| board B（ASP3被験機） | `60:55:F9:57:C2:60`（"ASP3-C3-BLE"），JTAG port 13334系。ストーム再現に使用してきた個体 |
| board A（NuttX参照機） | `60:55:F9:57:C9:88`（"NuttX"），JTAG port 13333系。★board Bと混同厳禁（`docs/bt-shim.md`に複数回の注意書きあり） |
| NuttX-C3-BLE参照バイナリ | `/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c3ble/nuttx/nuttx.bin`（460308 bytes，存在確認済み，2026-07-13） |
| ホストBLEアダプタ | `/usr/bin/bluetoothctl`（`hci0`，存在確認済み，`docs/bt-shim.md`で運用実績あり） |
| OpenOCD / ROM ELF | `/usr/local/bin/openocd`，`/home/honda/tools/espressif/tools/esp-rom-elfs`（存在確認済み） |
| esptool | `TOPPERS/tools/esptool-venv/bin/esptool`（RTC STORE事後読み用，MEMORY.md記載の罠：STORE5(0xBC)はusb-resetで上書きされるため使用不可，0xB8/0xC0/0xC4と0x50-5cのみ生存） |
| ビルド設定 | `-DESP32C3_BT=ON -DESP32C3_QEMU=OFF`（実機必須，QEMUモードは`csrw mie`で実機不正命令になる既知の罠） |
| 変更対象ファイル | `asp3/target/esp32c3_espidf/bt/bt_shim.c`（407-477行）のみ。`.cfg`/CMake変更は不要見込み（2.5節） |

---

## 6. 結論（サマリ）

**判定2（S3バグのC3実在性）**：`asp3/target/esp32c3_espidf/bt/bt_shim.c` 393-447行に，
S3が修正前に持っていたのと構造的に同一の「単一静的ハンドル＋固定CPU線への
無条件上書き配線」パターンが**実在する**ことをコード読解で確認した。C3の既存
観測（「source=5固定」）はこのバグの存在を否定する証拠にはならない（上書き
記録の原理的盲点，2.3節）。さらに，C3とS3が**文字通り同一の`bt.c`ソースファイル
＋同系列blob**（`lib_esp32c3_family`）を使っている一次情報（2.4節）は，バグ実在の
確度をさらに引き上げる。加えて，本セッションで確立したAPM/TEEという新知見は
C3/S3世代にはハードウェア自体が存在せず適用不可（3.1節）——これによって
「探索範囲を割込みルーティング系に絞ってよい」という結論が消去法でも補強される。

**推奨する第一手**：4章ラウンド1——`bt_shim.c`に(a)呼出し回数・source値の
時系列を記録する診断計装と(b)S3と同型の配列化修正を**同一の低リスクパッチ**として
実装し，board Bで一発検証する。修正は「1回呼出しでも2回呼出しでも安全」な
設計にできるため，診断と修正を分ける追加ラウンドを挟む必要はなく，即座に
白黒つけられる。予測が外れた場合（呼出しが実際には1回だった場合）に備えて
ラウンド2に分岐Bの代替計画（INTMTX配線・ROM/blob深掘り）も用意してある。

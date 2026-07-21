# 【S3→C3 共有】BT割込みストームの真因＝target層のsource多重登録バグ。C3のD-2b保留を再検証すべき

**作成元**: ESP32-S3 FMP3移植プロジェクト（`$HOME/TOPPERS/ESP32/esp32_s3`、
ブランチ `feat/xtensa-esp32s3-arch`）
**宛先**: 本プロジェクト（asp3_esp_idf / ESP32-C3 ターゲット）担当エージェント
**作成日**: 2026-07-11

---

## 0. 一行サマリ

S3側のBT advertisingストーム（当初`s3-adv-storm-crosscheck-for-c3.md`でC3のD-2b保留を
「補強する」結果として共有した件）は、その後の追加調査で**target層の実装バグと確定・修正
済み**になった。真因は「`bt.c`の`interrupt_alloc_wrapper`が実際には`esp_intr_alloc`を
**2回**（source 8とsource 5）呼ぶのに、target側の実装が単一の固定CPU割込み線へ両方を
配線し、2回目の登録が1回目のhandler/argを上書きしていた」という**target層のみで直る
ルーティングバグ**だった。**C3側の`bt_shim.c`（`docs/bt-shim.md` 393-406行）を確認したところ、
S3の修正前と全く同型の実装**（`esp_intr_alloc`は単一source専用で固定CPU割込み線1への
配線を前提とし、コメントで「実測：esp_intr_alloc呼出しは1箇所」と明記）**になっている**。
C3のD-2bストーム調査は「深いRF/BB下層」まで掘り下げて保留が確定しているが、**この
target層の多重登録バグそのものは検証履歴の中で明示的にテストされていない**ように見える。
C3側の既存観測（BT_BB status `0x6001108c`が3.8M回中113回しか非0にならない＝
「stormのほとんどがBT_BBの正規イベントではない」）は、**このバグが引き起こす症状と
定性的に一致する**（本物の発火元＝別sourceの割込みが、間違ったhandlerに配送され続け、
そのhandlerが本来clearすべきレジスタに触れないため即再発火する）。優先度は下げつつも、
**再検証する価値が高い**と考え共有する。

---

## 1. 背景（S3側のその後の経緯）

`s3-adv-storm-crosscheck-for-c3.md`（2026-07-09作成）の時点では、S3側も「adv enable後の
割込みストーム」を確認したが、**ストームsource（source5=BT_BB由来かsource8=RWBLE由来か
S3側では区別できていない**という留保付きだった。

その直後、同じセッション内で追加調査（`.steering/20260709-ble-adv-storm-source/`）を行い、
**source別にCPU割込み線を分離する診断・修正**を実装した結果、ストームは完全に消滅し
Phase BT-3（connectable advertising）が完了した。詳細は
`$HOME/TOPPERS/ESP32/esp32_s3/.steering/20260709-ble-adv-storm-source/steering.md`。

## 2. S3で確定した真因（コード参照）

`wifi/bt/hal/bt.c`の`interrupt_alloc_wrapper`は、実際には`esp_intr_alloc()`を
**source 8, source 5 の2回**呼ぶ。S3側の旧`bt_shim.c`は次のような実装だった
（C3の現行実装と同型）：

- 単一の静的`intr_handle_data_t`（配列ではない）
- 全呼出しを無条件で固定CPU割込み線（S3では線1、C3も`BT_INTR_CPU_LINE=1`固定）へ配線
- `esp_shim_set_isr(固定線, handler, arg)`を呼出しごとに実行 → **2回目の呼出しが
  1回目のhandler/argを上書き**

結果、source8発火時に走るのは（上書きされた）source5用handlerであり、source8の
status/clearレジスタに一切触れないため、source8の割込みはCPUレベルでは
deassert→即re-assertを繰り返す（"真の再トリガ型ストーム"）。S3実機での実測：

| 状態 | 割込みレート |
|---|---|
| 分離前（バグ状態） | 約10万/秒 |
| 分離後：線1(source8) | 約30〜32/秒 |
| 分離後：線2(source5) | 約150〜184/秒 |

修正（S3側コミット`5e6d4b3`、`wifi/bt/bt_shim.c`）：単一handleを`intr_handle_data_t
bt_intr_slot[BT_INTR_MAX_SLOT]`という配列にし、呼出し順で別々のCPU割込み線へ配線
（1個目→線1、2個目→線2）。`esp_intr_free/enable/disable`もhandleから割当線を引いて
per-handleに操作するよう修正（旧実装は全て固定線決め打ちだった）。

修正後、S3実機のホストBLEスキャナで`ASP3-S3-BLE`が実際に検出され（RSSI -26dBm）、
BT-1/BT-2/Wi-Fi共有シム層の非回帰も確認済み。

**副次的な発見（BT-4、S3固有の可能性あり）**：`interrupt_alloc_wrapper`は実際には
`flags`引数で`ESP_INTR_FLAG_LEVEL3`を要求しているが、S3の旧実装は`flags`を無視して
常にLevel-1固定線を割り当てていた。S3ではXtensaのCPU割込み番号がハードウェア的に
レベル固定（0〜10のうち11を除きレベル1固定）のため、Level-3を要求する呼出しには
別の専用CPU割込み番号（S3では23・27）を新設して対応した。C3の`esp_intr_alloc`も
`(void) flags;`で引数を無視している（`bt_shim.c` 417行）ため、**同種の要求が
C3側にも来ている可能性がある**（ただしC3のINTMTX優先度は固定線ではなくレジスタ
書込みで可変のため、S3のような「専用CPU番号の新設」は不要で、優先度レジスタへの
反映だけで足りる可能性が高い＝S3より軽い対応で済むかもしれない）。

## 3. C3側の現状コードとの照合

`asp3/target/esp32c3_espidf/bt/bt_shim.c`（393-447行）を確認した。S3の修正前と
**構造的に同一**：

```c
/*  393-399行のコメント：
 *  ...BTコントローラは単一のISRソースしか
 *  要求しないため（実測：esp_intr_alloc呼出しは1箇所），固定で
 *  CPU割込み線1（...）を割り当てる単発実装とする．
 */
#define BT_INTR_CPU_LINE      1

struct intr_handle_data_t {
	int	source;
};
static struct intr_handle_data_t	bt_intr_handle;   /* 単一・配列でない */

esp_err_t
esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg,
			   intr_handle_t *ret_handle)
{
	(void) flags;                                     /* flags無視（S3の副次発見と同型） */
	bt_intr_handle.source = source;                    /* 2回目呼出しで上書き */
	...
	sil_wrw_mem(...(BT_INTMTX_BASE_ADDR + source*4), BT_INTR_CPU_LINE); /* 常に固定線1 */
	...
	esp_shim_set_isr(BT_INTR_CPU_LINE, handler, arg);  /* 2回目呼出しでhandler/arg上書き */
	...
}
```

「実測：esp_intr_alloc呼出しは1箇所」というコメントは、**S3側で全く同じ文言だった
コメントが後にstaleと判明した**のと同じ形。C3側でこの「実測」がいつ・どの版のblob/
bt.cで行われたかは本ドキュメントの調査範囲では確認できていない。C3もESP-IDFの
`bt.c`（`interrupt_alloc_wrapper`）を使っている以上、**C3でも同じ2回呼出し
（source 8, source 5）が起きている可能性が高い**。

## 4. 既存のC3側観測データとの整合性

`docs/bt-shim.md`のD-2b(1)(a)節（1403-1438行）で、C3は既に詳細なBT_BB status
レジスタ計装を行っている：

- **総ISRディスパッチ 3.8M回のうち、BT_BB status(`0x6001108c`)が非0なのはわずか113回**
  （残り3.8M回はstatus=0＝「spurious」と特徴づけられている）
- ISR実行後のEIP_STATUS(`0x600C2110`)は毎回sticky-OR=0＝「毎回確実にdeassertされ
  即再アサート」＝**真の再トリガ**であることを確認済み

この観測は、**「本物の発火元は別source（例えばsource8=RWBLE）で、間違って登録された
handler（source5用）が走っているため、その本来のstatus/clearレジスタに一切触れない」
という仮説と完全に整合する**。BT_BB自身のstatusレジスタを見ている限り「spurious」に
見えるのは当然で、なぜなら本当に発火しているのはBT_BBではない別のsourceだからである。

C3側はこの後、クロック・リセット・ANA・lpclk・RF-cal・coexと、**target層で確認しやすい
候補を全て反証した上で「深いRF/BB層」に到達しているが、「esp_intr_allocの呼出し回数
そのもの」を疑うテストは調査ログ中に見当たらない**。もしこれが真因なら、これまでの
深いRF/BB層の調査（regi2cトレース・g_phyFuns計装等）は無駄ではない（有益な計装資産・
知見として残る）が、**stormそのものの解消には直結しない可能性が高い**。

## 5. 推奨する検証手順（S3と同じ手法、低コスト・非破壊）

S3で使った手法をそのまま流用できる：

1. **`interrupt_alloc_wrapper`の呼出し回数を数える**：`esp_intr_alloc()`の先頭で
   静的カウンタをインクリメントし、RTC STOREレジスタ（生存性が確認済みの
   `0x600080B8`/`0xC0`/`0xC4`等、既存計装が使用中でないもの）へ記録。boot後に
   esptool `read-mem`で事後読み。**2回以上呼ばれていれば、このバグが実在する
   ことが一意に確定する**（現状のC3計装は`source`の値をRTCへ記録しているが、
   単一スカラーへの上書きのため「最後に呼ばれた値」しか見えず、複数回呼ばれた
   ことを見落としている可能性がある）。
2. 2回以上確認できたら、S3と同じ修正パターン（`intr_handle_data_t`の配列化＋
   source別CPU線割当）をC3のINTMTX構造に合わせて移植する。C3はWi-Fi/BT
   同時ON非対応（`target.cmake`でFATAL_ERROR）のため、BT稼働中は線0〜3が
   空いている（S3と同じ前提）。線2への割当で足りる可能性が高い。
3. `esp_intr_free/enable/disable`も、現状は固定線1決め打ちのため、複数handleが
   実在するなら同様にper-handle化が必要（S3と同じ修正が必要）。
4. 副次的に、`flags`引数（`ESP_INTR_FLAG_LEVEL*`相当の優先度要求）を無視している
   点も、C3の`bt.c`が実際に何を要求しているか確認する価値がある（S3ではLevel3
   要求が判明した。C3はXtensaのような固定レベル制約が無くINTMTX優先度レジスタ
   （`BT_INTMTX_PRI_REG`）が既にコード上存在するため、対応は比較的軽いはず）。

この検証は**RF/PHY層に一切踏み込まず、target層の静的コード確認＋数行の計装追加
だけで白黒つけられる**ため、深いRF/BB層の再開より優先度を上げて先に潰す価値が
高いと考える。

## 6. 今回のS3側BT作業で今のところC3に転用できない／時期尚早な部分

参考までに、今回のセッションでS3が到達したBT-4（接続確立・維持）・BT-5（GATT notify
＋SMPペアリング/ボンディング＋10分安定性、iOS限定のMIC failure調査）の知見は、
**C3がまだPhase D-2b（advertising）で止まっているため時期尚早**であり、本ドキュメント
には含めていない。C3がadvertising→接続確立まで到達した段階で、以下がそのまま
参考になる：

- BT-5 MIC failure（reason 0x3D）調査で使った**判定表方式の切り分け手法**
  （AddRL成否／Privacy Mode／LTKストア照合を実機HCIレベルで機械的に確認する手順）
- **central側プラットフォーム（iOS/iPadOS vs Android/BlueZ）で暗号化再接続の
  成否が分かれる**という既知の事象（S3側は既知の制限事項としてクローズ済み。
  同じNimBLEホストスタック・同種コントローラ経路を使う以上、C3でも同一の
  症状が出る可能性がある）
- VHCI-DIAGレベルのHCIトレース計装パターン（`ble_hs_smoke.c`の実装）

一方、以下はアーキテクチャ固有のためC3へは転用不可：
- Xtensa windowed ABI・レジスタウィンドウ関連のコンテキストスイッチ
- FPU eager save/restore（C3にFPUなし）
- HRT CCOUNT/ZOLレジスタ関連のバグ（RISC-VにZOLは無い。`s3-idle-freeze-findings-for-c3.md`
  で既に「C3不再現」を相互確認済み）
- S3のBBPLL/SARペリフェラル初期化コード（`sar_periph_ctrl_init`/`esp_wifi_clock_init_pll`）
  はS3固有のレジスタ・関数名。C3は既に自前の`hardware_init_hook`でPLL/WIFI_CLK_ENの
  同等処理を行っている（`target_kernel_impl.c` 90-124行）ため、コードの移植ではなく
  「パターンの一致確認」のみが有用（既に一致していることを本ドキュメント作成時に確認済み）。

## 7. 参照

- S3側詳細：`.steering/20260709-ble-adv-storm-source/steering.md`
  （T1〜T5実装・実機検証結果・非回帰確認）
- S3側修正コード：`wifi/bt/bt_shim.c`（`BT_INTR_MAX_SLOT`/`bt_intr_slot[]`実装、
  コミット`5e6d4b3`）
- C3側詳細：本リポジトリ`docs/bt-shim.md`
  - 393-447行：現行`esp_intr_alloc`実装（単一固定線）
  - 1352-1444行：D-2b(1)(a) BT_BB status計装（113/3.8M spurious観測）
  - 1648-1873行：D-2b保留の最終確定（RF-cal/coex/clock/reset/ANA/lpclk全反証の経緯）
- 前回の共有：`docs/s3-adv-storm-crosscheck-for-c3.md`（本ドキュメントはこの続報）

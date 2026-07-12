# C5 CLIC割込み出口バグ修正（実施28 synthetic mret）精査レビュー

- 日付: 2026-07-13
- 対象: `asp3/arch/riscv_gcc/esp32c5/chip_support.S` の `irc_begin_int` synthetic mret（コミット 4fc078e）
- 精査方法: 完全読み取り専用のコード読解（ASP3共通部 submodule・FMP3 trunk・arm_m_gcc 両実装）＋独立レビュアー（サブエージェント）による RISC-V CLIC 仕様（riscv-fast-interrupt Smclic 仕様書 v0.9系）原文との突き合わせ。以下で「仕様」と言う場合は同仕様書を指す
- 依頼事項: (1) FMP3に同じ問題があるか (2) arm_m_gcc（NVIC）の確立済み解法との対称性

---

## 1. バグの構造（要約）

CLIC（ESP32-C5, mnlbits=3）はHWが割込み受付時に`mintstatus.mil`を自動昇格し**mretでのみ降格**するが、TOPPERS RISC-V共通部（`asp3/asp3_core/arch/riscv_gcc/common/core_support.S`）の割込み出口には mret 非経由の2経路——(a) wake-from-idle の `j dispatcher_0`（`core_support.S:556`）、(b) 遅延ディスパッチで切替先タスクの `TCB_pc`（`dispatch_r`/`start_r`）へ `jr`（`:617`→`:294`）——があり、そこを通ると mil が固着して全割込みが永久ブロックされる。
修正は割込み入口 `irc_begin_int`（mintthreshソフト昇格の直後）での **synthetic mret**（`chip_support.S:189-194`：mepc=継続ラベル・MPIEクリア・mret）により mil を受付前レベル（=mcause.MPIL、常に0）へ即時降格し、以後の優先度保護は mintthresh（CSR 0x347）に一本化するもの。出口経路は無改変。

---

## 2. FMP3判定：**同型バグは「存在した」が、既に実機で顕在化し恒久修正済み**（現trunkに問題なし）

### 2.1 同型経路の有無

FMP3の RISC-V 共通部（`~/TOPPERS/FMP3/work/fmp3_trunk/arch/riscv_gcc/common/core_support.S`）は ASP3 と同型の出口構造を持つが、**問題の2経路は既に「素の mret リダイレクト」へ変換済み**である：

| 経路 | ASP3共通部（未対策・mret非経由） | FMP3共通部（対策済み） |
|---|---|---|
| wake-from-idle | `j dispatcher_0`（core_support.S:556） | **mepc=dispatcher_0 を設定し素の mret**（fmp3 core_support.S:603-616。コメントに「arm_m_gcc の return_to_idle と同設計」と明記） |
| 割込み出口の遅延ディスパッチ | `j dispatcher`（core_support.S:617） | **mepc=dispatcher を設定し素の mret**（fmp3 core_support.S:677-689） |
| 通常復帰（プリエンプトされたタスク／ネスト） | mret（core_support.S:731） | mret（fmp3 core_support.S:803）——変更なし |

加えて CLIC チップ依存部（esp32p4）の `irc_end_int` が**全出口共通で `mcause.mpil=0` を強制**し（`fmp3_trunk/arch/riscv_gcc/esp32p4/chip_support.S:166-180`、マスク `0xFF00FFFF`）、後続の mret（経路を問わず）が一律に mil=0 へ降格する。設計文書は `fmp3_trunk/arch/riscv_gcc/doc/clic_design.md`（§トラップ機構, 77-127行）・`doc/clic_memo.md:30-42`。

タスクコンテキストからの `dispatch()`（fmp3 core_support.S:147-161→dispatch_r）と CPU例外経路の `j dispatcher_0`（:1047）・`j dispatcher`（:1107）は mret 非経由のまま残っているが、前者は mil=0 のタスク文脈で走り、後者は CLIC が例外で mil を昇格しない（horizontal trap）ため、いずれも正しく対象外（ASP3実施28の分析と同じ整理）。

### 2.2 顕在／潜在の別

**顕在だった（既に修正済み）**。FMP3は現状 CLIC ターゲットを持つ：**ESP32-P4**（`arch/riscv_gcc/esp32p4/`＋`target/m5stamp_esp32p4_gcc/`。mtvec.MODE=3ハードワイヤ＝CLICモード固定）。このターゲットで同型バグが **実機の `mtrans2`/`raster2` の lost-wakeup／livelock として顕在化**し、根治された経緯が `clic_design.md:79-84`（「mil を 0 へ落とさないと，以降の割込み（他コアからの dispatch IPI=msip やタイマ）がマスクされたまま起床不能…実機 mtrans2/raster2 が露呈．根治の経緯は 50a1a88 / 24d63fa」）と `esp32p4/mtrans2_lost_wakeup_analysis.md` に記録されている。PolarFire（PLIC）のみだった時代には潜在だった、という時系列も ASP3/C5 とまったく同じ。

### 2.3 FMP3特有の事情（マルチプロセッサ）による経路の増減

- **IPI（プロセッサ間割込み）**: CLINT msip を CLIC 線3 として受け、`irc_begin_int` の `do_msi` 分岐（esp32p4 chip_support.S:136-138）で `msi_handler`（`common/msi_ipi.c`）を通常の割込みハンドラとして呼ぶ。**出口は core_int_entry の共通3分岐を共有し、新しい mret 非経由出口は増えない**。ただし「他コアを起床させる dispatch IPI」はまさに wake-from-idle／遅延ディスパッチ経路を高頻度で踏むため、**SMP では本バグの露出頻度が桁違いに高い**（ASP3/C5 では wifi_scan の最初の wake-from-idle＝72ms でようやく1回踏んだ経路を、FMP3 は毎ディスパッチ要求で踏む）。FMP3 が先にこのバグへ到達していたのは構造的必然。
- **マイグレーション**: `dispatch_and_migrate`／`exit_and_migrate`（fmp3 core_support.S:318-372）はタスク文脈（mil=0）で走り、移住先での再開は上記の（修正済み）ディスパッチ経路に合流する。新規経路は増えない。
- **アイドル**: FMP3はアイドル専用スタック（dispatcher_1, :279-281）を持つが、mil 管理上の差はない（`clic_design.md:117-122`「アイドルは CLIC 固有処理を要さない」）。

### 2.4 結論（依頼1への回答）

**FMP3 に「同じ問題」は現存しない**。同一の構造バグを ESP32-P4（CLIC）で先に踏み、**ASP3/C5 とは別解（出口正規化型）で恒久修正済み**である。逆に言えば、FMP3 の修正履歴は「TOPPERSのRISC-V共通部＋CLICで本バグが必然的に発生する」という実施28の判断の独立な再確認になっている。

---

## 3. arm_m_gcc（NVIC）の解法の構造

NVIC では例外の活性状態（active bit）と実効優先度をHWが管理し、**例外リターン（EXC_RETURN による復帰）でのみ解除**される（ソフトから活性ビットは書けない）。TOPPERS arm_m ポートの解法は「**ディスパッチを例外コンテキスト（PendSV/SVC）に閉じ込め、例外の出口は必ず正規の例外リターンを踏む。正規フレームが無い行き先へは偽の例外フレームを合成して返る**」：

ASP3版（`asp3/asp3_core/arch/arm_m_gcc/common/core_support.S`）:

1. **割込みからの遅延ディスパッチ**: 割込み出口で直接ディスパッチせず、`request_dispatch_retint()` が ICSR.PENDSVSET で **PendSV（最低優先度例外）をペンド**する（`core_kernel_impl.h:608-616`）。実際の切替は `pendsv_handler`（core_support.S:147-255）内。
2. **pendsv_handler の3出口が全て正規の例外リターン**:
   - 切替先が割込みでプリエンプトされたタスク（TCB_pc が EXC_RETURN 値）→ そのまま `bx r1`＝例外リターン（:241）。
   - **wake-from-idle** → `return_to_idle`（:242-255）：**idle_loop を指す偽の例外フレームを PSP 上に合成**し、`ldr pc, exc_return_const` で例外リターン。
   - 自発的待ち・新規起動タスク（TCB_pc が Thread 番地）→ `return_to_thread`（:256-292）：**TCB_pc を指す偽フレームを合成**して例外リターン。
3. **タスク文脈からの dispatch()**: `do_dispatch`→`dispatcher_0`（:434-496）。切替先の TCB_pc が EXC_RETURN 値（＝PendSV 内で中断されたタスク）なら **`svc #0`（:496）で例外に入り直し**、`svc_handler`（:321-365）が `bx r1` で例外リターンする。「例外リターンが必要なら、まず例外に入る」という徹底ぶりで、活性状態の帳尻が常に合う。
4. ハンドラ実行中・切替区間の優先度保護は **basepri（ソフトマスク）** が担う（pendsv_handler 入口 :151-152 の `msr basepri, IIPM_LOCK`、復帰直前の faultmask+basepri 操作 :224-241）。NVIC の実行優先度は「PendSV が最低優先度」という一点でのみ利用。

FMP3版（`fmp3_trunk/arch/arm_m_gcc/common/core_support.S:148-305, 368-`）は同一構造（p_runtsk 等が per-PCB になり、ASP3 側にある SAFEG/MVE 拡張が無い程度の差）。**割込み出口規律に関して ASP3/FMP3 の arm_m に本質差はない**。

---

## 4. 対称性の判定：C5修正は arm_m パターンの「同型」ではなく「双対」——だが達成する不変量は同一

### 4.1 判定

- arm_m は依頼文の分類でいう **(b)：ディスパッチを PendSV に遅延させ、例外リターンを常に正規経路で通す**（偽フレーム合成込みの「出口正規化」型）。これは NVIC がそれ以外を許さないため（活性状態はEXC_RETURNでのみ解除・ソフト書込み不可）の**強制された選択**である。
- C5 の synthetic mret は **(ii)：例外リターン相当を入口で早期に済ませてからディスパッチに臨む**（「入口解除」型）。CLIC では mret が**ハンドラ途中でも実行できる通常命令**であるという、NVIC に無い自由度を突いた解であり、arm_m では原理的に採れない。
- よって両者は**手段としては双対（入口で解除 vs 出口を正規化）だが、達成する不変量は同一**：「HW の in-service 状態（NVIC active / CLIC mil）はハンドラの入口・出口で完結して外へ漏れず、実行中の優先度保護はソフトマスク（basepri / mintthresh）が全面的に担う」。arm_m も実際に basepri を門番にしており（§3-4）、この意味論レベルでは C5 修正は arm_m と対称である。FMP3 の CLIC 修正（出口の素mretリダイレクト＋mcause.mpil=0）は同じ不変量の「出口正規化」側の実装で、FMP3 自身が「arm_m_gcc の return_to_idle と同設計」とコメントしている（fmp3 core_support.S:604-605）——つまり **3つの実装（arm_m／FMP3-CLIC／ASP3-C5）は同一不変量の3通りの実現**と整理できる。
- なお設計空間には第4の点として、**(δ) mcause をコンテキストの一部として保存・復元する spec-canonical な出口正規化**（CLIC 仕様 §9.1 が想定する形。Espressif 自身の `hal/components/riscv/vectors.S:524-531` が `csrw mcause` で実装）がある。TOPPERS 系3実装はいずれも mcause を保存しない設計で、それが正当である根拠が各方式の不変量（C5: mpil≡0、FMP3: 出口で mpil 強制0）になっている（§5-#4 の隠れ結合を参照）。

### 4.2 arm_m 型（出口正規化）の C5 対称形は採れるか・採るべきか

| 代替設計 | 内容 | 評価 |
|---|---|---|
| (i) FMP3方式の移植 | 共通部の2出口（:556, :617）を素の mret リダイレクトへ変換＋`irc_end_int` で mcause.mpil=0 | 意味論的に等価で実証済み（P4 SMP）。ただし **asp3_core submodule の共通部編集が必要＝本リポジトリの禁則①に抵触**。chip層で `OMIT_CORE_INT_ENTRY` を定義して core_int_entry 全体（約370行）を複製する回避策はあるが、5命令の入口追加に対して保守コストが見合わない |
| (ii) PendSV 模倣 | CLIC の最低レベルのエッジ線を `ras_int` 相当でソフトトリガし、遅延ディスパッチをその割込みへ集約 | CLIC の機能上は可能（IPビットのソフトセット可）だが、共通部の遅延ディスパッチ構造（core_int_entry_3）の全面再設計＋ディスパッチレイテンシ増（追加のトラップ往復）＋テールチェイン等のHW支援がRISC-Vに無いため arm_m のような利点が出ない。**過剰** |
| 現行 (synthetic mret) | 入口5命令＋mret 1回 | chip層に完結・出口網羅性が構造的（そもそも昇格状態がハンドラ本体に存在しないため、**将来共通部に新しい出口が増えても自動的に安全**）。mintthresh 昇格が先行するため保護の空白窓もない（mret 前は MIE=0） |

**判定：現行修正が本リポジトリの制約下（submodule 編集禁止）での最適解であり、arm_m 型代替への改修は不要**。FMP3 方式は「共通部を編集できる側」（asp3_core 本体・FMP3 trunk）でのみ意味を持つ選択肢（§6）。

---

## 5. C5修正の弱点候補の批判的検証

コード内コメント（chip_support.S:151-187）の机上検証を、共通部・CLIC仕様と突き合わせて再検証した。

| # | 弱点候補 | 検証結果 | 判定 |
|---|---|---|---|
| 1 | mepc破壊 | 共通部は入口で mepc をスタック保存済み（core_support.S:414-415）で、`jal irc_begin_int`（:465）は保存後。正規出口はスタック値を `csrw mepc`（:680-681）で復元してから mret。**問題なし** | 問題なし |
| 2 | MPIE/MIE整合 | 保存済み mstatus（スタック上、MIE=0/MPIE=1）は synthetic mret に影響されない。mret後 MIE=0（事前MPIEクリアの効果）・MPIE=1 だが、MPIE はトラップ受付時に HW が MIE から再設定するため、`is_kernel_exception_asm`（common/core_asm.inc:152-156、生mstatus.MPIE参照）はトラップ時点の正しい値を見る。**問題なし** | 問題なし |
| 3 | MPP連鎖 | mret は MPP をサポート最小特権へ書き換える（U-modeサポート時はU。ESP32-C5 は soc_caps の APM/TEE・hal `vectors.S` の U-mode CLIC CSR（`CSR_UINTSTATUS` 等）から U-mode 実装がほぼ確実で、mret 後 MPP=U となる期間は実在する）。ただし (a) `dispatch_r`/`start_r` 経路は MPP を参照しない、(b) 正規出口は保存 mstatus（MPP=M）を `csrw mstatus`（:682-683）で復元してから mret、(c) ネスト受付時は HW が MPP←M を再設定——のため**実害なし**。補足: mret の「M 以外へ戻る場合の MPRV←0」も MPP=M で実行するため不発（無関係） | 問題なし |
| 4 | ネスト時の MPIL 連鎖 | mil が昇格している期間は「HW受付〜synthetic mret」の数命令のみで、この間 MIE=0 のためネスト不可（仕様上、同一特権への割込み受付条件はグローバル割込み許可であり、M より上位特権は無い＝この窓での受付は仕様レベルで否定できる）。ゆえに**あらゆる受付時点で mil=0 → mcause.MPIL≡0** となり、どの出口 mret がどの（上書きされた）mcause を読んでも常に0で一貫。コメントの主張どおり。**ただし隠れ結合を1点明記すべき**：本ポートは mcause をスタックに保存・復元しない（core_int_entry_5 は mepc/mstatus のみ）。CLIC 仕様 §9.1 は本来 xepc とともに xcause の保存を想定し、Espressif の `vectors.S` も mcause を復元する。**mcause 無保存が正当なのは「mpil 恒等0」不変量があるからこそ**であり、synthetic mret を外す将来変更は「mcause 保存欠如」という第2のバグを同時に顕在化させる | 問題なし（結合の明文化を推奨） |
| 5 | 例外（同期トラップ）経路との相互作用 | 仕様原文 "Horizontal synchronous exception traps … are serviced with the same interrupt level" ＝例外は mil を昇格しない。例外トラップでも HW は mcause.mpil に現 mil を書く（CLIC モードでは例外も CLIC 形式 mcause）が、本方式では現 mil≡0 のため値は不変。例外出口の mret（core_support.S:1139）も mret 非経由の例外出口（:964）も mil=0 のまま無害。`irc_begin_exc` 無変更は正当 | 問題なし |
| 6 | コメントの「この区間はMIE=0のためプリエンプト不可＝mcause/mepcは書き換わらない」 | **厳密には割込みにのみ正しい**。同期例外は MIE=0 でもトラップし mcause/mepc を上書きする。帰結は当初想定より重い：窓内（HW受付〜synthetic mret）でフォールトすると、例外の mcause.mpil ← その時点の mil＝**昇格値L** が書かれ、例外復帰後の synthetic mret が mil←L を拾い**固着バグが静かに再発し得る**（窓前半なら :131-134 の INTNO 取得も破壊）。緩和要因：窓内は MPIE=0 のため `is_kernel_exception_asm` はカーネル管理外例外＝panic 経路に入り、静かな進行はしにくい。実害評価：窓の命令列（la/csrw/li/csrc/lbu）でフォールトし得るのは CLIC MMIO への lbu（`0x20801000`、M-mode・PMPロックなしのDirect Boot＝デフォルト許可）のみで、毎割込みで実行され実機長時間動作済み＝現実的なフォールト源なし。**実害なし。ただし「この窓にフォールトし得る命令を追加してはならない」を保守上の禁止事項としてコメントに明記すべき**（#4 の不変量も同じ無フォールト仮定に載る） | 問題なし（保守制約の明記を推奨） |
| 7 | NMI相当 | RISC-V CLIC に標準 NMI はなく、C5 ポートはカーネル管理外割込み（TOPPERS_SUPPORT_NONKERNELINT）も NMI ハンドリングも持たない（chip層に該当コードなし）。**非該当** | 非該当 |
| 8 | 診断リングの劣化（新規指摘） | `ESP32C5_CLIC_DEBUG_RING` の begin 側記録（chip_support.S:196-223）が synthetic mret の**後**に置かれているため、記録される mepc は常に `irc_begin_int_demote`、mintstatus.mil も常に降格後の値となる。実施28で凍結特定に使った「割込まれた地点」「受付直前のmil」という2つの主要情報が今後のダンプでは失われる。**正しさには無関係だが、診断としては記録ブロックを mret の前へ移すべき**（mcause.MPIL の記録は仕様 "The xret instruction does not modify the xcause.xpil field" により mret 後も有効なまま） | 要改善（診断のみ・機能影響なし） |
| 9 | 性能 | 入口あたり +5命令＋mret 1回（パイプラインフラッシュ）。C6 の THRESH メモリアクセス数回と同オーダーで許容範囲 | 問題なし |
| 10 | mnxti との将来非互換（新規指摘） | CLIC の `mnxti` CSR（テールチェイン高速化）は書込み副作用で **mil を再昇格**する（受付判定自体は mpil と thresh で行われるため選択は正しく動くが）。FMP3 方式は全出口が「mpil強制0＋mret」を通るため自己回復するのに対し、**C5 方式は mret 非経由出口が残存する設計のため、将来 mnxti を導入すると mil 固着が復活する**。現状 mnxti は未使用のため実害なしだが、本方式の不変量の適用限界として明記すべき | 問題なし（制約の明文化を推奨） |

補足（レビュアーによる仕様確認）：mcause.mpp/mpie が mstatus の当該フィールドの**ミラー（エイリアス）であることは仕様に明文**があり（"reading or writing mstatus fields mpp/mpie in mcause is equivalent to …"）、`csrc mstatus, MPIE` だけで CLIC モード mret の pie 復元とも整合する——#2 の結論はこの規定が根拠。ミラーの実装忠実性は最終的にHW依存だが、破れていれば CPU ロック区間への割込み侵入で即座に壊れるところ、実施28以降の実機動作（85秒・1800割込み超の継続配送）で実効的に検証済み。

**総合：修正の正しさに問題は見つからなかった**。要改善は #8（診断リングの記録位置、既定無効の診断コードのみ）と、コメントへの明文化3点（#6 窓へのフォールト命令追加禁止・#4 mcause無保存との隠れ結合・#10 mnxti非互換）で、いずれも現行機能に影響しない。

---

## 6. 推奨

1. **現行修正のまま行く**。synthetic mret は本リポジトリの制約（asp3_core submodule 編集禁止）下で最適であるだけでなく、「昇格状態をハンドラ本体に持ち込まない」ことで出口網羅性が構造的に保証される点は FMP3 方式（全出口で irc_end_int が呼ばれる規約に依存）より頑健ですらある。arm_m 型（出口正規化）への改修は不要（§4.2）。
2. **軽微改善（任意・次に chip_support.S を触る機会で可）**：(a) `ESP32C5_CLIC_DEBUG_RING` の begin 側記録を synthetic mret の前へ移動（受付時 mepc・昇格中 mil の記録を回復）。(b) コメントへの明文化3点——「プリエンプト不可」の主語を割込みに限定し、**「HW受付〜synthetic mret の窓にフォールトし得る命令を追加してはならない（窓内例外は mcause.mpil に昇格値を残し固着が再発する）」**という保守禁止事項（§5-#6）、**「本ポートが mcause を保存/復元しない設計は mpil≡0 不変量に依存する（synthetic mret を外すなら mcause 保存が必要になる）」**という隠れ結合（§5-#4）、**「mnxti 導入とは非互換（mil を再昇格するため）」**という適用限界（§5-#10）。
3. **asp3_core 本体への一般化（将来の C61/H4/P4 等 CLIC チップ対応）**：2案とも意味論的に等価で、
   - **A案（推奨）**: C5 の synthetic mret を「CLIC チップ依存部の標準パターン」として文書化（PORTING_GUIDE / 新設 clic 実装メモ）。共通部無変更で済み、asp3_core の禁則①（kernel/共通部の乖離最小化）と整合。実装は各 CLIC チップの `irc_begin_int` に閉じる。文書には §5 の3制約（窓へのフォールト命令追加禁止・mcause 無保存との結合・mnxti 非互換）を併記すること。
   - B案: FMP3 の実証済み設計（共通部2出口の素 mret リダイレクト＋chip 層 `irc_end_int` の mcause.mpil=0。fmp3 core_support.S:603-616/677-689 と esp32p4 chip_support.S:166-180 がそのまま参照実装）を上流共通部へ取り込む。素の mret＋MPIEクリアは PLIC 系ターゲットにも無害（FMP3 は PolarFire で回帰済み）だが、共通部の変更と `MSTATUS_MPP_M` マクロ追加（FMP3 riscv.h:54 相当。ASP3 common/riscv.h には現状無い）を要する。将来 FMP3↔ASP3 でコードを行き来させる場合や、**mnxti によるテールチェイン最適化を将来視野に入れる場合は B案（出口正規化）の方が耐性がある**（§5-#10）。単体では A案が低コスト。
   - 参考: チップ実装間の差異として、標準 CLIC CSR の実装は個体差が大きい（C5=mintthresh CSR 0x347・mintstatus 0xFB1 が有効／P4=mintthresh CSR 不正・mintstatus 0x346・thresh はメモリマップトのみ、`fmp3 doc/clic_memo.md:40-42`）。一般化文書には「CSR 番号・thresh 実装形態はチップごとに実機確認必須」と明記すべき。
4. **test_porting への項目追加を asp3_core 側へ申し送り**（実施28 §8-4 と同じ結論を本精査でも支持）：現行6項目は「割込みからの wake-from-idle／遅延ディスパッチ」を構造的に一度も踏まず、本バグ系を検出できない。dly_tsk 待ちからのタイマ起床（wake-from-idle）と、割込みハンドラからの高優先度タスク起床（遅延ディスパッチ）を踏む項目の追加が、将来の CLIC ポートの一発検出に直結する。FMP3 側では mtrans2/raster2 が canary として機能した実績がある。

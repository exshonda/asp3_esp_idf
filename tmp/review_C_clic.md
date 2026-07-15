# ★C レビュー：CLIC出口正規化（asp3_core a888d48 + 本体 7ab2590）

対象コミット：
- asp3_core `a888d48`（`arch/riscv_gcc/common/{riscv.h,core_support.S}`）
- 本体 `7ab2590`（`asp3/arch/riscv_gcc/esp32c5/chip_support.S`）
- 参考：`docs/c5-clic-exit-fix-review.md`（2026-07-13付、実施28 synthetic mretの精査レポート。今回の変更の設計判断の前段）、`docs/c5-bringup.md` 実施27/28

読解の範囲：read-only。実機・QEMU実行なし。CLIC仕様（Smclic）の細目（mret時のmintstatus.mil復元がinterrupt/exceptionいずれのtrapでも一律に走ること、exceptionはmilを昇格させないこと）は `docs/c5-clic-exit-fix-review.md` §5-#5・#4 に引用された仕様原文（"Horizontal synchronous exception traps … are serviced with the same interrupt level"、mcause.mpp/mpieがmstatusのミラーである旨の明文）に依拠。この仕様原文そのものはリポジトリ内引用の又引きであり、一次ソース（riscv-fast-interrupt/Smclic仕様書）を本レビューでは直接参照していない（反証条件として明記）。

---

## 問い1：mret非経由2経路の出口正規化は正しいか

**結論：同意（正しい）。**

### 根拠
- `asp3/asp3_core/arch/riscv_gcc/common/core_support.S:556-577`（idle復帰．core_int_entry_2内）と `core_support.S:638-656`（遅延ディスパッチ．core_int_entry_3内）が，従来の`j dispatcher_0` / `j dispatcher`を
  ```
  la t0, <label>; csrw mepc, t0
  li t0, MSTATUS_MPP_M; csrs mstatus, t0
  li t0, MSTATUS_MPIE;  csrc mstatus, t0
  mret
  ```
  へ変換している（差分は `git -C asp3/asp3_core show a888d48` で確認）。
- この2経路がCLICで問題になる理由（mnlbits>0のHWが割込み受付時にmintstatus.milを自動昇格し，mretでしか降格できない）は `docs/c5-bringup.md` 実施27/28で実機JTAG計測により確定済み（`asp3/arch/riscv_gcc/esp32c5/chip_support.S:59-83`のファイル冒頭コメントに要約）。
- 出口正規化後は，`irc_end_int`（後述・問い2）がどの出口分岐よりも必ず先に1回呼ばれ（`core_support.S:519-520`のcore_int_entry_2ラベル直後が`jal irc_end_int`），そこでmcause.mpilを0に強制するため，以後どの出口が実行するmretも「mcause.MPILから復元」でmil=0へ帰着する。**「(a)(b)以外の経路（dispatch_r/start_r．タスク文脈からの自発的dispatch()）はmretを経由しない」という実施28の分析は変わらないが，(a)(b)自身がmret化されたことで，そこに到達する前に当該トラップのmilは既に0で片付いている**ため，どの経路へ続いても安全（`chip_support.S:88-104`）。
- **ネスト時の健全性を実装追跡で確認**：ネストした割込みBがA処理中に割り込んでも，A用のa0/a1（割込み番号・旧mintthresh）はスタック上に保存済み（`core_support.S:476-478`保存／`:512-513`復元）でBの影響を受けない。mcauseはCSRとして共有されるためBの実行後は「Bが最後に書いた値（mpil=0強制済み）」が残るが，Aの`irc_end_int`は**無条件に**mcause.mpil=0へ再設定してからAの出口mretへ進むため（`chip_support.S:288-291`），Bによる汚染があっても実害はない（mretが参照するのはbits[23:16]のみで，Aの`irc_end_int`実行後の値が常に0に確定しているため）。この点は依頼書に明示された問いではないが，「ネスト」の観点で重要な健全性根拠として確認した。
- **例外系（core_exc_entry_2の`j dispatcher`：`core_support.S:1063`／core_exc_entryの`j dispatcher_0`：`core_support.S:1003`）は今回未変換のまま**——これは見落としではなく正しい非対称性：CLIC仕様上，例外（同期トラップ）はmintstatus.milを昇格させない（`docs/c5-clic-exit-fix-review.md` §5-#5「例外は mil を昇格しない」）。したがって例外専用の出口がmret非経由でも，milは元々変化していないため固着は起きない。

### 反証条件
- 上記の「例外はmilを昇格させない」という前提はCLIC仕様の一次文書で未確認（引用の又引き）。もしC5の実CLIC実装が例外でもmilを昇格させる非標準仕様であれば，`core_exc_entry_2`/`core_exc_entry`のidle復帰・遅延ディスパッチ相当（`core_support.S:1003`,`:1063`）にも同型の固着リスクが残る。実機JTAGで「カーネル管理外例外ハンドラ内でCPU例外を故意に多発させ，かつp_runtskがNULLになる（IDLE中に例外が起きる）状況」を作ってmintstatus.milの推移を確認するテストが反証実験になる。
- FMP3参照実装（`fmp3_trunk/core_support.S:603-616,677-689`）は本リポジトリに実体がなく，コミットメッセージ経由の引用のみ。行番号の裏取りはできていない。

---

## 問い2：mpilクリアのマスク`0xFF00FFFF`

**結論：同意（正しい）。マスクは意図通りbits[23:16]のみを狙い撃ちし，隣接フィールドを破壊しない。**

### 根拠
- `asp3/arch/riscv_gcc/esp32c5/chip_support.S:288-291`：
  ```
  csrr t0, mcause
  li   t1, 0xFF00FFFF
  and  t0, t0, t1
  csrw mcause, t0
  ```
- ビット分解：`0xFF00FFFF` = `1111_1111  0000_0000  1111_1111_1111_1111`。バイト単位で見ると bits[31:24]=0xFF（保持）・bits[23:16]=0x00（クリア＝ここがmpil）・bits[15:0]=0xFFFF（保持＝bit31のInterruptは実は31番目単独ビットでbits[31:24]内、bits[11:0]のexception code、および両者の間の予約ビットも保持）。**mask自体はコメントが主張する「bit31=Interrupt保持・bits23:16=mpilクリア・bits11:0=exccode保持」を正確に実現している**（実測ビット演算で確認．算術ミスなし）。
- CLIC拡張mcauseの他のフィールド（bit30=minhv，bits29:28=mpp，bit27=mpieに相当するミラーフィールドが存在する版のCLIC仕様もある）もbits[31:24]内に収まるため，このマスクで保持される。取りこぼし・競合は確認できない。
- 「mret時のmpil→mintstatus.milへの復元」との相互作用：mretはmcauseの他フィールド（Interrupt・exccode等）を一切参照せず，bits[23:16]のみを読む（`docs/c5-clic-exit-fix-review.md` §5-#4・#8のレビュー時点の理解と整合）。したがって他フィールドを保持したままbits[23:16]だけを0にするこの実装は，mretの副作用と過不足なく整合する。
- **irc_end_intが全出口より必ず先に1回呼ばれる**という前提（問い1で確認済み）により，マスク処理と3つの出口分岐（通常mret復帰=core_int_entry_5／idle復帰／遅延ディスパッチ）は，どれが選ばれても同一のmcause値を参照する。競合・取りこぼしなし。

### 反証条件
- 実機C5でCLIC_INT_CONFIG.mnlbitsが不定形（ドキュメント記載の「冷間ブートでも3固定．ROM/HW設定」以外の値）を取る個体差がある場合，mpilのビット幅（NLBITS由来）が仕様想定と食い違う可能性がある。実機で`csrr mcause`の生値をJTAGダンプし，マスク前後のビットパターンを確認する反証実験が有効（`docs/c5-bringup.md`実施28のリング計装が流用できる）。
- FMP3esp32p4のマスクが同一という主張（`chip_support.S:283-286`のコメント）は外部リポジトリの又引きで直接確認不可。

---

## 問い3：MPP/MPIEの扱い

**結論：同意（正しい）。ネスト中の割込み許可状態を取りこぼす経路は見当たらない。**

### 根拠
- `riscv.h:54`：`MSTATUS_MPP_M = 0x1800`（bits[12:11]）。2ビットフィールドを`csrs`（OR）で0b11にする実装は，**元の値が0b00/01/10/11のいずれであってもOR演算だけで確実に0b11になる**（2bitフィールド全体をORで埋めるため，クリア無しでも数学的に正しい）。副作用なし。
- `MSTATUS_MPIE`（`riscv.h:53`=0x80）を`csrc`（AND NOT）で個別クリア。他のmstatusフィールド（FS等）に影響しない単一ビット操作。
- mret後の副作用：RISC-V特権仕様どおりmret実行で`MIE←MPIE`（クリア済みなので0）、かつ`MPIE←1`（無条件）。前者がdispatcher_0/dispatcherの契約（「CPUロック状態で呼び出される」．`core_support.S:277-281`のコメント）を満たし，後者はコメントが指摘する通り次のトラップ受付時にHWがMIEから再設定するため無害（旧synthetic mret設計の同型分析を踏襲．`docs/c5-clic-exit-fix-review.md` §5-#2で「問題なし」判定済み）。
- **ネスト中に「本来復元すべき割込み許可」を落とすか**：dispatcher_0/dispatcherへ到達する時点でMIEは既に0（非スプリアス経路は`lock_cpu_asm`＝`core_support.S:510`で明示的に0化済み，スプリアス経路はトラップ入口でHWが0にした状態を保ったまま到達）。したがって本パッチのMPIEクリアは「既に0のはずの状態を明示的に再確認する」冗長操作であり，新たに割込み許可を落とす経路は追加されていない。実際の割込み再許可は，dispatcher_0/dispatcherの先（`ret_int_r`の`ret_int_prepare_unlock_cpu_asm`，`start_r`の`unlock_cpu_asm`，`dispatcher_2`アイドルループの`unlock_cpu_asm`）が担い，これらは本パッチで無変更。

### 反証条件
- MPP=Mを強制する変更が，将来U-modeタスクサポート（`docs/c5-clic-exit-fix-review.md` §5-#3で言及されたAPM/TEEのU-mode CLIC CSR）を導入した際に，U-mode復帰を必要とする出口（現状は存在しない）に誤って適用されると復帰特権を破壊する。現状のASP3はM-mode専用カーネルのため非該当だが，将来のメモリ保護／ユーザモード機能追加時に要再検証。

---

## 問い4：QEMU 8/8と実機の差（mie有無）を踏まえた見落とし

**結論：見落としなし。ただし「QEMU 8/8」という言葉が指す対象が誤解を招きやすい点を指摘。**

### 根拠
- CLAUDE.mdの「QEMUと実機で割込み配送の仕組みが異なる（mie必須／mie非実装）」は，**C3固有の`mie`/`mip` CSR非実装**の話（`asp3/asp3_core/docs/dev/esp-idf-integration.md:218`，`docs/bt-shim.md:999-1006`）であり，本パッチが操作する`mstatus`（標準CSR，全RISC-V実装で存在必須）や`mcause`/`mintthresh`（CLIC標準CSR，C5で実機JTAG確認済み＝`chip_support.S:59-70`）とは別のレジスタである。本パッチのコード自体はC3の「mie CSR非実装」問題に触れない。
- 本パッチの検証構成を整理すると：
  - asp3_core側`a888d48`：**QEMU esp32c3（非CLIC）8/8・実機ESP32-C3/ESP32-C6（非CLIC）8/8・POSIX linux 8/8**＝これは「共通部変更による非CLIC回帰がないこと」の確認であり，**CLIC固有の正しさそのものは検証していない**（対象チップが非CLICのため）。
  - 本体側`7ab2590`：**実機ESP32-C5（board=C5#2）でtest_porting 8/8＋wifi_scan 20 AP完走**＝これがCLIC固有の正しさ（idle復帰・遅延ディスパッチの2経路が実際に固着しないこと）を検証した唯一の証跡。**C5のQEMUは存在しない**（Espressif fork QEMUはesp32c6同様esp32c5マシン未実装）。
  - したがって「QEMU 8/8」はCLIC経路を一度も検証しておらず，**CLIC部分の検証は実機のみ**という構図になっている。これは本パッチにとってはむしろ適切（QEMUがCLICのmil自動昇格を忠実にエミュレートしているかどうか自体が未確認であり，QEMUで緑でも実機のCLIC固有バグを見逃す可能性が高い．実際，旧来のtest_porting 6項目はQEMU/実機とも緑のままC5実機でハングを起こしていた＝実施27/28の教訓そのもの）。
- 「実機のみmintthresh経路が効く」というレビュー依頼の懸念については：mintthreshはC5固有のCSR（0x347）で，C3/C6（PLIC/PLIC_MX）には存在しない。本パッチの共通部変更（mepc/mstatus操作＋mret）はmintthreshに一切触れないため，mintthreshの実装差は無関係。C5実機でのtest_porting 8/8とwifi_scan実証（`7ab2590`コミットメッセージ）がmintthresh経路込みで確認済み。

### 反証条件
- 「実機C5#2」1台のみの検証（`7ab2590`コミットメッセージに「C5#1はboard latch中で無関係な既知症状」と明記）。個体差（NLBITS実装差・CLIC_INT_CONFIGのHW初期値差）がある場合，C5#1や別ロットのC5での再検証が反証実験になる。
- QEMUがCLIC（mnlbits>0）を将来サポートした場合，QEMU上でも本パッチのmret経路が期待通り動くかは未検証（現状スキップ以外の選択肢がない）。

---

## 問い5：共通部変更のC3/C6/他arch非回帰

**結論：同意（正しい）。ガードは`#ifdef`ではなく「チップ層のirc_end_intがmcauseを書き換えない」という契約で担保されており，これは実装を確認した4チップ（C3・C6・polarfire_soc・rp2350）全てで成立している。**

### 根拠
- 変更箇所（`core_support.S:556-577`,`:638-656`）は`#ifdef USE_RISCV_DIRECT_TRAP`等のCLIC専用ガードで囲われておらず，**全RISC-Vターゲット共通で無条件に実行される**コードになっている。安全性の根拠は「非CLICチップのirc_end_intはmcauseに触れない」という主張（コミットメッセージ・`riscv.h`コメント）。
- 実装を直接確認：
  - `asp3/asp3_core/arch/riscv_gcc/esp32c3/chip_support.S`のirc_end_int（111-112行目付近）：mcauseへの`csrw`なし（`csrr`での読取りはirc_begin_int側のみ）。
  - `asp3/asp3_core/arch/riscv_gcc/esp32c6/chip_support.S`のirc_end_int：同上，mcause書込みなし。
  - `asp3/asp3_core/arch/riscv_gcc/polarfire_soc/chip_support.S:238-272`のirc_end_int：mcause書込みなし。
  - `asp3/asp3_core/arch/riscv_gcc/rp2350/chip_support.S:151-170`のirc_end_int：mcause書込みなし。
  - grep結果（`csrw mcause`）はC5以外のどのチップ層にも出現しない＝確認済み。
- これら非CLICチップでは，そもそも`mintstatus`/`mnlbits`CSRを実装しない（標準RISC-V基本モード＝mtvec.MODE 0/1）ため，mretが「mintstatus.mil←mcause.mpil」という復元動作自体を行わない（CLIC拡張＝Smclicを実装しないハートにはこの意味論が存在しない）。したがって新しく追加された`mepc`書換え＋`mstatus`のMPP/MPIE操作＋`mret`は，**「plain jumpの代替として，mepc/mstatus.MIEを明示的に設定し直すだけの操作」**に縮退し，機能的にはjump前と同じ「CPUロック状態でdispatcher(_0)へ分岐する」という結果を再現する。実際，`a888d48`のQEMU esp32c3・実機ESP32-C3/C6・POSIX linuxでの8/8 pass（非CLIC回帰確認）がこれを裏付ける。
- `USE_RISCV_DIRECT_TRAP`は例外/割込み弁別の分岐（`core_int_entry`冒頭）にのみ関与し，今回変更した2経路（core_int_entry_2/3の出口）はこのマクロの影響を受けない箇所。ガードの対象範囲を正しく理解した上での確認。

### 反証条件
- 将来，非CLICチップ側で`irc_end_int`が独自にmcauseへ書き込む変更が入った場合（例えば診断目的でmcauseをスタブする等），この「mcauseに触れない」契約が暗黙のドキュメント外契約であるため，レビューを経ずに壊される余地がある。**明文化されたコメント／アサーションが共通部側に存在しない**（`riscv.h`/`core_support.S`のコメントのみで，コンパイル時にチェックする機構はない）ことは軽微な保守リスクとして指摘できる。

---

## 総合まとめ

| 問い | 結論 | 一言 |
|---|---|---|
| 1 | 同意 | idle復帰／遅延ディスパッチのmret化は正しい．例外系が非対称（未変換）なのはCLIC仕様上妥当（例外はmilを昇格しない）だが一次仕様書での裏取りは未実施 |
| 2 | 同意 | `0xFF00FFFF`はbits[23:16]（mpil）のみを正確にクリアし，bit31(Interrupt)・bits11:0(exccode)・その他拡張フィールドを保持．ネストしたmcause汚染があっても各割込み自身のirc_end_intが無条件に0へ再設定するため実害なし |
| 3 | 同意 | MPP=M（OR）・MPIEクリア（AND NOT）とも他ビットを破壊しない精密なビット操作．到達時点で既にMIE=0（lock_cpu_asm済み）のため，取りこぼしは発生しない |
| 4 | 見落としなし（要注記） | 「QEMU 8/8」はCLIC経路を検証しておらず（C5用QEMUが存在しない），CLIC固有の正しさは実機C5（1台）でのみ検証．これは設計上避けられず，かつ実機検証の方が本質的に必要な検証（QEMUでは元々このバグ自体が再現しない） |
| 5 | 同意 | 変更は無条件ガードだが，C3/C6/polarfire_soc/rp2350の4チップ全てでirc_end_intがmcauseに触れないことをソース確認．非CLIC回帰なしの主張は裏付けられる．ただし「mcauseに触れない」契約が非明文のため将来の保守リスクは残る |

**mpilマスク・MPIEクリアの安全性判定（依頼の重点）**：いずれも安全と判定する。マスクはビット単位で意図通りに機能し，ネストや隣接フィールドとの競合は見当たらない。MPIEクリアもnew/old双方の状態遷移を追跡した限り，割込み許可の取りこぼしは生じない。最大の留意点は「実機C5#2 1台・QEMU非対応」という検証範囲の狭さであり，これは実装の誤りではなく，CLIC実機という検証資源の制約に起因する。

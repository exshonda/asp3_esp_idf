# トーン自己ループバックADCゼロ問題の解決計画（実施21改訂版）

> **【結果追記（実施21実施済み，2026-07-12）】**
> - **候補A＝棄却**。事前固定予測（stock bit31=0）が不成立：stockもbit31=1のまま較正完走・
>   27AP検出（独立2ブート実測）。真因＝本計画の一次情報（`hal/.../clk.c:239`）は
>   esp-hal-3rdpartyサブモジュール（別世代）のコードで，**実ビルドのIDF v6.1-beta1には
>   当該erratum対策が存在しない**（v6.1の唯一の使用箇所はesp_pm DFSの一時保護のみ）＝
>   両プラットフォームとも「無い」ので差分になり得ない。ルールどおり注入は実施せず。
> - **候補B＝差分実在・因果棄却**。前提（`CONFIG_ESP_ENABLE_PVT=y`・eFuse blk_version=3≧2）成立，
>   A/B差分（stock=PVT有効・動作中／ASP3=クロックゲート・全ゼロ）を各2ブートで確認。
>   しかしJTAG注入（+2.7s，txcap/iq_est窓被覆）と加算移植（較正前タイミング，
>   `esp_shim_pvt_init()`）の**両方でPVT実動作を読み戻し確認した上で症状不変**（done=0・
>   生ADC=0）。
> - 詳細・教訓（hal/とv6.1の世代差・新PCのJTAG罠3件）＝`docs/c5-bringup.md`実施21。
>   次段は「実施21改訂：PMU/LP全域スイープ」節（特に決定実験C＝PMU HP_ACTIVE 4レジスタ注入）へ。
>
> **【結果追記（実施22実施済み，2026-07-12）】**
> - **決定実験C＝新規差分2件（XPD_BIAS・PMU_POWER_PD_HPWIFI_CNTL）を発見・
>   mid-hang注入とbefore-PHY移植の両方で因果棄却**（DIG_POWER・HP_REGULATOR1は
>   差分なし，HP_REGULATOR0の差分は実施21のPVT軸で既に棄却済み）。XPD_BIAS
>   （HP_ACTIVEアナログバイアス生成器の起動ビット）はmid-hang注入だけでは
>   タイミング上不十分な懸念があったため，実施21の候補Bと同水準まで
>   before-PHY加算移植（`esp_shim_hpactive_bias_init()`）で再検証し，stock値との
>   完全一致を確認した上で症状不変を確定した。
> - **PMU/LP/MODEM1/MODEM_PWR0スイープ**：未踏査だった`LP_CLKRST`・`LP_AON`・
>   `LP_I2C_ANA_MST`・`LPPERI`・`LP_ANA`・`MODEM1`・`MODEM_PWR0`を一括ダンプ。
>   MODEM1/MODEM_PWR0は差分なし（全ゼロ域）。LP_ANA/LPPERIの表面上の差分は
>   フィールド解読の結果，BOD（brown-out detector）／電圧グリッチ検出器／
>   LPドメイン周辺クロックであり，WiFi RF/PHYバイアス経路と機序上無関係と判明し
>   机上棄却（JTAG注入は実施せず）。
> - **分岐計画ケース2の(1)（MODEM1/MODEM_PWR0未踏査の解消）は完了**。**(2)（電源系
>   初期化列の関数単位・段階的加算移植A/B）は未着手のまま**——`pmu_hp_system_init()`
>   のdigital/clock/retentionパラメータ・`pmu_lp_system_init()`・`esp_rtc_init()`の
>   今回扱っていない部分が残る。したがって現時点で「C6-genericな共通アナログ壁」と
>   結論するのは時期尚早（(2)を尽くしてから判断すべき）。
> - 詳細＝`docs/c5-bringup.md`実施22。
>
> **【結果追記（実施23実施済み，2026-07-13）】**
> - 分岐計画ケース2(2)「電源系初期化列の関数単位・段階的加算移植A/B」を実施。
>   `pmu_hp_system_init()`残余（`HP_ACTIVE.CK_POWER`＝BBPLL/BB-I2Cアナログ電源，
>   `SYSCLK`/`BACKUP`/`BACKUP_CLK`）と`pmu_lp_system_init()`（LP_ACTIVE
>   REGULATOR0＋LP_SLEEPバンク）を関数単位で加算移植・実行確認込みで**全て
>   因果棄却**（実施21〜23累計で11件目〜）。
> - `pmu_power_domain_force_default()`の未移植3ドメイン（`PD_TOP/HPAON/HPCPU`）
>   ＋LP側force（`PD_LPPERI`）は，このタイミング（WiFi init時点の後付け移植）で
>   書くとJTAG単発halt捕捉法のブート到達性が悪化する現象を検出し，
>   「悪化したら即revert」方針に従い`#if 0`で無効化・**未検証のまま保留**
>   （真のハングか捕捉タイミングのシフトかは未切り分け）。
> - `esp_rtc_init()`＝`pmu_init()`呼出しのみと確認。残る唯一の未検証部分＝
>   `esp_ocode_calib_init()`（bandgap o-code較正，regi2c `I2C_ULP`(0x61)経由）。
>   **差分自体はソースコード（stockは`set_ocode_by_efuse()`で`IR_FORCE_CODE=1`を
>   無条件force）とASP3側の信頼できるJTAG読み（`IR_FORCE_CODE=0`，2ブート再現）
>   から確定した**——ASP3は`pmu_init()`自体を呼ばないため未force。ただし
>   ASP3のOCODE実測値（0x65/0x68）は妥当な中間値でHW自動較正が機能している
>   ことを示し，「基準電圧回路は生きていて較正済み，基準点が違うだけ」という
>   機序であるため症状（ADC完全固定ゼロ）に対しては相対的に弱い候補と評価。
>   **因果検証は未実施**（`esp_rom_regi2c_write_mask`がC5では真のROM関数でなく
>   `hal/`のpatchesファイル実装であり，ASP3ビルドへの追加リンクというCMake
>   構造変更が必要なため，移植そのものを見送った）。なお簡易JTAG直接読みで
>   stock側の生値（`0xc2`）も取得を試みたが，3レジスタが不自然に同一値を
>   返しアーティファクトと判断し破棄——ただしdiffの有無自体はソース解析で
>   確定済みのため実害なし。
> - **「分岐計画ケース2(2)を尽くした」とは言い切れない状態で終了**——3件の
>   残余（PD_TOP等4ドメイン・ocode・意識的に対象外としたHP_MODEM/HP_SLEEPバンク）
>   が残る。C6-generic結論はこのラウンドでは書かず，ユーザーに(a)ocode正式実装の
>   一段階限定ラウンド／(b)ここまでの11件の反証を材料に凍結してEspressif問い合わせへ
>   進む，の2案を申し送った。
> - 詳細＝`docs/c5-bringup.md`実施23。
>
> **【結果追記（実施24実施済み，2026-07-13）——分岐計画ケース2(2)消化完了】**
> - ★本ラウンドはC5#1のnative USB-JTAGが未接続でJTAG使用不能な環境だった
>   （同居機はJTAG可能だが別調査★FROZENのC6ボードで不接触）。
>   `target_fput_log`直接出力＋`phy_get_pkdet_data`への`--wrap`フック
>   （`syslog`/logtaskはPHY較正の無限リトライループ突入後に出力が止まると
>   本ラウンドで新規判明したための代替）で，JTAG非依存の機械確認・ライブ
>   観測手法を確立して遂行した。
> - **ocode force（before-PHY移植）＝因果棄却**。eFuse値`0x65`の強制書込みを
>   regi2c読み戻しで機械確認（独立5ブート）した上で症状不変。ただし強制値が
>   ASP3の自己較正値（`0x65`/`0x68`）とほぼ同一のため**弱い試験**（bandgap
>   基準電圧が大きくズレるケースは未検証）。
> - **PD_TOP/HPAON/HPCPU/LPPERI force解除＝因果棄却**。`#if 1`化しUARTで
>   機械確認（stock値`0x00000000`と一致，独立5ブート）した上で症状不変・
>   UART可視のブート劣化なし。ただし実施23が検出したのは「JTAG単発halt
>   捕捉法がPHYハングループへ到達できない」という**JTAG介入限定の現象**で
>   あり，本ラウンドはJTAGが使えず**この現象自体は再検証できていない**——
>   次回JTAG環境での主要調査ツールを壊さないよう**安全側で`#if 0`のまま維持**。
> - 累計13件の個別候補が因果棄却済み——**分岐計画ケース2(2)は消化完了**。
>   C6-genericの総括・推奨2案（(a)Espressif問い合わせへ／(b)継続する場合の
>   残り手段）を`docs/c5-bringup.md`実施24に記載，**最終判断はユーザーに委ねる**。
> - 詳細＝`docs/c5-bringup.md`実施24。
>
> **【結果追記（実施25実施済み，2026-07-13）——未公開regi2c block(0x63/0x68/0x6b)
> 含む全8blockの0x00〜0x1Fフルスイープ，新規かつ未説明の差分ゼロ】**
> - 総括§6(b)「未公開regi2c blockの逆アセンブル・トレース」を実施。advisor
>   レビューで「host_idルーティング＋ANA_CONF1 RD_MASKの推測ベースの読取り
>   プロトコルは未検証」という致命的な穴を指摘され，実測の既知答え合わせ
>   （block=0x6b,reg=0x02は実施16で確定した恒久値`0x87`）でプロトコルを
>   先に検証してからスイープを実施（`host=1→I2C1_CTRL`＋block固有の
>   RD_MASKビット，未公開3blockぶんを新規実測で確定）。
> - 8block×0x00〜0x1F×host(0/1)×2採取点（`register_chipv7_phy`エントリ／
>   `phy_iq_est_enable_new`loop-top）を4-way比較。検出された5件の
>   プラットフォーム決定的候補は全て「CPU動作周波数差（ASP3=192MHz対
>   stock=240MHz，BBPLL/DIG_REGの較正値差として説明可能）」「実施24で
>   自ら`keep`したocode force shimによる既知差」「実施16 4b/4dの既知
>   再現（txcap停止・低信頼度ノイズ）」「無効host_idルーティングの
>   アーティファクト」のいずれかで説明でき，**新規の未説明差分はゼロ**。
> - stockのperturbation検証（スイープ挿入後も同一ブートが`Total APs
>   scanned`まで完走）済み。詳細＝`docs/c5-bringup.md`実施25。

対象：`docs/c5-bringup.md` 実施20時点の唯一の壁＝blob内トーン自己
ループバック測定の生ADCサンプル（`MODEM0+0x81C..0x828`）がASP3のみ
ハードゼロ（stockは同一個体・同一blob・同一条件で非ゼロ変動・完走）。

本計画は実施11〜20の全記録レビュー＋机上検証（レポート全文＝
`tmp/c5_review_jisshi21_plan.md`。実施11〜20の論理チェック結果は
「統一像は堅牢・致命的な穴なし」）に基づき、中断された実施21計画
（PMU/LPスイープ）を改訂する。**フルスイープより先に潰すべき
単発候補2件が新規に見つかった**ため、実施順を再構成した。

## 候補A（最優先・単一ビット・新規発見）：`PCR_FPGA_DEBUG_REG` bit31 ——modem root clock自動ゲーティングのバイパス未設定

**一次情報（halで検証済み）**：
- `hal/components/esp_system/port/soc/esp32c5/clk.c:236-239`：
  stockの`esp_perip_clk_init()`は冒頭で無条件に
  `clk_ll_soc_root_clk_auto_gating_bypass(true)`を呼ぶ。コメントは
  「**soc root clock source切替時の自動クロックゲーティングにより
  modem root clockが失われる**問題の対策（IDF-11064）」と明記。
- `hal/components/esp_hal_clock/esp32c5/include/hal/clk_tree_ll.h:435-444`：
  同関数の実体は`PCR_FPGA_DEBUG_REG`（`0x60096FF4`）の**bit31クリア**
  （`ESP_CHIP_REV_ABOVE(rev,1)`ガード付き＝eco2は対象）。
- `hal/components/soc/esp32c5/register/soc/pcr_reg.h:2599-2607`：
  同レジスタのPOR既定値は`0xFFFFFFFF`（＝bit31=1＝自動ゲーティング
  有効のまま）。
- ASP3側：`esp_wifi_adapter.c`の`esp_perip_clk_init()`相当の移植
  コメント（763-808行）は`modem_clock_select_lp_clock_source`系のみを
  移植しており、このビットへの言及・書込みはリポジトリ全体でgrep 0件。

**症状との整合**：下流のICG/クロック制御レジスタが全ビット一致
（実施20）でも、root clock自動ゲーティングはそれらより上流で
modemクロックを動的に止める。「制御系は健全・測定データだけ出ない」
「ASP3の方がstockより約1秒遅く測定点に到達する（実施20のsettling
観察）」のいずれとも矛盾しない。実施15/20の比較対象に`0x60096FF4`が
含まれていた記録は無い（未比較領域）。

**判別手順（安価・この順で）**：
1. 採取点（`phy_rfcal_txcap`エントリ等、実施19/20と同じbp）で
   stock/ASP3両方の`0x60096FF4`をJTAG読み。予測＝stock：bit31=0／
   ASP3：bit31=1。**予測が外れたら本候補を棄却して候補Bへ**（注入に
   進まない）。
2. 予測どおりなら、ASP3側でJTAGから`0x60096FF4`のbit31をクリアして
   トーン測定を再実行（クリーンブート＝UARTブリッジRTSリセット→
   ブート早期に注入。実施14の罠に注意）。生ADCサンプルが非ゼロに
   なれば因果確定。
3. 恒久修正＝移植層（`esp_wifi_adapter.c`のクロック初期化部か
   `target_kernel_impl.c`のhardware_init_hook）に
   bit31クリアを1件追加（chip revガードをhalと同条件で）。

## 候補B（次点・新規発見）：PVT自動dbias初期化の欠落

- IDF v6.1はC5で`CONFIG_ESP_ENABLE_PVT`が既定ONであり、stockは毎起動
  `pvt_auto_dbias_init`等（PVT＝process/voltage/temperature監視に
  よるdbias＝電源バイアス自動調整）を実行する。ASP3はgrep 0件。
- dbiasはアナログ供給電圧の実効値を変える＝「デジタル可視レジスタ
  一致でもアナログ測定だけ死ぬ」症状と整合しうる。
- 判別：stockビルドの`sdkconfig`で`CONFIG_ESP_ENABLE_PVT`実値を確認
  →`PVT_MONITOR`ブロック（reg_base.h参照）を両プラットフォームで
  A/B読み→差分があればJTAG注入（可能な範囲）または加算移植で因果検証。

## 実施21改訂：PMU/LP全域スイープ（候補A/Bで解決しない場合）

元計画（bringup末尾の再開メモ）に以下を追補する：

1. **スイープ対象の追加**：元のPMU・LP_ANA・LP_AON・LPPERI・PCRに
   加え、`LP_CLKRST`（隣接ブロックの抜け）・`PVT_MONITOR`・
   `LP_I2C_ANA_MST`・**`MODEM1`（`0x600AC000`）・`MODEM_PWR0`
   （`0x600AD000`）**（名前がmodem電源を示唆するのに実施11〜20で
   一度も触れられていない未踏査領域）。各ベース/サイズは
   `tmp/c5_review_jisshi21_plan.md`の表を使う。
2. **「readback一致≠有効状態一致」の罠を計画に組み込む**：
   `PMU_IMM_*`系にはWT（write-trigger・自己クリア）型ビットが実在し
   （`pmu_reg.h`で確認済み）、スナップショット比較では「トリガを
   発火させたか」が原理的に見えない。スイープ前にレベル保持型／
   ストローブ型を分類し、ストローブ型は「stockのpmu_init()/
   esp_rtc_init()が該当トリガを呼ぶか」のソース読解で決着させる
   （スイープ差分ゼロ＝電源状態同一、とは結論しない）。
3. 手順は元計画どおり：採取点2点（`phy_rfcal_txcap`エントリ＋
   `register_chipv7_phy`エントリ）×stock×2・ASP3×2の4-way→差分の
   意味解読（xpd/LDO/バイアス最優先）→JTAG注入で因果検証→
   移植層修正1件。

## 分岐計画（実施22以降）

- **差分あり**：実施13型（1回1機構：因果確認→恒久移植→反証）で
  1件ずつ潰す。複数差分をまとめて移植しない。
- **差分ゼロ**：「C6-generic（Direct Boot共通のアナログ壁）」と
  結論する**前に**、(1) MODEM1/MODEM_PWR0の未踏査が本当に埋まったか
  確認、(2) **stockの電源系初期化列の加算移植A/B**（`pmu_init()`＋
  `esp_rtc_init()`＋PVT init を関数単位で段階注入。C6実施34は
  「まとめて移植でハング多発」だったが、C5は同一個体の陽性対照・
  クリーンブート再現手順・JTAG捕捉環境が確立しており、1関数ずつ
  戻す/進めるの二分探索が可能）を実施してから停止・ユーザー判断へ。
  - **(1)＝実施22で完了**。**(2)＝実施23で主要部分（`pmu_hp_system_init()`の
    RF/BB近傍レジスタ・`pmu_lp_system_init()`全体）を消化し全て因果棄却**したが，
    `pmu_power_domain_force_default()`の残り3+1ドメイン（後付け移植のタイミングで
    書くとブート到達性が悪化したため意図的に未検証で保留）と
    `esp_ocode_calib_init()`（実装コストを理由に未着手）の**2件が未消化のまま
    残る**——「尽くした」とは言い切れないため，実施23はC6-generic結論を書かず
    ユーザー判断へ申し送った（詳細＝`docs/c5-bringup.md`実施23）。
  - **(2)の残り2件＝実施24で消化完了**。本ラウンドはJTAGが使用不能な環境
    （C5#1の native USB-JTAGが未接続。同居していたJTAG可能なボードは別調査
    ★FROZENのC6であり不接触）だったため，`target_fput_log`直接出力＋
    `phy_get_pkdet_data`への`--wrap`フックで**JTAG非依存の機械確認・ライブ観測
    手法**を新たに確立して遂行した。
    - `esp_ocode_calib_init()`のbefore-PHY移植（手動regi2cリプレイのshim化，
      hal/のROM regi2cパッチは読取り専用で参照のみ）：eFuse ocode値
      （`0x65`）の強制書込みをregi2c読み戻しで機械確認（独立5ブート再現）した
      上で症状不変——**因果棄却**。ただし強制値がASP3の自己較正値
      （`0x65`/`0x68`，実施23）とほぼ同一のため，**弱い試験**である点に注意
      （bandgap基準電圧が大きくズレるケースは検証できていない）。
    - `PMU_POWER_PD_TOP/HPAON/HPCPU/LPPERI_CNTL`のforce解除：`#if 1`にして
      UARTで機械確認（0x00000000＝stock一致，独立5ブート）した上で症状不変・
      UART可視のブート劣化なし——**因果棄却**。ただし実施23が検出した問題は
      「JTAG単発halt捕捉法がPHYハングループへ到達できない」という**JTAG介入時
      限定の現象**であり，本ラウンドはJTAGが使えず**この現象自体は再検証
      できていない**。次回JTAG環境での主要調査ツール（単発halt捕捉法）を
      壊さないよう，**安全側でコードは`#if 0`のまま維持**する。
    - 累計13件の個別候補が全て因果棄却——**「分岐計画ケース2(2)は消化完了」**。
      C6-genericの総括・推奨案は`docs/c5-bringup.md`実施24に記載，最終判断は
      ユーザーに委ねる。

## 運用注意（再開メモ第2版より）

- **C5#1のflash内容は不確定**（実施21中断の副作用）。着手時に必ず
  ASP3計装ビルドを書き直してから始める。
- C5#2（stock参照機）は読み取り比較のみ・書換え禁止。
- クリーンブート＝UARTブリッジRTSリセットのみ（JTAG reset halt／
  native USB-JTAGリセットはMODEM/PMUドメインを消さない、実施14）。
  その他の罠は再開メモ第2版の早見表を参照。
- 各ラウンドは`docs/c5-bringup.md`に実施NNとして追記し、本計画の
  候補A/B・スイープの成立/棄却を明記する。

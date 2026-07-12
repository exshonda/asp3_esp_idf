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

## 運用注意（再開メモ第2版より）

- **C5#1のflash内容は不確定**（実施21中断の副作用）。着手時に必ず
  ASP3計装ビルドを書き直してから始める。
- C5#2（stock参照機）は読み取り比較のみ・書換え禁止。
- クリーンブート＝UARTブリッジRTSリセットのみ（JTAG reset halt／
  native USB-JTAGリセットはMODEM/PMUドメインを消さない、実施14）。
  その他の罠は再開メモ第2版の早見表を参照。
- 各ラウンドは`docs/c5-bringup.md`に実施NNとして追記し、本計画の
  候補A/B・スイープの成立/棄却を明記する。

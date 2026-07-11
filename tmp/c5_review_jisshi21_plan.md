# 実施21計画レビュー（PMU/LPアナログ電源ドメインスイープ）

対象: `docs/c5-bringup.md` 実施11〜20（751行目以降）＋「別PC再開メモ（第2版）」（3058行目以降）。
実機・ビルドは使用せず，`hal/`（読取専用）・`asp3/target/esp32c5_espidf/`のgrep/読解のみで検証した。

---

## 0. 要旨

- **実施11〜20の統一像（トーン自己ループバック測定の生ADCサンプルがASP3のみハードゼロ／
  デジタル可視領域は全一致）は堅牢**。因果検証（JTAG注入）・反証実験・4-way比較・時系列反証など
  一貫して厳格な手法が使われており，致命的な論理の穴は見つからなかった。ただし**軽微だが実務上
  重要な見落とし2点**（後述1.2）がある。
- **実施21計画の「PMU/LP電源ドメイン全域スイープ」という方向性は妥当**であり，`hal/`の実装から
  「ASP3がstockの電源初期化を根こそぎ丸ごと欠いている」ことを**机上で確定的に裏付けられた**
  （`asp3/target/esp32c5_espidf/`全体をgrepしても`pmu_init`・`xpd_bias`・`dbias`・`regulator0`・
  `HPWIFI`は一件もヒットしない＝ASP3はPMUのHP/LP電源初期化を文字通りゼロ行も実行していない）。
- **一方，計画のスイープ対象は不完全**。`PCR_FPGA_DEBUG_REG`（既知のIDF-11064 modem clock erratum，
  bit31）と`CONFIG_ESP_ENABLE_PVT`経路（C5はKconfig既定でON）の2件は，PMU/LP/PCRという
  「ブロック名」レベルでは計画のスイープ範囲に含まれるはずなのに，**実施11〜20のどのラウンドの
  実測リストにも一度も出現していない未検証の具体的候補**として新たに発見した。特に
  `PCR_FPGA_DEBUG_REG`はレジスタ1個・意味説明つきの安価な決定実験であり，**フルスイープより
  先に単独で潰すべき最優先候補**として提案する。
- **スイープの限界（ラッチ/ストローブ型書込み）は実在する**。PMUの`PMU_IMM_*`系8レジスタ
  （0xCC〜0xE8）の一部ビットは`WT`（write-trigger，自己クリア）型であり，**読み出し値は
  「発火させたかどうか」を一切反映しない**——スナップショット比較そのものが原理的に無力な
  領域が存在する。実測ではなくソース読解で「ASP3が何を一度も呼んでいないか」を先に固定する
  補完策を提案する。

---

## 1. 実施11〜20の論理チェック

### 1.1 総評：統一像は堅牢

実施13（ICGゲート，A/B/A/B因果確認）→実施14（regi2c基盤生存・RF PLLロック確認不能で保留）
→実施15（stock陽性対照・MMIO全面比較・因果棄却2件）→実施16〜18（regi2cトレース・4b/4c発見・
同一コード確認・データ内容の分岐へ帰着）→実施19（cal_mode交絡の実測解消・txcap探索が
候補評価に一度も入らないという新知見）→実施20（トーン測定チェーン4関数の有界逆アセンブルで
regi2c不使用を確定・制御レジスタ全ビット一致・生ADCサンプル自体がASP3だけ恒久ゼロ）という
連鎖は，各段で「前段の結論を後段が実測で裏付けるか，反証して訂正する」という健全なサイクルを
繰り返している。具体的な美点：

- 実施12の極性誤りを実施13が自己訂正，実施16の統合仮説候補を実施18が時系列で反証，実施18の
  「`phy_param[35]`＝cal_modeフラグ」という解釈を実施19が「完了マーカーだった」と自己訂正する
  等，**過去ラウンドの結論を無批判に引き継がず，都度実測で再検証している**。
- 実施15のadvisorレビュー起点のpre-trigger再検証，実施17の判定基準事前固定，実施19のcal_mode
  交絡潰し等，**「棄却」の多くが単発観測ではなく反証実験（A/B/A/B，25秒因果監視，pre/post-trigger
  二重確認）で裏付けられている**。
- 実施14/15/17で発見された「JTAG resetの罠」「JTAG haltは周辺回路を止めない」「NULLバイト
  パースバグ」等の方法論的教訓が，後続ラウンドで実際に回避策として使われている
  （知見が使い捨てになっていない）。

### 1.2 見落とし・要フラグ事項（棄却済み判定の中に「根拠が弱い」ものは無いが，未着手の具体的候補が2件ある）

いずれも「実施11〜20のどこかの判定が誤っている」という意味の穴ではなく，**PMU/LP/PCRという
スイープ対象ブロック名の中に，一度も実測されていない具体的なレジスタ／初期化経路が存在する**
という意味での見落としである。

#### (a) `PCR_FPGA_DEBUG_REG`（`0x60096FF4`，bit31）——IDF-11064 erratumが未チェック【最優先】

`hal/components/esp_system/port/soc/esp32c5/clk.c`の`esp_perip_clk_init()`（stockが第2段
ブートローダ後・アプリ起動前に必ず呼ぶ関数）は次を無条件で呼ぶ：

```c
// esp_perip_clk_init() 内 (clk.c:239)
clk_ll_soc_root_clk_auto_gating_bypass(true);
```

`hal/components/esp_hal_clock/esp32c5/include/hal/clk_tree_ll.h:435`の実装：

```c
static inline void clk_ll_soc_root_clk_auto_gating_bypass(bool ena)
{
    if (ESP_CHIP_REV_ABOVE(efuse_hal_chip_revision(), 1)) {
        if (ena) { REG_CLR_BIT(PCR_FPGA_DEBUG_REG, BIT(31)); }
        else     { REG_SET_BIT(PCR_FPGA_DEBUG_REG, BIT(31)); }
    }
}
```

コメント（`clk.c:234-238`）："On ESP32-C5 ECO1, clearing BIT(31) of PCR_FPGA_DEBUG_REG is used
to fix the issue where **the modem module fails to transmit and receive packets due to the loss
of the modem root clock caused by automatic clock gating during soc root clock source
switching**. (IDF-11064)"

**なぜ見落とされたか**：`esp_wifi_adapter.c:763-808`のコメント（実施6〜10由来）は
`esp_perip_clk_init()`の**うち`modem_clock_select_lp_clock_source()`部分だけ**を意識して
手動移植しているが，同じ関数内の`clk_ll_soc_root_clk_auto_gating_bypass(true)`は**言及すら
されていない**。`grep -rn "soc_root_clk_auto_gating_bypass\|FPGA_DEBUG" asp3/target/esp32c5_espidf/`
は0件。実施20はPCRブロックから`PCR_SARADC_CONF`（`0x60096088`）だけを比較対象に選んでおり，
PCR全体（0x1000バイト）のうち`+0xFF4`は範囲外だった。

**なぜ症状と整合するか**：レジスタ既定値は`0xFFFFFFFF`（bit31=1=オートゲート有効＝
bypass無効）。stockは`ESP_CHIP_REV_ABOVE(rev,1)`が真である限り（DUTは`rev v1.0`＝
`ESP_CHIP_REV_ABOVE(rev,1)`は`1<=rev`なので真になる可能性が高い，要実機確認）bit31を
クリアする。ASP3はこの呼出しが無いため既定値のまま＝**オートゲートが有効なまま**。
erratumの文言「clock source switching時の自動クロックゲーティングでモデムのroot clockが
失われ，TX/RXできなくなる」は，まさに本調査が繰り返し確認してきた「ICG以降の全レジスタは
ビット一致なのに，MODEM0内部の生ADCサンプルだけが出ない」という症状——**ICGより上流の
root clock供給そのものが自動ゲートされていれば，下流のICG/CLK_CONF/CLK_CONF1が
「有効」を示していても実際にはクロックパルスが来ない**という機序に整合する。
ASP3もCPU周波数切替（実施03，192MHz確定）を行っており，"soc root clock source switching"に
相当するイベントを経由している可能性が高い。

**優先度＝最高，かつ即実行可能**：単一レジスタ・単一ビット・意味説明あり・スイープ待ち不要。
実施15〜20で確立済みの「STAGE1/STAGE2キャプチャ」の1行追加で読める。stock/ASP3で異なれば
（ASP3=bit31セット，stockはクリア），**フルPMU/LPスイープに入る前にこれ単体をJTAG注入で
因果検証**（ASP3でbit31をクリアしてから`phy_iq_est_enable_new`を再実行させる方法は，
起動シーケンス中の一度きりのイベントに依存するため，実施15型のpost-trigger注入では
再現できない可能性がある——**この一点は要注意**。起動の十分早期，PHY初期化より前に
注入する必要があり，実施21のキャプチャ設計に組み込む場合はSTAGE0（`esp_wifi_init`より前，
できればCPU周波数切替直後）にbpを置く必要がある）。

#### (b) `CONFIG_ESP_ENABLE_PVT`経路——stockは毎起動走るがASP3は皆無・Kconfig既定差の見落とし

`hal/components/esp_hw_support/port/esp32c5/pmu_init.c:228-242`：

```c
#if CONFIG_ESP_ENABLE_PVT
    uint32_t blk_version = efuse_hal_blk_version();
    if (blk_version >= 2) {
        pvt_auto_dbias_init();
        charge_pump_init();
        pvt_func_enable(true);
        charge_pump_enable(true);
        esp_rom_delay_us(1000);
    }
#endif
```

`hal/components/esp_hw_support/Kconfig:393`：`ESP_ENABLE_PVT`は**C6のみ`default n`，C5は
`default y`**（"MUST ENABLE FOR MP"とコメントあり）。stockの`examples/wifi/scan`は
sdkconfig.defaultsでこの設定を上書きしていない（実施15の記載を確認）＝**stockは毎回
`CONFIG_ESP_ENABLE_PVT=y`でビルドされ，`efuse_hal_blk_version()>=2`ならPVT自動dbias調整・
チャージポンプが実際に有効化される**。

これは実施13/14/19が確認した「reset reasonに依存する条件分岐」（`esp_ocode_calib_init()`は
`RESET_REASON_CHIP_POWER_ON`限定）とは異なり，**reset種別に関わらず毎起動走る**——本調査が
一貫して使ってきたUARTブリッジRTSリセット（`Super Watchdog resets core and rtc`，POWERONでは
ない）でもPVT初期化条件（efuseのblk_version）は変わらないため，**stockの陽性対照ブート全て
（実施15〜20）でPVT初期化が有効に働いていた可能性が高い**一方，`asp3/target/esp32c5_espidf/`
を全文grepしても`pvt_auto_dbias_init`・`charge_pump_init`・`PVT_REG`への参照は0件——
**ASP3は一度もPVT初期化を実行していない**。

PVTは"process/voltage/temperature"補正でHP/LPのdbias（コア電圧trim）を動的に微調整する
機構であり，SAR ADC/PWDET等アナログ回路の基準電圧・バイアス点に影響し得る。ただし
`efuse_hal_blk_version()>=2`という条件がDUTで実際に成立するかは**machine-readableに
確認していない**（本レビューではeFuseの当該ブロックバージョン値は読めなかった）——
実施21で最初に確認すべき前提条件として明記する（成立しなければこの経路はstockでも
未実行となり，候補から外れる）。

#### (c) 実施20の`phy_override.c`未リンク反証は「blobが呼ぶか」までしか見ていない——結論自体は正しいが射程が限定的

実施20 §2は「`set_xpd_sar`/`phy_set_pwdet_power`/`sar_periph_ctrl_pwdet_power_acquire`/
`regi2c_saradc_enable`はlibphy.aのどの`.o`からも参照されない（`nm -u`で確認）」として
リードを反証している。これは「**blobが直接これらの関数を呼ぶか**」という問いには正しく
Noと答えているが，「**stockの起動時に一度だけ走るシステム全体の電源初期化
（pmu_init／esp_perip_clk_init）が，blobから見て透過的に，SAR/PWDETが依存する電源・
バイアスレールを別の経路で整えている**」という，より広い可能性までは反証していない
——実施20自身が§8/申し送りで「次段はPMU/LP電源ドメイン」と正しく射程を広げているため，
これは実施20の誤りではなく**計画通りの残課題**である。本レビューはこの残課題に(a)(b)という
具体的な内容を与えた，という位置づけになる。

#### (d) 「readback一致＝有効状態一致」という前提の限界（実施15/17/20の比較全般に関わる注意点）

実施13が「PMU即時反映パルス2本が無いと`icg_modem.code`の書込みが効かない」ことを
**A/B/A/B因果実験で発見・確認**しているにも関わらず，実施15/17/20のMMIOスナップショット
比較は基本的に「静的な読み出し値が一致するか」だけを見ている。実施13の`icg_modem.code`
自体は幸い「レベル保持型」（読めば設定値がそのまま見える）レジスタだったため，実施13が
別途A/B/A/B因果実験で「反映されたか」を直接確認しており実害はなかった。しかし，
**この方式論上の穴が一般化されないまま実施21の「4-wayスイープ」計画に持ち越されている**
点は指摘に値する（詳細は3節）。

---

## 2. 実施21スイープ対象ブロック 推奨表

`hal/components/soc/esp32c5/register/soc/`・`hal/components/soc/esp32c5/include/modem/`・
`hal/components/soc/esp32c5/ld/esp32c5.peripherals.ld`を実地確認し，ベース/サイズは
`reg_base.h`実測値を採用した。

| ブロック | ベース | サイズ(実測ヘッダ上の最大offset+4) | 根拠ヘッダ | 優先度 | 備考 |
|---|---|---|---|---|---|
| `PMU`（特に`HP_ACTIVE`モード群） | `0x600B0000` | 0x1AC (実側は+0x400までLP_CLKRSTに隣接) | `pmu_reg.h` | **最高** | `DIG_POWER`(+0x0)・`BIAS`(+0x18)・`HP_REGULATOR0`(+0x28)・`HP_REGULATOR1`(+0x2C)は`pmu_hp_system_init(..., PMU_MODE_HP_ACTIVE, ...)`が書く値そのもの。ASP3は一度も書かない（grep 0件） |
| `PMU`（電源ドメインforce群） | `0x600B0000+0x108`（`PMU_POWER_PD_HPWIFI_CNTL_REG`） | 1 word | `pmu_reg.h:2129` | **最高** | 「WIFI」と明示的に名の付く唯一のPMUレジスタ。POR既定は`FORCE_PU=1・FORCE_NO_RESET=1・FORCE_NO_ISO=1`（安全側）だが，stockの`pmu_power_domain_force_default()`はこれを含む4ドメイン全ての強制ビットを明示的に**0へ戻し**HW FSM制御に委ねる。ASP3はこの遷移（force→FSM委譲）自体を一度も経験しない — 詳細は3節 |
| `PCR`（`FPGA_DEBUG`のみ最優先） | `0x60096000+0xFF4` | 1 word | `pcr_reg.h:2599` | **最高（単独で即検証可）** | 1節(a)。IDF-11064 erratum該当ビット |
| `PVT`(`PVT_MONITOR`) | `0x60019000` | 0x1000 | `pvt_reg.h` | 高 | 1節(b)。`efuse_hal_blk_version()>=2`が前提条件，まず確認要 |
| `LP_ANA`(`lp_analog_peri`) | `0x600B2C00` | 0x400 | `lp_analog_peri_reg.h` | 高 | BOD/POWER_GLITCH/FIB_ENABLE等，計画が名指しした`LP_ANA`そのもの |
| `LP_AON` | `0x600B1000` | 0x400 | `lp_aon_reg.h` | 中 | 計画記載済み |
| `LPPERI` | `0x600B2800` | 0x400 | `lpperi_reg.h` | 中 | 計画記載済み |
| `LP_CLKRST` | `0x600B0400` | 0x400 | `lp_clkrst_reg.h` | 中 | **計画に未記載だがPMU/LP_AONに挟まれた隣接ブロック，抜け防止に追加推奨** |
| `MODEM_SYSCON`（未比較の残り域） | `0x600A9C00+0x2C`以降 | `+0x2C`までしか実測されていない（実施14/15/20は`+0x2C`まで） | `modem_syscon_reg.h`（公開定義は`+0x28`まで） | 中 | 公開ヘッダで名の付く範囲は使い尽くしたが，物理レジスタ空間は`0x600AC000`まで続く可能性——ここは「PMU/LP」より優先度は落ちるが，スイープのついでに未読域を広げる価値あり |
| `MODEM1` | `0x600AC000` | 0x1000（`MODEM_PWR0`との間隔から推定） | `reg_base.h`のみ（構造体/レジスタ名は非公開） | 低〜中 | **どのラウンドでも一度も触れられていない**。ld scriptに`PROVIDE(MODEM1=0x600AC000)`はあるが公開ヘッダが無い＝MODEM0内部offset群と同様，blobが直接叩く未公開領域の可能性 |
| `MODEM_PWR0` | `0x600AD000` | 0x2000（`MODEM_PWR1`/`MODEM_LPCON`との間隔） | `reg_base.h`のみ | 低〜中 | 名前が示唆する通り「モデム電源」ブロックの可能性が高いが**公開ヘッダが一切無く実施11〜20のどこにも登場しない**。MODEM0の未公開internal offset（`+0x41C`等）と同様の手法（有界逆アセンブルでの間接特定）でしか読み解けない可能性が高いため，優先度は中程度に留め，PMU/LP/PCRで決着しなければ次点として着手 |
| `I2C_ANA_MST`（HP側，既実測） | `0x600AF800` | 0x34 | `i2c_ana_mst_reg.h` | 済 | 実施14で確認済み（再掲不要） |
| `LP_I2C_ANA_MST`（LP側，未実測） | `0x600B2400` | 0x400 | `lp_i2c_ana_mst_reg.h` | 低 | HP側とは別のregi2cマスタ。WiFi RF規正には無関係の可能性が高い（BOD/xtal32k較正用と推定）が，念のため計画のスイープ対象へ追加 |
| `LP_APM`/`LP_APM0`/`HP_APM`/`CPU_APM` | 各種 | — | 各`*_reg.h` | 低（既に別筋で決着） | 実施04/05でAPM仮説は既に反証済み（「poke無効」）。スイープに含めても良いが優先度は最下位でよい |

**計画からの追加推奨**：`PCR_FPGA_DEBUG_REG`単独チェック（表内「最高・単独で即検証可」の1行）を
**フルスイープ着手前の第0ステップ**として先に済ませることを強く推奨する。これが当たりなら
残りのスイープは大幅に軽量化できる（実施22は「注入して直るか」の確認のみで済む可能性がある）。

---

## 3. スイープの限界（ラッチ/ストローブ型書込み）と決定実験の設計

### 3.1 「読み出し値が一致」は「有効化された」の必要条件だが十分条件ではない——実例で示す

`PMU_IMM_*`系8レジスタ（`0x600B00CC`〜`0x600B00E8`，`pmu_reg.h:1560-1784`）は，実施13が
`PMU_IMM_MODEM_ICG_REG`（`+0xDC`）・`PMU_IMM_SLEEP_SYSCLK_REG`（+0xD0）で発見した通り，
「値を書いてもトリガパルスを送らないと下流に反映されない」ラッチ機構を持つ。さらに悪いことに，
このファミリのうち`PMU_IMM_HP_CK_POWER_REG`（`+0xCC`）を実際に読むと，含まれるビット
（`PMU_TIE_LOW_GLOBAL_BBPLL_ICG`・`PMU_TIE_LOW_GLOBAL_XTAL_ICG`）は**`WT`（write-trigger，
自己クリア）型**で「default: 0」と明記されている（`pmu_reg.h:1561-1574`）。

これはすなわち：**このビットは，一度もパルスされていなくても，1000回パルスされていても，
読み出し値は常に`0`である**。実施13の`icg_modem.code`（レベル保持型）はたまたま比較可能
だったが，`WT`型ビットに対して「stock/ASP3のスナップショットを比較して差分ゼロ」を得ても，
それは**「両者とも同じ回数パルスした」ことの証拠にはならず，「両者ともパルスを一度も
発行していない（＝読めば必ず0になる）」ことの証拠にもならない**——スナップショット比較
そのものが原理的に無情報になる。

### 3.2 スイープへの実務的な組み込み方（提案）

1. **レジスタを2種類に事前分類してから比較する**：
   - レベル保持型（`DIG_POWER`・`BIAS`・`HP_REGULATOR0/1`・`PD_xxx_CNTL`の`FORCE_*`ビット，
     `MODEM_SYSCON`/`PCR`の大半，`FPGA_DEBUG`含む）＝**スナップショット差分比較が有効**。
   - ストローブ/WT型（`PMU_IMM_*`ファミリ全般，他ブロックにも同型のwrite-1-pulse機構が
     ある可能性——`pmu_reg.h`を`WT;`でgrepして事前に列挙する）＝**スナップショット比較は
     やらない（無情報と分かっているものに時間を使わない）**。
2. **ストローブ型についてはソース読解で決着させる**：ASP3のDirect Bootコードは（blobと違い）
   自社ソースなので，「対応するトリガ関数を一度でも呼んでいるか」は`grep`一発で判定できる
   （本レビューで実施済み：`pmu_init`・`xpd_bias`・`dbias`・`regulator0`・`HPWIFI`は
   `asp3/target/esp32c5_espidf/`に一件もヒットしない＝ASP3はPMU・HP/LP電源初期化・
   ドメインforce解除のいずれも**一度も実行していないことが机上で確定**している）。
   したがって「差分があるかもしれないので読んでみる」のではなく，**「ASP3は最初から
   何も設定していないと分かっている前提で，stockの値を丸ごと注入して症状が変わるか」**
   という順で実験を組む方が効率的（実施22の設計に反映，4節）。
3. **PMU即時反映パルスを忘れない**（計画にも明記済み，実施13参照）。レベル保持型レジスタで
   あっても，`hp_pd[domain]`のforce系（例：`PMU_POWER_PD_HPWIFI_CNTL_REG`）が実際に
   ハードウェアの電源シーケンサへ反映されるまでのタイミング要件（イミディエイト反映か，
   何らかの内部FSM遷移を要するか）は`hal/`のコメントだけでは確定できないため，**注入後は
   十分な待ち時間を置いてから判定する**（実施13の「片方だけパルスすると反映されない」
   という罠と同型の罠が他にもある前提で臨む）。

### 3.3 決定実験の設計（スイープを補完する）

スイープ（静的比較）に加えて，以下を**差分の有無によらず独立に**実施することを推奨する
（3.1の限界がある以上，「差分ゼロ」がそのまま「電源状態も同一」を意味しないため）：

- **決定実験A（最優先・最安価）**：`PCR_FPGA_DEBUG_REG` bit31を，起動極早期
  （`esp_wifi_init`より前，理想的にはCPU周波数切替直後）でstock値（クリア）にJTAG注入し，
  そのままトーン測定まで到達させて生ADCサンプルが動くかを見る。**単一ビット・単一レジスタ・
  意味説明ありで，フルPMUスイープよりはるかに安く白黒つく**。
- **決定実験B**：`efuse_hal_blk_version()`をJTAGで直読みし，`CONFIG_ESP_ENABLE_PVT`経路が
  DUTで実際に成立するかを先に確認する（成立しなければPVT系は候補から除外でき，Bはスキップ）。
  成立するなら，PVT初期化5関数（`pvt_auto_dbias_init`・`charge_pump_init`・
  `pvt_func_enable`・`charge_pump_enable`・1msディレイ）が触るレジスタをASP3で複製注入し
  症状変化を見る。
- **決定実験C（4節の「電源系初期化列の加算移植A/B」の縮小版）**：`pmu_hp_system_init`が
  `PMU_MODE_HP_ACTIVE`に書く4レジスタ群（`DIG_POWER`/`BIAS`/`HP_REGULATOR0`/`HP_REGULATOR1`，
  合計4 word）をstockの実測値でASP3へ丸ごと注入し（IMMパルスの要否は要現場確認），
  トーン測定を再試行させる。

---

## 4. 実施22以降の分岐計画

### ケース1：スイープ（決定実験A/B/Cを含む）で有意な差分が見つかり，注入で症状が改善した場合

1. 該当レジスタ／初期化関数を`asp3/target/esp32c5_espidf/`側へ恒久移植（実施13の
   `esp_shim_modem_icg_init()`パターンを踏襲：**1回の変更で1つの機構だけを直す**，
   切り分け可能性を保つというユーザ指示に従う）。
2. 実機で0x3001〜実施20までの全既知シナリオ（stock 27AP相当）が再現するかフルスキャンで確認。
3. `docs/c5-bringup.md`実施22として記録し，`memory/project_c6_agc_investigation.md`・
   `MEMORY.md`をコーディネータが更新（CLAUDE.md運用通り）。

### ケース2：スイープ＋決定実験A/B/Cのいずれでも差分・改善が得られなかった場合

**この場合に限り**，「C6-genericな共通アナログ壁」への構造比較整理・停止をユーザ判断へ
仰ぐ，という計画の分岐は妥当である。ただし停止前に，以下の**安価な残り一手**が尽きている
ことを確認してからにすべき（実施13の打ち切り基準「安価な反証実験を先に」を踏襲）：

- 表2節の`MODEM1`（`0x600AC000`）・`MODEM_PWR0`（`0x600AD000`）——**公開名は無いが
  実施11〜20のどのラウンドでも一度も触れられていない領域**。MODEM0内部offset
  （`+0x41C`等）と同じ「有界逆アセンブルで間接的に特定する」手法がまだ未適用。
- `LP_I2C_ANA_MST`（`0x600B2400`）経由のregi2cブロック一覧——HP側`I2C_ANA_MST`
  （`0x600AF800`）は実施14で確認済みだがLP側は未実測。

**「電源系初期化列の加算移植A/B」（`pmu_init`+`esp_rtc_init`相当+SAR電源初期化を
まとめて丸ごと移植してみる）の妥当性評価**：

- C6実施34は同種の「まとめて足す」アプローチでハング多発と記録されている
  （ユーザ提示の懸念）。しかし**C5とC6は条件が異なる**：
  - C5には**陽性対照（stock，同一個体・同一blob）が既に確立**しており，「まとめて足した後」
    に症状が変わったかどうかを**同一の再接続競争JTAG手法で即座にA/B判定できる**環境が
    ある。C6実施34時点でこの水準の実機比較環境が整っていたかは`docs/wifi-shim-c6.md`側の
    確認が必要だが，本ドキュメントの記述からはC5の方が高速に「効いたか否か」を判定できる
    と推測される。
  - C5の`pmu_init()`は**ESP-IDF公式の分離された関数群**（`pmu_hp_system_init`/
    `pmu_lp_system_init`/`pmu_power_domain_force_default`/PVT）であり，各関数が独立して
    JTAG注入で模倣可能——「まとめて足す」を実際には「1関数ずつ加算し，都度JTAG A/Bで
    寄与を確認する」段階的な適用に分解できる（本レビュー3.3節の決定実験A/B/Cがこの
    第一段）。C6実施34が「まとめて足してハング」だったのは，切り分け粒度が粗すぎた
    ことが一因である可能性があり，C5では同じ轍を踏まず**関数単位での段階適用**を
    推奨する。
  - リスク：Direct Bootは元々`pmu_init()`等を意図的に呼ばない設計（カーネル内動的メモリ
    不使用等，CLAUDE.mdの禁則と設計思想に関わる）。`pmu_init()`をまるごと呼ぶことは
    スリープ・retentionサブシステムへの依存を持ち込みかねず（実施12が`_wifi_pm_sleep_
    lock_acquire`等のno-opスタブで意図的に切り離した領域と一部重なる），**関数全体の
    呼び出しではなく「値の直接注入（レジスタ書き込みのみ）」に留める**方が，ASP3の
    「カーネル内動的メモリ確保禁止」という制約とも整合し安全である。
- 上記を尽くした上でなお差分・改善が得られない場合に，「C6-genericな共通アナログ壁」との
  構造比較整理・停止をユーザ判断へ仰ぐのが妥当な着地点となる。

---

## 附記：本レビューの制約

- 実機・JTAG・ビルドは一切行っていない（指示通り）。上記の`PCR_FPGA_DEBUG_REG`・PVT・
  PMU HP_ACTIVEレジスタ群はいずれも`hal/`ヘッダ・Cソースの読解から導出した**理論的候補**
  であり，DUT実機での成立（特にチップrev条件・efuse blk_version）は実施21以降で
  必ず実測確認すること。
- IDF v6.1実物はこの環境に無いため，`esp_perip_clk_init()`/`pmu_init()`の実際のリンク後
  挙動（`--gc-sections`後も本当にこの経路が使われるか等）はhal内の対応物（esp-hal-3rdparty
  submodule）からの推定であり，stockビルド実物（`stock_scan/build`）での`nm`/`objdump`
  再確認を推奨する。
- `memory/`配下のファイル（`feedback_hardware_investigation_rigor.md`・
  `project_c6_agc_investigation.md`）は本セッションの環境には存在せず（別PC環境の資産と
  推測される），参照できなかった。本レビューは`docs/c5-bringup.md`本文の記述のみに基づく。

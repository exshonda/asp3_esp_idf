# C6 evidence-04 — **真cold の phy_init ハングを直す**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration` ／ 前段 commit: `e614973`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`**（hub `1-6` port2）
前段: `evidence-c6-03` ＝ **帰属は ASP3 側**（同一個体・同一 libphy バイトで stock は真cold 完走・ASP3 は phy_init で停止）

---

## 1. 目的（1行）

**真cold(POR) で ASP3-C6 が phy_init を通るようにする。効いた最小集合を同定し、逆方向対照で因果を確定する。**

---

## 2. 着手前に**自分で実測**した事実（引き継ぎを鵜呑みにしない）

親プロンプト §1 の「確定済みの事実」も含めて独立に裏を取った。

### 2.1 追認できたもの

| 引き継がれた主張 | 私の検算 | 判定 |
|---|---|---|
| `MEMORY.md` は 20.2KB・サイズフック無し | `wc -c` = **20665 B** | **★追認** |
| `hardware_init_hook()` は `.data` 初期化より前に走る | **実機バイナリを逆アセンブルして確認**（§2.3） | **★追認** |
| §20 `pmu_init` はリンク済み・既定 ON | `nm build/c6u_v61_after/asp.elf` に `4200590a T pmu_init`・`T esp_shim_bt_pmu_init`・`ESP32C6_BT_PMU_INIT:BOOL=ON` | **★追認** |

### 2.2 ★**引き継ぎの誤りを1件見つけた**（evidence-03 §5.1 の差分表）

evidence-03 §5.1 は **`rtc_clk_modem_clock_domain_active_state_icg_map_preinit()` を「★欠落」**と記載した。
**これは誤り**。ASP3 には **`esp_shim_modem_icg_init()`（`wifi/esp_shim.c:1521`）として移植済み**で、
stock（`esp-idf/components/esp_hw_support/port/esp32c6/rtc_clk_init.c:44-57`）と**行単位で同一**：

| stock `rtc_clk_init.c` | ASP3 `esp_shim.c:1521` |
|---|---|
| `pmu_ll_hp_set_icg_modem(&PMU, PMU_MODE_HP_ACTIVE, PMU_HP_ICG_MODEM_CODE_ACTIVE)` | `pmu_ll_hp_set_icg_modem(pmu, PMU_MODE_HP_ACTIVE, 2U)` |
| `modem_syscon_ll_set_modem_apb_icg_bitmap(&MODEM_SYSCON, BIT(…))` | `modem_syscon_ll_set_modem_apb_icg_bitmap(syscon, code_bit)` |
| `modem_lpcon_ll_set_i2c_master_icg_bitmap(&MODEM_LPCON, BIT(…))` | `modem_lpcon_ll_set_i2c_master_icg_bitmap(lpcon, code_bit)` |
| `modem_lpcon_ll_set_lp_apb_icg_bitmap(&MODEM_LPCON, BIT(…))` | `modem_lpcon_ll_set_lp_apb_icg_bitmap(lpcon, code_bit)` |
| `pmu_ll_imm_update_dig_icg_modem_code(&PMU, true)` | 同 |
| `pmu_ll_imm_update_dig_icg_switch(&PMU, true)` | 同 |

**呼出しも実在**：`wifi/esp_wifi_adapter.c:698`（WiFi 経路）・`bt/bt_shim.c:83`・`bt/bt_shim_idf61.c:73`（BT 経路）。
⇒ **evidence-03 §5.2 の「推奨順1＝最有力」は前提が崩れている**（ICG preinit は既にある）。
★これは evidence-03 を責める話ではない：§5 は明示的に **«調査対象の列挙・原因未主張»** と宣言されており、
その宣言どおり**検証したら1件外れた**というだけ。**列挙を鵜呑みに実装しなくてよかった**。

同様に **`pmu_init()` は stock `rtc_clk_init()` のアナログ部と大きく重複**する（両方 `ENIF_RTC_DREG=1`・
`ENIF_DIG_DREG=1` を書き、`get_act_hp_dbias()`/`get_act_lp_dbias()` は
`pmu_hp_system_init_default`→`pmu_hp_system_param_default` 内で適用される）＝
**「rtc_clk_init 相当を丸ごと移植」は二重適用になりかねない**。

### 2.3 ★★**本ラウンドの発見（静的に確定）＝`pmu_init()` は `.data` 未初期化で走っている**

**(a) 起動順**（`build/c6u_v61_after/asp.elf` を逆アセンブル。ソース `start.S` の読みではなく**実機バイナリ**）：

```
42000008: j    42000100 <start>
42000100 <start>:
4200012a: jal  42008cc2 <hardware_init_hook>   ← ① §20 の pmu_init() はここ
42000136: ...  __bss_end                       ← ② .bss クリア
4200014c: ...  __idata_start → __data_start    ← ③ .data コピー
42000174: jal  <software_init_hook>            ← ④
42000178: j    <sta_ker>
```

**(b) stock `PMU_instance()` は `.data` 上の static を返す**（`hal/…/esp32c6/pmu_init.c:42`）：

```c
pmu_context_t * __attribute__((weak)) IRAM_ATTR PMU_instance(void)
{
    static DRAM_ATTR pmu_hal_context_t pmu_hal = { .dev = &PMU };            /* 非0初期化子 → .data */
    static DRAM_ATTR pmu_sleep_machine_constant_t pmu_mc = PMU_SLEEP_MC_DEFAULT();
    static DRAM_ATTR pmu_context_t pmu_context = { .hal = &pmu_hal, .mc = (void *)&pmu_mc };
    return &pmu_context;
}
```

**nm 実測**（`build/c6u_v61_after/asp.elf`）＝**3つとも `.data`**：

| symbol | addr | 種別 | `.data` 範囲 `[0x40819000, 0x408193d0)` 内か |
|---|---|---|---|
| `pmu_context.0` | `0x40819378` | **`d`** | **★内** |
| `pmu_hal.2` | `0x40819348` | **`d`** | **★内** |
| `pmu_mc.1` | `0x4081934c` | **`d`** | **★内** |
| `PMU_instance` | `0x4203b310` | **`W`（weak）** | — |

**(c) その `.data` ポインタは実際に deref されて PMU 記述子の書込み先になる**（逆アセンブル）：

```
4203b310 <PMU_instance>:
4203b310: auipc a0,0xfe7de
4203b314: addi  a0,a0,104   # 40819378 <pmu_context.0>   ← .data のアドレスを返す
4203b318: ret

42005530 <pmu_hp_system_init>:
42005538: lw   a4,0(a0)    ← a4 = ctx->hal        （.data から読む）
42005540: lw   a0,0(a4)    ← a0 = ctx->hal->dev   （それを deref）
42005554: add  a5,a5,a0
42005556: sw   t3,0(a5)    ← ★ PMU 記述子を (dev + mode*52) へ書く
```

⇒ **`hardware_init_hook`（①）から `pmu_init()` を呼ぶと、`.data`（③）がまだコピーされていない**。
∴ **真cold では `ctx->hal` は SRAM のゴミ＝`dev` が PMU(`0x600B0000`) を指さず、PMU は一切設定されない。**
**warm では前ブートが初期化した `.data` が HP SRAM に残るため «たまたま» 正しく動く。**

★これは **evidence-03 §4.1 の #4（BT: 真cold ハング／warm D-1 成功）を、新しいハードウェア理論を一切
持ち出さずに説明する**。かつ **「§20 の pmu_init はリンク済み・既定ON なのに真cold 2/2 ハング」**＝
**«足りない» のではなく «その位置では壊れている»** と読み替える。
★これは親プロンプト §2 が名指しした罠そのもの（「C3/C6/S3 で必ず踏む」HANDOFF §4-3-6）。

---

## 3. ★★実機前に固定する予測（★ここから下は実機を1回も触らずに書いた）

### 3.1 実験の設計（**一度に1つだけ動かす**）

**新規オプション2つ**（いずれもガード付き・可逆）：

| option | 既定 | 効果 |
|---|---|---|
| `ESP32C6_PMU_INIT_LATE` | **ON**（実測で決める。§6 で答合せ） | `esp_shim_bt_pmu_init()` を `software_init_hook`（**.data 後・カーネル前**）から呼ぶ。**OFF＝§20 の `hardware_init_hook`＝逆方向対照** |
| `ESP32C6_PMU_DIAG` | **OFF** | `PMU_instance()->hal` を LP_AON **STORE8**（`0x600B1020`＝`hardware_init_hook`＝.data 前）と **STORE9**（`0x600B1024`＝`software_init_hook`＝.data 後）へミラー。**hal は deref しない**（ゴミなら不正アドレス） |

**A/B 2アーム**（**唯一の差＝`ESP32C6_PMU_INIT_LATE`**。`ESP32C6_PMU_DIAG=ON` は**両アーム共通＝定数**）：

```
cmake -S asp3/asp3_core -B build/c6_e04_{late,off} -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/asp3_core/cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c6_espidf -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c6_espidf \
  -DASP3_APPLDIR=$PWD/apps/bt_smoke_c6 -DASP3_APPLNAME=bt_smoke_c6 \
  -DESP32C6_BT=ON -DESP32C6_BT_IDF61=ON -DESP32C6_BT_PMU_INIT=ON \
  -DESP32C6_PMU_DIAG=ON -DESP32C6_CONSOLE=uart0 \
  -DASP3_EXTRA_COMPILE_DEFS=TOPPERS_C6_BT_D1_TRACE \
  -DESP32C6_PMU_INIT_LATE={ON,OFF}
```

**両アームがビルド通過済み**、かつ**逆アセンブルで «唯一の差» を静的に確認済み**（実機前）：

| arm | `hardware_init_hook` | `software_init_hook` |
|---|---|---|
| **late** | `j <esp_shim_bt_pmu_diag>`（slot 8）のみ | `jal <esp_shim_bt_pmu_diag>`（slot 9）→ **`jal <esp_shim_bt_pmu_init>`** |
| **off** | `jal <esp_shim_bt_pmu_diag>`（slot 8）→ **`j <esp_shim_bt_pmu_init>`** | `jal <esp_shim_bt_pmu_diag>`（slot 9）のみ |

**両アームとも `pmu_hal.2 = 0x40819348`**（＝正しい `ctx->hal` の期待値）・`pmu_context.0 = 0x40819378`。

### 3.2 予測

| # | 測定 | **予測** | 確度 | 根拠 |
|---|---|---|---|---|
| **M1** | **真cold・`off` アーム**の **STORE8**（.data 前の `ctx->hal`） | **≠ `0x40819348`（ゴミ）** | **85%** | §2.3。POR で HP SRAM は不定。★これが**機序の直接実測**（推論でない） |
| **M2** | **真cold・両アーム**の **STORE9**（.data 後の `ctx->hal`） | **== `0x40819348`** | **95%** | .data コピー後だから |
| **M3** | **warm**の **STORE8** | **== `0x40819348`** | **70%** | 前ブートの .data が SRAM に残る。★**低め**の理由＝ROM が当該 SRAM を使い潰す可能性（§3.3(B)） |
| **P1** | **真cold・`late` アーム**の **STORE0** | **`0xb1d00008`（D-1 到達）** | **55%** | §2.3 が真なら PMU が正しく設定される。★**55% の理由＝「必要」と「十分」は別**（§3.3(A)） |
| **P2** | **真cold・`off` アーム**の **STORE0**（**逆方向対照**） | **`0xb1d00005`（ハング）** | **90%** | evidence-03 #4 が 2/2 で実測済み。★同一系統のバイナリで**私が再測**する |

### 3.3 ★含意表 — **「反証条件も仮説である」を自問した結果**

親指示 §3.8：**「A ⇒ B」と書く前に、その含意が本当に成立するか自問しろ**。

| | 主張 | 健全か |
|---|---|---|
| **(A)** | 「M1 が真（ゴミ）⇒ P1 も真（直せば通る）」 | **★健全でない**＝ゴミポインタが実在しても、**PMU 設定が cold phy_init に «必要» なだけで «十分» とは限らない**。stock `rtc_clk_init()` の regi2c トリム・`recalib_bbpll()` が追加で要る可能性は残る。∴ **P1 は 55%** |
| **(B)** | 「P1 が偽 ⇒ M1 も偽（機序は無かった）」 | **★健全でない**＝**M1 は diag が直接測る**ので P1 の成否と独立。**P1 が外れても M1 は生き残りうる**（＝「ゴミは実在するが、それだけでは cold は通らない」が正しい結論になる） |
| **(C)** | 「`late` が通り `off` が落ちる ⇒ **ゴミポインタが原因**」 | **★健全でない＝ここが本ラウンドの罠**。それが示すのは「**呼出し位置が因果**」まで。**対抗仮説 H1'(timing)＝「後で呼ぶ方がアナログの整定時間が稼げる／他の init との順序が変わるから効いた」**が潰せていない |
| **(D)** | **(C) の識別子＝diag(M1)** | **★健全**＝真cold で STORE8 がゴミなら、`off` アームの `pmu_init()` は**実際に誤った `dev` へ書いた**ことが**実測で**確定する（timing では説明できない）。逆に STORE8 が `0x40819348` なら **§2.3 は反証され、H1'(timing) が本命になる** |
| **(E)** | 「warm の STORE8 がゴミだった場合」 | **★重要な負の結果**＝warm でも `pmu_init()` はゴミで走ることになり、**「.data 残留で warm は通る」という cold/warm の説明は崩れる**（warm 成功は別機序）。**§2.3 の «起動順» 部分は静的事実として残るが、cold/warm 分岐の説明としては撤回する** |
| **(F)** | 「P1 が真 ⇒ C6 BLE が真cold で成立」 | **健全だが D-1 まで**。D-2b/W1 は別途測る（§6） |

★**∴ M1(diag) と P1(fix) は独立な2つの測定**であり、**M1 が機序を、P1+P2 が十分性と因果を担当する**。
この分離を**実機前に**宣言しておく。

### 3.4 測定の作法（本ラウンドで守るもの）

| 罠 | 対策 |
|---|---|
| `rst:0x1 (POWERON)` は真coldの証明にならない | **真coldの証明＝`uhubctl -l 1-6 -p 2 -a off` ＋ by-id 消滅の読み戻し**（毎回） |
| マーカが stale | ★**真cold は LP_AON を 0 に消す**（evidence-03 §6.5）＝**センチネル法**：`write-mem STORE6=0xCAFE5A9C` → 電源断 → 復電 → **STORE6==0 なら «真cold» と «マーカ非stale» を同時証明** |
| 途中 open が DUT をリブート | ★**cold の A/B では UART を一切開かない**（判定は LP_AON マーカ＝console 非依存＝memory「STOREを主判定」）。`rst:` 採取は**別の専用 run**（§3.2 の残ブロッカー用） |
| コンソール氾濫が成功を隠す | 同上（console を判定に使わない） |
| 「マーカ 0」を «ハング» と誤読 | **`--before usb-reset --after hard-reset`** を使う（`--before no-reset` はアプリを起動しないことがある＝evidence-03 §6.5-4） |
| 中間生成物 `cfg1_out.elf` を掴む | **`asp.elf` を明示**（evidence-03 §6.5-5） |
| リビルドでシンボルが動く | **本ラウンドの2アームは `pmu_hal.2 = 0x40819348` で一致**することを nm で確認済み |

### 3.5 電源操作（★`-p 2` のみ）

```
sudo uhubctl -l 1-6 -p 2 -a off ; sleep 4 ; sudo uhubctl -l 1-6 -p 2 -a on ; sleep 7
```
port1=C3・port3/4=C5 は**触らない**。

---

## 4. 実機測定（2026-07-17）— **★真cold で phy_init を通した**

### 4.0 結論（先に4行）

1. **★真因＝「真cold(POR) では ROM が CPU を XTAL@40MHz のまま Direct Boot へ渡す」**。
   ASP3 は `target_kernel_impl.c` で「**ROM が既に SPLL・160MHz へ設定済みだから
   追加のレジスタ操作は不要かつ行うべきでない**」と**明示的に «やらない» 判断**を
   しているが、**その «確認» は warm でしか行われていなかった**。
   ⇒ **「stock がやっていることを我々がやめた」型**（C6 実施90-91 と同型の再発）。
2. **★物証は1つの数**（同一バイナリ・同一レジスタ・電源状態だけ違う）：
   **真cold `0xbb110280`＝XTAL/40MHz ／ warm `0xbb110a01`＝PLL/160MHz**。
3. **★修正＝stock の `rtc_clk_init()` 相当を `software_init_hook` へ移植**：
   **真cold で BT D-1（`0xb1d00008`）・WiFi scan（16-20 APs, err=0）が通る**。
   **逆方向対照（OFF に戻す）で両方とも再ハング**＝因果確定。**warm 非回帰も確認**。
4. **★§20 の pmu_init は «足りない» のではなく «その位置では壊れていた»**（`.data`
   未初期化でゴミポインタ＝**実測**）。ただし**それを直しても cold は通らなかった**
   ＝**別個の実バグ**（修正は保持）。

### 4.1 ★測定マトリクス（**全て本個体 `14:C1:9F:E0:5A:9C`・本セッション・私が実測**）

**真cold の証明＝毎回2重**：(i) `uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 消滅の読み戻し=0**、
(ii) **センチネル `0xCAFE5A9C` → 電源断 → `0x00000000`**（＝POR が LP_AON を消した＝
**マーカは非stale であることが同時に証明される**）。**cold では UART を一度も開いていない**
（判定は LP_AON 直読み＝console 非依存）。

| # | arm（唯一の差） | 電源 | **STORE0** | 判定・物証 |
|---|---|---|---|---|
| 1 | `off`＝**§20 のまま**（LATE=OFF） | **真cold** | **`0xb1d00005`** | ハング（evidence-03 #4 を再現） |
| 2 | `late`＝pmu_init を `.data` 後へ | **真cold** | **`0xb1d00005`** | **★P1 外れ**（§4.3） |
| 3 | `late`（独立2回目） | **真cold** | `0xb1d00005` | 再現 2/2 |
| 4 | `settle=800ms`（時間だけ動かす） | **真cold** | `0xb1d00005` | **★H3（整定時間）反証** |
| 5 | **JTAG（reset せず attach）** | **真cold** | — | **★PC=`0x4203453E`＝`ram_set_chan_freq_sw_start+0x1e`＝RF synth PLL ロック待ちスピン**（`0x600a00cc` bit8）。**resume を挟んだ独立2サンプルとも同一 PC** |
| 6 | `recalib_bbpll=ON` | **真cold** | `0xb1d00005` | **★STORE5=`0xbb110002`＝`src != PLL`＝分岐が丸ごと skip**＝**ここで真因が露見** |
| 7 | クロック実測 | **真cold** | `0xb1d00005` | **★STORE5=`0xbb110280`＝src=0(XTAL)/40MHz** |
| 8 | `COLD_CPU_PLL`（PLL 切替のみ） | **真cold** | `0`（未到達） | 切替後マーカ STORE3=0 |
| 9 | ＋MSPI 分周比 | **真cold** | `0`（未到達） | **★JTAG：PC=`rtc_clk_cpu_freq_set_config+0x168`＝BBPLL 較正の regi2c 完了待ちスピン**（`0x600af818`） |
| **10** | **＋ICG preinit＋regi2c マスタを «前» へ** | **真cold** | **★`0xb1d00008`** | **★D-1 到達＝phy_init 完走**。STORE7=`0xa1020704`（storm 無・warm 既知良好と一致）。clk: **`0xbb110280`(XTAL/40) → `0xcc110a01`(PLL/160)** |
| **11** | **`COLD_CPU_PLL=OFF`（★逆方向対照）** | **真cold** | **`0xb1d00005`** | **★外すと再ハング＝因果確定** |
| 12 | `COLD_CPU_PLL=ON`・**MSPI 段だけ外す** | **真cold** | **`0xb1d00008`** | **★MSPI 段は最小集合に «入らない»**（§4.4） |
| **13** | 修正版（全部入り） | **warm** | **`0xb1d00008`** | **★非回帰**。`rst:0x15 (USB_UART_HPSYS)`。**STORE5=`0xbb110a01`＝PLL/160MHz** |
| **14** | **`wifi_scan` ＋修正** | **真cold** | — | **★`16 APs found (err=0)`・`RESCAN 20/15/19 APs`・601 byte・`promisc_rx_count=274`** |
| **15** | **`wifi_scan` 修正OFF（★逆方向対照）** | **真cold** | — | **★0 byte・RESCAN 0 行＝evidence-03 #3 の cold baseline を完全再現** |

**マーカ**：`0x600B1000`=`0xB1D00000|stage`（stage5＝`esp_bt_controller_enable()` 手前で停止／
stage8＝**D-1 達成＝HCI Command Complete 受信でしか到達しない**）。
`0x600B1014`/`0x600B100C`＝`0xBB11_0000`/`0xCC11_0000` `| src | freq<<4`（切替 前/後）。
`soc_cpu_clk_src_t`: **XTAL=0, PLL=1, RC_FAST=2**。

### 4.2 ★★帰属を決める1行（依頼 (a)(b)）

> **同一バイナリ・同一レジスタで、真cold は `0xbb110280`（XTAL/40MHz）、
> warm は `0xbb110a01`（PLL/160MHz）。ROM は POR 直後の Direct Boot へ
> «PLL 未設定» の CPU を渡してくる。ASP3 はそれを «設定済み» と決め打って
> 何もしないので、PLL 由来の modem/PHY クロックが立たず RF シンセが
> ロックできない（真cold JTAG：`ram_set_chan_freq_sw_start+0x1e` 永久スピン）。**

**cold/warm 分岐の機序**＝**PCR の `SOC_CLK_SEL` は warm リセット（`rst:0x15
USB_UART_HPSYS`）では «前ブートの PLL 設定» を保持する**が、**POR では既定へ戻る**。
∴ **warm だけを見ていた 90+ ラウンドでは前提が «真» に見え続けていた**。

### 4.3 ★予測の答合せ（§3.2・依頼 (c)）

| # | 予測 | 確度 | 実機 | 判定 |
|---|---|---|---|---|
| **M1** cold の STORE8 はゴミ | 85% | **`0x0e1ac94b`/`0x2e1ac943`/`0x2e0a4b63`**（毎 POR 別値＝ランダム SRAM） | **★的中** |
| **M2** cold の STORE9=`0x40819348` | 95% | **`0x40819348`**（全runs） | **★的中** |
| **M3** warm の STORE8=`0x40819348` | 70% | **未測定**（§6.3 の残件） | — |
| **P1** `late` は真cold で D-1 | **55%** | **`0xb1d00005`＝ハング** | **★外れ** |
| **P2** `off` は真cold でハング | 90% | **`0xb1d00005`** | **★的中** |

★**P1 を 55% にした理由（§3.3(A)：「必要」と「十分」は別）がそのまま起きた**。
**§3.3(B) の宣言どおり、P1 が外れても M1 は撤回しない**（diag が直接測っている）。
**もし「M1 が真なら P1 も真」と書いていたら、ここで «pmu_init が真因» という
誤った因果を恒久記録に焼き付けていた。**

### 4.4 ★効いた最小集合（依頼 (b)）と、**私が途中で犯した誤り**

**最小集合＝2要素**（`software_init_hook`＝`.data`/`.bss` 初期化後・`sta_ker` 前・`sio` 前）：

1. **ICG preinit（`esp_shim_modem_icg_init()`）＋ regi2c マスタのクロック
   （`_regi2c_ctrl_ll_master_enable_clock(true)` ＋ `regi2c_ctrl_ll_master_configure_clock()`）
   を «PLL 設定より前» へ持ってくる**
   — ★ASP3 は**これらを既に持っていた**が、呼ぶのが BT/WiFi 初期化時（main_task＝**ずっと後**）
   だけで、**stock と順序が逆**だった。stock は `rtc_clk_init()` の**最初の行**で呼び、理由を
   明記している：*"…because the system clock source (PLL) in the system boot up process
   needs to use the i2c master peripheral."*
2. **CPU/SOC ルートクロック → PLL@160MHz**（`rtc_clk_cpu_freq_mhz_to_config` ＋
   `rtc_clk_cpu_freq_set_config`）

**★最小集合に «入らない» もの（実測で除外）**：
- **MSPI HS 分周比（`clk_ll_mspi_fast_set_hs_divider(6)`）**：#12 で**外しても真cold D-1 通過**。
  ⇒ **★私は #8 の失敗を «MSPI 未設定による flash XIP 死» と書きかけたが、それは誤り**だった。
  #8 と #9 は**同じ原因（regi2c BBPLL 較正スピン）**で、私は #8 を JTAG せずに
  `STORE3=0` から «死んだ» と**推論**していた。**最小集合を «外して測る» 規律が自分の誤りを捕まえた。**
  ★**stock 準拠のため本ステップは残す**（`ESP32C6_COLD_CPU_PLL_NO_MSPI=ON` で外せる）が、
  **«必要と実証されてはいない»** と明記する。
- `recalib_bbpll()`：真cold では **`src != PLL` で分岐ごと skip される**＝**そもそも走らない**。
  ⇒ evidence-03 §5.2 の推奨順2 は、**真cold では条件が成立しないので効きようがなかった**。
  （**皮肉にも、その skip を記録した診断マーカが真因を暴いた。**）

### 4.5 ★★§20 `pmu_init` の «ゴミポインタ» は **実在する別バグ**（実測）

**真cold の STORE8（`hardware_init_hook`＝`.data` 前の `PMU_instance()->hal`）**：

| run | STORE8（.data 前） | STORE9（.data 後） |
|---|---|---|
| #1 | **`0x0e1ac94b`** | `0x40819348` |
| #2 | **`0x2e1ac943`** | `0x40819348` |
| #4 | **`0x2e0a4b63`** | `0x40819348` |

⇒ **真cold では毎回ゴミ（かつ毎 POR 別値＝ランダム SRAM）**、`.data` 後は常に正しい。
∴ **§20 が `hardware_init_hook` から呼ぶ限り、`pmu_init()` は
`ctx->hal->dev` が PMU(`0x600B0000`) を指さないまま PMU 記述子を書いていた**
（＝**PMU は真cold で一度も設定されていなかった**）。**これは実バグ**であり
`ESP32C6_PMU_INIT_LATE`（既定 ON）で修正した。
**ただし §4.1 #2 のとおり、これを直しただけでは真cold は通らない**＝
**真因は別（§4.2）。2つは独立した2件のバグ**。

### 4.6 ★evidence-03 の残ブロッカー（依頼 (d)）

**「evidence-02 の warm アーム(3/9/10) が再現しない」**＝evidence-03 の仮説は
**「リセットのスコープ差（HPSYS 保持 vs EN/RTS で PMU も落ちる）」**だった。

- **私の実測**：`--after hard-reset`（USB-JTAG）＝**`rst:0x15 (USB_UART_HPSYS)`**（#13 で採取）
  ＝**PCR 保持＝CPU は PLL/160 のまま**（STORE5=`0xbb110a01`）＝**通る**。
- **∴ 仮説は «機序» を得た**：**保持されるのは PMU だけでなく PCR の `SOC_CLK_SEL`**。
  **PCR ごと落ちるリセット（EN/RTS）なら CPU は XTAL@40MHz で上がる＝cold と同じ挙動**になる。
  ⇒ **両ラウンドは無矛盾に説明できる**。
- **★ただし «evidence-02 が実際にどのリセット経路だったか» は私は測っていない**
  ＝**推定であって実測ではない**（正直な限定）。決定打は
  「EN/RTS リセット直後に STORE5 を読む」1 run（**未実施**・§6.3）。

### 4.7 ★真cold での W1 / BLE D-1（依頼 (e)）

| 項目 | 結果 |
|---|---|
| **BLE D-1（真cold）** | **★達成**（`0xb1d00008`・STORE7=`0xa1020704`）＝**C6 BLE が真cold で成立したのは初**（§13/§14/§15 は**全て warm**＝memory の「真cold未検証」留保どおりだった） |
| **WiFi（真cold）** | **★`wifi_scan` 完走**：`16 APs found (err=0)`・`RESCAN 20/15/19 APs` |
| **W1（GOT IP + ping）** | **未実施**（§6.3）＝lwIP＋認証情報が要る別ビルド。**`wifi_scan` は evidence-03 が cold baseline を持つ «直接の対応物» なので、まずそちらで対照を取った** |

### 4.8 ★非回帰（依頼 (f)）

| 項目 | 結果 |
|---|---|
| **BT warm** | **`0xb1d00008`＝D-1**（#13）＝**非回帰** |
| **warm の clk 経路** | `0xbb110a01`（既に PLL/160）→ `rtc_clk_cpu_freq_set_config()` は BBPLL を止めず freq 設定のみ＝**実質 no-op** |
| **hal 参照ビルド**（`ESP32C6_BT_IDF61=OFF`） | **ビルド通過**（7構成スイープ）。**実機非回帰は未実施**（§6.4）。`ESP32C6_COLD_CPU_PLL` は `(ESP32C6_WIFI OR ESP32C6_BT)` ゲート・**OFF で従来と完全に同一**（nm で `esp_shim_cold_cpu_clk_init` が 0 個＝実測） |
| **素のビルド**（WiFi/BT 両OFF） | **ビルド通過**。nm で `esp_shim_cold_cpu_clk_init` が **0 個**＝ゲートが効いている（`rtc_clk.c` も積まない＝PHY も使わない） |
| **`ble_host_smoke_c6`**（既定ON の別アプリ） | **ビルド通過**。実機は未実施 |

---

## 5. 変更ファイル（すべてガード付き・可逆。`asp3_core`/`hal`/`esp-idf` は**無編集**）

| file | 内容 |
|---|---|
| `asp3/target/esp32c6_espidf/cold_clk_init_c6.c` | **新規**。`esp_shim_cold_cpu_clk_init()`＝stock `rtc_clk_init()` の移植（ICG/regi2c → MSPI → PLL@160）。**WiFi・BT 共有**（同じ真cold ハングが両経路で起きるため BT 専用にしてはいけない） |
| `asp3/target/esp32c6_espidf/target_kernel_impl.c` | `software_init_hook` から上記を呼ぶ。`pmu_init` の呼出しを `.data` 後へ（`ESP32C6_PMU_INIT_LATE`）。診断（`ESP32C6_PMU_DIAG`・`ESP32C6_COLD_SETTLE_MS`） |
| `asp3/target/esp32c6_espidf/target.cmake` | option 5 本（下表） |
| `asp3/target/esp32c6_espidf/esp_wifi.cmake` | `rtc_clk.c` を `NON_OS_BUILD=1` へ＋ROM regi2c パッチ追加（**実測**：修正が PLL 較正経路を実際に呼ぶため、従来 `--gc-sections` で落ちていた `regi2c_ctrl_write_reg` が未解決になる。esp_bt_idf61.cmake と同一解法） |
| `asp3/target/esp32c6_espidf/bt/bt_pmu_init_c6.c` | `esp_shim_bt_pmu_diag()`（診断）・`esp_shim_cold_recalib_bbpll()`（既定OFF・真cold では skip されると実測） |

| option | 既定 | 意味 |
|---|---|---|
| **`ESP32C6_COLD_CPU_PLL`** | **ON** | **★本ラウンドの修正**。OFF＝逆方向対照（#11 で再ハングを実証） |
| `ESP32C6_PMU_INIT_LATE` | **ON** | `pmu_init()` を `.data` 後で呼ぶ（§4.5 の実バグ修正） |
| `ESP32C6_COLD_CPU_PLL_NO_MSPI` | OFF | MSPI 段だけ外す（最小集合の判別用．#12） |
| `ESP32C6_PMU_DIAG` | OFF | `PMU_instance()->hal` を STORE8/9 へ |
| `ESP32C6_COLD_SETTLE_MS` | 0 | 整定時間の判別用（#4 で反証済） |
| `ESP32C6_COLD_RECALIB_BBPLL` | OFF | stock `recalib_bbpll()` 相当（真cold では skip＝#6） |

---

## 6. 結論・申し送り

### 6.1 一言

**ASP3-C6 の «真cold で phy_init がハングする» は直った**（BT D-1・WiFi scan とも真cold 通過、
逆方向対照つき、warm 非回帰つき）。**真因は「ROM が PLL を設定済みという warm 由来の思い込み」**。

### 6.2 ★方法論の収穫（再利用可能）

1. **★「stock がやっていることを我々がやめた」型は、コメントに «やらない理由» が
   書いてある所を疑え**。今回の犯人 `target_kernel_impl.c` の「追加のレジスタ操作は
   不要かつ行うべきでない」は、**実測に基づく正しい記述だった——warm では**。
   **«実測で確認した» と書いてある前提ほど «どの条件で測ったか» を確認する。**
2. **★診断マーカは «失敗の理由» を測るために置いたものが «真因» を暴くことがある**。
   `recalib_bbpll` の生還マーカ（本来は「XIP で死んだか」を区別するためだけのもの）が
   `src != PLL` を記録し、そこから全部が解けた。**「効かなかった」で終わらせず
   «なぜ効かなかったか» を1ビットでも記録する。**
3. **★「A を入れたら動いた」を因果にするには «A を外して再現» が要る**（#10 vs #11、#14 vs #15）。
   **さらに «A の中の何が効いたか» には «A の一部を外して測る» が要る**（#12）＝
   これが**私自身の «MSPI/XIP 死» という誤った物語を捕まえた**。
4. **★JTAG は «reset せず attach» で真cold のハング位置を直接読める**（#5・#9）。
   **`resume` を挟んで2回 halt** し、PC が同じ（#5）／違う（#9）を見れば
   «本当にスピンか» が判る（rigor 標準 5th recurrence 対策）。
5. **★センチネル（`0xCAFE5A9C` → 電源断 → `0`）は «真cold» と «マーカ非stale» を
   1回の読みで同時に証明する**。cold の A/B では **UART を一度も開かない**のが最も安全。

### 6.3 ★再現性と本個体の最終状態

- **真cold D-1 は独立 2/2 で再現**（#10 と、最終確認 run。両方 sentinel=`0`＝真cold 証明、
  `STORE0=0xb1d00008`・`STORE7=0xa1020704`・clk `0xbb110280`→`0xcc110a01`）。
- **真cold の `STORE5=0xbb110280`（XTAL/40MHz）は独立 4 ブートで再現**（#6・#7・#8/#9・最終）。
- **本個体の最終 flash＝`build/c6_e04_cpupll`**（＝既定オプション＋`TOPPERS_C6_BT_D1_TRACE`．
  **真cold で D-1 に到達する既知良好ビルド**）。
- ★**測定上の注意（実際に踏みかけた）**：`TOPPERS_C6_BT_D1_TRACE` **無し**のビルドでは
  `BT_D1_TRACE(stage)` が `((void) 0)` へ潰れるので **`STORE0` は 0 のまま**＝
  **«ハング» ではない**。**`STORE0=0` を失敗と読む前に、そのビルドがトレース有効か
  （`grep TOPPERS_C6_BT_D1_TRACE <build>/CMakeCache.txt`）を確認すること**
  （このとき `STORE7=0xa1020704` が «実は成功している» ことを示していた）。

### 6.4 ★残ブロッカー／ユーザー判断事項

1. **W1（GOT IP + ping）は未実施**＝lwIP＋認証情報の別ビルドが要る。
   **`wifi_scan` が真cold で 16-20 AP を返す**ところまでは実証済み＝**PHY/RF は通っている**。
2. **hal 参照ビルド（`ESP32C6_BT_IDF61=OFF`）の «実機» 非回帰は未実施**
   （ビルドは通過。`ESP32C6_COLD_CPU_PLL` は `(WIFI OR BT)` ゲートなので hal 経路にも効く）。
3. **evidence-02 の warm アーム非再現**は**機序を得た（§4.6）が実測していない**：
   **「EN/RTS リセット直後に STORE5 を読む」1 run で決着する**（`0xbb110280` なら確定）。
4. **`ESP32C6_COLD_CPU_PLL` の既定 ON を «全アプリ» で回帰させていない**
   （検証は `bt_smoke_c6` と `wifi_scan`）。
5. **★`ble_host_smoke_c6.c` は `LP_AON_STORE4`(`0x600B1010`) を割込みレートのミラーに
   使っているが、C6 では `STORE4 = RTC_XTAL_FREQ_REG`**（`esp_rom/esp32c6/rom/rtc.h:62`）
   ＝**`rtc_clk_xtal_freq_get()` が読む本物のレジスタを上書きしている**。
   本ラウンドの実測では真cold で `0x00280028`(=40MHz) と正常だったため**今回の真因ではない**が、
   **潜在バグとして申し送る**（同様に `STORE1 = RTC_SLOW_CLK_CAL_REG`）。

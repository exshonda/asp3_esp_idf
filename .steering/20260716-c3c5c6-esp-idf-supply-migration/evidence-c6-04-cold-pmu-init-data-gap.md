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

## 4. 実機測定（2026-07-17）

（実機実施後に記入）

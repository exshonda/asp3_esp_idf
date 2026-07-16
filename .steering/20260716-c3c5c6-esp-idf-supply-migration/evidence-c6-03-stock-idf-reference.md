# C6 evidence-03 — **stock ESP-IDF を参照機にして「ASP3 側か／個体・品種側か」の帰属を決める**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration` ／ 前段 commit: `ab108bf`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`**（hub `1-6` port2）
前段: `evidence-c6-02` が **BLOCKED**（この個体は何を焼いてもハング＝対照が落ち帰属不能）

---

## 1. 目的（1行）

**stock の ESP-IDF v5.5.4 サンプルを «同じ個体» で走らせ、
「blob・シリコン・個体が悪い」のか「ASP3 の統合が悪い」のかを分離する。**

★これは **「ASP3 を直す」タスクではない**。**「どちらの側か」を決める**タスク。
どちらに転んでも価値がある（動く⇒ASP3側に局在／動かない⇒ASP3無罪でboard C要求）。

---

## 2. 着手前に**自分で実測**した事実（＝予測の土台。予測ではない）

引き継がれた事実を鵜呑みにせず再測した（rigor 標準「引き継がれた事実も疑え」）。

### 2.1 DUT の同定（★親プロンプトの記載を独立に追認）

```
Chip type:  ESP32-C6FH4 (QFN32) (revision v0.2)
Features:   Wi-Fi 6, BT 5 (LE), IEEE802.15.4, Single Core + LP Core, 160MHz, Embedded Flash 4MB
BASE MAC:   14:c1:9f:e0:5a:9c
```
⇒ **rev v0.2・内蔵Flash 4MB＝追認**。~~board C は rev v0.3（evidence-02 §5.5）。~~
**★【2026-07-17 訂正】この一文は誤り**：**`14:C1:9F:E0:5A:9C` こそが board C 本体**であり
（`docs/wifi-shim-c6.md` に同 MAC が8回）、**rev v0.3 という一次情報は存在しない**
（同 doc に `v0.3` は0件）。**出所＝`evidence-c6-02:195` が «efuse blk_version» を
«chip revision» と取り違えた**（stock `efuse_hal.h` は両者を別APIとして持つ）。
∴ **本DUT＝board C＝rev v0.2** であり、**rev 差という交絡は存在しない**。
詳細＝`evidence-c6-06-w1-and-v554-d2b.md` §2。

### 2.2 ★tree の同定

| tree | HEAD | 版 | dirty |
|---|---|---|---|
| submodule `esp-idf/` | `735507283d` | **`v5.5.4` タグ（真の v5.5.4）** | **clean** |
| `hal/` | `b90b1837` | esp-hal-3rdparty | — |

### 2.3 ★★C6 blob md5 — **stock(v5.5.4) ≡ hal はバイト一致**（私が独立に再実測）

| file (`esp32c6/`) | submodule `esp-idf/`(v5.5.4) | `hal/` | 一致 |
|---|---|---|---|
| **`libphy.a`** | **`cb429107787d88023983668c9b161b56`** | **`cb429107787d88023983668c9b161b56`** | **★バイト一致** |
| **`libble_app.a`** | **`75db98e5139162fa60583becb38ea0a1`** | **`75db98e5139162fa60583becb38ea0a1`** | **★バイト一致** |

⇒ **これが本ラウンドを «blob 次元では単一変数の対照» にする**：
evidence-02 アーム12（`wifi_scan` hal fallback＝**同じ libphy `cb429107`**）が
phy_init で落ちている。**stock が同じ `cb429107` で phy_init を通れば、
「この libphy バイト列が rev v0.2 で動かない」は «同一バイト列» で反証される**（版差の交絡が無い）。

### 2.4 ★IDF v5.5.4 は rev v0.2 を明示サポート（静的事実）

`components/esp_hw_support/port/esp32c6/Kconfig.hw_support`:
```
choice ESP32C6_REV_MIN
    default ESP32C6_REV_MIN_0          ← 既定の最小サポート = v0.0
    config ESP32C6_REV_MIN_2  bool "Rev v0.2"    ← ★v0.2 は «名前付きの選択肢» として実在
config ESP32C6_REV_MAX_FULL  int  default 99     ← 最大 v0.99
```
⇒ **「rev v0.2 は IDF v5.5.4 の対象外」は a priori には成立しない**（＝予測を PASS 寄りにする材料）。
★ただしこれは「IDF が boot を拒否しない」ことの根拠であって、
**「PHY 較正が v0.2 で収束する」ことの根拠ではない**（別レイヤ。混同しない）。

### 2.5 サンプルの実在と C6 サポート（静的事実）

| example | Supported Targets に C6 | 挙動 |
|---|---|---|
| `get-started/hello_world` | （全ターゲット） | **~10秒ごとに `esp_restart()` でループ**＝後から capture を開いても全ブートを拾える |
| `wifi/scan` | **ESP32-C6 明記** | パッシブscan＝**認証情報不要** |
| `bluetooth/nimble/bleprph` | **ESP32-C6 明記** | — |

---

## 3. ★★実機前に固定する予測（★ここから下は実機を1回も触らずに書いた）

### 3.1 予測

| # | 段 | **予測** | 確度 | 根拠 |
|---|---|---|---|---|
| **P1** | `hello_world` | **PASS** | **90%** | evidence-02 で **ASP3 は `I (83) phy_init:` まで到達＝CPU/RAM/flash/console は動く**。RF に触らない stock が落ちる理由が無い。★情報量は低い（パイプライン健全性の確認） |
| **P2** | **`wifi/scan`（★本命）** | **PASS** | **60%** | PASS 寄り: §2.4（v0.2 は明示サポート）・C6FH4 は量産品・**C6 実施90-91 の PMU ICG 前例＝「stock がやっていることを ASP3 がやめていた」型のバグが実在**／HANG 寄り: evidence-02 は esp-idf 供給の `wifi_scan` でも落ちた・v0.2 は初期revision |
| **P3** | `bleprph` | （P2 が PASS なら**走らせない**） | — | P2 で帰属が決まるなら BT は不要（親指示：「3 まで行く必要が無いなら行くな」） |

### 3.2 ★含意表 — **「反証条件も仮説である」を自問した結果**

★親指示 §4.9：**「A ⇒ B」と書く前に、その含意が本当に成立するか自問しろ**。
以下は**自問して «健全な部分» と «健全でない部分» を分けた**もの。

#### (A) stock `wifi/scan` が **PASS** した場合

| | 主張 | 健全か |
|---|---|---|
| **A-1** | この個体・この rev v0.2 シリコンは **phy_init を完走できる** | **★健全**（実測そのもの） |
| **A-2** | **libphy `cb429107` は rev v0.2 で動く** | **★健全**＝§2.3 でバイト一致を確認済み＝版差の交絡なし。**evidence-02 アーム12（同一 libphy）の phy_init ハングは «blob のせい» では説明できなくなる** |
| **A-3** | 「個体の RF が物理的に死んでいる」 | **★反証される** |
| **A-4** | 原因は **stock と ASP3 の «差分全体»** に在る | **健全だが粗い**＝ Direct Boot / clock init / PMU / modem clock init / phy_init 呼出し経路 の**どれか**。**有界だが1点ではない** |
| **A-5** | 「**ASP3 のグルーが犯人**」 | **★健全でない＝断定禁止**。これが親指示の「正直な限定」。stock は FreeRTOS も 2nd-stage bootloader も IDF フル startup も連れてくる＝**単一変数の対照ではない** |
| **A-6** | 「board C(v0.3) で ASP3 が動き本DUT(v0.2) で動かないのは、**stock がやっている rev 依存の init を ASP3 が省いている**から」 | **★仮説にすぎない**（整合的で有望だが**未実証**）。次ラウンドの調査対象であって、本ラウンドの結論ではない |

#### (B) stock `wifi/scan` が **HANG** した場合（★`hello_world` PASS が前提）

| | 主張 | 健全か |
|---|---|---|
| **B-1** | **ASP3 のグルーは «ハングに必要» ではない** ⇒ **ASP3 無罪** | **★健全**（stock に ASP3 グルーは1バイトも無い） |
| **B-2** | 「**この個体が壊れている**」 | **★健全でない**＝ 対抗仮説が潰せていない: (i) 品種 C6FH4 固有, (ii) **基板の RF フロントエンド/アンテナ損傷**, (iii) 私の sdkconfig 誤り。**個体 vs 品種/rev の分離には board C(v0.3) が要る** |
| **B-3** | 次の行動＝**ユーザーへ board C 接続を依頼して止まる** | **★健全**（HANDOFF §8「無理に成功を作らない」） |

★**`hello_world` PASS が (B) の前提**である理由：hello_world が落ちると
「私の stock ビルド/flash 手順が誤り」と「個体が悪い」を分離できない。
∴ **P1 は情報量が低くても «(B) を解釈可能にするために必須»**。

### 3.3 ★測定の作法（本ラウンドで守るもの）

| 罠 | 対策 |
|---|---|
| `rst:0x1 (POWERON)` は真coldの証明にならない | **真coldの証明は `uhubctl -a off` ＋ by-id 消滅の読み戻しのみ** |
| 途中 open が DUT をリブートさせる（C5「存在しないバグ捏造」事故） | **capture を «測定対象のブートが始まる前» に1回だけ open して保持** |
| stale マーカ | **stock に LP_AON 計装は無い＝console 判定**。よって上の「open 先行・保持」が唯一の防衛 |
| grep がバイナリモードで無言全滅 | **`grep -a` 必須** |
| コンソール氾濫が成功も失敗も隠す（C6 実施03 誤診） | 全文を保存して行数も数える |
| **未検証の判別指標**（evidence-02 §5.3 で実際に踏んだ） | **stock の判定は «scan 結果の AP 行» という «仕様で定義された成功出力» のみ**を使う |

### 3.4 ★真cold と capture 開放の両立不能性（先に明示しておく）

本DUT は **USB-JTAG も CP2102N も «DUT と同じ hub port2» から給電**される。
∴ `uhubctl off` は **capture プロセスごと殺す**＝**「真cold ＋ live capture」は原理的に不可能**。

**しかしこれは本ラウンドの結論を弱めない**：evidence-02 は
**ASP3 が cold でも warm でも落ちる**ことを実測している（アーム 2/3・9/10）。
∴ **stock が «cold か warm のどちらかで» 通れば A-1/A-2 は成立する**
（＝「このシリコンは phy_init を完走できる」の証明に電源状態は効かない）。
∴ **主測定＝「capture を先に開いて保持 → EN リセットで boot」**（作法 §3.3 を完全に満たす）。
**真cold は補助的に別途実施**（`hello_world` は自己再起動ループなので後から open でも全ブートが拾える）。

---

## 4. 実機測定（2026-07-17）— **★帰属は決まった＝ASP3 側**

### 4.0 結論（先に3行）

1. **★真cold で「stock は通り／ASP3 は落ちる」を同一個体・同一 libphy バイトで実測した**
   ＝**シリコン・個体・rev v0.2・blob は «無罪»。帰属は ASP3 側**（＝本ラウンドの目的を達成）。
2. **★真の変数は «cold(POR) か warm か» であって «個体» ではなかった**。ASP3 は
   **真cold で WiFi も BT も落ち／warm では両方通る**。∴ evidence-02 の
   「この個体は何を焼いてもハングする」は**「ASP3 は真cold で落ちる」の誤読**。
   **board C の D-1/D-2b 成功は全て warm（memory：真cold未検証）＝矛盾しない。
   ∴ rev v0.2/v0.3 仮説は «不要» になった**（＝evidence-02 が «原因ではない» と
   留保していたのは正しかった）。
3. **§20 の `pmu_init` は «リンク済み・既定ON» のまま真cold 2/2 でハング**
   ＝**pmu_init だけでは足りない**。残りは **2nd-stage bootloader の
   `rtc_clk_init()`（modem ICG preinit＋regi2c アナログトリム）ほか**（§5 の差分表）。

### 4.1 ★測定マトリクス（**すべて本個体 `14:C1:9F:E0:5A:9C`・本セッション・私が実測**）

真cold の証明＝**`uhubctl -l 1-6 -p 2 -a off` ＋ by-id 消滅の読み戻し（毎回 0 を確認）**。

| # | build | 供給/blob | **真cold** | **warm** | 物証 |
|---|---|---|---|---|---|
| 1 | **stock `hello_world`** | v5.5.4 | （自己再起動ループで健全） | **PASS** | `Hello world!`×2・`Chip rev: v0.2`・heap 473128B |
| 2 | **★stock `wifi/scan`** | **libphy `cb429107`** | **★PASS（17 APs）** | **PASS（18 APs）** | `I (477) phy_init: phy_version 343,b513b46` **完全行**・`Total APs scanned = 18`・`Returned from app_main()` |
| 3 | **ASP3 `wifi_scan`（hal）** | **libphy `cb429107`（＝#2と同一バイト）** | **★HANG（0 byte・RESCAN 0行）** | **PASS（RESCAN 10行・err=0・14-18 APs）** | cold は t=+1.00s から12秒 capture して**無出力**（warm は同秒数で約10行） |
| 4 | **ASP3 BT v6.1 `c6u_v61_after`**（＝evidence-02 **アーム2の対照**・`ESP32C6_BT_PMU_INIT=ON`） | v6.1 | **★HANG `0xb1d00005` 2/2** | **★D-1 成功 `0xb1d00008`** | cold: `0x600B1000=0xb1d00005`＋`0x600B101C=0x00000000`／warm: `0xb1d00008`＋`0xa1020704`＋console `Phase D-1 milestone reached` |

**マーカ意味**（`apps/bt_smoke_c6/bt_smoke_c6.c`）：`0x600B1000 = 0xB1D00000|stage`。
**stage5＝`esp_bt_controller_enable()` 直前で停止**／**stage8＝D-1達成（Command Complete
受信でしか到達しない）**。`0x600B101C`＝intr trace（`0xa1020704`＝成功／`0`＝未到達）。
**マーカが stale でない証明＝電源断で LP_AON は 0 に消える**（evidence-02 §6.5・本ラウンドでも
`by-id 消滅`を毎回読み戻し）。

### 4.2 ★★帰属を決める1行（依頼 (b)）

> **同一個体・同一 libphy バイト列（`cb429107`）・同一の真cold 条件で、
> stock は `phy_init` を完走して 17 AP を受信し、ASP3 は `phy_init` で停止する。**

∴ **シリコン／個体／rev v0.2／libphy blob は無罪。帰属は ASP3 側。**
（#2 と #3 は **libphy がバイト一致**＝blob 次元は単一変数。§2.3 で事前に確認済み。）

### 4.3 ★予測の答合せ（§3.1・依頼 (c)）

| # | 予測 | 確度 | 実機 | 判定 |
|---|---|---|---|---|
| **P1** `hello_world` | **PASS** | 90% | **PASS** | **★的中** |
| **P2** `wifi/scan` | **PASS** | 60% | **PASS（cold 17 / warm 18 APs）** | **★的中** |
| **P3** `bleprph` | P2 PASS なら走らせない | — | **走らせていない** | **★宣言どおり**（親指示「3 まで行く必要が無いなら行くな」） |

★**今回は «対照が落ちる» という前提崩れが起きなかった**ので、evidence-02 §5.2 と違い
**予測を «的中» と記録してよい**（stock が両電源状態で通った＝参照機として機能した）。

### 4.4 ★★私が本ラウンドで犯した誤りと、それを捕まえた反証実験（正直に記録する）

**rigor 標準「相関を因果と早合点しない・反証実験を先に」が2回とも仕事をした。**

#### 誤り(1)：「evidence-02 のアーム12（ASP3 WiFi 完走せず）は再現しない＝前ラウンドの事実誤り」

- **私は ASP3 `wifi_scan` を warm で走らせて «10行 RESCAN・完走» を見て、
  「前ラウンドの «WiFi も完走しない» は誤り」と結論しかけた。**
- **★誤り**：evidence-02 のアーム11/12 は **真cold**、私が測ったのは **warm**＝**条件が違う**。
- **反証実験**：**ASP3 `wifi_scan` を真cold で走らせた → 0 byte・RESCAN 0行＝ハング**
  ＝**evidence-02 は正しかった。私の «反証» を撤回する**（#3 の行）。
- ★教訓：**「再現しない」と言う前に «相手と同じ条件か» を確かめる**。

#### 誤り(2)：「ASP3 は stock の初期化残留に依存している（stock を焼いた後だから動いた）」

- **私は「stock を間に焼いたから ASP3 BT が D-1 に到達したのだ」＝ «stock 残留依存»
  という筋の良い仮説を立てた**（C6 実施88-91 の «NuttX warm 残留依存» と同型なので
  非常に説得的だった）。**これを結論として書きかけた。**
- **反証実験（先に走らせた）**：**電源断後、stock を一切走らせずに warm リセットだけで
  ASP3 BT を起動 → `0xb1d00008`＝D-1 成功**。
  ⇒ **«stock 残留» は不要＝仮説は棄却**。真の変数は **cold か warm か**。
- ★**この控えを取らずに書いていたら、«stock 残留依存» という «存在しない機序» を
  恒久記録に焼き付けていた**（C5「存在しないバグを捏造」事故の同型）。

#### 誤り(3)：「§20 の `pmu_init` はリンクされていない」（危うく記録するところだった）

- `find ... -name "*.elf" | head -1` が**中間生成物 `cfg1_out.elf` を掴み**、`pmu_init` 0個と出た。
- **確認**：真の `asp.elf` には `4200590a T pmu_init`・`esp_shim_bt_pmu_init` が**在る**。
  ⇒ **§20 は «リンク済み・既定ON» で、それでも真cold ハング**（＝これが正しい記録）。

### 4.5 ★evidence-02 との突合せ — **どこが一致し、どこが未解決か**

| evidence-02 の観測 | 本ラウンド | 判定 |
|---|---|---|
| アーム2：v6.1 **真cold** ハング | **再現（`0xb1d00005` 2/2）** | **★一致＝evidence-02 は正しい** |
| アーム11/12：`wifi_scan` **真cold** 完走せず | **再現（0 byte）** | **★一致＝evidence-02 は正しい** |
| §5.5「rev v0.2 vs v0.3 が測定された差」 | **原因ではなかった**（cold/warm が真の変数） | **★evidence-02 の «原因ではない» という留保が正しかった** |
| **アーム3/9/10：v6.1・historical を warm でハング 2/2** | **★再現しない**（私の warm は **D-1 成功 `0xb1d00008`**） | **★未解決の相違** |

**★未解決（因果は主張しない）**：evidence-02 の **warm アームだけ**が再現しない。
**測定された差**＝**リセットの «種類»**：私の warm は `--after hard-reset`（USB-JTAG 経由）で
**`rst:0x15 (USB_UART_HPSYS)`＝HPシステムのみのリセット**（PMU/アナログ域は保持）。
memory は「**C6 は watchdog_reset 非対応→esptool が RTS ハードリセットへ自動フォールバック**」
と記録しており、**RTS＝EN ピン駆動なら PMU/アナログ域ごとリセット＝cold 相当**になり得る。
⇒ **「warm には «PMU を保持する warm»（HPSYS）と «PMU も落ちる warm»（EN/RTS）の2種類が
あり、後者は cold と同じ挙動になる」という仮説で両ラウンドは無矛盾に説明できる**が、
**本ラウンドでは実測していない＝仮説のまま申し送る**（次ラウンドで `rst:` 理由を
必ず記録すれば1回で決まる）。

---

## 5. ★ASP3 と stock の差分＝次ラウンドの調査対象（依頼 (e)）

### 5.0 ★探索空間は «cold で効く初期化» に絞り込めた（＝ここが本ラウンドの実利）

**warm では ASP3 は WiFi も BT も通る**＝**前のブートが残した PMU/アナログ/クロック状態が
在れば ASP3 は動く**。**真cold（POR 既定値）だけで落ちる**＝
⇒ **欠けているのは «POR 状態から立ち上げるための初期化» に限定される**。
これは §3.2 A-4 の「差分全体（有界だが1点でない）」を**大幅に絞った**もの。

### 5.1 ★stock が呼び、ASP3 が呼んでいない初期化（**実測：nm とソース grep**）

| stock の呼出し | どこで走るか | ASP3 | 備考 |
|---|---|---|---|
| **`rtc_clk_init(clk_cfg)`** | **★2nd-stage bootloader**（`bootloader_clock_configure`→`bootloader_clock_init.c:83`） | **★呼出し 0＝完全に欠落** | **Direct Boot は bootloader ごと飛ばす**。中身＝下の2件 |
| ├ `rtc_clk_modem_clock_domain_active_state_icg_map_preinit()` | 同上（`rtc_clk_init` 内・static） | **★欠落** | **PMU HP_ACTIVE の modem ICG code＋MODEM_APB/I2C_MST/LP_APB のクロックゲート解除**。**C6 実施90-91 で «真因» と確定した族そのもの** |
| └ `REGI2C_WRITE_MASK(I2C_DIG_REG, I2C_DIG_REG_SCK_DCAP, …)` 等 RC_FAST/RC_SLOW/RC32K トリム | 同上 | **★欠落** | **regi2c 経由のアナログトリム**＝cold でのみ効く類 |
| **`recalib_bbpll()`** | app 起動（`esp_rtc_init`・`CONFIG_ESP_SYSTEM_BBPLL_RECALIB=y`・**PASS したバイナリにリンク実測**） | **★呼出し 0** | stock 自身のコメント：**「bootloader が用意した PLL は十分に安定でない。ここで再較正する」**。★ASP3 は「ROM が 160MHz に設定済みだから触らない」と**明示的に «やらない» 判断**をしている（`target_kernel_impl.c`）＝**「stock がやっていることを我々がやめていた」型** |
| **`pmu_init()`** | app 起動（`esp_rtc_init`） | **§20 で移植済み・既定ON・リンク実測** | **★それでも真cold ハング 2/2 ＝ pmu_init «だけでは足りない»**（本ラウンドの実測。evidence-02 アーム2/5 と一致） |
| **`esp_clk_init()`** | app 起動 | **★呼出し 0** | 中身に `rtc_clk_8m_enable(true)`・`rtc_clk_fast_src_set(RC_FAST)`・`select_rtc_slow_clk()` を含む |
| ├ `rtc_clk_8m_enable(true)` / `rtc_clk_fast_src_set()` | 同上 | **★欠落** | RC_FAST(8M) 有効化 |
| ├ `select_rtc_slow_clk()` | 同上 | **★欠落** | RTC_SLOW 源選択 |
| └ `modem_clock_deselect_all_module_lp_clock_source()` | 同上 | **部分的に有り**（`wifi/esp_wifi_adapter.c:716`＝**WiFi 経路のみ・BT 経路には無い**） | ★WiFi/BT で非対称 |
| **`esp_perip_clk_init()`** | app 起動（`cpu_start.c:792`） | **★呼出し 0**（ASP3 のコメント自身が「Direct Boot では呼ばれない」と明記し、断片的に手当て） | `esp_wifi.cmake:870`・`esp_wifi_adapter.c:701` |

### 5.2 ★次ラウンドの推奨順（安い順・**因果は未主張**）

1. **`rtc_clk_init()` 相当（modem ICG preinit＋regi2c トリム）を Direct Boot に補う**
   ＝**最有力**。理由：(i) **bootloader 側なので ASP3 には «一片も» 無い**、
   (ii) **C6 実施90-91 の真因（PMU ICG modem code）と同じ族**、
   (iii) **cold でのみ効く**（warm は前ブートの設定が残る＝本ラウンドの観測と整合）。
2. **`recalib_bbpll()`** ＝ **BBPLL 再較正**。ASP3 は「ROM 設定を信頼して触らない」と
   明示判断しているが、**stock は «bootloader の PLL は安定でない» として再較正する**。
   **症状が «RF synth PLL がロックしない»** であることと符合する。
3. `esp_clk_init()` の残り（`rtc_clk_8m_enable`・`select_rtc_slow_clk`）／`esp_perip_clk_init()`。
4. **BT 経路に `modem_clock_deselect_all_module_lp_clock_source()` が無い**非対称の解消。

★**いずれも «仮説» であり «原因» ではない**（rigor 標準）。
**判別は安い**：**真cold で `0x600B1000` が `0xb1d00005`→`0xb1d00008` に変われば決まる**
（本ラウンドで手順・マーカ・電源制御は全て確立済み）。

### 5.3 ★「正直な限定」をどう扱ったか（依頼 (d)）

§3.2 A-5 で**事前に**「stock は FreeRTOS も 2nd-stage bootloader も IDF フル startup も
連れてくる＝**単一変数の対照ではない**。∴ «グルーが犯人» と断定してはならない」と宣言した。
**本ラウンドはこれを守っている**：

- **言った**：「stock が通り ASP3 が落ちる ⇒ **帰属は ASP3 側**（＝差分全体）」＝ A-4 の範囲。
- **言っていない**：「グルーが犯人」「`rtc_clk_init` 欠落が原因」。§5 は**調査対象の列挙**であり
  **原因の主張ではない**（親指示「そこまでで止めろ。直すのは次ラウンド」）。
- **★ただし限定は «事前に思っていたより弱くて済んだ»**：
  **warm では ASP3 が通る**という実測が入ったため、探索空間は
  「stock と ASP3 の差分**全体**」ではなく「**POR 状態から立ち上げる初期化**」に絞れた。
  FreeRTOS の有無・bootloader の «存在» そのもの・startup の «構造» は
  **warm で ASP3 が通る以上、犯人ではあり得ない**（warm でも同じだけ違うのだから）。
  **★これは «限定を実測で狭めた» のであって «限定を無視した» のではない。**
- **blob 次元だけは «単一変数» にできた**：#2 と #3 の libphy は**バイト一致**（§2.3）。

---

## 6. 結論（依頼 (a)〜(f)）

### 6.1 各段の実機結果と物証（依頼 (a)）＝§4.1 の表

- `hello_world`：**PASS**（`Hello world!`×2・rev v0.2 を bootloader が受理）。
- **`wifi/scan`：★真cold PASS（17 APs）／warm PASS（18 APs）＝ここで帰属が決まった**。
- `bleprph`：**走らせていない**（P2 で決着＝親指示どおり）。

### 6.2 帰属（依頼 (b)）

**★ASP3 側。** シリコン・個体・**rev v0.2**・**libphy `cb429107`** は**無罪**
（同一個体・同一バイトで stock が真cold 完走）。
**ASP3 は «真cold(POR) からの立ち上げ» に必要な初期化を持っていない**
（WiFi・BT の**両方**で真cold ハング／warm 成功）。

### 6.3 残ブロッカー（依頼 (f)）

1. **evidence-02 の warm アーム（3/9/10）が再現しない**＝**未解決**（§4.5）。
   仮説＝**リセットの種類（HPSYS 保持 vs EN/RTS で PMU も落ちる）**。
   **次ラウンドは `rst:` 理由を必ず記録すること**（1回で決まる）。
2. **4アーム目（blob vs グルー）の帰属は依然 «未決»**。ただし**前提は変わった**：
   **「対照が落ちる個体」ではなく「真cold では全部落ちる／warm では全部通る」**なので、
   **warm で 4アームを回せば交絡なく比較できる**（`build/c6u_v554` は用意済み）。
   ★ただし **warm 比較は «cold の真因» を含まない**ことに注意。
3. **真cold の恒久修正は未着手**（§5.2 が調査対象）。**board C の D-1/D-2b/D-2c 成功は
   全て warm＝真cold 未検証**（memory の留保どおり）＝**C6 BLE は現時点で «cold 未成立»**。

### 6.4 本個体の最終状態

**`build/c6_wifiscan_hal`（ASP3 hal fallback wifi_scan）を書込み済み**（warm で動作・真cold でハング）。
stock のビルドは **scratchpad 配下のみ**（`esp-idf/` submodule は **clean を維持＝`git status` で確認**）。

### 6.5 ★方法論の収穫（再利用可能）

1. **★「開けば chip がリセットされる」は本DUT の CP2102N では «偽»**。
   `stty`＋`cat` で開いても**リセットされない**（hello_world の `rst:0xc (SW_CPU)` ＝
   **アプリ自身の `esp_restart()`** だった）。∴ **capture を先に開いても «測定対象のブート» は
   始まらない**——**別途リセットを撃つ必要がある**（`--before usb-reset --after hard-reset`）。
   ★最初 **0 byte** を掴んだのはこれが原因。**«開けば reset» を未検証のまま前提にしない**。
2. **★真cold ＋ live capture が不可能でも «高速 open» で真cold を観測できる**：
   電源ON→**ポーリングで t=+1.0s に open**（`scratchpad/coldcap.py`）→
   **t=3.05s の scan 結果を捕捉**＝**stock の真cold PASS を console で直接実証**できた。
3. **★console は成功も失敗も隠す（C6 実施03 の教訓が本ラウンドでも発火）**：
   warm の BT 成功ブートで **console には `Phase D-1 milestone reached` が出ていないのに
   マーカは `0xb1d00008`（＝D-1 達成）**だった（console 1221 byte・化けあり）。
   ⇒ **判定は LP_AON マーカ（console 非依存）を主とする**（memory「実施04教訓に従い
   STOREを主判定」を踏襲）。
4. **★`--before no-reset --after hard-reset` はアプリを起動しないことがある**
   （マーカ 0＝stage1 にも到達しない＝**ブート自体が起きていない**）。
   **`--before usb-reset --after hard-reset` を使う**。**「マーカ 0」を «ハング» と読む前に
   «そもそもブートしたか» を疑う**（stage1 マーカが有効な自己検証）。
5. **★中間生成物 `cfg1_out.elf` を掴む事故**：`find -name "*.elf" | head -1` は危険。
   **`asp.elf` を明示せよ**（誤って「§20 未リンク」と記録しかけた）。

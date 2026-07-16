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
⇒ **rev v0.2・内蔵Flash 4MB＝追認**。board C は rev v0.3（evidence-02 §5.5）。

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

## 4. 実機測定

（★以降は実機実施後に追記）

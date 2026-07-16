# C3 evidence-02 — ★真cold クロック監査（**事前登録した予測が的中**）／★**真cold W1 達成**（GOT IP + ping）／hal参照0 の実機非回帰

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `60:55:F9:57:BA:BC`**（hub `1-6` **port1**．書込前に毎回 `read-mac` 照合）
toolchain: Espressif `riscv32-esp-elf` **esp-15.2.0**
前段: `evidence-c3-01`（供給移行＋静的監査．**予測はそこで実機前に commit 済み＝本ラウンドで改竄していない**）

---

## 0. 結論（先に4行）

1. **★事前登録した予測（85%）＝「OPTIONS0 の bits 10/8/6 が cold・warm とも 0」は的中**（cold 独立 2/2）。
   **C3 に C6型の init 欠落は «無い»**＝evidence-c3-01 §2 の静的 PROVEN が実機で追認された。
2. **★真cold で W1 達成**＝**GOT IP（`192.168.1.74`）＋ ping 21/21・49/49 OK（NG 0）**（独立 2/2）。
   **ミッションの完了条件**（W1 が全て esp-idf submodule 供給で実機動作）を **真cold** で満たした。
3. **★hal 参照 0 の実機非回帰**＝`wifi_scan`（esp-idf供給・hal=0）が**真cold で 19 APs・err=0**。
4. **CPU は真cold で 160MHz**（PCCR/SYSTIMER 比 **9.935 ≈ 10.0**）＝「bit=0 ⇒ 動く」の不健全な含意を
   埋めるために用意した**機能側の対照も的中**。

---

## 1. ★予測の答合せ（依頼 (a)）— **予測は evidence-c3-01 §7.2 で実機前に固定済み**

| # | 事前登録した予測 | 確度 | **実機（真cold）** | 判定 |
|---|---|---|---|---|
| **M1** | `RTC_CNTL_OPTIONS0_REG` の **bit10/8/6 = 0 が cold・warm とも** | **85%** | **cold=`0x1c00a000` / warm=`0x1c00a080`＝bits 10/8/6 は両方とも 0,0,0** | **★的中** |
| **F1** | 機能側＝`mcycle`(PCCR)/SYSTIMER 比 **≈10.0 → 160MHz** | — | **9.935 → 159.0MHz** | **★的中** |
| **F2** | ≈6.67 なら BBPLL が実は 320M | — | 該当せず | — |
| **F3** | 値が出ない/0 なら cold で BBPLL 死 | — | 該当せず | — |

### 1.1 生の実測値（マーカ直読み．**コンソールは一度も開いていない**）

| marker | addr | **WARM** | **TRUE COLD #1** | **TRUE COLD #2** |
|---|---|---|---|---|
| STORE0 `OPTIONS0` | `0x60008050` | `0x1c00a080` | **`0x1c00a000`** | **`0x1c00a000`** |
| STORE2 `PCCRデルタ` | `0x60008058` | `0x00026d6a` | **`0x00026d6a`** | **`0x00026d6a`** |
| STORE3 `CLKCFG` | `0x6000805C` | `0x0001000d` | **`0x0001000d`** | **`0x0001000d`** |
| STORE6 `SYSTIMデルタ` | `0x600080C0` | `0x00003e8c` | `0x00003e8d` | `0x00003e8d` |
| STORE7 `STAGE` | `0x600080C4` | `0xc3d00004` | **`0xc3d00004`** | **`0xc3d00004`** |

**ビット単位のデコード**：

```
 bit name                  warm  cold   verdict
  13 XTAL_FORCE_PU            1     1
  11 BBPLL_FORCE_PU           0     0
  10 BBPLL_FORCE_PD           0     0   ★予測どおり 0/0
   9 BBPLL_I2C_FORCE_PU       0     0
   8 BBPLL_I2C_FORCE_PD       0     0   ★予測どおり 0/0
   7 BB_I2C_FORCE_PU          1     0   ★cold/warm で «異なる»（warm残留）
   6 BB_I2C_FORCE_PD          0     0   ★予測どおり 0/0
XOR(warm,cold) = 0x00000080  → 差は bit7 のみ
```

- `CLKCFG=0x0001000d` ⇒ `SOC_CLK_SEL=1`（**PLL**）・`CPUPERIOD_SEL=1`（**160MHz**）＝
  **真coldでもクロックmuxは正しく PLL/160 に入っている**。
- `STAGE=0xc3d00004` ⇒ `software_init_hook` の測定完了まで到達（stage1=hook入口／
  2=PLL切替後／3=software_init_hook／4=周波数測定完了）。

### 1.2 ★★この null 結果は «盲目な読み» ではない（＝測定の感度が実証されている）

**最も重要な補強**：`OPTIONS0` は cold と warm で **bit7（`BB_I2C_FORCE_PU`）が実際に違う**。

- warm＝`1`（**前ブートの PHY 初期化が立てた残留**．ASP3-C3 は PHY を使うと BB I2C を force-PU する）
- cold＝`0`（**POR がリセット既定へ戻した**）

⇒ **`OPTIONS0` は «warm 保持・POR 既定復帰» するレジスタであることが実測で確認された**
（＝**C6 の PCR `SOC_CLK_SEL` と同じ性質を持つ**）。**それでもなお bits 10/8/6 は cold/warm とも 0**。

★これが効く理由＝**「cold と warm が同じ値だった」だけなら «読みが stale／latch が効いていない» 可能性が残る**。
本測定は**同じレジスタの別ビットが cold/warm で変化している**ので、**latch は生きていて，かつ
warm 残留を検出する感度がある**。その上で **BBPLL 電源ビットだけは残留に依存しない**＝
**evidence-c3-01 §2 の «POR 既定が «有効» の側だから cold が安全側» という静的 PROVEN の実機追認**。

---

## 2. ★C3 は真cold で健全か／C6型の init 欠落があるか（依頼 (b)）

**健全。C6型の init 欠落は無い。** 根拠は3つとも独立：

| # | 証拠 | 内容 |
|---|---|---|
| 1 | **レジスタ機構** | `OPTIONS0` bits 10/8/6 = 0（cold 2/2）＝BBPLL は POR 既定で «強制電源断されていない»。§1.2 のとおり測定に感度がある上での 0 |
| 2 | **機能（クロック）** | PCCR/SYSTIMER 比 **9.935 ≈ 10.0 → CPU 159.0MHz**（cold 2/2．warm と**同一値** `0x26d6a`）＝**BBPLL は 480MHz でロックし，CPU は真cold でも 160MHz で回っている** |
| 3 | **機能（RF/上位）** | **真cold で `wifi_scan` 19 APs（err=0）**・**真cold で W1（GOT IP + ping 49/49）**＝PHY/RF/supplicant/mbedtls まで真coldで通る |

**⇒ C6（真cold で ROM が CPU を XTAL@40MHz のまま渡し、ASP3 が «設定済み» と決め打って
phy_init がハング）と C3 は «同型ではない»** と**実機で確定**。
機構の違いは evidence-c3-01 §2 のとおり構造的：

- **C6 のバグ＝mux を «書かない»（warm 残留に暗黙依存）。C3 は `target_kernel_impl.c:91-94` で
  mux を毎ブート «無条件に» 書く**（＝残留に依存する余地が無い）。
- **BBPLL の enable は RTC ドメインで warm 保持・POR 既定復帰**（§1.2 で bit7 が実証）
  **だが，その POR 既定が «有効» の側**（`FORCE_PD` 3bit すべて `default 1'b0`）＝**cold が安全側**。

★**`cold_clk_init_c6.c` は移植しなかった**（evidence-c3-01 §2.4 の自警どおり）。
**移植は不要だっただけでなく有害になり得た**——C3 の BBPLL は既定で上がっており，
C6 が真cold で実際に踏んだ **BBPLL 較正の regi2c 完了待ちスピン**を持ち込む risk があった。
**「効く保証のない予防的修正を入れない」判断が実機で正しかった**ことになる。

### 2.1 ★残る UNKNOWN は変わらず1件（正直に据え置く）

evidence-c3-01 §2.3 行10＝**`rtc_clk_bbpll_configure()` のアナログ regi2c 較正 8本は未適用**＝
BBPLL は**工場アナログ既定**で走っている。**本ラウンドはそれを «常温・この個体・この電源» で
160MHz が出ることを示しただけ**であり、**温度/電圧マージンは依然未検証**。
（cold/warm の問題ではないので本ラウンドのスコープ外．§5 の申し送り。）

---

## 3. ★W1 の可否と物証（依頼 (c)）— **真cold で達成**（SSID はマスク）

**app＝`apps/wifi_dhcp`／供給＝esp-idf submodule（既定．hal=0）／`-DESP32C3_QEMU=OFF`**
認証情報は**ビルド注入のみ**（`-DASP3_EXTRA_COMPILE_DEFS='…;WIFI_SSID="<SSID-2G>";WIFI_PASSWORD="<masked>"'`）。

| run | 電源 | 待機 | **IPv4 (`0x600080B8`)** | **ping (`0x60008054`)** | decode |
|---|---|---|---|---|---|
| #1 | **真cold** | 45s | `0x4a01a8c0` | `0x77001500` | **IP=192.168.1.74 ／ ok=21 ng=0** |
| #2 | **真cold**（独立） | 60s | `0x4a01a8c0` | `0x77003100` | **IP=192.168.1.74 ／ ok=49 ng=0** |

- `ping` マーカ＝`0x77<<24 | ok<<8 | ng`。**NG は 2 run とも 0**。
  ok が 21→49 と増えるのは待機時間の差（1Hz ping）＝**整合**。
- 同じ run の clock マーカも健全（`OPTIONS0=0x1c00a000`・`PCCR=0x26d6a`・`CLKCFG=0x0001000d`）
  ＝**1回の cold ブートで «クロック健全» と «W1 成立» を同時に立証している**。
- ★**mbedtls 3.6.5（版ダウン後）が走行経路で実際に効いている証拠**：WPA2 の 4-way handshake
  （PTK/MIC 導出＝`crypto_mbedtls*.c`）を通らないと DHCP bound に到達しない
  ＝**リンクが通っただけでなく暗号が機能している**（C5 evidence-02 §5.1 と同じ判定基準）。

### 3.1 前提確認（コーディネータ指示：`reason=201` を回帰と誤断しない）

**W1 を組む前に `wifi_scan` で対象 SSID の実在を確認した**（コンソール採取．**SSID はマスク**）：

```
wifi_scan: 20 APs found (err=0)
  [0] <SSID-2G> (rssi=-55 ch=1)
  [7] <SSID-2G> (rssi=-81 ch=1)
```
⇒ **2.4GHz ch1・rssi=-55 で実在**。∴ `reason=201`（AP不在）の可能性を**事前に排除**してから W1 を測った。

---

## 4. hal 参照 0 の実機非回帰（依頼 (d)）

| build | 供給 | `.d` hal | `.d` esp-idf | **真cold 実機結果** |
|---|---|---|---|---|
| `c3_cold`（`wifi_scan`） | **esp-idf submodule** | **0** | 6264 | **★19 APs・err=0**（`0x600080B8=0x5ca00013`．**コンソール未open**） |
| `c3_w1`（`wifi_dhcp`） | **esp-idf submodule** | **0** | 6278 | **★GOT IP + ping 49/49**（§3） |

- `wifi_scan` の cold マーカ＝`0x5CA0<<16 | err<<8 | AP件数` ⇒ `0x5ca00013` ⇒ **err=0・AP=0x13=19**。
- ⇒ **「hal 参照 0」は静的（evidence-c3-01 §6）だけでなく，真cold の実機で機能非回帰**。
- warm のコンソール採取でも `20 APs (err=0)`・`RESCAN 17/19/19 APs`＝**hal ベースラインと同等**。

---

## 5. 認証情報の混入 0 の確認方法（依頼 (e)）

**注入経路＝ビルド時のみ**：`-DASP3_EXTRA_COMPILE_DEFS='…;WIFI_SSID="…";WIFI_PASSWORD="…"'`
（★`-DWIFI_SSID=` を cmake に直接渡しても効かない＝plumbing 不在で既定 `"your-ssid"` のまま `reason=201`）。

**混入 0 の担保（実測）**：

1. **`build/` は `.gitignore:1` で無視**（`git check-ignore -v build/c3_w1/CMakeCache.txt` で確認）
   ＝**認証情報を含む `CMakeCache.txt` は commit 不能**。
2. **採取ログ・スクリプトは `/tmp` のスクラッチパッド**（リポジトリ外）。
3. **commit 前チェック**（コーディネータ指示）：
   ```
   git diff | grep -c "^+.*<SSID-2G>"  → 0   （<REDACTED> / <REDACTED> も 0）
   git diff --cached | grep -c "^+.*…"  → 0
   ```
   **全パターン `+` 側 0 を確認**。
4. **マーカ設計自体に載せない**：RTC STORE には **IP／ping数／AP件数／クロック値のみ**。
   **SSID・パスワードは 1bit も載せていない**。
5. **evidence 化時のマスク**：`wifi_dhcp.c:255` は `SSID='%s'` を syslog に出す（`wifi_connect.c` と同族）
   ため，**本ファイルの生ログは `<SSID-2G>` へマスク済み**。
6. ★**HEAD 側の非回帰**：コーディネータの `264fb99`（SSID 33箇所をマスク）を**書き戻していない**
   （`git grep '<SSID-2G>' HEAD` = 0 を維持）。

---

## 6. 測定の作法（本ラウンドで «実測して» 確立したもの）

### 6.1 ★`--before usb-reset` は **アプリを走らせずに** download mode へ入る（実証済み）

マーカを 0 クリア → **`--before usb-reset` を伴う read を 5 回** → **5 回とも 0 のまま**。
⇒ **usb-reset ではアプリが走らない**＝**読み出し操作そのものがマーカを汚染しない**ことを実測で担保。
∴ 後で読めた非0値は**必ず «測りたいブート» のもの**（＝stale ではない）。

### 6.2 ★C3 の RTC STORE の空き＝**ROMの宣言ではなく実測で決める**

C3 の ROM ヘッダ（`esp_rom/esp32c3/include/esp32c3/rom/rtc.h:48-55`）は
**STORE0 以外の全 STORE に用途を宣言**している（STORE1=RTC_SLOW_CLK cal／STORE2,3=Boot time／
STORE4=XTAL freq／STORE5=APB freq／STORE6,7=FAST_RTC entry,CRC）。
**しかしそれらを使うのは ESP-IDF の `rtc_clk` 系であって、ASP3 Direct Boot では誰も走らせない。**

**真cold ブート直後の実測**：

| STORE | 宣言された用途 | 実測値 | 判定 |
|---|---|---|---|
| STORE1 `0x60008054` | RTC_SLOW_CLK cal | **`0x00000000`** | **ROMは書かない＝空き** |
| STORE4 `0x600080B8` | External XTAL freq | **`0x00000000`** | **ROMは書かない＝空き** |
| STORE5 `0x600080BC` | APB bus freq | **`0x13121312`** | **★ROMが上書きする＝使用禁止** |

⇒ **memory の «STORE5 は ROM-clobbered．他を使え» を独立に再確認**し、
かつ **«ROMヘッダの用途宣言 ≠ 実際に書かれる»** ことを実測で示した
（C6 evidence-04 §6.4-5 が «STORE4=RTC_XTAL_FREQ_REG を潰している» と申し送った潜在バグは、
**C3 の ASP3 Direct Boot では顕在化しない**——ただし**宣言を根拠に安全と決めてはいけない**、
という方向は同じ）。

### 6.3 その他

- **真coldの唯一の証明＝`uhubctl -l 1-6 -p 1 -a off` 後に `ls /dev/serial/by-id/ | grep -c BA:BC` = 0**
  （毎 run 実施．`rst:` は証明にならない）。**他ポート（port2=nested hub／port3/4）には一切触れていない**
  （off 中も他の Espressif 3台が by-id に残存することを毎回確認）。
- ★**`esptool ... | head -N` をやってはいけない**：本ラウンドで **SIGPIPE が `write-flash` を
  途中で切った**（4MB・`--no-stub` は約72秒）。**書込みは必ずログへリダイレクトし，
  `Hash of data verified` を確認する**。
- **★安全上の齟齬（コーディネータの表と実機が違った）**：プロンプトの hub 表は
  「port2=C6／port3/4=C5#1」としていたが、実測は **port2＝ネストした ganged hub（`05e3:0608`）**・
  **port4＝`D0:CF:13:F0:A7:44`**。**私の DUT が port1（`60:55:F9:57:BA:BC`）である点だけは一致**したので
  `-p 1` のみで進めた。**電源操作前に毎回 `uhubctl -l 1-6` で port1 の中身を目視確認した。**

---

## 7. 残ブロッカー（依頼 (f)）

1. **BBPLL のアナログ較正（regi2c 8本）は未適用のまま**＝温度/電圧マージン未検証（§2.1）。
   **cold/warm の問題ではない**ので本ラウンドでは触らない（反射的な C6 移植はむしろ危険＝§2）。
2. **C3 BT の供給移行は未実施**（evidence-c3-01 §9-2）。★**`-DASP3_BT_IDF_V554=ON` は
   «真の v5.5.4 タグ» を指すようになったが実機未測定**（既定 OFF＝実機挙動不変）。
   HANDOFF §4-4 の bond 失敗エビデンスは **+1169(≡v6.1系)** に対するもの＝**再測が要る**。
3. **BLE（D-1〜D-2d）の真cold は依然未検証**。本ラウンドが真coldで検証したのは **WiFi 経路のみ**。
   `docs/bt-shim.md:2791` の「物理電源断 cold boot はユーザー保留」は **WiFi については解消したが
   BLE については残る**（BT は hal 供給・別コード経路）。
4. **`docs/blob-unify-v554.md` 等の «v5.5.4＝`~/tools/esp-idf`» 記述は未訂正**（evidence-c3-01 §9-4）。
5. **W1 の長時間/負荷試験は未実施**（本ラウンドは最長 60s・ping 49 発）。
</content>

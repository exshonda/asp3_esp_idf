# C5 evidence-08 — ツールチェーン／ビルドオプションを ESP-IDF v5.5.4 標準へ整合

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #1**（BASE MAC `d0:cf:13:f0:a7:44`, hub `1-6` port **3/4**）
toolchain: **実測値**（宣言ではない）＝下表のとおりアームごとに異なる。
本ラウンドで導入した既定＝Espressif `riscv32-esp-elf` **esp-14.2.0_20260121**
（＝esp-idf submodule＝真の v5.5.4 タグ `735507283d` が `tools/tools.json` で指定する版）

> ★ヘッダの `toolchain:` は実測値を書く運用。evidence-05 のヘッダは
> `esp-15.2.0` と宣言していたが、C5 BLE の既定ビルド（`c5_ble_d2cd`）の
> `CMakeCache.txt` を実測すると確かに esp-15.2.0_20251204 であり、
> **C5 に関してはヘッダは正しかった**（親の引き継ぎは「宣言と実体が食い違う」
> と一般化していたが、C5 では食い違っていない。C3/C6 は未確認）。

---

## 0. 結論サマリ（先に）

1. **IDF v5.5.4 が esp32c5 に渡す実フラグ＝`-march=rv32imac_zicsr_zifencei` のみ**
   （`-mabi` すら渡さない＝ツールチェーン既定 ilp32 に委ねている）。
   実測方法＝**esp-idf submodule で hello_world を実際に configure**（後述 §1.2）。
2. **blob は `rv32imc`（A拡張なし）**。`libphy` に至っては `.riscv.attributes`
   セクションを持たない。**全 blob の A拡張命令数＝0**。
   ⇒ **blob は A拡張を要求も使用もしない**。
3. ★**`rv32imc`→`rv32imac` の変更は «機械語として no-op»**（実測）。
   同一コンパイラで march だけ変えた対照で **`.text` サイズ完全一致・
   差分はビルド時刻(`__TIME__`)のみ**、最終イメージの A拡張命令数 **0**。
   WiFi/BLE 両アプリで独立に確認。
4. ★**`arch/riscv_gcc/esp32c5/chip.cmake:29` の «マルチリブが rv32imc を
   持たない» コメントは «汎用ツールチェーンの話» だった**＝実測で確定。
   正しいコンパイラでは前提が消滅する（§1.4）。**親の疑い（フラグ設計が
   間違ったコンパイラに合わせて形作られていた）は実測で裏付けられた。**
5. ★★**本ミッションの W1 A/B は «退化した実験» だった**＝
   A(esp-14.2.0_20241119) と B(esp-14.2.0_20260121) は **どちらも上流 GCC 14.2.0** で、
   ISA 変更が no-op なため **生成物がビルド時刻を除いてバイト同一**。
   ⇒ **実機で W1 の A/B を比べても «同じプログラムを2回動かす» だけ**（§3.1）。
6. ★**C5 の «現行» は汎用GCCではない**（親の前提の訂正・§1.5）。
   C5 の既知良好は **W1=esp-14.2.0_20241119 / W2=esp-15.2.0_20251204** ＝
   **W1 と W2 は そもそも別のコンパイラでしか検証されていなかった**。
7. ★**C5 BLE は HEAD で «素では» ビルド不能**（§4）。既知良好は
   `-DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration` を
   **コマンドラインで手渡ししていた**（ツリーには存在しない）。
   隠していた警告の実体＝**2件のみ**（`esp_timer_is_active` /
   `esp_timer_get_expiry_time`）＝**共有 stub ヘッダが本物を隠す実バグ**。

---

## 1. Step 1 — IDF v5.5.4 の «標準» の実測

### 1.1 コンパイラ（各ツリーが指定する版）

`tools/tools.json` の `riscv32-esp-elf` を実測：

| ツリー | 指定版 |
|---|---|
| **esp-idf submodule（真の v5.5.4 `735507283d`）** | **`esp-14.2.0_20260121`** |
| `/home/honda/tools/esp-idf-v6.1` | `esp-15.2.0_20251204` |
| `~/tools/esp-idf`（実体は v5.5.0） | `esp-14.2.0_20241119` |

**本リポジトリ `build/` 配下 320 構成の `CMakeCache.txt` を独立に再実測**
（親の表の追検証。`CMAKE_C_COMPILER_AR:FILEPATH` で判定）：

| 実際に使われたコンパイラ | 構成数 |
|---|---|
| `/usr/bin`（Ubuntu 汎用 GCC 13.2.0） | **164** |
| `esp-15.2.0_20251204`（v6.1 の指定） | 84 |
| `esp-14.2.0_20241119`（v5.5.0 の指定） | 72（＝43 + 29） |
| **`esp-14.2.0_20260121`（真の v5.5.4 の指定）** | **0** |

**親の表を完全に再現した**（164/84/72/0）。**refinement**＝72 は
`~/tools/espressif/...`(43) と `~/.espressif/...`(29) の **2箇所の導入先に分裂**していた。
164+84+43+29 = 320 ＝ 全数一致 ⇒ **「0」は «測っていない0» ではなく実測の0**。

### 1.2 ビルドオプション（★どう測ったかを明記）

**方法＝IDF 本体で esp32c5 を実際に configure した**（読解ではない）：

```
IDF_PATH=<repo>/esp-idf  IDF_PYTHON_ENV_PATH=~/.espressif/python_env/idf5.5_py3.12_env
python $IDF_PATH/tools/idf.py -DIDF_TARGET=esp32c5 reconfigure   # rc=0
```

★**罠**：`build/compile_commands.json` を `-march` で grep すると **0 件**。
これは «無い» のではなく **IDF がレスポンスファイル `@build/toolchain/cflags`
経由で渡す**ため。**「0 を読んだら測定対象が存在するか先に確かめろ」に該当**し、
実際に一度踏んで是正した。実体：

```
build/toolchain/cflags   : -march=rv32imac_zicsr_zifencei
build/toolchain/asmflags : -march=rv32imac_zicsr_zifencei
build/toolchain/cxxflags : -march=rv32imac_zicsr_zifencei / -fno-rtti
build/toolchain/ldflags  : -nostartfiles / -fno-rtti
```

コンパイラ実体（492 コンパイル単位すべて）＝
`~/.espressif/tools/riscv32-esp-elf/**esp-14.2.0_20260121**/.../riscv32-esp-elf-gcc`。

出典（ソース側の裏取り）＝`esp-idf/components/soc/project_include.cmake`：
C2/C3 → `rv32imc_zicsr_zifencei` ／ **C5**/C6/C61/H2/H21 → **`rv32imac_zicsr_zifencei`**。
`tools/cmake/toolchain-clang-esp32c5.cmake` も `rv32imac_zicsr_zifencei`/`ilp32` で**一致**。
`soc_caps.h` に `SOC_CPU_HAS_FPU`/`HWLOOP`/`PIE` は**無い**⇒ march 接尾辞も `ilp32f` も付かない。

⇒ **IDF は esp32c5 に `-march` 以外の ABI/ISA フラグを渡していない。**

### 1.3 ツールチェーン既定値の実測（`-Q --help=target`、実 march 下で）

`riscv32-esp-elf-gcc esp-14.2.0_20260121 -march=rv32imac_zicsr_zifencei`：

| 項目 | 既定 | ASP3 現行 | 判定 |
|---|---|---|---|
| `-mabi=` | **ilp32** | `-mabi=ilp32` | **同値**（IDF は渡さない） |
| `-msmall-data-limit=` | **8** | `=8` | **同値** |
| `-mstrict-align` | **enabled** | 明示 ON | **同値** |
| `-msave-restore` | **disabled** | `-mno-save-restore` | **同値** |
| `-mcmodel=` | **medlow** | **medany** | **差異**（ASP3 固有・§3.3） |
| char 符号 | **unsigned**（`__CHAR_UNSIGNED__`） | `-fsigned-char` | **差異**（ASP3 固有・§3.3） |

⇒ ASP3 が明示している ABI 系フラグ4本は **すべて esp ツールチェーン既定と同値**＝
明示していても差は出ない。**実差は «コンパイラ» と «march の A» の2点だけ**。

### 1.4 ★`chip.cmake:29` コメントの検証（＝親の仮説の検証）

旧コメント：「ツールチェーンのマルチリブは rv32imc を持たないため rv32im/ilp32 がリンクされる」

`-print-multi-directory` 実測：

| コンパイラ | `-march=rv32imc_zicsr_zifencei` | `-march=rv32imac_zicsr_zifencei` |
|---|---|---|
| `/usr/bin/riscv64-unknown-elf-gcc` 13.2.0（汎用） | **`rv32im/ilp32`**（C拡張なし！） | `rv32imac/ilp32` |
| `riscv32-esp-elf` esp-14.2.0_20260121 | `rv32imc_zicsr_zifencei/ilp32` | `rv32imac_zicsr_zifencei/ilp32` |

汎用 GCC の multilib 一覧に **`rv32imc` は実在しない**（`rv32im`・`rv32imac` はある）。
⇒ **旧コメントは «汎用ツールチェーンを使っていた時代の事実» としては正しかった**。
Espressif 版では両方実在し指定 ISA と一致する multilib が選ばれる＝**前提が消滅**。
∴ **「フラグ設計が間違ったコンパイラに合わせて形作られていた」という親の疑いは実測で裏付けられた**
（ただし «コメントが嘘» ではなく «正しいコンパイラでは前提が消える» が正確）。

### 1.5 ★★「現行＝汎用GCC」は C5 では成り立たない（親の前提の訂正）

C5 の build dir 80 構成の内訳（実測）：

| コンパイラ | C5 構成数 |
|---|---|
| `esp-14.2.0_20241119` | **52** |
| `esp-15.2.0_20251204` | 22 |
| `/usr/bin` 汎用 13.2.0 | **6** |

**既知良好の実体**：

| 既知良好 | build dir | 実際のコンパイラ |
|---|---|---|
| **W1**（DHCP+ping） | `c5_r44/r45_wifi_dhcp` | **esp-14.2.0_20241119**（GCC 14.2.0） |
| **W2**（BLE D-2c/D-2d） | `c5_ble_d2cd` | **esp-15.2.0_20251204**（GCC **15**.2.0） |

⇒ **164 件の汎用GCC事故は主に C3/C6 であって C5 ではない**。
⇒ ★**C5 の W1 と W2 は «別々のコンパイラ» でしか検証されていない**（本ラウンドで初めて統一される）。
⇒ **アーム A を «汎用GCC» と定義すると «一度も既知良好でなかった構成» を対照にすることになる**ので、
   **A ＝ ワークストリームごとの既知良好構成**と定義し直す（§3.1）。

### 1.6 blob が要求する ISA（＝真の要求仕様）

`riscv32-esp-elf-readelf -A` で C5 blob の `Tag_RISCV_arch`：

| blob | Tag_RISCV_arch |
|---|---|
| `libpp.a`(54 obj) / `libcore.a`(1) / `libnet80211.a`(50) / `libble_app.a`(154) / `libcoexist.a`(10) | **`rv32i2p0_m2p0_c2p0`** ＝ rv32imc（**A なし**） |
| `libphy.a`(20 obj) | **`.riscv.attributes` セクション自体が無い**（ISA 制約を主張しない） |

さらに**全 blob を逆アセンブルして A拡張命令（`lr.w`/`sc.w`/`amo*`）を計数＝すべて 0**。

★**計器の検定**：同じ検出器を «A拡張を必ず出すコード» に当てて **3 件検出**、
`rv32imc` では **0 件** ⇒ **検出器は生きている**⇒上の「0」は実測の0。

⇒ **blob は A拡張を要求も使用もしない**。ASP3 側を `rv32imac` にしても
blob は ISA の部分集合なので整合は崩れない（リンカは属性を和集合にマージ）。

---

## 2. Step 2/3 — 変更内容

### 2.1 分界（★C3/C6 への転写を見据えた «共有 / チップ固有» の線引き）

| 置き場所 | 内容 | 共有性 | C3/C6 転写時 |
|---|---|---|---|
| **`asp3/cmake/toolchain-esp32-riscv32.cmake`**（新規） | コンパイラの**絶対パス固定**・版既定 `esp-14.2.0_20260121`・未導入なら FATAL | **チップ非依存＝3チップ共有** | そのまま使える（**submodule 変更不要**） |
| **`asp3/cmake/esp_toolchain_check.cmake`**（新規） | 実際に選ばれたコンパイラを実測し期待と違えば FATAL | **チップ非依存＝3チップ共有** | そのまま使える（**submodule 変更不要**） |
| `asp3/target/esp32c5_espidf/target.cmake` | 上記 check の呼び出し（2行） | **本リポジトリ**。C3/C6 の target.cmake も本リポジトリ | **同じ2行を足すだけ**（**submodule 変更不要**） |
| `asp3/arch/riscv_gcc/esp32c5/chip.cmake` | **march/mabi＝チップ固有 ISA** | **C5 のみ本リポジトリ** | ★**C3/C6 は `asp3_core` 側にあり編集不可**＝§6 で報告 |

**設計理由**：`chip.cmake` は C3/C6 では submodule（禁則）だが、
**`target.cmake` は3チップとも本リポジトリ**にある。よって
**「target.cmake から共有 cmake を include する」形**にすれば C3/C6 へは
submodule を触らずに転写できる。`hal_stub/`・`net/` を `C3_TARGETDIR` 経由で
3チップが共有する**既存の前例と同型**（新規則は導入していない）。

### 2.2 「黙って汎用GCCへ落ちる」の検出＝★実際に発火させた

| # | 条件 | 期待 | 実測 |
|---|---|---|---|
| 1 | asp3_core toolchain＋**prefix 渡し忘れ**（＝164件の事故そのもの） | FATAL | **発火**：`-dumpmachine : riscv64-unknown-elf` |
| 2 | riscv32-esp-elf だが **版違い**（esp-15.2.0） | FATAL | **発火**：`found: esp-15.2.0_20251204 / expected: esp-14.2.0_20260121` |
| 3 | toolchain file で **未導入の版**を指定 | FATAL | **発火**：`toolchain not found` |
| 4 | 正しい版 | PASS | `-- ESP toolchain OK: esp-14.2.0_20260121 (riscv32-esp-elf)` |
| 5 | 逃げ道 `-DASP3_ESP_EXPECTED_TOOLCHAIN=` | 通る | ★**最初 «効かなかった»**（下記） |
| 6 | 逃げ道 `-DASP3_ESP_TOOLCHAIN_CHECK=OFF` | 検証のみ無効化 | PASS |

★★**自己検出した私のバグ**：test5 で、**エラーメッセージが案内する
`-DASP3_ESP_EXPECTED_TOOLCHAIN=<tag>` が効かなかった**。原因＝`target.cmake` が
素の `set()` で無条件上書きしていた。⇒ **「存在しない退避先を案内する FATAL_ERROR」を
自分で作りかけた**（このリポジトリで前例のある事故型）。`if(NOT DEFINED ...)` へ修正し
**再発火させて全6件を再確認**（1 は修正後も発火することを確認済み＝守りを壊していない）。

**案内先の実在確認**（実際に存在を確かめた）：
`asp3/cmake/toolchain-esp32-riscv32.cmake`＝**在**／`esp-idf/install.sh`＝**在（実行可）**・
`esp32c5` は idf_tools が知るターゲット／`esp-15.2.0_20251204`＝**導入済み**。

### 2.3 chip.cmake（ISA）

- `-march=rv32imc_zicsr_zifencei` → **`rv32imac_zicsr_zifencei`**（IDF 標準）
- **`option(ESP32C5_IDF_STD_ISA)` 既定 ON**／`OFF` で移行前へ**完全復帰**（可逆）
- `:29` の旧コメントを **実測に基づき全面改訂**（§1.4 の測定値を本文に埋め込み、
  «汎用ツールチェーン由来の記述だった» という経緯も残した）
- **ASP3 固有として維持**＝`-mcmodel=medany`・`-fsigned-char`（§3.3）

---

## 3. Step 4 — 実機 A/B の設計と★予測の事前登録

### 3.1 ★アーム定義（親の定義を実測で訂正）

| アーム | コンパイラ | march | 位置づけ |
|---|---|---|---|
| **A**(W1) | esp-14.2.0_20241119（GCC 14.2.0） | rv32imc | **W1 の既知良好そのもの** |
| **A2**(W2) | esp-15.2.0_20251204（GCC **15**.2.0） | rv32imc | **W2(BLE) の既知良好そのもの** |
| **B** | **esp-14.2.0_20260121（GCC 14.2.0）** | **rv32imac** | **IDF v5.5.4 標準** |
| （A0） | 汎用 13.2.0 | rv32imc | 参考（C5 の既知良好ではない） |
| （B2） | esp-14.2.0_20260121 | rv32imc | **ISA 変数を単離する対照** |

### 3.2 ★★ビルド生成物の実測（実機の前に判明した決定的事実）

`.text` を抽出してバイト比較（`__TIME__` バナーの位置も判定）：

| 比較 | 結果 |
|---|---|
| **A vs B**（W1） | **2 バイトのみ相違・すべて `__TIME__` バナー内**（`14:14:43` vs `14:12:45`） |
| **B vs B2**（**ISA 単離**・同一コンパイラ） | **3 バイトのみ相違・すべて `__TIME__` バナー内** |
| A vs A0（汎用GCC） | **サイズが違う**（527296 vs **531536**＝+4240 B）＝**別物** |
| **A2 vs B**（**BLE**・GCC15 vs GCC14） | **416224 vs 417712＝+1488 B ＝ 別物** |

★**md5 は不正な計器だった**（実際に踏んだ）：`asp_flash.bin` の md5 は A/B で違うが、
中身の差は **ビルド時刻2バイトだけ**。**「md5 が違う＝別物」と読んでいたら誤断していた**。

⇒ **結論(1)：`rv32imc`→`rv32imac` は機械語として no-op**（同一コンパイラ・march のみ差＝時刻以外一致、
最終イメージの A拡張命令数 **0**／WiFi・BLE 両アプリで独立に確認）。
⇒ **結論(2)：W1 の A/B は退化**（A と B は **同じプログラム**）。
⇒ **結論(3)：W2 の A/B は本物**（GCC15 vs GCC14 で 1488 B 差）。

### 3.3 ASP3 固有として維持する差異（IDF と意図的に異なる）

| フラグ | IDF | ASP3 | 判断 |
|---|---|---|---|
| `-mcmodel` | medlow（既定・非明示） | **medany** | **維持**。ASP3 は RP2350/PolarFire と統一。medlow/medany は**リンク互換**（ABI 差ではない） |
| char 符号 | unsigned（既定・非明示） | **`-fsigned-char`** | **維持**。TOPPERS は signed char 前提。**言語意味論の差であり呼出規約=ABI の差ではない**。ただし**IDF 本体は unsigned char でビルドされる**＝shim が IDF ヘッダの `char` を跨ぐ箇所は理屈上差が出うる（**未検証**） |

★**丸写ししていない**：IDF の `-Og`/`-fno-jump-tables`/`-fstrict-volatile-bitfields`/
`-nostartfiles` 等は **ABI/ISA に効かない**ので採用していない（ASP3 は IDF ではない）。

### 3.4 ★★予測の事前登録（実機の前に確定・後付け禁止）

**P1. B × W1（DHCP+ping）が通る確率＝92%**
- 根拠：**B は W1 既知良好 A と «バイト同一（時刻除く）» のプログラム**。残る不確実性は
  環境（AP 在否・電波）と RAM 81.29% のみ。
- ★**外れたら何を意味するか**：**A と B は同じプログラムなので、A 成功／B 失敗が起きたら
  それは «ツールチェーンの差» では絶対に説明できない**⇒(i) 環境の非決定性、または
  (ii) **私のバイト同一測定が誤り**、のどちらか。**この場合 §3.2 の測定を疑え**（計器を先に疑う）。

**P2. B × W2（BlueZ hci0 接続+GATT）が通る確率＝85%**
- 根拠：GCC15→GCC14 は **IDF v5.5.4 が公式に指定する版への «降格»**。BT blob も
  同 submodule 由来。ただし既知良好は GCC15 でしか取れていない＝**真の未検証構成**。
- ★**外れたら**：**「IDF 標準へ揃える」が BLE を壊す**＝重大。その場合
  **`ESP32C5_IDF_STD_ISA` ではなく «コンパイラ» が原因**（ISA は no-op と実証済）⇒
  **BLE だけ esp-15.2.0 に留める**という結論があり得る（＝「A だけ動く」＝現行固定が正解の型）。

**P3. march に A拡張が入るか／blob と整合するか**
- ★**正直な申告**：これは **予測ではなく先に実測してしまった**（§1.2/§1.6）。
  後付けで「予測が当たった」とは書かない。**測定結果**＝IDF は A を入れる／blob は A を
  要求も使用もしない／**入れても機械語は変わらない（no-op）**⇒**整合する**。

**P4. B × iPhone が OK である確率＝80%**（現状 A2 で OK ＝「壊さないか」の問い）
- 外れたら：**IDF 標準化が iPhone 経路を壊した**＝P2 と同じく «コンパイラ» に帰属。

**P5. B × Android が OK に «変わる» 確率＝12%**
- 根拠：既知の C5×Android は `ENC=0x5de00007`(`BLE_HS_ENOTCONN`) で **3/3 再現**。
  memory の確定マトリクスでは **チップが効いており C5 は Android で必ず落ちる**。
  **コンパイラ差でプロトコル層の病態が変わるとは考えにくい**。
- ★**外れたら（＝OK になったら）重大**：Android 不通に **toolchain が関与していた**ことになり、
  「チップが効いている」という既存の帰属を**部分的に覆す**。
- ★**変わらなければ収穫**：**Android 調査から toolchain を «除外» できる**（帰属の前進）。

**★反証条件（先に固定）**
- **H（本ラウンドの主張）＝「ISA の rv32imc→rv32imac は挙動に影響しない」**
  - **反証条件**：B と B2（ISA のみ差）で**実機挙動が違えば H を捨てる**。
  - ★**反証条件自体の検算**：H は既に **«機械語が時刻以外一致»** という独立測定で支持済み。
    もし実機で B と B2 が違ったら、**H を捨てる前に «同じ機械語が違う挙動を出した» という
    矛盾を疑い、まず §3.2 の測定と書込み手順を再検定する**（過去に «反証条件が invalid» だった実例に従う）。
- **H2＝「B は A/A2 の既知良好を壊さない」**
  - **反証条件**：**B が W1 か BlueZ で落ちたら、スマホ検証へ進まず即報告**（土台が壊れた状態で
    スマホ結果を採っても意味がないため。PVCY draft で «C0 が壊れているのに先へ進まない» 判断が
    正解だった前例に従う）。

---

## 4. ★C5 BLE は HEAD で «素では» ビルド不能（本ラウンドで発見・私の変更とは無関係）

`-DESP32C5_BT=ON -DASP3_APPLNAME=ble_host_smoke_c5` は **HEAD で build rc=1**：

```
esp-idf/components/bt/porting/npl/freertos/src/npl_os_freertos.c:842:12:
  error: implicit declaration of function 'esp_timer_is_active' [-Wimplicit-function-declaration]
esp-idf/.../npl_os_freertos.c:862:12:
  error: implicit declaration of function 'esp_timer_get_expiry_time'
```

**私の変更のせいではないことの証明**＝**歴史的な既知良好レシピを逐語再現**
（asp3_core の toolchain file・`ASP3_ESP_TOOLCHAIN_CHECK=OFF`・`ESP32C5_IDF_STD_ISA=OFF`・
既知良好と同じ **GCC15**）でも **同じエラーで rc=1**。⇒ **HEAD 側の pre-existing。**

**なぜ 01:55 の `c5_ble_d2cd` は通ったのか**＝`CMakeCache.txt` を実測：
```
CMAKE_C_FLAGS:STRING=-Wno-error=implicit-function-declaration
```
⇒ **ビルドした人／エージェントがコマンドラインで手渡ししていた**。
**ツリーには存在しない**（`grep` 実測：BLE 経路の cmake に当該フラグは無い。
`esp_wifi_v8.cmake:92` にあるのは **WiFi 経路のみ**）。
∴ **C5 BLE の既知良好は «記録されていない手作業の回避» に依存していた。**

**★隠していた警告の実体（＝コーディネータ指示「実体を報告しろ」への回答）**
- **件数は 2 件のみ**（GCC14・GCC15 とも同一）：`esp_timer_is_active` / `esp_timer_get_expiry_time`。
- **真因＝共有 stub ヘッダによる shadowing**：include 順で
  `asp3/target/esp32c3_espidf/hal_stub/include`（**7 番目**）が
  `esp-idf/components/esp_timer/include`（**47 番目**）より先。
  stub の `esp_timer.h` は `esp_timer_get_time`/`esp_timer_create` は宣言するが
  **上記2関数を宣言していない**（実測 grep：0 件）。
- **実害の評価**：両関数とも **ELF に実体が存在**（`nm`＝`T esp_timer_is_active` /
  `T esp_timer_get_expiry_time`）。本物の宣言は `bool` / `esp_err_t(=int)` で、
  暗黙宣言の `int` と **RISC-V ABI 上 戻り値表現が一致**⇒**現状は実害なし（benign）**。
  ただし **ヘッダ衛生の実バグ**であり、フラグはそれを隠している。
- ★**私はこのフラグをツリーに «追加していない»**（コーディネータ指示どおり）。
  **A/B 両アームともコマンドラインで同一に渡し**、既知良好と同条件に揃えた（交絡を作らない）。
- **正しい修正＝stub `esp_timer.h` に 2 宣言を足す**（1行×2）。ただし
  **`hal_stub/` は別エージェントが編集中で触るなと指示されている**⇒**未実施・ユーザー判断事項**。

---

## 5. 非回帰（C3/C6 — 挙動を変えていないこと）

| 対象 | 実測 |
|---|---|
| **構造**：C3/C6 の `target.cmake` が本ラウンドの共有 cmake を参照する数 | **0 / 0**（C5 は 6） |
| **C6** `wifi_scan`（`ESP32C6_WIFI=ON`・HEAD と同じ汎用GCC経路） | configure rc=0 / **build rc=0** |
| **C3** `wifi_scan`（`ESP32C3_WIFI=ON`・同上） | configure rc=0 / **build rc=0** |
| 私の guard が C3/C6 で発火した回数 | **0 / 0**（include されないため。＝挙動不変） |

⇒ **仕組みは3チップ共有可能な場所に置いたが、適用は C5 のみ**（コーディネータ指示どおり）。

★**「ビルドが通る」と「既知良好」は別**なので、C3/C6 は**実機を触っていない**
（ユーザー指示「C5 を先行」に従い、C3/C6 の挙動を変えていないことの確認に留めた）。

---

## 7. Step 4 実測結果（実機・すべて真cold）

### 7.1 ★真cold の証明と、計器の検定（本ラウンドで harness を1つ直した）

**真cold の証明＝2つの独立した witness**：
1. `uhubctl -l 1-6 -p 3-4 -a off` ＋ **by-id 2ノードの消失を読み戻し**（全 run で `cp2102n=GONE usbjtag=GONE`）。
2. ★**sentinel（コンソール非依存）**：`STORE6=0xCAFE5A9C` を植えて**読み戻しで植込み成功を確認**→
   電源断→POR 後 **`0x600b1018 = 0x00000000`**
   ⇒ **C5 では真POR が LP_AON をクリアする**（＝**真cold 後のマーカは本質的にフレッシュ**）。
   ※コーディネータの「`ENC`/`PAIR` は毎起動クリアされない」は **warm/EN-reset では真**だが、
   **真POR では消える**＝本ラウンドは常に真POR なので stale 交絡は原理的に起こらない。
   （それでも明示クリアは実施＝二重の担保。`STORE1`=RTC cal は回避。）

★★**capture harness のバグを自己検出して修正した**：
`c5-uart-capture-open-resets-dut.md` に従い「DTR/RTS を **deassert** して open すれば
リセットしないはず」と実装したが、**実測で反証**——**アプリ実行中に open したら
ESP-ROM バナーが出て ping が 0 行**＝**リセットしていた**。極性を総当りしたところ：

| open 時の線 | 実測 |
|---|---|
| `dtr=False, rts=False`（当初の私の実装） | **RESET する**（ESP-ROM バナー・ping 0） |
| **`dtr=True, rts=True`** | **RESET しない**（ping 7 行・バナー無し） |
| `dtr=True, rts=False` | RESET しない（ping 8 行） |

⇒ harness を `dtr=True/rts=True` へ修正。**効果＝POR ブートそのものを捕捉できる**
（修正後の全 run で **ESP-ROM バナー数＝0**＝open がリセットしていない＝
**観測しているのは boot#1＝POR ブート**）。
★**修正前に採った run（`w1_B_run1.log`）は «POR の次に来る EN-reset ブート» を見ていた**＝
本文の結果には採用していない（同 run も ping 38/0 で PASS だったが、種別が違うので混ぜない）。

★★**ヒヤリハット（実測で捕捉・実害なし）**：**`/dev/ttyACM*` の番号は電源断のたびに入れ替わる**。
セッション開始時 C5#1=`ttyACM2` → 電源断を重ねた後 **`ttyACM2` は ESP32-S3
（`F4:12:FA:5B:4A:58`）＝別個体**になっていた（`read-mem` が
`This chip is ESP32-S3, not ESP32-C5` で失敗して発覚）。
**実害ゼロの理由**＝(i) 全書込みが `--chip esp32c5` 付き＝esptool が接続時に
チップ判定して**他チップへの書込みを拒否**する（監査：4回の書込みすべて
`Hash of data verified`・`wrong chip` エラー **0**）、(ii) capture harness は
**最初から by-id パス**を使っていた。**是正＝esptool も by-id パスに統一**。
⇒ **教訓：raw `ttyACM*` を実機手順に書くな。by-id（MAC入り）だけを使え。**

### 7.2 W1（`apps/wifi_dhcp`：DHCP で IP 取得 + ping）

| アーム | コンパイラ / ISA | run | 真cold証明 | POR観測 | DHCP | ping OK/NG |
|---|---|---|---|---|---|---|
| **A** | esp-14.2.0_20241119 / rv32imc | 1 | GONE/GONE | ESP-ROM=0 | **一度 timeout→その後 bound** | **4 / 0** |
| **A** | 〃 | 2 | GONE/GONE | ESP-ROM=0 | OK | **44 / 0** |
| **A** | 〃 | 3 | GONE/GONE | ESP-ROM=0 | OK | **45 / 0** |
| **B** | **esp-14.2.0_20260121 / rv32imac** | 1 | GONE/GONE | ESP-ROM=0 | OK | **40 / 0** |
| **B** | 〃 | 2 | GONE/GONE | ESP-ROM=0 | OK | **40 / 0** |

IP＝**`192.168.1.70`**（gw `192.168.1.1`）。**A 3/3 PASS・B 2/2 PASS・NG は全 run で 0**。

★**A run1 の «ping 4・DHCP timeout» を «アーム差» と読んではならない**：
§3.2 で **A と B は `__TIME__` 以外バイト同一**と実測済み＝**同じプログラム**。
∴ この揺れは**環境（AP/電波）side の非決定性**であり、**事前登録した P1 の
「外れたら意味するもの」の (i) に該当**（(ii) 測定誤りではないことは、
続く A run2/run3 が 44/45 と B 並みに戻ったことで支持される）。
**単一 run で結論しない**という作法どおり run を足して判定した。

★**バナーがビルド同一性の証拠になった**：`Kernel Release ... (Jul 17 2026, 14:12:45)`＝**arm B の
ビルド時刻**（A は `14:14:43`）。**A/B を隔てていた 2 バイトが、そのまま「今どちらが載っているか」の
識別子として機能した**（皮肉だが有用）。

### 7.3 W2（BlueZ `hci0`：接続 + GATT）

D-Bus 直叩き（`bluetoothctl` パイプ駆動は使っていない）。**毎セル真cold ＋ central 側 `RemoveDevice`**。

| アーム | コンパイラ | run | 発見(RSSI) | Connected / ServicesResolved | `0xABF1` READ | `0xABF3` WRITE | 判定 |
|---|---|---|---|---|---|---|---|
| **A2** | esp-15.2.0（**GCC15**）＝BLE既知良好 | 1 | -63 | 1 / **True** | **`BT4-OK`** | OK | **PASS** |
| **A2** | 〃 | 2 | -48 | 1 / **True** | **`BT4-OK`** | OK | **PASS** |
| **B** | **esp-14.2.0_20260121（GCC14）** | 1 | -41 | 1 / **True** | **`BT4-OK`** | OK | **PASS** |
| **B** | 〃 | 2 | -41 | 1 / **True** | **`BT4-OK`** | OK | **PASS** |

⇒ **A2 2/2・B 2/2 PASS**。**GCC15→GCC14 の «降格» は BlueZ 経路の BLE を壊さない**。
**この A/B は本物**（§3.2：A2 と B は 1488 B 差＝別バイナリ／flash 圧縮長も 280944 vs 346869 で異なる）。

★**`0xABF4`（暗号必須 READ）の `NoReply` を «正しく弾かれた» と読んでいない**：
私のハーネスは **agent を登録していない**＝memory が明示する «agent 未登録なら
pairing 認可不可で `NoReply`» の既知 artifact に該当し、**デバイス側の可否を判別できない**。
∴ **D-2d の判定はスマホセル（ユーザー操作）に委ねる**。

### 7.4 事前登録した予測の的中/外れ

| 予測 | 内容 | 結果 |
|---|---|---|
| **P1** | B × W1 が通る **92%** | **★的中**（B 2/2 PASS） |
| **P2** | B × W2(BlueZ) が通る **85%** | **★的中**（B 2/2 PASS） |
| **P3** | march の A拡張／blob 整合 | **（予測ではなく先に実測した旨を §3.4 に明記済み）**＝A は入るが **no-op**・blob と整合 |
| **P4** | B × iPhone が OK **80%** | **未測定**（ユーザー確認待ち） |
| **P5** | B × Android が OK に変わる **12%** | **未測定**（ユーザー確認待ち） |
| **H（ISA は挙動に影響しない）** | 反証条件＝B と B2 で実機挙動が違えば棄却 | **反証されず**（機械語が時刻以外一致＝実機で差が出る余地が無い。★ただし B2 の実機 run は**未実施**＝「反証する機会を作っていない」ことは正直に記す） |
| **H2（B は既知良好を壊さない）** | 反証条件＝B が W1/BlueZ で落ちたらスマホへ進まず即報告 | **反証されず**（B は W1 2/2・BlueZ 2/2）⇒ **スマホセルへ進んでよい** |

### 7.5 スマホセル（★準備完了・ユーザー確認待ち＝ここで停止）

**セル1 ＝ B(IDF標準) × Android** を準備し、**広告中のまま残置**：

- 載っているビルド＝`build/c5_tc_B_ble`（**esp-14.2.0_20260121 / rv32imac / `ASP3_BT_IDF_V554=ON` 既定 / SM=ON**）
  ＝**本ラウンドで BlueZ 2/2 PASS を出した構成そのもの**。
- **全マーカー明示クリア済み**（`STORE0/2/3/4/5/6/7/8/9`＝0 を読み戻して確認。`STORE1`＝RTC cal は回避）
  ＋**真cold（by-id 消失を読み戻し）**＝§7.1 の sentinel により **真POR が LP_AON を消すことも実証済み**。
- **BlueZ central 側の stale bond を削除**（`RemoveDevice`）。
- **広告在席は «スキャンで» 確認**（`RSSI=-41`・`D0:CF:13:F0:A7:44`）。**esptool は使っていない**
  （`--after no-reset` は download mode へ落として広告を止めるため）。

★**ユーザーが試す «前» にマーカーを読まない**。読むのはユーザーの「終わった」の後。

**読み出し表（タグで引く。アドレスで引かない＝全 STORE 共用が常態）**

| 事象 | reg | タグ／形式 |
|---|---|---|
| CONNECT | `0x600B1020` | `0x604E<status><count>` |
| DISCONNECT | `0x600B1024` | `0xD15C<reason><count>` |
| WRITE(`0xABF3`) | `0x600B1014` | `0x7717<count><先頭byte>` |
| **ENC_CHANGE** | **`0x600B1018`（共用）** | **`0x5DE0<status>`**（`0x5de00007`=`ENOTCONN`＝既知の Android 病態／`0x0d`=ETIMEOUT） |
| **PAIRING_COMPLETE** | **同上（last-wins）** | **`0x5DC0<status><our_sec><peer_sec>`** |

★**判別子**＝**`0x5DC0` が立てばフレッシュにペアリングが起きた**。
**`0x5DE0` のままなら PAIRING 未到達**（C5 は PAIRING が ENC の **後**に発火するため）。

---

## 8. スマホ セル1 ＝ **B(IDF標準) × Android** — 実測（NG。既知ベースラインと同一）

**ユーザー観測（逐語）**：「Android x C5 : **CONNECT後ペアリング要求が来るが切れる．登録されていない**」

### 8.1 マーカー実測（真cold＋全クリア後の1セッション）

| reg | 生値 | タグで引いた意味 |
|---|---|---|
| `0x600B1000` | `0x5ade51c0` | **SYNC 到達**（`BLE_SYNC_MARK_VAL`）＝host は sync した |
| `0x600B1008` | `0xad000000` | **adv rc=0**＝広告開始成功 |
| `0x600B100C` | `0x00000000` | `ble_hs` reset **無し**（ホストは落ちていない） |
| `0x600B1010` | `0x00000000` | 割込み線1累積＝0 |
| **`0x600B1014`** | **`0x00000000`** | **WRITE(`0xABF3`) は一度も発火せず** |
| **`0x600B1018`（ENC/PAIR 共用）** | **`0x5de00007`** | **タグ `0x5DE0`＝ENC_CHANGE・status=7** |
| `0x600B101C` | `0xa1020704` | bt_shim 割込みトレース（**予約reg。C5 の SM マーカは STORE6 であって此処ではない**＝`0x5DC0` と混同しない） |
| **`0x600B1020`** | **`0x604e0001`** | **CONNECT・status=0x00（成功）・count=1** |
| **`0x600B1024`** | **`0xd15c1301`** | **DISCONNECT・reason byte=0x13・count=1** |

### 8.2 デコードの裏取り（memory ではなく **ソース**で確認）

- `BLE_HS_ENOTCONN = 7`（`esp-idf/.../host/ble_hs.h:90`）⇒ **`0x5de00007` = ENC_CHANGE status `ENOTCONN`**。
- DISC マーカ構築＝`0xD15C0000 | ((event->disconnect.reason & 0xff) << 8) | count`
  （`ble_host_smoke_c5.c:636-639`）。NimBLE の HCI 由来 reason は
  `BLE_HS_HCI_ERR(x) = 0x200 + x`（`ble_hs.h:171-174`）で、**`&0xff` が 0x200 を捨てる**。
  ⇒ `0x13` は **`BLE_ERR_REM_USER_CONN_TERM = 0x13`**（`nimble/ble.h:216`）＝
  **リモート（スマホ）側からの切断**、と読むのが整合的。
  ★**正直な限界**：`&0xff` は不可逆なので、生の `BLE_HS_*` 側の 0x13 と原理的に区別できない
  （**マーカ形式の既知の欠損**。断定はしない）。

### 8.3 ★言えること／言えないこと

**言えること（数字のみ）**：
1. **接続は成立した**（`CONN status=0x00`・count=**1**＝この真cold セッションで接続は1回）。
2. **ENC_CHANGE は発火し、status=7 (`ENOTCONN`)**。
3. **`PAIRING_COMPLETE` は発火していない**（STORE6 に `0x5DC0` が立っていない）。
   C5 は **PAIRING が ENC の «後»** に発火し **last-wins** なので、`0x5DE0` が残っている＝
   **PAIRING_COMPLETE に到達しなかった**（アプリ自身のコメントが規定する読み方）。
   ⇒ **ユーザーの「登録されていない」＝bond 不成立と、デバイス側マーカは整合する。**
4. **切断イベントはデバイスに届いている**（`DISC count=1`）＝**C3 の «切断が来ずに詰まる» 型ではない**。
5. **`0xABF3` WRITE は 0**＝ユーザーはそこまで到達していない。

**言えないこと（★担当外・憶測しない）**：
- **「なぜ切れるか」の機序**。`ENOTCONN` と ユーザーの「ペアリング要求が来た」を **接続して物語にしない**。
  本セルは **1 run**であり、**単一 run から機序を作らない**（本リポジトリで実際に起きた事故型）。
- 「スマホは要求を出したがデバイスが応答しなかった」等の**方向づけも未測定**（RX/TX の計装をしていない）。

### 8.4 既知ベースラインとの比較（★実測で言う）

| | 既知（memory・3/3） | **本セル（B＝IDF標準 toolchain）** |
|---|---|---|
| C5 × Android | **`ENC=0x5de00007`＝`ENOTCONN`(7)** | **`0x5de00007`＝`ENOTCONN`(7)** |
| 判定 | NG | **NG** |

⇒ **病態は既知ベースラインと «同一の数値»**。**B は C5×Android を «変えなかった»**。
（新情報＝**`DISC=0xd15c1301` が立つ**こと。memory の「C5/C6 は切断が届く」と整合。
既存記録には C5×Android の DISC 値は無かったので、これは本ラウンドで初めて数値化した。）

### 8.5 事前登録した予測の記録

| 予測 | 事前登録値 | 結果 |
|---|---|---|
| **P5** | **B × Android が OK に «変わる» 確率＝12%** | **★的中**（＝88% 側の «変わらない» が起きた）。OK に変わらなかった |
| **P4** | B × iPhone が OK＝80% | **未測定**（セル2＝次） |

★**P5 の事前登録した含意「変わらなければ Android 調査から toolchain を除外できる」は、
まだ書かない**。**`B × iPhone`（陽性対照）が OK になって初めて
«ビルド／ボード／ハーネスが生きている» が実証され、NG を «Android 固有» に帰属できる**。
現時点で言えるのは **«B × Android は NG»** までである。

---

## 9. スマホ セル2 ＝ **B × iPhone**（★陽性対照・準備完了・ユーザー確認待ち）

**目的**＝**「B が Android を直さない」と「B/ボード/ハーネスが今そもそも壊れている」の区別**。
既知ベースライン＝**C5 × iPhone = OK**。

- ビルド＝`build/c5_tc_B_ble`（**セル1と同一のアーム B**）を**再 flash**（`Hash of data verified`）。
- **全マーカー明示クリア**＋読み戻しで `STORE6/8/9/5 = 0x00000000` を確認。
- **真cold**（`cp2102n=GONE usbjtag=GONE` を読み戻し）→ POR で LP_AON も消える（§7.1 sentinel）。
- **BlueZ central 側 stale bond 削除**、**広告在席は «スキャン» で確認**（`RSSI=-41`・`Paired=False`）。

**判定の意味（事前に固定）**：
- **iPhone OK** ⇒ ビルド・ボード・ハーネスは健全 ⇒ **セル1の NG を «Android 固有» に帰属できる**
  ⇒ **toolchain を Android 調査の容疑者から外せる**。
- **iPhone NG** ⇒ **«Android 固有» と書いてはならない**。B 自体の破損が立つ ⇒ **A2 × iPhone** で切り分ける。

## 6. ★`asp3_core` の変更が必要と判明した項目（ユーザー判断事項）

1. **C3/C6 の `-march` を IDF 標準へ揃えるには `asp3_core` の変更が必要**。
   `asp3/asp3_core/arch/riscv_gcc/esp32c{3,6}/chip.cmake` は **submodule＝禁則**。
   - 参考（実測）：IDF 標準は **C3=`rv32imc_zicsr_zifencei`**（＝**C3 は現行と一致＝変更不要の見込み**）／
     **C6=`rv32imac_zicsr_zifencei`**（**C6 は現行 `rv32imc` と不一致＝要変更**）。
   - ★ただし **C5 で «ISA 変更は no-op» と実証された**ので、**C6 の march 変更も
     機能上の必要性は低い**（実測せずに断定はしない）。
2. **`asp3_core/cmake/toolchain-riscv64.cmake` の «既定 prefix が PATH 依存» 自体**は
   submodule 側の問題。本ラウンドは **本リポジトリ側に代替 toolchain file を置いて回避**した
   （submodule は無改変）。恒久的には asp3_core 側で既定を見直す価値がある。

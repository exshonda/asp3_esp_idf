# C3 evidence-10 — heap_caps_malloc 根治・esp-idf 供給統一・toolchain を IDF 標準へ整合

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `60:55:F9:57:BA:BC`（hub `1-6` port 1）**
正本: `evidence-c5-08-toolchain-idf-alignment.md`（C5 で確立した型）／
`evidence-c6-15-toolchain-idf-alignment.md`（C6 への転写実績）を C3 へ再転写。
commit: Phase1+2 `c2939f6`

> ★ヘッダの `toolchain:` は**実測値**を書く運用（C5/C6 の作法を踏襲）。
> 本ラウンドのアーム別実測値は §3.1 の表を参照。

---

## 0. 結論サマリ（先に）

1. **`heap_caps_malloc` を根治した**（C3 最後の «手渡しフラグ依存»）。
   **完了条件を実測で達成**＝C3 の WiFi・BLE とも **手渡しフラグ 0 本**で
   `rc=0`・implicit 0 件（§1.3）。
2. ★**前エージェントの機序説明は «半分» 誤っていた**（実測で訂正）：
   「**hal 側のコピーが include を欠く**のが原因」ではない。
   **hal 版・esp-idf 版とも欠いている**（`:470` / `:479`）。esp-idf 版は
   本来 `freertos/FreeRTOS.h` 経由で推移的に宣言を得るが，**本ビルドでは
   その `FreeRTOS.h` が我々の «スタブ» に解決される**＝**shadow が原因**
   ＝`esp_timer.h`（evidence-c6-15 §1）と**同一クラスの事故**（§1.1-1.2）。
3. ★**スタブを新設しない**という判断が本質：本物の `esp_heap_caps.h` は
   **インクルードパス上に既に在る**ので，`hal_stub/` に置くと**本物を
   shadow して同じバグを再生産する**。⇒ **本物を force-include** した（§1.4）。
4. ★**`-D MALLOC_CAP_*` は撤去できた＝旧コメントの事実認識が誤っていた**：
   「`bt.c` は `esp_heap_caps.h` を include しない」は**実測で偽**（`bt.c:13`・
   両供給とも無条件）。**真に欠いていたのは `phy_init.c` だけ**。
   ⇒ -D は冗長なだけでなく **`MALLOC_CAP_* redefined` 警告を実際に出していた**
   （2/9/6 件 → **0 件**）（§1.5）。
5. ★**コード生成は no-op**（較正＋ノイズ対照つき・§1.6）。ただし
   **「型互換だから無害」を前提にしていない**：**`bool` の前例は再現した上で，
   `void*` には «当たらない» ことを実測で示した**（機序つき）。
6. ★★**C3 の A/B は «本物» だった**（C5/C6 は退化＝バイト同一）：
   **BLE 110/122 オブジェクト相違**・**W1 187/222 相違**（§3.2）。
   ⇒ **実機 A/B に意味がある**＝本ラウンド最大の構造的差異。
7. **esp-idf 供給へ統一**（ユーザー決定）＝**hal 参照 0 を3指標で実測**
   （deps=0・リンク `-L`/`-T`=0・リンク行 any=0．**検出器は較正済み**＝
   hal fallback で 5679/4/158）（§2.1）。
8. **toolchain guard を転写**（`target.cmake` 2行・asp3_core 不要）。
   **FATAL 6/6 実発火**・**案内先の実在確認**・★**案内文の
   `install.sh esp32c5` ハードコードを発見して是正**（§2.3）。

---

## 1. Phase 1 — `heap_caps_malloc` の根治

### 1.1 ★前提の訂正（親の引き継ぎ／evidence-c6-15 §1.6(a) の機序説明）

引き継ぎは「**hal 側のコピーが `heap_caps_malloc` を推移的に宣言する include を欠く**」
＝**hal 固有の問題**と述べていた。**実測すると両供給とも落ちる**：

| 経路 | 供給 | 箇所 | 結果（GCC14・手渡しフラグ無し） |
|---|---|---|---|
| C3 WiFi | **esp-idf**（`ASP3_ESPIDF_SUPPLY=ON`） | `esp-idf/components/esp_phy/src/phy_init.c:479` | **error: implicit declaration** |
| C3 BLE | **esp-idf**（`=ON`） | 同上 `:479` | **error** |
| C3 BLE | **hal**（`=OFF`） | `hal/components/esp_phy/src/phy_init.c:470` | **error** |

⇒ **「どちらの経路が生きるか」は `ASP3_ESPIDF_SUPPLY` で決まり，どちらでも落ちる。**

### 1.2 ★真の機序（＝shadow。hal 固有ではない）

`#include` を実測（読解ではなく両ファイルの実物）：

| 供給 | `esp_heap_caps.h` を include? | `freertos/FreeRTOS.h` を include? |
|---|---|---|
| **esp-idf** `phy_init.c` | **していない** | **している**（`:26`） |
| **hal** `phy_init.c` | **していない** | **していない**（代わりに `platform/os.h`） |

- **esp-idf 版**：実物の IDF ビルドなら `freertos/FreeRTOS.h` 経由で推移的に
  宣言が来る。**しかし本ビルドの `freertos/FreeRTOS.h` は
  `asp3/target/esp32c3_espidf/bt/stub/include/freertos/FreeRTOS.h`（82行の
  スタブ）に解決される**＝**推移経路が切れている**。
  ⇒ **原因は «我々のスタブが本物を shadow していること»**。
  ＝`esp_timer.h` の shadow（evidence-c6-15 §1）と**同一クラス**。
- **hal 版**：そもそも `FreeRTOS.h` を include しない世代＝**上流の素の欠落**。

★**インクルードパスは «既に» 通っている**（実測）：WiFi/BLE とも
`-I<supply>/components/heap/include` がコンパイル行に在る。
⇒ **「パスが無い」問題ではない＝宣言が読まれていないだけ**。

### 1.3 完了条件（★実測。ここが本 Phase のゴール）

```
（すべて -DCMAKE_C_FLAGS 無し＝手渡しフラグ 0 本／toolchain = esp-14.2.0_20260121）
  C3 WiFi (esp-idf)  : configure rc=0 / build rc=0 / errors=0 / implicit-err=0 / implicit-warn=0
  C3 BLE  (esp-idf)  : configure rc=0 / build rc=0 / errors=0 / implicit-err=0 / implicit-warn=0
  C3 BLE  (hal)      : configure rc=0 / build rc=0 / errors=0 / implicit-err=0 / implicit-warn=0
  CMakeCache: CMAKE_C_FLAGS:STRING=（空）   ← 3構成とも実測
```

★**「implicit 0」は «測った 0»**：
- **陽性対照**＝修正前の同一3構成が **implicit-err=1** を出している（検出器は発火する）。
- **網羅性**＝修正後は **build rc=0＝全ファイルがコンパイルされた**ので，
  「最初のエラーで止まったから 1 件しか見えなかった」型の見落としが無い
  （ninja は既定で最初のエラーで停止する＝**修正前の 1 は «1 件以上» の意味しかない**）。
- **抑制の不在**＝ツリー内に `-Wno-error=implicit*` 等は**存在しない**（実測 grep。
  唯一の一致は本ラウンドで私が書いたコメント自身）。

⇒ ★**他に implicit declaration は «無かった»**（＝「古い GCC が隠していた実バグ」は
`heap_caps_malloc` 1件のみ）。**黙らせずに済んだ。**

### 1.4 シムの内容（★新規則を発明していない）

**`set_source_files_properties(<phy_init.c> PROPERTIES COMPILE_OPTIONS "-include;esp_heap_caps.h")`**
を `esp_wifi.cmake`（`${ESP_SUP_DIR}` 側）と `esp_bt.cmake`（`${BT_IDF}` 側）に各1つ。

**設計判断（なぜ他の型にしなかったか）**：

| 案 | 採否 | 理由（実測に基づく） |
|---|---|---|
| `hal_stub/include/esp_heap_caps.h` を新設 | ★**却下** | **本物がインクルードパス上に在る**ので**本物を shadow する**＝**この事故そのものの再生産**（`esp_timer.h` の轍） |
| 宣言を手写しした ext ヘッダ（C6 `bt_esp_timer_ext.h` 型） | 却下 | 本物を読ませれば**署名が定義と乖離しない**。C6 のそれは「stub が本物を隠していて編集不可」という**別の制約下の回避策**であり，ここは制約が無い |
| 全ファイルへ force-include | ★**却下** | `esp_heap_caps.h` は `multi_heap.h`/`sdkconfig.h` を引く⇒**カーネルまで巻き込む** |
| **対象1ファイルへ force-include** | **採用** | `set_source_files_properties` は **C6 `esp_wifi.cmake:910`・`esp_bt.cmake:497` に既存の前例**＝新規則ではない |

★**計器の検定（force-include が «噛んだ» か）**＝**実測**：
`-include esp_heap_caps.h` の出現数は **3構成とも «1»**、かつ**その1つは
`phy_init.c` のコンパイル行**（各供給の正しいコピー：esp-idf 構成では
`esp-idf/.../phy_init.c`、hal 構成では `hal/.../phy_init.c`）。
＝**噛んでいる**／**他ファイルには当たっていない**（陰性対照）。
（★`--wrap` が噛まずに «沈黙» した前例＝evidence-c3-05 の教訓を、force-include にも適用した。）

### 1.5 ★`-D MALLOC_CAP_*` の撤去（＝旧コメントの事実誤認の是正）

**実測**：`MALLOC_CAP_*` を使う**コンパイル対象**ファイルと、その include 状況：

| ファイル | `esp_heap_caps.h` を include | 判定 |
|---|---|---|
| `esp_phy/src/phy_init.c`（hal・esp-idf とも） | **していない** | ★**-D を真に必要としていた唯一のファイル** |
| `bt/controller/esp32c3/bt.c`（hal・esp-idf とも） | **している（`:13`・無条件）** | -D は**冗長** |
| `bt/porting/mem/bt_osi_mem.c` | している | 冗長 |
| `mbedtls/port/esp_mem.c` | している | 冗長 |

⇒ `esp_bt.cmake` の旧コメント
**「`bt.c` は `esp_heap_caps.h` を #include せず直値のビットマスクを期待する」は偽**。
（`MALLOC_CAP_RETENTION` を使う esp-idf 版 `bt.c:1077` も同様に include 済み。）

★**-D は有害だった（実測）**：`-D MALLOC_CAP_DMA=8` と本物の `#define MALLOC_CAP_DMA (1<<3)` は
**トークン列が違う**ため **`"MALLOC_CAP_DMA" redefined` 警告**が出ていた：

| 構成 | 修正前 | 修正後 |
|---|---|---|
| WiFi (esp-idf) | **2 件** | **0** |
| BLE (esp-idf) | **9 件** | **0** |
| BLE (hal) | **6 件** | **0** |

★値は一致（DMA=8=(1<<3)／INTERNAL=2048=(1<<11)／RETENTION=16384=(1<<14)）なので
**撤去は意味論を変えない**（§1.6 のコード生成実測でも裏付け）。

★**射程**：C5/C6 の `esp_wifi*.cmake`/`esp_bt.cmake` にも**同じ -D の wart が残る**
（実測：C5 `esp_wifi_v8.cmake:233-234`・`esp_bt.cmake:186-187`／C6 `esp_wifi.cmake:192`・
`esp_bt.cmake:237-238`）。**本ラウンドでは触っていない**（C3 スコープ・§5 に申し送り）。

### 1.6 ★修正前後のコード生成差（計器を明記）

**md5 は不正な計器**（`__TIME__` 埋込み）なので使わない。
**計器＝オブジェクト単位の逆アセンブル比較**（再配置可能＝リンクアドレスのシフトを受けない）。
**objdump の `#` 注釈とローカルラベル名を正規化**（前エージェントが**同一入力で偽の差分**を
掴んだ既知のノイズ源。硬化しないと「自分の変更が別ファイルを変えた」と誤報告する）。

**比較は «同一コンパイラ（GCC15）» 上で行う**＝私の変更だけを分離する
（before＝HEAD＋手渡しフラグ／after＝本修正・フラグ無し）。

| 対照 | 結果 | 意味 |
|---|---|---|
| ★**ノイズ対照**（同一入力の2ビルド） | **179 SAME / 0 DIFFER** | **計器は偽陽性を出さない** |
| ★**較正（陽性対照）**：`bool` vs 暗黙 `int` | **DIFFER=1** | ★**計器は盲目でない** |
| **WiFi**：before vs after（GCC15） | **179 SAME / 0 DIFFER** | **no-op** |
| **BLE**（hal・NimBLE 実効 ON）：before vs after（GCC15） | **122 SAME / 0 DIFFER** | **no-op** |

★**「型互換だから無害」を前提にしていない — 実測して機序まで示した**：

```
較正（bool・evidence-c6-15 が報告した現象を私の環境で再現）：
  explicit bool : auipc t1 / jr t1                       ← 末尾呼出
  implicit int  : addi sp,-16 / sw ra / jalr / lw ra / snez a0,a0 / addi sp,16 / ret

本件（void* ＝ heap_caps_malloc のパターン）：
  explicit void*: lui a1 / addi a1 / li a0,84 / auipc t1 / jr t1
  implicit int  : lui a1 / addi a1 / li a0,84 / auipc t1 / jr t1   ← **完全一致**
```

**機序**：`bool` は戻り値を **{0,1} へ正規化**する必要があるので暗黙 `int` だと
呼出側に `snez` が挿入される。**`void *` と `int` は RV32 では
どちらも 32bit・同じ a0** で返り，`(uint32_t *)` へのキャストは
**純粋な再解釈＝命令を要さない**。⇒ **`bool` の前例は `void*` には当たらない**。
★**これは «前提» ではなく «測定の結論»**（較正済みの計器が差を検出できる状態で 0 だった）。

★**私の測定バグ（自己申告・2件）**：
1. **比較範囲が足りていなかった**：最初 `CMakeFiles/asp.dir` だけを見て
   「BLE 90 objects・DIFFER=0」と読みかけた。**実際の総数は 122**で、
   `CMakeFiles/asp3.dir`（30）と `cfg1_out.dir`（2）を**測っていなかった**。
   ⇒ **32 個を «測らずに 0» と報告する寸前**。`CMakeFiles` 直下へ広げて再測した。
2. **CMakeCache を «実効値» と読みかけた**：`ESP32C3_BT_NIMBLE:BOOL=OFF` と
   表示されるので「NimBLE が入っていない＝別構成を測っている」と誤断しかけた。
   **実効値は ON**（`option()` の cache を `set()` の通常変数が shadow する）。
   **実測で確定**＝nimble オブジェクト **63 個**・総数 **122**＝既知良好 `c3ble_halbk` と**一致**。
   ⇒ **cache の表示は実効値ではない。オブジェクトを数えて確かめた。**

### 1.7 3チップ非回帰（★実測）

| チップ | 構成 | 結果 |
|---|---|---|
| **C3** | wifi_scan / wifi_dhcp / ble_host_smoke / bt_smoke（既定＝esp-idf） | **4/4 rc=0・implicit 0** |
| **C3** | ble_host_smoke / wifi_scan（`ASP3_ESPIDF_SUPPLY=OFF`＝hal fallback） | **2/2 rc=0**＝**可逆性維持** |
| **C5** | `ble_host_smoke_c5`（`ESP32C5_BT=ON`） | **rc=0・implicit 0** |
| **C6** | `ble_host_smoke_c6`（`ESP32C6_BT=ON`） | **rc=0・implicit 0** |
| **C6** | `wifi_scan`（`ESP32C6_WIFI=ON`） | **rc=0・implicit 0** |

★**構造的にも C5/C6 は無関係**（実測）：C5/C6 は**自分の** `esp_wifi.cmake`/`esp_bt.cmake`
を持つ（各 `target.cmake` の `TARGETDIR` は自分のディレクトリ）。
私の Phase1 の変更は **C3 の2ファイルのみ**。
（★`hal_stub/` は3チップ共有だが，**本ラウンドでは hal_stub を一切変更していない**
＝スタブ新設を却下したため。）

---

## 2. Phase 2 — esp-idf 供給統一 + guard 転写

### 2.1 ★hal 参照 0 の実測（deps «かつ» リンク行）

**指標は2系統を別々に測る**（`ninja -t deps` は **`-L`/`-T` を見ない**＝
evidence-c3-01 が確立した指標）。

★**まず検出器を間違えた（自己申告）**：`grep '/hal/'` は
**`esp-idf/components/hal`（esp-idf 自身の hal コンポーネント）に 70 件**当たる。
「hal 参照 70」と読みかけたが、**«何にマッチしたか» を確かめて発覚**。
**hal submodule は `<repo>/hal/`**＝パターンを厳密化して再測した。

| 構成（**既定**＝esp-idf 供給） | deps | リンク `-L`/`-T` | リンク行 any |
|---|---|---|---|
| **C3 WiFi**（`u_wifi`） | **0** | **0** | **0** |
| **C3 BLE**（`u_ble`） | **0** | **0** | **0** |
| ★**較正**：hal fallback（`ASP3_ESPIDF_SUPPLY=OFF`） | **5679** | **4** | **158** |

★**この 0 は «測った 0»**：較正アームで **3指標とも非0**＝検出器は盲目でない。
特に `-L`/`-T` は hal 側で実体を捕まえている（＝**blob 4 ディレクトリ＋ROM ld**）：
```
-Lhal/components/bt/controller/lib_esp32c3_family/esp32c3
-Lhal/components/esp_coex/lib/esp32c3
-Lhal/components/esp_phy/lib/esp32c3
-Lhal/components/soc/esp32c3/ld      （+ -T）
```

**外部 `~/tools/esp-idf` 参照＝0**（deps・commands とも。較正＝同じ corpus で
`/home/honda/TOPPERS` は 9935 件当たる＝grep は生きている）。

### 2.2 ★既定の変更（ユーザー決定）と、その帰結の記録

`ASP3_ESPIDF_SUPPLY` の既定を **全構成 ON**（従来：`ESP32C3_BT=ON` のときだけ OFF＝hal）。

★**帰結を承知の決定**（ツリー内コメントにも明記した）：
**現在の測定では esp-idf 供給の BT は bond が通らない**
（真cold A/B：esp-idf 失敗 2/2 ／ hal 成功 2/2＝evidence-c3-03 §5）。
**本変更で C3 の既定は bond を失う。** 可逆＝`-DASP3_ESPIDF_SUPPLY=OFF`。
（`ASP3_BT_IDF_V554` は追従＝基盤と BT ツリーの混成は `esp_bt.cmake` の FATAL_ERROR で禁止。）

### 2.3 ★guard 転写と FATAL の実発火

`asp3/target/esp32c3_espidf/target.cmake` に **C5/C6 と同じ2行**（+経緯コメント）。
**asp3_core 変更不要**（`asp3/cmake/esp_toolchain_check.cmake` はチップ非依存）。

**FATAL/PASS の実発火（6/6）**：

| # | 条件 | 期待 | 実測 |
|---|---|---|---|
| 1 | toolchain file 渡し忘れ（asp3_core の toolchain-riscv64.cmake） | FATAL | **発火**（`Wrong C compiler` / `-dumpmachine` 不一致） |
| 2 | 正しい版（esp-14.2.0_20260121） | PASS | `ESP toolchain OK` |
| 3 | 版違い（esp-15.2.0＝**C3 の既知良好**） | FATAL | **発火**（found/expected を表示） |
| 4 | 退避路 `-DASP3_ESP_EXPECTED_TOOLCHAIN=esp-15.2.0_20251204` | PASS | **通る** |
| 5 | 退避路 `-DASP3_ESP_TOOLCHAIN_CHECK=OFF` | PASS | **通る** |
| 6 | 存在しない版 | FATAL | **発火**（`not found`） |

★**#1 は C3 で «転写の価値» そのもの**：C6-15 §2.4 が記録するとおり、guard の無い
C3 は**渡し忘れても configure が通り、ビルド途中で `-mabi=ilp32` の
不可解なエラー**になっていた。**本転写でこれが configure 段階の診断済み失敗になる。**

★**案内先の実在確認**（「存在しない退避先を案内する」事故が過去2回）：

| 案内 | 実在 |
|---|---|
| `<repo>/asp3/cmake/toolchain-esp32-riscv32.cmake` | **在** |
| `<repo>/esp-idf/install.sh` | **在** |
| `<repo>/esp-idf/tools/tools.json` | **在** |
| `esp-15.2.0_20251204`・`esp-14.2.0_20260121` の導入先 | **在** |

★★**案内文のバグを1件発見して是正**：`esp_toolchain_check.cmake:109` は
**チップ非依存のファイルなのに `./install.sh esp32c5` とハードコード**していた
⇒ **C3/C6 のユーザーに esp32c5 と案内する**（riscv32-esp-elf は RISC-V 全チップ共通なので
**実害は無かったが案内としては誤り**）。`ASP3_TARGET` からチップ名を導くよう修正し、
**3チップで実発火して確認**（`install.sh esp32c3` / `esp32c5` / `esp32c6`）。
**メッセージのみ＝挙動は不変**（C5/C6 とも FATAL は従来どおり発火）。

### 2.4 ★march / blob の ISA（★スコープを広げていない）

**C3 の march は IDF 標準と «既に一致»**＝**論点なし**（3点で確認）：

| 出典 | 値 |
|---|---|
| `esp-idf/components/soc/project_include.cmake:8-10`（C2/**C3**） | `rv32imc_zicsr_zifencei` |
| `asp3_core/arch/riscv_gcc/esp32c3/chip.cmake:24,35` | `rv32imc_zicsr_zifencei` |
| ★**実際のコンパイル行**（読解でなく実測） | `-march=rv32imc_zicsr_zifencei -mabi=ilp32` |

⇒ **`asp3_core` の変更は不要**（C5/C6 と違い C3 では ISA の論点が発生しない）。
**march 実験は行っていない**（不要＝スコープを広げない、というユーザー方針に合致）。

**blob の `Tag_RISCV_arch`（実際にリンクされる esp32c3 blob）**：

| blob | `Tag_RISCV_arch` | atomic 命令数 |
|---|---|---|
| `libbtdm_app.a` / `libbtdm_app_flash.a` / `libcoexist.a` | `rv32i2p0_m2p0_c2p0`＝**rv32imc（A なし）** | **0** |
| `libphy.a` / `libbtbb.a` / `libbttestmode.a` / `librfate.a` / `librftest.a` | **`.riscv.attributes` 無し** | **0** |

★**検出器の較正（3段）**：
- **陽性対照**：`__atomic_fetch_add`/`__atomic_exchange_n` を **rv32imac** で
  コンパイル＋**アーカイブ化** ⇒ **2 件検出**（`.a` でも盲目でない）。
- **陰性対照**：同じソースを **rv32imc** ⇒ **0 件**。
- **空振り防止**：`libbtdm_app.a`＝**101 メンバ・97,244 命令**／`libphy.a`＝**20 メンバ・16,864 命令**
  を実際に逆アセンブルできている ⇒ **「0」は «測っていない 0» ではない**。

⇒ **blob は A 拡張を要求も使用もしない**＝C3 の `rv32imc` で整合。

---

## 3. Phase 3 — 実機 A/B の設計と★予測の事前登録

### 3.1 ★アーム定義（★親の指示どおり «自分で実測して» 同定した）

**C3 の «既知良好» の実体**（`build/*/CMakeCache.txt` の `CMAKE_C_COMPILER_AR` を実測）：

| 既知良好 | build dir | 実際のコンパイラ |
|---|---|---|
| **W1**（真cold DHCP+ping・evidence-c3-02） | `c3_w1` | **esp-15.2.0_20251204（GCC 15.2.0）** |
| **BLE/bond**（hal・真cold bond 成功 2/2・evidence-c3-03） | `c3ble_halbk` | **esp-15.2.0_20251204** |
| **BLE/bond 失敗アーム**（esp-idf・失敗 2/2） | `c3ble_idf` | **esp-15.2.0_20251204** |
| 機序局在（evidence-c3-04/05） | `c3map_A/B`・`c3tx_A/B` | **esp-15.2.0_20251204（全て）** |

⇒ ★★**bond 失敗は «すべて GCC15 で» 測られていた＝GCC14 では一度も測られていない**
（親の「汎用 GCC13.2 か esp-15.2.0 か、自分で確認しろ」への回答＝**esp-15.2.0**）。
**∴ 本ラウンドの問いは «未測定の空白» に対する真の問いである。**

**アーム**：

| アーム | コンパイラ | 供給 | 位置づけ |
|---|---|---|---|
| **A** | **esp-15.2.0_20251204（GCC15）** | esp-idf | **C3 の既知良好 toolchain**（bond 失敗が測られた条件の再現） |
| **B** | **esp-14.2.0_20260121（GCC14）** | esp-idf | **IDF v5.5.4 標準**＝**本ラウンドの被験体** |
| ★**C_hal** | esp-15.2.0（GCC15） | **hal** | ★**陽性対照**＝bond が通る唯一の既知構成 |

★**C_hal を置く理由（evidence-c3-03 が確立した作法）**：
**「A も B も失敗」は帰属不能**（私の forget 手順・central・RAM bond 汚染でも同じ絵になる）。
**同一セッションで C_hal が成功して初めて「ハーネスは生きている」と言える。**

### 3.2 ★★ビルド生成物の実測（実機の前に判明した決定的事実）

| 比較 | オブジェクト | `.text` | 実験として |
|---|---|---|---|
| **BLE（esp-idf）: A vs B** | ★**110 / 122 相違**（同一は 12 のみ） | 282,336 → 283,712（**+1,376 B**） | ★★**本物** |
| **W1: A vs B** | ★**187 / 222 相違** | 498,256 → 500,640（**+2,384 B**） | ★★**本物** |

⇒ ★★**C5/C6 との構造的な違いが確定した**：
C5/C6 では BLE の A/B が**退化**（バイト同一・差は `__TIME__` のみ）だったため
「B は壊しようがない」と静的に言えた。**C3 は違う＝GCC15 と GCC14 はメジャー版違いで、
機械語が実際に大きく変わる**（**相違オブジェクトに `ble_sm.c` `ble_sm_alg.c` `ble_sm_sc.c`
`bt.c` が含まれる**＝**bond 失敗の機序が局在した当のコード**）。
⇒ **実機 A/B は «同じプログラムを2回動かす» ではない＝本物の実験。**

★**正直な申告**：§2.4（march）と §3.2（A/B のバイト差）は **«予測» ではなく先に実測した**。
後付けで「予測が当たった」とは書かない。

### 3.3 ★★予測の事前登録（実機の前に確定・後付け禁止）

**P1. B × W1（真cold で DHCP による IP 取得 + ping）が通る確率＝85%**
- 根拠：A/B は**本物の別プログラム**（187/222 相違）。しかし **B は Espressif が
  v5.5.4 に指定する版**＝供給（ソース・blob）と同世代＝**整合性は B の方が高い**。
  既知ベースライン＝真cold W1 達成（ping ok21/49・NG 0・2/2＝evidence-c3-02）。
- ★**外れたら何を意味するか**：**IDF 標準コンパイラが C3 の WiFi を壊す**＝重大。
  ISA は無関係（§2.4 で C3 は既に IDF 標準と一致）⇒ **コンパイラ（15.2→14.2）に帰属**。
  ★ただし**まず環境（AP/電波）の非決定性を疑う**（C5 §7.2 で A run1 が
  «DHCP timeout» と揺れた前例）⇒ **各アーム最低2回・単一 run で結論しない**。

**P2. B × BlueZ（`hci0` で adv 到達・D-2b）が通る確率＝85%**
- 根拠：esp-idf 供給は **adv/D-2b までは真cold で成立**（evidence-c3-03 §5）。
  B は compiler だけが違う。
- 外れたら：**まず A と C_hal を同セッションで測り**、A も落ちるなら環境／
  A が通って B だけ落ちるなら **toolchain に帰属**。

★★**P3. B × bond（esp-idf 供給・BlueZ・フレッシュ）が «通る» 確率＝20%**（★本ラウンドの問い）
- ★**低いが «測らない理由» ではない**（親の指示）。**通らなければ toolchain を
  «ここからも除外» できる＝それも成果**。
- **20% の根拠（低い側）**：evidence-c3-05 が機序を
  **「DHKey Check 送出後にコントローラが暗号開始を完遂しない（LTK Request が
  一度も来ない）」**＝**blob 側**に局在させた。**blob（`libbtdm_app.a` 859e8c8e）は
  A/B で同一バイト**（コンパイラは blob を作り直さない）⇒ **toolchain が触れるのは
  glue（`bt.c`）とホストだけ**。ホスト SM は **C5 クロスチップ対照で無罪**。
- **20% が 0% でない根拠（高い側・実測に基づく）**：
  (i) ★**`bt.c` は A/B で実際に相違する**（§3.2）＝**コントローラ glue は toolchain の射程内**。
  (ii) ★**「GCC15 が bond を壊す」は成り立たない**という制約が既にある＝
      **hal 供給は GCC15 で bond 成功 2/2**。⇒ もし B で通るなら機序は
      **«GCC15 × esp-idf 版 bt.c» という組合せ固有**（esp-idf の `bt.c` は
      OSI_VERSION `0x0001000B`＋`_malloc_retention` で hal 版と 91 行差＝**別のコード**）。
      これは**狭いが実在しうる経路**。
- ★**外れたら（＝B で bond が通ったら）**：**重大な発見**＝
  「bond 失敗は供給に帰属」（evidence-c3-03）を **«供給 × toolchain の交互作用»** へ
  書き換える必要がある。★**その場合でも即断せず**、(i) A を同セッションで再測して
  **本当に A だけが落ちるか**、(ii) §3.2 のビルド同定（どちらが載っているか）を
  独立に確認する。
- ★**予測どおり（＝B でも失敗）なら**：**toolchain を除外**できる
  ＝機序は **blob（+ 供給固有の glue）に、コンパイラと独立に**局在する、が強まる。

**P4. B × iPhone が OK である確率＝5%**
- 根拠：★**C3 × iPhone は供給不問で NG**（既知＝hal でも esp-idf でも NG）。
  かつ B は esp-idf 供給＝**bond 自体が P3 の見込みで落ちる**。
  ⇒ **二重に低い**。**iPhone セルは «B が bond を通した場合にのみ» 意味を持つ**。
- ★**C3 の実害は «timeout» ではなく «切断が来ずに詰まること»**（`DISC=0` ⇒
  リンクを握って広告停止 ⇒ 復旧はリセットのみ ⇒ **そのリセットが RAM bond を消す**）。
  **⇒ iPhone セルは «デバイスを詰まらせて次のセルを壊す» 危険がある**⇒ **最後に回す**。

**P5. B × Android が OK である確率＝12%**
- 根拠：既知＝**hal なら OK（`0x5dc00011`）／esp-idf なら NG（`ENC=0x5de0000d`＝`ETIMEOUT`・詰まる）**。
  B は **esp-idf 供給**＝既知 NG 側。toolchain が変わるのが唯一の新規性。
  **P3 と強く相関**（BlueZ で bond が通らなければ Android も通らない見込み）。
- ★**外れたら（＝Android が OK になったら）**：P3 と同じく重大＝toolchain 依存の証拠。
  ★ただし**まず (i) 両スマホの forget 漏れ、(ii) 真cold による RAM bond 消失、
  (iii) 古い bond の再利用**を疑う（**«Android で繋がらない/繋がる» の大半は
  測定手順が作った交絡**＝MEMORY.md の最大の教訓）。

**★反証条件（先に固定）**

- **H1＝「IDF 標準 toolchain は C3 の «既知良好» を壊さない」**
  - **反証条件**：**A が同条件・同セッションで通るのに B が W1 か BlueZ adv で落ちたら H1 を捨てる**。
  - ★**反証条件自体の検算（独立測定）**：**「A も B も落ちた」＝環境**（帰属不能）⇒
    **C_hal（陽性対照）を測り、それも落ちるならハーネス／central／AP を直してから測り直す**。
    **「A 成功・B 失敗」でなければ toolchain に帰属してはならない。**
- **H2＝「bond 失敗は toolchain 非依存（blob に局在）」**（evidence-c3-05 の帰結）
  - **反証条件**：**B で bond が «フレッシュに» 成立したら H2 を捨てる**。
  - ★**反証条件自体の検算**：**「bond 成功」は «古い bond の再利用» でも起こる**
    ⇒ **毎試行 `remove`（forget）してフレッシュで測る**／**`PAIR` タグ `0x5DC0` の
    有無で «今回発火したか» を判別する**（★C3 は PAIR(`0x54`) と ENC(`0x58`) が
    **別レジスタ**なので `0x5DC0` は**観測可能**＝C5 の原理的不能とは違う。
    ★ただし `0x54` は bt_shim intr トレースと**共用**＝**タグで引く**）。
- **H3（土台）＝「B が W1 か BlueZ adv で落ちたら、スマホセルへ進まず即報告」**
  （壊れた土台でスマホ結果を採らない＝PVCY draft の前例）。

### 3.4 ★測定の作法（自分に課す）

- **真cold の唯一の証明＝`sudo uhubctl -l 1-6 -p 1 -a off` + by-id 消滅の読み戻し**
  （★`rst:0x1 (POWERON)` は証明にならない／★uhubctl は segfault しても電源は切れる
  ＝**読み戻しだけが証拠**）。★**`-p 2`(nested hub)・`-p 3/4`(C5) を叩かない。
  hub `1-5` は別プロジェクト＝絶対に触らない。**
- **by-id のみ**（`/dev/ttyACM*` は電源断で番号が入れ替わる＝別プロジェクトの S3 が居る）。
  **write は必ず `--chip esp32c3`。** ★**`esptool | head -N` 厳禁**（SIGPIPE が write-flash を切る）。
- **C3 は UART ブリッジ無し**＝コンソールで cold を観測できない ⇒ **RTC STORE マーカー方式**
  （`esptool --before usb-reset --after no-reset --no-stub read-mem`。
  ★`--before usb-reset` は **ROM download モードに留めアプリを走らせない**＝マーカーは凍結）。
- ★**真 POR が C3 の RTC STORE をクリアするかは «未検証»**（C5/C6 では実証済み）
  ⇒ **センチネルを植えて確かめてから**「クリアされる/されない」を前提にする。
- **マーカーはアドレスでなくタグで引く**（`0x50`SYNC/`0x5C`ADV/`0xC4`advRC は
  **boot クリアされない**⇒セル毎に明示クリア／`0xC0`CONN・`0xB8`DISC はアプリが毎起動0クリア／
  **`0xBC`(STORE5) は ROM が上書き＝使わない**／`0x58` は WRITE/BOOT_TRACE/ENC の**共用＝last-wins**）。
- **bond は RAM backed**＝電源断のたびにデバイスは鍵を失う ⇒ **毎セル «両方の» スマホを
  forget → BT OFF/ON**（鍵を持ったスマホの自動再接続が次セルを汚す＝9時間溶かした事故）。
  **1ビルド1端末。**
- **ユーザーが試す «前» にマーカーを読まない**（download mode に落ちて広告が止まる）。
  **OTA/在席確認はスキャンを先に。**
- **BlueZ は D-Bus 直叩き**（`bluetoothctl` パイプ駆動は使わない）／**agent を自分で登録**
  しないと pairing 認可不可。

---

（§4 以降＝実機実測結果は、測定後に追記する）

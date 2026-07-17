# C6 evidence-15 — esp_timer ヘッダ shadow の根治（3チップ共有）と C6 への toolchain 転写

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`（= board C 本体）／hub `1-6` port 2**
正本: `evidence-c5-08-toolchain-idf-alignment.md`（C5 で確立した型を C6 へ転写）
commit: Phase1 `24715e5` ／ Phase2 `c981bf5`

> ★ヘッダの `toolchain:` は**実測値**を書く運用（evidence-c5-08 の作法を踏襲）。
> 本ラウンドのアーム別実測値は §3.1 の表を参照。

---

## 0. 結論サマリ（先に）

1. **`esp_timer.h` の shadow を根治した**（`hal_stub/include/esp_timer.h` に宣言2本）。
   **C5 BLE は手渡しフラグ無しでビルドできる**（完了条件・実測 rc=0／implicit 0件）。
2. ★**「戻り値型互換だから無害」は実測で FALSE だった**＝`esp_timer_is_active` の
   本物の戻り値は **`bool`**（暗黙宣言は `int` を仮定）⇒ **コード生成が実際に変わった**
   （`npl_freertos_callout_is_active` が **0x16→0x08 バイト**・`snez` とスタックフレームが消え
   **末尾呼出**になった）。**ただし benign**＝ただしそれは**前提ではなく実測の結論**（§1.4）。
3. ★**shadow の被害は C3 と C5**（両方 implicit 2件）。**C6 は無傷**＝C6 は
   **`bt/bt_esp_timer_ext.h` を force-include** して**同じ2本を独自に宣言済み**だった
   （＝**私の修正後の機械語＝C6 が最初から出荷していた機械語**。§1.5）。
4. ★★**C3/C5 の «既知良好» は «記録されていない手渡しフラグ» に依存していた**＝
   evidence-c5-08 §4 は「C5 BLE」の話だったが、**C3 も同じ**（`c3_base_hal`・`c3_cold`・
   `c3_d2d` すべて `CMAKE_C_FLAGS=-Wno-error=implicit-function-declaration`）。§1.6。
5. ★**C6 の march 変更は不要＝`asp3_core` 変更は不要**（**C6 で独立に実測**）。
   `rv32imc→rv32imac` は **135/135 オブジェクト同一・`.text` 同一・atomic 0**＝**no-op**。
   **実験が効いていた証拠＝ELF の `Tag_RISCV_arch` が実際に変わった**（§2.3）。
6. ★**toolchain guard を C6 へ転写**（`target.cmake` 2行・**submodule 無改変**）。
   **FATAL を6通り実発火**・**案内する退避先の実在も確認**（§2.4）。
7. ★★**本ラウンドの «B × Android» は退化した実験**＝**BLE の B は既知良好 A2 と
   バイト同一（差は `__TIME__` の3バイトのみ）**⇒ **B は Android を壊しようがない**（§3.2）。
   **W1 の A/B は本物**（177/230 オブジェクト相違・`.text` −4272 B）。

---

## 1. Phase 1 — `esp_timer.h` の shadow 根治

### 1.1 本物の宣言（実測）

| 関数 | 本物の宣言（出典） | 暗黙宣言との差 |
|---|---|---|
| `esp_timer_is_active` | **`bool`** `esp_timer_is_active(esp_timer_handle_t)`（`esp_timer.h:322`） | ★**戻り値 `bool` vs `int`＝差あり** |
| `esp_timer_get_expiry_time` | `esp_err_t` `esp_timer_get_expiry_time(esp_timer_handle_t, uint64_t*)`（`:269`） | `esp_err_t`＝`int`＝**差なし** |

### 1.2 shadow の実測（include 順）

| チップ | stub `esp_timer.h` | 本物 | 判定 |
|---|---|---|---|
| C5 | **#4** | #44 | **stub が勝つ＝shadow** |
| C6 | **#4** | #45 | **stub が勝つ＝shadow**（ただし §1.5 で無害化されていた） |

HEAD で C5 BLE を素ビルド＝**rc=1・implicit 2件のみ**（他のエラー 0）＝evidence-c5-08 §4 を再現。

### 1.3 完了条件（★実測）

```
-DESP32C5_BT=ON -DASP3_APPLNAME=ble_host_smoke_c5   （手渡しフラグ無し）
  → configure rc=0 / build rc=0 / implicit error 0 / implicit warning 0
  → CMakeCache: CMAKE_C_FLAGS:STRING=（空）
```

### 1.4 ★修正前後のバイナリ差（計器を明記）

**md5 は不正な計器**（`__TIME__` 埋込み）なので使わない。**使った計器＝
オブジェクト単位の逆アセンブル比較**（`.obj` は再配置可能＝リンクアドレスのシフトを受けない）。

★**計器の較正（3段）**：
- **較正**：既知変更オブジェクト（`npl_os_freertos`）で **DIFFER**／無関係
  （`task.c`）で **SAME** を確認＝**識別できる**。
- **変異検定**：ラベル正規化が**本物の opcode 変更を隠さない**ことを確認（意図的に
  1バイト変えて **検出された**）。
- **ノイズ対照**：★**同一入力の2ビルドで `efuse_hal.c.obj` が «相違» と出た**
  ⇒ objdump の `#` 注釈（`.LASF0+0xe`→`+0xd`）だけの差で**命令バイトは同一**
  ⇒ **計器を硬化**（`#` 注釈とローカルラベル名を除去）し**同一入力の対照で 0 differ** を確認。
  **この対照が無ければ «私の変更が efuse_hal を変えた» と誤読していた**。

**結果（硬化後の計器）**：

| チップ | 構成 | 相違オブジェクト | 判定 |
|---|---|---|---|
| **C3 BLE** | **GCC15＝C3 の既知良好コンパイラ** | **121 同一 / 1 相違** | `npl_os_freertos.c.obj` のみ |
| **C5 BLE** | esp-14.2.0_20260121 | **127 同一 / 1 相違** | `npl_os_freertos.c.obj` のみ |
| **C6 BLE** | esp-14.2.0_20260121 | **135 同一 / 0 相違** | ★**完全な no-op** |

**変わった中身（★これが本題）**：

```
npl_freertos_callout_is_active   0x16 (22B)  ->  0x08 (8B)

before:  lw a5,0(a0) / addi sp,sp,-16 / sw ra,12(sp) / lw a0,0(a5)
         jal esp_timer_is_active
         lw ra,12(sp) / snez a0,a0 / addi sp,sp,16 / ret
after:   lw a5,0(a0) / lw a0,0(a5)
         j esp_timer_is_active        ← 末尾呼出・フレーム無し・snez 無し
```

⇒ **「戻り値型互換だから無害」は前提として誤り**。暗黙宣言は `int` を仮定するため
呼出側が `bool` へ正規化する **`snez` を挿入**していた。正しい `bool` 宣言だと
**その正規化が不要**になり、後処理が消えて**末尾呼出**へ最適化された。

**★それでも benign と言える根拠（実測。推論ではない）**：

```
esp_timer_is_active:
   beqz a0, .L   /  lw a0,4(a0)  /  snez a0,a0  /  ret
.L: li a0,0      /  ret
```
**callee 自身が末尾で `snez` して返り値を {0,1} に正規化している**（null 経路も `li a0,0`）
⇒ **a0 ∈ {0,1} が保証**され、呼出側で消えた `snez` は**その範囲で恒等**
（`snez(0)=0`・`snez(1)=1`）⇒ **意味論は不変**。

★**独立の裏取り**＝**C6 は `bt/bt_esp_timer_ext.h` で同じ `bool` 宣言を最初から使っていた**
（§1.5）⇒ **修正後の機械語は C6 が実機で出荷し続けてきたもの**（Android OK を含む）。

### 1.5 ★C6 が無傷だった理由（＝私の修正の正しさの独立証拠）

C6 の BLE コンパイル行は **`-include bt/bt_esp_timer_ext.h`**（C6 の `esp_bt.cmake` が付与）。
その中身は**私が stub へ足したのと同一シグネチャの2本**：

```c
esp_err_t esp_timer_get_expiry_time(esp_timer_handle_t timer, uint64_t *expiry);
bool esp_timer_is_active(esp_timer_handle_t timer);
```

同ファイルのコメントは根本原因を正しく把握したうえで
**「hal_stub/esp_timer.h は C3 領域（別エージェント担当）で編集不可のため」**
C6 側だけで回避したと明記している。
⇒ **本ラウンドの修正は、そのコメントが «できない» と書いた根本修正そのもの**。
⇒ C6 の当該 force-include は**冗長になった（同一宣言＝合法・実測で衝突なし）**が、
**本ラウンドでは撤去していない**（スコープ外・ユーザー判断）。

### 1.6 ★他に見つかった «隠されていた実バグ»（★報告のみ・修正していない）

**(a) `heap_caps_malloc` の implicit declaration（C3 のみ・pre-existing）**

| 経路 | 箇所 | 供給 |
|---|---|---|
| C3 BLE | `hal/components/esp_phy/src/phy_init.c:470` | hal |
| C3 WiFi | `esp-idf/components/esp_phy/src/phy_init.c:479` | esp-idf |

★**pre-existing の証明**＝**私の編集を revert して HEAD で再ビルド＝同一エラーが再現**。
**機序**＝`phy_init.c` は `heap_caps_malloc` を呼ぶが `esp_heap_caps.h` を include していない
（**hal 版・esp-idf 版とも**）。esp-idf 版は `freertos/FreeRTOS.h`／`esp_check.h` 経由で
**推移的に**宣言を得るが、**hal 版はその2つを include していない**世代のスナップショット。
**hal は submodule＝編集禁止**⇒ shim/force-include が必要＝**ユーザー判断事項**。
**C3 の既知良好は、このためだけに今も手渡しフラグを必要とする**（私の修正後は **3件→1件**）。

**(b) stub の他の shadow（構造調査）**

| stub ヘッダ | 本物 | 判定 | gap |
|---|---|---|---|
| **`esp_timer.h`** | `esp-idf/components/esp_timer/include` | **shadow** | **修正後も残り10本** |
| **`endian.h`** | `esp-idf/components/bt/porting/include/os/endian.h` | **shadow** | 識別子 49 |
| 他 21 本（`stdio.h`/`string.h`/`nvs.h`/`esp_netif.h`/`driver/gpio.h` 等） | — | **本物が include path に無い＝shadow ではない** | — |

★**`esp_timer.h` に残る10本のうち2本は «同じ型の潜在バグ» として危険**：

| 関数 | 戻り値 | 危険度 |
|---|---|---|
| **`esp_timer_get_next_alarm`** | **`int64_t`** | ★**暗黙 int だと a0 のみ読む＝上位32bitを黙って捨てる** |
| **`esp_timer_get_next_alarm_for_wake_up`** | **`int64_t`** | ★**同上** |
| 他8本 | `esp_err_t`/`void` | RV32 では暗黙 int と一致＝benign |

★**現時点では «潜在»**＝**10本とも現行の全経路から呼ばれていない**（実測 0 参照）。
**検出器の較正**＝既知呼出の `esp_timer_is_active`=7ファイル・`esp_timer_get_expiry_time`=6ファイル
で**非0を確認**してから「0」を主張している。

**(c) `esp_wifi_v8.cmake:92-97` の4フラグ抑制は «現在は何も隠していない»**

`-Wno-error=implicit-function-declaration` / `implicit-int` / `int-conversion` /
`incompatible-pointer-types` の4本（C5 WiFi 経路）。**実測＝4本とも 0 件**。
★**この 0 は較正済み**＝(i) 232 オブジェクトを実際にコンパイルし**ログに警告が 65 件届いている**、
(ii) **陽性対照**（意図的な implicit 宣言）で**検出器が 1 件検出**。
⇒ 抑制の前提だった「hal は編集できずヘッダ追加もできない」は
**WiFi 供給が esp-idf submodule へ移行した時点で失効**している。**撤去は可能と思われるが本ラウンドでは触っていない**（スコープ外）。

### 1.7 3チップ非回帰（★実測）

| チップ | 構成 | HEAD | 本修正後 | 相違 |
|---|---|---|---|---|
| **C3 BLE**（hal・GCC15＝既知良好） | rc=0 | implicit **3** | implicit **1** | 121/1（意図どおり） |
| **C5 BLE** | rc=1（**素では不能**） | — | **rc=0・implicit 0** | 127/1（意図どおり） |
| **C6 BLE** | rc=0・implicit 0 | rc=0・implicit 0 | **135/0＝no-op** |
| **C5 WiFi** | — | rc=0・implicit 0 | 影響なし |
| **C6 WiFi** | — | rc=0・implicit 0 | 影響なし |
| **C3 WiFi** | rc=1（`heap_caps_malloc`・**pre-existing**） | 同左 | **変化なし** |

⇒ **C3/C5 の «挙動» は変えていない**（唯一の機械語変化は §1.4 の1関数＝
**C6 が既に出荷している形へ寄せる方向**）。

---

## 2. Phase 2 — C6 への転写

### 2.1 C6 blob の実測（★C5 から引き写さず、C6 で測った）

`riscv32-esp-elf-readelf -A` で **esp32c6 のみ**（`esp32c61` は別チップ＝除外）16 個：

| blob | `Tag_RISCV_arch` |
|---|---|
| `libble_app.a` / `libcoexist.a` / `libcore.a` / `libespnow.a` / `libmesh.a` / `libnet80211.a` / `libpp.a` / `libsmartconfig.a` / `libwapi.a` | `rv32i2p0_m2p0_c2p0` ＝ **rv32imc（A なし）** |
| `libble_mesh.a` | `rv32i2p1_m2p0_c2p0_zmmul1p0_zca1p0` ＝ **A なし** |
| `libphy.a` / `libbtbb.a` / `librfate.a` / `librftest.a` / `libbttestmode.a` | **`.riscv.attributes` セクション自体が無い** |
| ★**`libopenthread_br.a`** | **`rv32i2p1_m2p0_a2p1_c2p0_..._zaamo1p0_zalrsc1p0` ＝ A あり** |

★**C5 と違い «A を宣言する blob» が実在した**（＝「C6 の blob は別物かもしれない」は正しい疑い）。
**ただし実測で無害**：

- **`libopenthread_br.a` は ASP3 の C6 ビルドに一切リンクされない**（実測：
  WiFi＝`-lcoexist -lcore -lespnow -lmesh -lnet80211 -lphy -lpp`／
  BLE＝`-lble_app -lbtbb -lcoexist -lphy`）。
- **atomic 命令数＝リンクされる全 blob で 0**。★**`libopenthread_br.a` 自身も 0**
  （＝Tag は「コンパイル時 ISA の宣言」であって「使用の証拠ではない」）。

★**検出器の較正（2段）**：
- **陽性/陰性対照**：`__atomic_fetch_add`/`__atomic_exchange_n` を **rv32imac** で
  コンパイル＝**2件検出**（`amoadd.w.aqrl`/`amoswap.w.aqrl`）／**rv32imc** で **0 件**。
- ★**アーカイブ較正**：**測定対象は `.a` であって `.o` ではない**ので、
  `.a` に固めた対照でも **2件検出**＝**アーカイブでも盲目でない**ことを確認。
- **空振り防止**：`libopenthread_br.a` は **21 メンバ・10,883 命令**を実際に逆アセンブル
  できている（＝「0」は «測っていない 0» ではない）。

### 2.2 IDF 標準 march（出典）

`esp-idf/components/soc/project_include.cmake:8-16`：
**C2/C3 → `rv32imc_zicsr_zifencei`** ／ **C5/C6/C61/H2/H21 → `rv32imac_zicsr_zifencei`**。
ASP3 の C6 現行＝`asp3/asp3_core/arch/riscv_gcc/esp32c6/chip.cmake:24,35` の
**`rv32imc_zicsr_zifencei`** ⇒ **IDF 標準とは不一致**（C5 と同じ状況）。

### 2.3 ★C6 で「march 変更は no-op」を実測（＝`asp3_core` 変更は不要）

★**まず «効かないレバー» を掴みかけた**：`-DCMAKE_C_FLAGS=-march=rv32imac...` は
**chip.cmake の `-march` より «前» に置かれる**ため **rv32imc が後勝ちして無効**だった
（コマンドラインを実測して発覚）。**気付かなければ «rv32imc と rv32imc» を比べて
「差が無い＝no-op」と結論する詐欺実験**になっていた。
⇒ 有効なレバー＝**C6 `target.cmake` の末尾で `ASP3_COMPILE_OPTIONS` に追記**
（`CMakeLists.txt:144` で target.cmake を include → `:236/:334` で適用＝**後勝ち**）。

**実験が効いた証拠（＝陽性対照）**：**生成 ELF の `Tag_RISCV_arch` が実際に変わった**

| ビルド | ELF の `Tag_RISCV_arch` |
|---|---|
| rv32imc（現行） | `rv32i2p1_m2p0_c2p0_zicsr2p0_zifencei2p0_zmmul1p0` |
| **rv32imac（実験）** | `rv32i2p1_m2p0_**a2p1**_c2p0_zicsr2p0_zifencei2p0_zmmul1p0_**zaamo1p0_zalrsc1p0**` |

**それでも結果**：

| 指標 | rv32imc | rv32imac |
|---|---|---|
| オブジェクト比較 | — | **135 同一 / 0 相違** |
| `.text` | 401392 | **401392（同一）** |
| 最終イメージの atomic 命令 | 0 | **0** |

⇒ ★**C6 でも `rv32imc→rv32imac` は機械語として no-op**＝
**march を変える機能上の必要は無い**⇒ **`asp3_core` の変更は不要**（ユーザー方針
「価値がほぼ無いなら不要」に合致）。**本ラウンドでは march を変更していない**
（実験用の追記は測定後に revert 済み＝`git diff` 0 行を確認）。

> ★参考（事実のみ）：**もし将来 march を変えたくなっても `asp3_core` は触らずに済む**
> ＝C6 `target.cmake`（本リポジトリ）末尾で後勝ちの `-march` を append すればよい。
> ただし `-march` が2つ並ぶ形になるので**推奨はしない**（記録として残すだけ）。

### 2.4 ★toolchain guard の転写と FATAL の実発火

`asp3/target/esp32c6_espidf/target.cmake` に **C5 と同じ2行**（+経緯コメント）：

```cmake
if(NOT DEFINED ASP3_ESP_EXPECTED_TOOLCHAIN)
    set(ASP3_ESP_EXPECTED_TOOLCHAIN esp-14.2.0_20260121)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/esp_toolchain_check.cmake)
```

★★**転写の価値を «偶然» 実証した**：本ラウンドで私が toolchain file を渡し忘れたとき、

| チップ | guard | 実際に起きたこと |
|---|---|---|
| **C5** | **あり** | **configure で即 FATAL**：`-dumpmachine : x86_64-linux-gnu`＝**診断済みの失敗** |
| **C3/C6** | **なし** | **configure は成功**し、ビルド途中で `cc: error: unrecognized argument in option '-mabi=ilp32'`＝**原因の分からない失敗** |

**FATAL の実発火（6/6）**：

| # | 条件 | 期待 | 実測 |
|---|---|---|---|
| 1 | toolchain file 渡し忘れ | FATAL | **発火**（`x86_64-linux-gnu`） |
| 2 | 正しい版 | PASS | `ESP toolchain OK: esp-14.2.0_20260121` |
| 3 | 版違い（esp-15.2.0） | FATAL | **発火**（`found: esp-15.2.0_20251204 / expected: esp-14.2.0_20260121`） |
| 4 | 退避路 `-DASP3_ESP_EXPECTED_TOOLCHAIN=<tag>` | 通る | **通る**（`ESP toolchain OK: esp-15.2.0_20251204`） |
| 5 | 退避路 `-DASP3_ESP_TOOLCHAIN_CHECK=OFF` | 通る | **通る** |
| 6 | 存在しない版 | FATAL | **発火**（`ESP RISC-V toolchain not found`） |

**案内先の実在確認**＝`asp3/cmake/toolchain-esp32-riscv32.cmake`＝**在**／
`esp-15.2.0_20251204`・`esp-14.2.0_20260121` の導入先＝**在**。

★**私の測定バグ（自己申告）**：test3/4 は最初 **`ASP3_ESP_TOOLCHAIN_VERSION`** という
**存在しない変数名**を使ったため **rc が期待と逆**に出た（版が変わらず既定のまま通った）。
**正しい変数は `ESP_TOOLCHAIN_VERSION`**（`toolchain-esp32-riscv32.cmake:58`）。
⇒ **«変数名を推測して測る» のは沈黙する計器そのもの**。実測して是正した。

**非回帰**：guard 追加後も **C6 BLE（text=401392）・C6 wifi_scan（text=521344）** とも rc=0。
**C3 は guard を include しない**（実測 0 参照）＝**挙動不変**。

---

## 3. Phase 3 — 実機 A/B の設計と★予測の事前登録

### 3.1 ★アーム定義（★親の定義を実測で訂正）

**C6 の «現行» は C5 とも違った**（build/ の C6 構成 70 の実測）：

| 実際に使われたコンパイラ | C6 構成数 |
|---|---|
| **`/usr/bin` 汎用 GCC 13.2.0** | **34** |
| `esp-15.2.0_20251204` | 18 |
| `esp-14.2.0_20241119` | 15 |
| `esp-14.2.0_20260121`（真の v5.5.4 の指定版） | 3（＝**本ラウンドで私が作ったもの**） |

**既知良好の実体（実測）**：

| 既知良好 | build dir | 実際のコンパイラ |
|---|---|---|
| **W1**（ping 36/36・evidence-c6-06） | `c6_r89/r90_wifi_dhcp` | ★**`/usr/bin` 汎用 GCC 13.2.0** |
| **W2**（BLE D-2c/D-2d） | `c6ble_d2c_verify` | **esp-14.2.0_20241119**（GCC 14.2.0） |

⇒ **アーム**：

| アーム | コンパイラ | march | multilib（実測） | 位置づけ |
|---|---|---|---|---|
| **A**（W1） | **汎用 GCC 13.2.0** | rv32imc | ★**`rv32im/ilp32`＝C拡張なし** | **W1 の既知良好そのもの** |
| **A2**（W2/BLE） | esp-14.2.0_20241119（GCC 14.2.0） | rv32imc | `rv32imc.../ilp32` | **BLE の既知良好そのもの** |
| **B** | **esp-14.2.0_20260121（GCC 14.2.0）** | rv32imc（**変更せず**＝§2.3） | `rv32imc.../ilp32` | **IDF v5.5.4 標準のコンパイラ** |

★**B は «IDF標準 ISA» ではなく «IDF標準 コンパイラ»**＝march は変えていない
（§2.3 で no-op と実証したため、かつ chip.cmake は submodule）。

### 3.2 ★★ビルド生成物の実測（実機の前に判明した決定的事実）

| 比較 | 結果 | 実験として |
|---|---|---|
| **W1: A vs B** | **`.text` 585856 vs 581584＝−4272 B**／**177/230 オブジェクト相違** | ★**本物** |
| **BLE: A2 vs B** | **135/135 オブジェクト同一**／**flash イメージの差は 3 バイトのみ＝すべて `__TIME__` バナー**（`15:38:15` vs `15:36:39`） | ★★**退化＝同じプログラム** |

⇒ **結論(1)：W1 の A/B は本物**（汎用 GCC13.2＋**rv32im multilib** vs Espressif GCC14.2＋rv32imc multilib）。
⇒ ★★**結論(2)：BLE の A/B は退化**＝**B は既知良好 A2 とバイト同一**
（**両者とも上流 GCC 14.2.0**・ISA も同じ rv32imc）。
⇒ **∴ «B が C6×Android を壊すか» は静的に答えが出ている＝壊しようがない**
（**同じ機械語**）。実機セルは «壊れていないこと» ではなく
**«ビルド／ボード／ハーネスが生きていること» の確認**として意味を持つ。

★**`__TIME__` バナーが «今どちらが載っているか» の識別子になる**（C5 §7.2 と同じ皮肉な効能）。

### 3.3 ★★予測の事前登録（実機の前に確定・後付け禁止）

**P1. B × C6 W1（DHCP+ping）が通る確率＝88%**
- 根拠：**A/B は本物の別プログラム**（−4272 B）。ただし B は **Espressif が IDF v5.5.4 に
  指定する版**であり、A は **multilib が `rv32im`（C拡張なし）へ落ちる汎用 GCC**＝
  B の方が «正しい» 側。既知ベースライン＝**ping 36/36・失敗0**。
- ★**外れたら何を意味するか**：**IDF 標準コンパイラが C6 の WiFi を壊す**＝重大。
  その場合 **ISA は無関係**（§2.3 で no-op と実証済）⇒ **コンパイラ（13.2→14.2）に帰属**。
  ★ただし**まず環境（AP/電波）の非決定性を疑う**（C5 §7.2 で A run1 が
  «DHCP timeout・ping 4» と揺れた前例＝**単一 run で結論しない・各アーム最低2回**）。

**P2. B × W2（BlueZ hci0 接続+GATT）が通る確率＝93%**
- 根拠：**B は BLE 既知良好 A2 と «バイト同一（`__TIME__` 除く）»**。残る不確実性は
  環境（central/電波）と真cold 手順のみ。
- ★**外れたら**：**同じ機械語が違う挙動を出した**ことになる⇒**H2 を捨てる前に
  §3.2 の «バイト同一» 測定と書込み手順を再検定する**（C5 §3.4 の作法を踏襲）。

**P3. B × iPhone が OK である確率＝90%**
- 根拠：既知＝C6×iPhone OK。**B は A2 とバイト同一**。
- 外れたら：P2 と同じく**測定側をまず疑う**。

★★**P4. B × Android が OK を «維持» する確率＝93%**（★本ラウンド最大の watch point）
- 根拠：★**C6 は本プロジェクトで唯一 Android が通るチップ**だが、
  **B は A2 とバイト同一**⇒ **toolchain が Android 経路を壊す経路が機械語として存在しない**。
  残り 7% は **環境・bond 汚染・測定手順**（memory：「«Androidで繋がらない» の大半は
  親の測定手順が作った交絡」＝**RAM-backed bond ＋ 電源断 ＋ forget 忘れ**）。
- ★**外れたら（＝Android が NG になったら）**：**«IDF標準 toolchain が Android を壊した»
  と書いてはならない**（同じ機械語だから）。**まず (i) 両スマホの forget 漏れ、
  (ii) 真cold による RAM bond 消失、(iii) §3.2 の測定誤り**を疑う。
  **これが «toolchain のせい» になる唯一の道は §3.2 が誤っていた場合のみ**。
- ★**維持されれば収穫**：**Android 調査から toolchain を «除外» できる**（帰属の前進）。

**★反証条件（先に固定）**

- **H1＝「IDF標準 toolchain は C6 で安全（既知良好を壊さない）」**
  - **反証条件**：**B が W1 か BlueZ で «A が同条件で通るのに» 落ちたら H1 を捨てる**。
    ★**A アームも «今» 測る**（過去の記録を A の代用にしない）＝
    **A が同セッションで通ることが、B の失敗を «B に» 帰属させる前提**（C3 evidence-c3-03 の型）。
  - **反証条件自体の検算**：★**「A も B も落ちた」＝環境**（帰属不能）⇒ その場合は
    **AP/central 側を直してから測り直す**。**「A 成功・B 失敗」でなければ帰属してはならない。**
- **H2＝「BLE の B は A2 とバイト同一＝同じプログラム」**
  - **反証条件**：**実機で B と A2 が違う挙動を出したら H2 を疑う**。
  - ★**独立測定での検算**：**バナーの `__TIME__`（`15:36:39`=B / `15:38:15`=A2）を
    実機コンソールで読み、載っているビルドを同定する**＝
    「そもそも意図したビルドが載っているか」を**挙動と独立に**確認できる。
- **H3（土台）＝「B が W1 か BlueZ で落ちたら、スマホセルへ進まず即報告」**
  （壊れた土台でスマホ結果を採らない。C5 §3.4／PVCY draft の前例に従う）。

★**正直な申告**：**§2.3（march no-op）と §3.2（A/B のバイト差）は «予測» ではなく
先に実測した**。後付けで「予測が当たった」とは書かない。

---

## 4. Phase 3 実測結果（実機・すべて真cold）

### 4.1 ★真cold の証明と、計器の検定

**真cold の証明＝`uhubctl -l 1-6 -p 2 -a off` ＋ by-id **2ノード**の消失を読み戻し**
（**全 run で `usbjtag=GONE cp2102n=GONE`**）。`-p 2` はネストハブごと
`1-6.2.3`（C6 USB-JTAG）と `1-6.2.4`（C6 の CP2102N＝UART0）を**同時に**落とす＝
**2つの独立した witness**（実測トポロジ：§4.5）。

★★**`rst:0x1 (POWERON)` は真cold の証明にならない — 本セッションで実演された**：
**capture を `dtr=False/rts=False` で open しただけで**
`ESP-ROM:esp32c6-20220919` ＋ **`rst:0x1 (POWERON)`** が録れた（電源は切っていない）。
⇒ **POR と EN リセットは signature が同一**＝**by-id 消失の読み戻しだけが証明**。

★**DTR/RTS 極性は «C5 の結論を輸入せず» C6 で実測した**（この仮定が過去に
«存在しないバグ» を1ラウンド捏造している）。**動作中のアプリに対して**：

| open 時の線 | ESP-ROM バナー | 実測 |
|---|---|---|
| **`dtr=True, rts=True`** | **0** | **リセットしない**（`IP acquired`=1・ping=7 を捕捉） |
| `dtr=False, rts=False` | **1** | **リセットする**（`rst:0x1`・`Kernel Release`・ping=0） |

⇒ **C6 も C5 と同じ**＝`dtr=True/rts=True` を採用。**全 run で ESP-ROM バナー＝0**
＝**open がリセットしていない＝観測しているのは boot#1＝POR ブート**。

★**`uhubctl` は毎回 Segmentation fault を出すが電源は実際に切れている**
（**exit code を信じず by-id を読み戻したから分かった**）。**戻り値でなく状態を読め。**

★**正直な限界**：**`Kernel Release` バナー（`__TIME__`）は全 cold run で取り逃した**
（CP2102N の enumerate が ~1s かかり、その間にバナーが流れる）。
⇒ **載っているビルドの同定は «flash 時の `Hash of data verified`» に依存**しており、
**§3.3 H2 で用意した «バナーによる独立同定» は実行できていない**（**取れなかった対照**）。

### 4.2 W1（`apps/wifi_dhcp`：DHCP で IP 取得 + ping）

| アーム | コンパイラ / ISA | run | 真cold証明 | POR観測 | IP | ping OK / timeout |
|---|---|---|---|---|---|---|
| **A** | 汎用 GCC 13.2.0 / rv32imc（multilib は **rv32im**） | 1 | GONE/GONE | ESP-ROM=0 | `192.168.1.69` | **51 / 0** |
| **A** | 〃 | 2 | GONE/GONE | ESP-ROM=0 | `192.168.1.69` | **50 / 0** |
| **B** | **esp-14.2.0_20260121 / rv32imc** | 1 | GONE/GONE | ESP-ROM=0 | `192.168.1.69` | **50 / 0** |
| **B** | 〃 | 2 | GONE/GONE | ESP-ROM=0 | `192.168.1.69` | **49 / 0** |

⇒ **A 2/2 PASS・B 2/2 PASS・timeout は全 run で 0**。
★**A を «今» 測ったことが本質**＝**A が同一セッション・同一 AP で通る**からこそ、
仮に B が落ちていれば **B に帰属**できた（＝帰属の前提を用意した上での PASS）。
SSID は syslog に出るため本文では **`<SSID-2G>`** にマスクしている。

### 4.3 W2（BlueZ `hci0`：接続 + GATT）

D-Bus 直叩き（`bluetoothctl` パイプ駆動は**使っていない**）。**agent を自分で登録**
（未登録なら pairing 認可不可＝`NoReply` を «デバイスが SM を通せない» と誤断する既知 artifact）。
**毎セル真cold ＋ central 側 `RemoveDevice`**。

| アーム | コンパイラ | run | 発見(RSSI) | Connected / ServicesResolved | `0xABF1` READ | `0xABF3` WRITE | 判定 |
|---|---|---|---|---|---|---|---|
| **A2** | esp-14.2.0_20241119（BLE既知良好） | 1 | -69 | True / **True** | **`BT4-OK`** | OK | **PASS** |
| **A2** | 〃 | 2 | -68 | True / **True** | **`BT4-OK`** | OK | **PASS** |
| **B** | **esp-14.2.0_20260121** | 1 | -68 | True / **True** | **`BT4-OK`** | OK | **PASS** |
| **B** | 〃 | 2 | -68 | True / **True** | **`BT4-OK`** | OK | **PASS** |

⇒ **A2 2/2・B 2/2 PASS**。**`0xABF4` が characteristic 一覧に実在**
（`['2a05','2b29','2b3a','abf1','abf2','abf3','abf4']`）＝**`ESP32C6_BT_SM=ON` が実効**。

★**B run2 は `le-connection-abort-by-local` で2回 connect に失敗→3回目で成功**。
**これをアーム差と読んではならない**＝§3.2 で **A2 と B はバイト同一**と実測済み＝
**同じプログラム**。BlueZ 側の既知の一過性であり、**リトライで PASS**。
**単一 run で結論しない**作法どおり run を足して判定した。

★**`0xABF4`（暗号必須 READ）は測っていない**＝agent は登録したが、
**D-2d の判定はスマホセル（ユーザー操作）に委ねる**（§4.4）。

### 4.4 スマホセル（★準備完了・ユーザー確認待ち＝ここで停止）

**セル ＝ B（IDF標準 toolchain）× {iPhone, Android}** を準備し、**広告中のまま残置**：

- 載っているビルド＝**`build/gd_c6_ble`**（**esp-14.2.0_20260121 / rv32imc /
  `ASP3_BT_IDF_V554=ON` 既定 / `ESP32C6_BT_SM=ON` 既定**）
  ＝**本ラウンドで BlueZ 2/2 PASS を出した構成そのもの**（`Hash of data verified`）。
- **真cold**（`usbjtag=GONE cp2102n=GONE` を読み戻し）。
- **BlueZ central 側の stale bond を削除**（実測 0 件＝既にクリーン）。
- **広告在席は «スキャンで» 確認**＝**`name=ASP3-C6-BLE RSSI=-66 Paired=0`**。
  **esptool は使っていない**（`--after no-reset` は download mode へ落として広告を止めるため）。

★**ユーザーが試す «前» にマーカーを読まない**。読むのはユーザーの「終わった」の後。

★★**ユーザーへの依頼（memory の «9時間溶かした事故» を避けるため必須）**：
1. **iPhone と Android の «両方» で先に forget**（鍵を持ったスマホの自動再接続が
   次セルを汚す。**片方だけでは不十分**）。
2. **1ビルド1端末**（`ENC` マーカは最後の1件しか保持しない）。
3. 手順＝`0xABF1`=`BT4-OK` → `0xABF2` subscribe → `0xABF3` WRITE →
   **`0xABF4` は未ペアで弾かれるのが正しい** → ペアリング → **bond 後に `BT4-OK` なら D-2d 達成**。

**読み出し表（★タグで引く。アドレスで引かない＝全 STORE 共用が常態）**

| 事象 | reg | タグ／形式 |
|---|---|---|
| CONNECT | `0x600B1020` | `0x604E<status><count>` |
| DISCONNECT | `0x600B1024` | `0xD15C<reason><count>` |
| **ENC_CHANGE / reset_cb（共用）** | **`0x600B1018`** | `0x5DE0<status>` ／ reset_cb は `0x5E00` |

★★**既知の潜在バグを実測で確認した（＝マーカーを信じる前の確認）**：
**`ble_host_smoke_c6.c:112` の `LP_AON_STORE4 = 0x600B1010`（«割込みレート CPU線1累積ミラー»）は、
C6 では `RTC_XTAL_FREQ_REG` そのもの**＝`esp-idf/components/esp_rom/esp32c6/include/esp32c6/rom/rtc.h:62`
に **`#define RTC_XTAL_FREQ_REG LP_AON_STORE4_REG`**（**実測・原文**）⇒ **衝突は実在**。
★**ただし «実害あり» とは書かない**：本セッションで観測した
`W (23) rtc_clk: invalid RTC_XTAL_FREQ_REG value, assume 40MHz` は
**`wifi_dhcp`（STORE4 を一度も書かないアプリ・実測 0 参照）のブート**で出ている
⇒ **この警告は pre-existing**（ASP3 Direct Boot が `RTC_XTAL_FREQ_REG` を初期化しないため）
であり、**マーカー衝突の証拠ではない**。**2つを混同しない。**
∴ 現状**無害**（ASP3 は当該レジスタに依存せず、いずれにせよ 40MHz を仮定する）だが、
**STORE4 を «割込みレートの信頼できるミラー» として読むのは危険**。**修正はしていない**（スコープ外）。

### 4.5 実測トポロジ（★by-id のみを使う。`ttyACM*` 番号は電源断で入れ替わる）

| USB パス | 個体 | 備考 |
|---|---|---|
| `1-6.1` | C3 `60:55:F9:57:BA:BC` | 本ラウンド対象外 |
| **`1-6.2.3`** | **C6 `14:C1:9F:E0:5A:9C`（DUT＝board C）** | `-p 2` で落ちる |
| **`1-6.2.4`** | **CP2102N `125a266b…`＝C6 の UART0** | `-p 2` で落ちる＝**第2の witness** |
| `1-6.3` / `1-6.4` | C5 の UART / C5 `D0:CF:13:F0:A7:44` | 対象外 |
| **`1-5.2` / `1-5.3` / `1-5.4`** | **別プロジェクト（ESP32-S3 `F4:12:FA:5B:4A:58` 等）** | ★**hub 1-5 は一切触っていない** |

★**実際に危なかった**：`F4:12:FA:5B:4A:58`（別プロジェクトの S3）は本セッション中
**`/dev/ttyACM0`** に居た。**全書込みを by-id ＋ `--chip esp32c6` で行った**
（監査：`Hash of data verified` ×5・`wrong chip` エラー **0**）。

### 4.6 事前登録した予測の的中/外れ

| 予測 | 事前登録値 | 結果 |
|---|---|---|
| **P1** | B × W1 が通る **88%** | **★的中**（B 2/2 PASS・timeout 0） |
| **P2** | B × W2(BlueZ) が通る **93%** | **★的中**（B 2/2 PASS） |
| **P3** | B × iPhone が OK **90%** | **未測定**（ユーザー確認待ち） |
| **P4** | **B × Android が OK を維持 93%** | **未測定**（ユーザー確認待ち） |
| **H1**（IDF標準 toolchain は C6 で安全） | 反証条件＝**A が通るのに B が落ちたら棄却** | **反証されず**（**A 2/2・B 2/2 で両方 PASS**＝帰属の前提を満たした上での非棄却） |
| **H2**（BLE の B は A2 とバイト同一） | 反証条件＝実機で B と A2 が違う挙動 | **反証されず**（A2 2/2・B 2/2 とも同一結果）。★**ただし «バナーによる独立同定» は取り逃した**＝**検算は未完** |
| **H3**（土台が壊れたらスマホへ進まない） | — | **土台は健全**（W1・BlueZ とも B が PASS）⇒ **スマホセルへ進んでよい** |

★**正直な申告**：**§2.3（march no-op）と §3.2（A/B のバイト差）は予測ではなく先に実測した**旨を
§3.3 に明記済み。後付けで «予測が当たった» とは書いていない。

### 4.7 ★取らなかった対照／未確定（no silent caps）

- **`0xABF4`（暗号必須 READ）の BlueZ 判定**＝**未実施**（D-2d はスマホセルに委ねた）。
- **バナー（`__TIME__`）による «載っているビルド» の独立同定**＝**取り逃した**（§4.1）。
  ⇒ ビルド同一性の根拠は `Hash of data verified` のみ。
- **B2（コンパイラ B × ISA rv32imac）の実機 run**＝**未実施**。
  §2.3 で **135/135 同一＝機械語 no-op** と実証したため**実機で差が出る余地が無い**が、
  **«反証する機会を作っていない»** ことは記録する（C5 §7.4 と同じ限界）。
- **C6 の march を実際に IDF 標準へ変更した構成での実機**＝**未実施**（変更不要と結論したため）。
- **C3/C5 の実機**＝**本ラウンドでは触っていない**（Phase 1 の非回帰はビルド実測のみ。
  ★**«ビルドが通る» と «既知良好» は別**）。

---

## 5. スマホセル ＝ **B（IDF標準 toolchain）× Android** — 実測（★OK）

**ユーザー観測（逐語）**：「**C6 x Android : all ok**」

### 5.1 マーカー実測（★タグで引く。アドレスで引かない）

読み出し＝`esptool --before usb-reset --after no-reset`（**ROM download モードに留まりアプリが走らない
＝マーカは凍結**）。**電源は落としていない**（真POR は LP_AON を消すため、読む前に電源を切ると証拠が消える）。

| reg | 生値 | タグ | タグで引いた意味 |
|---|---|---|---|
| `0x600B1000` | `0x5ade51c0` | `0x5ADE51C0` | **SYNC 到達**＝host は sync した |
| `0x600B1004` | `0x5a6e0005` | （STORE1） | ★**app が «ノイズ» と明記＝使用禁止レジスタ。読まない** |
| `0x600B1008` | `0x0ade5000` | `0x0ADE5000` | **adv 開始試行** |
| `0x600B100C` | `0xad000000` | **`0xAD00`** | **adv rc=0**。★**`0x7717` ではない**が**判定不能**（§5.2） |
| `0x600B1010` | `0x00000000` | （STORE4） | 割込みレート線1＝0。★**＝`RTC_XTAL_FREQ_REG` 衝突レジスタ**（§4.4） |
| `0x600B1014` | `0x00005c25` | （STORE5） | 割込みレート線2＝**23589** |
| **`0x600B1018`** | **`0x5de00000`** | **`0x5DE0`** | ★★**ENC_CHANGE・status=`0x00`＝暗号確立**（reset_cb タグ `0x5E00` ではない＝ble_hs リセット無し） |
| **`0x600B101C`** | **`0x5dc00011`** | **`0x5DC0`** | ★★**PAIRING_COMPLETE・status=`0x00`・our_sec=**1**・peer_sec=**1** |
| `0x600B1020` | `0x604e0002` | `0x604E` | **CONNECT・status=`0x00`・count=2** |
| `0x600B1024` | `0xd15c1302` | `0xD15C` | **DISCONNECT・reason byte=`0x13`・count=2** |

**デコードの裏取り（memory ではなく «ソース» で確認）**＝`ble_host_smoke_c6.c:615-620`：
```c
0x5DC00000UL | ((status & 0xff) << 8) | ((our_cnt & 0xf) << 4) | (peer_cnt & 0xf)
```
⇒ `0x5dc00011` → **status=0x00 / our=1 / peer=1**。`our_cnt`/`peer_cnt` は
**`ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC/PEER_SEC)` の実測値**＝
**DUT 側 bond store に鍵が入っている**。

⇒ **暗号確立（ENC status=0）と bond 成立（PAIRING status=0・our=1・peer=1）を、
ユーザーの «all ok» とは独立に、デバイス側の数字で確認した。**

★**`our_sec=1` は本プロジェクトで稀な観測**（memory：C3 hal×Android が初）。
**C5 の判定基準「status=0 かつ our_sec>=1」は C5 では一度も満たされた記録が無い**が、
**C6×Android は満たした**（`peer_sec=1` も同時）。

### 5.2 ★「`0x7717` が立っていない」を «WRITE 未到達» と読んではならない

`BLE_WRITE_MARK_ADDR` は **STORE3＝adv-return rc（`0xAD00`）との共用**。
app のコメントは「**接続中は** `start_advertising()` が呼ばれないため不変」と言い、**それは正しい**。
**しかし不十分**＝**切断のたびに再 adv が走り `0xAD000000` を書く**（`:481`）。
実測は **DISC count=2**＝**切断後の再 adv が最低2回走った**
⇒ **最後の再 adv が `0x7717` を確実に破壊する**。

∴ **`0x7717` は «切断で終わるセッション» では原理的に事後観測できない**
（読むのは必ず切断後だから）。**⇒「`0xABF3` が発火したか」は判定不能。**
**「立っていない＝未到達」と書くのは誤り**（C5 の `0x5DC0` 不在と同型の罠。
**不在は over-determined ＝ 証拠価値ゼロ**）。
ユーザーの「all ok」は WRITE 到達を示唆するが、**マーカーでは裏取りできない**と正直に記す。

★**`0xABF1` READ にはそもそもマーカが無い**（実測：app に READ 計装は存在しない）。
**ただし本ラウンドの BlueZ セルで `0xABF1`=`BT4-OK` を4/4 実測済み**（§4.3）＝別経路で確認済み。

### 5.3 ★★`0x5DC0` は C6 では «観測可能» — 構造から判定（app のコメントを信じない）

コーディネータの問い＝「**C5 で原理的に観測不能だった `0x5DC0` が C6 でも同じ罠か**」。
**答え＝C6 は «同じ構造ではない»＝観測可能。**（**構造・実測の両方で確定**）

| | ENC の書込先 | PAIR の書込先 | 帰結 |
|---|---|---|---|
| **C5** | `LP_AON_STORE_SM` = **`0x600B1018`**（`:662`） | `LP_AON_STORE_SM` = **`0x600B1018`**（`:685`） | ★**同一レジスタ＝last-wins**⇒`pairing_complete` が先に発火すると ENC が PAIR を潰す＝**観測不能**（別エージェントの実証） |
| **C6** | `LP_AON_STORE_ENC` = `LP_AON_STORE6` = `0x600B1018`（`:153,569`） | **`LP_AON_STORE_PAIR` = `BT_INTR_TRACE_REG` = STORE7 = `0x600B101C`**（`:154,615`） | ★**別レジスタ＝互いに独立**⇒**発火順に関係なく両方残る＝観測可能** |

C5 の定義は逐語で **`#define LP_AON_STORE_SM 0x600B1018UL /* STORE6：ENC/PAIRING 共用マーカ */`**
＝**共用であることを定義自身が明言**。C6 は **PAIR を STORE7 へ分離**している。
∴ **C5 の罠（共用 last-wins）は C6 には存在しない。**

★**実測がこれを裏付ける**：**`0x600B101C = 0x5dc00011` が実際に立った**
＝**«観測可能» は理屈だけでなく実物で確認済み**（かつ **ENC も同時に `0x5de00000` で生存**
＝**両方同時に読めた**＝共用でないことの決定的証拠）。

★**共用相手（bt_shim の intr トレース `0xA1xxxxxx`）に潰されなかったことも実測で確認**
（もし controller init 以外でも書いていれば `0x5DC0` は消えていた）。**ただし射程限定**＝
「**このセッションでは**潰されなかった」まで。**恒久的な安全は主張しない**。

★**C5 のコメント（`:133-134`「PAIRING は ENC の «後» に発火する」）は別エージェントの実証と逆**
＝**コーディネータの «app のコメントを信じるな» は正しかった**。
**本節の C6 判定はコメントではなく «`#define` の実アドレス» と «実測値» に基づく。**

### 5.4 ★★チップ横断の陽性対照（＝C5 側の結論を «外部から» 支える）

**同一セッション・同一 Android 端末・同一 forget 手順**で：

| セル | 結果 | 根拠 |
|---|---|---|
| **C5 × Android** | **NG** | 別エージェントの実測（2アーム・9レジスタ一致）＝`evidence-c5-08` |
| **C6 × Android** | ★**OK** | **本節**（ENC status=0・PAIRING status=0・our=1・peer=1） |

⇒ **証明されたこと**：
1. **Android 端末は健全に動作する**（今日・この端末で bond まで通る）。
2. **forget 手順は «効く»**（フレッシュな pairing が実際に起きた＝**`0x5DC0` が立った**
   ＝**古い bond の再利用ではない**）。

⇒ **対抗仮説「Android が今日どこでも壊れている／forget が効いていない」は死ぬ。**
⇒ ∴ **C5 の Android NG は «C5 固有» と、より強く言える。**

★★**射程を守る（過大に書かない）**：
本セルが示したのは **forget が «この端末で・この日に・C6 相手に» 効いたこと**であって、
**C5 のセルで実際に効いていたことの «直接» の証明ではない**（**別セル・別時刻**）。
⇒ **«強い支持» であって «証明» ではない。**
（相互参照＝`evidence-c5-08-toolchain-idf-alignment.md`。★**同ファイルは別エージェントの担当につき
本ラウンドでは編集していない＝参照のみ**。）

### 5.5 事前登録した予測の的中/外れ（最終）

| 予測 | 事前登録値 | 結果 |
|---|---|---|
| **P1** | B × W1 が通る **88%** | **★的中**（B 2/2 PASS・timeout 0） |
| **P2** | B × W2(BlueZ) が通る **93%** | **★的中**（B 2/2 PASS） |
| **P3** | B × iPhone が OK **90%** | ★**未測定**（ユーザーは Android のみ実施） |
| **P4** | **B × Android が OK を «維持» 93%** | **★★的中**（`all ok`＋**ENC status=0・PAIRING status=0/our=1/peer=1** で裏取り） |

★**P4 の «根拠» も検証された**：事前登録時の論拠は
**「B は BLE 既知良好 A2 と機械語がバイト同一（差は `__TIME__` の3バイトのみ）⇒
toolchain が Android 経路を壊す経路が機械語として存在しない」**であり、
**実機がそのとおりになった**。⇒ **«IDF 標準 toolchain は C6 の Android を壊さない» は、
実機1セルだけでなく «同じプログラムである» という静的事実に支えられている**（＝強い）。
⇒ **Android 調査から toolchain を «除外» できる**（帰属の前進）。

---

## 6. C6 最終状態（本ラウンド）

| 項目 | 状態 |
|---|---|
| **W1（DHCP+ping）真cold** | **A 2/2 PASS（51/0・50/0）／B 2/2 PASS（50/0・49/0）**・timeout 全 run 0 |
| **W2 BlueZ 真cold** | **A2 2/2 PASS／B 2/2 PASS**（`0xABF1`=`BT4-OK`・`0xABF3` WRITE OK） |
| **W2 iPhone** | ★**未実施** |
| **W2 Android（アーム B）** | ★★**OK**（ENC status=0・PAIRING status=0・our=1・peer=1） |
| **BLE の A2 vs B** | ★**縮退＝135/135 オブジェクト同一・差は3バイト＝`__TIME__` のみ⇒B は既知良好そのもの** |
| **march** | ★**変更不要＝`asp3_core` 変更不要**（135/135 同一・atomic 0・`Tag_RISCV_arch` は実際に変わった＝実験は効いていた） |
| **toolchain guard** | **`target.cmake` 2行で転写・FATAL 6/6 実発火・退避先の実在確認済み** |
| **`esp_timer.h` shadow** | **根治（3チップ共有）**。C6 は元々無傷（`bt_esp_timer_ext.h` で回避済み）＝**修正後の機械語＝C6 が出荷し続けてきた形** |

## 7. ★未測定・積み残し（隠さない）

- **iPhone セル＝未実施**（ユーザーは Android のみ実施）⇒ **P3 は未検証のまま**。
- ★**`__TIME__` バナーを全 cold run で取り逃した**⇒ **H2 の «載っているビルド» 独立同定は
  `Hash of data verified` 単独に依存**（§4.1）。
- **`0xABF3` の発火可否は判定不能**（§5.2＝共用レジスタが再 adv で潰れる構造）。
- **`0xABF1` READ にマーカ無し**（BlueZ 側で 4/4 実測済みだが、スマホセルでは裏取りしていない）。
- **B2（コンパイラ B × ISA rv32imac）の実機 run＝未実施**（機械語 no-op と実証済＝差が出る余地無しだが、
  **反証の機会を作っていない**）。
- **C3/C5 の実機非回帰＝未実施**（Phase 1 の非回帰は**ビルド実測のみ**。★**«builds» ≠ «known-good»**）。

## 8. 申し送り（ユーザー判断事項）

1. **`esp_timer.h` に残る10穴**。うち **2本は `int64_t` 戻り**（`esp_timer_get_next_alarm{,_for_wake_up}`）
   ＝**暗黙 int だと上位32bit を黙って捨てる**。**現状の呼び出しは 0 件＝潜在**（検出器較正済み）。
2. **`endian.h` の shadow**（本物＝`esp-idf/components/bt/porting/include/os/endian.h`・識別子 49）。
3. ★**`heap_caps_malloc` の implicit declaration は hal 側＝シムが要る**
   （C3 の残り1件。**hal は submodule＝編集禁止**）。**C3 は今もこの1件のためだけに
   `-Wno-error=implicit-function-declaration` を必要とする**。
4. **`esp_wifi_v8.cmake:92-97` の4フラグ抑制はもう何も隠していない**（較正済みの実測 0/0/0/0）
   ⇒ 撤去可能と思われる（本ラウンドでは触っていない）。
5. ★**`ble_host_smoke_c6.c:112` の STORE4 衝突は実在**
   （`rom/rtc.h:62`＝`RTC_XTAL_FREQ_REG = LP_AON_STORE4_REG`）。
   **ただし観測された `invalid RTC_XTAL_FREQ_REG` 警告の原因ではない**（あれは STORE4 を書かない
   `wifi_dhcp` でも出る＝**pre-existing**）。**現状無害だが STORE4 を «信頼できる割込みミラー» として読むな。**
6. ★**`0x7717`（WRITE）マーカは «切断で終わるセッション» では事後観測できない**（§5.2）。
   **恒久的に判定したいなら共用先を変えるか、接続中に読む設計が要る。**
7. **C6 の `bt/bt_esp_timer_ext.h` は本修正で冗長化**（同一宣言＝合法・実測で衝突なし）。
   **撤去はしていない**（スコープ外）。

---

## 9. スマホ セル2 ＝ **B × iPhone**（★予測の事前登録＝測定前に commit）

### 9.1 アームの同一性（★再ビルドしていない）

**アーム B を再ビルドしていない**（ツリーが動いていれば二変数実験になる）。**実測で確認**：

| 検査 | 実測 |
|---|---|
| bin の mtime | **`2026-07-17 15:36:41`**（＝Android セルより前。以後変更なし） |
| bin の md5 | `2f74e55a7f4c3eb8d729a070797412cf` |
| bin 内バナー | **`Jul 17 2026, 15:36:39`** ＝ arm B の `__TIME__` |
| ★**`esptool verify-flash 0x0`** | **`Verification successful (digest matched)`** |

★★**これは §7 で «未測定» と申告した H2 の穴を «事後に» 埋める**：
`verify-flash` は **4MB 全域を bin とダイジェスト照合**する。**Android セル以降フラッシュへの
書込みは一度も行っていない**（read-mem とリセットのみ）⇒ **Android セルが載せていたのも
この同じ arm B**。**write 時の `Hash of data verified` 単独より強い**。
★**ただし正確に**：これは **«保存されている像» の静的照合**であって、
**«走っている像» をバナーで観測したわけではない**（§4.1 の取り逃しは取り逃したまま）。

### 9.2 ★予測の事前登録（★測定前・後付け禁止）

**P3（改訂）：C6 × iPhone が OK である確率＝94%**

- ★**正直な申告**：**当初の登録値は §3.3 の «P3 = 90%»**（C6 実機に触る前）。
  本セルの前に **90% → 94%** へ改訂する。**改訂の根拠は測定«前»に出揃った情報のみ**：
  1. **B は BLE 既知良好 A2 とバイト同一**（135/135・差3バイト＝`__TIME__`）
     ⇒ **toolchain が iPhone 経路を壊す機械語経路は存在しない**。
  2. **同じボード・同じ bin で BlueZ が 4/4 PASS**（§4.3）。
  3. ★**同じ bin で C6 × Android が «今日» OK**（§5）＝**ENC status=0・PAIRING our=1/peer=1**
     ＝**ボード・ビルド・BLE スタック・SM 経路が健全であることが実証済み**。
  4. **既知マトリクス＝C6 × iPhone は OK**。
- ⇒ **本セルは «歴史的 OK が今も再現するか» のセル**（C5 で歴史的ベースラインを閉じたのと同型）。
  **新規性の証明ではなく、再現性の確認。**

**★外れたら（＝iPhone NG なら）何を意味するか**

- ★**«IDF 標準 toolchain が iPhone を壊した» とは書けない**＝**B は A2 とバイト同一**だから。
  **機械語が同一である以上、toolchain 差では原理的に説明できない。**
- ⇒ 疑う順序（**測定側を先に疑う**）：
  1. ★★**Android の stale 鍵による汚染**（**最有力**）。**Android は数分前に C6 と bond して鍵を持つ**。
     **デバイス側 bond store は RAM-backed ⇒ 真cold で鍵を失う**
     ⇒ **«スマホは鍵を持つ／デバイスは失った» の不一致**が生まれ、**Android の自動再接続が
     このセルを汚す**（memory：**この型で9時間溶けた**）。**⇒ 両端末の forget が必須。**
  2. **環境（電波・BlueZ central が同じ DUT を掴む）**。
  3. **§3.2 の «バイト同一» 測定が誤り**（⇒ その場合は §3.2 を再検定する）。
  4. **歴史的な «C6 × iPhone OK» が別構成での観測だった**（供給・SM 設定が今と違う可能性）。
- ★**1セルで機序の物語を作らない**（単一 run から結論しない＝本リポジトリの事故型）。

**★判定に使うもの／使わないもの（自分の実測に従う）**

| | 使う | 理由 |
|---|---|---|
| **`ENC`** `0x600B1018` タグ `0x5DE0` + status | ★**使う** | **status=0 ＝暗号確立** |
| **`PAIR`** `0x600B101C` タグ `0x5DC0` + `our_sec`/`peer_sec` | ★**使う** | **C6 は ENC と別レジスタ＝観測可能**（§5.3 で構造+実測により確定） |
| `CONN` `0x600B1020` / `DISC` `0x600B1024` | 使う | count と status/reason |
| **`0xABF3`（`0x7717`）の «不在»** | ★**使わない＝判定不能** | **STORE3 は adv 戻り rc と共用・切断後の再広告が必ず壊す**（§5.2） |
| **`0xABF1`** | ★**マーカーが存在しない** | BlueZ 側で `BT4-OK` 4/4 実測済み（§4.3） |

★**`0x5DC0` が立てば «フレッシュな新規ペアリング»＝C6 では有効な判別子**
（**C5 では無効＝混同しない**）。⇒ **iPhone セルでも «古い bond の再利用でない» を直接判定できる。**

★**«最初の disconnect でエラーが出る» のは既知の C5/C6 挙動＝デバイス側 reason `0x13`＝正常切断**
（remote user terminated）。**ユーザーは «以前は再コネクトできなかったので進んでいる・このままで良い» と
判断済み**⇒ **これを «回帰» や «アーム B の副作用» と書かない。**

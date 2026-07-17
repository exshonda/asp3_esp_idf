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

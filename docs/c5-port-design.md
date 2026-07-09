# ESP32-C5移植 詳細設計書

## 0. 位置づけ・参照文書

本書はESP32-C5への TOPPERS/ASP3 移植の**詳細設計書**（コード生成は次フェーズ）。
ESP32-C6移植（`asp3/target/esp32c6_espidf/`・`asp3_core/arch/riscv_gcc/esp32c6/`）を
雛形とし，C6と異なる箇所のみを明示する差分ベースの設計とする。

必読の前提文書：

- `/home/user/asp3_esp_idf/CLAUDE.md`（禁則：submodule直接編集禁止・動的メモリ禁止）
- `/home/user/asp3_esp_idf/README.md`（ビルド手順・フェーズ構成・C6の到達点）
- `/home/user/asp3_esp_idf/docs/wifi-shim-c6.md`冒頭150行（C6でのチップ固有差替え一覧）
- `/home/user/asp3_esp_idf/docs/hal-integration.md`（B-1コンソール・タイマ統合，C6横展開の経緯）
- `asp3/asp3_core/docs/porting/PORTING_GUIDE.md` §「外部（SDK）ターゲットの置き方」
  （読み取り専用．本書の§2.2の結論の直接的根拠）

**C5移植のねらい**：C6で「deaf-RX」（Wi-Fi RX不成立．`docs/wifi-shim-c6.md`）が
未解決のまま凍結されている。C5で同一症状が再現すれば新世代モデム（INTMTX+新
割込みコントローラ系統）共通の問題である可能性が高まり，再現しなければC6固有
（個体差・シリコンrev・C6固有レジスタの誤り等）と切り分けられる。C5はC6と
「似ているが同一ではない」設計（後述の通り割込みコントローラが根本的に異なる）
であるため，このB-2a到達可否そのものが有力な切り分け材料になる。

---

## 1. 課題

1. **チップ依存部の置き場所問題**：C6の`arch/riscv_gcc/esp32c6/`は
   `asp3/asp3_core`（submodule）内にある。CLAUDE.mdの禁則によりsubmoduleを
   直接編集できないため，C5版をどこに新設し，どうビルドに組み込むかを
   確定させる必要がある（単なる思いつきではなく，CMakeのパス解決を
   実際に読み解いて機能することを検証する）。
2. **C5はC6の「割込みコントローラ以外はほぼ同じ延長」ではない**：
   ヘッダを実際に読むと，C5はC6と異なり**標準RISC-V CLIC**を採用しており
   （C6は独自方式の"PLIC"命名レジスタ＝実体はC3のINTMTXと同型），
   ASP3のCPU側割込み制御（`chip_support.S`のトラップベクタ・
   `intmtx_kernel_impl.h`のENABLE/PRI/THRESH操作）を書き直す必要がある。
   これは「レジスタアドレスの差し替え」では済まない，**アーキテクチャ
   レベルの設計変更**である。
3. **ターゲット依存部（`asp3/target/esp32c5_espidf/`）のWi-Fi shim**は
   C6同様チップ固有レジスタ（RNG・eFuse MAC・PMU/modem_clock）の
   差し替えが必要だが，C6の教訓（C3→C6でレジスタアドレスが毎回移動した
   実績）から，**値を推測せず必ずhalヘッダから引く**ことが必須。
4. **実機がないと確定できない事項**（CLICのmie/mstatus挙動，mtvecモード
   3の実際の起動シーケンス，SIL_DLY較正値，XTAL 40/48MHzのどちらが
   実装されているか等）を明示し，実機投入前に机上で潰せる問題と
   実機でしか潰せない問題を切り分ける。

---

## 2. 設計方針

### 2.1 二層構成の踏襲

C6と同じ2層構成を維持する：

| 層 | 配置 | 性質 |
|---|---|---|
| チップ依存部 | `asp3/arch/riscv_gcc/esp32c5/`（本リポジトリ側，**新設**） | 将来asp3_coreへupstream予定。22ファイル |
| ターゲット依存部 | `asp3/target/esp32c5_espidf/`（本リポジトリ側） | `esp32c6_espidf/`のコピー起点 |

### 2.2 arch配置問題の結論（検証済み）

**結論：`asp3/arch/riscv_gcc/esp32c5/`（本リポジトリ側，submodule外）への
配置で機能する。** 必要な変更は次の2点のみ：

1. **`chip.cmake`内の`CHIPDIR`定義を`${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6`から
   `${CMAKE_CURRENT_LIST_DIR}`へ変更する**（C6の`chip.cmake:11`は
   `set(CHIPDIR ${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6)`だが，これは
   「submodule内にある前提」のハードコードであり，`chip.cmake`自身が
   物理的にどこにあっても正しく動く`${CMAKE_CURRENT_LIST_DIR}`に
   置き換えれば，配置場所に依存しなくなる。`target.cmake`が既に
   `set(TARGETDIR ${CMAKE_CURRENT_LIST_DIR})`という全く同じイディオムを
   使っており，前例がある）。
2. **`target.cmake`（`asp3/target/esp32c5_espidf/target.cmake`）の
   `include(...)`行を新しいパスに書き換える**：
   `include(${CMAKE_CURRENT_LIST_DIR}/../../arch/riscv_gcc/esp32c5/chip.cmake)`
   （`ASP3_ROOT_DIR`基準ではなく`CMAKE_CURRENT_LIST_DIR`基準にする）。

この結論は，asp3_core自身のポーティングガイド
（`asp3_core/docs/porting/PORTING_GUIDE.md` §「外部（SDK）ターゲットの
置き方」）が想定している構成そのものであることを確認した。同ガイドは
外部target.cmakeの骨格として

```cmake
set(TARGETDIR ${CMAKE_CURRENT_LIST_DIR})
set(CHIPDIR   ${CMAKE_CURRENT_LIST_DIR}/../../arch/<arch_dir>/<chip>)  # 外部に持つ場合
```

を明示しており（"外部に持つ場合"という注記付き），チップ依存部を
submoduleの外に置くこと自体がガイドの想定範囲内であることの一次証拠。

以下，asp3_coreのCMake機構を全経路精読して確認した，配置変更で壊れうる
箇所の網羅的一覧（BREAKS/SAFE判定と対処）：

| # | 対象 | 現状（C6） | 配置変更の影響 | 対処 |
|---|---|---|---|---|
| 1 | `chip.cmake`の`CHIPDIR`定義 | `${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6`（chip.cmake:11） | **BREAKS**（そのままコピーするとsubmodule内の非存在パスを指す） | `set(CHIPDIR ${CMAKE_CURRENT_LIST_DIR})`に変更 |
| 2 | `CHIPDIR`由来の各種list追加（インクルードパス・コンパイル対象・コンソール選択） | chip.cmake:13-15,39-42,57-66,85 | SAFE（#1修正後は自動的に解決） | 対処不要 |
| 3 | `include(${ASP3_ROOT_DIR}/arch/riscv_gcc/common/arch.cmake)` | chip.cmake:77 | SAFE（共通archは意図的にsubmodule側を参照し続ける） | 変更不要 |
| 4 | `${ASP3_ROOT_DIR}/arch/riscv_gcc/polarfire_soc/libc_stub.c`の流用 | chip.cmake:86 | SAFE（他チップディレクトリの共有ファイル．意図的） | 変更不要 |
| 5 | `target.cmake`の`include(${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6/chip.cmake)` | target.cmake:120 | 配置変更の主対象そのもの | `${CMAKE_CURRENT_LIST_DIR}/../../arch/riscv_gcc/esp32c5/chip.cmake`へ書換え |
| 6 | `asp3_core.cmake`／トップレベル`CMakeLists.txt`のarchパスのハードコード | 全文精読（`ASP3_ARCH_C_FILES`等の汎用アキュムレータ変数のみを消費，`arch/riscv_gcc/<chip>`という文字列は一切登場しない） | SAFE（そもそもフック不要＝arch配置はtarget.cmake/chip.cmakeが完全に所有） | 変更不要 |
| 7 | `chip_kernel.py`のPythonコンフィギュレータ探索 | `target_kernel.py`の`IncludeTrb("chip_kernel.py")`→`cfg/cfg.py`の`search_file_path()`（`include_directories`のリスト線形探索，`ASP3_INCLUDE_DIRS`由来） | SAFE（#1修正が`ASP3_INCLUDE_DIRS`へ`CHIPDIR`を追加する経路そのものなので自動的に解決） | 変更不要（#1に従属） |
| 8 | Kconfigベースの探索 | リポジトリ全体を検索したが，Kconfigを参照するのは`hal/`（esp-hal-3rdparty自身のKconfig，ASP3コンフィギュレータとは無関係）のみ | SAFE（該当なし） | 対処不要 |
| 9 | `chip_asm.inc`／`chip_rename.h`／`chip_unrename.h`の`#include` | いずれも`#include "chip_xxx.h"`のベア形式（ディレクトリ相対指定なし）．コンパイラの`-I`（`ASP3_INCLUDE_DIRS`）経由で解決 | SAFE（#1修正に従属） | 変更不要 |
| 10 | `target_os_awareness.py`の`sys.path`ハードコード（`"../../arch/riscv_gcc/esp32c6"`） | 現状のC3/C6版は**実はこの文字列がすでに指す先が存在しない**（`asp3/target/esp32c6_espidf/`から`../../arch/riscv_gcc/esp32c6`は`asp3/arch/riscv_gcc/esp32c6`になり，実体は`asp3/asp3_core/arch/riscv_gcc/esp32c6`なので不一致＝既存の潜在バグ，GDB OS-Awareness専用でビルドには無関係） | 新設するC5版で同じ文字列パターン（`"../../arch/riscv_gcc/esp32c5"`）を書くと，**今回の新配置(`asp3/arch/riscv_gcc/esp32c5/`)とはたまたま一致し正しく動く** | C5版`target_os_awareness.py`はそのまま複製でよい（副次的にC3/C6の既存バグ修正は本移植のスコープ外として別途記録） |

**結論**：arch配置は技術的に安全に実現できる。変更点は#1・#5の2行のみ。

### 2.3 段階的検証によるdeaf-RX切り分け方針

CLAUDE.mdの検証の鉄則に従い，B-0（起動・コンソール）→B-1（タイマ・
test_porting）→B-2a（Wi-Fi scan）の順で完了判定を行う（詳細は§7）。
割込みコントローラがC6と異なる（CLIC）ため，**B-1のtest_porting
6/6自体がC6より一段高いハードル**になる（C6は「レジスタベースアドレス
の差し替え」で済んだが，C5は「トラップベクタ・優先度制御の再設計」を
経て初めてtest_portingに到達する）。この非対称性を計画に織り込む。

---

## 3. C5固有差分表

値はすべて`hal/components/soc/esp32c5/`（および比較対象の`esp32c6/`）の
実ヘッダから引用。推測箇所は【未確認】と明示。

| 項目 | C6の値 | C5の値 | 根拠（hal内パス） |
|---|---|---|---|
| **割込みコントローラ方式** | 独自"PLIC"命名（実体はC3 INTMTXと同型のCPU側制御．ENABLE/TYPE/CLEAR/EIP/PRI×32/THRESHのメモリマップトレジスタ配列） | **標準RISC-V CLIC**（`SOC_INT_CLIC_SUPPORTED=1`） | `soc/esp32c6/include/soc/interrupt_reg.h`（`DR_REG_INTERRUPT_BASE = DR_REG_INTMTX_BASE`），`soc/esp32c5/include/soc/interrupt_reg.h`（"ESP32C5 uses the CLIC controller as the interrupt controller (SOC_INT_CLIC_SUPPORTED = y)"），`soc/esp32c6/include/soc/soc_caps.h`（`SOC_INT_PLIC_SUPPORTED 1`），`soc/esp32c5/include/soc/soc_caps.h`（`SOC_INT_CLIC_SUPPORTED 1`） |
| CPU側制御レジスタのベース | `PLIC_MX_BASE = 0x20001000` | `DR_REG_CLIC_BASE = 0x20800000`（グローバル設定）／`DR_REG_CLIC_CTRL_BASE = 0x20801000`（per-line制御） | `soc/esp32c6/register/soc/reg_base.h`，`soc/esp32c5/include/soc/clic_reg.h` |
| ペリフェラルソース→CPU割込み線ルーティング（INTMTX） | `DR_REG_INTMTX_BASE = 0x60010000`（ソースnのMAPレジスタ = BASE+4n） | **同一アドレス・同一方式で存続**：`DR_REG_INTMTX_BASE = 0x60010000`（`DR_REG_INTERRUPT_CORE0_BASE`のエイリアス） | `soc/esp32c6/register/soc/reg_base.h`，`soc/esp32c5/register/soc/reg_base.h`（両方とも`0x60010000`） |
| per-line制御レジスタのビット構成 | ENABLE/TYPE/CLEAR/EIP/PRI/THRESHが別々のワード配列（`intmtx_kernel_impl.h`のPLICMX_*_OFF） | **CLIC_INT_CTRL_REG(i) 1ワードにIP/IE/ATTR(TRIG/SHV/MODE)/CTL(優先度)を全て格納**（別々の配列ではない）。グローバル優先度マスクはCSR化（後述） | `soc/esp32c5/include/soc/clic_reg.h`（`CLIC_INT_CTRL_REG(i) = DR_REG_CLIC_CTRL_BASE + i*4`），`hal/components/hal/include/hal/interrupt_clic_ll.h` |
| 割込み優先度マスク（THRESH相当） | メモリマップトレジスタ`PLICMX_THRESH_OFF`（`sil_wrw_mem`で書込み） | **CSR化**：`MINTTHRESH_CSR = 0x347`（メモリアクセスではなく`csrw`命令） | `hal/components/riscv/include/riscv/csr_clic.h`（"The ESP32-C5 (MP), C61, H4 and P4 (since REV2) use the standard CLIC specification...defines the mintthresh CSR"） |
| ベクタドモードのCSR値 | `MTVEC_MODE_VECTORD = 0x1`（標準RISC-V vectoredモード．`mcause&0x1f`が直接テーブル索引） | **`MTVEC_MODE_CSR = 3`**（CLIC専用モード）＋別CSR`MTVT`（`0x307`）が実際のベクタテーブルベース | `asp3_core/arch/riscv_gcc/common/riscv.h`（`MTVEC_MODE_VECTORD 0x1`），`hal/components/riscv/include/riscv/csr_clic.h`（`MTVEC_MODE_CSR 3`・`MTVT_CSR 0x307`） |
| 外部割込み番号のオフセット | ソース→CPU割込み線は1〜31（オフセット無し） | **CLIC内部番号0〜15はRISC-V標準例外/内部割込み予約，外部（マトリクス経由）は16〜47（`RV_EXTERNAL_INT_OFFSET=16`・`RV_EXTERNAL_INT_COUNT=32`）** | `hal/components/riscv/include/riscv/csr_clic.h`，`hal/components/riscv/vectors_clic.S`（`_mtvt_table`が0〜47の48エントリ） |
| CLIC総割込み数 | （PLIC_MX方式のため該当せず．31本固定） | `CLIC_INT_INFO_REG`既定値**48**（`CLIC_INT_INFO_NUM_INT`のデフォルト13'd48）＝ちょうど16(内部)+32(外部)と一致 | `soc/esp32c5/include/soc/clic_reg.h` |
| XTAL周波数 | **40MHzのみ**（`SOC_XTAL_SUPPORT_40M`のみ，`soc_xtal_freq_t`は`SOC_XTAL_FREQ_40M`のみ） | **40MHz／48MHzの両対応**（`SOC_XTAL_SUPPORT_40M`と`SOC_XTAL_SUPPORT_48M`が両方定義，`soc_xtal_freq_t = {SOC_XTAL_FREQ_40M=40, SOC_XTAL_FREQ_48M=48}`）。**どちらが実装されているかはボード依存であり実機で確認する必要がある（推測禁止）** | `soc/esp32c6/include/soc/soc_caps.h`・`clk_tree_defs.h`，`soc/esp32c5/include/soc/soc_caps.h`（`SOC_XTAL_SUPPORT_40M 1`・`SOC_XTAL_SUPPORT_48M 1`）・`clk_tree_defs.h` |
| SPLL固定周波数 | 480MHz固定，派生タップは`F80M/F160M/F240M`のみ | SPLLは`SOC_MOD_CLK_SPLL`（480MHz固定，コメントに明記）だが派生タップが**F12M/F20M/F40M/F48M/F60M/F80M/F120M/F160M/F240M**へ拡張 | `soc/esp32c6/include/soc/clk_tree_defs.h`，`soc/esp32c5/include/soc/clk_tree_defs.h` |
| RC32K内蔵発振器 | あり（`SOC_CLK_RC32K_SUPPORTED`） | **なし**（C5には該当マクロ・定義が存在しない） | `soc/esp32c6/include/soc/soc_caps.h`（`SOC_CLK_RC32K_SUPPORTED (1)`），`soc/esp32c5/include/soc/soc_caps.h`（該当なし） |
| APB_CLK_FREQ | 40MHz固定 | 40MHz固定（同一） | `soc/esp32c6/include/soc/soc.h:138`・`soc/esp32c5/include/soc/soc.h:135`（`APB_CLK_FREQ (40*1000000)`） |
| CPUクロック（既知の実測／既定値） | 実機診断済み**160MHz**（ROMがSPLL÷3÷1に設定済み，起動後の書換え不要。`chip_kernel_impl.c`/`esp32c6.h`のコメント参照） | 【未確認】。C5の`soc_cpu_clk_src_t`はXTAL/RC_FAST/PLL_F160M/PLL_F240Mを露出しており240MHz運用の可能性が示唆されるが，ROM起動直後の実際の分周設定・PCR相当レジスタの初期値は実機診断が必要 | `soc/esp32c5/include/soc/clk_tree_defs.h`（要実機PCR/HPレジスタダンプでの確認．asp3_core側に相当ヘッダ未作成） |
| systimerクロック | XTAL基準・16MHz固定（40MHz÷2.5，実機較正済みticks/us=16.024） | systimerクロックソースenum自体は`SYSTIMER_CLK_SRC_XTAL`/`RC_FAST`でC6と同一だが，**XTALが40/48MHzのどちらかで実際のticks/us値が変わる**ため実機較正が必須 | `soc/esp32c5/include/soc/clk_tree_defs.h`（enum一致を確認）．実測値は未取得 |
| HP SRAM（IRAM/DRAM統合領域） | `SOC_IRAM_LOW=0x40800000`〜`SOC_IRAM_HIGH=0x40880000`＝**512KiB** | `SOC_IRAM_LOW=0x40800000`〜`SOC_IRAM_HIGH=0x40860000`＝**384KiB**（C6より128KiB少ない） | `soc/esp32c6/include/soc/soc.h`，`soc/esp32c5/include/soc/soc.h` |
| LP（RTCメモリ） | `0x50000000`〜16KiB | 同一：`0x50000000`〜16KiB | 両`soc/esp32c{5,6}/include/soc/soc.h`（それぞれ"only has 16k LP memory"と明記） |
| Flashマップ窓（IROM/DROM） | `SOC_IROM_LOW=0x42000000`．窓幅は`SOC_MMU_PAGE_SIZE<<8`＝既定64KiB×256＝**16MiB**（`SOC_MMU_PAGE_SIZE_CONFIGURABLE`あり） | `SOC_IROM_LOW=0x42000000`（ベースはC6と同じ）／`SOC_IROM_HIGH=0x44000000`＝**32MiB固定**（`SOC_MMU_PAGE_SIZE_CONFIGURABLE`マクロ自体が存在しない＝可変ページサイズ非対応） | `soc/esp32c6/include/soc/soc.h`・`ext_mem_defs.h`，`soc/esp32c5/include/soc/soc.h`（`SOC_IROM_HIGH`固定値）・`ext_mem_defs.h` |
| MMUエントリ数 | `SOC_MMU_ENTRY_NUM=256` | `SOC_MMU_ENTRY_NUM=512` | `soc/esp32c6/include/soc/ext_mem_defs.h`，`soc/esp32c5/include/soc/ext_mem_defs.h` |
| IROM/DROM分離 | **なし**（`SOC_DROM_LOW==SOC_IROM_LOW`共に`0x42000000`）→C6版`esp32c6.ld`は単一FLASH領域 | 【要確認】：`SOC_IROM_LOW=0x42000000`のみ確認できた。`SOC_DROM_LOW`がC6同様IROMと同一かどうかは`soc.h`の当該行を個別に再確認すること（本調査では明示的な等号関係の記述箇所を確定できていない＝【未確認】。ここが分離型ならC3方式のリンカスクリプトへ回帰が必要） | `soc/esp32c5/include/soc/soc.h`（IROM/DROM等号関係は追加確認要） |
| UART0ベースアドレス | `DR_REG_UART_BASE = 0x60000000`（マクロ名に"0"なし） | `DR_REG_UART0_BASE = 0x60000000`（マクロ名が変わっただけで**数値は同一**） | `soc/esp32c6/register/soc/reg_base.h`，`soc/esp32c5/register/soc/reg_base.h` |
| UARTレジスタオフセット | FIFO=+0x0／INT_RAW=+0x4／INT_ST=+0x8／INT_ENA=+0xc／INT_CLR=+0x10／STATUS=+0x1c／CONF1=+0x24（RXFIFO_CNT bit[7:0]・TXFIFO_CNT bit[23:16]） | **全オフセット・ビット位置ともC6と完全一致**（実ヘッダで逐一確認済み） | `soc/esp32c6/register/soc/uart_reg.h`，`soc/esp32c5/register/soc/uart_reg.h`（両者diffなし） |
| USB Serial/JTAGベースアドレス | `DR_REG_USB_SERIAL_JTAG_BASE = 0x6000F000` | 同一：`0x6000F000` | `soc/esp32c6/register/soc/reg_base.h`，`soc/esp32c5/register/soc/reg_base.h` |
| eFuseベースアドレス | `DR_REG_EFUSE_BASE = 0x600B0800` | **`DR_REG_EFUSE_BASE = 0x600B4800`**（+0x4000移動） | `soc/esp32c6/register/soc/reg_base.h`，`soc/esp32c5/register/soc/reg_base.h` |
| eFuse MACフィールドのベース相対オフセット | `+0x44`（`EFUSE_RD_MAC_SPI_SYS_0_REG`）／`+0x48`（`_1_REG`） | **オフセット自体は不変**：`+0x44`（`EFUSE_RD_MAC_SYS0_REG`）／`+0x48`（`EFUSE_RD_MAC_SYS1_REG`）。レジスタ名のみ変化 | `soc/esp32c6/register/soc/efuse_reg.h`，`soc/esp32c5/register/soc/efuse_reg.h` |
| LPPERIベースアドレス | `DR_REG_LPPERI_BASE = 0x600B2800` | 同一：`0x600B2800` | `soc/esp32c6/register/soc/reg_base.h`，`soc/esp32c5/register/soc/reg_base.h` |
| HW RNGレジスタ（`WDEV_RND_REG`） | `LPPERI_RNG_DATA_REG`＝`LPPERI_BASE + 0x8` | **`LPPERI_RNG_DATA_SYNC_REG`＝`LPPERI_BASE + 0x28`**（C5では"sync"版へエイリアス先が変わる。+0x8番地自体は存在するが`WDEV_RND_REG`が指すのは+0x28） | `soc/esp32c6/include/soc/wdev_reg.h`（`WDEV_RND_REG = LPPERI_RNG_DATA_REG`），`soc/esp32c5/include/soc/wdev_reg.h`（`WDEV_RND_REG = LPPERI_RNG_DATA_SYNC_REG`），両`lpperi_reg.h` |
| PMU／modem_clock | `esp_hal_pmu/esp32c6/`・`hal/esp32c6/modem_clock_hal.c`・`esp_hw_support/port/esp32c6/`（rtc_clk.c等） | **ファイル構成は左とファイル単位で対称に存在**（`esp_hal_pmu/esp32c5/`・`hal/esp32c5/modem_clock_hal.c`・`esp_hw_support/port/esp32c5/`一式）。レジスタ値レベルの中身は本調査では逐一比較していない | `find hal -path "*modem_clock*"`・`esp_hw_support/port/esp32c{5,6}/`のディレクトリリスト（file-for-file同一と確認） |
| Wi-Fi blob（`esp_wifi/lib/`） | フラット構造，`libcore.a libespnow.a libmesh.a libnet80211.a libpp.a libsmartconfig.a libwapi.a`の7本 | **同一ファイル名7本，同じくフラット構造**（サイズのみ相違．例：`libnet80211.a` C6=1,429,794B／C5=1,668,150B） | `hal/components/esp_wifi/lib/esp32c{5,6}/`の実listing |
| PHY blob（`esp_phy/lib/`） | フラット構造5本：`libbtbb.a libbttestmode.a libphy.a librfate.a librftest.a` | 同一ファイル名5本，同じくフラット構造 | `hal/components/esp_phy/lib/esp32c{5,6}/`の実listing |
| ROM ld（esp_wifi.cmakeが参照する12本＋net80211/pp/phy/systimer/coexist） | `esp32c6.rom.{ld,api,libc,libgcc,newlib,libc-suboptimal_for_misaligned_mem,version,net80211,pp,phy,systimer,coexist}.ld`（`newlib-normal.ld`という未使用ファイルも存在） | **同一命名規則で全項目が実在**（`esp32c5.rom.*.ld`）。加えて`esp32c5.rom.eco3.ld`が**C5にのみ存在**（後述） | `hal/components/esp_rom/esp32c{5,6}/ld/`の実listing，`asp3/target/esp32c6_espidf/esp_wifi.cmake`の該当行 |
| `phy_get_max_pwr`（PHY最大送信電力） | ROM未解決（`eco*.ld`が存在しない）ため`esp_shim_blobglue.c`で固定値20dBmのスタブを暫定実装 | **`esp32c5.rom.eco3.ld`が存在し，そこに`phy_get_max_pwr = 0x400010f0`という実ROMアドレスへの定義がある**＝スタブ不要でROM実体をリンクできる（C6より有利な状況。ただし"eco3"というシリコンrev固有命名の意味・全rev互換性は要確認） | `hal/components/esp_rom/esp32c5/ld/esp32c5.rom.eco3.ld` |
| `esp_rom/patches/` | `esp_rom_hp_regi2c_esp32c6.c`のみ | `esp_rom_hp_regi2c_esp32c5.c`に加え**`esp_rom_cache_esp32c5.c`が追加存在**（C6には対応patchなし） | `hal/components/esp_rom/patches/`の実listing |
| `SOC_CPU_CORES_NUM` | `(1U)` | `(1U)`（同一．シングルコア） | 両`soc_caps.h` |
| `SOC_WIFI_SUPPORTED`／`SOC_BLE_SUPPORTED`／`SOC_IEEE802154_SUPPORTED` | すべて`1` | すべて`1`（同一） | 両`soc_caps.h` |
| flash_headerマジック（Direct Boot） | `0xaedb041d`×2（C3と共通の値） | 【未確認・実機要検証】：ASP3独自Direct Boot規約（esptool標準imageヘッダ非使用）のため理屈上チップ非依存の値のはずだが，C5では未検証 | `asp3/target/esp32c6_espidf/flash_header.S`のコメント（C3/C6での確認実績のみ）．C5では実機検証が必須 |
| esptool `--chip`値 | `esp32c6` | `esp32c5`（`hal/components/bootloader_support/include/esp_app_format.h`に`ESP_CHIP_ID_ESP32C5 = 0x0017`が定義済み＝esp-hal-3rdparty側はC5を正式に認識している。ただしASP3のDirect Bootはimage header方式を使わないためこの値自体はASP3のブートには使わない．esptoolコマンドラインの`--chip esp32c5`が現行pinned esptoolバージョンでサポートされているかは別途確認要） | `hal/components/bootloader_support/include/esp_app_format.h` |

---

## 4. arch/riscv_gcc/esp32c5 ファイル別変更計画（22ファイル）

配置：`/home/user/asp3_esp_idf/asp3/arch/riscv_gcc/esp32c5/`（新設，submodule外）。
以下はC6の同名ファイル（`asp3_core/arch/riscv_gcc/esp32c6/`）を全文精読した上での
変更計画。「c6→c5」はファイル内の全識別子（`ESP32C6_*`→`ESP32C5_*`・
`esp32c6_*`→`esp32c5_*`・`TOPPERS_ESP32C6*`→`TOPPERS_ESP32C5*`・コメント中の
"ESP32-C6"→"ESP32-C5"）の一括置換を指す。

| ファイル | 内容 | 変更区分 | 変更内容 |
|---|---|---|---|
| `chip.cmake` | チップ依存部CMake定義本体。`CHIPDIR`定義・コンパイルオプション（`-march=rv32imc_zicsr_zifencei`等）・コンソール選択・共通arch(`arch/riscv_gcc/common/arch.cmake`)のinclude | **要修正（設計上最重要）** | (1) `CHIPDIR`を`${CMAKE_CURRENT_LIST_DIR}`に変更（§2.2）。(2) `-march`はC5もRV32IMC＋Zicsr/Zifencei前提だが**要実機/ツールチェーン確認**（CLIC拡張命令セットの必要有無を要確認）。(3) `ASP3_RISCV_OMIT_PLIC_MTIMER`はC5でも維持（SYSTIMERをHRTに使う方針は変えない。C5のCLINT/mtimeは使わない）。他は`c6→c5`置換のみ |
| `chip_asm.inc` | アセンブリマクロ（`save/restore_additional_regs_*`はいずれも空．TLS初期化マクロのみ実装） | **無変更コピー可**（コメントのみ`c6→c5`） | C6版が「pico2_riscv/rp2350版からの流用・内容差分なし」と明記する通りチップ非依存。TLS初期化ロジックも汎用 |
| `chip_kernel.h` | 割込み優先度範囲（`TMIN_INTPRI=-7`〜`TMAX_INTPRI=-1`）・サポート機能マクロ（ENA/DIS/CLR/RAS/PRB_INT） | **要修正（内容精査）** | CLICの優先度ビット幅がPLIC_MX同様7段階（1〜7）で表現できるかは`NLBITS`設定に依存（`clic_reg.h`の`CLIC_INT_CONFIG_MNLBITS`）。デフォルトのNLBITS値を実機/ROM初期値から確認し，7段階で足りるかを検証すること。CLR_INT/RAS_INT（FROM_CPUソースの利用）が新方式でも同様に使えるかは§4の`chip_kernel_impl.c`の項を参照 |
| `chip_kernel.py` | パス2生成スクリプト。`INTNO_VALID = list(range(1, 32))` | **要修正（値の再検討）** | ASP3のINTNO番号割当をCLICでも「1〜31」のまま維持するか（INTMTXのソース→線番号は0〜31のまま存続する見込みのため，論理的なINTNOはC6同様1〜31を維持し，CLICへの物理変換（+16オフセット）は`chip_kernel_impl.c`/`intmtx_kernel_impl.h`相当層に閉じ込める設計を推奨）。この設計が妥当であることを`chip_kernel_impl.c`実装時に再確認すること |
| `chip_kernel_impl.c` | チップ依存の初期化本体：`chip_initialize()`（mtvec設定・`intmtx_initialize()`・`mie`全許可）・`esp32c6_intmtx_route()`（ソース→線の割当）・`initialize_interrupt()` | **要修正（アーキテクチャ変更・最大の作業項目の一つ）** | (1) `riscv_write_mtvec(...\|MTVEC_MODE_VECTORD)`を，CLIC用の初期化列に置き換える：`mtvec`に`MTVEC_MODE_CSR=3`をセットし，別途`MTVT_CSR(0x307)`にベクタテーブルベースアドレスを書く（`csrw`インラインアセンブリまたは新規ヘルパ関数）。(2) `mie`全許可(`csrw mie, ~0`)は，CLIC下では**ソース単位のenableがCLIC_INT_CTRL_REG(i)のIEビット側にある**ため，標準`mie`の役割（特にMEIE等の外部割込み許可ビット）がCLICモードでどう振る舞うかを実機・仕様で要確認（【未確認】。安易にC6のコードをそのまま複製しない）。(3) `esp32c6_intmtx_route()`のINTMTX書込み自体（`INTMTX_BASE + intsrc*4`へのソース→線番号書込み）は**アドレス・方式ともC5でも同一のまま流用可**（§3の通りINTMTX自体は不変）。(4) 新たに，CLICの`CLIC_INT_CTRL_REG(i)`へIE/ATTR(TRIG=level／SHV=hardware-vectored有無)/CTL(優先度)を書き込む初期化コードを追加する必要がある（`intmtx_initialize()`相当の拡張，あるいは新規`clic_initialize()`として分離することを推奨） |
| `chip_kernel_impl.h` | `kernel_impl.h`のチップ依存部。`TMIN_INTNO=1`/`TMAX_INTNO=31`・`t_set_ipm`/`t_get_ipm`（THRESH経由）・`disable_int`/`enable_int`/`probe_int`（INTMTX委譲）・`intmtx_kernel_impl.h`のinclude | **要修正** | `TMIN/TMAX_INTNO`はASP3内部の論理番号としては1〜31を維持する設計を推奨（§chip_kernel.pyの項参照）。`t_set_ipm`/`t_get_ipm`はCSR(`MINTTHRESH_CSR`)アクセスへ変更。`disable_int`/`enable_int`/`probe_int`は新設する`clic_kernel_impl.h`（後述）への委譲に変更 |
| `intmtx_kernel_impl.h` | ソースルーティング（INTMTX）とCPU割込み線制御（PLIC_MX）の低レベル操作。`PLICMX_BASE/ENABLE_OFF/TYPE_OFF/CLEAR_OFF/EIP_OFF/PRI_BASEOFF/THRESH_OFF`の定義とinline関数群 | **大幅書き換え（新規ファイル`clic_kernel_impl.h`として再設計することを推奨）** | INTMTX側（ソース→線のMAPレジスタ操作，`intmtx_srcmask`/`intmtx_from_cpu`のテーブル管理，`intmtx_probe_int`の生ステータス読み＝`INTMTX_STATUS0/1/2_OFF`）は**アドレス・ロジックともほぼ流用可**（§3参照）。CPU側制御（現在の`PLICMX_*`操作）は全面的にCLICのAPIへ置き換える：`intmtx_enable_int`/`intmtx_disable_int`→`CLIC_INT_CTRL_REG(i)`のIEビット操作，`intmtx_set_priority`→同レジスタのCTLフィールド（`NLBITS`変換込み，`csr_clic.h`の`NLBITS_TO_BYTE`相当のマクロが必要），`intmtx_set_thresh`/`get_thresh`→`MINTTHRESH_CSR`へのCSR読み書き（メモリアクセスからCSR命令へ変更）。ファイル名を実態に合わせ`clic_kernel_impl.h`に改名することも設計上検討する（chip_kernel_impl.hからのinclude名を変更） |
| `chip_os_awareness.py` | GDB OS-Awareness．PLICMX ENABLE/EIPレジスタを直接読んで`int_enabled`/`int_pending`を実装 | **要修正** | CLICのCLIC_INT_CTRL_REG(i)からIE/IPビットを読むよう書き換え。アドレスは`DR_REG_CLIC_CTRL_BASE(0x20801000) + i*4`（`i`はCLIC内部番号＝ASP3のINTNO+16オフセットに注意） |
| `chip_rename.def` | genrename入力（識別子のTOPPERS標準命名規則へのマングリング指定） | **無変更コピー可（要再生成）** | チップ非依存の識別子リストのはずだが，`chip_kernel_impl.c`/`intmtx_kernel_impl.h`の関数名が変わる場合はここに追加が必要。`genrename`で`chip_rename.h`/`chip_unrename.h`を再生成すること（手編集しない） |
| `chip_rename.h` | genrename生成物 | **再生成（手編集禁止）** | `chip_rename.def`から`genrename`で生成し直す |
| `chip_unrename.h` | genrename生成物 | **再生成（手編集禁止）** | 同上 |
| `chip_serial.c` | SIOドライバのUART0/USBJTAG切替ブリッジ（`TOPPERS_ESP32C6_CONSOLE_USBJTAG`マクロで`ESP32C6_SIO(name)`が`esp32c6_uart_*`/`esp32c6_usbjtag_*`のどちらに展開されるかを切替） | **無変更コピー可（識別子置換のみ）** | ロジックはチップ非依存（マクロ経由の間接呼出しのみ）。`c6→c5`置換のみ |
| `chip_serial.cfg` | SIOの静的API定義（`ATT_TER`・`CFG_INT`・`CRE_ISR`） | **無変更コピー可** | チップ非依存の定義のみ |
| `chip_serial.h` | SIOドライバの公開インタフェース宣言＋UART/USBJTAGヘッダの選択include | **無変更コピー可（識別子置換のみ）** | ロジック変更なし |
| `chip_sil.h` | ビット操作マクロ（read-modify-write方式．RP2350のアトミックエイリアスは不使用） | **無変更コピー可** | C5にRP2350型のアトミックエイリアスが存在しないことを確認する必要はあるが（【未確認・低リスク】），通常のread-modify-writeで問題ない設計は変更不要 |
| `chip_stddef.h` | ターゲット識別マクロ`TOPPERS_ESP32C6`・`TOPPERS_STDFLOAT_TYPE1` | **要修正（マクロ名のみ）** | `TOPPERS_ESP32C6`→`TOPPERS_ESP32C5`。他は無変更 |
| `chip_support.S` | トラップベクタテーブル（vectoredモード，`mcause&0x1f`で32エントリ索引）・`irc_begin_int`/`irc_end_int`/`irc_get_intpri`/`irc_begin_exc`/`irc_end_exc`（すべてPLICMX_THRESHのメモリアクセスで優先度昇格・復元） | **大幅書き換え（最大の作業項目）** | (1) トラップベクタテーブル自体を，CLICの`_mtvt_table`方式（**ジャンプ命令の配列ではなくハンドラアドレスのワード配列**．`hal/components/riscv/vectors_clic.S`参照）へ変更する必要がある可能性が高い。ただし，ASP3は「vectoredモードで全エントリが共通の`core_int_entry`へ飛び，そこでソフトウェアが`inh_table`を引く」という設計であるため，CLICの「各エントリに直接ハンドラを書く」方式とは設計思想が異なる。**CLICのnon-vectoredモード（`clicintattr[i].shv=0`）を使えば，全割込みが単一の`mtvec`（アライン要件`<<6`＝64byte境界）へ飛び，`mcause`のexception codeから番号を取り出す，という現行に近い設計を維持できる可能性がある**（要検討・要実機確認）。(2) `irc_begin_int`/`irc_end_int`等の`PLICMX_THRESH_OFF`へのメモリアクセス（`lw`/`sw`命令＋`li PLICMX_BASE`）は，`MINTTHRESH_CSR(0x347)`への`csrr`/`csrw`命令へ全面的に置き換える。(3) 割込み番号取得（`mcause`下位5bit）のロジックは，CLICでも`mcause`のexception codeフィールドから取得する点は同様だが，**ASP3のINTNO(1〜31)とCLIC内部番号(外部は16〜47)の対応をここで変換する**必要がある（`andi a0, t0, 0x1f`だけでは足りず，オフセット減算が必要になる可能性が高い．要設計・要実機確認） |
| `chip_unrename.h` | genrename生成物（chip_rename.hの逆定義） | **再生成（手編集禁止）** | 同上 |
| `esp32c6.h` | 全レジスタベースアドレス・メモリマップ・WDT解錠キー・SIL_DLY較正値・SYSTIMER/USBJTAGレジスタオフセット定義集 | **要修正（値の全面差替え，最重要ファイルの一つ）** | ファイル名`esp32c5.h`。§3の差分表に従い，`ESP32C5_DRAM_SIZE`（384KB！512KBではない），`ESP32C5_IROM/DROM_BASE`（DROMがIROMと同一かは§3の【要確認】項目），`ESP32C5_INTMTX_BASE`（0x60010000のまま流用），`ESP32C5_PLIC_MX_BASE`は**削除し，代わりに`ESP32C5_CLIC_BASE(0x20800000)`・`ESP32C5_CLIC_CTRL_BASE(0x20801000)`を新設**，`ESP32C5_UART0_BASE`（0x60000000のまま），`ESP32C5_USBJTAG_BASE`（0x6000F000のまま），`ESP32C5_TIMG0/1_BASE`・`ESP32C5_LP_WDT_BASE`・`ESP32C5_PCR_BASE`・WDT解錠キー・SIL_DLY較正値は**すべて実機診断が必要**（C6の値をそのまま転記しない。C6のesp32c6.h冒頭コメント自体が「Phase A時点でCLICと誤認していた」という訂正履歴を持つ＝レジスタドキュメントの誤読は実際に起きた事故であるという教訓を活かす） |
| `esp32c6_uart.c` | UART0用簡易SIOドライバの実装（getready/putready/getchar/putchar＋初期化・ISR等） | **要修正（レジスタベース定義のみ．ロジックは流用可）** | ファイル名`esp32c5_uart.c`。§3で確認した通り，UARTレジスタオフセット（FIFO=+0x0／INT_RAW=+0x4／.../CONF1=+0x24，RXFIFO_CNT・TXFIFO_CNT のビット位置）はC5とC6で完全一致することを実ヘッダで確認済み。したがってベースアドレスマクロ名（`ESP32C6_UART0_BASE`→`ESP32C5_UART0_BASE`，値0x60000000は同一）と識別子の置換のみで移植可能性が高い |
| `esp32c6_uart.h` | UART0レジスタオフセット定義・SIOPCB型・関数宣言 | **要修正（識別子置換のみ，オフセット値は据え置き）** | 同上。§3の差分表で全オフセット一致を確認済みのため実質コピー＋rename |
| `esp32c6_usbjtag.c` | USB Serial/JTAG用簡易SIOドライバの実装（`esp32c6.h`のEP1/EP1_CONFマクロを使用） | **要修正（ベースアドレスのみ，要一次確認）** | ファイル名`esp32c5_usbjtag.c`。ベースアドレス0x6000F000はC5/C6で同一（§3）。ただしEP1/EP1_CONFのオフセット・ビット定義自体は本調査で`esp32c6.h`側の定義（+0x00/+0x04）しか確認しておらず，C5側のUSB_SERIAL_JTAGレジスタ（`soc/esp32c5/register/soc/usb_serial_jtag_reg.h`相当）でのオフセット一致は**未確認（実装着手前に確認すること）** |
| `esp32c6_usbjtag.h` | USBJTAGレジスタオフセット・SIOPCB型・関数宣言 | **要修正（同上，要一次確認）** | 同上 |

**まとめ**：22ファイル中，**無変更コピー可（識別子置換のみ）**が
`chip_asm.inc`・`chip_serial.c`・`chip_serial.cfg`・`chip_serial.h`・
`chip_sil.h`（5ファイル），**軽微な値差替えのみ**が`chip_stddef.h`・
`chip_rename.def`（genrename入力）・`esp32c6_uart.{c,h}`（4ファイル，
uartはオフセット一致確認済み），**要一次確認だが方式は同じ**が
`esp32c6_usbjtag.{c,h}`（2ファイル），**genrename再生成のみ**が
`chip_rename.h`・`chip_unrename.h`（2ファイル），**値の全面差替え**が
`esp32c6.h`・`chip.cmake`・`chip_kernel.py`（3ファイル），**アーキテクチャ
再設計を要する**のが`chip_kernel_impl.c`・`chip_kernel_impl.h`・
`intmtx_kernel_impl.h`・`chip_support.S`・`chip_os_awareness.py`
（5ファイル，割込みコントローラ関連）。新規ファイルなし（全22ファイルが
C6に対応物を持つ）。

---

## 5. target/esp32c5_espidf ファイル別変更計画

配置：`asp3/target/esp32c5_espidf/`（`asp3/target/esp32c6_espidf/`のコピー起点）。

### 5.1 単純リネームで足りるファイル（~14ファイル）

`target_asm.inc`・`target_cfg1_out.h`・`target_check.py`・
`target_kernel.cfg`・`target_kernel.py`・`target_os_awareness.py`
（sys.pathの文字列のみ`esp32c5`化）・`target_rename.def`（`genrename`で
`target_rename.h`/`target_unrename.h`を再生成）・`target_serial.cfg`・
`target_serial.h`・`target_sil.h`・`target_stddef.h`（`TOPPERS_ESP32C6_GCC`→
`TOPPERS_ESP32C5_GCC`）・`target_timer.cfg`。いずれもチップ依存の
レジスタ知識を持たない薄い委譲層であることをC6版の内容から確認済み。

### 5.2 パス・マクロ置換＋C5 SoCヘッダでの値再検証が必要（~9ファイル）

| ファイル | 変更内容 |
|---|---|
| `esp32c5.ld` | §6参照。IROM/DROM分離要否（§3の【要確認】項目）に応じて構造そのものが変わりうる最重要ファイル |
| `run.cmake` | `esp32c5.ld`の構造決定に従属（`.text`/`.data`のみの2セクション抽出か，C3方式の3セクション抽出か） |
| `target.cmake` | `ESP32C6_*`→`ESP32C5_*`（`ESP32C5_CONSOLE`/`ESP32C5_ESPTOOL`/`ESP32C5_PORT`/`ESP32C5_WIFI`），`--chip esp32c6`→`--chip esp32c5`（**pinned esptoolバージョンが`esp32c5`をサポートするか要確認**），`include(...chip.cmake)`のパスを§2.2の通り書換え，Espressif版QEMU forkに`esp32c5`マシンが追加されているかを**C6のときと違い思い込みで「非対応」と決め打ちせず確認する**（追加されていればQEMU検証を先行できる可能性がある） |
| `esp_wifi.cmake` | §6参照 |
| `target_syssvc.h` | `ESP32C6_UART0_BASE`→`ESP32C5_UART0_BASE`（値同一），`TARGET_NAME`→`"ESP32-C5"`。`INTNO_SIO`等の「Wi-Fi shimが線1-15を占有するため退避」という設計思想を維持するかは，CLIC移行後のINTNO割当設計（§4）と整合させる必要がある |
| `target_test.h` | 同上（`INTNO1`のFROM_CPU_1強制のロジックがCLIC後も同じ形で使えるかは§4の`chip_kernel_impl.c`の項に従属） |
| `target_kernel_impl.h` | `#include "esp32c5.h"`へ変更 |
| `target_timer.h` | `ESP32C5_SYSTIMER_TICKS_PER_US`は**実機較正必須**（C6も16.024/16と一致したが，C5のXTALが40MHzか48MHzかで比率が変わりうる。既定値をそのまま転記しないこと）。`ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0`によるソフトウェア割込み強制がCLIC後も同じレジスタ位置に存在するかは§4のchip_kernel_impl.c項と合わせて要確認 |
| `flash_header.S` | マジック`0xaedb041d`は理屈上チップ非依存だが実機未検証（§3参照）。C3/C6での実績から流用してよいが，B-0で最初に検証すべき項目として明記する |

### 5.3 レジスタ知識を持ち実機検証が前提（~2ファイル．最重要）

| ファイル | 変更内容 |
|---|---|
| `target_kernel_impl.c` | WDT無効化（`ESP32C5_TIMG*_WDTWPROTECT/WDTCONFIG0`・`ESP32C5_LP_WDT_*`）とクロック関連の診断コードは**すべて実機診断が前提**。C6では`esp32c6.h`側の解錠キー定数に実際にバグ（SWDキー`0x8F1D312A`のはずが誤記あり）があり，このファイル側でインラインの正しい値を使う回避策が入っていた実績がある＝**C5でも同様のヘッダ側の誤りが起こりうる前提で臨むこと**。割込みルーティング呼出し（`esp32c6_intmtx_route`相当）は§4のCLIC対応後のAPIに合わせて書き換える。C6由来の一時診断コード（GPIO/UART/RTC-RAM計装）は移植せず，クリーンな状態から始めることを推奨（C6ブリングアップ専用の残骸のため） |
| `wifi/esp_wifi_adapter.c` | Wi-Fi os_adapter shim中で最もチップ固有レジスタ密度が高いファイル。§5.4参照 |

### 5.4 Wi-Fi shimファイル（`wifi/`）

| ファイル | 変更内容 |
|---|---|
| `esp_shim.c` | `TOPPERS_ESP32C5_WIFI`ガードを追加。`esp32c6_systimer_read()`呼出しをリネーム。HW RNGレジスタ（`esp_shim_random()`）を**`LPPERI_RNG_DATA_SYNC_REG`（`LPPERI_BASE+0x28`）**へ差し替え（C6の`+0x8`をそのまま流用しない。§3参照） |
| `esp_shim_blobglue.c` | `esp_read_mac()`のeFuseベースを`0x600B4800`に変更（オフセット+0x44/+0x48自体は維持）。**`phy_get_max_pwr()`の固定値スタブ（C6の20dBm決め打ち）は削除し，`esp32c5.rom.eco3.ld`をリンクしてROM実体を使う設計に変更する**（§3・§6参照。C6より良い実装が可能な数少ない好材料）。`floor`/`putchar`スタブは（ツールチェーン側のlibm欠如に起因するため）C5でも同様に必要と見込まれる |
| `esp_wifi_adapter.c` | **書き換え量が最大**。`INTMTX_BASE_ADDR`（0x60010000，変更なし）はそのまま。`PLICMX_BASE_ADDR`（0x20001000）を用いた`set_intr_wrapper`/`clear_intr_wrapper`/`ints_on/off_wrapper`は，§4のCLIC対応API（`CLIC_INT_CTRL_REG`操作／`MINTTHRESH_CSR`）へ全面書き換え。`MODEM_LPCON`（`0x600af018`等）・`pmu_dev_t`（`0x600B0000`）・`modem_syscon_dev_t`（`0x600A9800`）・`LP_AON_STORE1`（`0x600B1004`）は**C6の値を転記せず，C5のhal `soc/esp32c5/register/soc/{lp_clkrst,pmu,lp_aon,modem_syscon,modem_lpcon}_reg.h`相当から個別に再確認**すること（C3→C6でこの種のレジスタが実際に移動した実績があるため）。esp-hal自身の`wifi_os_adapter.h`が`CONFIG_IDF_TARGET_ESP32C6 || ESP32C5 || ESP32C61`という条件でABI（構造体レイアウト）をC5とC6で共有している点は，Espressif自身がC5をC6に近い世代として扱っている一次証拠であり，設計の出発仮説（"C6の構造をベースにレジスタ値だけ差し替える"）を支持する好材料 |
| `wifi_trace.c`／`wifi_trace.h` | **C5へは移植しない（推奨）**。C6 RX不成立調査専用の一時診断コードであり，レジスタ値もC6専用・一部は公開ヘッダ未収録の値（プローブで発見したもの）。C5で同種の調査が必要になった時点で，C5の実アドレスに基づき新規に書き起こす |

---

## 6. esp_wifi.cmake・リンカスクリプト計画

### 6.1 esp_wifi.cmake

C6版の`WIFI_CHIP_SERIES`変数（`set(WIFI_CHIP_SERIES esp32c6)`）を
`esp32c5`に変更するだけで，以下はそのまま機能する見込み（§3・Wi-Fi blob
inventory調査で実在を確認済み）：

- 全includeディレクトリ（`esp_hw_support/port/esp32c5/include`・
  `efuse/esp32c5/include`・`esp_rom/esp32c5/include/esp32c5`・
  `esp_hal_gpio/esp32c5/include`・`esp_hal_clock/esp32c5/include`・
  `esp_hal_pmu/esp32c5/include`・`esp_phy/esp32c5/include`）
- `CONFIG_IDF_TARGET_ESP32C5=1`のコンパイル定義
- `esp_phy/esp32c5/phy_init_data.c`・`esp_hw_support/port/esp32c5/rtc_clk.c`・
  `hal/esp32c5/modem_clock_hal.c`・`hal/esp32c5/efuse_hal.c`のソース差替え
- Wi-Fi/PHY blobのリンクパス（`-L.../esp_wifi/lib/esp32c5`等）
- ROM ld 12本（`esp32c5.rom.{ld,api,libc,libgcc,newlib,
  libc-suboptimal_for_misaligned_mem,version,net80211,pp,phy,systimer,
  coexist}.ld`）

**C5固有の追加変更**：

1. `esp32c5.rom.eco3.ld`を`-Wl,-T`リストへ追加し，`phy_get_max_pwr`を
   ROM実体解決させる（§3・§5.4参照）。全リビジョンで有効かは
   `efuse_hal_chip_revision()`ベースの分岐要否を含め実機確認が必要。
2. C6版に残る診断用`-Wl,--wrap=...`ブロック（`wifi_trace.c`companion，
   C6のRX不成立調査専用）は**移植しない**（クリーンな状態から開始）。
3. 実際にリンクして未定義シンボルエラーを潰していく「link→エラー
   確認→ROM ld追加」のプロセス（C6のesp_wifi.cmake自身がこの手順で
   ROM ldセットを確定させた実績）を，C5でも同様に踏襲する前提で計画する
   （事前にファイルが揃っていることは確認済みだが，実際にリンクが
   通るかはビルドしてみないと確定しない）。

### 6.2 リンカスクリプト（`esp32c5.ld`）

**最重要の未確定事項**：C6の`esp32c6.ld`が単一FLASH領域で済んでいるのは
`SOC_DROM_LOW == SOC_IROM_LOW`（共に`0x42000000`）というC6の特性による。
C5について本調査で確認できたのは`SOC_IROM_LOW=0x42000000`のみで，
`SOC_DROM_LOW`がIROMと同一かどうかの直接証拠は確認できていない
（§3表の【要確認】項目）。したがって設計は2分岐で用意する：

- **分岐A（IROM/DROM統合，C6と同型）**：C6の`esp32c6.ld`をベースに
  `INCLUDE esp32c5.peripherals.ld`・`FLASH ORIGIN=0x42000000`
  （LENGTHは§3の32MiB固定窓を踏まえ再検討）・
  `RAM ORIGIN=0x40800000 LENGTH=384k`（**512kではなく384k**．§3参照，
  C6版の一時的PMP診断用アドレスシフトは持ち込まない）とする。
- **分岐B（IROM/DROM分離，C3と同型）**：C3の`esp32c3.ld`の3領域
  （IROM/DROM/RAM）構成をベースにする。

**実装着手前に必ず`soc/esp32c5/include/soc/soc.h`の`SOC_DROM_LOW`定義を
直接確認し，どちらの分岐かを確定させること**（本書では両分岐を明示し，
コード生成フェーズでの手戻りを避ける）。

セクション構成（`.iram1.*`/`.dram1.*`/`.coexiram.*`/`.wifi0iram.*`等の
Wi-Fi blob/実ソース用IRAM_ATTR/DRAM_ATTRセクション）は，C6で発生した
orphan-sectionリンクエラー（`.data`とLMAが重なる）の教訓を踏まえ，
C3の全セクション名一覧を最初から含めておく（C6は後追いで気づいて
追加した経緯があるため，C5では最初から倣うことで手戻りを避ける）。

### 6.3 flash_header.S・run.cmake

`flash_header.S`のマジック値（`0xaedb041d`）はC3/C6で共通実績があり
チップ非依存の可能性が高いが，**B-0の最初の実機書込みで動作確認するまで
確定情報として扱わない**。`run.cmake`のobjcopy対象セクション・
`--pad-to`値は§6.2のリンカスクリプト分岐と，実際のFlashサイズ
（ボード搭載チップの実装容量．C5のSOC_IROM_HIGH=0x44000000という
32MiB窓はあくまでマップ窓であり実搭載Flash容量とは別）に従って決定する。

---

## 7. ビルド・検証計画

### 7.1 想定コマンド（README.mdのC6手順をC5に読み替え）

```bash
# 実機ビルド（QEMU非対応を既定とするが，§5.2の通りEspressif版QEMU fork
# にesp32c5マシンが追加されているかは着手前に確認すること）
cmake -S asp3/asp3_core -B build/esp32c5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c5_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c5_espidf \
  -DESP32C5_PORT=/dev/ttyACM0
cmake --build build/esp32c5
cmake --build build/esp32c5 --target run    # esptool --chip esp32c5

# test_porting実行にはC6同様tap.cの明示追加が必要
  -DASP3_EXTRA_APP_C_FILES=<...>/test/porting/tap.c \
  -DASP3_APPLNAME=test_porting

# Wi-Fi scan（B-2a）
  -DESP32C5_WIFI=ON \
  -DASP3_APPLDIR=$PWD/apps/wifi_scan -DASP3_APPLNAME=wifi_scan
```

`ASP3_TARGET`・`ASP3_TARGET_DIR`・`ESP32C5_PORT`・`ESP32C5_ESPTOOL`・
`ESP32C5_CONSOLE`・`ESP32C5_WIFI`はいずれもC6の命名規則
（`ESP32C6_*`）をそのまま`ESP32C5_*`に読み替えたもの（target.cmakeの
実装が§5の通りこの命名規則に従うよう変更される前提）。

### 7.2 フェーズ分割と完了判定

| フェーズ | 内容 | 完了判定 |
|---|---|---|
| B-0 | リポジトリ骨格・ビルド・実機書込み・コンソール疎通 | `asp_flash.bin`生成・esptool書込み成功・USB Serial/JTAGコンソールに起動ログが出力される（`flash_header.S`のDirect Bootマジックが機能する一次証拠） |
| B-1 | CLIC対応の割込み基盤・SYSTIMER HRT・`test_porting` | **`test_porting 6/6 PASS`**。C6よりハードルが高い（§2.3）：割込み配送そのものが§4のCLIC実装（`chip_support.S`のベクタ・`MINTTHRESH_CSR`操作）に懸かっているため，まずタイマ割込み・SIO割込みが単発で配送されることを確認してから6項目のテストへ進む段階的デバッグを推奨 |
| B-2a | Wi-Fi scan（`esp_wifi_init`〜`start`〜`scan`） | 実機でAP一覧（SSID/RSSI/ch）を受信。**deaf-RX切り分けの本丸**：ここで受信できればC6固有問題の確定に強く寄与し，できなければ新世代モデム共通問題の可能性が高まる（§0参照） |

### 7.3 段階的デバッグの推奨手順（CLIC移行のリスクを踏まえて）

1. 割込みを一切使わない状態でのDirect Boot起動・コンソール出力（ポーリング
   送信）確認をB-0の最初のマイルストーンとする。
2. 単一の割込み（SYSTIMERまたはSIO）のみを有効化し，`chip_support.S`の
   ベクタ／CSR操作が最小構成で機能することを確認してからtest_portingの
   6項目全体に進む（C6のbring-upで「mie CSRが実機で読めるかどうか」を
   JTAGで確認してから全体が動いた実績を踏襲し，CLICについても同様に
   `mtvec`/`mtvt`/`mintthresh`の値をJTAGで直接読み書きして仕様通りの
   挙動か確認する）。
3. Wi-Fi shimは§5.4の書き換え完了後，まず`esp_wifi_init()`が成功する
   ことを確認し（C6はここで`Breakpoint.`例外に遭遇し，原因が
   `periph_module_reset()`のenum値域最適化によるtrapだった経緯がある。
   C5でも`shared_periph_module_t`まわりの同種の落とし穴がないか
   注意する），その後scanへ進む。

---

## 8. リスクと未確認事項

実機がないと確定できない事項，および机上調査だけでは判断しきれない
事項を列挙する。

### 8.1 実機必須（最優先で潰すべき事項）

1. **CLICのmie/mstatus挙動**：C6では「mie/mipへのアクセスは不正命令」
   というC3からの類推が誤りだったことが実機JTAG調査で判明した実績が
   ある。C5のCLICでも，`mie`の役割（グローバル外部割込み許可か，
   CLIC下では別の意味を持つか）を**同じ轍を踏まず必ず実機で確認する**。
2. **`mtvec`のCLICモード（値3）と`mtvt`(CSR 0x307)の実際の初期化順序・
   アライメント要件**（`hal/components/riscv/vectors_clic.S`は64byte
   アライン要求＝C6の256byteアラインとは異なる）。
3. **CPUクロック（既定値・分周設定）**：C6は「ROMが起動時点で既に
   160MHz設定済み・書換え不要」と判明したが，C5で同じ前提が成立するかは
   PCR相当レジスタの実機ダンプが必要（§3参照）。
4. **XTALが40MHzか48MHzか**（ボード実装依存．ソフトウェアから読み取る
   手段があるか，あるいは既知の固定値として扱えるかも含めて確認）。
5. **SIL_DLY較正値（TIM1/TIM2）**とSYSTIMER ticks/us実測値：C6は
   「単純な比例外挿では合わない」という教訓済み。C5でも同様の反復較正が
   必要。
6. **WDT解錠キー**（TIMG_WDT_WKEY・LP_WDT系）：C6の`esp32c6.h`は
   実際に一部キーの記載 miss（SWDキー）があった実績があり，
   ヘッダの数値を鵜呑みにせず実機で解錠成功を確認する。
7. **flash_header.Sのマジック値がC5でも通用するか**（B-0最初の書込みで
   確認）。
8. **`SOC_DROM_LOW`とC5のIROM/DROM分離要否**（§6.2の分岐決定に必須。
   ヘッダ再確認で分かる可能性もあるが，リンク・実機起動で最終確認）。
9. **`esp32c5.rom.eco3.ld`の`phy_get_max_pwr`が全チップrevで有効か**
   （"eco3"というrev固有命名の意味）。
10. **USB_SERIAL_JTAGレジスタのEP1/EP1_CONFオフセットがC5でもC6と同一か**
    （§4の`esp32c6_usbjtag.{c,h}`の項で「要一次確認」とした箇所。実装
    着手前にヘッダで確認可能なはずだが本調査では未実施）。
11. **esp_wifi_adapter.cのMODEM_LPCON/PMU/MODEM_SYSCON/LP_AON各レジスタの
    C5での実アドレス**（C3→C6で実際に移動した実績があるレジスタ群。
    C6の値を転記せず個別に確認する）。
12. **Wi-Fi shim移植後，C6と同様の`Breakpoint.`例外（enum値域最適化に
    起因するtrap）がC5でも発生するか**（`shared_periph_module_t`が
    C5でも同じ狭いenumのままなら同種の罠が起こりうる）。
13. **esptoolの`--chip esp32c5`がpinnedバージョンでサポートされているか**。
14. **Espressif版QEMU forkに`esp32c5`マシンが追加されているか**
    （C6のときのように「非対応」と決め打ちせず確認する）。

### 8.2 設計判断が必要（実機投入前に方針だけ決めておくべき事項）

1. **`chip_support.S`のベクタ方式**：CLICのnon-vectoredモード
   （単一エントリ＋ソフトウェアでmcauseから番号抽出，現行に近い設計）を
   採用するか，vectoredモード（`_mtvt_table`に各割込み専用ハンドラを
   並べる，esp-hal-3rdparty流の設計）を採用するか。前者はASP3の既存
   `inh_table`ディスパッチ機構との親和性が高く変更量が少ないが，
   CLICが要求する`shv`（selective hardware vectoring）ビットの扱い次第
   では後者が必須になる可能性がある。**本書ではnon-vectored方式を
   第一候補として推奨するが，実機・仕様確認前の暫定判断**である。
2. **ASP3論理INTNO(1〜31)とCLIC外部番号(16〜47)のオフセット変換を
   どの層に閉じ込めるか**：`chip_kernel_impl.h`/`intmtx_kernel_impl.h`
   （提案：`clic_kernel_impl.h`）の中に隠蔽し，`target_*`層・Wi-Fi shim
   層には現行同様「1〜31」のASP3 INTNOのみを見せる設計を推奨（C6からの
   移植性・既存コードとの整合性を優先）。
3. **CLIC優先度のNLBITS設定**：ASP3の内部表現（1〜7の7段階）を
   CLICのCTLフィールド（8bit，実効ビット数はNLBITS依存）にどう
   マッピングするかは，ROM/HAL既定のNLBITS値を採用するか，ASP3側で
   明示的に`CLIC_INT_CONFIG_REG`のMNLBITSを設定するかを決める必要が
   ある。

### 8.3 C6資産の非移植方針（明示）

- `wifi_trace.c`/`wifi_trace.h`（C6専用の一時診断コード）
- `target_kernel_impl.c`のGPIO/UART/RTC-RAM診断計装（C6ブリングアップ
  専用の残骸）
- `esp_wifi.cmake`の`-Wl,--wrap=...`診断ブロック
- `esp32c6.ld`の一時的PMP診断用RAM ORIGINシフト（`0x40819000`）

いずれも「C6の特定調査のための一時コード」であり，C5移植はこれらを
持ち込まずクリーンな状態から開始する（C5固有の調査が必要になれば
その時点でC5の実アドレスに基づき新規に書き起こす）。

---

## 9. 追記：本書の【要確認】項目のうち机上確認で確定した事項（設計書レビュー時）

コード生成着手前にhal実ヘッダで以下を確定させた（いずれも
`hal/components/soc/esp32c5/`の実記載）：

1. **IROM/DROM分離（§3・§6.2・§8.1.8）→ 分岐Aで確定**：
   `soc/esp32c5/include/soc/soc.h:150-151`に
   `SOC_DROM_LOW = SOC_IROM_LOW`・`SOC_DROM_HIGH = SOC_IROM_HIGH`と
   明記（C6と同型の統合マップ）。リンカスクリプトはC6型の
   単一FLASH領域構成でよい。
2. **USB_SERIAL_JTAGのEP1/EP1_CONFオフセット（§8.1.10）→ C6と同一で確定**：
   `soc/esp32c5/register/soc/usb_serial_jtag_reg.h`で
   `EP1_REG=+0x0`・`EP1_CONF_REG=+0x4`（C6と完全一致）。
3. **主要レジスタベースの実値（§8.1.11の一部）**：
   `soc/esp32c5/register/soc/reg_base.h`・`include/modem/reg_base.h`より，
   TIMG0=`0x60008000`（マクロ名は`DR_REG_TIMERG0_BASE`に改名）・
   TIMG1=`+0x1000`・SYSTIMER=`0x6000A000`・INTMTX=`0x60010000`・
   PCR=`0x60096000`・PMU=`0x600B0000`・LP_CLKRST=`0x600B0400`・
   LP_AON=`0x600B1000`・LP_WDT=`0x600B1C00`・MODEM_LPCON=`0x600AF000`は
   いずれもC6と同一値。**MODEM_SYSCONのみ`0x600A9C00`へ移動**
   （C6は`0x600A9800`。esp_wifi_adapter.c移植時の要注意点）。
   なおC5にはAPM/TEE系ベース（TEE=`0x60098000`・HP_APM=`0x60099000`・
   CPU_APM=`0x6009A000`・LP_TEE=`0x600B3400`・LP_APM=`0x600B3800`）が
   存在し，C6のdeaf-RX調査で浮上したAPM/TEE未初期化候補
   （`docs/wifi-shim-c6.md`実施86）はC5でも同様に該当し得る点に留意。

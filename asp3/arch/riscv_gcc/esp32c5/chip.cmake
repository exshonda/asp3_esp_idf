#
#		チップ依存部のCMake定義（ESP32-C5用）
#
#  target.cmake からincludeされる（Makefile.chipのCMake版に相当）．
#
#  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/chip.cmake）からのコピー・
#  C5対応。RP2350 RISC-V（arch/riscv_gcc/rp2350）を雛形に，割込み
#  コントローラをXh3irq→標準RISC-V CLIC，UARTをPL011系→ESP32-C5 UARTに
#  置き換えたもの．ISAはRV32IMC（A拡張なし）．
#
#  本ファイルはsubmodule外（asp3/arch/riscv_gcc/esp32c5/）に配置される
#  （CLAUDE.mdの禁則：asp3/asp3_core submoduleを直接編集しないため）．
#  docs/c5-port-design.md §2.2で検証済みの通り，CHIPDIRを
#  ${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6ではなく${CMAKE_CURRENT_LIST_DIR}
#  にすることで，配置場所に依存せず機能する（target.cmakeの
#  TARGETDIR定義と同じイディオム．PORTING_GUIDE.md §6で外部target.cmake
#  の骨格として明示されている構成）。
#

set(CHIPDIR ${CMAKE_CURRENT_LIST_DIR})

list(APPEND ASP3_INCLUDE_DIRS
    ${CHIPDIR}
)

#
#  ESP32-C5コア（RV32IMAC＋Zicsr/Zifencei．FPU無し）
#
#  ------------------------------------------------------------------
#  ISA／ABI の指定（ESP-IDF v5.5.4 標準に整合させたもの．実測に基づく）
#  ------------------------------------------------------------------
#
#  【ESP-IDF が esp32c5 に渡す実測値】
#  esp-idf submodule（真の v5.5.4 タグ 735507283d）で hello_world を
#  実際に configure し，build/toolchain/cflags（IDFはレスポンスファイル
#  経由で渡すため compile_commands.json に -march は現れない）を読んだ：
#      -march=rv32imac_zicsr_zifencei
#  -mabi は **渡していない**（riscv32-esp-elf の既定が ilp32 のため．
#  IDFはFPUを持つターゲットでのみ -mabi=ilp32f を明示する）。
#  出典：esp-idf/components/soc/project_include.cmake
#      C2/C3        → rv32imc_zicsr_zifencei
#      C5/C6/C61/H2 → rv32imac_zicsr_zifencei   ← C5はA拡張を持つ
#  esp-idf/tools/cmake/toolchain-clang-esp32c5.cmake も同じ
#  rv32imac_zicsr_zifencei / ilp32 を与えており，GCC版と一致する。
#  soc_caps.h に SOC_CPU_HAS_FPU/HWLOOP/PIE は無く，march の追加接尾辞
#  （_xesploop/_xespv）やilp32fは付かない＝上記が最終値。
#
#  【★旧コメントの訂正（実測）】
#  ここには「ツールチェーンのマルチリブはrv32imcを持たないため
#  rv32im/ilp32がリンクされる」と書かれていたが，これは
#  **汎用ツールチェーンを使っていたことに由来する記述**だった：
#      /usr/bin/riscv64-unknown-elf-gcc 13.2.0（Ubuntu汎用）
#        -march=rv32imc_zicsr_zifencei -print-multi-directory
#          -> rv32im/ilp32          ← 旧コメントの言うとおり（C拡張なし）
#      riscv32-esp-elf-gcc esp-14.2.0_20260121（IDF v5.5.4 指定版）
#        -march=rv32imc_zicsr_zifencei  -> rv32imc_zicsr_zifencei/ilp32
#        -march=rv32imac_zicsr_zifencei -> rv32imac_zicsr_zifencei/ilp32
#  ＝Espressif版はどちらのマルチリブも実在し，指定ISAと一致するものが
#  選ばれる。旧コメントの前提（マルチリブ不足）は正しいコンパイラでは
#  消滅する。フラグ設計が «間違ったコンパイラ» に合わせて形作られていた
#  ことの記録として，訂正の上で経緯を残す。
#
#  【blobが要求するISA（実測）】
#  riscv32-esp-elf-readelf -A で C5 blob の Tag_RISCV_arch を読むと，
#  libpp/libcore/libnet80211/libble_app/libcoexist はいずれも
#      rv32i2p0_m2p0_c2p0   ＝ rv32imc（**A拡張なし**）
#  libphy は .riscv.attributes セクション自体を持たない（ISA制約なし）。
#  さらに全blobを逆アセンブルして lr.w/sc.w/amo* を数えると **0 個**＝
#  blobはA拡張を要求も使用もしない。よってASP3側を rv32imac にしても
#  blobとの整合は崩れない（blobはISAの部分集合＝リンカは属性を和集合に
#  マージする）。逆に «blobがrv32imcだからASP3もrv32imcでなければ
#  ならない» ということもない。
#
#  ESP32C5_IDF_STD_ISA=ON（既定）… IDF標準 rv32imac_zicsr_zifencei
#  ESP32C5_IDF_STD_ISA=OFF        … 移行前の rv32imc_zicsr_zifencei
#     （A/B比較・不具合時の完全な復帰用．挙動を旧に戻す）
#
option(ESP32C5_IDF_STD_ISA
    "Use the ISA that ESP-IDF v5.5.4 specifies for esp32c5 (rv32imac_zicsr_zifencei, measured from esp-idf/components/soc/project_include.cmake). OFF reverts to the pre-migration rv32imc_zicsr_zifencei."
    ON)

if(ESP32C5_IDF_STD_ISA)
    set(ESP32C5_MARCH rv32imac_zicsr_zifencei)
else()
    set(ESP32C5_MARCH rv32imc_zicsr_zifencei)
endif()

#
#  【ASP3固有＝IDFと意図的に異なる項目】
#  以下はIDFが渡していないが，ASP3側の要求として維持する：
#    -mcmodel=medany  … IDFは指定せず riscv32-esp-elf 既定の medlow。
#        ASP3はRP2350/PolarFire等と同じくmedanyで統一している。
#        medlow/medanyはリンク互換（ABI差ではなくコード生成の差）。
#    -fsigned-char    … RISC-Vの既定は unsigned char（実測：
#        __CHAR_UNSIGNED__ が定義される）。IDFは -fsigned-char を
#        渡さない＝IDFはunsigned charでビルドされる。ASP3/TOPPERSは
#        signed char前提のコードを持つためこちらを維持する。
#        （言語意味論の差であり，呼出規約＝ABIの差ではない。）
#  以下はIDFが渡していないが，riscv32-esp-elf の **既定と同値** で
#  あることを実測済み（-Q --help=target で確認）＝明示しても差は無い：
#    -mabi=ilp32（既定 ilp32）／-msmall-data-limit=8（既定 8）／
#    -mstrict-align（既定 enabled）／-mno-save-restore（既定 disabled）
#
list(APPEND ASP3_COMPILE_OPTIONS
    -march=${ESP32C5_MARCH}
    -mabi=ilp32
    -mcmodel=medany
    -msmall-data-limit=8
    -mstrict-align
    -mno-save-restore
    -fsigned-char
    -ffunction-sections
)

list(APPEND ASP3_LINK_OPTIONS
    -march=${ESP32C5_MARCH}
    -mabi=ilp32
)

list(APPEND ASP3_ARCH_C_FILES
    ${CHIPDIR}/chip_kernel_impl.c
    ${CHIPDIR}/chip_support.S
)

#
#  非TECS版SIOドライバ（コンソール実体の選択）
#
#  ESP32C5_CONSOLE=uart0（既定）… UART0（QEMU・UARTブリッジ付きボード）
#  ESP32C5_CONSOLE=usbjtag      … USB Serial/JTAGコントローラ
#                                 （UARTブリッジを持たないネイティブUSB
#                                 ボード．/dev/ttyACM*がそのままコンソール）
#
if(NOT DEFINED ESP32C5_CONSOLE)
    set(ESP32C5_CONSOLE uart0)
endif()
if(ESP32C5_CONSOLE STREQUAL "usbjtag")
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_CONSOLE_USBJTAG)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${CHIPDIR}/chip_serial.c
        ${CHIPDIR}/esp32c5_usbjtag.c
    )
else()
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${CHIPDIR}/chip_serial.c
        ${CHIPDIR}/esp32c5_uart.c
    )
endif()

#
#  CLIC・Machine Timerは使用しない（割込みコントローラは標準RISC-V
#  CLIC＝clic_kernel_impl.h，高分解能タイマはSYSTIMER（ターゲット依存部）
#  を使用）。マクロ名はC6から流用（riscv_gcc/common/arch.cmakeが
#  common部のPLIC/Machine Timerコードを省く際に見るフラグで，CLICの
#  略称ではない．紛らわしいが変更するとcommon側の分岐に影響するため
#  そのまま維持する）
#
set(ASP3_RISCV_OMIT_PLIC_MTIMER ON)

#
#  コア依存部のインクルード
#
include(${ASP3_ROOT_DIR}/arch/riscv_gcc/common/arch.cmake)

#
#  ベアメタルリンクのため libc は使用しない（PolarFire・RP2350と同様）．
#  コンパイラが生成する memcpy/memset 等は libc_stub.c（ISA非依存・
#  PolarFire依存部からパス参照．submodule側の共有ファイルであり意図的）
#  が提供する．
#
list(REMOVE_ITEM ASP3_LINK_LIBS c)
list(APPEND ASP3_ARCH_C_FILES
    ${ASP3_ROOT_DIR}/arch/riscv_gcc/polarfire_soc/libc_stub.c
)

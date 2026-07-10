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
#  ESP32-C5コア（RV32IMC＋Zicsr/Zifencei．A拡張・FPU無し）
#
#  ツールチェーンのマルチリブはrv32imcを持たないためrv32im/ilp32が
#  リンクされる（ABI互換・C拡張の有無はコードサイズのみの差）。
#
#  【実機確認待ち】docs/c5-port-design.md §4「chip.cmake」の項。
#  C5もC6と同じRV32IMC＋Zicsr/Zifencei前提で組んでいるが，CLIC拡張が
#  要求する追加命令セット（もしあれば）の要否は未確認。ビルドが通れば
#  少なくともツールチェーン側の要求は満たしている（本フェーズの完了
#  条件）が，実機での動作は別途確認が必要。
#
list(APPEND ASP3_COMPILE_OPTIONS
    -march=rv32imc_zicsr_zifencei
    -mabi=ilp32
    -mcmodel=medany
    -msmall-data-limit=8
    -mstrict-align
    -mno-save-restore
    -fsigned-char
    -ffunction-sections
)

list(APPEND ASP3_LINK_OPTIONS
    -march=rv32imc_zicsr_zifencei
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

#
#		ESP RISC-V ツールチェーン検証（「黙って汎用GCCへ落ちる」の検出）
#
#  project() の後に target.cmake から include する．
#  toolchain-esp32-riscv32.cmake を使わずに configure された場合
#  （＝asp3_core の toolchain-riscv64.cmake を素で渡した場合）でも，
#  ここで **実際に選ばれたコンパイラを実測して** 期待と違えば止める．
#
#  【なぜ toolchain ファイルだけでは不十分か】
#  toolchain ファイルは «こちらが指定した場合» にしか効かない．
#  事故の実体は «指定し忘れ» なので，指定し忘れた経路（＝asp3_core の
#  toolchain-riscv64.cmake ＋ -DRISCV64_TOOLCHAIN_PREFIX 忘れ）でこそ
#  発火する必要がある．toolchain ファイルの外側＝target.cmake から
#  呼ぶのはこのため．
#
#  【配置理由＝C3/C6 への転写可能性】
#  本ファイルは asp3/cmake/ にあり **チップ非依存**．
#  chip.cmake は C3/C6 では asp3_core（submodule．編集禁止）にあるが，
#  target.cmake は C3/C5/C6 とも **本リポジトリ** にある．
#  よって「target.cmake から include する」形にしておけば，C3/C6 へは
#  submodule を触らずに1行の追加だけで転写できる．
#  （`asp3/target/esp32c3_espidf/hal_stub`・`net/` を C3_TARGETDIR 経由で
#    3チップが共有している既存の前例と同型．新しい規約は導入しない．）
#
#  【使い方】target.cmake から：
#      set(ASP3_ESP_EXPECTED_TOOLCHAIN esp-14.2.0_20260121)
#      include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/esp_toolchain_check.cmake)
#
#  【逃げ道】-DASP3_ESP_TOOLCHAIN_CHECK=OFF で検証を無効化できる
#  （挙動は一切変えない．検証を切るだけ）．
#

option(ASP3_ESP_TOOLCHAIN_CHECK
    "Verify that the C compiler is the Espressif riscv32-esp-elf toolchain version that ESP-IDF specifies. OFF disables the check only (it never changes flags)."
    ON)

#  期待版．include する側が事前に set していればそれを使う．
if(NOT DEFINED ASP3_ESP_EXPECTED_TOOLCHAIN)
    set(ASP3_ESP_EXPECTED_TOOLCHAIN esp-14.2.0_20260121)
endif()

if(ASP3_ESP_TOOLCHAIN_CHECK)
    #
    #  (1) ターゲット三つ組を実測する．
    #      汎用GCCは riscv64-unknown-elf を返すので，これだけで
    #      「/usr/bin へ落ちた」164構成型の事故は確実に捕まる．
    #
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -dumpmachine
        OUTPUT_VARIABLE _asp3_esp_machine
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    #
    #  (2) crosstool-NG の版タグを実測する．
    #      例：riscv32-esp-elf-gcc (crosstool-NG esp-14.2.0_20260121) 14.2.0
    #      CMAKE_C_COMPILER_VERSION（14.2.0）だけでは
    #      esp-14.2.0_20241119 と esp-14.2.0_20260121 を区別できない．
    #
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} --version
        OUTPUT_VARIABLE _asp3_esp_verstr
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(REGEX MATCH "esp-[0-9]+\\.[0-9]+\\.[0-9]+_[0-9]+" _asp3_esp_tag "${_asp3_esp_verstr}")

    if(NOT _asp3_esp_machine STREQUAL "riscv32-esp-elf")
        message(FATAL_ERROR
            "Wrong C compiler: this is NOT an Espressif riscv32-esp-elf toolchain.\n"
            "\n"
            "  compiler     : ${CMAKE_C_COMPILER}\n"
            "  -dumpmachine : ${_asp3_esp_machine}\n"
            "  expected     : riscv32-esp-elf\n"
            "\n"
            "This is the classic silent failure: asp3_core's cmake/toolchain-riscv64.cmake\n"
            "defaults to the prefix 'riscv64-unknown-elf-' and resolves it through PATH, so a\n"
            "missing -DRISCV64_TOOLCHAIN_PREFIX silently selects Ubuntu's generic GCC. It has\n"
            "rv32 multilibs, so the build SUCCEEDS and the mismatch goes unnoticed -- but the\n"
            "generic toolchain has no rv32imc multilib (measured: -print-multi-directory gives\n"
            "rv32im/ilp32), so libgcc is linked WITHOUT the C extension.\n"
            "\n"
            "How to fix (recommended first):\n"
            "  Use this repository's toolchain file instead of asp3_core's:\n"
            "    -DCMAKE_TOOLCHAIN_FILE=<repo>/asp3/cmake/toolchain-esp32-riscv32.cmake\n"
            "  (it pins the compiler by absolute path and does not consult PATH)\n"
            "\n"
            "Alternative (keeps asp3_core's toolchain file, still PATH-dependent):\n"
            "    -DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-\n"
            "  with <toolchain>/riscv32-esp-elf/bin on PATH.\n"
            "\n"
            "To disable this check (NOT recommended): -DASP3_ESP_TOOLCHAIN_CHECK=OFF")
    endif()

    if(NOT _asp3_esp_tag STREQUAL "${ASP3_ESP_EXPECTED_TOOLCHAIN}")
        message(FATAL_ERROR
            "Wrong Espressif toolchain version.\n"
            "\n"
            "  compiler : ${CMAKE_C_COMPILER}\n"
            "  found    : ${_asp3_esp_tag}\n"
            "  expected : ${ASP3_ESP_EXPECTED_TOOLCHAIN}\n"
            "\n"
            "${ASP3_ESP_EXPECTED_TOOLCHAIN} is the version declared by the esp-idf submodule\n"
            "(true v5.5.4 tag) in <repo>/esp-idf/tools/tools.json (tool: riscv32-esp-elf).\n"
            "The sources and the prebuilt blobs in this build come from that submodule, so the\n"
            "compiler should come from it too.\n"
            "\n"
            "How to fix (choose one):\n"
            "  1. Install the expected version:\n"
            "       cd <repo>/esp-idf && ./install.sh esp32c5\n"
            "  2. Select an already-installed one explicitly:\n"
            "       -DCMAKE_TOOLCHAIN_FILE=<repo>/asp3/cmake/toolchain-esp32-riscv32.cmake \\\n"
            "       -DESP_TOOLCHAIN_VERSION=${_asp3_esp_tag}\n"
            "  3. Accept a different version for this target on purpose:\n"
            "       -DASP3_ESP_EXPECTED_TOOLCHAIN=${_asp3_esp_tag}\n"
            "\n"
            "To disable this check entirely: -DASP3_ESP_TOOLCHAIN_CHECK=OFF")
    endif()

    message(STATUS
        "ESP toolchain OK: ${_asp3_esp_tag} (${_asp3_esp_machine}) -- ${CMAKE_C_COMPILER}")
endif()

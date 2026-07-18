#
#		ツールチェーンファイル（ESP32 RISC-V：riscv32-esp-elf）
#
#  asp3_core の cmake/toolchain-riscv64.cmake と同じ役割・同じ書式だが，
#  以下の2点が異なる：
#
#   (1) コンパイラを **絶対パス** で指定する（PATH に依存しない）．
#   (2) ESP-IDF が指定する版（tools.json の riscv32-esp-elf）を既定にする．
#
#  【なぜ必要か＝実測に基づく】
#  asp3_core の toolchain-riscv64.cmake は
#      set(CMAKE_C_COMPILER ${RISCV64_TOOLCHAIN_PREFIX}gcc)
#  で **プレフィクス名だけ** を与え，既定プレフィクスが
#  riscv64-unknown-elf- である．解決は PATH 任せなので，
#  -DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf- を渡し忘れると
#  **黙って /usr/bin/riscv64-unknown-elf-gcc（Ubuntu汎用GCC）へ落ちる**．
#  rv32 マルチリブを持つためビルドは通ってしまい，誰も気づかない．
#  実測（本リポジトリの build/ 配下 320 構成の CMakeCache.txt）：
#      164 構成 = /usr/bin（汎用GCC 13.2.0．Espressif版ですらない）
#       84 構成 = esp-15.2.0_20251204（ESP-IDF v6.1 の指定版）
#       72 構成 = esp-14.2.0_20241119（ESP-IDF v5.5.0 の指定版）
#        0 構成 = esp-14.2.0_20260121（**真の v5.5.4 の指定版**）
#  ＝供給（ソース・blob）を真の v5.5.4 へ統一したのに，コンパイラだけ
#  一度も揃っていなかった．本ファイルはこの事故の再発を構成側で封じる．
#
#  さらに汎用GCCは害が具体的である（実測）：
#      /usr/bin/riscv64-unknown-elf-gcc -march=rv32imc_zicsr_zifencei
#        -print-multi-directory  ->  rv32im/ilp32   ← **C拡張を持たない**
#  ＝汎用ツールチェーンのマルチリブに rv32imc が無いため libgcc は
#  rv32im 版が選ばれる．riscv32-esp-elf では
#      rv32imc_zicsr_zifencei/ilp32 ／ rv32imac_zicsr_zifencei/ilp32
#  が実在し，指定した ISA と一致するマルチリブが選ばれる．
#
#  【使い方】
#    cmake -S asp3/asp3_core -B build/xxx -G Ninja \
#      -DCMAKE_TOOLCHAIN_FILE=<repo>/asp3/cmake/toolchain-esp32-riscv32.cmake \
#      -DASP3_TARGET=esp32c5_espidf ...
#  （asp3_core の toolchain-riscv64.cmake の代わりに指定する．
#    RISCV64_TOOLCHAIN_PREFIX は不要になる．）
#
#  【上書き】
#    -DESP_TOOLCHAIN_VERSION=esp-15.2.0_20251204   … 版を変える
#    -DESP_TOOLCHAIN_ROOT=<dir>                    … 導入先を変える
#    -DESP_TOOLCHAIN_PREFIX=<...>/riscv32-esp-elf- … 完全に手で与える
#
#  【共有範囲】ESP32-C3／C5／C6 で共通に使える（チップ依存の値を一切
#  持たない）．-march/-mabi 等のチップ固有 ISA 指定は各チップの
#  chip.cmake の責務であり，本ファイルには置かない．
#

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

#
#  既定＝esp-idf submodule（真の v5.5.4 タグ）の tools.json が指定する版．
#  実測：esp-idf/tools/tools.json の riscv32-esp-elf → esp-14.2.0_20260121
#
if(NOT DEFINED ESP_TOOLCHAIN_VERSION)
    set(ESP_TOOLCHAIN_VERSION esp-14.2.0_20260121)
endif()

#
#  Espressif の標準導入先．
#
#  ★導入先は $HOME 決め打ちではない．**IDF_TOOLS_PATH を尊重する**．
#  実測（esp-idf submodule の tools/idf_tools.py．推測ではない）：
#      :3576  g.idf_tools_path = os.environ.get('IDF_TOOLS_PATH') \
#                                or os.path.expanduser(IDF_TOOLS_PATH_DEFAULT)
#      :96    IDF_TOOLS_PATH_DEFAULT = os.path.join('~', '.espressif')
#      :919   get_path()             -> join(g.idf_tools_path, 'tools', <tool名>)
#      :925   get_path_for_version() -> join(get_path(), <version>)
#  ＝ 実体は  <IDF_TOOLS_PATH>/tools/riscv32-esp-elf/<version>/riscv32-esp-elf/bin/
#  （このPCの実体で確認：~/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/
#    riscv32-esp-elf/bin/riscv32-esp-elf-gcc）．
#
#  ★これを見ないと «案内が嘘になる»：IDF_TOOLS_PATH を別の場所に設定した
#  CI／開発機では，下の FATAL が案内する `./install.sh` は **成功する** のに
#  cmake は $HOME を見て «見つからない» と言い続ける＝直しようが無くなる．
#
#  Python の `or` に合わせ **空文字列は «未設定» 扱い** にする（実測どおり：
#  `os.environ.get(...) or ...` は空文字列で既定へ落ちる）．DEFINED ENV{} だと
#  空文字列を «設定済み» と読んでしまい，挙動が idf_tools.py と食い違う．
#
if(NOT "$ENV{IDF_TOOLS_PATH}" STREQUAL "")
    set(_esp_tools_base "$ENV{IDF_TOOLS_PATH}")
    set(_esp_tools_base_origin "(from the IDF_TOOLS_PATH environment variable)")
else()
    set(_esp_tools_base "$ENV{HOME}/.espressif")
    set(_esp_tools_base_origin "(not set; using the idf_tools.py default ~/.espressif)")
endif()

if(NOT DEFINED ESP_TOOLCHAIN_ROOT)
    set(ESP_TOOLCHAIN_ROOT ${_esp_tools_base}/tools/riscv32-esp-elf)
else()
    #  -DESP_TOOLCHAIN_ROOT が明示された場合，導入先の由来は無関係になる
    #  （下の FATAL で «IDF_TOOLS_PATH を直せ» と誤誘導しないため明示する）．
    set(_esp_tools_base_origin "(overridden by -DESP_TOOLCHAIN_ROOT; not used)")
endif()

#  プレフィクス（絶対パス）．全体を手で与えることもできる．
if(NOT DEFINED ESP_TOOLCHAIN_PREFIX)
    set(ESP_TOOLCHAIN_PREFIX
        ${ESP_TOOLCHAIN_ROOT}/${ESP_TOOLCHAIN_VERSION}/riscv32-esp-elf/bin/riscv32-esp-elf-)
endif()

#
#  存在しない版を «黙って PATH へ落とさず» ここで止める．
#  （PATH 依存をやめるのが本ファイルの目的なので，見つからない場合に
#    フォールバックしてはならない．）
#
if(NOT EXISTS ${ESP_TOOLCHAIN_PREFIX}gcc)
    message(FATAL_ERROR
        "ESP RISC-V toolchain not found:\n"
        "    ${ESP_TOOLCHAIN_PREFIX}gcc\n"
        "\n"
        "Expected version : ${ESP_TOOLCHAIN_VERSION}\n"
        "Searched under   : ${ESP_TOOLCHAIN_ROOT}\n"
        "IDF_TOOLS_PATH   : ${_esp_tools_base} ${_esp_tools_base_origin}\n"
        "\n"
        "This is the version required by the esp-idf submodule (true v5.5.4 tag);\n"
        "it is declared in <repo>/esp-idf/tools/tools.json (riscv32-esp-elf).\n"
        "\n"
        "How to fix (choose one):\n"
        "  1. Install it with the esp-idf submodule's own installer:\n"
        "       cd <repo>/esp-idf && ./install.sh esp32c5\n"
        "  2. Point at an existing install directory:\n"
        "       -DESP_TOOLCHAIN_ROOT=<dir containing ${ESP_TOOLCHAIN_VERSION}/>\n"
        "  3. Give the full prefix explicitly:\n"
        "       -DESP_TOOLCHAIN_PREFIX=<dir>/bin/riscv32-esp-elf-\n"
        "  4. Use a different installed version (NOT the IDF v5.5.4 standard):\n"
        "       -DESP_TOOLCHAIN_VERSION=esp-15.2.0_20251204\n")
endif()

set(CMAKE_C_COMPILER ${ESP_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${ESP_TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${ESP_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_OBJCOPY ${ESP_TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_NM ${ESP_TOOLCHAIN_PREFIX}nm)

#  try_compileはリンク不要のスタティックライブラリで行う
#  （toolchain-riscv64.cmake と同じ．ベアメタルでリンクが通らないため．）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

#  実行ファイルの拡張子（toolchain-riscv64.cmake と同じ）
set(CMAKE_EXECUTABLE_SUFFIX_C ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM ".elf")

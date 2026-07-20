#
#		esptool の解決（ESP32-C3／C5／C6 共有）
#
#  【なぜ必要か＝実測に基づく】
#  従来は各 target.cmake が
#      set(ESP32CN_ESPTOOL esptool CACHE STRING "Path to esptool")
#  と **裸のコマンド名** を既定にしていた。解決は PATH 任せなので、
#  esptool が PATH に無い環境では次が壊れる：
#    - `cmake --build <dir> --target run`（write-flash）が実行時に失敗
#    - ★C5 の seam 構成（`-DASP3_SEAM_BOOT=ON`）は **POST_BUILD** で
#      `esptool elf2image` を呼ぶため **ビルドそのものが失敗**する
#      （実測：`/bin/sh: 1: esptool: not found`）
#
#  ★Direct Boot 経路が無事だったのは `${CMAKE_OBJCOPY}`（toolchain ファイルが
#  絶対パスで解決する）を使うためで、**PATH に依存していたのは esptool だけ**
#  だった＝「片方だけ動くので気づかれない」型。
#
#  本ファイルは ESP-IDF の python venv（`~/.espressif/python_env/*/bin`）を
#  探索して **絶対パス** を既定にする。見つからなければ従来どおり裸の名前を
#  残す（PATH に置いている環境の挙動を変えないため＝非回帰）。
#
#  上書き：`-DESP32CN_ESPTOOL=/path/to/esptool`（find_program はキャッシュに
#  既に値があれば何もしないので、明示指定が常に勝つ）。
#

function(asp3_find_esptool _var)
    #  ESP-IDF が作る python venv を探索候補にする（版名は環境依存なので glob）。
    file(GLOB _asp3_esptool_hints
        $ENV{HOME}/.espressif/python_env/*/bin
        $ENV{IDF_TOOLS_PATH}/python_env/*/bin
    )
    #  find_program は「キャッシュに値があれば何もしない」＝-D 指定が勝つ。
    find_program(${_var}
        NAMES esptool esptool.py
        HINTS ${_asp3_esptool_hints}
        DOC "Path to esptool (auto-detected from the ESP-IDF python env; override with -D)"
    )
    if(NOT ${_var})
        #  見つからない場合は従来の既定（裸の名前＝PATH 解決）を残す。
        #  PATH に置いている環境ではこれで従来どおり動く。
        set(${_var} esptool
            CACHE STRING "Path to esptool (not auto-detected; resolved from PATH at build/run time)")
    endif()
endfunction()

#
#  seam のように **ビルド中に** esptool を必要とする構成で、実体が無いまま
#  進むと `/bin/sh: esptool: not found` という分かりにくいビルドエラーになる。
#  configure 時に明示的に止め、対処方法を案内する。
#
function(asp3_require_esptool _var _why)
    if(NOT EXISTS ${${_var}})
        message(FATAL_ERROR
            "${_why} requires a real esptool executable, but ${_var}='${${_var}}' "
            "was not found (it is not an existing path, and auto-detection from "
            "$ENV{HOME}/.espressif/python_env/*/bin failed).\n"
            "\n"
            "This configuration runs esptool during the BUILD (not only at flash "
            "time), so the build would fail late with a bare "
            "'/bin/sh: esptool: not found'.\n"
            "\n"
            "Fix by either:\n"
            "  1. Passing the path explicitly:\n"
            "       cmake <build dir> -D${_var}=/path/to/esptool\n"
            "  2. Or putting esptool on PATH (e.g. source the ESP-IDF export script).")
    endif()
endfunction()

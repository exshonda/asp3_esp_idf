#
#		既存 build dir が «古い既定» のまま黙って動くのを検出する
#
#  target.cmake から option(ASP3_ESPIDF_SUPPLY ...) の **直後** に呼ぶ：
#      asp3_warn_if_cache_overrides_default(
#          ASP3_ESPIDF_SUPPLY ${_asp3_espidf_supply_default} "<追加の案内>")
#
#  【なぜ必要か＝実測に基づく】
#  cmake の option()／set(... CACHE ...) は **既存のキャッシュ実体を上書き
#  しない**．よって «既定を変更しても，既に configure 済みの build dir には
#  一切届かない»．その dir は古い既定のまま **黙って** 動き続ける．
#
#  実測（本チェック導入前．指標＝build.ninja 中の生パス出現数．
#  両方向で較正済み：esp-idf 供給の既知 dir で hal=0/esp-idf=6827，
#  意図的 hal dir（c3_ble_hal）で hal=9216/esp-idf=0）：
#      build/c3_dflt_ble（＝名前どおり «C3 の既定» BLE ビルド）
#          CMakeCache: ASP3_ESPIDF_SUPPLY:BOOL=OFF（型 BOOL・-D 指定の痕跡なし
#                      ＝ «明示指定» ではなく «当時の既定» が入った証拠）
#          CMakeCache: 旧 docstring（"Default OFF for ESP32C3_BT=ON ..."）を保持
#                      ＝既定変更後に一度も再 configure されていない証拠
#          build.ninja: hal=9216 / esp-idf=0 ＝ **実際に hal で建っている**
#  ＝「既定は esp-idf 供給（ON）」に変えた後も，この dir は hal のまま．
#  ★これは «ビルドが，あなたが思っているものと違うものを黙って使う» 型
#    （build/ 321 dir 中 164 が汎用 GCC を使っていた事故と同じ構造）．
#
#  【WARNING に留める理由】
#  FATAL にすると «今まで通っていた既存ビルドが落ちる»＝挙動変更になる．
#  意図的な hal fallback（-DASP3_ESPIDF_SUPPLY=OFF）は正当な構成なので，
#  «違いを告げる» までが本チェックの責務．採否はユーザー判断．
#
#  【案内は «実際に効く» ものだけ書く（実測）】
#  ★素朴な «-DASP3_ESPIDF_SUPPLY=ON を渡せ» は **C3 BT では効かない**：
#    ASP3_BT_IDF_V554 の既定は ASP3_ESPIDF_SUPPLY に追従する設計だが，
#    **それ自体もキャッシュ実体**なので古い値（OFF）に据え置かれ，
#    esp_bt.cmake の «混成禁止» FATAL に当たる．実測（stale specimen）：
#        -DASP3_ESPIDF_SUPPLY=ON のみ            -> rc=1
#            （esp_bt.cmake:142 "ASP3_ESPIDF_SUPPLY=ON with
#              ASP3_BT_IDF_V554=OFF mixes an esp-idf base with ..."）
#        -DASP3_ESPIDF_SUPPLY=ON -DASP3_BT_IDF_V554=ON -> rc=0
#  ＝ «追従する既定を持つオプションは，それ自身も stale» という一般則．
#  だから本ファイルは «build dir を消す» を第一候補に案内する（追従オプションが
#  何個あっても必ず正しい唯一の手）．-D 経路は実測済みの但し書き付きで併記する．
#  （このリポジトリでは «存在しない/効かない退避先を案内する» 事故が既に3件
#    起きている．「案内先が存在する」と「案内に従うと直る」は別物である．）
#

function(asp3_warn_if_cache_overrides_default _var _computed_default)
    #  ON/OFF/1/0/TRUE 等の表記ゆれを吸収して真偽で比較する
    #  （文字列比較だと "ON" vs "1" を誤って «食い違い» と読む）．
    if(${_var})
        set(_cur ON)
    else()
        set(_cur OFF)
    endif()
    if(${_computed_default})
        set(_def ON)
    else()
        set(_def OFF)
    endif()

    if(_cur STREQUAL _def)
        return()
    endif()

    set(_hint "")
    if(ARGC GREATER 2)
        set(_hint "${ARGV2}")
    endif()

    message(WARNING
        "${_var}=${_cur} in this build directory, but the default computed by the "
        "current sources is ${_def}.\n"
        "\n"
        "cmake's option() never overwrites an existing CMakeCache entry, so a default "
        "that changed AFTER this directory was first configured does not reach it: this "
        "build keeps using ${_var}=${_cur} silently.\n"
        "\n"
        "If that is intentional (e.g. the reversible hal fallback), ignore this warning.\n"
        "\n"
        "To adopt the current default (${_def}):\n"
        "  1. Delete this build directory and re-configure.\n"
        "     <-- always correct; prefer this if unsure.\n"
        "  2. Or re-configure passing the value explicitly:\n"
        "       cmake <build dir> -D${_var}=${_def}\n"
        "     CAUTION (measured): other options whose DEFAULT follows ${_var} are cached "
        "too and are therefore ALSO stale -- passing ${_var} alone can leave them at the "
        "old value and stop the configure.\n"
        "${_hint}")
endfunction()

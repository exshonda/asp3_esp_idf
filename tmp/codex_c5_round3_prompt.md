非対話（一回きり）の相談です。コードの編集や実装は不要——**分析と次の一手の提案だけ**をお願いします。
回答は日本語で。根拠となるESP-IDF/blob/ROM/HALの該当箇所は `file:line` や関数名・ROMシンボル名で示してください。

このリポジトリは `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf`（branch `claude/c6-wifi-c5-dev-5vc6x9`）。
過去の相談＝`tmp/codex_c5_round1_prompt.md`/`_output.txt`（実施13時点）、`tmp/codex_c5_round2_prompt.md`/`_output.txt`（実施26時点）。
その後の全経緯は `docs/c5-bringup.md` の実施27〜36。ESP-IDF v6.1-beta1は`/home/honda/tools/esp-idf-v6.1`、HALは`hal/`。

# 現状（round2以降の激変——実施27〜35で確定した事実）

TOPPERS/ASP3（Direct Boot）のESP32-C5で、以下を**すべて解決済み**：
1. CLIC割込み出口バグ（mret非経由出口でmintstatus.mil固着→全割込み凍結）→synthetic mretで根治（実施28）
2. Super WDTアンロックキー誤り→修正（実施33）
3. **CPUルートクロックがXTAL(48MHz)のまま**だった→bootloader相当の切替を移植（実施32）→PHY較正（トーン自己ループバック）が完走するようになった
4. **ROMのBBPLLが「XTAL=40MHzプロファイル」で較正されていた**（実XTAL=48MHz→480×1.2=576MHzで誤ロック。OC_DIV=12/DR1=DR3=0を実測）→48MHzプロファイルで再較正（実施34）→CPU実測80.00/240.00MHz（理論一致）
5. regi2cマスタ(I2C_ANA_MST)自体のICGゲート→解除（実施34）

現在の状態：**冷間Direct Bootで較正完走・esp_wifi_init完走・scan完走（約11秒・WDTリセット無し）・CPU=240MHz（ESP-IDF標準）・BBPLL=正確な480MHz——しかしAP数=0（deaf）**。
一方、**同一のASP3バイナリ**を「stockのapp_main先頭で即ハンドオフ」（stock標準ブート列のみ実行、WiFi/PHY/NVS未実行）で起動すると**AP 20-25検出**（実施29、再現多数）。

差分探索の消尽状況：
- 同一ソフトウェア（冷間 vs ハンドオフ後）で広域11ブロックMMIO差分→19語まで縮小→全て既知の非因果カテゴリに帰属（実施35）。esp_clk_init/esp_clk_tree_initializeの静的効果4候補（RTC_FAST源・RTC_SLOW較正値LP_AON_STORE1・PMU HP_MODEM/HP_SLEEPバンク）は全て移植・JTAG適用確認の上で因果棄却。
- 実施36（実行中）：同じ同一ソフトウェア差分をregi2c全block（未公開0x63/0x68/0x6b含む）へ拡張中。
- 実施30の二分探索：RXキーは「stock appの`esp_clk_tree_initialize()`+`esp_clk_init()`実行」を境に確立される（P1=前:RX死/P2=後:RX生存）——だが上記のとおり静的効果は全部移植しても冷間は0APのまま。

# 相談したいこと（仮説の評価と判別設計）

## H1（本命候補）：「XTAL周波数の信念」の残存
実施34の発見はROMが**XTAL=40MHzと信じて**BBPLLを較正していたことを意味する。C5は40/48MHz両対応でXTAL周波数はランタイム値（`rtc_clk_xtal_freq_get()`、`esp_hw_support/port/esp32c5/rtc_clk.c:505`）。
**PHY/WiFi blobはRFシンセサイザのチャンネルLO周波数計算・TSF/タイミング換算にXTAL周波数を使うはず**——blobが参照する「XTAL周波数の信念」が冷間ASP3では40MHzのまま（stockブート列は48MHzへ更新済み）なら、LOが約20%ずれて全チャンネルでdeaf（TXも周波数外＝内部的には正常動作に見える）という現症状を完全に説明できる。
質問：
- C5でXTAL周波数の「信念」はどこに保存されるか（レジスタ？LP_AON/RTC store？ROM .dataのグローバル？`clk_ll_xtal_load_freq_mhz`系？）。`rtc_clk_xtal_freq_get`/`_update`の実体とブート列での更新箇所を`file:line`で。
- **PHY blobがXTAL周波数をどう取得するか**（`phy_get_xtal_freq`類のシンボル、ROM関数`ets_get_xtal_freq`、g_phyFuns経由等）。libphy.aのundefinedシンボルから特定できるか。
- 冷間ASP3でこの値が40のままかを確認する最小のJTAG読み方。

## H2（一般化）：ROM .data/.bssグローバルの系譜依存
C6調査で実証済みの前例がある：ROMの`s_ticks_per_us`をASP3が更新し忘れ、blob内の全遅延が1/3になっていた（C6実施48）。同じクラス——**stockブート列がROM/SRAMグローバルを更新し、blobがそれを消費する**——は、MMIOにもregi2cにも映らず、ハンドオフでは（stockの値がSRAMに残るので）正しく動く。現症状の不可視性と完全に整合する。
質問：`esp32c5.rom.ld`とlibphy.a/libpp.aのundefinedシンボルから、**blobが消費しstockブート列が更新するROMグローバル**の候補を列挙してほしい（xtal freq・ticks_per_us・RTC較正値・dbias系・その他）。
また、判別実験として「**冷間 vs ハンドオフ後で、ROM .data/.bss相当のSRAM領域（例：0x4085f000〜0x40860000等、正確な範囲はrom.ldから）を丸ごとダンプ・diff**」は筋が良いか？（我々の同一ソフトウェア差分はMMIOブロックのみでSRAMは未比較——盲点だった可能性）

## H3：それでも残る候補
H1/H2で説明できない場合に残る候補（esp_clk_init内の過渡効果等）があれば。

## 批判
実施30の「RXキー=esp_clk_tree_initialize+esp_clk_init」と実施35の「その静的効果は全部移植しても駄目」の間の矛盾の解き方——H1/H2なら「これらの関数がグローバル/信念を更新する」ことで整合するか、それとも別の見落としがあるか。

回答は「H1の評価と具体的な保存場所・消費経路（file:line）」「H2の候補列挙とSRAM diff実験の設計」「H3」「批判と最小判別実験の優先順位」の構成で。

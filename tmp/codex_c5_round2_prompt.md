非対話（一回きり）の相談です。コードの編集や実装は不要——**分析と次の一手の提案だけ**をお願いします。
回答は日本語で。根拠となるESP-IDF/blob/ROM/HALの該当箇所があれば `file:line` や関数名で示してください。

このリポジトリは `$HOME/TOPPERS/ASP3CORE/asp3_esp_idf`（branch `claude/c6-wifi-c5-dev-5vc6x9`）。
前回の相談（round1、実施13時点）＝`tmp/codex_c5_round1_prompt.md`/`_output.txt`。
その後の全経緯は `docs/c5-bringup.md` の実施14〜26（各セクション見出しだけでも流れが分かるように書いてあります）。
ESP-IDF v6.1-beta1 は `$HOME/tools/esp-idf-v6.1`、HALは `hal/`（esp-hal-3rdparty submodule）。

# 前提（round1からの差分＝実施14〜26で確定した事実）

対象問題：TOPPERS/ASP3（Direct Boot、ESP-IDFブートローダ/FreeRTOS不使用）上のESP32-C5で、
Wi-Fi blob のPHY較正内トーン自己ループバック測定の生ADCサンプル（`MODEM0(0x600A0000)+0x81C..0x828`）が
**ASP3でのみ恒久ハードゼロ**→txcap探索空振り→`phy_iq_est_enable_new`のdoneビット（`+0x47C` bit16）不成立で恒久ハング。
stock ESP-IDF v6.1 `examples/wifi/scan` は**同一個体（C5#1）・同一libphy.a（MD5一致）・同一init_data（256バイト一致）・
同一cal_mode（full、レジスタ実測）**で完走・27AP検出（5GHz含む）。

実施14〜25で以下を全て確認済み（詳細・生データはdocs参照）：
1. MMIO可視空間の一致：MODEM_SYSCON/MODEM_LPCON/MODEM0(0x000/0x400/0xc00域)/PCR/PMU/LP_ANA/LP_AON/LPPERI/
   LP_CLKRST/PVT_MONITOR/MODEM1(0x600AC000)/MODEM_PWR0(0x600AD000)——4-way比較（各側2ブート）で
   プラットフォーム決定的差分は全て検出→JTAG注入または加算移植で**因果棄却**（累計13件）。
2. regi2cアドレス空間の全域一致：未公開block(0x63/0x68/0x6b)含む8block×reg 0x00-0x1F×host両側を
   known-answer gate（実施16の既知凍結値0x87でルーティング実測検証）付きでスイープ——新規未説明差分ゼロ。
3. 起動時電源初期化のパリティ移植：pmu_hp_system_init系（bias/XPD_BIAS/CK_POWER/SYSCLK/regulator）、
   pmu_lp_system_init、PVT auto-dbias、ocode強制（手動regi2cで）、PD_HPWIFI force解除——全て実行を
   読み戻しで機械確認した上で症状不変。
4. regi2cトランザクション列も最初の測定失敗点まで完全一致（実施16、--wrapトレース4-way）。

★実施26（転回点）：C6実施33型クロスカーネル・ハンドオフをC5に移植。
stock（flash 0x0、本物の2nd-stageブートローダ経由、CONFIG_ESP_SYSTEM_MEMPROT=nでPMPロック解除）を
scan完走させ、**リセット無しで** MMU再マップ（mmu_hal_unmap_all+map+cache invalidate）→ASP3イメージ
（flash 0x200000）へ直接ジャンプ。結果：**ASP3カーネル起動後、esp_wifi_init前の時点で生ADCが非ゼロ・
doneビット=1（2/2試行、値は試行間で変動＝生信号）**。
＝**原因は「stockのブートが確立し、ASP3のDirect Bootが確立しない、ソフト到達可能な状態」と確定**。
ASP3ランタイム干渉説は否定。ただしASP3自身の`esp_wifi_init`は`g_wifi_osi_funcs._recursive_mutex_create`
未実装(NULL)でクラッシュ、ASP3自身の較正完走はまだ未確認（実施27で実装・確認中）。

# 相談したいこと

1. **「これまでの比較に映らない状態」の候補**：MMIO全域・regi2c全空間・電源初期化パリティが全て一致/棄却
   なのに、stockブートでは生きASP3ブートでは死ぬ「状態」として何があり得るか。特に：
   - 書込み専用/セルフクリア型レジスタで、スナップショット比較に原理的に映らないもの（modemドメインの
     リセットパルスの順序・回数、クロック切替の過渡シーケンス等）
   - ROM関数・ブートローダ・ESP-IDF起動列（bootloader_init→call_start_cpu0→app起動）の中で、
     「一回性の副作用」（アナログラッチ、eFuse→アナログ転写、PLL再ロック手順等）を持つ処理
   - C5のmodem電源ドメインのpower-up/isolation解除のシーケンス依存性（PMU_POWER_PD_*のFSMハンドオフ、
     isolation解除とクロック供給の順序等）——スナップショットは「最終値」しか映さない
   候補は具体的に file:line で（IDF v6.1/hal/ROM ldのシンボル）。我々のリポジトリのASP3側
   Direct Boot初期化は `asp3/target/esp32c5_espidf/target_kernel_impl.c` の `hardware_init_hook()` と
   `wifi/esp_wifi_adapter.c` のshim群です。
2. **ジャンプ点二分探索の設計**：実施27以降、stock側のジャンプ点を早める二分探索で「鍵の状態」が
   確立される段階を絞る予定。情報量最大の切り方（例：bootloader完了直後／`call_start_cpu0`内の
   どこか／`app_main`前／`esp_phy_enable`前）と、各点での実装上の注意（その時点で生きている
   クロック/スタック/キャッシュ状態でジャンプ機構が動くか）について助言がほしい。
   逆方向（ASP3を先に起動し、stockの特定初期化段だけを後から実行）の方が筋が良いならその設計も。
3. **証拠連鎖への批判**：実施26の解釈（「ブート時確立状態が原因」）に対する別解釈・見落としが
   あれば指摘してほしい。例えば「stockの較正完了そのものが状態を作った（ブート列ではなく較正の
   実行が鍵）」は現時点で区別できていない——これを安価に区別する実験があれば。

回答は「候補リスト（根拠付き・優先度付き）」「二分探索の推奨設計」「批判と追加実験」の3部構成で。

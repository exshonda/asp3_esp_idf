# 相談：ESP32-C5 + 独自RTOS（Direct Boot）でWi-Fi scanが「完走するのに0 AP」——ブート系譜だけで受信可否が分かれる原因の候補を求む

コードの実装は不要です。**原因候補の列挙（機構の説明付き・優先度付き）と、それぞれを判別する最小実験の設計**を回答してください。回答は日本語で。
私たちは以下に書く仮説をすべて実機で検証・棄却済みです。**同じ仮説の再提案は不要**——「まだ検証していない領域」への具体的な提案が欲しい。

参考（アクセス可能なら）：https://github.com/exshonda/asp3_esp_idf のbranch `claude/c6-wifi-c5-dev-5vc6x9`。
全経緯＝`docs/c5-bringup.md`（実施01〜37、実験記録）。過去のAI相談＝`tmp/codex_c5_round1〜3_*`。
以下は自己完結の要約なので、リポジトリを読めなくても回答可能です。

## システム構成

- **チップ**：ESP32-C5（RISC-V、Wi-Fi 6 デュアルバンド 2.4/5GHz、XTAL=48MHz実装）
- **OS**：TOPPERS/ASP3（μITRON系RTOS）を**Direct Boot**で起動（ESP-IDFの2nd-stageブートローダもFreeRTOSも不使用。ROMがflash上のアプリを直接起動）
- **Wi-Fi**：ESP-IDF v6.1-beta1のクローズドソースblob（libphy.a/libpp.a/libnet80211.a等）をos_adapterシム経由で駆動。blobバイナリ・PHY init_data（256バイト）・cal_mode（full）はstock ESP-IDFと完全同一
- **対照（陽性）**：stock ESP-IDF `examples/wifi/scan`は同一個体で27 AP検出（5GHz含む）＝ハードウェア・電波環境は健全

## 決定的な観測（この非対称が問題の核心）

1. **冷間Direct Boot（ASP3）**：PHY較正完走・esp_wifi_init成功・esp_wifi_start成功・scanが約11秒かけて正常完走——**しかしAP数=0**。再現100%。
2. **クロスカーネル・ハンドオフ**：stock ESP-IDFを「`app_main`の先頭」（NVS/WiFi/PHYコードを一切実行していないことをポインタ検査で証明済み）まで走らせ、リセット無しでMMU再マップ＋ジャンプで**同等のASP3イメージ**を起動→同じscanコードが**20-25 AP検出**。再現100%。

つまり「**ESP-IDFの標準ブート列（2nd-stageブートローダ→call_start_cpu0→esp_system_init）が確立する何か**」が受信の成否を分ける。WiFi/PHYコードの実行は無関係（app_main先頭ハンドオフで証明済み）。

## 解決済み（この過程で発見・修正したDirect Boot欠落——もう原因ではない）

これらを全部直した結果が上記1の状態（較正もscanも通るが0 AP）：
- CPUルートクロックがXTAL(48MHz)のまま→bootloader相当のPLL切替を移植（CPU実測240.0MHz、ESP-IDF標準と同一）
- ROMがBBPLLを「XTAL=40MHzプロファイル」で較正し576MHzで誤ロック→48MHzプロファイルで再較正（実測480MHz正確）
- regi2c（アナログ設定バス）マスタのICGクロックゲート解除、WiFi BBのICG、RTC_FASTクロック源、Super WDTキー誤り、カーネルの割込み出口バグ（CLIC mil固着）等

## 検証済み・棄却済みの仮説（再提案不要）

同一バイナリを冷間/ハンドオフ後の**両系譜で同一停止点**において比較する手法（各2ブート、既知ノイズの4-way除去付き）で：
1. **MMIOレジスタ**：MODEM_SYSCON/MODEM_LPCON/MODEM0(WiFi BB)/MODEM1/MODEM_PWR0/PCR/PMU/LP_ANA/LP_AON/LPPERI/LP_CLKRST/PVT等の広域ブロック——系譜差分は19語まで縮小、全て注入実験で非因果と確定
2. **regi2c空間**：公開5block＋未公開3block（0x63/0x68/0x6b）×全reg×host両側（読取りプロトコルの有効性はknown-answer gateで毎回証明）——系譜差分9語、全て注入で非因果or「較正の出力（＝症状であって原因でない）」と確定
3. **XTAL周波数の信念**：PCR_SYSCLK_CONFのCLK_XTAL_FREQフィールド[30:24]は両系譜とも48で一致。eFuse XTAL_48M_SELも一致
4. **SRAM上のROMデータ/グローバル**：SRAM最上位16KB（ROM .data/.bss域）の系譜diff——ROMブートスタック残渣ノイズとビルドレイアウト差のみ。blobが参照するvtableルート（g_osi_funcs_p/phy_rom_phyFuns等）は完全一致。ROMの`s_ticks_per_us`はASP3側で更新済み
5. **PHY較正モード・init_data・phy_param**：cal_mode=full同士で比較済み、init_data 256バイト一致、較正結果テーブルは現在は正常値
6. **stockブート列のパルス的初期化**：bootloader_random（SAR ADCエントロピーサイクル）の加算移植＋減算teardownの両面棄却。リセット/イネーブルパルス列のソース監査＝ASP3に欠落しているパルスはゼロ
7. **電源系**：pmu_init全バンク（HP_ACTIVE/HP_MODEM/HP_SLEEP/LP）・PVT dbias・XPD_BIAS・ocodeトリム等を移植（適用を読み戻しで機械確認）——全て症状不変
8. RTC SLOWクロック較正値の注入、周波数実測（mcycle vs SYSTIMER 2点法）による裏取り等も実施済み

## まだ検証していない領域（ここへの意見が特に欲しい）

- **RX指標の再ベースライン**：promiscuousカウンタ・MAC割込み率等の測定は全てクロック修正**前**のもの。現在の（クロック正常な）冷間状態で「電波を全く感知していない」のか「受信しているが破棄している」のかは未測定
- **WiFi MACレジスタブロック**（RXフィルタ等）の系譜diff——BB系は比較済みだがMAC系ブロックは未比較の可能性
- **LP RAM／LP_AON STOREレジスタ全域**の系譜diff（STORE1のみ検証済み）
- **blob内部の実行時シーケンス**（同一コードでも読み取る環境値で分岐する箇所）

## 質問

1. **ESP-IDF標準ブート列がWiFi受信可否に影響を与えうる経路として、上記の棄却済みリストに載っていないものは何か？** 具体的な機構（どのレジスタ/メモリ/状態を、ブート列のどの処理が、どう変えるか）と、ESP-IDF v6.1のソース上の該当箇所（components/…のファイル名・関数名）を挙げてほしい。
2. 「scanは正常に完走する（タイムライン正常・エラー無し・WDT無し）のにAP=0」「TX側も含めて内部的には全て成功に見える」という症状プロファイルから、**受信経路のどの段階（RF→AGC→BB復調→MAC filter→ソフト集計）で落ちていると推定するのが合理的か**。各段階を切り分けるための、ESP32系で実際に観測可能な指標（レジスタ・カウンタ・関数）を挙げてほしい。
3. 私たちのハンドオフ実験の解釈（「ブート列が確立する状態が原因」）に対する**別解釈**があれば。例えば「ハンドオフ系譜ではリセット無しなので、ROM初回ブートが壊す/確立し損ねる何かが、2度目のソフト起動では偶然回復している」（＝鍵は「stockのブート列」ではなく「POR後2回目の初期化であること」）のような可能性と、その判別実験。
4. Espressifへ問い合わせる場合、この症状プロファイルで**先方が最初に確認を求めそうな項目**（こちらで事前に測っておくべきデータ）は何か。

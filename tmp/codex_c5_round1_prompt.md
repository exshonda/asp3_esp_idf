非対話（一回きり）の相談です。コードの編集や実装は不要——**分析と次の一手の提案だけ**をお願いします。
回答は日本語で。根拠となるESP-IDF/blob/ROM/HALの該当箇所があれば `file:line` や関数名で示してください。

このリポジトリは `/home/honda/TOPPERS/ASP3CORE/asp3_esp_idf`（branch `claude/c6-wifi-c5-dev-5vc6x9`）。
全経緯は `docs/c5-bringup.md`（実施1〜13）。C6側の長期調査は `docs/wifi-shim-c6.md`（実施1〜85）。
ESP-IDF v6.1-beta1 は `/home/honda/tools/esp-idf-v6.1`、HALは `hal/`（esp-hal-3rdparty submodule）。

# 文脈：なぜC5をやっているのか

ESP32-C6で85ラウンドかけても解けなかった **deaf-RX**（MAC/BB割込みは約140/s発火するが
`lmacRxDone`が一度も立たない＝正当な802.11 RX_DONEがHWから上がらない。至近距離の既知強ビーコン
にも無反応。TXも電波として放射されていない疑い）がある。C6調査は「ソフト/JTAG＋物理RF刺激の層では
出尽くした」として凍結（`memory/project_c6_agc_investigation.md`）。

**ESP32-C5は同世代モデム**なので、C5で同種の症状が再現するか否か自体が
「C6固有 vs 同世代共通」の判別指標になる、という理由でC5移植を主軸に切り替えた。

ASP3はTOPPERS/ASP3（μITRON系RTOS）で、**Direct Boot**（ESP-IDFのブートローダもFreeRTOSも使わない）。
Wi-Fi blobはos_adapter shim経由で駆動している。

# C5でこれまでに解決したこと（実施11〜13）

1. **実施11**：`esp_wifi_init_internal`の0x3001失敗 → `sdkconfig_stub/sdkconfig.h`に
   `CONFIG_SOC_*`ミラー（`CONFIG_SOC_WIFI_HE_SUPPORT`等）が欠落し、`wifi_osi_funcs_t`のサイズが
   4バイト小さく`_magic`がずれていた。`nm -S`で0x1f8→0x1fcを実測して確認・修正。
2. **実施12**：修正後に露見したハング → JTAGで`core_exc_entry_1`→`_kernel_exc_table`→`T_EXCINF`と辿り、
   `mepc=0`のInstruction access fault＝**NULL関数ポインタ呼出し**と特定。blobの`wifi_hw_start()`が
   `g_wifi_osi_funcs`のoffset 0xC8を無条件に呼ぶ。v9で`_wifi_apb80m_request/release`が削除された
   **同じスロットに置換された後継フィールド** `_wifi_pm_sleep_lock_acquire/release` の追加漏れ。no-opスタブで修正。
3. **実施13（直前のラウンド）**：さらに先の`phy_iq_est_enable_new`の無限ループを追跡。
   - `MODEM0`(`0x600A0000`)＝Wi-Fi BBのレジスタ書込みが**CPU起点でも一切効かない**ことを、
     陽性対照（`MODEM_LPCON`/`MODEM_SYSCON`への書込みは成立）付きで確認＝JTAG/APM由来のアーティファクトではない。
   - 原因は**WIFIBBクロックのICGゲート**：`MODEM_SYSCON_CLK_CONF_POWER_ST`(`0x600A9C0C`)の
     `CLK_WIFI_ST_MAP`=`BIT(1)|BIT(2)` に対し、`PMU_HP_ACTIVE_ICG_MODEM_REG`(`0x600B000C`)の
     `code`(bit31:30)が**0**のまま（Direct Bootで`pmu_init()`が走らないため）。
     `CLK_CONF1`(`0x600A9C14`)の`CLK_WIFIBB_*_EN`は全て1なのに、上位のICGで閉じていた。
   - `FORCE_ON=0`・ST_MAP不変のまま**`icg_modem.code`だけを0↔2でトグルするA/B/A/B反証実験**で因果確認
     （code=0→BB書込み無視／code=2→成立）。適用にはPMU即時反映パルス2本
     （`0x600B00DC` bit31 = `update_dig_icg_modem_en`、`0x600B00D0` bit28 = `update_dig_icg_switch`）の
     **両方**が必要（片方だけでは反映されないことも実測）。
   - 「C6ではICG初期化は冗長」というC6実測を**C5に未検証のまま踏襲**して無効化されていた
     `esp_shim_modem_icg_init()`（コード上も`【実機確認待ち】`と明記）を有効化＝修正。
   - 実機効果：`MODEM0+0x450`が**永久`0x00000000` → `0x00102003`**に変化＝blobのBB書込みが到達するようになった。

# 現在の未解決症状（ここを相談したい）

`esp_wifi_init`は進むが、**PHY較正の`phy_iq_est_enable_new`が無限ループのまま**。

blobの当該ループ（逆アセンブル、アドレスはビルドで変動）：
```
loop: lw   a5,[0x600A047C]    ; BB done/status レジスタ
      slli a4,a5,0xf          ; a4の符号ビット = a5のbit16
      bgez a4, body           ; bit16==0 → ループ本体へ（＝0で回り続ける）
      <epilogue> ret          ; bit16==1 で脱出
body: jal  phy_get_pkdet_data ; [0x600A0C50] を読む
      jal  phy_abs_temp
      ... 閾値比較・カウンタ++ ...
      j    loop               ; **全経路がloopへ戻る＝bit16以外に脱出路が無い**
```
- `MODEM0+0x450` bit1 … IQ推定の**起動(start)**ビット（blobがclear→delay→setとパルス）。
  ICG修正後は`0x00102003`（bit0,1,13,20が立っている）＝**起動ビットは立っている**。
- `MODEM0+0x47C` bit16 … 対応する**完了(done)**ビット。**修正後も永久に0**。
- ループ脱出の`ret`にHWブレークポイントを置き100秒resumeしても**一度も着火せず**（PCサンプリングだけでなく構造的にも無限ループ確認）。

## ICG修正後の実測レジスタ値（ハング中、halt状態、C5#1 DUT）

| レジスタ | アドレス | 値 |
|---|---|---|
| PMU hp_active icg_modem | `0x600B000C` | `0x80000000`（code=2、FWが適用済み） |
| MODEM_SYSCON CLK_CONF_FORCE_ON | `0x600A9C08` | `0x00000000`（強制ONに頼っていない） |
| MODEM_SYSCON CLK_CONF | `0x600A9C04` | `0x00201002` |
| MODEM_SYSCON CLK_CONF_POWER_ST | `0x600A9C0C` | `0x64646400` |
| MODEM_SYSCON CLK_CONF1 | `0x600A9C14` | `0x003BE7FF` |
| MODEM_SYSCON WIFI_BB_CFG | `0x600A9C18` | `0x10003802` |
| MODEM_SYSCON FE_CFG | `0x600A9C1C` | `0x00000000` |
| MODEM_LPCON CLK_CONF | `0x600AF018` | `0x00000007`（WIFIPWR/COEX/I2C_MST） |
| MODEM_LPCON +0x08 | `0x600AF008` | `0x00000310` |
| MODEM0 BB start | `0x600A0450` | `0x00102003` |
| MODEM0 BB done | `0x600A047C` | `0x00000000` |
| MODEM0 pkdet | `0x600A0C50` | `0x00000000` |
| MODEM0 (0x8D0) | `0x600A08D0` | `0x00000000` |

`CLK_CONF1`=`0x003BE7FF`のデコード：`WIFIBB_22M/40M/44M/80M/40X/80X/40X1/80X1/160X1`(bit0-8)=1、
`WIFIMAC`(9)=1、`WIFI_APB`(10)=1、**`FE_20M`(11)=0、`FE_40M`(12)=0**、`FE_80M`(13)=1、`FE_160M`(14)=1、
`FE_APB`(15)=1、`BT_APB`(16)=1、`BTBB`(17)=1、`BTMAC`(18)=0、`FE_PWDET_ADC`(19)=1、`FE_ADC`(20)=1、`FE_DAC`(21)=1。

## 本ラウンドで**棄却済み**の候補（同じ提案は不要）

- **FE_20M/FE_40Mクロック欠落**：実行中にJTAGで`CLK_CONF1`を`0x003BFFFF`（両bit ON）にしてresume
  → **ループから脱出せず、done bitも0のまま**。棄却。
- **ICG初期化のタイミングが遅すぎる（前倒しすべき）**：ハング中もBBは書込み可能
  （実行中のJTAG書込みが成立、`0x450`=`0x00102003`）＝**IQ推定ループが回っている最中にBBクロックは生きている**。
  よって呼出し位置の前倒しでは何も変わらない。自己棄却。
- **`modem_syscon_ll_set_modem_apb_icg_bitmap`が効いていない疑い**：構造体オフセット・ビットフィールド・
  C5のsyscon基底(`0x600A9C00`、C6から+0x400)は全て正しいと確認。`POWER_ST`が元値のままなのは
  `esp_shim_modem_icg_init()`の後に走る`modem_clock`側が上書きしているだけ。A/B実験でこの書換えは
  不要と確認済み＝実害なし。

# 現時点の最有力仮説（検証方法も含めて評価してほしい）

**regi2c経由のRF/PLL較正が未完了**。IQ不均衡推定はアナログRF鎖（LO/ミキサ/ADC）が実際に動作して
テストトーンを注入・測定できて初めてdoneが立つはず。RF PLLがロックしていない、あるいは
regi2c越しのRF較正が完了していないなら、起動ビットを立てても有効な測定結果が得られず
doneは永久に立たない。C6の根本原因修正（regi2cのI2C_MSTクロック）とも、上表の`FE_CFG`=0 とも整合する。

# 相談したいこと

1. **`MODEM0+0x47C` bit16 と `MODEM0+0x450` の正体**について、ESP-IDF/HAL/blob/ROM/TRMから
   根拠のある同定はできるか（`phy_iq_est_enable_new`はblob内なのでソースが無い）。
   `pkdet`(`0x600A0C50`)と`0x600A08D0`が両方0なのは何を意味するか。

2. **doneが立たないための必要条件として、C5のDirect Bootで欠けていそうな初期化**は何か。
   特に以下を評価してほしい：
   - RF PLL（BBPLL/RFPLL）のロック状態はどのレジスタ／regi2cアドレスで確認できるか
   - `FE_CFG`(`0x600A9C1C`)=0 は正常か。ESP-IDFでは誰がいつ書くか
   - `WIFI_BB_CFG`(`0x600A9C18`)=`0x10003802` は妥当か
   - `esp_phy_enable`→`register_chipv7_phy`の呼出し前提（PHY較正データ、`phy_init_data`、
     `esp_phy_load_cal_and_init`相当）でDirect Bootが飛ばしているものはあるか
   - PMUのMODEM電源ドメイン（`PMU_HP_ACTIVE_*`の他フィールド、`pmu_init()`が本来行う設定）で
     ICG code以外に**アナログ側**に効くものはあるか

3. **次にやるべき実験を、費用対効果順に3つ以内**で。JTAG（OpenOCD、read/write可）と実機2台
   （C5#1=DUT・書込み可、C5#2=stock参照機・**読み取り専用/書換え厳禁**）が使える。
   「これを見れば仮説が判別できる」という**判別実験**の形で。

4. **打ち切り基準の妥当性**：私は「regi2c/RF較正が正常に動いていると確認できた上でIQ推定が完了しない
   なら、それはC6と同種のアナログ壁を較正時点で再現したということであり、それ自体が答え
   （C6-genericと結論して停止、85ラウンド級のレジスタ総当りには入らない）」と決めている。
   この基準は妥当か。もっと早く/遅く打ち切るべきか。**C5でRF較正が正常でIQ推定だけ失敗する**という
   結果は、本当にC6のdeaf-RXと同一原因を示唆すると言えるか（それとも別物と考えるべきか）。

なお、C6のdeaf-RXでは`esp_wifi_init`自体は完走しscanまで到達していた（PHY較正は通っていた）。
**C5はそれより手前のPHY較正時点で止まっている**——この非対称性をどう解釈すべきかも含めて意見が欲しい。

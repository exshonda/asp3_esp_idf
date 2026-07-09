# ESP32-C6 deaf-RX コードレビュー（レンズ：起動シーケンス・メモリ保護・電源/クロック・リンカ）

調査方式：コードレビューのみ（実機・JTAG不使用）。対象は `hal/`
（esp-hal-3rdparty submodule，コミット `b90b1837cb5ad24747deb4c895246037cc206ce5`，
`esp32c6.rom.*.ld`等から確認済みの実際にビルドへ使われているスナップショット）
と `asp3/target/esp32c6_espidf/`・`asp3/asp3_core/arch/riscv_gcc/esp32c6/`。

前提として `docs/wifi-shim-c6.md` 実施1-85・README.mdのESP32-C6行を読了。
確定事実（実施22のPMP/PMA・実施30のPMU・実施66のRXディスクリプタ・実施80の
lmacRxDone等）と矛盾しない範囲で候補を検討した。

---

## 候補1（最有力）：APM（Access Permission Management）／TEEマスタセキュリティモードが
ASP3では一切初期化されず，POR既定値のまま「WiFiモデムマスタ＝REEモード」
「APM制御フィルタ＝有効（全マスタ対象）」という，実ESP-IDF/NuttXなら必ず
解除している状態が残っている

### 該当箇所

- **ASP3側（欠落側）**：
  `asp3/target/esp32c6_espidf/target_kernel_impl.c` の `hardware_init_hook()`
  （83-182行目）。WDT無効化・SWDキー修正・`esp_rom_set_cpu_ticks_per_us()`・
  `esp_shim_coex_adapter_register()`のみで，`apm`/`tee`/`APM`/`TEE`という
  文字列は本ファイルに**1件も出現しない**（`grep -n 'apm\|tee' target_kernel_impl.c`
  結果0件，実際に確認済み）。
  `asp3/asp3_core/arch/riscv_gcc/esp32c6/`・`asp3/target/esp32c6_espidf/esp_wifi.cmake`
  のいずれにも `apm_hal.c`（APM実装本体）はビルド対象として一切現れない
  （`esp_hal_security/include` はhmac_types.h等mbedtls用ヘッダパスとして
  1箇所だけ参照されるが，`.c`ソースはリンクされていない）。

- **実ESP-IDF側（対照・根拠）**：
  `hal/components/bootloader_support/src/bootloader_mem.c`（1-45行目，
  全文）：

  ```c
  void bootloader_init_mem(void)
  {
  #if !defined(BOOTLOADER_BUILD)
      /* By default, these access path filters are enable and allow the
       * access to masters only if they are in TEE mode. Since all masters
       * except HP CPU boots in REE mode, default setting of these filters
       * will deny the access to all masters except HP CPU.
       * So, at boot disabling these filters. They will enable as per the
       * use case by TEE initialization code.
       */
  #if SOC_APM_CTRL_FILTER_SUPPORTED
      apm_hal_enable_ctrl_filter_all(false);
      /* [APM] On power-up, only the HP CPU starts in TEE mode; others
       * default to REE2. APM blocks REE0–REE2 access by default.
       * Thus, all masters are set to TEE mode.
       */
  #if SOC_APM_SUPPORT_TEE_PERI_ACCESS_CTRL
      apm_hal_set_master_sec_mode_all(APM_SEC_MODE_TEE);
  #endif
  #endif
  #endif
  #if CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE
      esp_cpu_configure_region_protection();
  #endif
  }
  ```

  この`bootloader_init_mem()`は
  `hal/components/bootloader_support/src/esp32c6/bootloader_esp32c6.c:109-126`
  の`bootloader_init()`（`#if !CONFIG_APP_BUILD_TYPE_RAM`ガード下，120行目）
  から，かつ`hal/components/esp_system/port/cpu_start.c:255,690`
  （ESP-IDF app側 `call_start_cpu0`系）からも呼ばれる。
  実施84で確認済みの通り，**NuttXは`CONFIG_ESPRESSIF_SIMPLE_BOOT`により
  `esp_start.c`から`bootloader_init()`をアプリ自身から直接呼ぶ**
  （2ndステージブートローダという別バイナリは経由しないが，同じ
  `bootloader_init()`関数は実行する）。つまり**NuttXは必ずこの
  APM初期化コードを通過する**。
  ASP3のDirect Bootは`bootloader_init()`（ゆえに`bootloader_init_mem()`）
  を一切呼ばない（`grep -rn 'bootloader_init' asp3/target/esp32c6_espidf/`
  結果0件，`hardware_init_hook`の内容は上記の通り）。

### 根拠（レジスタ定義のリセット値）

`hal/components/soc/esp32c6/register/soc/tee_reg.h`：

```c
#define TEE_M0_MODE_CTRL_REG (DR_REG_TEE_BASE + 0x0)
/** TEE_M0_MODE : R/W; bitpos: [1:0]; default: 0;    ← HPCORE=tee_mode(0) */
#define TEE_M4_MODE_CTRL_REG (DR_REG_TEE_BASE + 0x10)
/** TEE_M4_MODE : R/W; bitpos: [1:0]; default: 3;    ← MODEM=ree_mode2(3) */
```

`DR_REG_TEE_BASE = 0x60098000`（`hal/components/soc/esp32c6/register/soc/reg_base.h:54`）。
マスタID割当ては`hal/components/esp_hal_security/include/hal/apm_types.h`：

```c
typedef enum {
    APM_MASTER_HPCORE  = 0,   /* HP CPU */
    APM_MASTER_LPCORE  = 1,
    APM_MASTER_REGDMA  = 2,
    APM_MASTER_SDIOSLV = 3,
    APM_MASTER_MODEM   = 4,   /* ← WiFi/BTのモデムDMAマスタ */
    ...
} apm_master_id_t;
```

すなわち**リセット直後（POR），HP CPU（M0）はTEEモード，WiFi/BTモデム
（M4）はREEモード2**——これは`apm_hal_set_master_sec_mode_all(TEE)`という
コード自体のコメントが明示している通り「デフォルトでは全マスタが
TEEモード（＝HP CPU以外）にされる」という手順を実行しないと**変更されない
値**である。

さらに`HP_APM_FUNC_CTRL_REG`（`hal/components/soc/esp32c6/register/soc/hp_apm_reg.h`，
`DR_REG_HP_APM_BASE=0x60099000`のオフセット`0xc4`＝`0x600990c4`）：

```c
/** HP_APM_M0_PMS_FUNC_EN : R/W; bitpos: [0]; default: 1;  */
/** HP_APM_M1_PMS_FUNC_EN : R/W; bitpos: [1]; default: 1;  */
/** HP_APM_M2_PMS_FUNC_EN : R/W; bitpos: [2]; default: 1;  */
/** HP_APM_M3_PMS_FUNC_EN : R/W; bitpos: [3]; default: 1;  */
```

（4マスタ全てPOR既定でPMSフィルタ有効＝`apm_hal_enable_ctrl_filter_all(false)`
が呼ばれない限りON）。`HP_APM_REGION0_ADDR_START/END`のPOR既定値は
`0x00000000`〜`0xFFFFFFFF`（アドレス空間全域），
`HP_APM_REGION0_R{0,1,2}_PMS_{X,W,R}`は全てPOR既定`0`（アクセス不可）。

### 重要な留保（率直な限界）

`HP_APM_FUNC_CTRL_REG`が直接名指しするマスタは**M0-M3
（HPCORE/LPCORE/REGDMA/SDIOSLV）の4本のみ**（`apm_defs.h`の
`APM_CTRL_HP_APM_PATH_NUM=4`と整合）。**WiFi/BTモデム（M4）は
HP_APM／LP_APM0／LP_APM のいずれの`FUNC_CTRL_REG`のビットフィールドにも
現れない**——これらregionベースのAPMコントローラがモデムのSRAM DMAパスを
直接ゲートしているという確証は，本レビュー（レジスタ定義とヘッダの
突合せ）だけでは得られなかった。TEEペリフェラル自体はM0〜M24まで
マスタ単位でモード（tee/ree0/ree1/ree2）を記録する専用レジスタ
（`TEE_Mx_MODE_CTRL_REG`）を持つが，そのモード値を実際に「参照して
アクセスを拒否する」下流の回路（regionベースAPM以外の経路，たとえば
モデム自身の内蔵セキュリティラッパ）がこのhal/スナップショットの
ヘッダ検索範囲内には見つからなかった（TRMの本文記述に相当する情報は
ヘッダのコメントより先には確認できていない）。

したがって：
- 「ASP3はNuttX/実ESP-IDFが必ず実行するAPM/TEE初期化
  （`bootloader_init_mem()`）を一度も呼んでいない」——これは**確定した
  コード上の事実**（確度：高）。
- 「そのこと自体が，WiFi MACのDMAアクセスを遮断してdeaf-RX/TX無放射を
  引き起こしている」——これは**強く示唆されるが，レジスタ定義の突合せ
  だけでは機構の終端（モデムマスタの実際のアクセス可否を左右する
  具体的な回路）まで追い切れていない仮説**（確度：中）。

### 観測事実との整合性

- **MMIO全域一致（実施25/60/73-77等）**：整合する。CPU（M0=HPCORE）は
  POR既定でTEEモード＝region filterの対象外（`apm_hal_enable_ctrl_filter_all`
  はM0-M3を含めて全マスタのフィルタをON/OFFする единый knobだが，
  TEEモードのマスタはそもそもPMSチェックの対象外という設計——
  `bootloader_mem.c`のコメント通り）。ゆえにJTAG/CPU経由のMMIO読出しは
  ASP3・NuttXどちらでも常に成功し，値も一致する——CPU自身のアクセス経路は
  APM/TEEの影響を受けない。
- **MAC割込みは定常的にアサートされる（~140/s，実施58/59/78/83）**：
  整合しうる。割込み線のアサート自体はINTMTX配線・ハードウェアイベント
  ロジックの領域であり，APMのバスマスタ権限チェックとは別レイヤ。
  モデムがバスアクセス権を持たなくても，何らかのハードウェアイベント
  （TX完了・エネルギー検出等，実施59でbit7=TX完了と確認済み）で
  割込み線自体は上がりうる。
- **RXディスクリプタはCPU視点で完全に健全，しかしRX完了ライトバックが
  一度も起きない（実施66）**：**最も強く整合する**。ディスクリプタ
  リング自体はCPU（M0，TEEモード）が構築するため健全に見えるが，
  モデム（M4，REEモード2）が実際にそのSRAM領域へRX完了データを書き戻す
  （owner bit解放・`0x600a408c`更新）操作は，APM/regionフィルタが
  有効なままなら拒否されうる——「バッファは正しい場所にあるのに
  一度も書き込まれない」という実施66の帰結と完全に一致する。
- **TXも無放射（実施81）**：整合しうる。モデムがTXバッファをDMA読出し
  できなければ変調すべきデータが得られず送信されない。
- **lmacRxDone（実施80，最上流指標）が一度も発火しない**：整合しうる。
  RXの完了通知自体がバスマスタとしての書込み成立を前提とするなら，
  その前段でブロックされていれば当然発火しない。
- **AGC/CCAは動的に変化し続ける（実施78/83）**：矛盾しない。AGC/CCAは
  アナログRF回路・PHYのregi2c経由設定（CPU=M0が行う）に基づく現象であり，
  モデムのSRAM DMAパスとは別回路。

### 安価な検証方法（実機再開時にすぐ使える形）

以下をASP3稼働中のボードでJTAG `mdw`で読み，NuttX稼働中の同一箇所と
比較する（実施75以降のASP3-vs-NuttX比較フォーマットに準拠）：

| レジスタ | アドレス | 期待値（ASP3，POR未変更なら） | 期待値（NuttX，`bootloader_init_mem`実行後） |
|---|---|---|---|
| `HP_APM_FUNC_CTRL_REG` | `0x600990c4` | `0x0000000F`（M0-M3 PMS_FUNC_EN全て1のPOR既定） | `0x00000000`（`apm_hal_enable_ctrl_filter_all(false)`実行後） |
| `HP_APM_REGION_FILTER_EN_REG` | `0x60099000` | `0x00000001`（region0のみ有効のPOR既定） | 実装依存（無効化後は無意味） |
| `HP_APM_REGION0_PMS_ATTR_REG` | `0x6009900c` | `0x00000000`（R/W/X全モード0のPOR既定） | 実装依存 |
| `TEE_M0_MODE_CTRL_REG` | `0x60098000` | `0x0`（HPCORE=tee_mode，POR既定） | 同じく`0x0`のはず（HP CPUは変更不要） |
| `TEE_M4_MODE_CTRL_REG` | `0x60098010` | `0x3`（MODEM=ree_mode2，POR既定，ASP3は未変更のはず） | **要確認**（`SOC_APM_SUPPORT_TEE_PERI_ACCESS_CTRL`はc6で未定義のため`apm_hal_set_master_sec_mode_all`は呼ばれない＝NuttXでも`0x3`のまま かもしれない。もし両者とも`0x3`で一致するなら，TEEモード単体はdeaf-RXの原因ではなく，`HP_APM_FUNC_CTRL_REG`の差だけが効いていることになる） |
| `LP_APM0_FUNC_CTRL_REG` | `DR_REG_LP_APM0_BASE(0x60099800)+相当オフセット` | POR既定（要`apm_ll.h`でオフセット確認） | 無効化後 |
| `LP_APM_FUNC_CTRL_REG` | `DR_REG_LP_APM_BASE(0x600B3800)+相当オフセット` | POR既定 | 無効化後 |

**決定実験（安価・一回限り）**：`HP_APM_FUNC_CTRL_REG`（`0x600990c4`）を
JTAGで`0`へ直接書き込み，`HP_APM_REGION0_PMS_ATTR_REG`
（`0x6009900c`）の該当ビットを全許可（X/W/R=1）にした状態で，
稼働中のASP3スキャンに対し`lmacRxDone`（`0x40000c50`）が発火するかを
確認する。発火すれば本仮説はほぼ確定，発火しなければ
（HP_APMがそもそもモデムを見ていない可能性が高いという上記留保通り）
この経路は否定され，TEE_Mx方式または全く別の機構を疑う必要がある。
なお本レビューはソース変更を伴わないため，この検証はJTAGレジスタ直書き
（ソフトウェア修正ではない）として提案する。

### 確度：**中〜高**

コード上の「ASP3が実ESP-IDF/NuttX必須の初期化を欠いている」事実自体は
確度高。「それがdeaf-RXの直接原因である」という因果の确度は中
（HP_APMのマスタ範囲がM0-M3止まりという留保のため，実機JTAG一撃での
確認を強く推奨）。

---

## 候補2：PMP/PMA（asp3_core側）——本レンズでは「反証済み・無関係」を再確認するにとどまる

`asp3/asp3_core/arch/riscv_gcc/esp32c6/`・`asp3/asp3_core/arch/riscv_gcc/common/`
を`grep -rn 'pmp\|PMP\|pma\|PMA'`した結果，**該当0件**。ASP3はPMP/PMAを
一切設定しない。

これは実施22の実測（通常Direct Boot下ではpmpcfg0-3/pmpaddr0-3が終始
全ゼロ＝PMP無効＝無制限アクセス）と整合しており，**通常のDirect Boot
経路においてPMP/PMAは無関係**（実機で確定済み）。実施22でPMPロックが
問題になったのは，本物の2ndステージブートローダを経由する特殊な
A/Bテスト（実施21）の中でのみであり，通常運用のDirect Bootとは別の
コードパスである。

確度：**低**（本レンズの担当としては「関与なし」を追認するのみ。
新規の手がかりではない）。

---

## 候補3：起動シーケンスの他の未移植初期化（`esp_rtc_init`/`esp_clk_init`/PMU等）

`docs/wifi-shim-c6.md`実施30（PMUレジスタ1KB全域一致）・実施34
（`esp_clk_init()`減算法で無関係と確認）・実施84（棚卸しで全候補が
「移植済み」「idempotent」「単体では無関係」のいずれかに分類済み）に
より，本レンズの観点でも新規の有望な欠落は見つからなかった。
`asp3/target/esp32c6_espidf/target_kernel_impl.c`の`hardware_init_hook()`
（後述の全文）は，実施6/48/54で個々に実機検証済みの3項目
（WIFIPWRクロックゲート・`s_ticks_per_us`・coex_pre_init早期化）に
特化しており，それ以上の追加初期化（`bootloader_init_mem`含む）は
一切行っていない——これは候補1の「APM/TEEも同様に欠落している」という
指摘と論理的に同じ系列（Direct Bootが`bootloader_init()`系列の初期化を
丸ごとスキップしている）に属する。

確度：**低**（既存ラウンドで個別に反証済み。候補1のみが新規）。

---

## 候補4：リンカ（`esp32c6.ld`／`esp_wifi.cmake`のROM ld）

`asp3/target/esp32c6_espidf/esp32c6.ld`のIRAM/DRAMセクション
（`.iram1.*`/`.coexiram.*`/`.wifi0iram.*`/`.dram1.*`）はC3版の全セクション名を
移植済みで欠落なし（38-64行目のdocs記述と本レビューでの`grep`結果が一致）。
`esp_wifi.cmake`のROM ld一覧（339-361行目）はビルド時に全123ファイル
実在確認済みかつリンクエラーなし（コメント716-726行目）。TXがそもそも
無放射（実施81）という事実は，「blobコードがIRAM上で実行できていない」
のであればリンク・実行自体が早期にクラッシュするはずであり
（現状は`esp_wifi_init`/`start`/`scan_start`まで正常完走），リンカ配置の
問題ではRX/TXの選択的な機能不全（割込みは出る，MMIOは一致）を説明できない。

確度：**低**（観測事実と整合しない＝データパスの問題ではなくロード
可否の問題であり，deaf-RXの選択的症状を説明しない）。

---

## 候補5：PHY較正経路（`phy_get_max_pwr`固定20dBm等）

`asp3/target/esp32c6_espidf/wifi/esp_shim_blobglue.c`の`phy_get_max_pwr`
プレースホルダ（固定20dBm相当）はTX**送信電力**の上限値のみに影響し，
RXの復調可否（deaf-RX，実施58-65で確立した「RX成功チェーン未到達」）
とは無関係。TXが無放射（実施81，電波そのものが出ていない）という
結果は，電力値が低く見積もられている・不正確という話ではなく，
「送信自体が成立していない」という，より根本的な症状であり，
この較正戻り値だけでは説明がつかない（実施18で較正戻り値・delay精度は
既に反証済み）。

確度：**低**（既存ラウンドで実質的に反証済み，新規性なし）。

---

## 総合順位

| 順位 | 候補 | 確度 | 新規性 |
|---|---|---|---|
| 1 | APM/TEEマスタセキュリティモード未初期化（`bootloader_init_mem()`/`apm_hal_enable_ctrl_filter_all`/`TEE_M4_MODE_CTRL_REG`をASP3が一切呼ばない） | 中〜高 | **84ラウンドを通じて未比較，本ラウンドで新規発見** |
| 2 | PMP/PMA（asp3_core側） | 低（関与なしを再確認） | なし（実施22で既に反証済み） |
| 3 | 他の未移植初期化（PMU/esp_clk_init等） | 低 | なし（実施30/34/84で反証済み） |
| 4 | リンカ配置 | 低 | なし（観測事実と不整合） |
| 5 | PHY較正戻り値 | 低 | なし（実施18で反証済み） |

**次の一手（実機再開時）**：候補1の決定実験（`HP_APM_FUNC_CTRL_REG`
`0x600990c4`を`0`へJTAG直書き＋`HP_APM_REGION0_PMS_ATTR_REG`
`0x6009900c`を全許可にし，`lmacRxDone`（`0x40000c50`）が発火するかを
確認）を最優先で提案する。あわせて`TEE_M4_MODE_CTRL_REG`
（`0x60098010`）をASP3・NuttX双方で読み比較し，値が一致するか
（＝TEEモード自体は無関係でHP_APM側の差だけが効いているか）を
切り分けること。

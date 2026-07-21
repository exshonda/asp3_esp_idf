# ESP32-C3 `wifi_scan`実機クラッシュ（Illegal instruction, pc=0）調査

## 課題

`apps/wifi_scan`をESP32-C3実機（QFN32, rev v0.3, serial
`<MAC-21>`）で実行すると，`esp_wifi_scan_start`成功後，
`WIFI_EVENT id=1`（`WIFI_EVENT_SCAN_DONE`）の直後にIllegal instruction
（`pc=0x00000000`）でクラッシュする。前段（別タスクでのTLSサポート
追加作業）でA/Bテスト済み：`TOPPERS_SUPPORT_TLS`のON/OFFに関わらず同一
クラッシュ（同一`ra=0x4200057e`）が再現し，TLSとは無関係な既存バグと
確定済み。

静的解析（`objdump -d`）により，クラッシュは`main_task`（実際には
インライン化されたイベントハンドラ本体）内の以下の命令列で発生する
ことが判明していた：

```
42000542: lw   s6, -1616(a5)     ; a5=0x40880000 → 実効アドレス 0x4087f9b0
4200054a: auipc s3, ...          ; s3 = blk[]配列（静的const）
42000552: auipc s2, ...          ; s2 = hst[]配列（静的const）
42000568: lbu  a1, 0(s2)         ; a1 = hst[i]
4200056c: lbu  a0, 0(s3)         ; a0 = blk[i]（i=0でa0=0x6b＝クラッシュ時のa0と一致）
42000570: zext.b a2, s0          ; a2 = i
42000574: li   a4, 0
42000576: li   a3, 7
4200057c: jalr s6                ; ★クラッシュ：s6=0（未設定）のためpc=0へジャンプ
```

クラッシュ時のレジスタ：`a0=0x6b, a1=0x1, a2=0x0, a3=0x7, ra=0x4200057e`
（＝`jalr`の次命令のアドレス）。

## 実施1：ソース監査で即座に特定 — JTAG実測を待たずに確定した根本原因

### 実装（調査手順）

依頼時点の仮説は「`0x4087f9b0`はROM常駐関数テーブル（`g_phyFuns`類似）
の特定エントリが未設定」というもので，JTAGでのライブ読み出し・
テーブル構造特定を要求されていた。しかし着手前に本リポジトリの過去の
調査記録（`docs/wifi-shim-c6.md`実施23・`docs/bt-shim.md`）を
grepしたところ，**`0x4087f954`という定数がすでに複数箇所でヒットし，
この投機的仮説を検証するまでもなく即座に正体が判明した**：

1. `docs/wifi-shim-c6.md`実施23：ESP32-C6で`g_phyFuns`テーブル
   （ROM隣接RAM上の固定関数ポインタテーブル）の実測アドレスが
   `0x4087f954`であることを特定済み。offset 92 = `rom_i2c_readReg_Mask`
   （5引数：block, host_id, reg_add, msb, lsb）。
2. `docs/bt-shim.md`：C3のBLE調査でも同じ`0x4087f954`が
   `esp_coex_adapter.c`のC6専用診断コード（offset 20読出し）内に
   **無guardで混入**していたことが既に発見・修正済み（`#if
   defined(TOPPERS_ESP32C6_WIFI)`でguard）と記録されていた
   （`MEMORY.md`のBLEエントリにも「副次修正」として記載あり）。

これを踏まえ`grep -rn "0x4087f954" apps/ asp3/target/esp32c3_espidf/`を
実行したところ，**`apps/wifi_scan/wifi_scan.c:312`に全く同一パターンの
未guardコードが現存**していることを発見した：

```c
/*  追記12：RF較正regi2cブロックを読み戻してRTC[16..](0x50000040)へ．
 *  ROM PHY funsテーブル(0x4087f954)のidx23=read_mask関数で全8bit取得．
 *  native(受信OK)と同じ読み出しをして比較＝RF較正の正否を判定． */
uint32_t *romtbl = (uint32_t *)0x4087f954U;
uint8_t (*rd)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) =
	(uint8_t (*)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t))
		(uintptr_t)romtbl[23];
volatile uint8_t *out = (volatile uint8_t *)0x50000040U;
static const uint8_t blk[4] = {0x6bU, 0x6aU, 0x66U, 0x6dU};
static const uint8_t hst[4] = {1U, 1U, 0U, 1U};
int bi, r, o = 0;
for (bi = 0; bi < 4; bi++) {
	for (r = 0; r < 16; r++) {
		out[o++] = rd(blk[bi], hst[bi], (uint8_t)r, 7U, 0U);
	}
}
```

静的解析で「未知のblob内部配列」と見えていた`blk.1`/`hst.0`（GCCが
static local const配列に付与した匿名サフィックス）は，実は**ASP3側
アプリ自身のソース**（`wifi_scan.c`の`static const uint8_t blk[4]`/
`hst[4]`）だった——DROM上の匿名シンボルという見た目に反射的に「リンク
済みblob内部」と推定したのが唯一の誤りで，`nm`/`objdump`だけでなく
`grep`でソースツリー自体を素直に探せば即座に見つかった。

引数の対応を確認：`rd(blk[0]=0x6b, hst[0]=1, r=0, msb=7, lsb=0)` →
`a0=0x6b, a1=1, a2=0, a3=7, a4=0`。クラッシュ時のレジスタ
（`a0=0x6b, a1=1, a2=0, a3=7`）と**完全一致**。

### なぜこのコードがwifi_scan.cに存在するか

このブロックはコメント「追記12」「追記19」から分かる通り，**ESP32-C6
のAGCデッドRX調査（`docs/wifi-shim-c6.md`）でC6実機のみを対象に追加
された診断計装**であり，`apps/wifi_scan/wifi_scan.c`はC3/C6共有の
アプリファイルであるため，同ファイル内の他の同種C6専用診断
（例：`0x50000000`台のRTC-RAM累積カウンタ，`0x600af018`への
`MODEM_LPCON_CLK_CONF`直書き）はすべて`#ifdef TOPPERS_ESP32C6_WIFI`で
正しくguardされていたのに対し，**このブロック（308-325行目）だけ
guardが漏れていた**——`esp_coex_adapter.c`で修正済みの「同種のguard
漏れバグ」の**同一ファイル内・別インスタンス**である。

### 根本原因の確定

- `0x4087f954`はESP32-C6の`g_phyFuns`テーブル（ROM常駐関数ポインタ
  テーブル）の固定アドレスであり，**ESP32-C3には存在しない
  （C3のROM/ブロブレイアウトは全く別）**。
- C3実機で`*(uint32_t*)0x4087f954`から読んだ値（このアドレスにたまたま
  存在する何らかのデータ）のoffset 92（idx23）はNULL相当（0）となり，
  `rd`関数ポインタが0のまま`jalr`され，Illegal instruction（pc=0）で
  クラッシュする。
- **JTAGでのライブ読み出しは実施しなかった**（依頼書のタスク1〜4は
  この時点で不要と判断）——ソース監査で「そもそも呼ぶべきでない
  C6専用コードがguardなしでC3ビルドに混入している」ことが確定した
  ため，「このアドレスの正体を突き止める」「テーブルが埋まる条件を
  探す」という調査自体が的外れ（C3ではこのアドレスに`g_phyFuns`
  テーブルは存在しないので，どんな条件でも「正しく埋まる」ことはない）
  と判断した。反証すべき仮説（C3固有の未知の初期化ギャップ）は
  ソースの時点で消滅した。

## 実施1（続き）：修正実装と実機検証

### 修正（`apps/wifi_scan/wifi_scan.c`のみ．submoduleでも`asp3/target/`
### でもなくアプリ層——禁則対象外）

依頼書は「変更が必要な場合は`asp3/target/esp32c3_espidf/`側」を想定
していたが，実際の不具合は**アプリ層**（`apps/wifi_scan/wifi_scan.c`）
に存在したため，`asp3/target/`ではなくアプリファイル自体を修正した
（`asp3/asp3_core/`・`hal/`のsubmoduleには触れていない．禁則の対象外）。

1. **クラッシュ本体（308-325行目）**：ブロック全体を
   `#ifdef TOPPERS_ESP32C6_WIFI` ... `#endif`でguard。ファイル内の
   既存の同種C6専用ブロックと同じスタイル。念のため`esp_coex_adapter.c`
   の先例に倣い`if (rd != NULL)`のNULLガードも追加（C6では常に非NULLの
   はずだが，防御的に）。
2. **同一パターンの副次的な未guard箇所を3件追加修正**（同じ
   「C6専用診断のguard漏れ」系統．今回のクラッシュ自体の原因ではないが，
   同じ調査で発見したためついでに是正）：
   - `diag_g_ic_byte`/`diag_wifi_nvs_byte0`の呼出し3箇所
     （post-init/post-set_mode/post-start）：`DIAG_G_IC_BASE`
     （`0x408476b0`）・`DIAG_G_WIFI_NVS_ADDR`（`0x40800890`）は
     C6の`libnet80211.a`ブロブで実測されたアドレス（実施12/13）で
     あり，C3では別ブロブ・別アドレスのため無guardのままでは無関係な
     値を誤ってg_ic/nvsとして表示していた（クラッシュはしないが
     誤解を招く）。`#ifdef TOPPERS_ESP32C6_WIFI`でguard。
   - OSIRATEループ内の`*(volatile uint32_t *)0x600af018U = 0x7U`
     （毎秒書込み）：`0x600af018`＝`MODEM_LPCON_CLK_CONF_REG`は
     C6/H2/H4/H21/C61等の新modem系統にのみ存在する周辺で，
     `hal/components/soc/esp32c3/register/soc/reg_base.h`を確認した
     ところ**ESP32-C3のペリフェラルバスには該当ベースアドレスが
     存在しない**（未使用領域書込み＝この命令ではクラッシュしないが，
     意味のない書込みを毎秒行っていた）。`#ifdef TOPPERS_ESP32C6_WIFI`
     でguard。

### 検証：ビルド成功＋ディスアセンブル確認＋実機2回起動で再現性確認

- ビルド：`cmake --build build/wifi_scan_c3_hw`（`ASP3_TARGET_DIR=
  esp32c3_espidf`, `ESP32C3_WIFI=ON`, `ESP32C3_QEMU=OFF`）が0エラーで
  完了（`riscv32-esp-elf-nm`が必要なためtoolchain binをPATHに追加する
  必要があった）。
- `objdump -d`で修正後の`asp.elf`を確認：`0x4087f954`・`romtbl`・
  対応する`jalr s6`パターンが**完全に消滅**。`esp_wifi_scan_get_ap_records`
  呼出し直後は直接syslog出力→rescanループへ分岐するのみで，以前
  クラッシュ地点だった`0x4200057e`は無関係な別関数
  （`syslog_wri_log`内部）へシフトしている。
- 実機検証（`<MAC-21>`，`--no-stub write-flash`書込み後，
  `reset_and_capture.py`でRTS toggle経由の独立リセット）：
  - **1回目**：`WIFI_EVENT id=1`後にクラッシュせず，`wifi_scan: RESCAN
    N APs (err=0)`ループが継続的に動作（複数回，14〜18 APs）。
  - **2回目（独立した新規リセット）**：`wifi_scan: 18 APs found
    (err=0)` → `wifi_scan: done` → RESCANループが安定して継続
    （13〜20 APsで複数回成功）。
  - 2回とも同一の結果（クラッシュなし・スキャン完走・AP検出成功）で
    **再現性を確認**。

## 結論・まとめ

- **`0x4087f9b0`の正体**：ESP32-C6の`g_phyFuns`（ROM常駐regi2c関数
  ポインタテーブル）の固定アドレス`0x4087f954`のオフセット92
  （idx23＝`rom_i2c_readReg_Mask`）。ESP32-C3には存在しない
  C6固有のアドレスであり，「特定のエントリだけ未設定」という当初仮説は
  誤り——**テーブルそのものがC3には存在しない**。
- **未設定である根本原因**：C6のAGCデッドRX調査で`wifi_scan.c`
  （C3/C6共有アプリ）に追加された診断計装（追記12）が，同ファイル内の
  他の同種C6専用コードと異なり`#ifdef TOPPERS_ESP32C6_WIFI`でguardされ
  ないまま残置されていた。`esp_coex_adapter.c`で以前修正済みの
  「C6専用診断のguard漏れ」と全く同じ系統のバグの，別ファイルでの
  再発。
- **因果テストの結果**：ソース修正（guard追加）→ビルド→実機フラッシュ
  →2回の独立起動で，クラッシュが完全に消滅しスキャンが正常完走する
  ことを実測で確認済み。JTAGでのライブpokeによる因果テストは不要
  だった（ソースレベルで根本原因が完全に説明でき，修正の実機検証で
  確定したため）。
- **target側で修正可能か**：今回の修正は`asp3/target/esp32c3_espidf/`
  ではなく**アプリ層**（`apps/wifi_scan/wifi_scan.c`）で行った。
  submodule禁則（`asp3/asp3_core/`・`hal/`）には抵触していない。
- **次の申し送り**：
  1. 同様の「C6専用診断の未guard混入」が他の共有ファイル
     （`wifi_trace.c`関連や他アプリ）にも残っていないか，念のため
     `grep -rn "0x4087f9\|TOPPERS_ESP32C6_WIFI" apps/ asp3/target/`
     で棚卸しする価値がある（今回は`wifi_scan.c`と
     `esp_coex_adapter.c`の2箇所が見つかった）。
  2. `diag_g_ic_byte`/`diag_wifi_nvs_byte0`関数定義自体は今回
     未使用警告（`-Wunused-function`）が出るようになったが，ビルドは
     エラーにならないため実害はない（C6ビルドでは引き続き使用される）。
  3. C3実機での`wifi_scan`アプリ自体の正常動作（AP検出・RSSI/チャネル
     表示）は今回の副産物として確認できた——別タスクのTLS対応や
     他の機能開発を進める上で「C3のWi-Fiスキャン経路は健全」という
     前提が使える。

## 変更ファイル

| ファイル | 変更内容 |
|---|---|
| `apps/wifi_scan/wifi_scan.c` | C6専用診断（`0x4087f954`のROM PHYFUNS表idx23直呼び／`diag_g_ic_byte`・`diag_wifi_nvs_byte0`の3呼出し／`0x600af018`書込み）を`#ifdef TOPPERS_ESP32C6_WIFI`でguard。C3では実行されないようにした（クラッシュ修正＋副次的な未guard箇所の是正） |

## 検証

- `cmake --build build/wifi_scan_c3_hw`：0エラー（IROM 9.26%／DROM
  10.83%／RAM 88.65%使用）。
- `objdump -d`でクラッシュパターン（`0x4087f954`直読み・対応する
  `jalr`）が消滅していることを確認。
- 実機（`<MAC-21>`，`--no-stub write-flash`＋
  `reset_and_capture.py`によるRTSリセット読み取り）で独立2回起動，
  いずれもクラッシュなし・スキャン完走・RESCANループ継続を確認。

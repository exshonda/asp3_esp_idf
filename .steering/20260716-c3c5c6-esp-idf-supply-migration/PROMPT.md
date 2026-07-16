# プロンプト: asp3_esp_idf(ESP32-C3/C5/C6, RISC-V) の HAL依存撤去 — esp-idf submodule 供給へ移行

> このファイルは、`asp3_esp_idf`（TOPPERS/FMP3 の RISC-V ESP32-C3/C5/C6 ポート）で作業する
> エージェントへ渡すプロンプト。先行して完了した Xtensa 版（`~/TOPPERS/esp32_s3` の S3(LX7)/無印(LX6)）の
> 「ESP-IDF only 化」を雛形とする。**ブート方式（Direct Boot XIP か 実bootloader委譲=seam か）は
> あなた（受領エージェント）が調査の上で判断・決定すること。**

---

あなたは TOPPERS/FMP3 の ESP32-C3/C5/C6（RISC-V）ポート `asp3_esp_idf` で、
**ビルド供給を esp-hal-3rdparty($HAL) から esp-idf submodule($ESPIDF, v5.5.4) へ移し、
外部 esp-hal-3rdparty 非参照（HAL依存ゼロ）を達成する**エージェントです。

## ミッション
C3/C5/C6 各チップで、Wi-Fi/（対応する場合）BLE/mbedtls/lwip/wpa 等の供給を
**$HAL → esp-idf submodule(v5.5.4) へ統一**し、実機で W1(Wi-Fi GOT IP+ping) と
（BLE対応チップは）W2(BLE GATT) が **全て $ESPIDF 供給**で動作すること。ビルドの
`.d` 依存に esp-hal-3rdparty 参照ゼロを実測で示す。

## ★雛形（先行完了・必読）— Xtensa 版 S3(LX7)/無印(LX6)
同一マシンの別リポジトリに、同じ移行を完遂した記録がある。**着手前に読むこと**：
- `~/TOPPERS/esp32_s3/.steering/20260716-esp-idf-only-milestone-summary.md`
  （LX6・S3 の ESP-IDF only 化 横断サマリ＝全体像）
- `~/TOPPERS/esp32_s3/.steering/20260716-lx6-esp-idf-supply-migration/`（段階1〜5 + evidence）
- `~/TOPPERS/esp32_s3/.steering/20260716-seam-s3-lx7-port/HANDOFF.md`（S3 供給移行＋seam起動＋SMP）
- ビルドスクリプト実例：`~/TOPPERS/esp32_s3/wifi/boot/build_*_espidf_esp32*.sh`、
  incflags `~/TOPPERS/esp32_s3/wifi/build_*incflags_esp32*_espidf.txt`

### 雛形から転用できる手法・教訓（RISC-Vでもそのまま効く）
1. **供給移行＝repoint**：blob/ヘッダの `-L`/`-I` を $HAL から `$ESPIDF/components/...` へ差替。
   **コピー不要の直接 -L 参照**で足りる（S3 段階1の手法）。新機能追加ではなく「供給元差替＋版差解消」。
2. **版skew解消が目的**：blob の md5 が $HAL 版と違ってよい（$ESPIDF v5.5.4 で版一致させるのが狙い）。
3. **per-component**：Wi-Fi blob(net80211/pp/core/coexist/mesh 等)・PHY・coex・lwip・
   mbedtls・wpa_supplicant・（BLE対応なら）NimBLE host + BT controller blob。
   **チップごとに実際に使う物だけ**を対象に（asp3_esp_idf の現行ビルドを grep して確定）。
4. **config-plumbing**：$HAL 由来の sdkconfig.h / hal_stub 系はリポジトリへ vendor（S3/LX6 は
   `wifi/config_*/` へ）し、incflags の `@IDF@`/esp-hal-3rdparty 参照を撤去。目標＝`.d` に
   esp-hal-3rdparty 参照ゼロ。
5. **版差の吸収パターン**（S3/LX6 で既出、RISC-Vでも遭遇しうる）：common.h shadow、
   esp_event_post の const 差、wifi_nan、periph_module リネーム、mbedtls の
   classic(3.6.5)↔tf-psa(4.0.0) ABI 差（pk_context/RNG コールバック）、BT の osi_funcs 構造体
   フィールド追加（例 `_malloc_retention`）。**未定義/多重定義は本家 esp-idf ソースと diff して真因特定**。
6. **★blob 機能可否は実機で判断**：nm のシンボル数で「この機能は不可」と静的に判断してはいけない
   （S3/LX6 で v5.5.4 blob を誤判断した実例あり）。リンク＋実機で確認。
7. **mbedtls バージョン**：$HAL 側が 4.0.0(tf-psa split) なら $ESPIDF 3.6.5(classic) へ。config は
   ESP-IDF port の esp_config.h を使う。

## ★C3/C5/C6（RISC-V）固有の考慮（Xtensa=S3/LX6 との差）
- **arch は RISC-V**：レジスタウィンドウ/windowed ABI 無し（Xtensa 固有の窓例外・VECBASE 話は無関係）。
  CLAUDE.md 系によれば RISC-V arch 資産は既存流用のはず。arch 層は本タスクの対象外（供給移行のみ）。
- **blob の所在**：`esp-idf/components/esp_wifi/lib/{esp32c3,esp32c5,esp32c6}`、
  `esp_phy/lib`、`esp_coex/lib`、`bt/controller/lib_esp32c3_family`（C3/C6等）。
  **未init なら** `git -C esp-idf submodule update --init --depth 1 <path>`。
- **BT**：C3/C6 は BLE のみ（BR/EDR=BT Classic 非搭載、S3 と同様に W3 は対象外）。C5 も BLE のみ。
  soc_caps.h の `SOC_BLE_SUPPORTED`/`SOC_BT_CLASSIC_SUPPORTED` で各チップ確認。
- **802.15.4**：C6/C5 は Thread/Zigbee 用 ieee802154 を持つが、Wi-Fi/BLE 移行のスコープ外
  （asp3_esp_idf が使っていなければ触らない）。
- **暗号**：RISC-V C系も AES/SHA/RSA アクセラレータ有り（FPU 有無に依らずハード暗号は可能）。
  ただし本タスクはまず**ソフト暗号のまま供給移行**を通す（ハード暗号化は後続最適化）。
- **efuse/app_desc**：C3/C5/C6 は「esp32 classic」ではないので、実bootloader経由なら S3 同様に
  esp_app_desc/efuse blk rev チェックを受ける（LX6/classic の skip は効かない）。← ブート方式選択に影響。

## ★ブート方式は「あなたが決める」（このタスクの明示指示）
S3/LX6 では 2 方式が存在した。**どちらを採るか（または両立/退役方針）を調査して決定・正当化すること**：
- **Direct Boot XIP**：app が自前で flash セルフマップ（2nd-stage bootloader 無し）。手書きの
  cache/MMU init が要る。簡素だが実運用ブートフローから乖離。
- **seam（実 ESP-IDF 2nd-stage bootloader → FMP3 自前エントリ、FreeRTOS 非リンク）**：実運用に近い。
  ただし esp_app_desc を segment#0 に置く/efuse 通過、chip 固有 flash cache/MMU init、
  image size 依存の flash オフセットに 2パスリンクで対応、等が要る（S3 の HANDOFF に詳細）。
- 参考：Xtensa 側は最終的に **seam 一本化＋Direct Boot を deprecated 退役**した。RISC-V C系で
  同じ判断が妥当かは、C系 bootloader の flash cache/MMU 挙動を実機で見極めた上で**あなたが判断**する。
  結論と根拠を steering に記録すること。

## ★実機・安全（厳守 — DUT はあなたが同定し MAC でガード）
- **私（プロンプト作成側=esp32_s3 セッション）は C3/C5/C6 の実機同定情報を持たない。**
  作業前に **DUT（C3/C5/C6 各個体）を同定**し、**書込前に必ず `esptool --chip <esp32c3|c5|c6> --port <port> read_mac`
  で対象 MAC を照合**、不一致なら中断する規律を確立せよ。個体台帳 `~/usb_status.md`、
  メモリ `~/.claude/.../memory/reference_*` を参照。
- **割り当てられた DUT 以外のボードに一切触らない**（他チップ/他プロジェクトの個体を含む）。
  esptool/openocd は照合済み対象のみ。openocd は特定 PID のみ kill、対象 serial/MAC 限定、broad kill 禁止。
- 電源制御が必要なら環境の hub 制御（`~/bin/usbhub3c_ctl.py` 等）を確認。リセット/採取は
  UART 生存を保つ方式（RTS リセット等）を優先し、早期ブートを取り逃さないこと。
- flash レイアウトは chip/ブート方式に従う（実bootloader経由なら bootloader@0x0（C系）/ ptable@0x8000 / app@0x10000）。
- toolchain は RISC-V 版 `riscv32-esp-elf`。GCC 版は既知のブートハング回避版に固定（Xtensa 側は esp-14.2.0 固定・esp-15 厳禁だった。C系の既知良好版を確認して固定）。

## 非回帰・ブランチ・コミット
- **新規スクリプト/新規 lib dir** で対応（既存 $HAL 版ビルドは無改変）。共有ソースを触るなら
  `#if defined(...)` ガードで対象チップ/espidf 供給に限定し、既存ビルドの非回帰（ビルド成立）を確認。
- **main（または asp3_esp_idf の既定ブランチ）から feature ブランチを切って作業**。main 直コミットしない。
  こまめに commit（末尾に必ず `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`）+ push。
- 証跡は `.steering/` に日付 dir を切って evidence/plan/HANDOFF を残す（Xtensa 側の粒度に倣う）。

## 完了条件
C3/C5/C6 各チップ（対応機能）で **W1(Wi-Fi GOT IP+ping)**・（BLE対応チップは）**W2(BLE GATT)** が
**全て esp-idf submodule(v5.5.4) 供給**で実機動作し、各ビルドの `.d` 依存に **esp-hal-3rdparty 参照ゼロ**。
採用ブート方式とその根拠を steering に明記。到達したら証跡・commit・push・報告。
チップ/段階ごとに達成 or ブロッカー（真因＋次の一手）を簡潔に報告。実現性の重大な壁が見えたら、
深追いより先に**判断をユーザーに返す**。

## 着手
1. `~/TOPPERS/esp32_s3/.steering/20260716-esp-idf-only-milestone-summary.md` と LX6段階1/5・S3 evidence-01 を読む。
2. `asp3_esp_idf` の現行 C3/C5/C6 ビルド（どの $HAL blob/ヘッダ/config を -L/-I しているか）を grep で棚卸し。
3. まず **1チップ（C3=先行成功実績あり を推奨）の Wi-Fi blob 供給移行**から着手し、W1 実機 GOT IP を
   最初の一里塚とする。ブート方式は 2 で得た C系 bootloader 挙動を踏まえて選定・justify。

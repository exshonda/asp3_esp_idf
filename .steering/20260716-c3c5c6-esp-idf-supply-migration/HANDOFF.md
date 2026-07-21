# 継続エージェントへの引き継ぎ（2026-07-16 時点・別PCで再開する用）

このファイル自体が**継続エージェントへ渡すプロンプト**です。上から順に読んでください。

---

## 0. あなたのミッション

`.steering/20260716-c3c5c6-esp-idf-supply-migration/PROMPT.md` が正本（**最初に読む**）。
要旨＝**asp3_esp_idf（TOPPERS/ASP3, ESP32-C3/C5/C6, RISC-V）のビルド供給を
esp-hal-3rdparty($HAL) から esp-idf submodule(v5.5.4) へ移し、HAL依存ゼロを達成する**。
完了条件＝各チップで **W1(Wi-Fi GOT IP+ping)**・**W2(BLE GATT)** が全て esp-idf 供給で実機動作し、
ビルドの依存に esp-hal-3rdparty 参照ゼロ。

## 1. 環境セットアップ（別PC）

```bash
git clone <this repo> && cd asp3_esp_idf
git checkout claude/c5-espidf-supply-migration      # 作業ブランチ（push済）
git submodule update --init --recursive             # esp-idf は shallow/v5.5.4タグ
```
- submodule pin：`asp3/asp3_core`=`9904a44`(feat/esp32c6) ／ **`esp-idf`=`735507283d`（v5.5.4 タグ）** ／
  `hal`=`b90b1837` ／ `lwip`=`77dcd25a`。全て正規 URL・リモート存在確認済。
- **asp3_core の push は SSH 必須**（HTTPS は認証不可）：`git push git@github.com:exshonda/asp3_core.git HEAD:feat/esp32c6`
- toolchain：Espressif `riscv32-esp-elf` **esp-15.2.0**（`~/tools/espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/...`）
  ／ xpack `riscv-none-elf-gcc` 15.2.0（`~/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin`、正典）。両方 C5 で実績あり。
- **ビルドは全て cmake**（本 repo の規約＝CLAUDE.md「asp3_core 本体の CMake を `ASP3_TARGET_DIR` で駆動」）。
  `tmp/*.sh` は cmake のラッパに過ぎない。雛形の `~/TOPPERS/esp32_s3` は `.sh` 方式だが**持ち込まない**。

## 2. 現在地（何が終わって何が残っているか）

| 項目 | 状態 |
|---|---|
| **C5 WiFi 供給移行** | **✅完了**。`ninja -t deps` で **hal 参照 0**、実機 **scan 20 AP**・**W1（GOT IP + ping 30/30）** |
| **C5 BT 供給移行** | **✅完了**（2026-07-17 訂正）。**D-1**（HCI Command Complete）・**W2**（BlueZ で `ASP3-C5-BLE` → connect → ServicesResolved）を**真cold で達成**＝**evidence-c5-05 §4／§6.1**。さらに **D-2c/D-2d も真cold で達成**（`0xABF1` READ／`0xABF2` NOTIFY／`0xABF3` WRITE／`0xABF4` 暗号必須 READ）＝**evidence-c5-05 §8／§9**。hal 参照 **0**・外部 `~/tools/esp-idf` 参照 **0**。既定 `ASP3_BT_IDF_V554=ON` を維持 |
| **ブート方式** | **✅決定＝Direct Boot 継続**（seam は C5 で成立するが不採用。可逆 option `ASP3_SEAM_BOOT` 既定 OFF で温存） |
| **pmu_init 移植** | **✅完了**（stock を verbatim コンパイル）。**RF電源 `0x600B0014` 到達**。可逆 option `ASP3_C5_PMU_INIT` **既定 OFF**（昇格は保留＝§4-2） |
| **C6** | **✅完了**（2026-07-17 訂正。旧記述「未着手」は**偽**）。WiFi＝hal 参照 **0**・**W1 を真cold で達成**（GOT IP＋**ping 36/36・失敗0・切断0**）＝**evidence-c6-06**。BT＝**hal 経路そのものを撤去**して C5 と同型化（`ASP3_BT_IDF_V554` 既定 ON／`ESP32C6_BT_SM` 既定 ON）＝**evidence-c6-09／-10**。**W2＝iPhone・Android とも OK**＝**evidence-matrix-ble-central-supply** |
| **C3** | **✅WiFi 完了／BT は「hal 参照 0 達成」と「既定にしてよい」が別**（2026-07-17 訂正。旧記述「未着手」は**偽**）。WiFi＝hal 参照 **0**・**W1 を真cold で達成**（GOT IP＋**ping 21/21・49/49・NG 0**、独立 2/2）＝**evidence-c3-02**。BT＝供給移行自体は成立（hal 参照 **0**・adv／D-2b 到達）だが、**同一条件の真cold A/B で esp-idf 供給は bond 失敗 2/2・hal は成功 2/2**＝**evidence-c3-03 §5** ⇒ **BT 既定は hal**（`ASP3_ESPIDF_SUPPLY` は `ESP32C3_BT=ON` のとき既定 **OFF**＝`target.cmake:92-100` の `if()` で計算） |

証跡＝`.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-c5-01〜05`・**evidence-c3-01〜09**・**evidence-c6-01〜14**・**evidence-matrix-ble-central-supply**。
~~**evidence-05 §6 は記録未完**（親がサブエージェントを停止したため）。~~
**★2026-07-17 訂正：これも偽**。`evidence-c5-05` の §6（結論・申し送り）は **`:348` に存在し完結**しており、
同ファイルはその後 §7〜§12（D-2c/D-2d 予測・実機測定・bond 永続化・`reason=517` 追跡）まで書かれている。

## 3. ★★記録済み結論のうち「実測で覆ったもの」（鵜呑み禁止）

**このプロジェクトの memory / docs には、後から実測で否定された記述が複数ある。設計の土台にするなら必ず自分で実測して裏を取ること。**

1. **`~/tools/esp-idf` は v5.5.4 タグではない**。実体は **`v5.5.4-1169-gbb2188bf`（release/v5.5 の先端）**。
   `version.h` が 5.5.4 と表示するため気づけない。**これが provenance の罠の元凶**。
   → **submodule `esp-idf/`（`735507283d`）が「真の v5.5.4」**。
2. **WiFi blob は v5.5.4タグ版と +1169 版で全て別物**（libnet80211 `3996ba79` vs `c7b13c02` 等）。
   ＝**過去の実機検証（20 AP scan 等）の多くは +1169 blob でのもの**。
3. **BT blob の真相（evidence-05 §2 実測）— 記録済み結論が反証された**：
   - `+1169` ≡ **v6.1**（libble_app/libphy/libbtbb の 3/4 一致）
   - **真の v5.5.4 タグ ≡ hal（4/4 バイト一致）**
   - ∴ **「WiFi・BT 双方 v5.5.4＝統一完了」という記録（memory `c5c6-bt-blob-v554-feasibility`）は成立していなかった**
     （実際は WiFi=真のv5.5.4 ／ BT=+1169≡v6.1）。**BT の submodule 供給移行によって初めて実際に統一される**。
4. **`0x600B000C = 80000000` は「POR＝MODEM ICG 未設定」ではない**。実体は **`code=2=ACTIVE`**
   （＝pmu_init が書くのと同じ値）。evidence-03 の記述を evidence-04 が訂正済。
5. **memory `c5-wifi-osi-abi-he-field` の「HE フィールドを足す」は v5.5.4タグでは有害**
   （+1169 blob には正しかったが、タグ blob では `_magic` が 484→488 にずれて `esp_wifi_init 0x102` を招く）。
6. **memory `c5-wifi-modem-domain-unpowered` の「残壁＝RF/アナログ電源＝pmu_init の残り」には限定が付く**：
   pmu_init は **PHYデジタル・MODEM_SYSCON を 1 ビットも動かさない**（仕様どおり＝`modem_clock.c` の担当）。
7. **`esp_shim_modem_icg_init()` は pmu_init と無関係に元々冗長**（4アーム A/B で実証。§5 の「決定的対照」参照）。

## 4. 次の一手（優先順）

### 4-1. ~~【最優先】C5 BT 供給移行の実機検証~~ → **★2026-07-17：実施済み・完了**
> **この節はもう「次の一手」ではない**（記録として残す）。実測：**D-1・W2 とも真cold で達成**し、
> **D-2c/D-2d まで到達**（`evidence-c5-05` §4／§6.1／§8／§9）。下の「特に確認すべきリスク」
> （＝真の v5.5.4 タグ blob ≡ hal blob なので実施09 のハングが再現するのでは？）は
> **実機で再現せず**（`evidence-c5-05` §5＝「実施09 を C5#1 で反証」）。
> また **「evidence-05 §6 の記録も未完」は偽**＝§6 は `:348` に存在し完結している。

サブエージェントが `67d55cb`/`e04b82c` で **BT 供給を submodule（真の v5.5.4 タグ）へ移行しビルド復旧**させたが、
**実機検証が完了する前に親が停止した**（＝当時の状況。上記のとおり後日完了）。

**★特に確認すべきリスク**：§3-3 のとおり **真の v5.5.4 タグ BT blob ≡ hal blob（バイト一致）**。
つまり今の BT は「実質 hal blob」に戻っている。ところが記録には
**「hal(v8) の libphy.a は eco2 C5 の PHY RX 較正で収束せずハングする（実施09）」**がある。
**ただしこの実施09 の結論は memory `c5-wifi-hal-v8-scan-works` が「相関≠因果」として既に反証済**
（実施48-52 で hal(v8) blob は C5 で scan/connect/DHCP 全て実証）。
⇒ **どちらが正しいかは実機で決めるしかない**。予測を先に固定してから測ること。

- `bt_smoke_c5`（D-1＝controller のみ）を**真の cold** で：`esp_bt_controller_enable` → HCI Reset →
  **Command Complete** まで到達するか。
- `ble_host_smoke_c5` で **BlueZ から `ASP3-C5-BLE` が見えるか**（W2 相当）。
- **`ninja -t deps` で BT の hal 参照数**を実測（WiFi 側 0 を壊していないことも再測）。
- 通らなければ `-DASP3_BT_IDF_V554=OFF`（外部 v6.1 fallback）へ戻せる＝可逆。

### 4-2. pmu_init の BT A/B →`ASP3_C5_PMU_INIT` 既定 ON 昇格の判断
`15826f8` でガードを `ESP32C5_WIFI=ON **or** ESP32C5_BT=ON` に開放済＝**BT でも A/B 可能**になった。
**昇格を見送っている理由**＝(a) WiFi では機能上の便益が**実測ゼロ**（4アーム全て 20 AP）、(b) BT 未検証、(c) 壊さないこと優先。
BT で便益が出れば昇格材料になる。**出なければ「OFF のまま」も正しい結論**。

### 4-3. ~~C3 / C6 への横展開~~ → **★2026-07-17：実施済み**（§2 の表を参照）
> **この節はもう「次の一手」ではない**。C3・C6 とも横展開は完了しており、以下は
> **「その時どう移したか」の設計メモとして**読むこと（実際にこの型で移された）。
> 到達点＝§2 の表（C6＝WiFi/BT とも完了・W1/W2 真cold ／ C3＝WiFi 完了・**BT 既定は hal**）。

C5 で確立した型：**`ESP_SUP_DIR` / `ESP_SUP_HAL_<x>`**（`target.cmake`）＋ `ASP3_ESPIDF_SUPPLY`（既定 ON）。
- **構造的知見**：hal と esp-idf は**同じソースを持つがヘッダ名だけリネームされている**
  （`rtc_timer_hal.h`≡`lp_timer_hal.h`、`timg_ll.h`≡`timer_ll.h`）。**ヘッダとソースを揃えて移せばリネーム問題は消滅**する。
  これを知らずにヘッダだけ移すと詰む（前ラウンドの詰みの真因）。
- **hal 固有の8コンポーネント**（`esp_hal_clock`/`esp_hal_timg`/`esp_hal_rtc_timer`/`esp_hal_pmu`/
  `esp_hal_gpio`/`esp_hal_security`/`esp_hal_ana_conv`/`esp_hal_usb`）は **esp-idf に存在しない**
  ＝hal が IDF の単体 `hal` コンポーネントを分割したもの。`ESP_SUP_HAL_<x>` で吸収する。
- **mbedtls は版ダウン**：hal=**4.0.0**（tf-psa 分離）→ esp-idf v5.5.4=**3.6.5**（classic）。
  config は本家 port の `esp_config.h` へ。`common.h` shadow は**版ダウン固有の新規リスク**
  （`mbedtls/library` を wpa の**後ろ**に置いて解決）。**mbedtls と wpa は必ず揃えて移す**。
- **`pmu_init` を積むなら**：ASP3 の `hardware_init_hook()` は **`.data` 初期化より前に走る**
  （`start.S:120`→`127` bss→`143` data）＝初期化子つき static が未初期化ゴミになる。
  stock の **weak 拡張点を strong 上書き**して回避（stock ソースは無改変のまま）。**C3/C6/S3 で必ず同じ罠を踏む**。

### 4-4. その他
- 冗長な `esp_shim_modem_icg_init()` の削除（**なぜ冗長になったかの特定が先**）。
- 外部レビュー `docs/blob-unify-v554-review.md` の残項目：★B1（**C6 の BT 既定 flip が hal に着地していて効いていない**）、
  ★B2（C6-BT の可逆性の穴）、★A（C3 bond 失敗の因果未確定）、★E（实施92 の帰属 rigor）。
- **C3 BT は hal 版のまま**＝**結論は真**（実測：`ESP32C3_BT=ON` のとき `ASP3_ESPIDF_SUPPLY` の既定は
  `target.cmake:92-100` の `if()` で **OFF** に計算される）。
  ★**ただし括弧内の «理由» は 2026-07-17 の実測で偽と判明したので訂正する**：
  - 真の v5.5.4 タグでの真cold A/B が実際に観測した失敗モードは **`AuthenticationCanceled`**（失敗 2/2・hal は成功 2/2）
    ＝**`AuthenticationTimeout` ではない**（`evidence-c3-03` §5／`:167`）。
  - しかも旧記録の `AuthenticationTimeout` は **`+1169`（≡v6.1）に対する測定**であり、
    **「v5.5.4 切替」の根拠には一度もなっていなかった**（`evidence-c3-03:242-243` が
    「失敗モードが違う・別現象の可能性・**同一視しない**」と明記）。
  ⇒ **「hal 既定を維持する」判断は正しいが、その根拠は «真タグでの `AuthenticationCanceled` 2/2» であって
    «`AuthenticationTimeout`» ではない**（`docs/blob-unify-v554.md:493/502` 等の旧記述を引くときは要注意）。

## 5. ★測定・実験の鉄則（このプロジェクトで実際に事故ったもの）

**正本＝`memory/feedback_hardware_investigation_rigor.md`。以下は実際に踏んだ地雷。**

1. **決定的対照を省くと偽の成功譚になる**：「シムを外しても動いた＝pmu_init が置き換えた」は**誤り**だった。
   4アーム目（**pmu_init OFF × シム OFF**）を実際に走らせて初めて「シムは元々冗長」と判明。**相関≠因果**。
2. **`rts_boot_capture.py` は真の cold 観測に使えない**：RTS パルスが**観測したい cold boot を中断して warm を観測させる**。
   → **`tmp/c5_cold_passive_capture.py`（受動採取・リセットしない）を使う**。C5 の USB-Serial-JTAG はホスト非接続でも出力をバッファする。
3. **`.d` は `ninja -t deps` で測る**。`find -name '*.d'` は **0 と誤測**する（CMake+Ninja は `deps=gcc` で `.d` を削除）。
4. **`grep -a` 必須**：採取ログにバイナリバイトが混入すると grep が binary モードになり**全マッチが無言で消える**。
5. **電源操作は台帳照合＋読み戻し**：親が古い記憶のポート表を信じて `off 3` を（出力を `/dev/null` に捨てて）実行し、
   **一度も電源が切れていないのに「cold した」と誤認**した事故がある。**`~/usb_status.md` と `usbhub3c_ctl.py status` で実照合**し、
   **`off 5` 後に `ls /dev/serial/by-id/ | grep -c C8:94` = 0 を読み戻す**こと。
6. **latch を code のせいにしない**：`rst:0x7 TG0_WDT` ＋ `Core0 Saved PC:0x40038598`（**red-herring の ROM PC**）は
   **電源再投入で解消**する既知現象（`memory/c5-latched-board-state`）。PMU/ブート系の実験で頻発する。
7. **CP2102N 採取で `rts=True` を残すと EN を assert 保持**し、ログが毎回同じ位置で切れる（**DUT ではなくハーネス由来**）。
   これで seam を一度「失敗」と誤断しかけた。**ログが途中で切れたら DUT より先にハーネスを疑う**。
8. **blob の機能可否を nm のシンボル数で静的に判断しない**。**リンク＋実機で判断**。
9. **有用な手法**：blob は**自身がビルドされたヘッダの md5 を埋め込んでいる**（`g_wifi_osi_funcs_md5` 等）→
   **blob が要求するヘッダ版が確定できる**。ABI 疑義は **blob の逆アセンブル**（構造体オフセット直読み）が決定的。
10. **引き継がれた「事実」も疑う**：親が「override ヘッダは v5.5.4タグと中身同一＝非原因」と**断定して渡したが完全な誤り**で、
    それ自体が `esp_wifi_init 0x102` の真因だった（diff のコメント行しか見ていなかった）。

## 6. 実機・安全（厳守）

- **DUT は割り当てられたボードのみ。他に一切触らない。** 台帳＝**`~/usb_status.md`（実機を触る前に必ず参照）**。
  2026-07-16 時点：port1=ESP-WROVER-KIT／port2=P4-B／**port3=S3-B（⚠️絶対に触らない）**／port4=Unit PoE-P4／
  **port5=ESP32-C5 #2（本プロジェクトの DUT）**。**C3・C6 は現在このPCに未接続**（別PCでは要確認）。
- **C5#2**：BASE MAC **`<MAC-39>`**、native USB-JTAG=`ttyACM5`
  （`/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D0:CF:13:F0:C8:94-if00`）、CP2102N UART=`ttyUSB2`。
- **書込前に必ず `esptool --chip esp32c5 --port <port> read-mac` で MAC 照合**。不一致なら中断。
- **電源（真の cold）**：`python3 ~/bin/usbhub3c_ctl.py off 5` → 10秒 → `on 5`（エージェント権限で動作確認済）。
- **Wi-Fi 認証情報**：**ビルド注入のみ**（`CMAKE_C_FLAGS` 経由。**`-DWIFI_SSID=` は cmake に直接渡しても効かない**＝
  cmake plumbing 不在で既定 `"your-ssid"` のまま `reason=201` になる）。**docs/.steering/evidence/commit/memory/ログ/ファイルに絶対に残さない**
  （`wifi_connect.c` は SSID を syslog に出す＝evidence 化時はマスク）。

  ★★**混入チェックの方法（2026-07-17 訂正。旧記述の `git diff` は «盲点のある検査器» だった）**：
  - **`git diff` を使うな**——**untracked ファイルを見ない**。**新規 evidence ファイルは `??` 扱いで射程外**になり、
    実際にこれで漏洩した（「0 と測れた」は真、「安全だ」は偽）。**commit 対象ファイルを直接 grep しろ。**
  - **`git log -S` は pickaxe＝blob/差分の検索で、commit message を見ない**。**message は `git log --grep`**。
    両方を測れ（履歴の書換えも `--replace-text`＝blob と `--replace-message` の**両方**が要る）。
  - **禁止語を «部分文字列» で当てろ**（完全一致は分解形＝接頭辞/接尾辞単体を取りこぼす）。
  - ★**チェックしたことを記録する文章に、チェック対象の文字列を書くな。**
    **「漏洩を説明する文章それ自体が漏洩経路」**——この事故は**3回**起きた（うち1回は「除去した」と記録する
    commit message の中で置換順序を説明するために値を書いた）。**被害報告は抽象レベルでのみ書く。**

  ★**訂正（2026-07-17）：旧記述の「現在まで混入 0 件を維持」は事実ではなかった。**
  実測の結果、**AP の SSID が複数 commit・複数リモートブランチに混入し public リポジトリへ push 済み**だった。
  **対処と現状（★「除去済み」という完全性の断定はしない。指標で書く）**：
  - **パスワード**：`git log --all -S<部分文字列>` と `--grep` の**両方**で **blob 0 / message 0**（履歴含む）。
    完全形は**一度も commit されていない**。**3回の履歴書換え後、GitHub 実体を新規 clone して再確認済み。**
  - **W1 で使った自宅 AP の SSID**：同じ2系統で **blob 0 / message 0**（3回目の書換えで message まで到達）。
  - ★**それ以外の SSID（機関網のスキャン結果等）は «未除去»。** 検査指標＝
    `git grep <name> HEAD` ／ `git log --all -S<name>` ／ `git log --all --grep=<name>`。
    **重要度が低い（機関網は公開ブロードキャスト・パスワードを伴わない）と判断してユーザーが除去を見送った**
    ＝**「SSID は全部消えている」と読むな。** 新たに書き加えないこと。
  ⇒ **この種の「〜を維持」「除去済み」という «完全性の自己申告» を、検算せずに信じるな。**
    **HANDOFF §5-10 の実例が §6 自身に在り、その訂正文（本項の旧版）もまた同じ誤りを犯していた。**
    **完全性を主張するな。«何を・どの指標で測ったか» を書け。**

  **AP 環境は変わりうる。まず `wifi_scan` で実在する SSID を確認してから W1 を組む**。`reason=201` を回帰と誤断しない。

## 7. ビルド（既知良好）

```bash
export PATH="$HOME/tools/espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:$PATH"
cmake -S asp3/asp3_core -B build/<tag> -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake -DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf- \
  -DASP3_TARGET=esp32c5_espidf -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c5_espidf \
  -DESP32C5_WIFI=ON -DESP32C5_CONSOLE=usbjtag \
  -DASP3_APPLDIR=$PWD/apps/wifi_scan -DASP3_APPLNAME=wifi_scan \
  -DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration
cmake --build build/<tag>
cd build/<tag> && ninja -t deps | grep -c '/asp3_esp_idf/hal/'   # ← hal 参照の実測（0 が目標）
```
- BT：`-DESP32C5_BT=ON -DASP3_APPLDIR=$PWD/apps/bt_smoke_c5 -DASP3_APPLNAME=bt_smoke_c5`
  （BLE は `apps/ble_host_smoke_c5`。`ESP32C5_BT_NIMBLE` は自動 ON）
- W1：`-DASP3_APPLDIR=$PWD/apps/wifi_dhcp -DASP3_APPLNAME=wifi_dhcp`
- PMU プローブ：`apps/boot_pmu_probe`
- esptool：`~/tools/espressif/python_env/idf6.1_py3.12_env/bin/esptool --chip esp32c5`

**主な option（すべて可逆）**：
| option | 既定 | 意味 |
|---|---|---|
| `ASP3_ESPIDF_SUPPLY` | **ON** | esp-idf submodule 供給。**OFF で hal へ «完全復帰» するのは WiFi/素のビルドだけ**（実測：hal 7357／esp-idf 0）。★**BT ビルドでは OFF は «完全復帰» しない**＝基盤だけ hal に戻り、BT ツリーは `ASP3_BT_IDF_V554`（既定 ON）に従って esp-idf のまま＝**黙って混成（MIXED）**（実測：hal 1594／esp-idf 119、うち 88 が `components/bt`）。C5 `esp_bt.cmake` に `ESP_HAL_DIR` は **0箇所**＝**hal の BT 経路は存在しない**（2026-07-17 訂正） |
| `ASP3_BT_IDF_V554` | **ON** | BT も submodule（真の v5.5.4）。OFF で外部 v6.1 fallback |
| `ASP3_SEAM_BOOT` | **OFF** | seam 起動（実 bootloader 経由）。OFF=Direct Boot |
| `ASP3_C5_PMU_INIT` | **OFF** | stock pmu_init を起動経路で実行 |
| `ASP3_WIFI_BLOB_HAL` | OFF | WiFi blob を hal へ戻す |
| `IDF_V554` | submodule | `-DIDF_V554=<path>` で外部 tree へ A/B 可能 |

## 8. 運用

- branch `claude/c5-espidf-supply-migration`（main へは未マージ。main より大きく先行）。
- **こまめに commit + push**。commit 末尾に
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>` と `Claude-Session: <URL>`。
- 証跡は `.steering/20260716-c3c5c6-esp-idf-supply-migration/` に `evidence-*` として残す。memory も更新（`MEMORY.md` に1行ポインタ）。
- **禁則（CLAUDE.md）**：`asp3/asp3_core/`・`hal/` を直接編集しない（差分は `asp3/target/` にラッパ/シム）。
  カーネル内で動的メモリ確保をしない。
- **重大な壁が見えたら、深追いより先に判断をユーザーへ返す**。**無理に成功を作らない**（「できない／こういう条件が要る」も価値ある回答）。
</content>

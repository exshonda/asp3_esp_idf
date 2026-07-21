# 相談：ESP32-C3 の BLE bond が **Android 相手のときだけ** `ENC_CHANGE status=13`（SM 30s タイムアウト）で失敗する

あなたはこのリポジトリを閲覧できる上級エンジニアです。**NimBLE の SMP／LE Security の
実装に踏み込んだ診断**をお願いします。日本語で回答してください。

## 0. リポジトリの前提（1分で）

- **TOPPERS/ASP3**（μITRON系RTOS）＋ **ESP-IDF v5.5.4 の下層のみ**（PHY・Wi-Fi/BT blob・
  NimBLE ホスト）を ESP32-C3/C5/C6 で動かす統合リポジトリ。
  **ESP-IDF のスタートアップと FreeRTOS はリンクしない**（ブートは ASP3 自前の Direct Boot）。
  OSアダプタ（os_adapter）は ASP3 プリミティブで自前実装（`esp/common/wifi/esp_shim*.c`）。
- 供給元は **esp-idf submodule 一本**（`esp-idf/`＝v5.5.4 タグ pin）。`hal` は撤廃済み。
- toolchain は **`riscv32-esp-elf` esp-14.2.0_20260121 固定**（esp-15 は C3 の bond を壊す実測あり）。
- 全体像＝`README.md`、規律＝`CLAUDE.md`、始め方＝`docs/onboarding.md`。

## 1. 相談したい症状（**これが本題**）

C3 が **BlueZ 相手では bond 成立するのに、Android 相手では失敗する**。

```
DUT ログ（受動採取＝リセットせずに捕獲）:
  ble_host_smoke: GAP CONNECT status=0 handle=1
  ble_host_smoke: BT5 security_initiate(slave SecReq) rc=0     ← SecReq 送信は成功
  ble_host_smoke: GAP ENC_CHANGE status=13                     ← BLE_HS_ETIMEOUT（30s SMタイムアウト）
  ble_host_smoke: sec_state enc=0 auth=0 bond=0 keysz=0
```

Android 側 btsnoop（`adb bugreport` → `FS/data/log/bt/btsnoop_hci.log`）：
接続 → 25秒後に `DISCONNECT reason=0x16`（Local Host＝スマホが切断）→ 再接続 →
**`DISCONNECT reason=0x08`（supervision timeout＝リンクが物理的に沈黙）**。

| central | 結果 |
|---|---|
| **BlueZ (`hci0`)** | **bond 成立**。さらに真cold（物理電源断）を跨いで鍵を復元し暗号化再開まで実証 |
| **Android (Galaxy `SM_F966Q`)** | ペアリング要求は来るが **`ENC_CHANGE status=13`** で不成立 |

## 2. **既に潰した仮説**（同じ道を辿らないでください）

すべて実測で否定済み。証跡＝`.steering/20260721-docs-onboarding/evidence-05-c3-smp-notsup.md`。

| # | 仮説 | 否定の根拠（実測） |
|---|---|---|
| H1 | **SM がコンパイルアウトされている** | ★**これは «真» だった。既に修正済み**（§3）。`esp/c3/esp_bt.cmake` で SM を 0 に蓋する `else()` が `ESP32C3_BT_SM` ではなく **`ESP32C3_BT_CONN_WD` の else に繋がっていた**ため、`BT_SM=ON` でも常に `MYNEWT_VAL_BLE_SM_LEGACY/SC=0` になっていた。症状は `security_initiate` が **`rc=8`(ENOTSUP)**。修正後は `rc=0` になり **BlueZ では bond 成立**。**Android でもペアリング要求が出るようになった**（前進）。現在の症状はこの修正 «後» のもの |
| H2 | 旧真因の再発＝**`MYNEWT_VAL(BLE_HS_PVCY)=0`** で responder の Identity 鍵配布がコンパイルアウト（memory `c3-ble-d2d-gatt-notify-sm` が記録する過去の真因） | **否定**。`ble_sm.c` と**同一のコンパイルフラグ**で `_Static_assert(MYNEWT_VAL(BLE_HS_PVCY)==1)` と `_Static_assert(MYNEWT_VAL(BLE_SM_SC)==1)` を通した＝**両方 1 が実効** |
| H3 | **mbuf 枯渇の wedge**（`.steering/…/evidence-rc-c3-P1-wedge-mbuf-exhaustion.md` が記録する既知機構） | **否定**。JTAG で捕獲すると `msys_1 free=12/12`（min=11）・`msys_2 24/24`・`g_conn_handle=0xffff`・`notify_sent/fail=0/0`・`gap_conn/disc=1/1`（切断イベントの欠落なし）＝**記録済み wedge とは別物** |
| H4 | 接続そのものが失敗している | **否定**。`connect` 単体は再現性をもって成功（`ServicesResolved: yes`）。初期に出た `ConnectionAttemptFailed` は **BlueZ アダプタの状態**が原因で、power cycle で解消（環境要因） |
| H5 | bond ストアの自前実装（本セッションで追加）が原因 | **否定**。`ESP32C3_BLE_STORE_FLASH=OFF`（＝**本実装を一切含まない**従来の RAM store）でも同一症状。なお本実装は BlueZ 相手では**真cold を跨いだ鍵復元まで成功**している |

## 3. 見てほしい場所

- **アプリ**：`apps/ble_host_smoke_c3/ble_host_smoke_c3.c`
  - `bt5_security_tick()`（≈411行）＝**接続5秒後**に `ble_gap_security_initiate()` を撃つ
    （ペリフェラル発の Security Request）。S3 の実装からの逐語移植。
  - `ble_hs_cfg.sm_*` 設定（≈875行）＝`sm_io_cap=NO_IO`・`sm_bonding=1`・`sm_mitm=0`・
    `sm_sc=1`・`sm_our_key_dist`/`sm_their_key_dist`（**ENC|ID**）。
- **ビルド設定**：`esp/c3/esp_bt.cmake`（SM／PVCY のトグル、`ESP32C3_BT_SM`・`ESP32C3_BT_PVCY`）、
  `esp/common/bt/stub/include/bt_nimble_config.h`（`CONFIG_BT_NIMBLE_*` の実供給値）。
- **NimBLE 本体**：`esp-idf/components/bt/host/nimble/nimble/nimble/host/src/ble_sm*.c`。
- **OSアダプタ（疑うならここ）**：`esp/common/wifi/esp_shim*.c`（キュー・セマフォ・タイマを
  ASP3 API で実装）。**過去に「ブロッキング API が BT のクリティカルセクション内で `E_CTX` を
  返し、イベント投函が黙って失敗する」という同型のバグ実績がある**
  （memory `c3-ble-d2c-gatt-conn`）。**SM は 30s タイマを使う**ので、
  **タイマ/コールバックが落ちると ETIMEOUT になり得る**。

## 4. 特に答えてほしい問い

1. **`ENC_CHANGE status=13`（`BLE_HS_ETIMEOUT`）が出る経路を NimBLE のコードで特定してほしい。**
   `ble_sm` の 30秒タイマ（`ble_sm_timer`／`ble_hs_timer` 系）はどこで張られ、
   どの条件で発火するか。**「相手の応答が来ない」以外に、自分側の都合で発火する経路はあるか**
   （例：`ble_sm_proc` の状態不整合、`ble_hs_lock` 競合、タイマ再設定の取りこぼし）。
2. **BlueZ で成功し Android で失敗する差**を、SMP のフローで説明できるか。
   考えられる差分（**あくまで候補・断定しないでほしい**）：
   - Android は **LE Secure Connections + Numeric Comparison/Just Works** の分岐が BlueZ と違う
   - Android は **鍵配布（Identity Info/Address, `sm_their_key_dist`）を要求する**が BlueZ は要求しない
   - Android は **RPA（Resolvable Private Address）を使う**ので `ble_hs_resolv`／IRK の扱いが効く
   - conn param 更新（実測：7.5ms → 30ms）や DLE のタイミングが SMP と競合
3. **我々のペリフェラル発 Security Request（`ble_gap_security_initiate` を接続5秒後に撃つ）
   という設計自体に問題はないか。** Android が既に自発的にペアリングを始めている最中に
   SecReq を撃つと競合しないか。**撃つべきタイミング/条件の推奨**は？
4. **次に採るべき最小の測定**を1〜2個、具体的に指示してほしい
   （こちらの計装手段＝`ble_sm_tx` などの `--wrap` トレース（`rx_trace.c` 系の既存手法）、
   JTAG での変数ダンプ、Android btsnoop の SMP op 列）。
   **特に「我々が鍵配布 PDU を送っているか」を確定する方法**を優先してほしい。

## 5. 回答の形式（重要）

- **結論（同意/要修正/要追加検証）＋根拠（file:line）＋反証条件**の形で。
- **相関を因果と早合点しないでください。** このリポジトリでは
  「Aを入れたら直った」は **Aを外した対照**を採るまで因果と認めない規律で運用しています。
- **推測と実測を明示的に分けてください。** 断定できない部分は「未検証」と書いてください。
- **実測に反する指摘**をする場合は、§1-2 の実測とどう両立するかも述べてください。
- read-only（コード読解と診断のみ。ビルド・実機操作は不要）。

## 6. 補足：関連する既存文書

- `docs/ble-c3-smp-death-plan.md`（rev2）＝この症状群の調査計画。
  **rev2 の重要な訂正**＝初版の「活動5-6秒後に沈黙死」は **suptmo(5s) の誤読**で、
  実際は **SMP 直後（≤1s）に沈黙**。また **2M PHY 更新は一度も発生していない**（0件）。
  分類：**Step2型**＝phone の Pairing Request 受信 57ms 後に沈黙／
  **Step3型**＝LESC フル成功→**暗号化後に我々が鍵配布**→**0.77s 後**に沈黙。
  ⇒ **両ケースとも直前は «我々の SMP 処理»**。今回の症状がこのどちらかに該当するかは**未確定**。
- `.steering/20260721-docs-onboarding/evidence-05-c3-smp-notsup.md`＝本セッションの全実測。
- `docs/bt-shim.md`＝BLE shim の設計と過去の真因確定の記録。

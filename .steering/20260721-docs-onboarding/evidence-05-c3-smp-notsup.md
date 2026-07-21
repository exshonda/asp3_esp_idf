# evidence-05：C3 で SMP が始まらない真因＝cmake の条件取り違え（残課題D）／bond 永続化の完了（残課題C）

日付：2026-07-21 ／ DUT＝ESP32-C3 rev v0.3・flash 4MB（hub port3）

## 1. 症状と、既存の記録との食い違い

「接続はできるが**ペアリング要求が来ず bond できない**」。BlueZ でも Android
（Galaxy `SM_F966Q`）でも**同一症状**＝**central 非依存**と確定。

`docs/ble-c3-smp-death-plan.md` が扱う wedge（mbuf 枯渇）と同型かを**予測を固定して**検証した：

> 予測：wedge 状態なら `msys_1` が枯渇（`free=0/12`）し、host が古い接続を保持する。

**結果＝予測は外れ、記録済み機構とは別物と判明**（JTAG で wedge 状態を捕獲）：

| 項目 | 記録済み wedge | **今回の実測** | |
|---|---|---|---|
| `msys_1` | `free=0/12`（枯渇） | **`free=12/12`（健全・min=11）** | ❌ |
| `g_conn_handle` | `0x0001`（古い接続を保持） | **`0xffff`（未接続）** | ❌ |
| `notify_sent/fail` | 24/117 | **0/0** | ❌ |
| `gap_conn/gap_disc` | 5/4（切断1回欠落） | **1/1（欠落なし）** | ❌ |
| shim（`que_pend_total`/`crit_nest`） | 0/0 | 0/0 | ✅ |

⇒ **mbuf 枯渇でも切断イベント欠落でもない**。既存の緩和策 `ESP32C3_BT_CONN_WD` は
「stale 接続を terminate する」ものなので、**そもそも対象が無く効かない**。

## 2. ★btsnoop（Android・両端点の実測）で決着

`adb bugreport` → `FS/data/log/bt/btsnoop_hci.log` を取得し、リポジトリ既存の
`snoop/btsnoop_verify.py` で解析（過去の Step2/Step3 と同じ土俵）。

- 直近セッション（`+110568s`〜`+110700s`）＝接続4回・**SMP PDU は 0 件**。
- 切断理由 `raw=…0016` の下位バイト **`0x16`＝Connection Terminated By Local Host**
  ＝**スマホ側が諦めて切っている**。
- （`+63534s` に完全な LESC 一式が記録されているが、これは**別の古いセッション**。）

⇒ **どちらの端点も SMP を開始していない**。

## 3. ★真因＝`esp/c3/esp_bt.cmake` の条件取り違え

受動採取（`tmp/c5_cold_passive_capture.py` を C3 に転用。**リセットを起こさないため
測定対象の接続を壊さない**）で接続中の DUT ログを捕獲：

```
ble_host_smoke: BT5 security_initiate(slave SecReq) rc=8
```

`rc=8` = **`BLE_HS_ENOTSUP`**。`ble_gap_security_initiate()` は
`#if NIMBLE_BLE_SM`（`= BLE_SM_LEGACY || BLE_SM_SC`）でガードされている。
`nm` 実測でも **`ble_sm_slave_initiate`／`ble_sm_pair_initiate`／`ble_sm_enc_initiate`
が全て未リンク**＝SM 一式がコンパイルアウトされていた。

**原因のコード**（修正前）：

```cmake
if(ESP32C3_BT_SM)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_SM)
endif()
if(ESP32C3_BT_CONN_WD)          # ← ★SM とは無関係のオプション
    ...
else()                          # ← ★この else が SM を無効化していた
    MYNEWT_VAL_BLE_SM_LEGACY=0
    MYNEWT_VAL_BLE_SM_SC=0
endif()
```

**SM を 0 に蓋する `else()` が `ESP32C3_BT_SM` ではなく `ESP32C3_BT_CONN_WD` の
else に繋がっていた**。`CONN_WD` は既定 OFF なので、**`BT_SM=ON` にしても常に
SM が無効化**されていた。すぐ上のコメントは「SM=ON なら蓋をしない」と書いており、
**コメントと実装が食い違っていた**（構造的な取り違え）。

**修正**＝`if(NOT ESP32C3_BT_SM)` に変更（`CONN_WD` の分岐から切り離す）。

### 3.1 検証（ビルド成功で満足しない）

`nm` で **SM 一式がリンクされたこと**を確認（修正前は全て未リンク）：
`ble_sm_slave_initiate` ✓ ／ `ble_sm_pair_initiate` ✓ ／ `ble_sm_enc_initiate` ✓ ／ `ble_sm_sc_init` ✓

**実機**（BlueZ）：
```
Pairing successful / Paired: yes / Bonded: yes
DUT: GAP PAIRING_COMPLETE status=0 / GAP ENC_CHANGE status=0
     sec_state enc=1 auth=0 bond=1 keysz=16
```

## 4. ★残課題C（bond 永続化）も完了——真cold を跨いで復元を実証

SM が動くようになったので、evidence-04 で「前提が成立せず未検証」としていた
bond 永続化を最後まで検証できた。

| 段階 | 実測 |
|---|---|
| 書込み前 | 予約領域 `0x3F0000` = `ffffffff`（未使用） |
| ペアリング後 | **`magic=0x42535331`（"BSS1"）** ＋ peer アドレスがフラッシュに出現 |
| **真cold**（`usbhub3c_ctl.py off 3` → **USB 消滅を読み戻して電源断を実証** → 10s → `on 3`） | **`magic=0x42535331` のまま残存** |
| cold 後の再接続（**再ペアリングなし**） | `Connection successful`／`Bonded: yes`／DUT: **`bonds our=1 peer=1`**（保存済み鍵を読込）・`ENC_CHANGE status=0`・`enc=1 bond=1 keysz=16` |

⇒ **bond の不揮発化は実機で完全に成立**。README「既知の制限」の
「bondストアはRAM backed（`PERSIST=0`）＝電源断で鍵が消える」は**解消**。

## 5. 本ラウンドで踏んだ罠（記録）

1. **`rts_boot_capture.py` はペアリング中に使えない**——RTS でリセットするため
   観測対象の BLE セッションを壊す。受動採取（`c5_cold_passive_capture.py`）へ切替。
   その受動採取も「**開く瞬間に一度リセットが入る**」ので、
   **測定したい状態が始まる «前» に開き、測定中は開いたまま保持**する必要がある。
2. **接続失敗（`ConnectionAttemptFailed`）は環境要因**だった——BlueZ アダプタの
   power cycle で解消（`docs/c3-ble-connect-plan.md` 段階0 の指示が正しかった）。
3. **`grep` が ugrep に委譲**されており否定先読みが使えない（playbook 既知）。

## 6. 申し送り

- 修正は **C3 のみ**。**C5/C6 の同一箇所に同種の取り違えが無いか未確認**
  （C5/C6 は bond 実績があるので同型ではない可能性が高いが、**未検証**）。
- `ESP32C3_BLE_STORE_FLASH` は**既定 OFF のまま**（本ラウンドで実機実証できたので
  既定 ON への昇格は判断可能な状態になったが、C5/C6 未展開のため見送り）。

---

## 7. スマホ（Android）での検証——SM 修正は効いたが **bond は ETIMEOUT で不成立**

DUT＝`build/bondfix`（SM 修正＋bond 不揮発、既定 OFF オプションを ON）。
central＝Galaxy `SM_F966Q`。BlueZ は power off にして干渉を排除。

### 7.1 SM 修正の効果は出た（前後比較）

| | 修正前 | **修正後** |
|---|---|---|
| ペアリング要求 | **来ない**（btsnoop で SMP PDU 0件） | **来る**（ユーザー確認） |
| `security_initiate` | `rc=8`（ENOTSUP） | **`rc=0`（送信成功）** |

⇒ §3 の cmake 修正は**スマホでも効果を確認**できた。

### 7.2 ★残る症状＝`ENC_CHANGE status=13`（`BLE_HS_ETIMEOUT`）

受動採取（リセットしない）で捕らえた DUT ログ：

```
ble_host_smoke: GAP CONNECT status=0 handle=1
ble_host_smoke: BT5 security_initiate(slave SecReq) rc=0
ble_host_smoke: GAP ENC_CHANGE status=13      ← 30s SM タイムアウト
ble_host_smoke: sec_state enc=0 auth=0 bond=0 keysz=0
```

btsnoop 側（`+111689s`〜）：接続 → 25秒後に `0x16`（スマホが切断）→ 再接続 →
**`0x08`＝supervision timeout（リンクが物理的に沈黙）**。

### 7.3 旧真因（PVCY=0）は**今回には当てはまらない**（実測で否定）

memory `c3-ble-d2d-gatt-notify-sm` は同じ `ENC_CHANGE status=13` の真因を
「`MYNEWT_VAL(BLE_HS_PVCY)=0` で responder の Identity 鍵配布がコンパイルアウト」
と記録している。SM が実は無効だった前例（§3）があるため**設定ファイルの記述を信じず
プリプロセッサで実効値を確定**した：

```c
_Static_assert(MYNEWT_VAL(BLE_HS_PVCY) == 1, ...);   // ble_sm.c と同一フラグでコンパイル
_Static_assert(MYNEWT_VAL(BLE_SM_SC)  == 1, ...);
```
→ **両方とも通過＝`PVCY=1`・`SM_SC=1` が実効**。⇒ **旧真因の再発ではない**。

### 7.4 ★重要な観測：BlueZ では成功し、スマホでは失敗する

| central | 結果 |
|---|---|
| **BlueZ**（`hci0`） | **bond 成立**・真cold を跨いだ鍵の復元まで実証（§4） |
| **Android**（`SM_F966Q`） | ペアリング要求は来るが **ENC_CHANGE status=13 で不成立** |

⇒ **central 依存**。README の既知の制限「スマホcentralは全組合せが通るわけではない
（BlueZ では3チップとも成立）」と**整合**する。
これは `docs/ble-c3-smp-death-plan.md` が扱う**未解決の本丸**であり、
本ラウンドで**新たに壊したものではない**（修正前はそもそもペアリング要求すら
出ていなかったので、**症状はむしろ前進している**）。

### 7.5 本ラウンドの到達点と、次に必要な測定

**到達**：
- SM コンパイルアウトのバグを**特定・修正**（BlueZ で bond 成立を実証）
- **bond 永続化を完全実証**（真cold を跨いだ鍵の復元）
- スマホでも**ペアリング要求が出るところまで前進**
- 旧真因（PVCY=0）の**再発ではない**ことを実効値で否定

**残**：スマホ相手の `ENC_CHANGE status=13`。次に必要なのは
**SMP PDU レベルの両端点突合**——具体的には
「**我々が鍵配布（Identity Info/Address）を送っているか**」を
`ble_sm_tx` の `--wrap` 計装（`rx_trace.c` 系の既存手法）と btsnoop の
SMP op 列（0x08/0x09/0x0a/0x0b の有無）で照合する。
`docs/ble-c3-smp-death-plan.md` rev2 の分類では **Step3 型**（LESC 成功→鍵配布→沈黙）に
相当する可能性が高いが、**本ラウンドでは SMP op 列を採れていないため断定しない**。

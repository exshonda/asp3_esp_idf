# C6 evidence-13 — **shim の `= 0x7` → `|= 0x7`**／**マトリクス セル1（iPhone）の準備**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C6 `<MAC-03>`＝board C**（hub `1-6` port2）
前段: `evidence-c6-12`（app の重複 LPCON 書込み除去。`= 0x7` → `|= 0x7` を推奨として申し送り）

★**認証情報はビルド注入のみ。本ファイルには書かない。**

---

## A. `= 0x7` → `|= 0x7`（依頼 A）

### A.1 変更（2箇所・**立てるビットは変えていない**）

| 場所 | 関数 | 変更 |
|---|---|---|
| `wifi/esp_wifi_adapter.c:589`→`:597` | `phy_enable_wrapper()` | `= 0x7` → **`\|= 0x7`** |
| `wifi/esp_wifi_adapter.c:746`→`:756` | `wifi_clock_enable_wrapper()` | `= 0x7` → **`\|= 0x7`** |

**根拠**＝`bt/bt_shim.c` の `\|= 0x1` とそのコメント「**read-modify-write で bt.c が後段で立てる
他ビットを温存する**」＝**温存が設計上の正**。代入は **bit3 以上を潰す**——
**memory §12 は「LPCON `0x600af018` bit3 = LP_TIMER_EN は BT 正当」と実測記録**
⇒ **WiFi/BT 同時運用時のハザード**（現状は排他なので未顕在）。
★**どのビットが要るかの同定は «やっていない»**（範囲を広げない＝依頼どおり・§C-1 に申し送り）。
★**shim が同じ値を2箇所で書く件も触っていない**（§C-2）。

### A.2 命令レベルの確認（★ソース差分だけで済ませない）

```
420034ae <phy_enable_wrapper>:
420034ae:  lui  a4,0x600af
420034b2:  lw   a5,24(a4)      ← ★ストア前に «ロード» ＝ read-modify-write（offset 24=0x18）
...
42003424 <wifi_clock_enable_wrapper>:
4200343a:  lui  a5,0x600af
4200343e:  lw   a4,24(a5)      ← ★同上
```
**代入(`=`)なら `lw` は現れない**。⇒ **両箇所とも `\|=` になっている**。

### A.3 実機非回帰（依頼 (a)）＝**真cold**

真cold の証明＝`uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 読み戻し 0**（W1 は **sentinel `0x00000000`** も）。

| 測定 | 結果 |
|---|---|
| **`wifi_scan`（`\|=`）** | **`13 APs found (err=0)`・RESCAN×6（10/16/17/15）** ＝**基準 16 APs と同じ帯**（実測の AP 数は 10-23 の範囲で毎回揺れる） |
| **W1 `wifi_dhcp`（`\|=`）** | **`DHCP bound`／`IP acquired: 192.168.1.69`／ping OK **35**・失敗 **0**** |

**ビルド非回帰**：`wifi_scan`／W1 とも **hal deps 0・`__wrap_*` 0・`wifi_regsnap` 0**（＝
`evidence-c6-11/12` で達成した «既定に計装 0» を維持）。

★**1 run で結論を書かない**（`evidence-c6-12` の教訓）：AP 数は毎回揺れる量なので
**«13 vs 16» を回帰と読まない**。**判定は «err=0 で AP が取れること»**（0 AP でないこと）。

---

## B. マトリクス セル1（iPhone）の準備（依頼 (b)）

### B.1 供給の **実効値**（★cache でなく `build.ninja`／ELF で実測）

| 項目 | 実測 |
|---|---|
| BT `bt.c` の出所 | **`esp-idf`（submodule＝真の v5.5.4 タグ）** |
| blob `-L` の出所 | **`esp-idf/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6`** |
| **外部 v6.1 tree 参照** | **0** |
| **hal deps（指標[1]）** | **0** |

★**`CMakeCache.txt` は見ていない**（`feedback_hardware_investigation_rigor` 7th＝
«cache は option の宣言既定＝入力／`build.ninja`・ELF＝出力。証拠は出力のみ»）。

### B.2 計装ゼロ・SM スタックの実リンク（★焼く前に確認）

| | 値 |
|---|---|
| `__wrap_*` / `wifi_trace_*` / `wifi_regsnap` | **0 / 0 / 0** |
| `ble_sm_pair_initiate` / `ble_sm_alg_encrypt` / `tc_aes_encrypt` / `ble_store_config_init` / `uECC_*` | **1 / 1 / 1 / 1 / 15** |
| `ble_gap_adv_start` | 2 |

### B.3 真cold で焼いて **«電波» で広告を確認**（★`strings` で済ませない）

**デバイス側**（真cold・by-id 読み戻し 0・STORE0 センチネル自己検証）：
`sync 0x5ade51c0`／`adv 0x0ade5000`／**`rc 0xad000000`**／`intr 0xa1020704`。

**★電波（`bluetoothctl scan le`・受動）**：

```
[NEW] Device <MAC-03> ASP3-C6-BLE     ← ★MAC が本DUT と一致
[CHG] Device <MAC-03> RSSI: -71
[CHG] Device <MAC-03> RSSI: -70       ← 反復＝生きた広告
Discovering: no                                 ← ★スキャンは止めた（ユーザーのスマホを邪魔しない）
```

⇒ **広告名 `ASP3-C6-BLE`（source 実測 `:202` と一致）・RSSI 約 -70 dBm で «実際に電波が出ている»**。
★同時に `ASP3-C5-BLE`(`<MAC-37>`)・`ASP3-C3-BLE`(`<MAC-19>`) も見えたが
**触っていない**（別エージェント／ユーザーの領域）。

---

## C. 申し送り

1. **ビット同定は未実施**（`0x7` のどれが要るか）。**手順**＝`\|= 0x7` から
   **1本ずつ落として真cold で AP が取れるか**（`\|= 0x4`(I2C_MST) だけで足りるか等）。
   **一度に全部やらない。**
2. **shim は同じ値を2箇所で書く**（`phy_enable_wrapper` / `wifi_clock_enable_wrapper`）。
   **どちらが必要かは未測定**（触っていない）。
3. **過去の合格根拠（`c6_r89_*`・`c6_r90_*` の 600s 負荷）は «計装込み» のまま**＝未再測。
4. §10-12 hal ハングの正体・`STORE4`/`STORE1` 潰しは従来どおり。

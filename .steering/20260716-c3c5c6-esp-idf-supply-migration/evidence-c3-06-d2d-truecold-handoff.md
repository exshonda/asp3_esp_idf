# C3 evidence-06 — **真cold D-2c/D-2d の «デバイス側準備完了»**（スマホ central へ引き渡し）

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `<MAC-19>`**（hub `1-6` **port1**）
**ボードは «真cold 起動済み・広告中» のまま残置**（RSSI −42〜−51 で実測）

> ★**C3 にとって初物**：`docs/bt-shim.md` の D-2c/D-2d フル達成は**すべて warm**。
> **真cold では一度も成立していない**。本ラウンドは**その土俵を用意した**ところまで。

---

## 1. (a) 広告名（**実測**）

**`ASP3-C3-BLE`**

- バイナリから：`strings asp.elf | grep '^ASP3-.*BLE$'` → `ASP3-C3-BLE`
- **電波から**（真cold 起動後・hci0 パッシブスキャン）：
  `<MAC-19> ASP3-C3-BLE` / `RSSI -42 〜 -51`
- **BD address ＝ `<MAC-19>`＝DUT の BASE MAC と一致**（他個体との取り違えは無い）

## 2. (b) tripwire（**焼く前に確認**）

**ビルド構成（`CMakeCache` から実測．推測ではない）**：

```
ESP32C3_BT=ON  ESP32C3_BT_NIMBLE=ON  ESP32C3_BT_SM=ON  ESP32C3_QEMU=OFF
ASP3_ESPIDF_SUPPLY=OFF  ASP3_BT_IDF_V554=OFF        ← ★hal 経路＝bond する側（既定）
ESP32C3_BT_EVT_TRACE=OFF  ESP32C3_BT_ACL_TRACE=OFF  ESP32C3_BT_PHY_CAL_TRACE=OFF
```

### 2.1 SM/bonding/crypto が**実リンク**されている

| symbol | 個数 |
|---|---|
| `ble_sm_pair_initiate` / `ble_sm_alg_encrypt` / `ble_store_config_init` | 各 **1** |
| `tc_aes_encrypt` / `uECC_make_key` / `ble_gatts_add_svcs` | 各 **1**（`uECC_*` 計 **15**） |

### 2.2 ★`0xABF4`（READ_ENC）が**本当に居る**——C6 で踏んだ罠の回避

**`0xABF4` は `#ifdef TOPPERS_ESP32C3_BT_SM` の中**にある（＝SM を切ると**特性ごと消える**）。
`ESP32C3_BT_SM` は**既定 ON**（`esp_bt.cmake:392`）＝既定ビルドに含まれる。**それを実測で確認**：

- `custom_chrs` の**実サイズ＝160 B＝5 エントリ**（＝**4 特性＋終端**。SM 無しなら 4 エントリ=128 B）
- **第4エントリの flags＝`0x0202`＝`BLE_GATT_CHR_F_READ`(0x0002) | `BLE_GATT_CHR_F_READ_ENC`(0x0200)**
  （`objdump -s` の生バイト `02020000` で確認）
- イメージ内の 16bit UUID（LE バイト列）：`0xABF0`×1 / `0xABF1`×1 / `0xABF2`×1 / `0xABF3`×1 / **`0xABF4`×2**

⇒ **D-2d（暗号必須 READ）は測定可能**。

### 2.3 ★計装は**ゼロ**

本ラウンドで私は「**効いていない計装**」と「**bond を壊す計装**」の**両方**を踏んだ。
∴ **ユーザー検証は計装ゼロの素の既定で行う**：

| symbol | 個数 |
|---|---|
| `evt_trace_fast_dump` / `__wrap_ble_hs_hci_evt_process` / `__wrap_ble_transport_to_ll_acl_impl` / `__wrap_ble_mqueue_put` / `g_acl_vhci_tx` | **すべて 0** |
| RTC-FAST map magic（`0x5c3e0001`/`0x5c3e0002`） | **0** |
| GATTS regdiag（`0x5eed`） | **0** |

## 3. (c) 真cold で adv 到達した物証

| 手順 | 実測 |
|---|---|
| マーカ 7 本を 0 クリア → 読み戻し | **全て `0x00000000`**（＝クリア成立＋読み経路健全） |
| **真cold**：`uhubctl -l 1-6 -p 1 -a off` | **by-id の `BA:BC` 数 = 0**（＝**POR の唯一の証明**．`rst:` は証明にならない） |
| `-a on` → 10 s | by-id 復帰 = 1 |
| **★電波**（**スキャンを先に**．`read-mem` は DUT を止めるので後回し） | **`ASP3-C3-BLE` を検出・RSSI −48〜−51** |

⇒ **真cold 起動 → NimBLE sync → adv 到達 → 電波に出ている**ことを、**コンソール非依存**で確認。
（★`read-mem` を先にやると download mode へ落ちて広告が止まる＝**前ラウンドで実際に踏んで
「adv 非到達」と誤断しかけた**。順序を «cold → スキャン → 最後にマーカ読み» に固定した。）

**現在の残置状態**：**真cold 起動済み・広告中・stale bond 削除済み（`bluetoothctl remove` 実施）**
＝**ユーザーは «フレッシュな bond» を測れる**。ホスト側 `bluetoothctl` の scan も off・常駐なし。

---

## 4. (d) ★ユーザー向け スマホ手順

**対象：`ASP3-C3-BLE`（`<MAC-19>`）／アプリ：nRF Connect 等の GATT クライアント推奨**

### 4.0 ★★最初に必ず：GATT キャッシュの掃除

> **過去に `ASP3-C3-BLE` へ接続したことがある端末は、古いサービス定義をキャッシュしており、
> 新しい特性が «丸ごと不可視» になる。**
> **★これは C3 で実際に起きた**——`0xABF0` がスマホから見えず「**デバイス側の登録失敗**」と
> 誤診しかけ、`gatts_register_cb` の計装で「**ATT には正しく登録されている**」と判明して
> 初めて **100% スマホ側キャッシュ**と確定した（`docs/bt-shim.md`）。

**手順**：
1. スマホの Bluetooth 設定で **`ASP3-C3-BLE` を «このデバイスの登録を解除»（forget/ペア解除）**
2. **Bluetooth を OFF → ON**
3. これで再接続時に**フレッシュな discovery** になる

★**「接続＋bond 成功」は «古い bond の再利用» でも起こる**。**必ず forget 後のフレッシュ試行で測る**
（そうしないと «成功» が偽陽性になる）。デバイス側の stale bond は私が削除済み。

### 4.1 接続と D-2c（平文 GATT）

1. スキャンして **`ASP3-C3-BLE`** に **接続**
2. サービス **`0xABF0`** を開く（4 つの特性が見えるはず：`0xABF1`〜`0xABF4`）
3. **`0xABF1` を READ** → **`BT4-OK`**（生バイト `42 54 34 2d 4f 4b`）
4. **`0xABF2` を subscribe（Notify ON）** → **32bit LE カウンタが周期的に届く**（値が増える）
   - READ でも現在値が読める
5. **`0xABF3` へ WRITE** → 任意の値（例：`01` や `AA BB`）を書く。**エラーが返らなければ成功**
   （デバイス側は受信バイト数と先頭バイトをマーカに残す）

### 4.2 ★D-2d（bond ＋ 暗号必須 READ）＝**本体**

6. **`0xABF4` を READ** → **★未ペアなので «弾かれる» のが正しい**
   （`Insufficient Authentication` / `Read not permitted` 等のエラー）
7. **その拒否を受けてスマホがペアリングを開始する**（機種によっては手動で「ペア設定」を選ぶ）
   - 本機の IO capability は **NoInputNoOutput 相当**＝**PIN/パスキー入力は出ない想定**
     （「ペア設定しますか？」の確認だけが出るはず）
8. **ペアリング（bond）完了後にもう一度 `0xABF4` を READ** → **`BT4-OK` が返れば D-2d 達成**
   ＝**bond で確立した LTK による暗号が実効**であることの end-to-end 証明

### 4.3 うまくいかない時の切り分け（ユーザー向け）

| 症状 | まず疑うもの |
|---|---|
| `ASP3-C3-BLE` が見えない | 端末の BT OFF→ON／他端末が接続中（**接続中のペリフェラルは広告を止める**） |
| 接続できるが `0xABF0` が見えない／特性が足りない | **§4.0 のキャッシュ**（forget → BT OFF/ON） |
| `0xABF4` が最初から読めてしまう | **古い bond が残っている**（＝forget が効いていない）＝**D-2d の証明にならない** |

---

## 5. (e) ★コーディネータが読むべき STORE（アドレス・期待値・クリア挙動）

### 5.1 ★★読む «前» に知っておくべき2点

1. **`read-mem`（`--before usb-reset --after no-reset`）は DUT を download mode に落とす**
   ＝**広告が止まる**。∴ **ユーザーの検証が «完全に終わってから» 読む**こと。
2. **読み終わって広告を戻したいなら、最後の1回だけ `--after hard-reset` を付ける**。
   ★**ただしそれは «warm» 起動**になる。**再び «真cold» が要るなら
   `uhubctl -l 1-6 -p 1 -a off` → by-id 消滅確認 → `-a on`**（`-p 1` のみ）。

### 5.2 マーカ表

| addr | 名前 | **boot クリア** | 期待値（成功時） | 判定 |
|---|---|---|---|---|
| `0x60008050` | SYNC | **されない** | **`0x5ADE51C0`** | NimBLE ホスト sync 到達 |
| `0x60008054` | **PAIR**（★bt_shim の intr トレースと**共用**） | **されない**（設計上） | **`0x5DC000xy`** | **タグ `0x5DC0` の有無で判別**（設計者の意図＝ソース `:720-723`）。**`status = byte[1]`＝`00` なら成功**。`x`=our_cnt / `y`=peer_cnt。**`0xA102xxxx` のままなら «pairing は一度も完了していない»**（＝init 時の intr トレース値） |
| `0x60008058` | WRITE（`0xABF3`） | **されない** | **`0x7717ccff`** | `cc`=write 回数 / `ff`=最後に書かれた先頭バイト |
| `0x6000805C` | ADV | **されない** | **`0x0ADE5000`** | `ble_gap_adv_start` 試行 |
| `0x600080B8` | DISC | **★される（0 クリア）** | 非0 なら切断発生 | **毎起動フレッシュ** |
| `0x600080C0` | CONN | **★される（0 クリア）** | **`0x604Exxxx`** | **毎起動フレッシュ**＝接続確立 |
| `0x600080C4` | advRC | **されない** | **`0xAD000000`**（＝rc 0） | `0xAD0000xx`（xx=rc） |
| ~~`0x600080BC`~~ | ~~STORE5~~ | — | — | **★ROM が上書きする＝使用禁止**（実測：`0x13121312`） |

### 5.3 ★staleness は «本ラウンドで潰してある»

- **私は 7 本すべてを 0 クリアしてから真cold 起動した**（読み戻しで 0 を確認済み）。
- ∴ **`PAIR`／`WRITE` が非0 なら、それは «この残置セッション» のもの**（＝stale ではない）。
- ★**さらに強い保証**：マーカは**固有のマジック**（`0x5DC0…`／`0x7717…`）を持つ。
  **POR のゴミや ROM の上書きがこの値を «偶然» 作ることはない**
  ⇒ **タグが立っていれば «発火した» と読んでよい**。
- **`CONN`/`DISC` はアプリが毎起動 0 クリア**する（`ble_host_smoke.c:718-719`）＝**常にフレッシュ**。
- **`SYNC`/`ADV`/`advRC` はクリアされない**が、**起動のたびに書かれる**ので実質フレッシュ。
  **ただし «adv 到達前にハングした起動» では前回値が残る**（＝**成功に見える**）。
  ⇒ **これらだけで «今回の起動が adv した» と結論しない**こと（＝前ラウンドの教訓）。

### 5.4 成功時に期待される «一式»

```
0x60008050 SYNC  = 0x5ADE51C0
0x6000805C ADV   = 0x0ADE5000
0x600080C4 advRC = 0xAD000000
0x600080C0 CONN  = 0x604Exxxx      ← 接続した
0x60008054 PAIR  = 0x5DC000xy      ← ★D-2d の核心（byte[1]=status=00 なら成功）
0x60008058 WRITE = 0x7717ccff      ← 0xABF3 に書いたなら
0x600080B8 DISC  = 非0（切断後）
```

**`0x60008054` が `0xA102xxxx` のままなら → pairing は完了していない ＝ D-2d 未達**。

---

## 6. (f) 残ブロッカー

1. **★スマホ操作はユーザー待ち**（私の範囲外）。**ボードは真cold・広告中で残置**。
2. **ユーザーが長時間放置／電源が落ちた場合**：再度 §5.1-2 の手順で真cold を作り直す必要がある
   （マーカは私が 0 クリア済みだが、**再 POR しても `PAIR`/`WRITE` のマジックは «偶然» 復活しない**
   ので、**その後の非0 は依然として «発火» と読んでよい**）。
3. **本ラウンドは «hal 経路» の D-2c/D-2d を測る**。**esp-idf 供給での bond は不可**と
   evidence-c3-03/04/05 で確定済み（機構＝C3 の esp-idf v5.5.4 コントローラの暗号開始経路）。
   **その «設計判断» は依然ユーザー案件**（evidence-c3-05 §6.2）。
4. **`hci0` には触っていない**（ユーザーのスマホ検証を邪魔しないため scan off・常駐なし・
   stale bond 削除済み）。
</content>

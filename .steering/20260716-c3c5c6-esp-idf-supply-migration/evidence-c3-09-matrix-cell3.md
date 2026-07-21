# C3 evidence-09 — マトリクス **セル3（esp-idf 真v5.5.4 × iPhone）準備完了**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `<MAC-19>`**（hub `1-6` port1）／**残置＝真cold・広告中・全マーカ0クリア済み**

---

## 0. 前提の更新（コーディネータ実測．**私の «auto-reconnect» 指摘の正体が判明**）

| セル | 供給 | central | 実測 |
|---|---|---|---|
| **1** | hal | iPhone | `CONN=0x604e0001` / **`PAIR=0x5dc00000`（★タグ立＝フレッシュ・status 00）** / **`ENC=0x5de00000`（成功）** / `DISC=0` |
| **2** | hal | Android | `CONN=0x604e0002` / **`PAIR=0x5dc00011`（status 00・our_sec=1・peer_sec=1）** / **`ENC=0x5de00000`** / `DISC=0xd15c1302`（reason 0x13＝正常切断×2） |

- **セル1＝«デバイスは bond も暗号化も成功させた»**。それでも iPhone は timeout を出し、**切断を送らずに諦めた**
  ⇒ `DISC=0`＝**ボードはリンクを握って広告停止**。**«デバイスの失敗» ではない。**
- **セル2＝ALL OK**（ユーザー報告と一致．`our_sec=1` は**本プロジェクト初の観測**）。

### 0.1 ★★本日の «Android は失敗する» 観測群は **測定手順が作った交絡**だった

**デバイスの bond ストアは RAM backed**（C5 で3通りに確定：`ble_store_nvs.c` 非リンク／`PERSIST=0`／
ELF に `nvs_open` 0個）。⇒ **«セルをクリーンにする» ための電源断が、毎回
«スマホは鍵を持つ／デバイスは鍵を失う» という不一致を作っていた**。
⇒ **`ETIMEOUT`(13) も `ENOTCONN`(7) も «鍵が合わない» の症状として辻褄が合う。**
⇒ **私が evidence-c3-08 §1.2 で «3度目の再現» と書いた「hal × Android = FAIL」は
«本物の再現» ではなく «同じ交絡の3度目» だった公算が大きい**——**自己訂正する**。
★私が evidence-c3-08 §1.1 で「**auto-reconnect is the matrix's hidden enemy＝全端末で forget が必須**」
と書いたのは**結果的に正しかった**が、**機序（RAM backed store）までは特定していなかった**。
**機序はコーディネータが特定した。**

### 0.2 ★判別子（**今後は全セルでこれを読む**）

| `PAIR`(0x54) | 意味 |
|---|---|
| **`0x5DC0…` タグ** | **その接続でペアリングが実行された＝フレッシュ**（`status`／`our_sec`／`peer_sec` が有効） |
| **`0xA102…`** | **未発火**＝alloc trace の残留＝**既存 bond の再利用を試みた＝交絡** |

★**`ENC` だけ見ると «失敗» に見えるが、`PAIR` タグが `0xA102` なら «測定が壊れている» の方**。

---

## 1. セル3 の構成（★**供給の実効値は `build.ninja`/ELF で実測．cache ではない**）

### 1.1 供給＝esp-idf（真の v5.5.4）で確定

```
-L /…/esp-idf/components/soc/esp32c3/ld
-L /…/esp-idf/components/bt/controller/lib_esp32c3_family/esp32c3
-L /…/esp-idf/components/esp_phy/lib/esp32c3
-L /…/esp-idf/components/esp_coex/lib/esp32c3
```

| 検査 | 結果 |
|---|---|
| **リンク行の hal 参照** | **0** |
| リンク行の esp-idf 参照 | 158 |
| `~/tools/` 参照 | **0** |
| **`.d` の hal 参照** | **0** |
| **controller グルー** | **`esp-idf/components/bt/controller/esp32c3/bt.c`** |
| **ホスト SM** | **`esp-idf/…/host/src/ble_sm.c`・`ble_sm_alg.c`** |
| **実リンクされる blob** | **`libbtdm_app.a` = `859e8c8e`＝真の v5.5.4 タグ**（hal=`dfdadb9d` / v6.1=`d9753a31` のどちらでもない） |

⇒ **セル1（hal）との差は «供給» のみ**＝マトリクスの定義どおり。

### 1.2 ★`0xABF4` の実在を **構造で** 確認（★既定値を信じない）

`0xABF4` は `#ifdef TOPPERS_ESP32C3_BT_SM` の中にある＝**SM が落ちると特性ごと消える**。
**esp-idf 供給で `#ifdef` の効き方が変わりうる**ので、**オプション既定ではなく構造で確認**：

- **`custom_chrs` 実サイズ ＝ `0xa0` ＝ 160 B ＝ 5 エントリ**（4 特性＋終端）
  **★SM が落ちていれば 4 エントリ＝128 B になる**⇒**落ちていない**
- **第4エントリの flags ＝ `0x0202` ＝ `BLE_GATT_CHR_F_READ`(0x0002) | `BLE_GATT_CHR_F_READ_ENC`(0x0200)**
  （生バイト `02020000` を `objdump -s` で確認）
- イメージ内 UUID：`0xABF0`×2 / `0xABF1`×1 / `0xABF2`×1 / `0xABF3`×1 / **`0xABF4`×2**

⇒ **D-2c/D-2d は測定可能。**

### 1.3 計装ゼロ（`nm`／`objdump`）

| 検査 | 個数 |
|---|---|
| `__wrap_*` | **0** |
| `pvcy_filter` / `evt_trace` / `acl_trace` / `phy_cal_trace` / `g_acl_vhci_tx` | **すべて 0** |
| RTC-FAST map magic（`0x5c3e000[123]`） | **0** |
| `ble_sm_pair_initiate` / `ble_store_config_init` / `tc_aes_encrypt` | 各 **1**（＝SM/bond/crypto は実リンク） |

---

## 2. 準備の実施（依頼の必須項目）

| # | 項目 | 結果 |
|---|---|---|
| 1 | 供給の実効値 | **§1.1．hal 参照 0・esp-idf 供給で確定** |
| 2 | **7マーカー全クリア→読み戻し** | **全て `0x00000000` を確認**（下記） |
| 3 | **広告を電波で確認** | **`ASP3-C3-BLE` / RSSI −48〜−52**（`hci0` 受動スキャン．**確認後 scan は停止**） |
| 4 | `0xABF4` 実在 | **§1.2．5 エントリ・flags `0x0202`** |
| 5 | 計装ゼロ | **§1.3．すべて 0** |

**7マーカーの読み戻し（クリア後）**：
```
SYNC(0x50)=0x00000000  PAIR(0x54)=0x00000000  ENC(0x58)=0x00000000  ADV(0x5C)=0x00000000
DISC(0xB8)=0x00000000  CONN(0xC0)=0x00000000  advRC(0xC4)=0x00000000
```
★**`ENC`/`PAIR` は毎起動クリアされない**＝**明示クリアしないとセル1の
`ENC=0x5de00000`／`PAIR=0x5dc00000` が残り «iPhone も成功» と誤読される**。

**真cold の証明**：`-p 1 -a off` → **by-id の `BA:BC` = 0** → `-a on`（`rst:` は証明にならない）。
**残置＝真cold 起動・広告中・hci0 の scan 停止・hci0 側 stale bond 削除済み。**

---

## 3. ★ユーザー向け手順（セル3＝**esp-idf 版 × iPhone**）

### 3.0 ★★必須（**これを飛ばすとセルが潰れる**）

> **★両方のスマホで forget → BT OFF/ON**
>
> 1. **Android で `ASP3-C3-BLE` の «登録を解除（forget）»**
>    — **iPhone のセルなのに Android も forget する理由**：**Android が自動再接続して
>    ボードを吊る**（evidence-c3-08 §1.1 で実際に発生）。**吊られると広告が止まり
>    iPhone から一切見えなくなる**。
> 2. **iPhone でも «このデバイスの登録を解除»**
> 3. **両端末とも Bluetooth を OFF → ON**
>
> ★**理由（機序）**：**デバイスの bond ストアは RAM backed**＝**電源断で «デバイス側の鍵だけ» が消える**。
> **スマホに登録が残っていると «スマホは鍵を持つ／デバイスは持たない» の不一致**になり、
> **`ETIMEOUT`/`ENOTCONN` で失敗する**（＝**本日の «Android 失敗» 観測群の正体**）。
> **forget は «キャッシュ対策» ではなく «鍵の不一致対策»。**

### 3.1 iPhone での手順（nRF Connect 等の GATT クライアント推奨）

1. スキャンして **`ASP3-C3-BLE`** に接続
2. サービス **`0xABF0`** を開く（**`0xABF1`〜`0xABF4` の4特性**が見えるはず）
3. **`0xABF1` READ** → **`BT4-OK`**（`42 54 34 2d 4f 4b`）
4. **`0xABF2` subscribe** → 32bit LE カウンタが増える
5. **`0xABF3` WRITE** → 任意値（エラーが返らなければ OK）
6. **★`0xABF4` READ** → **未ペアなので «弾かれる» のが正しい**（insufficient authentication）
7. **ペアリング**（NoInputNoOutput 相当＝確認ダイアログのみ・パスキー入力は出ない想定）
8. **bond 後に `0xABF4` を再 READ** → **`BT4-OK` なら D-2c/D-2d 達成**

### 3.2 ★終わったら触らない

**そのまま放置**（コーディネータが LP_AON を読む）。
★**`ENC` は «最後の1件» しか残らない**＝**再接続を繰り返すと上書きされる**。
★**他の端末（Android）を絶対に繋がせない**（セルが混ざる）。

---

## 4. ★コーディネータが読むべき STORE とタグ

★**タグで引く**（アドレスだけで引くと共用に足を掬われる）。

| addr | 名前 | boot クリア | タグ／期待値 |
|---|---|---|---|
| `0x60008050` | SYNC | されない | `0x5ADE51C0` |
| `0x60008054` | **PAIR**（★bt_shim intr トレースと**共用**） | されない | **`0x5DC0<status><our><peer>`＝★フレッシュにペアリングが走った**／**`0xA102xxxx`＝未発火＝交絡を疑え** |
| `0x60008058` | **ENC／WRITE**（★**共用．ENC が後勝ち**） | されない | **`0x5DE0<delta秒><status>`**：**`00`＝成功／`0d`＝`BLE_HS_ETIMEOUT`／`07`＝`BLE_HS_ENOTCONN`**。`0x7717ccff` なら `0xABF3` WRITE の方 |
| `0x6000805C` | ADV | されない | `0x0ADE5000` |
| `0x600080B8` | DISC | **★される** | `0xD15C<reason><n>`＝切断／**`0`＝切断イベント来ず＝吊られている**（→次セル前に必ず `-p 1`） |
| `0x600080C0` | CONN | **★される** | `0x604E00xx`＝接続成立（xx=回数） |
| `0x600080C4` | advRC | されない | `0xAD000000`（rc=0） |
| `0x600080BC` | — | — | **★ROM が上書き＝使用禁止** |

★**本ラウンドで 7 本すべてを 0 クリアしてから真cold 起動した**
⇒**非0 の値は «このセル» のもの**（stale ではない）。
★加えて**固有マジック**（`0x5DE0`/`0x5DC0`/`0x604E`/`0xD15C`）なので**POR ゴミが偶然作れない**
⇒**タグが立てば «発火» と読んでよい**。

### 4.1 セル3 が «OK»（ユーザー仮説どおり）なら
```
CONN = 0x604E00xx / **PAIR = 0x5DC000xy（status=00）** / **ENC = 0x5DE0xx00（status=00）**
```
### 4.2 «NG» なら
```
**PAIR = 0x5DC0xx<非0>** → ペアリングは走ったが失敗（＝本物の失敗）
**PAIR = 0xA102xxxx**    → ★未発火＝«forget 漏れ» を疑え（＝交絡．セルを測り直す）
ENC  = 0x5DE0xx<非0>     → 0d=ETIMEOUT / 07=ENOTCONN
DISC = 0                 → 吊られている（次セル前に必ず -p 1）
```

---

## 5. 残ブロッカー

1. **★セル4（esp-idf × Android）の焼き直しは «コーディネータの合図待ち»**。**先走らない**
   （順序＝ユーザー試行 → 読み → 電源断 → 次）。★**セル4 は «焼き直し不要»**——
   **セル3 と同一ビルド**なので、**マーカ全クリア＋真cold＋両端末 forget だけで移れる**。
2. **★1ビルドにつき1端末**（`ENC` は最後の1件しか残らない）。
3. **★セル1/2 の «hal × Android = FAIL» 観測（本日3件）は交絡由来の公算**（§0.1）
   ⇒ **evidence-c3-07/08 の当該記述は本ファイルで自己訂正済み**。
   **ただし «PVCY draft(ii) が BlueZ を 7/7→1/4 に壊した» は別件で有効**
   （★あれは **同一手順・同一 central・baseline も同一セッションで再取得**しており、
   **bond ストアの RAM backed 性は両アームに等しく効く**＝**A/B の差は説明できない**）。
4. **C5 の Android 失敗（`ENOTCONN`）は要再測**（コーディネータ担当．**PAIR タグ未読**＝同じ交絡の疑い）。
</content>

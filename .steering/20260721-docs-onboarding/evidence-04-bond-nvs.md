# evidence-04：bond ストアの不揮発化（残課題C）

日付：2026-07-21 ／ ブランチ：`main`
課題＝README「既知の制限」の **bond ストアが RAM 保持（`PERSIST=0`）＝電源断で鍵が消える**。

## 1. 着手前の実測（設計判断の材料）

| 項目 | 実測 |
|---|---|
| ブート方式 | **Direct Boot**（2段ブートローダ無し・**パーティションテーブルが存在しない**） |
| フラッシュ | 4MB（`IROM 0x42000000` / `DROM 0x3C000000`） |
| アプリ実データ | **0x000000〜0x03dc48（約253KB）**。以降 0x400000 まで 0xff＝未使用 |
| 現行 store | `ble_store_config.c`（PERSIST=0＝RAM） |
| NVS | wifi blobglue の **no-op スタブのみ**（実装なし） |
| 最大 bond 数 | `CONFIG_BT_NIMBLE_MAX_BONDS = 3` |

## 2. ★方式の選定＝**自前の最小 store バックエンド**（ユーザー判断）

| 案 | 判定 |
|---|---|
| **1. 自前バックエンド（採用）** | フラッシュ末尾に固定領域を予約し `ble_hs_cfg.store_*_cb` を差し替え |
| 2. ESP-IDF の `nvs_flash` | **却下**——`esp_partition` 前提だが **Direct Boot にパーティションテーブルが無い**。テーブルを後付けしても読む主体（bootloader）が居ない。加えて `nvs_flash` は C++＋ヒープ多用で、hal 撤廃で減らした複雑さを戻す |
| 3. seam ブートへ移行して 2 | **却下**——ブート方式の決定（Direct Boot 継続．evidence-c5-03）を覆すため |

保存対象は **固定長・最大3件**で、NVS の汎用 KV は過剰という点も 1 を後押しした。

## 3. 着手前に固定した予測と結果

| 予測 | 結果 |
|---|---|
| (a) ROM の SPI flash API が3チップとも ld から供給されている | ✅ **的中**——C3/C5/C6 とも `esp_rom_spiflash_{read,write,erase_sector,unlock}` を `<chip>.rom.ld` が供給 |
| (b) NimBLE は関数ポインタ差し替えで独自バックエンドを差せる | ✅ **的中**——`ble_hs_cfg.store_{read,write,delete}_cb`（`ble_hs.h:398-404`） |
| (c) 保存対象は固定長で動的確保が不要 | ✅ **的中**——`union ble_store_value`（`ble_store_value_sec` ≒88B）＋ bond 最大3件 |

## 4. 実装（`esp/common/bt/ble_store_flash.c`）

- 予約＝**フラッシュ末尾 64KB**（`ASP3_BLE_STORE_FLASH_OFFSET` 既定 `0x3F0000`．`-D` で上書き可）。
  実データ末尾（0x03dc48）から遠く、アプリが太っても衝突しにくい。
- ROM API はセクタ消去（4KB）しか無いため、**1セクタに全レコードを収めて
  read-modify-write**。像は静的（`.bss`）で **動的確保なし**＝禁則に適合。
- magic＋CRC で「未初期化（全0xff）／中途半端な書込み」を検出し、
  壊れていれば**空で開始しつつ syslog に警告**（鍵が消えたことを黙って進めない）。
- 可逆オプション **`ESP32C3_BLE_STORE_FLASH`（既定 OFF）**。
  アプリは `TOPPERS_BLE_STORE_FLASH` で `ble_store_config_init()` と切替。

### 4.1 耐久性についての正直な注記

**ウェアレベリングはしない**。bond の書込みは「ペアリング成立時」等の稀なイベントに
限られ、消去回数上限に対して桁違いに少ないため。**高頻度書込みには使わないこと**を
ソース冒頭に明記した。

## 5. ★「ビルド成功」を成果と誤認しかけた（記録）

最初のビルドは `rc=0` だったが、**`asp3_ble_store_flash_init` はリンクされていなかった**
——アプリが依然 `ble_store_config_init()` を呼んでおり、**誰も参照しないので
`--gc-sections` に捨てられていた**。「ビルドが通った＝実装が入った」ではない。
アプリ側の分岐を入れて**シンボルの実在を `nm` で確認**するまでを検証とした。

| | ON | OFF（既定） |
|---|---|---|
| `asp3_ble_store_flash_init` | ✓ リンク | — |
| `ble_store_config_init` | ✗（不要） | ✓ リンク |
| build | rc=0 | rc=0（**非回帰**） |
| `.bss` 消費 | **760 B**（`store_image` 0x2f8） | — |

## 6. ★未検証（重要）＝**実機で bond が電源断を跨いで残ることは未確認**

本ラウンドで確認したのは **ビルド成立とシンボルのリンクまで**。
**フラッシュへの実書込み・再起動後の復元は実機で未検証**であり、
「bond が永続化された」とは**まだ言えない**。必要な実機検証：

1. C3 でペアリング → **真cold（物理電源断）** → 再接続で **鍵が残っている**こと
2. `esp_rom_spiflash_*` が **Direct Boot 環境で実際に書けるか**
   （キャッシュとの整合・書込み中の割込みは要確認）
3. 予約領域がアプリ/フラッシュ設定と衝突しないこと（4MB 前提の確認）

C5/C6 への展開は C3 で実機確認できてから（同じ `esp/common/` のソースを使う）。

---

## 7. 実機検証（C3・`60:55:F9:57:BA:BC` rev v0.3・flash 4MB）

### 7.1 ★真因を1つ潰した——`esp_rom_spiflash_config_param` が要る

**予測**：予約領域は書込み前に全 `0xff`。→ **的中**（esptool で確認）。

しかし起動ログは `image invalid (magic=0x00000000)` を出した。フラッシュは `0xff` なのに
コードが読んだ magic が `0x00000000`＝**`esp_rom_spiflash_read` が失敗し、`.bss`(=0) の
ままだった**。当初コードは戻り値を `(void)` で捨てており、**「読めなかった」が
「空のストア」として黙って通っていた**（失敗の握り潰し）。

戻り値を見るよう修正 → **`READ FAILED (rc=1)`** を可視化。
`rc=1` は `ESP_ROM_SPIFLASH_RESULT_ERR`。

**真因＝ROM の SPI flash API は「チップパラメータが設定済み」を前提とするが、
Direct Boot はそれを設定する 2段ブートローダを通らない**。
ESP-IDF 本家は bootloader が `esp_rom_spiflash_config_param()` を呼ぶ
（`bootloader_flash_config_esp32c61.c:131` 等）。

**予測**：`config_param(0, 4MB, 0x10000, 0x1000, 0x100, 0xffff)` を先に呼べば read が通る。
→ **的中**（`READ FAILED` が消え、警告も出なくなった＝`0xff` を正しく読めている）。

⇒ **検証項目2「`esp_rom_spiflash_*` が Direct Boot 環境で使えるか」は
「そのままでは使えない。`config_param` を自前で呼べば使える」と確定**。
検証項目3「予約領域が収まるか」も flash 4MB 実測で確認済み。

### 7.2 ★私のテスト手法の誤り（記録）

ペアリング中に `tmp/rts_boot_capture.py` を走らせたところ接続が切れた。
**同スクリプトは RTS で DUT をリセットする**ため、**観測しようとした BLE セッション自体を
壊していた**（DUT ログに store 初期化が2回＝再起動の証拠）。
memory `c3-usbjtag-serial-open-resets-dut` にある既知の罠を踏んだ。
⇒ **ライブ BLE はホスト側（BlueZ）から観測し、コンソールに触れない**。

### 7.3 ★bond 永続化そのものは **未達**——ただし原因は本実装«ではない»

コンソール非接触でペアリングし直しても失敗：
```
Failed to pair: org.bluez.Error.ConnectionAttemptFailed
```
DUT 側ログ＝**`conn=0 disc=0`**（**接続が一度も成立していない**）、
予約領域も `0xff` のまま（＝bond が成立していないので書込みも起きない）。
∴ **SMP 以前に「接続」段階で失敗**している。

**決定的対照**：`ESP32C3_BLE_STORE_FLASH=OFF`（＝**本実装を含まない**従来の RAM store）を
同じボード・同じ手順で書込んで試行 → **同じ `ConnectionAttemptFailed`**。

⇒ **接続失敗は本実装とは無関係の既存事象**と切り分け完了（相関を因果と早合点しない）。
本ラウンドでは **bond 永続化の可否を判定できない**（前提となる接続が成立しないため）。

### 7.4 現時点の到達点と残り

| 項目 | 状態 |
|---|---|
| 実装（ビルド・リンク・非回帰） | ✅ 完了 |
| **`esp_rom_spiflash_*` が Direct Boot で使えるか** | ✅ **解決**（`config_param` が必要と判明・修正済み） |
| 予約領域が収まるか（flash 4MB） | ✅ 確認 |
| フラッシュ read が成功するか | ✅ 実機で確認 |
| **フラッシュ write（bond 保存）** | ❌ **未検証**——bond が成立しないため到達せず |
| **真cold を跨いだ鍵の復元** | ❌ **未検証**——同上 |

**次の一手**＝先に**接続失敗（`ConnectionAttemptFailed`・`conn=0`）を解く**必要がある。
これは残課題D（`docs/ble-c3-smp-death-plan.md`）と同じ領域の可能性がある
（同 doc は「切断が届かず広告が止まる `DISC=0`」を扱うが、本件は `conn=0`＝
そもそも繋がらない）。**接続が回復してから bond 永続化の判定を行う。**
既定は **OFF のまま**とする。

## 8. 続き：接続問題の切り分け（`docs/c3-ble-connect-plan.md` 段階0 に従う）

### 8.1 ★接続失敗は**環境要因**だった（段階0 で決着）

同 doc は「**まず環境要因を消してから**コード側候補を切り分けよ」と指示しており、
これに従った。**BlueZ アダプタを power off/on**（＋`remove`）したところ：

```
Connection successful
[CHG] Device … ServicesResolved: yes
[CHG] Device … Connected: yes
```

⇒ §7.3 の `ConnectionAttemptFailed`（`conn=0`）は **BlueZ 側の状態**が原因で、
DUT/コードの問題ではなかった。**doc の指示順（環境要因が先）が正しかった**。

### 8.2 残る症状＝**ペアリングが完了しない**（接続はできる）

環境要因を消した後の状態：

| 段階 | 結果 |
|---|---|
| `connect` | ✅ **成功・再現性あり**（`Connection successful`／`ServicesResolved: yes`） |
| `pair` | ❌ `Attempting to pair` の後 **成功も失敗も返らない**。`Paired: no` |
| フラッシュ予約領域 | `0xff` のまま＝**bond が成立しないので書込みも起きない** |
| ペアリング試行後の DUT | **広告が止まる**（`Device not available`）。**リセットで復活** |

「接続直後に `le-connection-abort-by-local` で切れる」事象も1度観測したが、
**同一手順の再試行では再現しなかった**（＝単発。因果を断定しない）。

### 8.3 ★決定的対照：**本実装の有無に依らず同じ**

`ESP32C3_BLE_STORE_FLASH=OFF`（＝**本実装を一切含まない**従来の RAM store）を
同一ボード・同一手順・クリーンな状態（DUT リセット＋BlueZ power cycle）で試行：

```
Attempting to pair with …
	Bonded: no
	Paired: no          ← ON と同一の症状
```

⇒ **ペアリング未完了は本実装とは無関係の既存事象**と確定。
「ペアリング試行後に広告が止まり、リセットで復活する」という挙動は、
残課題D の証跡 `evidence-rc-c3-P1-wedge-mbuf-exhaustion.md` が記録する
**wedge（mbuf 枯渇で送信キューが詰まり reset が要る）** と同型に見える
（ただし**同一と断定はしない**——本ラウンドでは mbuf プールを実測していない）。

### 8.4 本ラウンドの結論

| 項目 | 状態 |
|---|---|
| 実装・ビルド・リンク・非回帰 | ✅ |
| **Direct Boot でのフラッシュ read** | ✅ **解決**（`config_param` が必要と判明・修正） |
| 予約領域が 4MB に収まる | ✅ |
| 接続（connect） | ✅ **環境要因を消せば成功**（コード問題ではなかった） |
| **フラッシュ write（bond 保存）** | ❌ **未検証**——bond が成立しないため到達しない |
| **真cold を跨いだ復元** | ❌ **未検証**——同上 |

**bond 永続化の可否は、依然として判定できない。** 前提となるペアリングが
（本実装と無関係の理由で）完了しないため。**既定は OFF のまま**とする。

**次の一手**＝残課題D（ペアリング後の wedge／`DISC=0`）の解決。
それが済めば bond 永続化の判定は**そのまま続行できる**（実装・フラッシュ read は動作確認済み）。

# C6 evidence-08 — **BT の hal 参照 0 を達成（122箇所の移行）**

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C6 `14:C1:9F:E0:5A:9C`＝board C**（hub `1-6` port2）
前段: `evidence-c6-07`（D-2c/D-2d 達成・BT 既定は v5.5.4 submodule だが**基盤は hal**）
ユーザー判断: **「改名より先に 122箇所の移行をやる」**（移行が終われば hal 経路が消え→
`ESP32C6_BT_IDF61` トグルごと不要→C5 と同型＝**改名の議論自体が消滅する**）

★**認証情報は一切使用していない**（BT のみ）。

---

## 1. 結論（先に4行）

1. **★`ninja -t deps` の hal 参照 0 を達成**（**全5指標で 0**。§3）。**既定（フラグ無し）で 0**。
2. **踏んだ壁は 2件だけ**で、**両方とも C5/C3 の «既知» で解けた＝新規壁ゼロ**（§2）。
   ★**`shared_periph_module_t` は «BT ツリーを丸ごと移す» と消えた**＝C3 の知見
   「**壊れているのは «混ぜたこと» そのもの**」が C6 でも**実測で再現**。
3. **実機非回帰＝D-1・D-2b とも 真cold・warm で維持**（§4）。
4. **可逆＝`-DASP3_ESPIDF_SUPPLY=OFF` で hal 供給へ完全復帰**（hal deps 1924/2139・ビルド通過）（§5）。

---

## 2. 実装＝**C5 からの転写**（新規設計はしていない）

### 2.1 122箇所の «内訳» を先に測った（＝どこを触るべきか）

| ファイル | `ESP_HAL_DIR` | 位置 | 扱い |
|---|---|---|---|
| **`esp_bt_idf61.cmake`** | **47** | **既定（非hal）経路＝アクティブ** | **★ここを移行した** |
| `esp_bt.cmake` | 75 | **全て 220-533 行＝`else()` の «hal 経路» ブロック内**（`if(ESP32C6_BT_IDF61)`=59／`else()`=61／終端=608） | **触らない**＝これは «hal を使う» 経路そのもの。移行しても意味が無く、**endgame は削除**（§6-1） |

★**「122箇所を全部書き換える」ではなかった**。**アクティブ経路の 47 だけが «hal 参照 0» を決める**。

### 2.2 置換規則＝**C5 と同一**（`asp3/target/esp32c5_espidf/esp_bt.cmake` は `ESP_HAL_DIR` **0箇所**）

```
${ESP_HAL_DIR}/components/esp_hal_<x>/…   →   ${ESP_SUP_HAL_<x>}/…
${ESP_HAL_DIR}/components/<other>/…       →   ${ESP_SUP_DIR}/components/<other>/…
```

- **機構は C6 の `target.cmake` に «既に» あった**（`ESP_SUP_DIR`＝供給元選択／
  `ESP_SUP_HAL_<x>`＝`esp_hal_<x>` 分割↔`components/hal` 集約の吸収．**C5 と同一実装**）。
  ⇒ **対応表を新規に書く必要は無かった**。**`esp_bt*.cmake` だけが機構を使っていなかった**。
- 順序も確認済（`ESP_SUP_DIR` set=102/107・`foreach` ESP_SUP_HAL=116 → `esp_bt.cmake` include=418）。
- **事前チェック**：移行先（esp-idf）に**ソース 15本・include dir 21本が «全て実在»**（欠落 0）を
  確認してから置換した。
- 結果：**`esp_bt_idf61.cmake` の `ESP_HAL_DIR` 47 → 0**、生成パターンは **C5 と一致**
  （`${ESP_SUP_HAL_clock}/${BT_CHIP_SERIES}/clk_tree_hal.c` 等）。

### 2.3 ★踏んだ壁は2件・**新規壁ゼロ**（依頼 (b)）

| # | 壁 | 解き方 | 出所 |
|---|---|---|---|
| **1** | **`shared_periph_module_t` / `soc_clk_freq_calculation_src_t` / `CLK_CAL_*` / `CLK_HAL_TAG` 未定義**（`evidence-c6-05` §5.6 で «壁» として記録したもの） | **★何もしていない＝«BT ツリーを丸ごと移した» ら消えた** | **C3 の知見／HANDOFF §4-3-5「壊れているのは «混ぜたこと» そのもの」＝C6 でも実測で再現**。**部分移行が壁を作っていた** |
| **2** | `undefined reference to 'lp_timer_hal_get_cycle_count'` | esp-idf 供給時のみ `${ESP_SUP_DIR}/components/hal/lp_timer_hal.c` をリンク | **★C5 が同じドリフトを記録済**（`esp32c5_espidf/esp_bt.cmake:247-249`「hal/rtc_timer_hal.h（hal供給時）／hal/lp_timer_hal.h（esp-idf供給時）．rtc_time.cが要求．**ソースも同じ供給元から取る**ので名前差は消える」）。実測＝hal の `rtc_time.c` は `rtc_timer_hal_get_cycle_count`／esp-idf の `rtc_time.c` は `lp_timer_hal_get_cycle_count` を呼ぶ。**esp-idf 側だけ実体が独立した `.c`** にあるため明示リンクが要る（hal 側は不要＝実測） |

★**C3 が挙げた «新規壁» の `esp_task.h:25 → freertos/FreeRTOSConfig.h` は C6 では発生しなかった**
（C6 BT は既に C3型グルー＋`bt_esp_timer_ext.h` 等の force-include が入っているため）。
★**C3 が挙げた «罠»（古い `option(... OFF)` が前方に居ると先に cache へ焼き付く）も未発生**
（`ASP3_ESPIDF_SUPPLY` の option は1箇所のみ＝重複宣言が無い）。

---

## 3. ★hal 参照 0 の実測（依頼 (a)）— **指標を明示**

★`.d` は `ninja -t deps`。**`-L`/`-T` は deps に出ない**ので**リンク行から別途**数えた。

| 指標 | **移行後（既定）** | 移行前（`evidence-c6-07` の e07_d2d） |
|---|---|---|
| **[1] `ninja -t deps` の hal 行数**（`.d`＝実際に読まれたヘッダ） | **★0** | **3182** |
| [2] コンパイル対象 hal `.c`（build.ninja・ユニーク） | **0** | 15 |
| [3] `-I` hal インクルード dir（ユニーク） | **0** | 39 |
| [4] **リンク行の `-L`/`-T` hal**（★deps に出ない＝別途） | **0** | 1 |
| [5] build.ninja 中の hal パス（ユニーク） | **0** | 107 |

**構成別（全て `ninja -t deps` の hal 行数）**：

| build | 構成 | hal deps |
|---|---|---|
| `bt_dflt` | 既定（BT・D-1 系） | **0** |
| `ble_dflt` | 既定（NimBLE・D-2b 系） | **0** |
| `ble_sm` | 既定＋`ESP32C6_BT_IDF61_SM=ON`（D-2c/D-2d 系） | **0** |
| `ble_v61` | `ASP3_BT_IDF_V554=OFF`（外部 v6.1 BT） | **0** |
| `wifi` / `plain` | 非BT（従来から HAL-free） | **0** |

★**`evidence-c6-07` の «コーディネータの 3121 は再現できなかった» を訂正する**：
**それは `ninja -t deps` の hal 行数だった**（私の実測 **3182**＝同一指標・ビルド構成差）。
**私が `build.ninja` 系の指標しか測っておらず `deps` を測っていなかった**のが原因。
⇒ **コーディネータの数値が正しく、私の探索が不足だった。**

**既定の変更**（`target.cmake`）：**`ASP3_ESPIDF_SUPPLY` の «BT だけ OFF» という例外を撤去**し
**全構成で既定 ON**。旧例外の根拠2つは両方とも実測で消えている：
1. 「hal-base＋v6.1-BT にしか実機実績が無い」→ `evidence-c6-05/06/07` で
   **v5.5.4 供給が D-1/D-2b/D-2c/D-2d を真cold・warm とも達成**（v6.1 は warm のみ）。
2. 「esp-idf base と hal の esp_hw_support を混ぜると壊れる」→ **«混ぜたこと» が原因**で、
   **BT ツリーごと移したら壁は消えた**（§2.3-1）。

---

## 4. 実機非回帰（依頼 (c)）＝**移行後の «hal 参照 0» ビルドで実測**

**真cold の証明**＝`uhubctl -l 1-6 -p 2 -a off` ＋ **by-id 読み戻し 0** ＋ **センチネル**
（bt_smoke_c6＝STORE6 に `0xCAFE5A9C`→`0x00000000`／ble_host_smoke_c6＝空き store が無いので
**判定対象 STORE0 自身**に置き `0xCAFE5A9C` 残存を «POR 未発生＝無効» として弾く自己検証）。
★**cold 中は UART を開いていない**（判定は LP_AON 直読み）。
★**`STORE4`(=`RTC_XTAL_FREQ_REG`)・`STORE1`(=`RTC_SLOW_CLK_CAL_REG`) は判定に使っていない。**

| 構成（**hal deps=0**） | 電源 | 結果 |
|---|---|---|
| **`bt_smoke_c6` 既定＝D-1** | **真cold** | **`STORE0=0xb1d00008`＝D-1**・`STORE7=0xa1020704`・sentinel `0x00000000` |
| 同 | **warm** | **`0xb1d00008`＝D-1**・`0xa1020704` |
| **`ble_host_smoke_c6` 既定＝D-2b** | **真cold** | **sync `0x5ade51c0`／adv `0x0ade5000`／rc `0xad000000`／intr `0xa1020704`** |
| 同 | **warm** | 同左 |
| **`ble_host_smoke_c6` ＋SM=ON（D-2c/D-2d 構成）** | **真cold** | 同左（tripwire＝`ble_sm_pair_initiate`/`tc_aes_encrypt`/`ble_store_config_init` 各 1） |

⇒ **hal を1バイトも参照しない構成で D-1／D-2b が真cold・warm とも維持**。
★**D-2c/D-2d はスマホ central が要る**ので**デバイス側準備までで止める**（親指示）＝
**board は `ASP3-C6-BLE` で広告中**（**hal-free ＋ SM=ON ＋ 真cold ブート**）。
手順・裏取りマーカは `evidence-c6-07` §4 が**そのまま使える**（アプリ・マーカとも不変）。

---

## 5. 可逆性（依頼 (d)）

| build | 指定 | hal deps | ビルド |
|---|---|---|---|
| `rev_bt` | **`-DASP3_ESPIDF_SUPPLY=OFF`** | **1924** | **PASS** |
| `rev_halbt` | `-DASP3_ESPIDF_SUPPLY=OFF -DESP32C6_BT_IDF61=OFF`（＝完全 hal） | **2139** | **PASS** |

⇒ **`=OFF` 一発で hal 供給へ «完全復帰» し、従来の hal 版ビルドも壊れていない**（8構成 PASS／FAIL 0）。

---

## 6. 残ブロッカー／ユーザー判断事項（依頼 (e)）

1. ★**`esp_bt.cmake` の hal 経路（`else()` ブロック 61-608・`ESP_HAL_DIR` 75箇所）は «残っている»**。
   **ユーザーの筋書き（hal 経路が消える→`ESP32C6_BT_IDF61` トグルごと不要→C5 と同型→改名不要）**
   を完遂するには**この削除**が要る＝**次の1手**。
   ★**本ラウンドで削除しなかった理由**：(i) 親指示「一度に全部やろうとするな・壁ごとに刻んで commit」、
   (ii) **削除は «可逆性を捨てる» 変更**＝`-DESP32C6_BT_IDF61=OFF` の退避路が消える。
   **`hal 参照 0` という目標は削除しなくても達成済**（hal 経路は既定で通らないため）。
   ⇒ **削除の是非はユーザー判断**（＝「退避路を捨ててよいか」）。
2. **`ESP32C6_BT_IDF61` の誤称**は**まだ残る**（既定供給は v5.5.4 なのに名前が "IDF61"）。
   ★ただし**1 を実施すればトグルごと消えて誤称も消滅する**＝ユーザーの筋書きどおり。
3. `ble_host_smoke_c6.c` の **STORE4=`RTC_XTAL_FREQ_REG` / STORE1=`RTC_SLOW_CLK_CAL_REG` 潰し**は未修正
   （STORE0-9 全使用で空き無し＝「判定に使わない」運用で回避中）。
4. **§10-12 hal ハングの正体**は未解決（`evidence-c6-06` §6．次の1手＝RTS/EN リセットで
   `STORE5`(clk src) を読む＝1 run。ただし候補1だけでは §13 の v6.1 成功を説明できない＝穴あり）。
5. **D-2c/D-2d の «hal-free 構成での» OTA 再確認**はユーザー待ち（デバイス側準備完了・広告中）。
   ★ただし `evidence-c6-07` で **同一アプリ・同一マーカ**の D-2c/D-2d は達成済＝
   **本ラウンドの変更は «供給元» だけでアプリは1バイトも変えていない**。

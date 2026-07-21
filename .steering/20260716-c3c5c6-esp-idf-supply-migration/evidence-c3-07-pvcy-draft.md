# C3 evidence-07 — PVCY draft(ii) 投入／**BlueZ 非回帰(C0) を先に**／予測を実機前に登録

日付: 2026-07-17 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C3 `<MAC-19>`**（hub `1-6` port1）

> ★**§3 は実機を1回も触らずに書き、実機の前に commit した。以後書き換えない。**

---

## 1. 出発点（コーディネータの実測．**私が検算した**）

| marker | 値 | 意味 |
|---|---|---|
| CONN `0x600080C0` | `0x604e0001` | **接続は成立**（毎起動クリア＝そのブートの話） |
| **ENC `0x60008058`** | **`0x5de0000d`** | **ENC_CHANGE 到達・status=`0x0d`** |
| PAIR `0x60008054` | `0xa1020805` | **未発火**（`0x5DC0` タグ無し＝bt_shim alloc trace の残留） |
| DISC `0x600080B8` | `0x00000000` | **切断イベントが来ていない**（毎起動クリア） |

**★私の検算**：`BLE_HS_ETIMEOUT = 13 = 0x0d`（`hal/.../host/ble_hs.h:108`）⇒
**コーディネータの読みは正しい**。

### 1.1 ★★私の evidence-c3-06 の STORE 表に誤りがあった（自己訂正）

**evidence-c3-06 §5.2 は `0x60008058` を «WRITE(`0xABF3`) マーカ» としか書かなかった。**
**実際は «共用» で、しかも ENC_CHANGE が «後勝ち» する**（ソース `ble_host_smoke.c:157-160, 555-582`）：

| tag | 意味 |
|---|---|
| `0x7717ccff` | `0xABF3` WRITE 受信 |
| **`0x5DE0<delta秒><status>`** | **GAP ENC_CHANGE 到達＋status**（★**後勝ちで WRITE を上書きする**） |

⇒ **私の引き継ぎ表は不完全で，読み手を誤らせ得た**（コーディネータは正しく読んだが，それは
私の表のおかげではない）。**本ファイルで訂正する**。
★教訓：**「8個の STORE は全て使用中」で «共用» が常態**（ソースのコメントが明記）
⇒ **マーカ表は «アドレス» ではなく «タグ» で書かねばならない**。

### 1.2 病態（コーディネータの整理．私も同意）

**「connect できない」のではない**＝**接続成立 → 暗号化が ETIMEOUT → 切断イベントが来ない →
デバイスがリンクを握ったまま広告を止める**。2回目以降が失敗するのは**もう広告していないから**
（`ble_host_smoke.c:531` の «切断後に再広告» は，その切断が来ないので発火しない）。**復旧は電源断のみ。**

### 1.3 ★決定的な非対称

| central | アドレス種別 | 結果 |
|---|---|---|
| **BlueZ `hci0`** | **public**（`<MAC-33>`） | **bond 成功**（私が真cold 2/2 で実証） |
| **Android** | **RPA** | **ENC ETIMEOUT 2/2**（コーディネータ実測） |

**同一デバイス・同一 hal ビルド（`ASP3_ESPIDF_SUPPLY=OFF`・計装0）・central だけが違う。**
⇒ **RPA/アドレス解決が絡む経路**が容疑＝PVCY 仮説の根拠。

★**HANDOFF §4-4 の `AuthenticationTimeout` と本件 `BLE_HS_ETIMEOUT` の関係は未検証＝同一視しない**
（`AuthenticationCanceled` ≠ `AuthenticationTimeout` と私自身が言った教訓の適用）。
★また **evidence-c3-03〜05 の «esp-idf 供給の bond 失敗»（`AuthenticationCanceled`・`ltk_req=0`）
とも別現象**：**本件は hal 供給で `ltk_req>0`・ENC_CHANGE 到達・status=ETIMEOUT**＝**症状が違う**。
**混ぜない。**

---

## 2. 投入するもの＝**draft (ii) だけ**（一度に1つ）

**`ESP32C3_BT_PVCY_FILTER=ON`**（`esp_bt.cmake:675`．既定 OFF）
＝`bt/hci_pvcy_filter.c` が `esp_vhci_host_send_packet` を `--wrap` し、
**`0x202D`（LE Set Address Resolution Enable）の enable バイトを 0 に潰す**
（Command Complete は偽造しない＝有効なコマンドとして送りコントローラに正常応答させる）。

★**draft (i)（on_sync で resolve 無効化）は «入れない»**——**同時に入れるとどちらが効いたか
分からなくなる**（コーディネータ指示・私も同意）。

### 2.1 ★「本当に `0x202D` が送られるのか」＝**静的に証明した**（計装を足さずに）

**懸念**：もし `0x202D` が «そもそも送られていない» なら，本フィルタは**無言の no-op**であり、
**Android が依然失敗しても «PVCY 仮説が反証された» とは言えない**（＝計器が黙っているだけ）。
★**本セッションで «効いていない --wrap» を実際に踏んだ**ので、**必ず確かめる**。

**当初は RTC FAST への «発火カウンタ» を足したが、`hci_pvcy_filter.c` は
ドラフトのまま維持する方針となったため撤去した。代わりに «静的に» 証明した**
（＝**計装を足さずに済んだ＝侵襲リスクゼロ**．こちらの方が良い）：

**実測（`build/c3_pvcy/asp.elf` の逆アセンブル）**：

```
<ble_hs_pvcy_set_our_irk>:
  lui  a0,0x2 ; addi a0,a0,45   # 202d      ← ★opcode 0x202D を組み立てて
  li   a2,1                                  ← cmd_len=1（enable 1バイト）
  jal  <ble_hs_hci_cmd_tx>                   ← 送出
  lui  a0,0x2 ; addi a0,a0,41   # 2029      ← 続いて resolving list Clear
  jal  <ble_hs_hci_cmd_tx>
  lui  a0,0x2 ...                            ← さらに 0x202D（enable=1）
```

- **分岐の同定**：`ble_hs_pvcy.c:91` が `#if (!MYNEWT_VAL(BLE_HOST_BASED_PRIVACY))` ⇒
  **本ビルドは `#else` 側**＝`set_resolve_enabled(0)` → `clear_entries()` → **`set_resolve_enabled(1)`**。
- **起動経路であること**：`ble_hs_pvcy_set_our_irk` は **`ble_hs_startup.c:573/584` から呼ばれる**
  （＝**ホスト起動時**）。
- ⇒ **`0x202D` は起動時に «実際に» 送られる。∴ フィルタは発火する。**

★**`ble_hs_hci_cmd_tx` → …→ `esp_vhci_host_send_packet` → 本 wrap**。
wrap の実効性は §3.2-P1 のとおり**逆アセンブルで direct jal ×2 を確認済み**。

## 3. ★★事前登録予測（実機前）

### 3.1 ★C0 を先に（省かない）

| # | 予測 | 確度 | 根拠 |
|---|---|---|---|
| **C0** | **`PVCY_FILTER=ON` で BlueZ の bond が «壊れない»** | **85%** | BlueZ `hci0` は **public アドレス**＝**アドレス解決は原理的に不要**。`0x202D` を 0 にしても public central との bond には効かないはず。★15% は「NimBLE が自身の privacy 状態と controller の実状態の不一致で内部エラーを起こす」可能性 |

★**C0 が外れたら（BlueZ が壊れたら）＝ そこで止めて報告する**（コーディネータ指示・私も同意）。
**「Android で直った」が「BlueZ を壊した代償」では意味がない。**

### 3.2 フィルタ自体

| # | 予測 | 確度 | 根拠 |
|---|---|---|---|
| **P1** | **`--wrap` が実際に効く**（逆アセンブルで `__wrap_esp_vhci_host_send_packet` への直 jal がある） | **90%** | ドラフト自身が「`esp_nimble_hci.c` から名前付き直 jal・`pvcy1.elf` で jal 2件確認・`acl_trace.c` も同関数 wrap 実績」と記録。★ただし**本セッションで «効かない wrap» を踏んだので必ず実測** |
| **P2** | **`0x202D` が起動時に実際に送られる** | **★予測ではなく «静的に確定»**（§2.1） | `ble_hs_pvcy_set_our_irk`（`ble_hs_startup.c:573/584` から呼ばれる）が **`#else` 側で `0x202D` を2回（enable=0→clear→enable=1）送出**することを**逆アセンブルで確認**。∴ **«無言の no-op» ではない**＝Android が直らなくても «試していない» とは言えない |

### 3.3 ★含意の自問（「反証条件も仮説である」）

| | 主張 | 健全か |
|---|---|---|
| (a) | 「C0 が通る ⇒ フィルタは無害」 | **健全でない**＝**BlueZ(public) で無害でも Android(RPA) 経路で悪さをする可能性は残る**。**C0 は «退行が無いこと» までしか示さない** |
| (b) | 「filt>0 かつ Android が直った ⇒ `0x202D` が真因」 | **健全でない**＝**filt>0 は «潰した» ことしか示さない**。同時に別の何かが変わった可能性（ビルド差）を排除するには **`PVCY_FILTER=OFF` で Android が «再び» 失敗すること**（＝逆方向対照）が要る。**ユーザーに2回試させる必要がある** |
| (c) | 「filt=0 ⇒ PVCY 仮説は死んだ」 | **★おおむね健全だが限定付き**＝死ぬのは **«draft(ii) が狙った `0x202D`»** であって、
**`0x204E`(LE Set Privacy Mode) や resolving-list 投入（`0x2027`/`0x2029`）** は別途生きている。**「(ii) は効かない」までしか言えない** |
| (d) | 「Android が直らない ⇒ PVCY 仮説は死んだ」 | **★§2.1 で «送られる» を静的に確定させたので «試していない» の芽は潰した**。ただし死ぬのは **(c) と同じく «draft(ii) が狙った `0x202D`» のみ**（`0x204E`/resolving-list 投入は別途生きている） |

### 3.4 手順（固定）

1. **ビルド → `nm`/逆アセンブルで wrap の実効性を確認**（P1）
2. **★C0：BlueZ で真cold bond**（＝私が実行）。**壊れたら止めて報告**
3. **P2 は静的に確定済**（§2.1）＝**RTC を読む必要が無い＝広告を止めずに済む**
4. **ユーザーへ Android 再試行を依頼**（デバイス側準備＋手順の提示まで）

---

## 4. この時点の残（実機前）

1. 実機未測定。§3 を先に固定した。
2. **draft(i) は入れていない**（一度に1つ）。
</content>

---

## 5. ★★実機結果 — **C0 が失敗＝draft(ii) は BlueZ を壊す ⇒ 宣言どおり «ここで止めた»**

**全 run 真cold**（`-p 1 -a off` ＋ by-id 読み戻し 0 を毎回確認）・同一 central(`hci0`)・
同一手順・毎回 `bluetoothctl remove`（フレッシュ化）。

### 5.1 ★C0 の結果（依頼 (a)）

| build | 差分 | BlueZ bond |
|---|---|---|
| **`PVCY_FILTER=ON`**（draft(ii)） | **これだけ** | **1/4 成功（3/4 失敗＝`AuthenticationCanceled`）** |
| **baseline `PVCY_FILTER=OFF`**（★**同一セッションで «今» 測り直した**） | — | **3/3 成功** |
| baseline（本日の既存分：B アーム×2・C0b・C0再） | — | 4/4 成功 |

⇒ **baseline 合計 7/7 成功 ／ draft(ii) 1/4 成功。**

### 5.2 ★なぜ «環境ドリフト» を排除できるか（決定的対照）

**run #1 が失敗した時点で «壊れた» と結論しなかった**（本セッションで «初回 run の外れ値» を
既に踏んでいる）。実際 **run #2 は成功**＝**1/2 では何も言えない**。
∴ **run #3/#4 を追加**（→3/4 失敗）し、**さらに «今この場で» baseline を測り直した**（→3/3 成功）。

★**これが要点**：baseline の 4/4 は «数時間前» のもので、**ホスト BT スタックの状態や RF 環境が
変わった可能性**があった。**同一セッションで baseline を再取得して 3/3 を得た**ことで、
**「環境が壊れたのではなく draft(ii) が壊している」**と帰属できる。
（**この対照を省いていたら «環境のせい» とも «draft のせい» とも言えなかった**。）

### 5.3 (b) どちらの draft か／`--wrap` は効いているか

- **投入したのは draft(ii) のみ**（`ESP32C3_BT_PVCY_FILTER=ON`）。**draft(i) は入れていない**（一度に1つ）。
- **★`--wrap` は «実際に効いている»**（逆アセンブルで実測．P1 的中）：
  ```
  420069c2: jal 42018f78 <__wrap_esp_vhci_host_send_packet>
  42006a4a: jal 42018f78 <__wrap_esp_vhci_host_send_packet>
  ```
  ＝**direct jal ×2**。`__wrap_esp_vhci_host_send_packet` は `T` シンボルとして実在。
- **★`0x202D` は起動時に実際に送られる**（§2.1．**逆アセンブルで静的に確定**）
  ⇒ **フィルタは «無言の no-op» ではない＝確かに発火して enable を潰している**。
- ∴ **「効いていない計装」ではない。draft(ii) は «意図どおり動作して、その結果 BlueZ を壊した»。**

### 5.4 予測の答合せ（依頼 (d)）

| # | 予測 | 確度 | 実機 | 判定 |
|---|---|---|---|---|
| **C0** | `PVCY_FILTER=ON` で BlueZ の bond が壊れない | **85%** | **1/4 成功＝壊れる** | **★外れ** |
| **P1** | `--wrap` が実際に効く | 90% | direct jal ×2 | **★的中** |
| **P2** | `0x202D` が起動時に送られる | （静的に確定） | 逆アセンブルで確認 | **★確定** |

★**C0 の 15%（「NimBLE の privacy 状態と controller の実状態の不一致で内部エラー」）を
引いた**。**「public central にはアドレス解決は原理的に不要だから無害のはず」という私の
推論が «実機で反証された»**＝**原理的に不要でも、ホストの内部状態と食い違わせると壊れる**。

### 5.5 機構の仮説（★**主張しない**．測っていない）

**測った事実**：draft(ii) は BlueZ bond を 7/7 → 1/4 に劣化させる。
**仮説（未検証）**：フィルタは `set_resolve_enabled(0)` と **`set_resolve_enabled(1)` の両方**を
enable=0 にする（前者は元々0＝no-op、後者が実効）。**コントローラは解決 OFF・NimBLE の host 状態は
«ON» と信じている**（Command Complete は成功で返るため）。この不一致が SM 中の
アドレス種別／IRK 配布の扱いに影響する可能性——**が、これは仮説であって実測していない**。
★**`AuthenticationCanceled` は esp-idf 供給アームの失敗（evidence-c3-03〜05）と «同じ表現»**だが、
**同一視しない**（本件は hal 供給・別の変更点・別の文脈）。

---

## 6. 残ブロッカー／★次に聞くべきこと（依頼 (e)）

1. **★draft(ii) は «このままでは» 投入できない**（BlueZ を壊す）。**ユーザーへの Android 再試行は
   依頼していない**——**壊れた土台で «Android で直った» を得ても意味がない**（コーディネータの
   指示どおり・私も同意）。
2. **★draft(i)（on_sync で resolve 無効化）は «未試行»**。**これは別機構**
   （＝「一瞬有効になる窓」を許すが、**enable=1 の Command Complete を正規に受けてから
   ホスト状態と整合させて無効化する**）⇒ **§5.5 の «状態不一致» 仮説が正しければ、
   draft(i) は draft(ii) の欠点を持たない可能性がある**。**ただしこれも C0 を先に測る必要**。
3. **★私が «試すべきか» を決められない事項**：
   - **draft(i) を試すか**（＝もう1ラウンド．C0 → BlueZ 非回帰 → ユーザー Android）
   - **あるいは «Android 対応» 自体を «要件» と見なすか**——**BlueZ(public central) では
     D-2c/D-2d が真cold で通る**ことは既に実証済み。**Android(RPA) 対応が必須要件かは
     プロダクト判断**であって技術判断ではない。
4. **本件と «esp-idf 供給の bond 失敗»（evidence-c3-03〜05）は別現象**として扱うこと
   （症状：本件＝hal 供給・`ltk_req>0`・ENC_CHANGE 到達・status=ETIMEOUT ／
   あちら＝esp-idf 供給・`ltk_req=0`・ENC_CHANGE 未到達）。**混ぜない。**
5. **board 最終状態**：**known-good baseline（`PVCY_FILTER=OFF`・`ASP3_ESPIDF_SUPPLY=OFF`・
   SM=ON・計装0）を真cold で焼いて広告中・stale bond 削除済み**
   ＝**evidence-c3-06 の «スマホ引き渡し» 状態に戻してある**（Android は依然 ETIMEOUT のはずだが、
   **BlueZ での D-2c/D-2d は測れる状態**）。

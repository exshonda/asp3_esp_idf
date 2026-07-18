# evidence-rc-c3 Steps C–F — C3 BLE «supervision timeout 死» の機序調査（系統的除外＋方法論的訂正）

2026-07-18。HEAD=`0867aba`（#1–#7 込み）。board C3=`60:55:F9:57:BA:BC`（ttyACM1・USB-JTAG のみ）。
host hci0=`8C:1D:96:BA:6D:BD`。ビルド＝`ble_host_smoke`（esp-idf 供給=ON・QEMU=OFF・GCC14 `esp-14.2.0_20260121`）。
前段＝`evidence-rc-c3-reconnect-prereg.md`（rc-c3 Step 0–B）。

## 0. 背景（前段で確立・再導出しない）
C3 BLE 接続は間欠的に supervision timeout(0x08) で沈黙死。安定不変量＝**DISC=0**（切断イベントが device host に配送されず、device が conn=1 を永久固持＝**wedge**・reset でしか復帰）＋リンクは能動終了でなく沈黙死。**局在＝我々の統合**（stock ESP-IDF は同一ボード/端末で通る＝blob でない・host は正しく PDU を出す＝host ソフトでもない）。本ラウンド群は「なぜ死ぬか」の «層» を系統的に絞った。

★新capability（本セッションで確立）：**C3 コンソール(USB-JTAG)は RTS reset→boot→hold でリセットループ無しにクリーン読取可**（従来 CDC ハザードで困難とされた）＝device 側ライブ観測が可能。RTC STORE は esptool read-mem 信頼可（RAM 0x3fc8xxxx は不可）。

## 1. Step C — 素 host 接続保持テスト（conn param を stock と比較）
- **素 host（BlueZ・ペアリング無し）接続は 5/6 が >18s 保持・1/6 落下**＝スマホ無しでは wedge は稀。
- 能動切断（bluetoothctl disconnect）は device に **配送される**（GAP DISCONNECT・再広告）＝**DISC=0 は supervision-timeout 死«限定»**（Step B と整合）。
- **実リンク conn param（app 計装・revert 済）**：`itvl=40(50ms) latency=0 suptmo=42(420ms)`＝supervision timeout 420ms（BlueZ 既定）。
- **我々の app は `ble_gap_update_params` を呼ばない＝central の param を passive 受容**。**stock bleprph も同じ**（README の update は central 主導・suptmo=500 はその central の値）。
  ⇒ ★**«conn param / supervision timeout の扱いが stock と違う» 説を否定**（両者 passive・値はセントラル依存）＝**仮説殺し**。

## 2. Step D — controller stall 計装（timer/ISR servicing）
BLE ctrl ISR カウント（線1/2＝`esp_shim_int_count[1]/[2]`）＋`bt_timer` 発火カウンタを 100ms probe（scratch・revert 済）。WEDGE/RECOVERY をスマホ無しで両方再現。
- ★**P_timer_stall＝反証**：bt_timer は全 wedge/死亡を跨いで発火継続（5→7・2→4）＝**timer は決して stall しない**＝**#5 の timer 起床経路は本死亡の機序でない**。
- ★**P_isr_stall＝部分的に真だが «因果でない»**：i1 は死亡点で凍結するが、idle では 11s 前に凍結・保持接続でも凍結・**死亡直前に >420ms の «先行» stall 無し**＝**リンク喪失の «帰結» で «原因» でない**。「shim ISR stall が supervision timeout を起こす」は不支持。
- i1/i2 凍結は «ACL/ペアリング活動の有無» で決まる bimodal baseline。⇒ redirect：runtime servicing でなく **INIT/LL/RF**。

## 3. Step E — RTC/SYSTIMER 精密計装（µs・接続イベント間隔）
線1 ISR で SYSTIMER-lo を読み間隔分布を RTC STORE 記録（scratch・revert 済）。DISC=0 wedge 3回再現。
- ★**線1割込み間隔は «死亡の指紋を持たない»**：max_gap≈**49.8ms**（=1 conn interval）・[50-400ms]gap=0・**drift <0.4%**。**wedge/健全接続で «同一»**。⇒ **P_missed_events・P_drift ともに棄却**。
- ★★**構造的洞察**：**線1割込みは «健全接続も含め» 接続開始 ~0.6-1.2s で停止**（初期活動の後、コントローラが host 割込み «無し» で自律維持）＝**死亡は割込みストリームの «構造的に外»**（当時は «idle 相の死» と解釈）。
- 副産物：**C3 SYSTIMER は 16MHz**（親の µs 閾値は 16× 誤り＝サブエージェントが自己訂正）。

## 4. Step F — SM 有無 A/B（死は SMP トリガか純 idle か）★本ラウンド群の転換点
ソース非変更・build option のみ。A=`ESP32C3_BT_SM=ON`（t+5s SecReq→SMP）／B=`OFF`（純 idle・SMP 皆無）。各素 host 接続を分類（HELD/RECOVERY/**WEDGE**=host 落下かつ console に GAP DISCONNECT «無し»）。
- **Build A（SM ON・出荷既定）：20/20 HELD・0 WEDGE**（SecReq 発火・ENC 失敗でもリンク生存）。
- **Build B（SM OFF）：7/12 WEDGE**・全て **~3s（post-MTU/pre-SUBSCRIBE・SecReq(t+5s) より «前»）**。
- 交絡排除：A2（8/8 HELD）を B と «同時刻» に実行＝RF/host drift でなく **build image に追従**（Fisher ≈ p<0.001）。

### 判定（事前登録・枠外・書き換えない）
- ★**P_smp_triggered＝反証（«逆»）**：SMP を出す A が 20/20 生存・SMP 皆無の B が wedge。**SMP-trigger 説 死亡。**
- **P_idle_independent＝機序は半分支持（SMP 非依存は真）だが «A≈B» の予測不成立**（0 vs 7）。
- **P_inconclusive＝非該当**。**真の結果は全枠外。**

### ★★本ラウンドの発見（枠外）
1. **wedge は SMP 非依存**（B は SMP ゼロで DISC=0 wedge）。
2. **wedge は «早期»（≤3s・pre-SUBSCRIBE）＝«idle death» でも «t+5s SMP 近傍» でもない**＝従来 framing の時間一致は **偶然だった（訂正）**。
3. ★**wedge は «build image 依存»**：death は SM 条件コード(t+5s)より «前»＝0-vs-7 差は SMP 意味論でなく **t=0 から在る image 差（layout/size：A は IROM ~36KB 大／GATT-DB／connect-config）**。**«SM が守る» と帰属しない（lead に留める）。**

## 5. ★★方法論的含意（本ラウンド群の最重要成果）
- **wedge は layout 敏感**＝**Step D/E で足した «計装» が wedge を摂動した疑い**（rigor 標準「計装が測定対象を変える」の実例）＝**計装ベースの直接測定は信頼性が損なわれる**。
- ★**出荷既定（SM ON）は素 host wedge を «ほぼ回避»（0/20）**＝**素 host wedge は主に «非出荷/計装 build» の layout artifact**。**実際の出荷問題は «スマホ SMP 失敗»（別 regime・phone は本物の pairing）。**

## 6. 系統的に «殺した» 仮説（本ラウンド群の実質）
conn param policy（C）／timer stall・ISR servicing stall «が原因»（D）／missed-events・active-phase クロックドリフト（E）／slow-clock ドリフト（config `sleep_mode=0`＝main clock で undercut・Step E で main clock 正確）／**SMP/暗号化トリガ（F・反証）**。**すべて否定。**

## 7. 言えること / 言えないこと（正直に）
- **言える**：素 host wedge は DISC=0・早期(≤3s)・SMP 非依存・**layout(build image) 敏感**。**出荷 SM-ON build は素 host で堅牢（0/20）**。上記多数の仮説を除外。
- **言えない**：*なぜ* SM-ON image が wedge を回避するか（layout vs GATT-DB vs connect-config は未検証）。素 host wedge と «スマホ SMP 失敗» が同一機序かは未確定（別 regime）。間欠ゆえ per-build 率の CI は広い（定性 A/B は強い）。

## 8. まとめ・申し送り（この line は «畳む»）
- **結論**：**素 host wedge line は畳む**——(a) layout 敏感で計装が摂動＝直接測定困難、(b) 出荷 build がほぼ回避＝出荷問題でない。
- **本命＝スマホ SMP 失敗**は要件（[[ble-android-connect-fails]]）だが **別ラウンド（phone «両側観測»＝device カウンタ×phone snoop・evidence-rc-c3-reconnect-prereg の型）** として必要時に着手。
- dedup Tier2（esp_shim.c 共有）は «C3 調査一段落» の条件に接近（素 host は堅牢と判明・esp32_s3 に precedent）＝[[project-target-dedup-plan]]。
- bench 注記：host hci0 は discovery-hung から «持続 bluetoothctl session の power off→on→scan» で回復（one-shot power off/on では不可）。C5/C6 は本ラウンド中も広告中（電源断されず・C3 のみ MAC 指定 connect＝交絡なし）。
- 全 scratch 計装は revert 済み・`git status` clean・#1–#7 に回帰なし（本ラウンド群はソースを恒久変更していない）。

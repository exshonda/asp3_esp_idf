# C3 BLE「connect不可」の切り分け計画（D-2d PVCY修正後・実機再テスト中断を受けて）

対象：`docs/bt-shim.md`末尾の中断メモ＝C3にPVCY修正（`ae21e7a`：
`CONFIG_BT_NIMBLE_HS_PVCY=1`）版ble_host_smokeをflashし広告
（ASP3-C3-BLE `60:55:F9:57:C2:60`）は確認できたがcentralから
connectできずペアリング未到達で中断。

本計画は静的コードレビュー（レポート全文＝`tmp/review_c3_connect.md`。
ソース無変更）に基づく。**環境要因3件（DTR/RTSリセット・ttyACM
ノードドリフト・BlueZ側の古いbond/キャッシュ）は中断メモ既出のため
本計画は「まず環境要因を消してから」コード側候補を切り分ける手順に
特化する。**

## レビュー結論：connect経路を触る新規コードは実質1つだけ

`f9dae7d`（connectが動いていたD-2c時点）〜HEADでC3ビルドに影響する
実ソース変更は6ファイル。診断計装（ACL_TRACE/EVT_TRACE/APIERR_TRACE）
はCMake既定OFF・`tmp/c3ble.sh`もONにしておらず、中断メモのビルドには
**含まれていない**。残る実質差分は2つ：

### 候補1（最有力・確度中）：`CONFIG_BT_NIMBLE_HS_PVCY=1`の起動時HCIバースト

- 機構：`hal/.../nimble/host/src/ble_hs_startup.c`（PVCY有効時）が
  sync/adv開始より**前**に、C3では新規となるHCIコマンド列
  （`LE_Set_Address_Resolution_Enable`＋`LE_Clear_Resolving_List`＋
  `LE_Add_Device_To_Resolving_List`）をコントローラへ発行する。
  app側は`store_gen_key_cb`未設定のため常にこの分岐に入る。
- 戻り値は握り潰されるため、この列の途中でlibbtdm世代コントローラが
  未対応/失敗を返しても**sync/adv開始自体は成功する**——「広告は
  出るがconnectだけ失敗」という観測と整合しうる唯一の新規機構。
- 補足：app（`ble_host_smoke.c:376`）は`ble_hs_id_infer_auto(0,…)`＝
  privacy=0でown_addr_typeを推論しRPA非使用のため、この起動時
  バースト以外にPVCYがconnect経路へ触る箇所はコード上ない。

### 候補2（確度低）：`esp_shim_sem_give()`のE_CTX/E_QOVR挙動変更（`1b8e028`）

常時有効のコントローラOSアダプタ経路だが、connect固有の失敗を
説明する具体機構は見いだせず。

### 重要な反証（両候補の確度を下げる事実）

候補1・2とも、**bond成功済みのC5実機ビルド（別blob）にbyte単位で
同一ロジックが含まれる**ことをgit historyで確認済み。C5はこの状態で
connect＋暗号＋bond全て成功＝両変更が「一般に」connectを壊すわけでは
ない。C3だけが失敗するなら、差はC3固有部分（libbtdm世代の
コントローラ／HCI経路・`bt_shim.c`）とPVCYバーストの**相互作用**に
絞られる（libbtdmが上記resolving-list HCIコマンドを未対応の場合等）。

## 切り分け手順（この順で・各段で判定を固定）

### 段階0：環境要因を消す（コード判断の前提）
1. BlueZで対象を除去して素の状態から：
   `bluetoothctl remove 60:55:F9:57:C2:60` → アダプタ power cycle →
   `scan on`で再検出 → `connect`。
2. ノードドリフト対策：接続対象のttyACMは都度実在確認（メモの
   ttyACM5↔8ドリフト）。DTR/RTSリセット回避は`tmp/c3ble.sh`の
   usb-reset→watchdog-reset手順を踏襲。
3. 別centralでの再現確認：nRF Connect（スマホ）or 別のBLEドングルで
   connectを試す。**BlueZ固有なら他centralで成功する**。
→ ここでconnectできれば環境要因で決着（コード変更不要）。

### 段階1：候補1のA/B（環境要因を消してもダメな場合・最優先）
- `CONFIG_BT_NIMBLE_HS_PVCY`を一時的に`0`へ戻したビルドで、**同一
  central・同一環境**でconnectをA/B比較（各1〜2回で機械判定）。
  - PVCY=0でconnect成功／PVCY=1で失敗 → **候補1確定**。
    恒久対応は「PVCYは維持しつつ起動時HCIバーストのlibbtdm非対応
    コマンドを回避」＝(i) `store_gen_key_cb`を実装してresolving-list
    投入分岐を回避する、または(ii) 当該HCIコマンドの戻り値を実機
    ログで確認し、libbtdmが本当に落としているコマンドを特定して
    shim/config側で無効化する。**bondはPVCY=1が必須（C5実証）**
    なので、connectとbondを両立させる形を選ぶこと。
  - PVCY=0でも失敗 → 候補1棄却。候補2および段階2へ。
- **注意**：PVCY=0はD-2d bondをコンパイルアウトする（=bond不可に
  戻る）ため、これは切り分け専用の一時ビルド。恒久ビルドをPVCY=0に
  戻してはいけない。

### 段階2：候補1が棄却された場合
- connect時のgap_event（`ble_host_smoke.c`の`gap_event_cb`）に
  CONNECT/DISCONNECTのreason/statusを既存マーカ（STORE系）で記録し、
  central側のログ（BlueZの`btmon`）とA/B。CONNECT_IND自体が
  コントローラに届いているか（adv→connectのLL遷移）を、既存の
  診断計装（ACL_TRACE等、CMakeオプションでON）で確認。
- libbtdm世代のHCI RX経路（`bt_shim.c`）のconnect関連イベント配送に
  D-2d変更の副作用がないかを実機トレースで確認。

## 前提予測（実施前に固定）

最有力予測：**段階0（BlueZ remove＋別central）で解決する公算が最も
高い**（C5で同一コードがbond成功済み＝コードは一般には健全、かつ
connectはD-2cでC3実機で動いていた実績がある）。段階0で解決しない
場合に限り候補1のA/Bへ進む。この予測が外れて候補1が確定すれば、
それは「libbtdm世代とPVCY起動バーストの相互作用」という新規かつ
移植価値のある知見になる。

## 記録

各段階の結果は`docs/bt-shim.md`のC3 D-2d節へ追記（本計画は切り分け
設計のみ。実機結果はbt-shim.md側が正）。C5はbond成功済み＝修正実証
済みで、C3は本切り分け後のconnect回復＋bond確認のみが残タスク。

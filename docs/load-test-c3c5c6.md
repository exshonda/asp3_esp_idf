# C3/C5/C6 Wi-Fi 負荷試験（S3知見の移植）

## 目的

ESP32-S3 FMP3移植プロジェクトが持続高レートWiFi送信で発見・修正した
2つのOSAシム潜在欠陥（`docs/s3-throughput-findings-for-c6.md`）を，
C3（本ラウンド）・C5・C6（後続ラウンド）の各ターゲットで机上照合し，
実在する箇所へ移植したうえで，持続負荷試験（TCP/UDP echo）により
検証する．

S3の欠陥は2つ：
- **欠陥A**：OSAシムのキュー（`esp_shim_queue_send`/`_send_from_isr`/
  `_recv`）がメッセージ毎に`esp_shim_malloc`/`esp_shim_free`していた．
  特にWiFi MAC ISR文脈で呼ばれる`queue_send_from_isr`のmalloc失敗が
  tx-done完了通知の取りこぼしに直結し，WiFi動的TXバッファが永久
  未回収になって送信自己ロックを招く．持続送受信のalloc/freeチャーン
  によるシムヒープ断片化も問題．
- **欠陥B**：`WIFI_INIT_CONFIG_DEFAULT()`の既定（`tx_buf_type=1`動的・
  `static_tx_buf_num=0`）のまま使うと，全送信パケットが毎回シムヒープ
  からmalloc/freeされ，欠陥Aの断片化を助長する．

S3の対策（commit `dd7a76d`，S3リポジトリ
`/home/honda/TOPPERS/ESP32/esp32_s3`）：
1. キューの固定プール化（生成時にdepth×item_sizeのプールを1回だけ
   確保し，送受信はスロット番号をDTQで運ぶ．mallocを送受信経路から
   排除）
2. TXバッファの静的化（`tx_buf_type=0`, `static_tx_buf_num=16`）
3. （S3固有・C3/C5/C6には無関係）ネットワークタスクのコア0固定
   （S3はFMP3/デュアルコア．C3/C5/C6はASP3単一コアでこの前提は
   そもそも成立しており対処不要）

---

## C3（実施1，2026-07-13〜14）

### dd7a76d照合結果：欠陥A・Bとも C3 に実在

`asp3/target/esp32c3_espidf/wifi/esp_shim.c`のキュー実装（旧
486〜633行付近：`esp_shim_queue_create`/`_delete`/`_send`/
`_send_from_isr`/`_recv`/`_reset`）は，S3修正前のコードと行単位で
ほぼ同一パターンだった（欠陥A実在）：
- `esp_shim_queue_send`/`_send_from_isr`が送信のたびに
  `esp_shim_malloc(q->item_size)`（`_send_from_isr`はWiFi MAC ISR
  文脈から呼ばれうる）
- `esp_shim_queue_recv`が受信のたびに`esp_shim_free`

欠陥Bも実在：C3の全Wi-Fiアプリは`WIFI_INIT_CONFIG_DEFAULT()`を
そのまま使用（`tx_buf_type`/`static_tx_buf_num`を上書きしない＝
既定は動的）．なお C3 の sdkconfig 相当
（`asp3/target/esp32c3_espidf/hal_stub/include/nuttx/config.h`）は
`CONFIG_ESPRESSIF_WIFI_TX_BUFFER_TYPE=1`（動的）・
`CONFIG_ESPRESSIF_WIFI_STATIC_TX_BUFFER_NUM=0`でコンパイルされて
おり，静的TXバッファへの切替はランタイム上書きだけでなく
コンパイル時設定との整合も本来必要（後述）．

### 移植内容（欠陥A：esp_shim.c固定プール化）

S3 commit `dd7a76d`の`wifi/adapter/esp_shim.c`差分を
`asp3/target/esp32c3_espidf/wifi/esp_shim.c`へ移植（C3の
`SHIM_LOCK`/`SHIM_UNLOCK`はS3と同一実装のため差分は無改変で適用
できた）：
- `SHIM_QUE`構造体に`pool`/`free_stk`/`depth`/`free_top`を追加
- `esp_shim_queue_create`：生成時に`depth*item_size`のプールと
  空きスロットスタックを1回だけ確保（depthは
  `ESP_SHIM_DTQ_CNT=256`でクランプ）
- `shim_que_slot_alloc`/`shim_que_slot_free`ヘルパ（`SHIM_LOCK`下）
- `_send`/`_send_from_isr`/`_recv`：malloc/free廃止，スロット番号を
  DTQで運ぶ
- `_delete`：個別free廃止，プール一括free
- `_reset`（S3に無いC3独自のNimBLE eventq_reset用API）も同様に
  slot_free方式へ（C3固有の欠陥A相当箇所のため追加修正）

**意味論の注意**：旧実装はDTQ深さ（256）まで無条件に積めたが，
新実装は`min(len, 256)`スロットに正しく制限される（FreeRTOSの
`xQueueCreate(len)`の意味論に一致．S3と同じ）．A/B試験（下記）で
この差が挙動変化を生まないことを確認済み．

※esp_shim.cは同時刻にBLEエージェントがD-2b計装（`intno==2`の
storm probe）を追記中だったため，キュー実装部のみを対象にした
差分編集で衝突を回避（編集後にBLE側hunkの残存をgit diffで確認）．

### 移植内容（欠陥B：静的TXバッファ＝opt-in・既定OFF）

既存アプリは改変せず，本ラウンド新規の`apps/load_test_c3/`内で
`-DLOAD_TEST_STATIC_TXBUF`によりopt-in実装（`cfg.tx_buf_type=0;
cfg.static_tx_buf_num=16;`）．**既定はOFF**とした．理由：
(1) C3のコンパイル時設定が動的型（上記）で，ランタイム上書きとの
整合が未検証，(2) 実測でONにしても改善が観測されなかった（A/B
結果参照．なおONでもクラッシュ等の異常はなく，静的確保分だけ
`heap_free`が約27KB減るのはログで確認できる）．

### 負荷試験アプリ

新規`apps/load_test_c3/`（`apps/tcp_socket_echo`・`apps/
udp_socket_echo`のボイラープレートを複製・統合．既存アプリと実機
使用中の他エージェントに影響を与えないため新規ディレクトリ）：
- TCP echo（port 8）＋UDP echo（port 9）を1バイナリで同時提供
- `mon_task`：5秒周期で`esp_shim_heap_free_size()`・累積送受信
  バイト数・セッション数・エラーカウンタをsyslog出力
  （`t_syslog`の`TNUM_LOGPAR=6`制約により1行あたり%指定子5個まで
  →TCP行とUDP行に分割）
- `-DLOAD_TEST_STATIC_TXBUF`で欠陥B対策をopt-in

ビルド（いずれもリンク成功・RAM 97.9%・エラーなし）：
```
cmake -S asp3/asp3_core -B build/load_test_c3_qfix -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/asp3_core/cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DASP3_APPLDIR=$PWD/apps/load_test_c3 -DASP3_APPLNAME=load_test_c3 \
  -DESP32C3_WIFI=ON -DESP32C3_LWIP=ON -DESP32C3_QEMU=OFF \
  -DASP3_EXTRA_COMPILE_DEFS='WIFI_SSID="...";WIFI_PASSWORD="..."'
```
3ビルド変種：
| 変種 | esp_shim.c | TXバッファ |
|---|---|---|
| baseline | 旧（欠陥A残存） | 動的（既定） |
| qfix | 固定プール化 | 動的（既定） |
| fixed | 固定プール化 | 静的（`LOAD_TEST_STATIC_TXBUF=1`） |

### 負荷試験の実施と結果

**環境**：DUT＝board A（`60:55:F9:57:C9:88`，USB-Serial-JTAG直結）．
AP＝SSID `<SSID-2G>`（2.4GHz．認証情報は
`build/wifi_connect/CMakeCache.txt`から取得，docs非記載）．ホスト＝
同一LAN有線（192.168.1.48）．DUTのDHCP取得IP=192.168.1.63．
ホスト側ハーネス（python）：TCP持続ストリーム（1400B chunk連続
sendall＋独立recvスレッドでパターン検証・切断時自動再接続）＋
UDP echo（512B・50pps・応答タイムアウト0.5s・シーケンス番号で
内容検証）＋2秒周期ping疎通プローブ＋シリアル持続ログ採取．

**判定基準（事前固定）**：スループット経時劣化なし／ヒープ単調減少
なし／エコー完全一致／10分完走／baselineとのA/B．

#### ★主要発見：S3欠陥とは別の，C3既存の「負荷誘発リンク停止」

全変種共通で，**負荷開始から約15〜30秒でWiFiリンクが双方向とも
完全停止する**（デバイス発のゲートウェイping（raw API・lwIP内蔵）
も止まる＝TX方向死亡を含む．ホストからのping/ARP解決も不能）．
停止後も：
- `STA_DISCONNECTED`イベントは一切発生しない（blobは接続中と認識）
- `heap_free`は完全一定（断片化・リークなし）
- デバイス側エラーカウンタ0・パニックなし・カーネル/タスクは正常
  稼働継続（MONログは5秒周期で正確に出続ける）
- アイドル5分以上放置しても自然回復しない（回復した観測も過去に
  1回あり＝数分オーダー後．再現条件不明）
- リセット（再起動）で復旧する

アイドル時は完全に健全：**Exp1（qfix+静的TX，無負荷255秒）で
デバイス発ping 242回連続成功・ホスト発ping 48/48成功**．つまり
負荷（本ハーネスのTCPストリーム＋UDP 50ppsという中程度の負荷）が
トリガ．

#### A/B結果（120秒負荷・同一手順．DHCP完了を確認してから負荷開始）

| 変種 | 実験 | リンク停止まで | 停止前エコー実績 | 停止時heap_free | デバイス側エラー |
|---|---|---|---|---|---|
| fixed（欠陥A+B修正） | Exp2 | 負荷開始から約15s | TCP 9,352B・UDP 48dgram（完全一致） | 134,880（一定） | 0 |
| qfix（欠陥Aのみ修正） | Exp3 | 約20s | TCP 18,076B・UDP 314dgram=160KB（完全一致） | 162,280（一定） | 0 |
| baseline（未修正） | Exp4b | 約5〜10s | TCP 4,824B・UDP 16dgram（完全一致） | 164,280（一定） | 0 |

- 3変種とも**同一の停止シグネチャ**＝この停止は**S3欠陥A/Bの修正
  では治らず，修正によって悪化もしない**（回帰なし）．変種間の
  停止までの時間・エコー量の差はrun-to-runばらつきの範囲と判断
  （同一変種でも負荷開始タイミングやRF環境で変動．qfixの300+dgram
  は最良run）．
- **ヒープはどの変種・どのrunでも1バイトも減少しない**＝S3欠陥Aの
  症状（断片化・malloc失敗）はC3のこの負荷レベルでは発現しないか，
  発現前に別の停止が先行する．
- エコー内容は全run・全プロトコルで**バイト単位完全一致**
  （mismatch 0．TCP合計・UDP合計とも）．
- fixed（静的TXバッファON）はboot時に`heap_free`が約27KB減る
  （静的TX 16枠分の一括確保）＝機能自体は動作，ただし停止への
  効果なし．
- 10分完走試験（fixed，600秒）も実施したが，上記停止により実質
  負荷が乗ったのは最初の約15秒のみ．**その後の685秒間デバイスは
  ヒープ一定・エラー0で健全に稼働し続けた**（停止＝ハング/暴走では
  なくリンクのみの死）．

**留意（環境交絡）**：試験時間帯，同一LAN/同一2.4GHz帯で複数の
並行エージェントが別ボード（C3 board B＝BLEストーム実験等）を
稼働させており，RF環境は劣悪だった可能性がある（DHCP自体が
タイムアウトしたrunも1回あり＝Exp4初回，無効として再試行）．
ただし「アイドルなら4分以上完全安定・負荷開始後15〜30秒で
確定的に停止・リセットまで不回復」というパターンは環境ノイズ
だけでは説明しにくく，デバイス側（blob/シム/lwIPのTX経路の
どこか）の負荷誘発性の停止と推定する．根本原因の特定は本ラウンド
のスコープ外＝**次ラウンドへの申し送り**（下記）．

### 判定

- **欠陥Aの移植**：完了（S3差分を忠実に移植＋C3固有の
  `esp_shim_queue_reset`も追随）．A/Bで回帰なしを確認．
  ISR内mallocの構造的リスクは除去された（予防的修正として妥当）．
- **欠陥Bの移植**：opt-in（`LOAD_TEST_STATIC_TXBUF`）として実装・
  動作確認済みだが，**既定OFF**（コンパイル時設定との整合が未検証
  ＋実測で効果が示せないため）．恒久有効化するなら
  `nuttx/config.h`の`TX_BUFFER_TYPE`/`STATIC_TX_BUFFER_NUM`と
  セットで行うこと．
- **判定基準に対して**：ヒープ単調減少なし＝合格．エコー完全一致＝
  合格．スループット持続・10分完走＝**不合格（ただし原因はS3欠陥
  とは別の既存問題）**．S3欠陥A/B修正の「修正後の健全性」は確認．
  修正前後で症状差が出なかったため**因果までは言えない**（S3欠陥が
  C3で顕在化する前に，別の停止が必ず先に起きるため検証不能）．

### 次ラウンドへの申し送り（C3負荷誘発リンク停止の調査）

- 再現手順：`apps/load_test_c3`（qfixビルド）を実機に書込み→DHCP
  完了確認→ホストからTCP持続ストリーム＋UDP 50ppsを同時印加→
  15〜30秒でデバイス発ゲートウェイping停止＋ホストから到達不能．
- 観測済みの絞り込み：ヒープ無関係・STA_DISCONNECTED出ない・
  カーネル健全・アイドルでは起きない・リセットで復旧．
- 候補：(a) `esp_wifi_internal_tx`の恒久エラー化（lwIP netif側は
  戻り値を捨てている＝`netif_esp32c3.c`にエラーカウンタ計装を
  足すのが次の一手），(b) blob内TXバッファ/シーケンサの別種の
  枯渇（欠陥Aとは別経路），(c) RXバッファ枯渇（
  `esp_wifi_internal_free_rx_buffer`の呼び忘れ経路），
  (d) 並行BLE実験によるRF妨害（board Bのストームとの時間相関を
  取れば切り分け可能）．
- C6にも同じ試験を適用する際は，この停止がC3固有かC3/C6共通かが
  そのまま切り分け情報になる．

---

## C5（後続ラウンド用の枠）

未実施．dd7a76d照合・移植・負荷試験は後続ラウンドが追記する．
（C5はWi-Fi bringup自体が未完＝トーンADC問題ブロック中．負荷試験は
bringup完了後）

## C6（後続ラウンド用の枠）

未実施．`docs/s3-throughput-findings-for-c6.md`が元々C6宛の
申し送り．C6のdeaf-RX問題（FROZEN）が解決してスループット試験に
到達した時に，C3の移植パターン（本ドキュメントのdd7a76d照合〜
移植内容）をそのまま踏襲できる．C3で発見した負荷誘発リンク停止が
C6でも出るかは切り分け情報になる（上記申し送り参照）．

---

## board A 転用の記録と復元手順

- **転用前**：C3 board A（`60:55:F9:57:C9:88`）はNuttX-C3-BLE参照機
  （host `hci0`とのover-the-air BLE比較用．console=UART0）だった．
- **転用**：2026-07-13．本ラウンド（Wi-Fi負荷試験）のためユーザー
  了承済み．
- **最終flash状態（本ラウンド終了時点）**：`apps/load_test_c3`の
  **qfixビルド**（esp_shim.c欠陥A修正込み・`LOAD_TEST_STATIC_TXBUF`
  なし＝動的TXバッファ．`build/load_test_c3_qfix/asp_flash.bin`）．
  boot→DHCP（192.168.1.63）→TCP port8/UDP port9 echoサーバ起動・
  ホストからping疎通まで確認済み．
- **NuttX-C3-BLE参照機への復元手順**：
  ```
  ~/TOPPERS/ASP3CORE/tools/esptool-venv/bin/esptool --chip esp32c3 \
    --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:57:C9:88-if00 \
    --before usb-reset --after watchdog-reset --no-stub \
    write-flash 0x0 \
    /home/honda/.claude/jobs/494f98a3/tmp/nuttx-c3ble/nuttx/nuttx.bin
  ```
  （NuttXのconsoleはUART0＝USB-Serial-JTAG CDCではない点に注意．
  復元後はhost `hci0`（`bluetoothctl scan le`）でのadv検出により
  正常性を確認すること）

## 参照

- `docs/s3-throughput-findings-for-c6.md`（S3の欠陥調査・対策詳細）
- S3リポジトリ`/home/honda/TOPPERS/ESP32/esp32_s3` commit `dd7a76d`
- `docs/tcpip-integration.md`（C3のlwIP統合設計・既存テスト実績）
- ホスト側ハーネス：セッションスクラッチの`load_test.py`／
  `serial_logger.py`（リポジトリ外．本ドキュメントの記述から再構成
  可能．要点＝TCPは切断時自動再接続で持続化，UDPはシーケンス番号
  付きで内容検証，デバイス側syslogと突合）

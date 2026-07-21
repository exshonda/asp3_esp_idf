# evidence-c3-11 — lwIP を esp-idf submodule 供給へ切替（段階1・実機GREEN）

2026-07-20。計画＝`tmp/plan-espidf-only-hal-removal.md` 段階1。
ブランチ＝`claude/espidf-only-hal-removal`（`main` から再スタート。旧
`claude/blob-unify-v5.5.4` は main から445コミット遅れており破棄——詳細は
本ラウンドの経緯参照）。

## 0. 背景

lwIP はこれまで hal(esp-hal-3rdparty) とも esp-idf(v5.5.4 submodule) とも
別系統の**専用 `./lwip` submodule**（lwip-tcpip upstream、pin
`STABLE-2_2_1_RELEASE`）から供給されていた。esp-idf submodule は自身の
フォーク（`components/lwip/lwip`）を同梱しており、`src/Filelists.cmake`
の変数名（`lwipcore_SRCS`/`lwipcore4_SRCS`/`lwipapi_SRCS`）が両ツリーで
完全一致することを実測確認済み（同一 lwip 系譜のフォーク差のみ）。

esp32_s3 repo の先行事例（`evidence-s3-supply-migration-02-lwip-espidf`
相当）で「esp-idf 版 `ping.c` には `ping_stop()` が無い」という版差が
報告されていたため、同じ罠を先回りして潰した。

## 1. 実装（C3 のみ・可逆・チップ別）

- `asp3/target/esp32c3_espidf/target.cmake`：新規オプション
  `ASP3_LWIP_ESPIDF`（既定 **OFF** ＝ `./lwip` 従来経路。ON で
  `LWIP_DIR = ${IDF_V554}/components/lwip/lwip` へ切替＋
  `TOPPERS_LWIP_ESPIDF_SUPPLY=1` を compile def へ追加）。
- `asp3/target/esp32c3_espidf/net/netif_esp32c3.c`：
  `#if defined(TOPPERS_LWIP_ESPIDF_SUPPLY)` ガードで `ping_stop()` の
  no-op 実装を追加（esp-idf 版 `ping.c` に実体が無いため。`ping_init()`
  が再init時に内部状態を自動クリーンするため実害なし。★このファイルは
  C3/C5/C6 3チップ共有につき、C5/C6 は本ラウンドでは未着手＝ガード未定義
  なので旧経路のまま非回帰）。

## 2. 静的検証（実機投入前）

`apps/wifi_dhcp` を C3 向けに `ASP3_LWIP_ESPIDF=ON` と `=OFF` の両方で
configure+build（toolchain=`esp-14.2.0_20260121` riscv32-esp-elf、
`asp3/cmake/toolchain-esp32-riscv32.cmake` 経由）：

| 構成 | オブジェクト数 | IROM | DROM | RAM |
|---|---|---|---|---|
| `ASP3_LWIP_ESPIDF=OFF`（`./lwip`、従来） | 231 | 423872 B | 500992 B | 311380 B (95.03%) |
| `ASP3_LWIP_ESPIDF=ON`（esp-idf） | 232 | 423616 B | 501136 B | 311380 B (95.03%) |

差は `ip4_napt.c`（esp-idf 版 lwip 独自の NAT 機能。従来 `./lwip` には
無い追加ソース）のみ。RAM 使用量は完全一致。csrw mie 命令数＝0（実機安全性
自己検査 pass）。

## 3. 実機検証（★実機GREEN・GOT IP + ping）

DUT: C3 `<MAC-21>`（hub 1-6 port1、USB-JTAGのみ）。
`-DASP3_LWIP_ESPIDF=ON -DESP32C3_WIFI=ON -DESP32C3_LWIP=ON` で
`apps/wifi_dhcp` を実 SSID/PASSWORD 付きでビルド・書込み。

コンソール出力（抜粋）：
```
wifi_dhcp: SSID='0024_MYNET'
event: STA_START
wifi_dhcp: esp_wifi_connect -> 0
event: STA_CONNECTED
net: link up, starting DHCP
wifi_dhcp: CONNECTED, waiting for DHCP
wifi_dhcp: AP info: channel=1 rssi=-58
net: DHCP bound ip=...
wifi_dhcp: IP acquired: 192.168.1.77
net: ping gateway -> OK   (以降繰り返し・継続成功)
```

⇒ **STA_CONNECTED → DHCP IP取得(192.168.1.77) → gateway ping 継続成功**。
計画の GREEN 判定基準（実機の観測可能イベント。nm/静的解析で結論しない
という規律に従う）を満たす。

非関連の既知警告（本変更で新規発生したものではない）：
`esp_shim: #8 UNSUPPORTED: esp_wifi sync API from 2nd non-pool task` —
wifi_dhcp アプリのタスク構成に起因する既存の shim 警告で、lwip 供給元の
変更とは無関係（`ASP3_LWIP_ESPIDF=OFF` の従来経路でも同一警告が出ることが
想定される。未検証だが実害なし＝ping/DHCPは正常完走）。

## 4. 結論・次の一手

- ★C3 の段階1（lwip→esp-idf）は**実機GREEN確定**。ただし既定は当面 OFF
  のまま据え置く（他チップ・他アプリでの回帰確認前に既定反転しない）。
- 次：C5/C6 へ同じ変更を横展開（`net/` 共有ファイルなので追加変更は
  各チップの `target.cmake` に同型の `ASP3_LWIP_ESPIDF` オプションを足す
  だけで済むはず）。実機GREEN確認後、3チップ揃った時点で既定反転を検討。
- その後：段階2残り（`MBEDTLS_PORT_DIR` の hal 依存除去）→段階5（残る
  `ESP_HAL_DIR` 参照の最終清掃）→段階6（`./hal`・`./lwip` submodule 撤廃）。

## 5. ブランチ経緯の記録（重要・引き継ぎ用）

本ラウンド開始時、作業ブランチ `claude/blob-unify-v5.5.4` は `main` から
2026-07-05 に分岐したまま445コミット遅れていることが判明した（toolchain
標準化・C3 bond のtoolchain帰属判明・C5供給移行完了など、本タスクが
「未解決」として扱っていた内容をmainが既に実機実証済みで解決していた）。
ユーザー判断で**mainから再スタート**：新ブランチ`claude/espidf-only-hal-removal`
を作成し、計画ドキュメント(`tmp/plan-espidf-only-hal-removal.md`)のみ
持ち込んだ。旧ブランチはリモートに残置（削除はしていない）。

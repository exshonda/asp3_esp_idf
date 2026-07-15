# blob inventory — asp3_esp_idf が実際に使用しているバイナリblob一覧

ESP32-C3/C5/C6 の WiFi・BT について，「どの供給元（hal submodule／実 ESP-IDF
v6.1）の，どの世代の blob（`.a`）が，実際にリンクされているか」を cmake の
`-L`/`-l` 実体から辿って確定し，一覧化する。read-only 調査（コード非変更）。
値はすべてファイルパス・実測コマンドの結果を伴って記載し，実測していない
推測は【未確認】と明示する。

調査時点：2026-07-15，tree `a2361bf`（ブランチ
`claude/c6-wifi-c5-dev-5vc6x9`）。実行環境はユーザーのローカルマシン
（`~/tools/esp-idf-v6.1` が実在＝`docs/wifi-blob-generation-todo.md` が
クラウド/CIサンドボックスで確認した「非存在」とは異なる環境）。

## 0. 供給元の実体

| 供給元 | 実体 | pin/版 |
|---|---|---|
| hal submodule | `/home/honda/TOPPERS/asp3_esp_idf/hal`（`.gitmodules`の`hal`） | commit `b90b1837cb5ad24747deb4c895246037cc206ce5`（`sync/master.c-nuttx-20260428-24-gb90b1837cb5`，`git submodule status`実測） |
| ESP-IDF v6.1 | `/home/honda/tools/esp-idf-v6.1`（ローカルパス直参照，submodule化されていない） | `esp_idf_version.h`文字列 = 6.1.0（下記） |

両者とも `esp_idf_version.h` の文字列は **同一**（`ESP_IDF_VERSION_MAJOR/MINOR/PATCH`
= `6`/`1`/`0`）——実測：

```
$ grep -n VERSION_M /home/honda/TOPPERS/asp3_esp_idf/hal/components/esp_common/include/esp_idf_version.h
14:#define ESP_IDF_VERSION_MAJOR   6
16:#define ESP_IDF_VERSION_MINOR   1
18:#define ESP_IDF_VERSION_PATCH   0

$ grep -n VERSION_M ~/tools/esp-idf-v6.1/components/esp_common/include/esp_idf_version.h
14:#define ESP_IDF_VERSION_MAJOR   6
16:#define ESP_IDF_VERSION_MINOR   1
18:#define ESP_IDF_VERSION_PATCH   0
```

**このため「IDF文字列=6.1.0」だけでは供給元も blob 世代も判別できない**——
hal submodule（NuttX同期版）は文字列上 6.1.0 を名乗りながら，実際の
WiFi os_adapter ABI は次節の通り **v8**（旧世代）であり，実 ESP-IDF v6.1 の
**v9** とは別物。本タスクの前提「バージョン文字列とblob世代は一致しない
ことがある」はこの実測で裏付けられる。

## 1. Wi-Fi os_adapter ABI 版数（実測）

`ESP_WIFI_OS_ADAPTER_VERSION`（`wifi_os_adapter.h`）：

| 供給元 | ファイル | 値 |
|---|---|---|
| hal submodule | `hal/components/esp_wifi/include/esp_private/wifi_os_adapter.h:20` | `0x00000008`（= v8） |
| ESP-IDF v6.1 | `~/tools/esp-idf-v6.1/components/esp_wifi/include/esp_private/wifi_os_adapter.h:20` | `0x00000009`（= v9） |

以降 hal 側を「v8」，IDF v6.1 側を「v9」と表記する。

## 2. チップ×機能 blob世代対応表（サマリ）

| チップ | 機能 | 供給元（既定） | os_adapter | IDF文字列 | cmake実体 |
|---|---|---|---|---|---|
| C3 | WiFi | hal（v8）のみ | 0x08 | 6.1.0 | `asp3/target/esp32c3_espidf/esp_wifi.cmake`（IDF切替なし） |
| C3 | BT | hal 旧世代`libbtdm_app.a`（NimBLEホストは自前C3ツリー，blob非依存） | N/A（os_adapter非該当，btdm独自ABI） | 6.1.0 | `asp3/target/esp32c3_espidf/esp_bt.cmake` |
| C5 | WiFi | hal（v8）単一実装（v9=IDF版は実施52で削除済み） | 0x08 | 6.1.0 | `asp3/target/esp32c5_espidf/esp_wifi_v8.cmake`（`target.cmake`から`include`） |
| C5 | BT | IDF v6.1（`libble_app.a`，新世代`r_`プレフィクス）が**唯一の実装**（hal版`lib_esp32c5`は存在するがcmakeから未参照） | 0x09相当（BT側にos_adapter版の概念はないが同一matched set） | 6.1.0 | `asp3/target/esp32c5_espidf/esp_bt.cmake` |
| C6 | WiFi | hal（v8）既定。診断用`ASP3_WIFI_BLOB_IDF`変数でIDF v9へ差替え可能だが実運用は既定のみ【未確認＝実行結果の記録なし，`docs/wifi-blob-generation-todo.md`と同じ判定】 | 0x08（既定） | 6.1.0 | `asp3/target/esp32c6_espidf/esp_wifi.cmake` |
| C6 | BT | hal（v8）既定（`ESP32C6_BT_IDF61=OFF`）／IDF v6.1（`ESP32C6_BT_IDF61=ON`，実機で収束・稼働中の版) | 既定=hal, トグルON=IDF v6.1 | 6.1.0 | `asp3/target/esp32c6_espidf/esp_bt.cmake`＋`esp_bt_idf61.cmake` |

C6 BT の既定/実働の逆転（cmakeオプション既定はhalだが，実機で収束し
稼働しているのはIDF v6.1版）は`docs/wifi-blob-generation-todo.md`
「0. blob世代対応表」の留意点と同一の事実（本ドキュメントは供給元・
blob実体の一次資料としてそれを補完する）。

## 3. 各blobの実体（md5・パス・サイズ）

### 3.1 Wi-Fi blob（`esp_wifi.cmake`/`esp_wifi_v8.cmake`が実際にリンクする7つ：
`phy`/`coexist`/`mesh`/`espnow`/`core`/`net80211`/`pp`）

`wapi`は全チップともOFF固定（`libwapi.a`は積まない．各cmakeの§2コメント参照）。

**C3（`asp3/target/esp32c3_espidf/esp_wifi.cmake` L210-224，hal のみ）**

| lib | パス | サイズ | md5 |
|---|---|---|---|
| libphy.a | `hal/components/esp_phy/lib/esp32c3/libphy.a` | 208024 | `a401b5bbcbd619a15d77f8d936c9cfe0` |
| libcoexist.a | `hal/components/esp_coex/lib/esp32c3/libcoexist.a` | 87302 | `e854dd6a17e7eb9d056441af30fd7a21` |
| libmesh.a | `hal/components/esp_wifi/lib/esp32c3/libmesh.a` | 1005088 | `f1dda0cb1efade91ea6d862dce845008` |
| libespnow.a | `hal/components/esp_wifi/lib/esp32c3/libespnow.a` | 65840 | `9cc396a338e2187f55102fce23c414e3` |
| libcore.a | `hal/components/esp_wifi/lib/esp32c3/libcore.a` | 4108 | `952499526ce64f2a296733ed2838a5b3` |
| libnet80211.a | `hal/components/esp_wifi/lib/esp32c3/libnet80211.a` | 1201794 | `79e5066fbb9ba8aa16fec42083e6ad7d` |
| libpp.a | `hal/components/esp_wifi/lib/esp32c3/libpp.a` | 544654 | `96fc359838938220b141b118cc8598cc` |

**C5（`asp3/target/esp32c5_espidf/esp_wifi_v8.cmake`，hal のみ．v9=IDF版は
実施52で削除済み＝cmakeにIDFパスの節はもう存在しない）**

| lib | パス | サイズ | md5 |
|---|---|---|---|
| libphy.a | `hal/components/esp_phy/lib/esp32c5/libphy.a` | 290100 | `51166fb6f054a9e57211dfcfc1af62e9` |
| libcoexist.a | `hal/components/esp_coex/lib/esp32c5/libcoexist.a` | 96940 | `c516e24ec1cf77a4a6d8ca8130f07eb4` |
| libmesh.a | `hal/components/esp_wifi/lib/esp32c5/libmesh.a` | 1004932 | `e3a3f0cdf1d56d4c1fb33ca4d146801a` |
| libespnow.a | `hal/components/esp_wifi/lib/esp32c5/libespnow.a` | 65840 | `1e135726dd283f324665576c30732d70` |
| libcore.a | `hal/components/esp_wifi/lib/esp32c5/libcore.a` | 4090 | `5446621667b44e781b958d4e50cf2512` |
| libnet80211.a | `hal/components/esp_wifi/lib/esp32c5/libnet80211.a` | 1668150 | `74a8c6cf7e93f3de609f80d7d6d886ce` |
| libpp.a | `hal/components/esp_wifi/lib/esp32c5/libpp.a` | 960954 | `ab3ecbba08272da2543870a26f044bbf` |

**C6（`asp3/target/esp32c6_espidf/esp_wifi.cmake` L307-330，hal 既定．
`else()`枝＝実運用パス）**

| lib | パス | サイズ | md5 |
|---|---|---|---|
| libphy.a | `hal/components/esp_phy/lib/esp32c6/libphy.a` | 184020 | `cb429107787d88023983668c9b161b56` |
| libcoexist.a | `hal/components/esp_coex/lib/esp32c6/libcoexist.a` | 98646 | `553448620fbc7f65fa559eef312d2d0e` |
| libmesh.a | `hal/components/esp_wifi/lib/esp32c6/libmesh.a` | 1005088 | `87581735de4a7ee9ff193c398a63c09a` |
| libespnow.a | `hal/components/esp_wifi/lib/esp32c6/libespnow.a` | 65840 | `413c5bcc227424759cbae50407364f81` |
| libcore.a | `hal/components/esp_wifi/lib/esp32c6/libcore.a` | 4108 | `952499526ce64f2a296733ed2838a5b3` |
| libnet80211.a | `hal/components/esp_wifi/lib/esp32c6/libnet80211.a` | 1429794 | `b465395be10138180ffb4bad4e96927f` |
| libpp.a | `hal/components/esp_wifi/lib/esp32c6/libpp.a` | 886312 | `22b025da9d9f367268c705eaac716676` |

**C6 IDF v6.1 版（`if(DEFINED ASP3_WIFI_BLOB_IDF)`枝．WiFi専用の
`net80211`/`pp`/`core`/`mesh`/`espnow`は実運用では未使用・
`docs/wifi-shim-c6.md`実施19の因果検証用診断フックのみで実行結果の記録なし
【未確認】。ただし `libphy.a`/`libcoexist.a` は同じディレクトリの実体が
`ESP32C6_BT_IDF61=ON`＝実機で収束・稼働中のBTビルドから常時リンクされて
おり「未使用」ではない——§3.2参照）**

| lib | パス | サイズ | md5 |
|---|---|---|---|
| libphy.a | `~/tools/esp-idf-v6.1/components/esp_phy/lib/esp32c6/libphy.a` | 185912 | `3fea07086717f1c7c18f58e2d3815721` |
| libcoexist.a | `~/tools/esp-idf-v6.1/components/esp_coex/lib/esp32c6/libcoexist.a` | 99604 | `68ebb703fd26b29c1a53094a3157c3cb` |
| libmesh.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c6/libmesh.a` | 1004820 | `c2cff7e6e5bcb0e85d9503972b7983c9` |
| libespnow.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c6/libespnow.a` | 66916 | `3f34be97beb70a106e0ca28d8bdc8ad5` |
| libcore.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c6/libcore.a` | 4108 | `994e256f1ab63e838a4cccd543f15a86` |
| libnet80211.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c6/libnet80211.a` | 1476736 | `e879b0249d994f976d84129cae8a36c7` |
| libpp.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c6/libpp.a` | 885876 | `c2e8bbc4e3bfdcc80225f65662de2b12` |

**C5 IDF v6.1 版 WiFi（v9．WiFi専用の`net80211`/`pp`/`core`/`mesh`/`espnow`
は実施52で削除済み＝現行cmakeからは非参照．歴史的比較のためファイル実体
のみ記録。ただし同ディレクトリの `libphy.a`/`libcoexist.a` は
`asp3/target/esp32c5_espidf/esp_bt.cmake`（C5 BTの唯一の実装）が常時
リンクしており「非参照」ではない——§3.2参照）**

| lib | パス | サイズ | md5 |
|---|---|---|---|
| libphy.a | `~/tools/esp-idf-v6.1/components/esp_phy/lib/esp32c5/libphy.a` | 292110 | `4ccdbdbe1faf04a84b4059c882febe0f` |
| libcoexist.a | `~/tools/esp-idf-v6.1/components/esp_coex/lib/esp32c5/libcoexist.a` | 97842 | `53b3f95021fe43caaff9dd0bf72203ca` |
| libmesh.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c5/libmesh.a` | 1004664 | `cbb1bb4572f0f0b44073309483e8fa47` |
| libespnow.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c5/libespnow.a` | 66916 | `092a366b75a3812a037940a37f3d1b01` |
| libcore.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c5/libcore.a` | 4090 | `991b82eb4336011e8ce06ac525274b3f` |
| libnet80211.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c5/libnet80211.a` | 1802138 | `e4813280da77aa57134797289675a2ed` |
| libpp.a | `~/tools/esp-idf-v6.1/components/esp_wifi/lib/esp32c5/libpp.a` | 967024 | `5c011b104572beb46986613b3eacdab1` |

### 3.2 BT controller blob

| チップ | 世代 | lib | パス（実際にリンクされる方に★） | サイズ | md5 |
|---|---|---|---|---|---|
| C3 | 旧世代（btdm，`btdm_*`シンボル） | ★libbtdm_app.a | `hal/components/bt/controller/lib_esp32c3_family/esp32c3/libbtdm_app.a` | 1363054 | `dfdadb9ddc12eeeab85edfb5d26eb4bf` |
| C3 | （参考・非採用）IDF v6.1同名 | libbtdm_app.a | `~/tools/esp-idf-v6.1/components/bt/controller/lib_esp32c3_family/esp32c3/libbtdm_app.a` | 1388286 | `d9753a31a8eeac9da8f3718cdfdb4938` |
| C3 | 併用 | libbtbb.a（hal，採用） | `hal/components/esp_phy/lib/esp32c3/libbtbb.a` | 11746 | `463253285ae81adeb942455c7f08f86f` |
| C5 | 新世代（`r_`プレフィクス） | ★libble_app.a（IDF v6.1，**唯一の実装**） | `~/tools/esp-idf-v6.1/components/bt/controller/lib_esp32c5/esp32c5-bt-lib/libble_app.a` | 1867004 | `c2785c98f3231f74c825da6162be60bc` |
| C5 | （参考・非採用）hal同名 | libble_app.a | `hal/components/bt/controller/lib_esp32c5/esp32c5-bt-lib/libble_app.a` | 1860394 | `015db3db5a44be084b44b3579c900a5b` |
| C5 | 併用 | libbtbb.a（IDF v6.1，採用） | `~/tools/esp-idf-v6.1/components/esp_phy/lib/esp32c5/libbtbb.a` | 75630 | `f553ddd33805f6380fe103f37fe185c1` |
| C6 | 新世代（`r_`プレフィクス，既定） | ★libble_app.a（hal，`ESP32C6_BT_IDF61=OFF`） | `hal/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6/libble_app.a` | 1810788 | `75db98e5139162fa60583becb38ea0a1` |
| C6 | 新世代（`ESP32C6_BT_IDF61=ON`，実機で収束・稼働中） | ★libble_app.a（IDF v6.1） | `~/tools/esp-idf-v6.1/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6/libble_app.a` | 1818848 | `c28653df7553ac7b9932a84b235b166b` |
| C6 | 併用（hal既定時） | libbtbb.a | `hal/components/esp_phy/lib/esp32c6/libbtbb.a` | 19592 | `cbe3022fd34cffb613ce81a15b207a95` |
| C6 | 併用（IDF61時） | libbtbb.a | `~/tools/esp-idf-v6.1/components/esp_phy/lib/esp32c6/libbtbb.a` | 19818 | `d31c8865a4c1230bd65711847638f244` |

**BTビルドごとのリンクセット全体（`phy`/`coexist`/`btbb`の供給元を明示）**——
PHY/coex blobはWiFiとBTで共有される（`docs/wifi-blob-generation-todo.md`
§2参照）ため，「WiFi実運用で未使用」という判定（§3.1）とは独立に，
各BTビルドは自分の`-L`が指す供給元から`libphy.a`/`libcoexist.a`/`libbtbb.a`
を**常時リンクしている**。取り違えないよう全4通りのBT構成の完全な
リンクセットを供給元込みで示す：

| BTビルド | controller | phy | coexist | btbb |
|---|---|---|---|---|
| C3（既定・唯一） | hal `libbtdm_app.a` | hal `libphy.a`(c3) | hal `libcoexist.a`(c3) | hal `libbtbb.a`(c3) |
| C5（既定・唯一） | IDF v6.1 `libble_app.a` | **IDF v6.1** `libphy.a`(c5) | **IDF v6.1** `libcoexist.a`(c5) | **IDF v6.1** `libbtbb.a`(c5) |
| C6（既定＝`ESP32C6_BT_IDF61=OFF`） | hal `libble_app.a` | hal `libphy.a`(c6) | hal `libcoexist.a`(c6) | hal `libbtbb.a`(c6) |
| C6（`ESP32C6_BT_IDF61=ON`，実機で収束・稼働中） | **IDF v6.1** `libble_app.a` | **IDF v6.1** `libphy.a`(c6) | **IDF v6.1** `libcoexist.a`(c6) | **IDF v6.1** `libbtbb.a`(c6) |

各blobのmd5・パス・サイズは §3.1（WiFi節に掲載した`libphy.a`/`libcoexist.a`
の値がそのままBT側の実体でもある．同一ファイルへの2箇所からの参照）と
上の表を参照。

C3のみ「旧世代」＝`libbtdm_app.a`。C5/C6は新世代コントローラ
（`SOC_ESP_NIMBLE_CONTROLLER=1`，`libble_app.a`，`r_`プレフィクス関数）で，
`asp3/target/esp32c6_espidf/esp_bt.cmake` L7-15 のコメントが明記する通り
C3とはプログラミングモデル自体が異なる（C3=NuttXのFreeRTOS API直呼び，
C5/C6=`platform/os.h`のesp_os_*経由）。

C5のBTは唯一IDF v6.1版のみが cmake でリンクされる（`asp3/target/esp32c5_espidf/esp_bt.cmake`
L296-306の`-L`が常に`${IDF}`配下を指す．hal版`lib_esp32c5/esp32c5-bt-lib/libble_app.a`は
ファイルとして存在するが，どのcmakeパスからも参照されない＝死蔵ファイル）。

## 4. 「同名blobが hal と IDF で同一か別物か」md5判定表

| ファイル名 | チップ | hal md5 | IDF v6.1 md5 | 判定 |
|---|---|---|---|---|
| libphy.a | esp32c5 | `51166fb6f054a9e57211dfcfc1af62e9` | `4ccdbdbe1faf04a84b4059c882febe0f` | **別物**（サイズも290100 vs 292110で不一致） |
| libphy.a | esp32c6 | `cb429107787d88023983668c9b161b56` | `3fea07086717f1c7c18f58e2d3815721` | **別物**（184020 vs 185912） |
| libphy.a | esp32c3 | `a401b5bbcbd619a15d77f8d936c9cfe0` | `a51adbdc7ad18a2411061c37b915383f` | **別物**（208024 vs 209150．C3はIDF版を採用しないため参考のみ） |
| libcoexist.a | esp32c5/c6/c3 | 各上表参照 | 各上表参照 | 全チップで**別物**（サイズが数百バイト単位で全て不一致） |
| libbtbb.a | esp32c3/c5/c6 | 各上表参照 | 各上表参照 | 全チップで**別物** |
| libble_app.a | esp32c5 | `015db3db5a44be084b44b3579c900a5b` | `c2785c98f3231f74c825da6162be60bc` | **別物**（1860394 vs 1867004） |
| libble_app.a | esp32c6 | `75db98e5139162fa60583becb38ea0a1` | `c28653df7553ac7b9932a84b235b166b` | **別物**（1810788 vs 1818848） |
| libbtdm_app.a | esp32c3 | `dfdadb9ddc12eeeab85edfb5d26eb4bf` | `d9753a31a8eeac9da8f3718cdfdb4938` | **別物**（1363054 vs 1388286） |
| libcore/libmesh/libespnow/libnet80211/libpp.a | esp32c5/c6 | 各上表参照 | 各上表参照 | 全チップで**別物**（サイズも全項目で不一致） |

**結論：本リポジトリが扱う hal（v8）と ESP-IDF v6.1（v9）の blob は，
調査した全ファイル・全チップで md5・サイズとも一致するものが一つもなく，
「同名だが実体は完全に別バイナリ」である**。これは`docs/wifi-blob-generation-todo.md`
§2 が `register_chipv7_phy` のリンクアドレス差（`0x42032590`→`0x42032abe`）
から間接的に導いた結論（「同名APIを持つ別blob」）を，本ドキュメントは
md5直接比較で一次資料として裏付ける形になる。

## 5. シンボルによる世代判別（`riscv-none-elf-nm`実測）

`~/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-nm`使用。

- **`register_chipv7_phy`**：hal/IDF・C3/C5/C6 いずれの`libphy.a`にも
  `T register_chipv7_phy`として定義済み（全6ファイルで確認）。WiFi/BT
  共有のPHY較正エントリポイントである点は世代・チップに依らず共通。
- **BT旧世代 vs 新世代の判別シンボル**：
  - C3 `libbtdm_app.a`：`T btdm_controller_init`・`T btdm_osi_funcs_register`
    （`btdm_`プレフィクス，旧世代）。同ファイルには`T`定義シンボルが1403個。
  - C6 `libble_app.a`（hal）：`T r_ble_controller_init`・`T r_esp_ble_msys_init`
    （`r_`プレフィクス，新世代）。
- **Wi-Fi osiアダプタのエントリ**：
  - `libnet80211.a`（hal, C3）に`T wifi_osi_funcs_register`・
    `D g_wifi_osi_funcs_md5`（ASP3側 `esp_wifi_adapter.c` が積む
    `g_wifi_osi_funcs`テーブルの整合性チェック用md5定数）を確認。
    `esp_wifi_init_internal`も同ファイルに`T`定義あり。
  - IDF v6.1 `libnet80211.a`（C6）は`U g_osi_funcs_p`（多数箇所で未解決参照）
    ＋`T esp_wifi_internal_osi_funcs_md5_check`＋`D g_wifi_osi_funcs_md5`
    ＋`T wifi_osi_funcs_register`——hal版とシンボル名の骨格は同じだが
    （osi登録の仕組み自体はv8/v9で共通設計），ABI版数（§1）が異なるため
    構造体レイアウトは非互換（`wifi_osi_funcs_t`のフィールド数がv8/v9で違う．
    `docs/wifi-shim-c6.md`実施10参照）。

## 6. 既存docsへの相互参照

- `docs/c5-toolchain.md`——C5の正典コンパイラ（xpack riscv-none-elf-gcc
  15.2.0）実測。本ドキュメントのmd5値はこの正典ツールチェーンでのビルド
  前提のファイル実体（コンパイラはblobそのものには影響しないが，
  ビルド系統の前提として参照）。
- `docs/wifi-blob-generation-todo.md`——「WiFiをBLEに揃えてv6.1化すべきか」
  の机上レビュー。§0の blob世代対応表・§2のlibphy md5実測（本ドキュメント
  の値と一致）・§7一次資料リストは本ドキュメントの前身データであり，
  本ドキュメントはそれを全チップ・全blobへ拡張し「同一か別物か」の判定を
  net80211/pp/core/mesh/espnow/coexist/btbb/btdm_appまで広げたもの。
  「単独WiFiは現状維持」という結論・ToDo（v6.1 pinned submodule化等）は
  変更しない。
- `docs/ble-c5c6.md`／`docs/ble-c5c6-plan.md`——C5/C6 BLEの実装ログ・設計書。
  §11-15（C6のhal/IDF v6.1トグル判断の経緯，PHY非収束の局在化）は本
  ドキュメントの「C6 BT既定/実働の逆転」の一次資料。
  §7（C5のライブラリ世代選定＝eco2 PHY非互換でIDF v6.1へ）はC5 WiFi
  実施09/10の一次資料。
- `docs/bt-shim.md`——C3 BLE（旧世代btdm+NimBLEホスト）の実装ログ。
  D-2c/D-2d到達の経緯，`libbtdm_app.a`のsymbol局所化計装（`--wrap`系診断）
  の一次資料。

## 7. 未確認事項一覧

- C6 `ASP3_WIFI_BLOB_IDF`診断枝（`esp_wifi.cmake` L307-319）を実際に
  `-DASP3_WIFI_BLOB_IDF=...`で有効化してビルド・実機検証した記録は
  `docs/wifi-shim-c6.md`含め見当たらない。IDF v9 WiFiがC6実機で収束するか
  は**【未確認】**（`docs/wifi-blob-generation-todo.md`と同じ判定を踏襲）。
- `wifi_osi_funcs_t`のhal(v8)/IDF(v9)間のフィールド差分の詳細（構造体
  サイズ実測値など）は本ドキュメントでは深追いしていない
  （`docs/wifi-shim-c6.md`実施10に既存の実測記録あり，そちらを参照）。
- C5 hal版`libble_app.a`（`lib_esp32c5/esp32c5-bt-lib/`）がなぜ今も
  submodule内に残っているか（将来「BLEをhalへ戻す」という
  `docs/c5-hal-v8-unification-memo.md`末尾の"第2段"のための保持か，
  単にhal submoduleの全チップ収録の副産物か）は未調査【未確認】。

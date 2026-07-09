# C6 deaf-RX コードレビュー（レンズ：shim関数の意味論・契約違反）

実施日：2026-07-09（実機・JTAG不使用，コードレビューのみ）

## 0. 前提

`docs/wifi-shim-c6.md`（冒頭150行・末尾500行・実施6/9/13/19/21/22/26/38/50/
58/59/70-72/76-85を`grep -n`で部分読み）と`README.md`のC6行を読了。
確定事実（MMIOレジスタほぼ全域一致・MAC割込みルーティング/WIFIPWR修正済・
INTMTX_STATUS0 bit0終始0・lmacRxDone=0・NuttX陽性対照確定・regi2c全一致・
84ラウンドで尽くされた「値レベル比較」）を前提として受け入れ，これらと
矛盾しない範囲でコードの意味論レベルの候補のみを探した。

レビュー対象：
- `asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c`（1083行，全読）
- `asp3/target/esp32c6_espidf/wifi/esp_shim.c`（1023行，全読）
- `asp3/target/esp32c6_espidf/wifi/esp_shim_blobglue.c`（446行，全読）
- 上記3ファイルのC3版との`diff`（機械差分，全hunkを確認）
- 契約：`hal/components/esp_wifi/include/esp_private/wifi_os_adapter.h`，
  参照実装`hal/components/esp_wifi/esp32c6/esp_adapter.c`（全読），
  `hal/components/riscv/interrupt.c`・`interrupt_intc.c`・
  `hal/components/hal/include/hal/interrupt_intc_ll.h`・
  `hal/components/soc/esp32c6/register/soc/plic_reg.h`（PLIC_MXレジスタ
  オフセット突合せ）

## 1. 先に結論

**意味論レンズでの全行精読の結果，「これがdeaf-RXの一次原因」と言える
新規候補は見つからなかった。** 84ラウンドの値レベル比較が到達不能とした
領域（blob内部命令列・DMA/APM/PMP/PMA・regi2c実測較正値）は本レンズの
射程外であり，コード上「意味論として明らかにおかしい」箇所
（インターフェース契約違反・単位不一致・ISR文脈違反）は，C3版との
diffおよびESP-IDF参照実装との突合せで潰しても，C6固有かつ未検証のもの
としては残らなかった。理由は主に2つ：

1. C3版とC6版の差分は，本ドキュメント記載の通りごく少数（`esp_wifi_adapter.c`
   309行・`esp_shim.c`378行・`esp_shim_blobglue.c`114行の変更行数のうち，
   大半は診断計装(GT_*・RTC書込み)の追加と，既に「根本原因修正」として
   コメント付きで実装済みの内容——割込み型設定・WIFIPWRクロック・regi2c
   マスタクロック・RNG/eFuseアドレス）。これらはレジスタ比較で既に
   「効果なし」または「必要だが不十分」と実機検証済み。
2. `wifi_osi_funcs_t`テーブルは指示付き初期化子（`._field = fn`）で埋めて
   おり，フィールド抜け・順序ずれによる「関数の取り違え」というクラスの
   バグは構造的に起きない（実施済みの構造完全一致確認とも整合）。

以下，順位付きで候補を記載する。**上位ほど「安価に確認でき，かつまだ
84ラウンドで検証されていない」もの**を優先した。

---

## 候補1（確度：中）：`putchar()`スタブが，未捕捉の一次診断出力を握りつぶしている

**該当箇所**：`asp3/target/esp32c6_espidf/wifi/esp_shim_blobglue.c:411-415`

```c
int
putchar(int c)
{
	return(c);
}
```

**疑いの根拠**：
- コメントは「blobの一部デバッグ経路が直接呼ぶ．他のログ出力は
  esp_log/esp_shim_log_write経由でsyslogへ折返し済み」としているが，
  実際にどの経路が`putchar`を直接呼ぶのか，84ラウンドを通じて
  一度も特定・検証されていない（`docs/wifi-shim-c6.md`中の`putchar`への
  言及はこの1箇所のみ，`grep -n putchar`で確認）。
- `hal/components/esp_wifi/src/lib_printf.c`・`esp_phy/src/lib_printf.c`
  （`pp_printf`/`phy_printf`/`rtc_printf`等）は`ESP_LOG*`経由でosiの
  `_log_write`（`log_write_wrapper`→syslog）に落ちることを確認した
  ため，**通常の**blobログ出力はASP3側で既に可視（実機ログの
  `I (35) phy_init:`はこの経路）。したがって`putchar`が直接呼ばれる
  経路は，これとは別の，ESP_LOGを経由しない**低レベルなデバッグ/
  アサート出力**である可能性が高い（Espressif ROM系コードに時々ある
  `ets_printf`的な生出力，あるいはPHY/regi2c較正失敗時の簡易ダンプ等）。
- deaf-RXは「クラッシュしない・エラーも返らない・ただ受信できない」
  という性質上，blob内部で**何らかの分岐が失敗側に倒れている**はずだが，
  84ラウンドの調査はレジスタ・カウンタの外形観測に終始しており，
  **blob自身が生成する可能性のある一次診断文字列を一度も読んでいない**。

**観測事実との整合性**：本候補は「なぜ受信もTXも無いか」の原因そのもの
ではなく，**原因を教えてくれるかもしれない未読の窓**という位置づけ。
仮にPHY較正やregi2c経路の内部で失敗時にだけ`putchar`ベースの生ログを
吐く設計になっていれば，この1文字ずつの出力を可視化するだけで
「どの内部関数がどう失敗したか」を教えてくれる可能性があり，既存の
確定事実（lmacRxDone=0・TX無放射・regi2c値は一致するが機能しない）を
損なわずに追加情報を得られる。的中しなくても実害はない。

**安価な検証方法**：`putchar`を1行変更し，`c`を`syslog(LOG_NOTICE, "%c", c)`
（または1バイトずつリングバッファに貯めてまとめてダンプ）に置き換えて
再ビルド・再実機実行するだけ。ログに新規の文字が1つでも出れば，
その出力元をELF逆アセンブルで追跡する次段調査に繋がる。出なければ
「この経路は呼ばれていない」ことが確定し，安全に候補から除外できる
（コスト：1行変更＋1回の実機ログ採取）。

**確度**：中（原因の特定ではなく，未読の情報源を開けるだけの位置づけ
のため，的中确度自体は不明だが，コストがほぼゼロで84ラウンドが
一度も試していない一次情報源である点を評価）。

---

## 候補2（確度：低〜中，TXのみ説明可）：`phy_get_max_pwr()`固定値スタブ

**該当箇所**：`asp3/target/esp32c6_espidf/wifi/esp_shim_blobglue.c:442-446`

```c
int8_t
phy_get_max_pwr(void)
{
	return(20);	/* 20dBm相当のプレースホルダ．要再検討 */
}
```

**疑いの根拠**：C3や他チップは`eco*.ld`経由でROM実体（eFuse較正値ベース
の実際の最大送信電力）を提供するが，本C6スナップショットにはこれが
存在せず固定値スタブになっている（コメント自身が「要再検討」と明記）。
libphy.aの内部ゲインテーブル/TXパワーステップ計算がこの値を分母や
インデックス計算に使っている場合，実際のeFuse値と乖離した固定値が
不正なゲインインデックス（範囲外→クランプで実質0出力）を生む余地は
理論上ある。

**観測事実との整合性**：**RXの説明にはならない**（`phy_get_max_pwr`は
TXパワー制御専用でRXパスに関与しない）。実施81のTX無放射所見の
「部分的な」説明候補にはなり得るが，deaf-RX全体（RXも0）の統一的
説明にはならないため，Codex第4回の「MAC-to-air境界が双方向とも
不成立」という統一原因説明力の点で他候補に劣る。

**安価な検証方法**：戻り値を`20`から極端に異なる値（例：`0`または`84`）
に変えて再ビルドし，TXスニファ実験（実施81と同一手法）を再実行して
挙動が変化するかを見る。変化がなければ「スキャン動作には影響しない
想定」というコメントの想定通りであることが実測で裏付けられ，安全に
除外できる。

**確度**：低〜中（RXを説明できないため主要候補にはなり得ないが，
コスト最小の反証実験が可能）。

---

## 候補3（確度：低）：`wifi_clock_enable_wrapper()`の無条件強制レジスタ書込みが呼び出し毎に繰り返される

**該当箇所**：`asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c:631-688`

```c
static void
wifi_clock_enable_wrapper(void)
{
	static bool_t	lpclk_selected = false;
	...
	if (!lpclk_selected) {
		modem_clock_deselect_all_module_lp_clock_source();
		modem_clock_select_lp_clock_source(...);
		_regi2c_ctrl_ll_master_enable_clock(true);
		regi2c_ctrl_ll_master_configure_clock();
		lpclk_selected = true;
	}

	wifi_module_enable();

	/* ★根本原因修正（追記10）… */
	*(volatile uint32_t *)0x600af018U = 0x7U;
}
```

**疑いの根拠**：LP clockソース選択とregi2cマスタクロック初期化は
`lpclk_selected`で1回限りにガードされているが，末尾の
`MODEM_LPCON_CLK_CONF = 0x7`直書きは**呼ばれる度に無条件で実行**される。
`_wifi_clock_enable`/`_wifi_clock_disable`はESP-IDFの実際のwifi_init.c
内で複数回（start/stop，あるいはpsモード遷移）呼ばれ得るAPIであり，
`wifi_module_enable()`（`modem_clock_module_enable()`本体）が内部で
保持するリファレンスカウント/ICG状態と，この直後の生レジスタ書込みが
「常に同じ値を強制する」設計は，`wifi_clock_disable_wrapper()`
（`wifi_module_disable()`のみ・生レジスタ書込みなし）との非対称性を
生んでいる——enableは毎回0x7を強制するがdisableは対応する強制解除を
一切行わない。

**観測事実との整合性**：単独では「なぜ初回起動時からRXが0か」を
説明しない（初回は問題なく0x7が書かれる）。むしろ「2回目以降の
enable/disableサイクル」がある場合の状態不整合候補であり，
本調査が「起動直後のscanで最初から0件」という単発の症状を扱っている
限り説明力は低い。

**安価な検証方法**：`wifi_clock_enable_wrapper`/`wifi_clock_disable_wrapper`
の呼び出し回数をカウンタ化し（既存のRTC診断カウンタ方式を流用），
1回のscanで実際に何度呼ばれるかをログ出力するだけで，このパスが
そもそも複数回踏まれているかどうかを確認できる。1回しか呼ばれない
ことが確定すれば，この候補は安全に除外できる。

**確度**：低（起動直後の一発目のscanという条件下では説明力が乏しい）。

---

## 参考（既に反証済み・確認のみ）：coex実リンク vs リファレンスのno-op化

**該当箇所**：`asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c:1059-1081`
（`._coex_init = coex_init`等，C3版と同一のまま`libcoexist.a`実体へ
無条件で素通し）

タスク冒頭の観点3（「coex系スタブの戻り値，RF grant相当，Codex第4回
指摘」）に対応する候補として最有力視して調べたが，**`docs/wifi-shim-c6.md`
実施9で既に実機検証済み**：`_coex_*`全フィールドをNuttXのno-op構成
（`#if CONFIG_SW_COEXIST_ENABLE`偽の場合の即時成功値）に合わせて
実際に無効化してビルド・実機実行しても「AP 0個」は不変だった
（実施9本文：「coex実装の違い（実passthrough vs no-op）は，症状に
一切影響しない」）。さらに実施21-22で`coex_schm_env_ptr`が両
プラットフォームとも常時NULL（coex機構自体が両ブート経路で
「未初期化」）であることも確認済み。**この候補は仮説として筋は
良いが，既に反証されているため順位を下げる**（参考として記載，
新規候補としては数えない）。

---

## 確認したが「意味論的に正しい」と判断し，候補から除外した箇所

以下はタスクの観点1・5・6に沿って重点的に見たが，参照実装
（`hal/components/esp_wifi/esp32c6/esp_adapter.c`・
`hal/components/riscv/interrupt.c`・`hal/components/hal/include/hal/
interrupt_intc_ll.h`・`hal/components/soc/esp32c6/register/soc/
plic_reg.h`）との突合せで整合を確認できた：

- **`set_intr_wrapper`のPLIC_MXレジスタオフセット**
  （`esp_wifi_adapter.c:76-80`）：`PLIC_MXINT_ENABLE_REG`=+0x0,
  `PLIC_MXINT_TYPE_REG`=+0x4,`PLIC_MXINT0_PRI_REG`=+0x10（`plic_reg.h`
  で確認）と完全一致。
- **割込み型ビットの意味（0=LEVEL,1=EDGE）**：`interrupt_intc_ll_get_type`
  （`interrupt_intc_ll.h:51-55`）およびそれを`INTR_TYPE_EDGE`/
  `INTR_TYPE_LEVEL`へ変換する`interrupt.c`側ロジックと，ASP3コメントの
  「ビットクリア＝LEVEL，セット＝EDGE」が一致。
- **IRAM_ATTR配置**：C3版・C6版とも同一関数（`wifi_int_disable/restore_
  wrapper`・`task_yield_from_isr_wrapper`・`queue_send_from_isr_wrapper`）
  にのみ付与されており，参照実装（`esp32c6/esp_adapter.c`）の
  IRAM_ATTR付与パターン（ISR文脈から呼ばれ得る関数群）とも一致。
  C6固有の追加関数（`phy_enable_wrapper`のレジスタ直書き等）はISR
  文脈から呼ばれない経路のため付与不要で問題なし。
- **クリティカルセクション（`esp_shim_int_disable/restore`，
  `esp_shim.c:49-64`）**：mstatus.MIEの単純な退避・復元でC3と完全に
  同一実装（diffで変更なし）。C3で動作実績（scan〜TCP/IPまで）が
  あるため，この機構自体が原因である可能性は極めて低い。
- **`wifi_osi_funcs_t`の全88フィールド**：指示付き初期化子で埋められて
  おり，フィールド抜け・型不一致による取り違えは構造的に発生しない
  （実施済みの構造完全一致確認、実施9冒頭「osi関数フィールドの
  欠落…棄却された仮説クラス」とも整合）。

---

## まとめ（順位）

| 順位 | 候補 | 該当箇所 | 確度 | 観測事実との整合性 | 検証コスト |
|---|---|---|---|---|---|
| 1 | `putchar()`スタブが一次診断出力を握りつぶしている可能性 | `esp_shim_blobglue.c:411-415` | 中（情報源としての価値） | 原因の直接説明ではないが未読の一次情報源 | 最小（1行＋1実機ログ） |
| 2 | `phy_get_max_pwr()`固定値スタブ | `esp_shim_blobglue.c:442-446` | 低〜中 | TXのみ説明可，RX無反応は説明不可 | 最小（値変更＋TXスニファ再実験） |
| 3 | `wifi_clock_enable_wrapper()`の無条件強制書込みの繰返し | `esp_wifi_adapter.c:631-688` | 低 | 初回起動一発目の症状は説明不可 | 最小（呼出し回数ログ） |
| 参考 | coex実リンク（Codex指摘） | `esp_wifi_adapter.c:1059-1081` | 反証済み | 実施9で無効化しても症状不変と実測済み | 実施済み |

**総合所見**：`esp_wifi_adapter.c`／`esp_shim.c`／`esp_shim_blobglue.c`の
3ファイルは，C3との差分・ESP-IDF参照実装との突合せの両面から見て，
「明白な契約違反」は既に修正済み（割込み型・WIFIPWR・regi2c・RNG・
eFuseアドレス）か，反証済み（coex）のいずれかであり，本レンズにおける
未発見の一次原因は見当たらなかった。唯一積極的に推す価値があるのは
**候補1（`putchar`フック）**——原因特定への寄与は不確実だが，コストが
ほぼゼロで84ラウンドが一度も開けていない情報源であるため，次の実機
ラウンドが発生する際の「ついでに1行」として強く推奨する。それ以外は
本セッションのCodex第4回評価（DMA/APM/PMP/PMA点検，同一個体NuttX A/B，
blob単一命令ステップ実行）が引き続き本命線であり，本レビューはその
判断を覆す材料を提供しない。

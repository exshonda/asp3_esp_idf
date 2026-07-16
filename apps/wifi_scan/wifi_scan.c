/*
 *  Wi-Fiスキャンデモ（ASP3＋esp_wifi blob＋os_adapter shim）
 *
 *  esp_wifi_init→start→scanを実行し，見つかったAPのSSID一覧を
 *  syslogへ出力する（Phase B-2aの動作確認．docs/wifi-shim.md）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "wifi_scan.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"
#ifdef TOPPERS_ESP32C6_WIFI
#include "wifi_trace.h"	/* C6 AGC調査専用の診断計装（wifi_trace.c／esp32c6_espidfのみ） */
#endif /* TOPPERS_ESP32C6_WIFI */
#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
#include "wifi_trace.h"	/* 実施16：C5 regi2cトレース（wifi_trace.c／esp32c5_espidfのみ） */
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */

/*
 *  スキャン完了通知（esp_event_shim経由）
 */
static ID	main_tskid;
static volatile bool_t scan_done;

/* DIAGNOSTIC (temporary, --wrap trace: promiscuous-mode RX test): */
static volatile uint32_t promisc_rx_count;

/*
 *  DIAGNOSTIC (temporary): g_ic（libnet80211.aのグローバル状態構造体．
 *  nmで実機アドレス確認済み＝0x408476b0）のoffset 497/499を直接
 *  ピークする．wifi_start_process/wifi_set_promis_processが
 *  wifi_hw_start呼出しをガードする条件がここにある（実施12/13参照）．
 */
#define DIAG_G_IC_BASE 0x408476b0UL
static uint8_t
diag_g_ic_byte(unsigned int off)
{
	return(*(volatile uint8_t *)(DIAG_G_IC_BASE + off));
}

#define DIAG_G_WIFI_NVS_ADDR 0x40800890UL
static uint32_t
diag_wifi_nvs_ptr(void)
{
	return(*(volatile uint32_t *)DIAG_G_WIFI_NVS_ADDR);
}

static uint8_t
diag_wifi_nvs_byte0(void)
{
	uint32_t	p = diag_wifi_nvs_ptr();
	if (p == 0U) {
		return(0xEEU);	/* NULLポインタの目印 */
	}
	return(*(volatile uint8_t *)p);
}

#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
/*
 *  【実施24】PD_TOP/HPAON/HPCPU/LPPERI force解除shimのA/B判定用，
 *  JTAG非依存のUART計装。wifi_scan.cfgのCRE_CYCから1秒周期で呼ばれる
 *  （TNFY_HANDLER＝タスク非依存コンテキスト。main_taskがPHY較正の
 *  無限リトライループ内で停止していても，タイマ割込み経由でこの
 *  ハンドラは独立して発火し続ける）。
 *
 *  出力：トーン自己ループバック測定の生ADCサンプル（MODEM0+0x81C）と
 *  IQ_DONEビット（MODEM0+0x47C bit16）＝実施14〜23で追ってきた症状
 *  そのもの，および実施23で新規差分と確定したPMU_POWER_PD_TOP/HPAON/
 *  HPCPU/LPPERI_CNTL（0x600B00F8/FC/100/10C）の現在値（shim適用の
 *  機械確認をJTAG無しで行うため）。
 */
void
wifi_diag_cyclic_handler(EXINF exinf)
{
	uint32_t	raw_adc = *(volatile uint32_t *)0x600A081CU;
	uint32_t	done    = *(volatile uint32_t *)0x600A047CU;
	uint32_t	pd_top    = *(volatile uint32_t *)0x600B00F8U;
	uint32_t	pd_hpaon  = *(volatile uint32_t *)0x600B00FCU;
	uint32_t	pd_hpcpu  = *(volatile uint32_t *)0x600B0100U;
	uint32_t	pd_lpperi = *(volatile uint32_t *)0x600B010CU;

	(void) exinf;

	syslog(LOG_NOTICE,
		   "wifi_diag: raw_adc=0x%08x done16=%d pd_top=0x%02x pd_hpaon=0x%02x",
		   (unsigned int)raw_adc, (int_t)((done >> 16) & 1U),
		   (unsigned int)pd_top, (unsigned int)pd_hpaon);
	syslog(LOG_NOTICE,
		   "wifi_diag: pd_hpcpu=0x%02x pd_lpperi=0x%02x",
		   (unsigned int)pd_hpcpu, (unsigned int)pd_lpperi);
}
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */

static void
promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
	(void) buf; (void) type;
	promisc_rx_count++;
}

#ifdef ESP32C5_R38_RXINSTR
/*
 *  【実施38】候補0（2×2判定）の(b) RX観測用，JTAG非依存のUART計装。
 *  1秒周期でpromiscuousコールバック数（3秒対照窓後もfilter=ALLで
 *  維持し続ける，scan実行中の受信も含む）とWi-Fi MAC割込み総数
 *  （esp_shim_int_count[1..15]，実施78のC6手法のC5移植）を出力する。
 *  併せてトーン自己ループバック測定チェーンの生ADC(0x600A081C)と
 *  IQ_DONE(0x600A047C bit16)＝実施20由来のAGC/noise floor生存代理指標も
 *  出す。TNFY_HANDLER（タスク非依存コンテキスト）なのでmain_taskが
 *  停止していても発火し続ける（実施24と同型）。
 */
void
r38_rxinstr_cyclic_handler(EXINF exinf)
{
	extern volatile uint32_t esp_shim_int_count[];
	static uint32_t last_promisc = 0U, last_int = 0U;
	uint32_t promisc_now = promisc_rx_count;
	uint32_t int_now = 0U;
	uint32_t raw_adc = *(volatile uint32_t *)0x600A081CU;
	uint32_t done    = *(volatile uint32_t *)0x600A047CU;
	int wi;

	(void) exinf;
	for (wi = 1; wi <= 15; wi++) {
		int_now += esp_shim_int_count[wi];
	}

	syslog(LOG_NOTICE,
		"R38RX: promisc=%d dpromisc=%d macint=%d dmacint=%d scandone=%d",
		(int_t)promisc_now, (int_t)(promisc_now - last_promisc),
		(int_t)int_now, (int_t)(int_now - last_int), (int_t)scan_done);
	syslog(LOG_NOTICE,
		"R38RX: raw_adc=0x%08x iq_done16=%d",
		(unsigned int)raw_adc, (int_t)((done >> 16) & 1U));

	last_promisc = promisc_now;
	last_int = int_now;
}
#endif /* ESP32C5_R38_RXINSTR */

#ifdef ESP32C5_R39_SELFHANDOFF
/*
 *  【実施39】ASP3→ASP3セルフハンドオフ（外部AI回答Q3判別＋候補1前哨）。
 *
 *  stockコードを一切経由せず，冷間Direct BootしたASP3自身から，
 *  flashオフセット0x200000に置いたASP3ゲスト（実施38のハンドオフ
 *  ゲストと同一バイナリ＝HANDOFF_SKIP_CLOCK_INIT+RXINSTR）へ，
 *  実施26/27/29のstockホスト版と同一の機構（割込み全マスク→
 *  ROMインターフェースWiFiポインタ域[0x4085fb80,0x4085ffc8)の
 *  ゼロクリア→MMU全unmap→0x42000000→0x200000の再マップ→cache
 *  invalidate→0x42000008へ直接ジャンプ）で制御を渡す。
 *
 *  判別（事前固定）：2周目ASP3のscanでAP>0なら「stockブート列は
 *  無関係＝POR後2回目のソフト起動（遷移の履歴）が鍵」（候補1=過渡
 *  ラッチ仮説を強く支持）。0APのままなら「stockブート列固有の何か」。
 *
 *  stockホスト版（スクラッチstock_scan/main/asp3_jump.c）との唯一の
 *  機構差分：ASP3はXIP（全コードがflash 0x42000000域で実行）のため，
 *  MMU unmap後に踏むコードをflashに置けない。そこでMMU操作＋cache
 *  invalidate＋ジャンプの最終段だけを.dataセクション（RAM常駐，
 *  start.Sが起動時にコピー済み）の自己完結スタブに置く。スタブは
 *  ROM常駐関数（Cache_Invalidate_Addr=0x40000650）とMMIOしか参照
 *  しない（文字列リテラル等のflash .rodata参照を持たない）。
 *
 *  ビルド時 ESP32C5_R39_SELFHANDOFF=1：起動直後（wifi完全未実行）に
 *  ジャンプ＝実施29のstock app_main先頭ジャンプと対照が揃う変種。
 *  =2：1周目でフルwifi初期化＋初回scan完走後にジャンプ＝実施26の
 *  stock scan完走後ジャンプと対照が揃う変種。既定（未定義）では
 *  完全に不活性。
 */
#define R39_GUEST_PADDR    0x00200000U
#define R39_GUEST_VADDR    0x42000000U
#define R39_GUEST_PAGES    16U            /* 16×64KB＝1MB（640KBゲストに十分） */
#define R39_ENTRY_VADDR    0x42000008U    /* Direct Bootエントリ（実施26と同一） */

/*
 *  C5のflash MMU：SPIMEM0（DR_REG_SPIMEM0_BASE=0x60002000）の
 *  ITEM_INDEX(+0x380)/ITEM_CONTENT(+0x37C)ペア経由の間接アクセス。
 *  512エントリ×64KBページ・VALID=bit10・FLASHターゲット=0
 *  （hal/components/hal/esp32c5/include/hal/mmu_ll.h・
 *  soc/ext_mem_defs.h・spi_mem_c_reg.hより。値はstockホスト版が
 *  実機で使ったmmu_hal_unmap_all/map_regionの実体と同一）。
 */
#define R39_MMU_ITEM_INDEX_REG    (*(volatile uint32_t *)0x60002380U)
#define R39_MMU_ITEM_CONTENT_REG  (*(volatile uint32_t *)0x6000237CU)
#define R39_MMU_ENTRY_NUM  512U
#define R39_MMU_VALID      0x00000400U    /* SOC_MMU_VALID=BIT(10) */

/*
 *  実施27で確立したROMインターフェースWiFiポインタ域（stale登録
 *  ポインタのゼロクリア範囲）。上限0x4085ffc8はcache/ets_ops等の
 *  ROM OSポインタ（ホスト自身が使用中）を保持するため（実施27）。
 */
#define R39_ROMIF_WIFI_PTRS_START  0x4085fb80U
#define R39_ROMIF_WIFI_PTRS_END    0x4085ffc8U   /* exclusive */

extern int Cache_Invalidate_Addr(uint32_t vaddr, uint32_t size);  /* ROM 0x40000650 */

typedef void (*r39_entry_fn)(void);

/*
 *  ジャンプ呼出しをこのvolatileフラグでガードする（実行時は常に1）。
 *  ガード無しだとGCCがr39_jump_stub末尾の無限ループからnoreturnを
 *  推論し，main_taskのジャンプ呼出し以降（esp_wifi_init〜scan一式）を
 *  デッドコード除去→wifi blob全体がGCされ，ホストイメージが
 *  実施38冷間ビルドと別物になってしまう（実測：FLASH 0.43%まで縮小）。
 *  ホストは「r38冷間ビルド＋ジャンプパス」の最小差分に保つ。
 */
static volatile uint_t r39_arm = 1U;

static void __attribute__((section(".data.r39_jump_stub"), noinline, aligned(4)))
r39_jump_stub(void)
{
	uint32_t	i;

	/* mmu_ll_unmap_all相当：全512エントリを無効化 */
	for (i = 0U; i < R39_MMU_ENTRY_NUM; i++) {
		R39_MMU_ITEM_INDEX_REG = i;
		R39_MMU_ITEM_CONTENT_REG = 0U;        /* SOC_MMU_INVALID */
	}
	/*
	 *  mmu_hal_map_region相当：vaddr 0x42000000〜（laddr=0→entry 0〜）
	 *  にpaddr 0x200000〜を16ページマップ。
	 */
	for (i = 0U; i < R39_GUEST_PAGES; i++) {
		R39_MMU_ITEM_INDEX_REG = i;
		R39_MMU_ITEM_CONTENT_REG = ((R39_GUEST_PADDR >> 16) + i) | R39_MMU_VALID;
	}
	(void) Cache_Invalidate_Addr(R39_GUEST_VADDR, R39_GUEST_PAGES << 16);
	((r39_entry_fn)R39_ENTRY_VADDR)();
	/* 戻ってくるはずがない */
	for (;;) {
	}
}

/*
 *  【実施39・診断】Espressif PMA CSR（0xBC0〜/0xBD0〜）のダンプ。
 *  Direct BootではROMが設定したPMAエントリが残り，SRAM(0x40800000〜)
 *  からの命令フェッチがInstruction access faultになることが実機で
 *  判明（r39_jump_stub先頭pc=0x40800004でフォルト）——どのエントリが
 *  阻んでいるかの特定用。
 */
static void
r39_pma_dump(void)
{
	uint32_t	cfg[16], addr[16];

	/* CSR番号は即値必須のため個別展開 */
	Asm("csrr %0, 0xBC0" : "=r"(cfg[0]));  Asm("csrr %0, 0xBD0" : "=r"(addr[0]));
	Asm("csrr %0, 0xBC1" : "=r"(cfg[1]));  Asm("csrr %0, 0xBD1" : "=r"(addr[1]));
	Asm("csrr %0, 0xBC2" : "=r"(cfg[2]));  Asm("csrr %0, 0xBD2" : "=r"(addr[2]));
	Asm("csrr %0, 0xBC3" : "=r"(cfg[3]));  Asm("csrr %0, 0xBD3" : "=r"(addr[3]));
	Asm("csrr %0, 0xBC4" : "=r"(cfg[4]));  Asm("csrr %0, 0xBD4" : "=r"(addr[4]));
	Asm("csrr %0, 0xBC5" : "=r"(cfg[5]));  Asm("csrr %0, 0xBD5" : "=r"(addr[5]));
	Asm("csrr %0, 0xBC6" : "=r"(cfg[6]));  Asm("csrr %0, 0xBD6" : "=r"(addr[6]));
	Asm("csrr %0, 0xBC7" : "=r"(cfg[7]));  Asm("csrr %0, 0xBD7" : "=r"(addr[7]));
	Asm("csrr %0, 0xBC8" : "=r"(cfg[8]));  Asm("csrr %0, 0xBD8" : "=r"(addr[8]));
	Asm("csrr %0, 0xBC9" : "=r"(cfg[9]));  Asm("csrr %0, 0xBD9" : "=r"(addr[9]));
	Asm("csrr %0, 0xBCA" : "=r"(cfg[10])); Asm("csrr %0, 0xBDA" : "=r"(addr[10]));
	Asm("csrr %0, 0xBCB" : "=r"(cfg[11])); Asm("csrr %0, 0xBDB" : "=r"(addr[11]));
	Asm("csrr %0, 0xBCC" : "=r"(cfg[12])); Asm("csrr %0, 0xBDC" : "=r"(addr[12]));
	Asm("csrr %0, 0xBCD" : "=r"(cfg[13])); Asm("csrr %0, 0xBDD" : "=r"(addr[13]));
	Asm("csrr %0, 0xBCE" : "=r"(cfg[14])); Asm("csrr %0, 0xBDE" : "=r"(addr[14]));
	Asm("csrr %0, 0xBCF" : "=r"(cfg[15])); Asm("csrr %0, 0xBDF" : "=r"(addr[15]));
	{
		int_t	i;
		for (i = 0; i < 16; i += 2) {
			syslog(LOG_NOTICE,
				   "R39PMA[%d]: cfg=%08x addr=%08x / cfg=%08x addr=%08x",
				   i, cfg[i], addr[i], cfg[i + 1], addr[i + 1]);
			(void) tslp_tsk(150000);	/* logtaskバースト取りこぼし回避 */
		}
	}
}

static void
r39_selfhandoff_now(const char *tag)
{
	volatile uint32_t	*p;

	/*
	 *  証拠プリント：ジャンプ直前のg_osi_funcs_p(0x4085ff60)／
	 *  g_coa_funcs_p(0x4085ffb4)の生値（stockホスト版と同一の観測点。
	 *  =2変種では1周目ASP3自身の登録値＝非ゼロ，=1変種ではゼロの
	 *  はず）。
	 */
	r39_pma_dump();

	/*
	 *  【実機実測（本ラウンド）】Direct BootではROMが残したPMAエントリ14
	 *  （NAPOT, 0x40800000/512KB＝HP SRAM全域, cfg=0xC0000019＝R+W+EN,
	 *  Xなし, L=0）がSRAMからの命令フェッチを禁じており，RAM常駐スタブの
	 *  実行が最初の1命令（pc=0x40800004）でInstruction access faultに
	 *  なる。L=0（非ロック）なのでX付与で解除できる（cfg=0xC000001D）。
	 *  stockブート列もcall_start_cpu0の region protection 設定でROMの
	 *  PMAを自前の設定へ上書きしてIRAM実行を可能にしている——本書換えは
	 *  その最小等価物（エントリ14へのX付与のみ，範囲・他属性は不変）。
	 */
	Asm("csrw 0xBCE, %0" : : "r"(0xC000001DUL));
	{
		uint32_t	chk;
		Asm("csrr %0, 0xBCE" : "=r"(chk));
		syslog(LOG_NOTICE, "R39: pmacfg14 X-enable -> %08x", chk);
	}

	syslog(LOG_NOTICE, "R39: self-handoff (%s): g_osi=%08x g_coa=%08x",
		   tag,
		   (uint_t)*(volatile uint32_t *)0x4085ff60U,
		   (uint_t)*(volatile uint32_t *)0x4085ffb4U);
	syslog(LOG_NOTICE, "R39: jumping to ASP3 guest @0x200000 now");
	(void) tslp_tsk(500000);	/* logtask→UART FIFOのドレイン待ち */

	/* 割込み全マスク（stockホスト版asp3_jump_now()と同一手順） */
	Asm("csrw mie, zero");
	Asm("csrci mstatus, 0x8");

	/* stale登録ポインタ域のゼロクリア（実施27。2周目の再登録を保証） */
	for (p = (volatile uint32_t *)R39_ROMIF_WIFI_PTRS_START;
		 p < (volatile uint32_t *)R39_ROMIF_WIFI_PTRS_END; p++) {
		*p = 0U;
	}

	r39_jump_stub();
}
#endif /* ESP32C5_R39_SELFHANDOFF */

#if defined(ESP32C5_R40_MODEMRST) || defined(ESP32C5_R40_PMUPULSE)
/*  段階(i)/(ii)共通の観測用レジスタ（両方から参照するためifdef外に置く）。 */
#define R40_MODEM_CLK_CONF_REG          (*(volatile uint32_t *)0x600A9C04U)
#define R40_ANA_CONF0_REG               (*(volatile uint32_t *)0x600AF818U)

extern void esp_rom_delay_us(uint32_t us);
#endif /* ESP32C5_R40_MODEMRST || ESP32C5_R40_PMUPULSE */

#ifdef ESP32C5_R40_MODEMRST
/*
 *  【実施40】候補1（外部AI回答・過渡ラッチ仮説）段階(i)：
 *  MODEM_SYSCONの全modemリセット一括パルス（set→短delay→clear）。
 *
 *  実施38が「TX無・RX無＝TX/RX共通のフロントエンド/シンセ段で死亡」と
 *  絞り込んだ症状に対し，MODEM_SYSCON_MODEM_RST_CONF_REG（0x600A9C10）の
 *  定義済み全ビット（RST_WIFIBB，RST_WIFIMAC，RST_FE，RST_FE_AHB，
 *  RST_FE_ADC，RST_FE_DAC，RST_FE_PWDET_ADC，RST_BTBB系，RST_ZBMAC系，
 *  RST_MODEM_ECB/CCM/BAH/SEC，RST_ETM，RST_BLE_TIMER，RST_DATA_DUMP，
 *  実施36調査済みのRST_WIFIMAC含む）を
 *  esp_wifi_init直前で一括アサート→短delay→デアサートする。
 *  読み出し・単発値注入では原理的に触れない「ハードウェアリセットのみが
 *  クリアできるラッチ/同期状態」が実在するかを判別する（値の移植では
 *  なく操作そのものを試す）。
 *
 *  マスクはビルド時定義で上書き可能（既定＝全定義済みビット，bisection
 *  実験用にASP3_EXTRA_COMPILE_DEFSでFE系のみ等へ絞り込める）。
 */
#define R40_MODEM_RST_CONF_REG          (*(volatile uint32_t *)0x600A9C10U)
#define R40_MODEM_CLK_CONF_POWER_ST_REG (*(volatile uint32_t *)0x600A9C0CU)
#define R40_MODEM_CLK_CONF1_REG         (*(volatile uint32_t *)0x600A9C14U)

#ifndef R40_MODEM_RST_MASK
/*  全定義済みRST_*ビット（bit8-18,22-27,29-31。19-21/28は未定義=予約と
 *  推定し対象外，0-7も同様）。 */
#define R40_MODEM_RST_MASK  0xEFC7FF00UL
#endif /* R40_MODEM_RST_MASK */

static void
r40_modemrst_pulse(void)
{
	uint32_t	pre_rst, pre_clk, pre_pst, pre_c1, pre_ana;
	uint32_t	mid_rst;
	uint32_t	post_rst, post_clk, post_pst, post_c1, post_ana;

	pre_rst = R40_MODEM_RST_CONF_REG;
	pre_clk = R40_MODEM_CLK_CONF_REG;
	pre_pst = R40_MODEM_CLK_CONF_POWER_ST_REG;
	pre_c1  = R40_MODEM_CLK_CONF1_REG;
	pre_ana = R40_ANA_CONF0_REG;
	syslog(LOG_NOTICE, "R40i: pre rst=%08x clk=%08x pst=%08x",
		   (unsigned int)pre_rst, (unsigned int)pre_clk, (unsigned int)pre_pst);
	(void) tslp_tsk(20000);	/* syslogバースト取りこぼし回避（実施39流儀） */
	syslog(LOG_NOTICE, "R40i: pre c1=%08x ana0=%08x mask=%08x",
		   (unsigned int)pre_c1, (unsigned int)pre_ana,
		   (unsigned int)R40_MODEM_RST_MASK);
	(void) tslp_tsk(20000);

	R40_MODEM_RST_CONF_REG = R40_MODEM_RST_MASK;
	esp_rom_delay_us(50U);
	mid_rst = R40_MODEM_RST_CONF_REG;	/* パルス成立の読み戻し確認 */
	R40_MODEM_RST_CONF_REG = 0U;
	esp_rom_delay_us(50U);

	post_rst = R40_MODEM_RST_CONF_REG;
	post_clk = R40_MODEM_CLK_CONF_REG;
	post_pst = R40_MODEM_CLK_CONF_POWER_ST_REG;
	post_c1  = R40_MODEM_CLK_CONF1_REG;
	post_ana = R40_ANA_CONF0_REG;
	syslog(LOG_NOTICE, "R40i: mid(during-pulse) rst=%08x", (unsigned int)mid_rst);
	(void) tslp_tsk(20000);
	syslog(LOG_NOTICE, "R40i: post rst=%08x clk=%08x pst=%08x",
		   (unsigned int)post_rst, (unsigned int)post_clk, (unsigned int)post_pst);
	(void) tslp_tsk(20000);
	syslog(LOG_NOTICE, "R40i: post c1=%08x ana0=%08x (CAL_DONE bit24 sentinel)",
		   (unsigned int)post_c1, (unsigned int)post_ana);
	(void) tslp_tsk(20000);
}
#endif /* ESP32C5_R40_MODEMRST */

#ifdef ESP32C5_R40_PMUPULSE
/*
 *  【実施40】候補1段階(ii)：PMU HP_WIFI電源ドメインのoff→on過渡パルス
 *  （段階(i)が空振りの場合のみ実施）。
 *
 *  対象＝`PMU_POWER_PD_HPWIFI_CNTL_REG`（0x600B0108）。実施22は本
 *  レジスタの**静的終値**（stock実測=0x0＝force全解除）をmid-hang注入・
 *  before-PHY移植の両方で因果棄却済み——本段の新規性は終値ではなく
 *  **遷移そのもの**（force power-down→短delay→復帰）であり，実施22が
 *  触れていない変数を狙う。復帰先はASP3の元の値（POR既定0x1C＝
 *  FORCE_PU/NO_RESET/NO_ISO=1のまま）とし，終値を変えることによる
 *  実施22との交絡を避ける（変えるのは「経由したか」だけ）。
 *
 *  実施23の前例（PD系force書込みをWiFi init時点で行うとJTAG捕捉の
 *  ブート到達性が悪化した）に鑑み，タイムアウト付き読み戻しで異常を
 *  即検知できるようにし，「悪化したら即revert」を徹底する。
 */
#define R40_PMU_PD_HPWIFI_CNTL_REG  (*(volatile uint32_t *)0x600B0108U)
#define R40_PMU_FORCE_HP_WIFI_RESET   0x01U  /* bit0 */
#define R40_PMU_FORCE_HP_WIFI_ISO     0x02U  /* bit1 */
#define R40_PMU_FORCE_HP_WIFI_PU      0x04U  /* bit2 */
#define R40_PMU_FORCE_HP_WIFI_NORESET 0x08U  /* bit3 */
#define R40_PMU_FORCE_HP_WIFI_NOISO   0x10U  /* bit4 */
#define R40_PMU_FORCE_HP_WIFI_PD      0x20U  /* bit5 */

static void
r40_pmupulse_cycle(void)
{
	uint32_t	pre, down, restored;
	uint32_t	pre_clk, pre_ana;
	uint32_t	post_clk, post_ana;

	pre = R40_PMU_PD_HPWIFI_CNTL_REG;
	pre_clk = R40_MODEM_CLK_CONF_REG;
	pre_ana = R40_ANA_CONF0_REG;
	syslog(LOG_NOTICE, "R40ii: pre pd_hpwifi=%08x clk=%08x ana0=%08x",
		   (unsigned int)pre, (unsigned int)pre_clk, (unsigned int)pre_ana);
	(void) tslp_tsk(20000);

	/*  power down: FORCE_RESET=1,FORCE_ISO=1,FORCE_PD=1，PU/NORESET/NOISOは
	 *  クリア（force保持を外して実際にpower-down状態へ入れる）。 */
	R40_PMU_PD_HPWIFI_CNTL_REG =
		R40_PMU_FORCE_HP_WIFI_RESET | R40_PMU_FORCE_HP_WIFI_ISO |
		R40_PMU_FORCE_HP_WIFI_PD;
	esp_rom_delay_us(100U);
	down = R40_PMU_PD_HPWIFI_CNTL_REG;
	syslog(LOG_NOTICE, "R40ii: during-down pd_hpwifi=%08x", (unsigned int)down);
	(void) tslp_tsk(20000);

	/*  power up: ASP3の元の値（0x1C＝FORCE_PU/NO_RESET/NO_ISO=1）へ復帰。
	 *  実施22が既に因果棄却した「終値0x0（force全解除）」とは別条件に
	 *  留める（変数を遷移の有無だけに絞る）。 */
	R40_PMU_PD_HPWIFI_CNTL_REG = pre;
	esp_rom_delay_us(100U);
	restored = R40_PMU_PD_HPWIFI_CNTL_REG;
	post_clk = R40_MODEM_CLK_CONF_REG;
	post_ana = R40_ANA_CONF0_REG;
	syslog(LOG_NOTICE, "R40ii: post pd_hpwifi=%08x clk=%08x ana0=%08x",
		   (unsigned int)restored, (unsigned int)post_clk, (unsigned int)post_ana);
	(void) tslp_tsk(20000);
}
#endif /* ESP32C5_R40_PMUPULSE */

static void
wifi_event_handler(void *arg, const char *base, int32_t id, void *data)
{
	(void) arg; (void) base; (void) data;
	if (id == WIFI_EVENT_SCAN_DONE) {
		scan_done = true;
		(void) wup_tsk(main_tskid);
	}
}

void
main_task(EXINF exinf)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	uint16_t			num;
	wifi_ap_record_t	*recs;
	esp_err_t			err;
	uint16_t			i;

	(void) exinf;
	(void) get_tid(&main_tskid);

#if defined(ESP32C5_R39_SELFHANDOFF) && (ESP32C5_R39_SELFHANDOFF == 1)
	/*
	 *  【実施39・変種1】起動直後（wifi完全未実行）セルフハンドオフ。
	 *  ASP3のブート列相当（hardware_init_hook＋カーネル起動）のみを
	 *  1周目として2周目へジャンプする＝実施29のstock app_main先頭
	 *  ジャンプと対照が揃う。3秒待つのはバナー・R38RX計装数行を
	 *  1周目ベースラインとしてUARTに出させるため。
	 */
	(void) tslp_tsk(3000000);
	if (r39_arm != 0U) {
		r39_selfhandoff_now("at-boot");
	}
#endif /* ESP32C5_R39_SELFHANDOFF == 1 */

#ifdef TOPPERS_ESP32C6_WIFI
	/*
	 *  DIAGNOSTIC（Step0 option2）：RTC RAM(0x50000000〜)にosiカウンタを
	 *  累積（LP Super WDTリセットを跨いで保持されることを確認済み）．
	 *  レイアウト：[0]magic [1]boot# [2]semTake [3]semGive [4]qRecv
	 *              [5]qSend [6]qSendISR [7]timerArm
	 *  各ブート冒頭で累積値をダンプ→1サイクルあたりの増分とリセット時刻
	 *  （blobログの "W (NNN)"）から頻度を算出し，native（40〜80/s）と比較．
	 */
	{
		volatile uint32_t *g = (volatile uint32_t *)0x50000000U;
		uint_t k;
		if (g[0] != 0xC6057A11U) {	/* 電源投入時=magic無し→全クリア */
			for (k = 0U; k < 16U; k++) {
				g[k] = 0U;
			}
			g[0] = 0xC6057A11U;
		}
		syslog(LOG_NOTICE,
			   "GT-ASP3 accum boot#=%d: semTake=%d semGive=%d qRecv=%d qSend=%d",
			   (int_t)g[1], (int_t)g[2], (int_t)g[3], (int_t)g[4],
			   (int_t)g[5]);
		/*  前回runのscan結果（flood-proof：Wi-Fi起動前=氾濫前に出力）．
		 *  [8]AP件数 [9]scan_get err [10]到達マーカ(1:loop脱出 2:calloc後
		 *  3:get_ap_records後) [11]qRecv増分の目安 */
		syslog(LOG_NOTICE,
			   "GT-ASP3 prev-scan: reach=%d apcount=%d scanerr=%d timerArm=%d qSendISR=%d",
			   (int_t)g[10], (int_t)g[8], (int_t)g[9],
			   (int_t)g[7], (int_t)g[6]);
		/*  HRTスプリアス割込み計測＋Wi-Fi割込み配送数（前回run累積）：
		 *  hrtEntries=HRT割込み総数 hrtFired=alarm発火主張 hrtSpur=うち
		 *  counter<target（＝スプリアス） wifiInt=Wi-Fi線の発火総数
		 *  （0近傍ならRX割込みが配送されていない＝0 APの根因） */
		syslog(LOG_NOTICE,
			   "GT-ASP3 hrtEntries=%d hrtFired=%d hrtSpur=%d wifiInt=%d",
			   (int_t)g[12], (int_t)g[13], (int_t)g[14], (int_t)g[11]);
		g[1] = g[1] + 1U;
	}
	/*  boot dumpの3行をlogtaskに吐かせてからWi-Fi起動（blobの同時
	 *  UART出力で文字化けするのを避ける）． */
	(void) tslp_tsk(300000);
#endif /* TOPPERS_ESP32C6_WIFI */

	syslog(LOG_NOTICE, "wifi_scan: initializing shim");
	esp_shim_initialize();
#ifdef TOPPERS_ESP32C6_WIFI
	wifi_trace_reset();
	wifi_regi2c_reset();	/* DIAGNOSTIC (temporary，実施23／Priority 2) */
	wifi_regi2c_patch_install();	/* DIAGNOSTIC（実施23）：PHY初期化前に必ずインストール */
	wifi_taskdelay_reset();	/* DIAGNOSTIC（実施26／タイミング感度調査） */
#endif /* TOPPERS_ESP32C6_WIFI */

	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);

	esp_shim_coex_adapter_register();

#ifdef WIFI_SCAN_PREINIT_SPIN
	/*
	 *  DIAGNOSTIC（一時的，実施31）：esp_wifi_init()直前でスピンし，
	 *  JTAGで全域レジスタダンプを取るための同期点．実験後にrevert．
	 */
	syslog(LOG_NOTICE, "wifi_scan: PREINIT_SPIN reached, halting here");
	while (true) {
		(void) tslp_tsk(1000000);
	}
#endif /* WIFI_SCAN_PREINIT_SPIN */

#ifdef HANDOFF_SKIP_WIFI_INIT
	/*
	 *  DIAGNOSTIC（ハンドオフStep0）：ESP-IDFが既にesp_wifi_init/start
	 *  まで済ませたWi-Fiの上へジャンプしてきた場合，ASP3側で
	 *  esp_wifi_init を再実行すると二重初期化でblobの残留グローバル
	 *  （ESP-IDF時代の関数ポインタ）を踏んでIllegal instructionになる．
	 *  そこでinitをスキップし，その先（scan）だけを試す切り分け．
	 */
	syslog(LOG_NOTICE, "wifi_scan: HANDOFF_SKIP_WIFI_INIT (esp_wifi_init/start をスキップ)");
	(void) cfg;
#else /* HANDOFF_SKIP_WIFI_INIT */
#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
	/*  実施16：esp_wifi_init()直前でリングバッファをリセットし，
	 *  アドレスを一度だけ出力する（JTAGでの生メモリ直読み用）。 */
	wifi_regi2c_reset();
	wifi_regi2c_dump_addr();
	/*  実施18：phy_set_txcap_reg引数トレース用の第2リングバッファも
	 *  同じタイミングでリセット・アドレス出力する。 */
	wifi_txcap_reset();
	wifi_txcap_dump_addr();
	/*  実施25：未公開regi2c block(0x63/0x68/0x6b)アクセス時の
	 *  ANA_CONF1/ANA_CONF2記録バッファも同じタイミングでリセット・
	 *  アドレス出力する。 */
	wifi_regi2c_cfgsnap_reset();
	wifi_regi2c_cfgsnap_dump_addr();
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */
#ifdef ESP32C5_R36_REGI2C_SEED
	/*
	 *  DIAGNOSTIC（一時的，実施36・点(b)因果検証）：ハンドオフ(WORKS)
	 *  実測のregi2c系譜差分8箇所を，PHY較正（esp_wifi_init内）より前に
	 *  シードする。書込み成立は読み戻し（esp32c5_r36_seed_readback[]）
	 *  で確認・syslogへ出力。既定ビルドでは未定義（実験時のみ
	 *  ASP3_EXTRA_COMPILE_DEFSで有効化）。
	 */
	{
		extern void esp32c5_r36_regi2c_seed(void);
		extern uint32_t esp32c5_r36_seed_readback[10];
		esp32c5_r36_regi2c_seed();
		syslog(LOG_NOTICE, "wifi_scan: R36SEED rb0-4 %02x %02x %02x %02x %02x",
			   (int_t)esp32c5_r36_seed_readback[0],
			   (int_t)esp32c5_r36_seed_readback[1],
			   (int_t)esp32c5_r36_seed_readback[2],
			   (int_t)esp32c5_r36_seed_readback[3],
			   (int_t)esp32c5_r36_seed_readback[4]);
		syslog(LOG_NOTICE, "wifi_scan: R36SEED rb5-8 %02x %02x %02x %02x marker=%08x",
			   (int_t)esp32c5_r36_seed_readback[5],
			   (int_t)esp32c5_r36_seed_readback[6],
			   (int_t)esp32c5_r36_seed_readback[7],
			   (int_t)esp32c5_r36_seed_readback[8],
			   (int_t)esp32c5_r36_seed_readback[9]);
	}
#endif /* ESP32C5_R36_REGI2C_SEED */
#ifdef ESP32C5_R40_MODEMRST
	/*
	 *  【実施40】候補1段階(i)：esp_wifi_init直前でMODEM_SYSCON全modem
	 *  リセットを一括パルス。
	 */
	r40_modemrst_pulse();
#endif /* ESP32C5_R40_MODEMRST */
#ifdef ESP32C5_R40_PMUPULSE
	/*
	 *  【実施40】候補1段階(ii)：esp_wifi_init直前でPMU HP_WIFI電源
	 *  ドメインのoff→on過渡パルス（段階(i)が空振りの場合のみ）。
	 */
	r40_pmupulse_cycle();
#endif /* ESP32C5_R40_PMUPULSE */
#ifdef ESP32C5_R41_CALL_ESPWIFIINIT
	/*
	 *  【実施41・変種A（主変種）】外部AI候補2＝実施35の残差19語のうち
	 *  R/Wで注入可能な14語を，esp_wifi_init直前で一括注入する。
	 */
	{
		extern void esp32c5_r41_combined_seed(void);
		esp32c5_r41_combined_seed();
		syslog(LOG_NOTICE, "wifi_scan: R41SEED called (ESPWIFIINIT variant)");
		/*  実施35 12節・rigor docの「syslogバースト損失」対策：blob側の
		 *  並行UART出力（esp_wifi_init直前後のpp/mac_vers等）と衝突しない
		 *  よう，各syslog呼出しの間にログタスクへ処理時間を与える。  */
		(void) tslp_tsk(50000);
	}
#endif /* ESP32C5_R41_CALL_ESPWIFIINIT */
#ifdef ESP32C5_R41_COMBINED19
	/*
	 *  変種A・変種B共通：esp_wifi_init直前（＝較正直前）に14語の
	 *  「生」再読取りを行い，注入がこの時点まで保持されているかを
	 *  確認する（変種Bの場合は特に，boot-hook末尾からここまでの間に
	 *  ASP3自身の他の処理やHWの自動補正で上書きされていないかの確認）。
	 */
	{
		extern void esp32c5_r41_dump_live(void);
		extern uint32_t esp32c5_r41_live_readback[14];
		extern uint32_t esp32c5_r41_live_mismatch_count;
		extern uint32_t esp32c5_r41_mismatch_count;
		extern uint32_t esp32c5_r41_first_mismatch_addr;
		esp32c5_r41_dump_live();
		syslog(LOG_NOTICE,
			   "wifi_scan: R41 seed_mismatch=%d live_mismatch=%d first_mismatch_addr=%x",
			   (int_t)esp32c5_r41_mismatch_count,
			   (int_t)esp32c5_r41_live_mismatch_count,
			   (int_t)esp32c5_r41_first_mismatch_addr);
		(void) tslp_tsk(50000);
		syslog(LOG_NOTICE, "wifi_scan: R41 live0-3 %x %x %x %x",
			   (int_t)esp32c5_r41_live_readback[0],
			   (int_t)esp32c5_r41_live_readback[1],
			   (int_t)esp32c5_r41_live_readback[2],
			   (int_t)esp32c5_r41_live_readback[3]);
		(void) tslp_tsk(50000);
		syslog(LOG_NOTICE, "wifi_scan: R41 live4-7 %x %x %x %x",
			   (int_t)esp32c5_r41_live_readback[4],
			   (int_t)esp32c5_r41_live_readback[5],
			   (int_t)esp32c5_r41_live_readback[6],
			   (int_t)esp32c5_r41_live_readback[7]);
		(void) tslp_tsk(50000);
		syslog(LOG_NOTICE, "wifi_scan: R41 live8-11 %x %x %x %x",
			   (int_t)esp32c5_r41_live_readback[8],
			   (int_t)esp32c5_r41_live_readback[9],
			   (int_t)esp32c5_r41_live_readback[10],
			   (int_t)esp32c5_r41_live_readback[11]);
		(void) tslp_tsk(50000);
		syslog(LOG_NOTICE, "wifi_scan: R41 live12-13 %x %x",
			   (int_t)esp32c5_r41_live_readback[12],
			   (int_t)esp32c5_r41_live_readback[13]);
		(void) tslp_tsk(50000);
	}
#endif /* ESP32C5_R41_COMBINED19 */
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init");
	err = esp_wifi_init(&cfg);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init -> %d", (int_t)err);
	if (err != 0) {
		return;
	}
#endif /* HANDOFF_SKIP_WIFI_INIT */
#if !defined(HANDOFF_SKIP_WIFI_INIT) && defined(TOPPERS_ESP32C6_WIFI)
	/*  DIAG_G_IC_BASE/DIAG_G_WIFI_NVS_ADDRはC6のlibnet80211.aブロブで
	 *  実測したアドレス（実施12/13）——C3では別blob・別アドレスのため
	 *  無guardのまま実行すると無関係な値を誤ってg_ic/nvsとして表示する
	 *  （docs/wifi-scan-c3-crash.md 実施1で発見，crash自体は別要因だが
	 *  同じ「C6専用診断のguard漏れ」系統のためついでにguard）．*/
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-init g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-init nvs_ptr=%x nvs[0]=%d",
		   (int_t)diag_wifi_nvs_ptr(), (int_t)diag_wifi_nvs_byte0());
#endif /* !HANDOFF_SKIP_WIFI_INIT && TOPPERS_ESP32C6_WIFI */

#ifndef HANDOFF_SKIP_WIFI_INIT
	(void) esp_wifi_set_mode(WIFI_MODE_STA);
#ifdef TOPPERS_ESP32C6_WIFI
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-set_mode g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
#endif /* TOPPERS_ESP32C6_WIFI */
	(void) esp_wifi_set_storage(WIFI_STORAGE_RAM);
#ifdef WIFI_SCAN_PS_MIN_MODEM
	/*
	 *  DIAGNOSTIC（一時的，実施71／Codex申し送り(a)の因果実験）：
	 *  native側scanアプリはesp_wifi_set_psを一切呼ばず，STAモードの
	 *  既定値WIFI_PS_MIN_MODEM（モデムスリープ有効）のまま動作する．
	 *  実施71でこれがesp_phy_enable/esp_phy_disable（osi_funcsの
	 *  _phy_enable/_phy_disable経由）の周期的呼出し（native実測4回/
	 *  scan）を駆動し，2回目以降のesp_phy_enableがs_is_phy_calibrated
	 *  既真のため`phy_wakeup_init`（FE再初期化，`fe_txrx_reset`含む）
	 *  を実行することを突き止めた．ASP3は下のWIFI_PS_NONEにより
	 *  esp_phy_enableを一度しか呼ばず`phy_wakeup_init`に一度も到達
	 *  しない．本フラグでWIFI_PS_NONEをWIFI_PS_MIN_MODEMに切替え，
	 *  周期的なPHY re-enable／FE再初期化が起きるかを検証する
	 *  （原因か症状かの切り分け用，恒久変更ではない）．
	 *
	 *  ★実施71実測：本フラグを有効にして実機検証した結果，
	 *  WIFI_PS_MIN_MODEMに切替えてもASP3のesp_phy_enableは依然
	 *  1回しか呼ばれず，`phy_wakeup_init`にも到達しなかった（陰性
	 *  結果）．すなわちnativeのesp_phy_enable/disable周期呼出しは
	 *  単純なPS設定値の違いでは説明できない——スキャン中（未接続）は
	 *  DTIM同期を伴う本来のモデムスリープが適用される状況ではない
	 *  ため，別の要因（blob内部のスキャン専用の電源管理ロジック，
	 *  もしくはOSプリミティブ／タイマ連携の違い）が真因である可能性が
	 *  高い．本フラグは反証済みの実験として残置（削除しない）．
	 */
	(void) esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#else /* WIFI_SCAN_PS_MIN_MODEM */
	(void) esp_wifi_set_ps(WIFI_PS_NONE);
#endif /* WIFI_SCAN_PS_MIN_MODEM */

	err = esp_wifi_start();
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_start -> %d", (int_t)err);
#ifdef TOPPERS_ESP32C6_WIFI
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-start g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-start nvs_ptr=%x nvs[0]=%d",
		   (int_t)diag_wifi_nvs_ptr(), (int_t)diag_wifi_nvs_byte0());
#endif /* TOPPERS_ESP32C6_WIFI */
	if (err != 0) {
		return;
	}

	/* DIAGNOSTIC (temporary, --wrap trace: promiscuous-mode RX test): */
	{
		err = esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
		syslog(LOG_NOTICE, "wifi_scan: DIAG set_promiscuous_rx_cb -> %d",
			   (int_t)err);
#ifdef ESP32C5_R38_RXINSTR
		/*  実施38：候補0のRX観測はscan実行中も継続するため，
		 *  filter maskをALL（mgmt/data/ctrl/misc全種別）へ明示設定する。 */
		{
			wifi_promiscuous_filter_t filt;
			filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
			err = esp_wifi_set_promiscuous_filter(&filt);
			syslog(LOG_NOTICE, "wifi_scan: R38 set_promiscuous_filter(ALL) -> %d",
				   (int_t)err);
		}
#endif /* ESP32C5_R38_RXINSTR */
		err = esp_wifi_set_promiscuous(true);
		syslog(LOG_NOTICE, "wifi_scan: DIAG set_promiscuous(true) -> %d",
			   (int_t)err);
		(void) tslp_tsk(3000000);	/* 3秒間，周辺の電波を受信できるか観測 */
		syslog(LOG_NOTICE, "wifi_scan: DIAG promisc_rx_count=%d",
			   (int_t)promisc_rx_count);
#ifdef ESP32C5_R38_RXINSTR
		/*  実施38：ここではdisableしない——scan実行中も含めてpromiscuous
		 *  RXカウントを取り続ける（r38_rxinstr_cyclic_handlerが1秒周期で
		 *  出力）。 */
#else
		(void) esp_wifi_set_promiscuous(false);
#endif /* ESP32C5_R38_RXINSTR */
	}
#endif /* HANDOFF_SKIP_WIFI_INIT */

#ifdef TOPPERS_ESP32C6_WIFI
	wifi_regsnap_reset();	/* DIAGNOSTIC (temporary, Priority 2) */
	/*  ★根本原因テスト（追記10）：JTAGでnative(受信OK)=0x7 vs ASP3=0x0の
	 *  差分が判明した MODEM_LPCON_CLK_CONF(0x600af018) を強制的に0x7へ．
	 *  bit0=WIFIPWR bit1=COEX bit2=I2C_MST(RF regi2c用)クロック．これで
	 *  APが検出できれば，shimのクロックenable欠落が0 APの根因と確定． */
	*(volatile uint32_t *)0x600af018U = 0x7U;
#endif /* TOPPERS_ESP32C6_WIFI */
	err = esp_wifi_scan_start(NULL, false);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_scan_start -> %d", (int_t)err);

	{
		/*  DIAGNOSTIC（Step0 option2）：scan待ちの間，RTC-RAMのosiカウンタ
		 *  (0x50000008〜．adapterがライブ加算)を毎秒読み，per-secデルタを
		 *  出力してosi呼出しレート（388Hz storm再現の有無）を実測する．
		 *  syslogは5引数上限のため1行5値．scan_doneが来なくても
		 *  20秒で抜けて後段のダンプへ進む．*/
		volatile uint32_t *g = (volatile uint32_t *)0x50000000U;
		uint32_t lsemt = g[2], lqr = g[4], lqs = g[5], lqsi = g[6], lta = g[7];
		int sec;
		for (sec = 0; sec < 20 && !scan_done; sec++) {
#ifdef TOPPERS_ESP32C6_WIFI
			/*  0x600af018=MODEM_LPCON_CLK_CONF_REGはC6(H2/H4/H21/C61等の
			 *  新modem系統)にのみ存在する周辺で，C3のペリフェラルバスには
			 *  対応レジスタが存在しない（hal/esp32c3のreg_base.hに該当
			 *  ベースなし）．無guardのままC3でも毎秒書込みしていたが，
			 *  未使用領域への書込みでクラッシュはしないため今回のIllegal
			 *  instruction本体とは無関係——ただし同種のguard漏れなのでC6
			 *  専用にguardする（docs/wifi-scan-c3-crash.md 実施1）．*/
			*(volatile uint32_t *)0x600af018U = 0x7U;	/* 追記10：クロック再アサート */
#endif /* TOPPERS_ESP32C6_WIFI */
			(void) tslp_tsk(1000000);	/* 1秒 */
			syslog(LOG_NOTICE,
				"OSIRATE/s semTake=%d qRecv=%d qSend=%d qSendISR=%d timerArm=%d",
				(int_t)(g[2] - lsemt), (int_t)(g[4] - lqr),
				(int_t)(g[5] - lqs), (int_t)(g[6] - lqsi),
				(int_t)(g[7] - lta));
			lsemt = g[2]; lqr = g[4]; lqs = g[5]; lqsi = g[6]; lta = g[7];
		}
	}
#if defined(TOPPERS_ESP32C6_WIFI) && 0
	/*  DIAGNOSTIC（Step0 option2）：これらの重いダンプはlogtaskを溢れさせ
	 *  "APs found"行を飲み込むため一時無効化．スキャン完走/AP件数の確認を
	 *  優先する（調査完了後に復帰）．*/
	wifi_trace_dump_counts();
	wifi_regi2c_dump_count();
	wifi_trace_dump();
	wifi_regsnap_dump();
	wifi_regi2c_dump();
	wifi_taskdelay_dump();
	wifi_phyinit_dump();
	wifi_trace_dump_addr();
#endif /* TOPPERS_ESP32C6_WIFI */

	*(volatile uint32_t *)0x50000028U = 1U;	/* [10]=reach 1: loop脱出 */
	num = 20;
	recs = (wifi_ap_record_t *)
				esp_shim_calloc(num, sizeof(wifi_ap_record_t));
	if (recs == NULL) {
		return;
	}
	*(volatile uint32_t *)0x50000028U = 2U;	/* [10]=reach 2: calloc後 */
	err = esp_wifi_scan_get_ap_records(&num, recs);
	*(volatile uint32_t *)0x50000020U = (uint32_t)num;	/* [8]=AP件数 */
	*(volatile uint32_t *)0x50000024U = (uint32_t)err;	/* [9]=scan_get err */
	*(volatile uint32_t *)0x50000028U = 3U;	/* [10]=reach 3: get後 */
	{
		/*  Wi-Fi割込み線(1〜15)の発火総数をRTC[11]へ．blobがset_intrで
		 *  ルーティングしたMAC/RX割込みが実際に配送されているかの実測．*/
		extern volatile uint32_t esp_shim_int_count[];
		uint32_t wsum = 0U;
		int wi;
		for (wi = 1; wi <= 15; wi++) {
			wsum += esp_shim_int_count[wi];
		}
		*(volatile uint32_t *)0x5000002CU = wsum;	/* [11]=Wi-Fi int総数 */
	}
#ifdef TOPPERS_ESP32C6_WIFI
	/*
	 *  DIAGNOSTIC（C6専用，実施(追記12)）：C6の固定ROM PHYFUNS表アドレス
	 *  (0x4087f954)のidx23=read_mask関数ポインタを直接呼ぶ．
	 *  ★C3では未guardのままこの分岐を素通しした結果，このアドレスの
	 *  idx23エントリが未設定(NULL)のままjalrされ，Illegal instruction
	 *  (pc=0)でクラッシュすることを確認した（docs/wifi-scan-c3-crash.md
	 *  実施1）．esp_coex_adapter.cで既に確立している同種のC6専用診断
	 *  （同じ0x4087f954直読み，そちらは#if defined(TOPPERS_ESP32C6_WIFI)
	 *  で正しくguard済み）と同じパターンで，本ファイルだけguard漏れして
	 *  いた．native(受信OK)と同じ読み出しをして比較＝RF較正の正否を
	 *  判定するためのC6専用計装なので，C3を含む他チップでは実行しない．
	 */
	{
		/*  追記12：RF較正regi2cブロックを読み戻してRTC[16..](0x50000040)へ． */
		uint32_t *romtbl = (uint32_t *)0x4087f954U;
		uint8_t (*rd)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) =
			(uint8_t (*)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t))
				(uintptr_t)romtbl[23];
		volatile uint8_t *out = (volatile uint8_t *)0x50000040U;
		static const uint8_t blk[4] = {0x6bU, 0x6aU, 0x66U, 0x6dU};
		static const uint8_t hst[4] = {1U, 1U, 0U, 1U};
		int bi, r, o = 0;
		if (rd != NULL) {
			for (bi = 0; bi < 4; bi++) {
				for (r = 0; r < 16; r++) {
					out[o++] = rd(blk[bi], hst[bi], (uint8_t)r, 7U, 0U);
				}
			}
		}
	}
#endif /* TOPPERS_ESP32C6_WIFI */
	syslog(LOG_NOTICE, "wifi_scan: %d APs found (err=%d)",
		   (int_t)num, (int_t)err);
#ifdef TOPPERS_C3_COLD_DIAG
	/*
	 *  真cold の scan 物証（既定OFF・非回帰．evidence-c3-02）．
	 *  ★C3はUARTブリッジが無く**コンソールのopenがDUTをリセットする**
	 *  ため，真coldのscan結果はコンソールでは観測できない＝RTC STORE
	 *  マーカへ出す（判定はconsole非依存＝esptool read-mem 直読み）。
	 *    STORE4(0x600080B8) = 0x5CA0_0000 | (err&0xFF)<<8 | (AP件数&0xFF)
	 *  ★SSIDは載せない（認証情報・環境情報の混入0を維持）。
	 *  ★本アプリはC3/C5/C6共用のため必ずガード内に置く。
	 */
	(*(volatile uint32_t *)0x600080B8U) =
		0x5CA00000U | (((uint32_t)err & 0xFFU) << 8) | ((uint32_t)num & 0xFFU);
#endif /* TOPPERS_C3_COLD_DIAG */
	for (i = 0; i < num; i++) {
		syslog(LOG_NOTICE, "  [%d] %s (rssi=%d ch=%d)",
			   (int_t)i, (const char *)recs[i].ssid,
			   (int_t)recs[i].rssi, (int_t)recs[i].primary);
	}
	esp_shim_free(recs);
	syslog(LOG_NOTICE, "wifi_scan: done");

#if defined(ESP32C5_R39_SELFHANDOFF) && (ESP32C5_R39_SELFHANDOFF == 2)
	/*
	 *  【実施39・変種2】フルwifi初期化＋初回scan完走後のセルフ
	 *  ハンドオフ＝実施26のstock scan完走後ジャンプと対照が揃う。
	 *  ジャンプ前にpromiscuousを止め（RXINSTRでALL維持中のため），
	 *  遷移中のWiFi RX DMA書込みが2周目ゲストのRAMを汚す可能性を
	 *  下げる（冷間は実測RX皆無だが安全側）。
	 */
	(void) esp_wifi_set_promiscuous(false);
	(void) tslp_tsk(2000000);
	if (r39_arm != 0U) {
		r39_selfhandoff_now("post-scan");
	}
#endif /* ESP32C5_R39_SELFHANDOFF == 2 */

	/*  DIAGNOSTIC（追記19・一時）：因果検証用の再scanループ．
	 *  JTAGでRFシンセ(0x6b)のreg2/4/11/13/14をnative値に上書きした後，
	 *  後続scanでAPが出るかを見る．native側GT-REGDIFFループと同形． */
	{
		wifi_ap_record_t	rec1;
		uint16_t			n1;
		for (;;) {
			scan_done = false;
			err = esp_wifi_scan_start(NULL, false);
			while (!scan_done) {
				(void) tslp_tsk(500000);
			}
			{
				uint16_t total = 0;
				(void) esp_wifi_scan_get_ap_num(&total);
				n1 = 1;
				err = esp_wifi_scan_get_ap_records(&n1, &rec1);	/* 結果flush */
				syslog(LOG_NOTICE, "wifi_scan: RESCAN %d APs (err=%d)",
					   (int_t)total, (int_t)err);
			}
		}
	}
}

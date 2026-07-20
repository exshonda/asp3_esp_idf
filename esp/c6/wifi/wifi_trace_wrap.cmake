#
#  ESP32-C6 Wi-Fi 診断計装の -Wl,--wrap 群（★診断専用・既定 OFF）
#
#  ★evidence-c6-11：もとは esp_wifi.cmake に «診断トグル無しで» 直書きされ、
#  if(ESP32C6_WIFI)＝**機能トグル**で有効化されていた（12日間・WiFi を使う全ビルドが
#  計装込み）。本ファイルへ切り出し、target.cmake の option(ESP32C6_WIFI_TRACE …OFF)
#  配下でのみ include する＝**既定では 1バイトも入らないが、退避路は捨てない**。
#
#  ★コスト（実測）：__wrap_esf_buf_alloc(a1==2) → wifi_regsnap_capture() が
#  AGC 1024ワードの volatile 読み＋MMIO 約11本（≒1035 読）を **パケットバッファ
#  確保経路** で毎回実行し、wifi_trace_frozen でガードされず phy_init 後も走り続ける。
#  ★ON にするのは «調査» のときだけ。機能/性能の判定に使うビルドでは絶対に ON にしない。
#
#  DIAGNOSTIC (temporary，RX-enable --wrapトレース)．nmで発見した
#  libpp.a／libnet80211.a内部シンボル（公開ヘッダなし）をラップし，
#  wifi_trace.cのリングバッファへ呼出し回数・引数・戻り値を記録する．
#  調査終了後にこのブロックごと削除する．docs/wifi-shim-c6.md
#  「実施12」参照．
list(APPEND ASP3_LINK_OPTIONS
    -Wl,--wrap=wifi_hw_start
    -Wl,--wrap=wifi_hmac_init
    -Wl,--wrap=wifi_lmac_init
    -Wl,--wrap=wDev_Rxbuf_Init
    -Wl,--wrap=esf_buf_setup
    -Wl,--wrap=esf_buf_setup_static
    -Wl,--wrap=wdev_set_promis
    -Wl,--wrap=sta_rx_cb
    -Wl,--wrap=wifi_recycle_rx_pkt
    -Wl,--wrap=esf_buf_alloc
    -Wl,--wrap=esf_buf_alloc_dynamic
    -Wl,--wrap=wdev_data_init
    -Wl,--wrap=wifi_set_rx_policy
    -Wl,--wrap=adc2_wifi_acquire
    -Wl,--wrap=ieee80211_set_hmac_stop
    -Wl,--wrap=wifi_mode_set
    -Wl,--wrap=_do_wifi_start
    -Wl,--wrap=ieee80211_update_phy_country
    -Wl,--wrap=wifi_start_process
    -Wl,--wrap=wifi_set_promis_process
    -Wl,--wrap=register_chipv7_phy
    -Wl,--wrap=scan_inter_channel_timeout_process
    -Wl,--wrap=chip_v7_set_chan_ana
    -Wl,--wrap=set_channel_rfpll_freq
    -Wl,--wrap=set_rfpll_freq
    -Wl,--wrap=write_rfpll_sdm
    -Wl,--wrap=wait_rfpll_cal_end
    -Wl,--wrap=enable_agc
    -Wl,--wrap=disable_agc
    -Wl,--wrap=mac_enable_bb
    -Wl,--wrap=fe_reg_init
    -Wl,--wrap=fe_txrx_reset
    -Wl,--wrap=phy_bbpll_cal
    -Wl,--wrap=set_rxclk_en
    -Wl,--wrap=set_txclk_en
    -Wl,--wrap=write_chan_freq
    -Wl,--wrap=restart_cal
    -Wl,--wrap=i2cmst_reg_init
    -Wl,--wrap=rxiq_cal_init
    -Wl,--wrap=set_rx_gain_cal_dc_new
    -Wl,--wrap=coex_init
    -Wl,--wrap=coex_schm_process_restart
    -Wl,--wrap=coex_schm_lock
    -Wl,--wrap=coex_schm_interval_get
    #  実施38：set_rx_gain_cal_dc_new()内部呼出しの直接トレース
    -Wl,--wrap=pbus_rx_dco_cal_1step_new
    -Wl,--wrap=ram_pbus_force_mode
    -Wl,--wrap=rx_pbus_reset
    #  実施58：RXデータパス各段（sta_rx_cbが0の原因切り分け・一時的）
    -Wl,--wrap=lmacProcessRxSucData
    -Wl,--wrap=ppRxPkt
    -Wl,--wrap=wdevProcessRxSucDataAll
    -Wl,--wrap=wDev_ProcessRxSucData
    -Wl,--wrap=wDev_IndicateFrame
)

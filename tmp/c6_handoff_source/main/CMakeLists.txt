idf_component_register(SRCS "main.c" "asp3_jump.c"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES esp_wifi esp_event nvs_flash hal esp_mm esp_hal_wdt esp_driver_gpio esp_timer)
# GROUND-TRUTH DIAGNOSTIC（一時的）：FreeRTOSプリミティブをリンカ--wrapで
# 横取りし，osi呼び出し頻度を採取する（IDF本体は無改変）．検証後に削除．
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=xQueueReceive;-Wl,--wrap=xQueueGenericSend;-Wl,--wrap=xQueueSemaphoreTake" APPEND)

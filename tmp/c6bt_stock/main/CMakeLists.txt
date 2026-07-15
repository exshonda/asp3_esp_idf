idf_component_register(SRCS "app_bt.c" "asp3_jump.c"
                    PRIV_REQUIRES bt nvs_flash hal esp_mm esp_hal_wdt
                    INCLUDE_DIRS ".")

idf_component_register(SRCS "main.c" "asp3_jump.c"
                        INCLUDE_DIRS "."
                        PRIV_REQUIRES esp_wifi esp_event nvs_flash hal esp_mm esp_hal_wdt esp_driver_gpio)

idf_component_register(SRCS "wifi_dap.c"
                       INCLUDE_DIRS "include"
                       REQUIRES cmsis_dap debug_engine
                                esp_wifi esp_netif esp_event nvs_flash)

idf_component_register(
    SRCS
        "main.c"
        "config/config_manager.c"
        "wifi/wifi_manager.c"
        "web/web_ui.c"
        "web/web_server.c"
        "system/system_manager.c"
        # USB/RNDIS + network(NAT) sources are added in later phases

    EMBED_FILES
        "web/index.html"

    INCLUDE_DIRS
        "."
        "config"
        "wifi"
        "web"
        "network"
        "system"

    REQUIRES
        esp_wifi
        esp_netif
        nvs_flash
        esp_http_server
        esp_event
        freertos
        # tinyusb / lwip added when USB/RNDIS + NAT modules are integrated
)

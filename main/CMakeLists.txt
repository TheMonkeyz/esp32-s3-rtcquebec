idf_component_register(
    SRCS "main.c" "ui.c" "app_text.c" "rtc_api.c" "favs.c" "departures.c"
    INCLUDE_DIRS "."
    REQUIRES board forge_core forge_net forge_ota forge_lvgl lvgl json esp_driver_gpio esp_wifi esp_http_client mbedtls nvs_flash
    EMBED_FILES "montserrat.ttf"
    EMBED_TXTFILES "web/index.html")

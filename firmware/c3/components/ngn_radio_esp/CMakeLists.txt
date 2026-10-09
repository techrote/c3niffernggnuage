idf_component_register(
    SRCS "ngn_radio_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES ngn_core esp_common
    PRIV_REQUIRES esp_wifi esp_event esp_netif esp_timer nvs_flash freertos
)

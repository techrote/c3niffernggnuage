idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES ngn_core ngn_radio_esp esp_system log esp_timer esp_hw_support freertos
    PRIV_REQUIRES ngn_ble_esp
)

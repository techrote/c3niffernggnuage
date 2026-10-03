idf_component_register(
    SRCS "ngn_ble_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES ngn_core bt nvs_flash esp_timer mbedtls freertos log
)

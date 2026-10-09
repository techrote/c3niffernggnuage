idf_component_register(
    SRCS "ngn_csi_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES ngn_core esp_common
    PRIV_REQUIRES esp_wifi esp_timer freertos
)

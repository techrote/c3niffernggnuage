idf_component_register(
    SRCS "ngn_oled_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES ngn_display esp_driver_i2c esp_driver_gpio
)

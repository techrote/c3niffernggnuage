#ifndef NGN_OLED_ESP_H
#define NGN_OLED_ESP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "ngn_display.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*ngn_oled_write_fn)(void *write_context,
                                       const uint8_t *bytes,
                                       size_t length);

typedef struct {
    const char *name;
    esp_err_t (*start)(void *controller_context,
                       ngn_oled_write_fn write_fn,
                       void *write_context,
                       uint16_t width,
                       uint16_t height);
    esp_err_t (*present)(void *controller_context,
                         ngn_oled_write_fn write_fn,
                         void *write_context,
                         const ngn_framebuffer_t *framebuffer);
    void (*stop)(void *controller_context,
                 ngn_oled_write_fn write_fn,
                 void *write_context);
} ngn_oled_controller_ops_t;

typedef struct {
    bool enabled;
    uint16_t width;
    uint16_t height;

    int i2c_port;
    int sda_gpio;
    int scl_gpio;
    uint16_t i2c_address;
    uint32_t scl_speed_hz;
    bool enable_internal_pullups;

    bool reset_enabled;
    int reset_gpio;
    bool reset_active_low;

    bool button_enabled;
    int button_gpio;
    bool button_active_low;
    bool button_enable_pullup;
    bool button_enable_pulldown;

    const ngn_oled_controller_ops_t *controller;
    void *controller_context;
} ngn_oled_esp_config_t;

typedef struct {
    ngn_oled_esp_config_t config;
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t device_handle;
    bool initialized;
} ngn_oled_esp_t;

/* Adapter storage must be zero-initialized before the first init call. */
bool ngn_oled_esp_config_is_complete(const ngn_oled_esp_config_t *config);
esp_err_t ngn_oled_esp_init(ngn_oled_esp_t *adapter,
                            const ngn_oled_esp_config_t *config);
esp_err_t ngn_oled_esp_present(ngn_oled_esp_t *adapter,
                               const ngn_framebuffer_t *framebuffer);
esp_err_t ngn_oled_esp_set_reset(ngn_oled_esp_t *adapter, bool asserted);
esp_err_t ngn_oled_esp_read_button(ngn_oled_esp_t *adapter, bool *pressed);
void ngn_oled_esp_deinit(ngn_oled_esp_t *adapter);

#ifdef __cplusplus
}
#endif

#endif

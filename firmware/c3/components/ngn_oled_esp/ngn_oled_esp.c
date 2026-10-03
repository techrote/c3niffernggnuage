#include "ngn_oled_esp.h"

#include <string.h>

#include "driver/gpio.h"

static bool gpio_number_configured(int gpio)
{
    return gpio >= 0 && gpio < GPIO_NUM_MAX;
}

bool ngn_oled_esp_config_is_complete(const ngn_oled_esp_config_t *config)
{
    return config != NULL && config->enabled && config->width > 0u && config->height > 0u &&
           config->i2c_port >= 0 && gpio_number_configured(config->sda_gpio) &&
           gpio_number_configured(config->scl_gpio) && config->i2c_address > 0u &&
           config->i2c_address <= 0x7fu && config->scl_speed_hz > 0u &&
           (!config->reset_enabled || gpio_number_configured(config->reset_gpio)) &&
           (!config->button_enabled || gpio_number_configured(config->button_gpio)) &&
           (!config->button_enabled ||
            !(config->button_enable_pullup && config->button_enable_pulldown)) &&
           config->controller != NULL && config->controller->name != NULL &&
           config->controller->name[0] != '\0' && config->controller->present != NULL;
}

static esp_err_t configure_output_gpio(int gpio, bool initial_high)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level((gpio_num_t)gpio, initial_high ? 1u : 0u);
}

static esp_err_t configure_button_gpio(const ngn_oled_esp_config_t *config)
{
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << (uint32_t)config->button_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->button_enable_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->button_enable_pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&button_config);
}

static esp_err_t transport_write(void *write_context, const uint8_t *bytes, size_t length)
{
    ngn_oled_esp_t *adapter = (ngn_oled_esp_t *)write_context;

    if (adapter == NULL || !adapter->initialized || adapter->device_handle == NULL ||
        bytes == NULL || length == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit(adapter->device_handle, bytes, length, -1);
}

esp_err_t ngn_oled_esp_init(ngn_oled_esp_t *adapter,
                            const ngn_oled_esp_config_t *config)
{
    i2c_master_bus_config_t bus_config;
    i2c_device_config_t device_config;
    esp_err_t err;

    if (adapter == NULL || !ngn_oled_esp_config_is_complete(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (adapter->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->config = *config;

    memset(&bus_config, 0, sizeof(bus_config));
    bus_config.i2c_port = config->i2c_port;
    bus_config.sda_io_num = config->sda_gpio;
    bus_config.scl_io_num = config->scl_gpio;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7u;
    bus_config.flags.enable_internal_pullup = config->enable_internal_pullups;

    err = i2c_new_master_bus(&bus_config, &adapter->bus_handle);
    if (err != ESP_OK) {
        goto fail;
    }

    memset(&device_config, 0, sizeof(device_config));
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = config->i2c_address;
    device_config.scl_speed_hz = config->scl_speed_hz;

    err = i2c_master_bus_add_device(adapter->bus_handle, &device_config, &adapter->device_handle);
    if (err != ESP_OK) {
        goto fail;
    }

    if (config->reset_enabled) {
        const bool inactive_high = config->reset_active_low;
        err = configure_output_gpio(config->reset_gpio, inactive_high);
        if (err != ESP_OK) {
            goto fail;
        }
    }

    if (config->button_enabled) {
        err = configure_button_gpio(config);
        if (err != ESP_OK) {
            goto fail;
        }
    }

    adapter->initialized = true;
    if (config->controller->start != NULL) {
        err = config->controller->start(config->controller_context,
                                        transport_write,
                                        adapter,
                                        config->width,
                                        config->height);
        if (err != ESP_OK) {
            ngn_oled_esp_deinit(adapter);
            return err;
        }
    }

    return ESP_OK;

fail:
    if (adapter->device_handle != NULL) {
        (void)i2c_master_bus_rm_device(adapter->device_handle);
    }
    if (adapter->bus_handle != NULL) {
        (void)i2c_del_master_bus(adapter->bus_handle);
    }
    memset(adapter, 0, sizeof(*adapter));
    return err;
}

esp_err_t ngn_oled_esp_present(ngn_oled_esp_t *adapter,
                               const ngn_framebuffer_t *framebuffer)
{
    if (adapter == NULL || !adapter->initialized || framebuffer == NULL ||
        framebuffer->width != adapter->config.width ||
        framebuffer->height != adapter->config.height ||
        adapter->config.controller == NULL || adapter->config.controller->present == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return adapter->config.controller->present(adapter->config.controller_context,
                                               transport_write,
                                               adapter,
                                               framebuffer);
}

esp_err_t ngn_oled_esp_set_reset(ngn_oled_esp_t *adapter, bool asserted)
{
    bool high;

    if (adapter == NULL || !adapter->initialized || !adapter->config.reset_enabled ||
        !gpio_number_configured(adapter->config.reset_gpio)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    high = adapter->config.reset_active_low ? !asserted : asserted;
    return gpio_set_level((gpio_num_t)adapter->config.reset_gpio, high ? 1u : 0u);
}

esp_err_t ngn_oled_esp_read_button(ngn_oled_esp_t *adapter, bool *pressed)
{
    bool high;

    if (adapter == NULL || pressed == NULL || !adapter->initialized ||
        !adapter->config.button_enabled ||
        !gpio_number_configured(adapter->config.button_gpio)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    high = gpio_get_level((gpio_num_t)adapter->config.button_gpio) != 0;
    *pressed = adapter->config.button_active_low ? !high : high;
    return ESP_OK;
}

void ngn_oled_esp_deinit(ngn_oled_esp_t *adapter)
{
    if (adapter == NULL) {
        return;
    }

    if (adapter->initialized && adapter->config.controller != NULL &&
        adapter->config.controller->stop != NULL) {
        adapter->config.controller->stop(adapter->config.controller_context,
                                         transport_write,
                                         adapter);
    }

    if (adapter->device_handle != NULL) {
        (void)i2c_master_bus_rm_device(adapter->device_handle);
    }
    if (adapter->bus_handle != NULL) {
        (void)i2c_del_master_bus(adapter->bus_handle);
    }
    memset(adapter, 0, sizeof(*adapter));
}

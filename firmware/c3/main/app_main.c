#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_system.h"

#include "ngn_node.h"
#include "ngn_version.h"

static const char *TAG = "nggunage";

static ngn_node_id_t configured_node_id(void)
{
#if CONFIG_NGN_NODE_ROLE_A
    return NGN_NODE_A;
#elif CONFIG_NGN_NODE_ROLE_B
    return NGN_NODE_B;
#elif CONFIG_NGN_NODE_ROLE_C
    return NGN_NODE_C;
#else
    return NGN_NODE_UNCONFIGURED;
#endif
}

void app_main(void)
{
    const ngn_node_id_t node_id = configured_node_id();

    ESP_LOGI(TAG, "C3niffer NGGUNAGE foundation");
    ESP_LOGI(TAG,
             "firmware=%s protocol=%u esp-idf=%s",
             NGN_FIRMWARE_VERSION,
             (unsigned)NGN_PROTOCOL_VERSION,
             esp_get_idf_version());
    ESP_LOGI(TAG,
             "node=%s board_profile=%s",
             ngn_node_id_name(node_id),
             CONFIG_NGN_BOARD_PROFILE);

    if (!ngn_node_id_is_valid(node_id)) {
        ESP_LOGW(TAG,
                 "logical node role is unconfigured; select A, B or C before multi-node use");
    }

    ESP_LOGI(TAG,
             "foundation ready; Wi-Fi CSI, ESP-NOW, BLE, fusion and OLED logic are not initialized");
}

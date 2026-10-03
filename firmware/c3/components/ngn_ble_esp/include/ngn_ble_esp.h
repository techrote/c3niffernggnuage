#ifndef NGN_BLE_ESP_H
#define NGN_BLE_ESP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "ngn_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ngn_node_id_t node_id;
    const uint8_t *session_nonce;
    size_t session_nonce_len;
    ngn_ble_tracker_config_t tracker;
    ngn_ble_event_sink_fn sink;
    void *sink_ctx;
} ngn_ble_esp_config_t;

typedef struct {
    uint32_t scan_reports;
    uint32_t queue_drops;
    uint32_t parse_drops;
    uint32_t key_failures;
    uint32_t tracker_failures;
} ngn_ble_esp_stats_t;

/*
 * Starts the singleton passive-observer subsystem. The caller supplies the
 * session nonce; this component never invents or persists one. NGN-002 may
 * later provide the coordinated nonce without changing this component's data
 * or transport boundary.
 */
esp_err_t ngn_ble_esp_start(const ngn_ble_esp_config_t *config);

bool ngn_ble_esp_is_started(void);
void ngn_ble_esp_get_stats(ngn_ble_esp_stats_t *out_stats);

#ifdef __cplusplus
}
#endif

#endif

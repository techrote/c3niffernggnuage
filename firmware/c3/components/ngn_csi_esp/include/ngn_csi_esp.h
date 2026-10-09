#ifndef NGN_CSI_ESP_H
#define NGN_CSI_ESP_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "ngn_csi.h"

typedef struct {
    uint8_t channel;
    uint16_t raw_queue_depth;
    uint16_t packet_queue_depth;
    uint16_t attribution_window_ms;
} ngn_csi_esp_config_t;

typedef struct {
    ngn_csi_capture_stats_t capture;
    uint32_t callback_unknown_source;
    uint32_t processed;
    uint32_t decode_invalid_argument;
    uint32_t decode_invalid_length;
    uint32_t decode_rx_error;
    uint32_t decode_channel_mismatch;
    uint32_t unattributed;
    uint32_t packet_queue_drops;
    uint32_t emitted;
} ngn_csi_esp_stats_t;

/* Requires NGN's Wi-Fi station to be initialized and started first. Enables
 * ESP-IDF CSI and promiscuous receive on the already selected fixed channel.
 * No AP association or IP interface is created here. */
esp_err_t ngn_csi_esp_start(const ngn_csi_esp_config_t *config);

bool ngn_csi_esp_active(void);

/* Called from the normal ngn_radio event sink, never from Wi-Fi callbacks. */
bool ngn_csi_esp_bind(ngn_node_id_t node,
                      const uint8_t mac[NGN_TRANSPORT_MAC_SIZE]);
bool ngn_csi_esp_set_session(uint64_t session_id);
bool ngn_csi_esp_note_probe(ngn_node_id_t source,
                            uint64_t session_id,
                            uint32_t epoch,
                            uint32_t sequence,
                            uint64_t received_ms);

/* Nonblocking packet output for NGN-005/008 or the current diagnostic drain. */
bool ngn_csi_esp_receive(ngn_csi_packet_t *packet);
void ngn_csi_esp_get_stats(ngn_csi_esp_stats_t *stats);

#endif

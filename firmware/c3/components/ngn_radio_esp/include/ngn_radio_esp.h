#ifndef NGN_RADIO_ESP_H
#define NGN_RADIO_ESP_H

#include <stdint.h>

#include "esp_err.h"
#include "ngn_transport.h"

#define NGN_RADIO_ESP_MAX_QUEUE_DEPTH 64u
#define NGN_RADIO_ESP_WORKER_STACK_BYTES 4096u
#define NGN_RADIO_ESP_STALL_AFTER_MS 1000u

typedef struct {
    uint8_t channel; /* Fixed 2.4 GHz channel, 1..11. */
    uint16_t rx_queue_depth;     /* Each queue depth is 1..64 records. */
    uint16_t tx_queue_depth;
    uint16_t status_queue_depth;
} ngn_radio_esp_config_t;

/*
 * Start the sole ESP-NOW/Wi-Fi owner once during serialized application startup.
 * Existing Wi-Fi ownership is rejected, not reconfigured. The adapter creates
 * no IP interface, AP association, CSI callback or BLE scan. It preserves NVS
 * contents and returns initialization errors without an erase-and-retry path.
 *
 * On success, the copied transport vtable and station MAC remain valid for the
 * firmware lifetime. All vtable calls are nonblocking; send copies its frame.
 * A lower-priority worker serializes actual driver sends, discards expired
 * queued frames, and publishes bounded completion statuses. A full status
 * queue drops its newest status without preventing the next transmission.
 * set_session atomically changes which nonzero session may claim a new send.
 * Obsolete queued frames produce CANCELED/tx_canceled; an already claimed send
 * may complete. Changing session does not block or drain queues in the caller.
 *
 * A missing completion increments tx_stalled once after one second, but never
 * releases that in-flight token: a late callback cannot complete another send.
 * Failure of the driver to call back therefore stops further driver sends;
 * caller polling, reception and queue/drop statistics remain available.
 *
 * On startup failure, owned queues, Wi-Fi, ESP-NOW, the default event loop and
 * newly initialized NVS are released. The SDK's process-wide esp_netif bootstrap
 * remains initialized because v5.5.5 provides no supported deinitialization.
 * No shutdown/restart API is provided by this boot-lifetime adapter.
 */
esp_err_t ngn_radio_esp_start(const ngn_radio_esp_config_t *config,
                             ngn_transport_t *out_transport,
                             uint8_t out_station_mac[NGN_TRANSPORT_MAC_SIZE]);

#endif

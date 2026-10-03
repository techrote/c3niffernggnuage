#ifndef NGN_BLE_H
#define NGN_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ngn_node.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NGN_BLE_ADDRESS_LEN 6u
#define NGN_BLE_SESSION_NONCE_MIN_LEN 8u
#define NGN_BLE_SESSION_NONCE_MAX_LEN 32u
#define NGN_BLE_MAX_SERVICE_UUID16 4u
#define NGN_BLE_MAX_TRACKS 32u
#define NGN_BLE_DIGEST_LEN 32u

typedef uint64_t ngn_ble_track_key_t;

typedef enum {
    NGN_BLE_TRACK_NEW = 0,
    NGN_BLE_TRACK_ACTIVE = 1,
    NGN_BLE_TRACK_AGING = 2,
    NGN_BLE_TRACK_EXPIRED = 3
} ngn_ble_track_state_t;

typedef enum {
    NGN_BLE_EXPIRE_NONE = 0,
    NGN_BLE_EXPIRE_TIMEOUT = 1,
    NGN_BLE_EXPIRE_EVICTED = 2
} ngn_ble_expire_reason_t;

typedef struct {
    uint64_t timestamp_ms;
    ngn_node_id_t receiver;
    uint8_t address_type;
    uint8_t address[NGN_BLE_ADDRESS_LEN];
    int8_t rssi_dbm;
    uint8_t advertisement_type;
    bool flags_present;
    uint8_t flags;
    bool company_id_present;
    uint16_t company_id;
    uint16_t service_uuid16[NGN_BLE_MAX_SERVICE_UUID16];
    uint8_t service_uuid16_count;
    uint8_t service_uuid32_count;
    uint8_t service_uuid128_count;
    uint16_t payload_length;
    uint32_t payload_signature;
} ngn_ble_observation_t;

typedef struct {
    size_t max_tracks;
    uint64_t aging_after_ms;
    uint64_t expire_after_ms;
    uint8_t rssi_ewma_divisor;
} ngn_ble_tracker_config_t;

typedef struct {
    ngn_ble_track_key_t key;
    ngn_node_id_t receiver;
    uint8_t address_type;
    ngn_ble_track_state_t state;
    ngn_ble_expire_reason_t expire_reason;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint32_t observation_count;
    int8_t latest_rssi_dbm;
    int16_t filtered_rssi_q8;
    uint8_t advertisement_type;
    bool flags_present;
    uint8_t flags;
    bool company_id_present;
    uint16_t company_id;
    uint16_t service_uuid16[NGN_BLE_MAX_SERVICE_UUID16];
    uint8_t service_uuid16_count;
    uint8_t service_uuid32_count;
    uint8_t service_uuid128_count;
    uint16_t payload_length;
} ngn_ble_track_event_t;

typedef void (*ngn_ble_event_sink_fn)(void *ctx,
                                      const ngn_ble_track_event_t *event);

typedef bool (*ngn_ble_digest_fn)(void *ctx,
                                  const uint8_t *data,
                                  size_t data_len,
                                  uint8_t digest[NGN_BLE_DIGEST_LEN]);

typedef struct {
    bool occupied;
    ngn_ble_track_key_t key;
    ngn_node_id_t receiver;
    uint8_t address_type;
    ngn_ble_track_state_t state;
    uint64_t first_seen_ms;
    uint64_t last_seen_ms;
    uint32_t observation_count;
    int8_t latest_rssi_dbm;
    int16_t filtered_rssi_q8;
    uint8_t advertisement_type;
    bool flags_present;
    uint8_t flags;
    bool company_id_present;
    uint16_t company_id;
    uint16_t service_uuid16[NGN_BLE_MAX_SERVICE_UUID16];
    uint8_t service_uuid16_count;
    uint8_t service_uuid32_count;
    uint8_t service_uuid128_count;
    uint16_t payload_length;
} ngn_ble_track_t;

typedef struct {
    ngn_ble_tracker_config_t config;
    ngn_ble_track_t tracks[NGN_BLE_MAX_TRACKS];
    ngn_ble_event_sink_fn sink;
    void *sink_ctx;
    size_t track_count;
} ngn_ble_tracker_t;

void ngn_ble_tracker_default_config(ngn_ble_tracker_config_t *config);

bool ngn_ble_observation_normalize(ngn_ble_observation_t *observation);

uint32_t ngn_ble_payload_signature(const uint8_t *payload, size_t payload_len);

bool ngn_ble_derive_session_key(const uint8_t *session_nonce,
                                size_t session_nonce_len,
                                const ngn_ble_observation_t *observation,
                                ngn_ble_digest_fn digest_fn,
                                void *digest_ctx,
                                ngn_ble_track_key_t *out_key);

bool ngn_ble_tracker_init(ngn_ble_tracker_t *tracker,
                          const ngn_ble_tracker_config_t *config,
                          ngn_ble_event_sink_fn sink,
                          void *sink_ctx);

void ngn_ble_tracker_reset(ngn_ble_tracker_t *tracker);

bool ngn_ble_tracker_observe(ngn_ble_tracker_t *tracker,
                             ngn_ble_track_key_t key,
                             const ngn_ble_observation_t *observation);

void ngn_ble_tracker_tick(ngn_ble_tracker_t *tracker, uint64_t now_ms);

size_t ngn_ble_tracker_count(const ngn_ble_tracker_t *tracker);

#ifdef __cplusplus
}
#endif

#endif

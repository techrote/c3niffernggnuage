#include <stdio.h>
#include <string.h>

#include "ngn_ble.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return false;                                                       \
        }                                                                       \
    } while (0)

typedef struct {
    ngn_ble_track_event_t events[64];
    size_t count;
} capture_t;

static void capture_sink(void *ctx, const ngn_ble_track_event_t *event)
{
    capture_t *capture = ctx;
    if (capture->count < 64u) {
        capture->events[capture->count++] = *event;
    }
}

static bool test_digest(void *ctx,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t digest[NGN_BLE_DIGEST_LEN])
{
    uint64_t hash = UINT64_C(1469598103934665603);
    size_t i;
    (void)ctx;

    for (i = 0u; i < data_len; ++i) {
        hash ^= data[i];
        hash *= UINT64_C(1099511628211);
    }
    for (i = 0u; i < NGN_BLE_DIGEST_LEN; ++i) {
        digest[i] = (uint8_t)((hash >> ((i % 8u) * 8u)) & 0xffu);
        hash = hash * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    }
    return true;
}

static ngn_ble_observation_t observation(uint64_t timestamp_ms,
                                         ngn_node_id_t receiver,
                                         uint8_t address_last,
                                         int8_t rssi_dbm)
{
    ngn_ble_observation_t obs;
    memset(&obs, 0, sizeof(obs));
    obs.timestamp_ms = timestamp_ms;
    obs.receiver = receiver;
    obs.address_type = 1u;
    obs.address[0] = 0xa1u;
    obs.address[1] = 0xb2u;
    obs.address[2] = 0xc3u;
    obs.address[3] = 0xd4u;
    obs.address[4] = 0xe5u;
    obs.address[5] = address_last;
    obs.rssi_dbm = rssi_dbm;
    obs.advertisement_type = 0u;
    obs.flags_present = true;
    obs.flags = 0x06u;
    obs.company_id_present = true;
    obs.company_id = 0x004cu;
    obs.service_uuid16[0] = 0x180fu;
    obs.service_uuid16[1] = 0x180du;
    obs.service_uuid16_count = 2u;
    obs.payload_length = 21u;
    obs.payload_signature = 0x11223344u;
    return obs;
}

static bool test_crc_and_normalization(void)
{
    static const uint8_t payload[] = "123456789";
    ngn_ble_observation_t obs = observation(1u, NGN_NODE_A, 0xf6u, -70);

    CHECK(ngn_ble_payload_signature(payload, 9u) == 0xcbf43926u);
    CHECK(ngn_ble_observation_normalize(&obs));
    CHECK(obs.service_uuid16_count == 2u);
    CHECK(obs.service_uuid16[0] == 0x180du);
    CHECK(obs.service_uuid16[1] == 0x180fu);

    obs.service_uuid16[2] = 0x180fu;
    obs.service_uuid16_count = 3u;
    CHECK(ngn_ble_observation_normalize(&obs));
    CHECK(obs.service_uuid16_count == 2u);
    return true;
}

static bool test_session_key_invariants(void)
{
    static const uint8_t nonce_a[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const uint8_t nonce_b[16] = {
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    ngn_ble_observation_t a = observation(100u, NGN_NODE_A, 0xf6u, -60);
    ngn_ble_observation_t b = a;
    ngn_ble_track_key_t key_a;
    ngn_ble_track_key_t key_b;
    ngn_ble_track_key_t key_c;
    ngn_ble_track_key_t key_d;

    b.receiver = NGN_NODE_C;
    b.service_uuid16[0] = 0x180du;
    b.service_uuid16[1] = 0x180fu;

    CHECK(ngn_ble_derive_session_key(nonce_a, sizeof(nonce_a), &a,
                                     test_digest, NULL, &key_a));
    CHECK(ngn_ble_derive_session_key(nonce_a, sizeof(nonce_a), &b,
                                     test_digest, NULL, &key_b));
    CHECK(key_a == key_b);

    CHECK(ngn_ble_derive_session_key(nonce_b, sizeof(nonce_b), &a,
                                     test_digest, NULL, &key_c));
    CHECK(key_c != key_a);

    b = a;
    b.address[5] ^= 0x01u;
    CHECK(ngn_ble_derive_session_key(nonce_a, sizeof(nonce_a), &b,
                                     test_digest, NULL, &key_d));
    CHECK(key_d != key_a);

    b = a;
    b.payload_signature ^= 0x01000000u;
    CHECK(ngn_ble_derive_session_key(nonce_a, sizeof(nonce_a), &b,
                                     test_digest, NULL, &key_d));
    CHECK(key_d != key_a);

    CHECK(!ngn_ble_derive_session_key(nonce_a, 4u, &a,
                                      test_digest, NULL, &key_d));
    return true;
}

static bool test_lifecycle_duplicates_missing_and_rssi(void)
{
    ngn_ble_tracker_t tracker;
    ngn_ble_tracker_config_t config = {
        .max_tracks = 4u,
        .aging_after_ms = 1000u,
        .expire_after_ms = 3000u,
        .rssi_ewma_divisor = 2u,
    };
    capture_t capture = {0};
    ngn_ble_observation_t obs = observation(100u, NGN_NODE_B, 0xf6u, -80);
    const ngn_ble_track_key_t key = UINT64_C(0x1234);

    CHECK(ngn_ble_tracker_init(&tracker, &config, capture_sink, &capture));
    CHECK(ngn_ble_tracker_observe(&tracker, key, &obs));
    CHECK(capture.count == 1u);
    CHECK(capture.events[0].state == NGN_BLE_TRACK_NEW);
    CHECK(capture.events[0].latest_rssi_dbm == -80);
    CHECK(capture.events[0].filtered_rssi_q8 == -80 * 256);
    CHECK(ngn_ble_tracker_count(&tracker) == 1u);

    obs.timestamp_ms = 200u;
    obs.rssi_dbm = -60;
    CHECK(ngn_ble_tracker_observe(&tracker, key, &obs));
    CHECK(capture.count == 2u);
    CHECK(capture.events[1].state == NGN_BLE_TRACK_ACTIVE);
    CHECK(capture.events[1].observation_count == 2u);
    CHECK(capture.events[1].filtered_rssi_q8 == -70 * 256);
    CHECK(ngn_ble_tracker_count(&tracker) == 1u);

    ngn_ble_tracker_tick(&tracker, 1199u);
    CHECK(capture.count == 2u);
    ngn_ble_tracker_tick(&tracker, 1200u);
    CHECK(capture.count == 3u);
    CHECK(capture.events[2].state == NGN_BLE_TRACK_AGING);

    obs.timestamp_ms = 1300u;
    obs.rssi_dbm = -50;
    CHECK(ngn_ble_tracker_observe(&tracker, key, &obs));
    CHECK(capture.events[3].state == NGN_BLE_TRACK_ACTIVE);
    CHECK(capture.events[3].filtered_rssi_q8 == -60 * 256);

    ngn_ble_tracker_tick(&tracker, 4299u);
    CHECK(ngn_ble_tracker_count(&tracker) == 1u);
    ngn_ble_tracker_tick(&tracker, 4300u);
    CHECK(ngn_ble_tracker_count(&tracker) == 0u);
    CHECK(capture.events[capture.count - 1u].state == NGN_BLE_TRACK_EXPIRED);
    CHECK(capture.events[capture.count - 1u].expire_reason == NGN_BLE_EXPIRE_TIMEOUT);
    return true;
}

static bool test_bounded_eviction(void)
{
    ngn_ble_tracker_t tracker;
    ngn_ble_tracker_config_t config = {
        .max_tracks = 2u,
        .aging_after_ms = 1000u,
        .expire_after_ms = 10000u,
        .rssi_ewma_divisor = 4u,
    };
    capture_t capture = {0};
    ngn_ble_observation_t obs = observation(100u, NGN_NODE_A, 0x01u, -70);

    CHECK(ngn_ble_tracker_init(&tracker, &config, capture_sink, &capture));
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(10), &obs));
    obs.timestamp_ms = 200u;
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(20), &obs));
    CHECK(ngn_ble_tracker_count(&tracker) == 2u);

    obs.timestamp_ms = 300u;
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(30), &obs));
    CHECK(ngn_ble_tracker_count(&tracker) == 2u);
    CHECK(capture.count == 4u);
    CHECK(capture.events[2].key == UINT64_C(10));
    CHECK(capture.events[2].state == NGN_BLE_TRACK_EXPIRED);
    CHECK(capture.events[2].expire_reason == NGN_BLE_EXPIRE_EVICTED);
    CHECK(capture.events[3].key == UINT64_C(30));
    CHECK(capture.events[3].state == NGN_BLE_TRACK_NEW);
    return true;
}

static bool test_deterministic_eviction_tie(void)
{
    ngn_ble_tracker_t tracker;
    ngn_ble_tracker_config_t config = {
        .max_tracks = 2u,
        .aging_after_ms = 1000u,
        .expire_after_ms = 10000u,
        .rssi_ewma_divisor = 4u,
    };
    capture_t capture = {0};
    ngn_ble_observation_t obs = observation(100u, NGN_NODE_A, 0x01u, -70);

    CHECK(ngn_ble_tracker_init(&tracker, &config, capture_sink, &capture));
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(20), &obs));
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(10), &obs));
    obs.timestamp_ms = 101u;
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(30), &obs));

    CHECK(capture.count == 4u);
    CHECK(capture.events[2].key == UINT64_C(10));
    CHECK(capture.events[2].state == NGN_BLE_TRACK_EXPIRED);
    CHECK(capture.events[2].expire_reason == NGN_BLE_EXPIRE_EVICTED);
    return true;
}

static bool test_out_of_order_rejected(void)
{
    ngn_ble_tracker_t tracker;
    ngn_ble_tracker_config_t config;
    capture_t capture = {0};
    ngn_ble_observation_t obs = observation(500u, NGN_NODE_C, 0x7fu, -55);

    ngn_ble_tracker_default_config(&config);
    CHECK(ngn_ble_tracker_init(&tracker, &config, capture_sink, &capture));
    CHECK(ngn_ble_tracker_observe(&tracker, UINT64_C(99), &obs));
    obs.timestamp_ms = 499u;
    CHECK(!ngn_ble_tracker_observe(&tracker, UINT64_C(99), &obs));
    CHECK(capture.count == 1u);
    return true;
}

int main(void)
{
    if (!test_crc_and_normalization() ||
        !test_session_key_invariants() ||
        !test_lifecycle_duplicates_missing_and_rssi() ||
        !test_bounded_eviction() ||
        !test_deterministic_eviction_tie() ||
        !test_out_of_order_rejected()) {
        return 1;
    }

    puts("ngn_ble: ok");
    return 0;
}

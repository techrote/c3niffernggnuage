#include "ngn_ble.h"

#include <string.h>

#define NGN_BLE_KEY_FORMAT_VERSION 1u

static void sort_and_deduplicate_uuid16(ngn_ble_observation_t *observation)
{
    uint8_t i;
    uint8_t j;
    uint8_t write_index = 0;

    for (i = 1; i < observation->service_uuid16_count; ++i) {
        const uint16_t value = observation->service_uuid16[i];
        j = i;
        while (j > 0u && observation->service_uuid16[j - 1u] > value) {
            observation->service_uuid16[j] = observation->service_uuid16[j - 1u];
            --j;
        }
        observation->service_uuid16[j] = value;
    }

    for (i = 0; i < observation->service_uuid16_count; ++i) {
        if (write_index == 0u ||
            observation->service_uuid16[i] != observation->service_uuid16[write_index - 1u]) {
            observation->service_uuid16[write_index++] = observation->service_uuid16[i];
        }
    }
    observation->service_uuid16_count = write_index;
}

void ngn_ble_tracker_default_config(ngn_ble_tracker_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->max_tracks = 24u;
    config->aging_after_ms = 5000u;
    config->expire_after_ms = 15000u;
    config->rssi_ewma_divisor = 4u;
}

bool ngn_ble_observation_normalize(ngn_ble_observation_t *observation)
{
    if (observation == NULL ||
        !ngn_node_id_is_valid(observation->receiver) ||
        observation->service_uuid16_count > NGN_BLE_MAX_SERVICE_UUID16) {
        return false;
    }

    sort_and_deduplicate_uuid16(observation);
    return true;
}

uint32_t ngn_ble_payload_signature(const uint8_t *payload, size_t payload_len)
{
    uint32_t crc = 0xffffffffu;
    size_t i;
    unsigned bit;

    if (payload == NULL && payload_len != 0u) {
        return 0u;
    }

    for (i = 0; i < payload_len; ++i) {
        crc ^= payload[i];
        for (bit = 0; bit < 8u; ++bit) {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }

    return ~crc;
}

static void append_u16_le(uint8_t *buffer, size_t *offset, uint16_t value)
{
    buffer[(*offset)++] = (uint8_t)(value & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 8u) & 0xffu);
}

static void append_u32_le(uint8_t *buffer, size_t *offset, uint32_t value)
{
    buffer[(*offset)++] = (uint8_t)(value & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 8u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 16u) & 0xffu);
    buffer[(*offset)++] = (uint8_t)((value >> 24u) & 0xffu);
}

bool ngn_ble_derive_session_key(const uint8_t *session_nonce,
                                size_t session_nonce_len,
                                const ngn_ble_observation_t *observation,
                                ngn_ble_digest_fn digest_fn,
                                void *digest_ctx,
                                ngn_ble_track_key_t *out_key)
{
    uint8_t material[64];
    uint8_t digest[NGN_BLE_DIGEST_LEN];
    uint16_t sorted_uuids[NGN_BLE_MAX_SERVICE_UUID16];
    size_t offset = 0u;
    size_t i;
    ngn_ble_track_key_t key = 0u;

    if (session_nonce == NULL ||
        session_nonce_len < NGN_BLE_SESSION_NONCE_MIN_LEN ||
        session_nonce_len > NGN_BLE_SESSION_NONCE_MAX_LEN ||
        observation == NULL ||
        observation->service_uuid16_count > NGN_BLE_MAX_SERVICE_UUID16 ||
        digest_fn == NULL ||
        out_key == NULL) {
        return false;
    }

    memcpy(sorted_uuids, observation->service_uuid16,
           observation->service_uuid16_count * sizeof(sorted_uuids[0]));
    for (i = 1u; i < observation->service_uuid16_count; ++i) {
        size_t j = i;
        const uint16_t value = sorted_uuids[i];
        while (j > 0u && sorted_uuids[j - 1u] > value) {
            sorted_uuids[j] = sorted_uuids[j - 1u];
            --j;
        }
        sorted_uuids[j] = value;
    }

    material[offset++] = NGN_BLE_KEY_FORMAT_VERSION;
    material[offset++] = (uint8_t)session_nonce_len;
    memcpy(&material[offset], session_nonce, session_nonce_len);
    offset += session_nonce_len;
    material[offset++] = observation->address_type;
    memcpy(&material[offset], observation->address, NGN_BLE_ADDRESS_LEN);
    offset += NGN_BLE_ADDRESS_LEN;
    append_u32_le(material, &offset, observation->payload_signature);
    material[offset++] = observation->company_id_present ? 1u : 0u;
    if (observation->company_id_present) {
        append_u16_le(material, &offset, observation->company_id);
    }
    material[offset++] = observation->service_uuid16_count;
    for (i = 0u; i < observation->service_uuid16_count; ++i) {
        append_u16_le(material, &offset, sorted_uuids[i]);
    }

    if (!digest_fn(digest_ctx, material, offset, digest)) {
        memset(material, 0, sizeof(material));
        return false;
    }

    for (i = 0u; i < sizeof(key); ++i) {
        key = (key << 8u) | digest[i];
    }

    memset(material, 0, sizeof(material));
    memset(digest, 0, sizeof(digest));
    *out_key = key;
    return true;
}

static bool config_is_valid(const ngn_ble_tracker_config_t *config)
{
    return config != NULL &&
           config->max_tracks > 0u &&
           config->max_tracks <= NGN_BLE_MAX_TRACKS &&
           config->aging_after_ms > 0u &&
           config->expire_after_ms > config->aging_after_ms &&
           config->rssi_ewma_divisor > 0u &&
           config->rssi_ewma_divisor <= 16u;
}

bool ngn_ble_tracker_init(ngn_ble_tracker_t *tracker,
                          const ngn_ble_tracker_config_t *config,
                          ngn_ble_event_sink_fn sink,
                          void *sink_ctx)
{
    if (tracker == NULL || !config_is_valid(config) || sink == NULL) {
        return false;
    }

    memset(tracker, 0, sizeof(*tracker));
    tracker->config = *config;
    tracker->sink = sink;
    tracker->sink_ctx = sink_ctx;
    return true;
}

void ngn_ble_tracker_reset(ngn_ble_tracker_t *tracker)
{
    if (tracker == NULL) {
        return;
    }

    memset(tracker->tracks, 0, sizeof(tracker->tracks));
    tracker->track_count = 0u;
}

static void fill_event(const ngn_ble_track_t *track,
                       ngn_ble_expire_reason_t expire_reason,
                       ngn_ble_track_event_t *event)
{
    memset(event, 0, sizeof(*event));
    event->key = track->key;
    event->receiver = track->receiver;
    event->address_type = track->address_type;
    event->state = track->state;
    event->expire_reason = expire_reason;
    event->first_seen_ms = track->first_seen_ms;
    event->last_seen_ms = track->last_seen_ms;
    event->observation_count = track->observation_count;
    event->latest_rssi_dbm = track->latest_rssi_dbm;
    event->filtered_rssi_q8 = track->filtered_rssi_q8;
    event->advertisement_type = track->advertisement_type;
    event->flags_present = track->flags_present;
    event->flags = track->flags;
    event->company_id_present = track->company_id_present;
    event->company_id = track->company_id;
    memcpy(event->service_uuid16, track->service_uuid16,
           sizeof(event->service_uuid16));
    event->service_uuid16_count = track->service_uuid16_count;
    event->service_uuid32_count = track->service_uuid32_count;
    event->service_uuid128_count = track->service_uuid128_count;
    event->payload_length = track->payload_length;
}

static void emit_track(ngn_ble_tracker_t *tracker,
                       const ngn_ble_track_t *track,
                       ngn_ble_expire_reason_t expire_reason)
{
    ngn_ble_track_event_t event;
    fill_event(track, expire_reason, &event);
    tracker->sink(tracker->sink_ctx, &event);
}

static ngn_ble_track_t *find_track(ngn_ble_tracker_t *tracker,
                                   ngn_ble_track_key_t key)
{
    size_t i;
    for (i = 0u; i < tracker->config.max_tracks; ++i) {
        if (tracker->tracks[i].occupied && tracker->tracks[i].key == key) {
            return &tracker->tracks[i];
        }
    }
    return NULL;
}

static ngn_ble_track_t *find_free_track(ngn_ble_tracker_t *tracker)
{
    size_t i;
    for (i = 0u; i < tracker->config.max_tracks; ++i) {
        if (!tracker->tracks[i].occupied) {
            return &tracker->tracks[i];
        }
    }
    return NULL;
}

static ngn_ble_track_t *choose_eviction_track(ngn_ble_tracker_t *tracker)
{
    ngn_ble_track_t *candidate = NULL;
    size_t i;

    for (i = 0u; i < tracker->config.max_tracks; ++i) {
        ngn_ble_track_t *track = &tracker->tracks[i];
        if (!track->occupied) {
            continue;
        }
        if (candidate == NULL ||
            track->last_seen_ms < candidate->last_seen_ms ||
            (track->last_seen_ms == candidate->last_seen_ms && track->key < candidate->key)) {
            candidate = track;
        }
    }
    return candidate;
}

static void update_metadata(ngn_ble_track_t *track,
                            const ngn_ble_observation_t *observation)
{
    track->receiver = observation->receiver;
    track->address_type = observation->address_type;
    track->advertisement_type = observation->advertisement_type;
    track->flags_present = observation->flags_present;
    track->flags = observation->flags;
    track->company_id_present = observation->company_id_present;
    track->company_id = observation->company_id;
    memcpy(track->service_uuid16, observation->service_uuid16,
           sizeof(track->service_uuid16));
    track->service_uuid16_count = observation->service_uuid16_count;
    track->service_uuid32_count = observation->service_uuid32_count;
    track->service_uuid128_count = observation->service_uuid128_count;
    track->payload_length = observation->payload_length;
}

static void update_rssi(ngn_ble_track_t *track,
                        int8_t rssi_dbm,
                        uint8_t divisor,
                        bool first_observation)
{
    const int32_t target_q8 = (int32_t)rssi_dbm * 256;
    int32_t filtered_q8;

    track->latest_rssi_dbm = rssi_dbm;
    if (first_observation) {
        track->filtered_rssi_q8 = (int16_t)target_q8;
        return;
    }

    filtered_q8 = track->filtered_rssi_q8;
    filtered_q8 += (target_q8 - filtered_q8) / divisor;
    track->filtered_rssi_q8 = (int16_t)filtered_q8;
}

bool ngn_ble_tracker_observe(ngn_ble_tracker_t *tracker,
                             ngn_ble_track_key_t key,
                             const ngn_ble_observation_t *observation)
{
    ngn_ble_track_t *track;

    if (tracker == NULL || observation == NULL ||
        !ngn_node_id_is_valid(observation->receiver) ||
        observation->service_uuid16_count > NGN_BLE_MAX_SERVICE_UUID16) {
        return false;
    }

    ngn_ble_tracker_tick(tracker, observation->timestamp_ms);
    track = find_track(tracker, key);

    if (track != NULL) {
        if (observation->timestamp_ms < track->last_seen_ms) {
            return false;
        }
        track->last_seen_ms = observation->timestamp_ms;
        ++track->observation_count;
        track->state = NGN_BLE_TRACK_ACTIVE;
        update_rssi(track, observation->rssi_dbm,
                    tracker->config.rssi_ewma_divisor, false);
        update_metadata(track, observation);
        emit_track(tracker, track, NGN_BLE_EXPIRE_NONE);
        return true;
    }

    track = find_free_track(tracker);
    if (track == NULL) {
        track = choose_eviction_track(tracker);
        if (track == NULL) {
            return false;
        }
        track->state = NGN_BLE_TRACK_EXPIRED;
        emit_track(tracker, track, NGN_BLE_EXPIRE_EVICTED);
        memset(track, 0, sizeof(*track));
        --tracker->track_count;
    }

    track->occupied = true;
    track->key = key;
    track->state = NGN_BLE_TRACK_NEW;
    track->first_seen_ms = observation->timestamp_ms;
    track->last_seen_ms = observation->timestamp_ms;
    track->observation_count = 1u;
    update_rssi(track, observation->rssi_dbm,
                tracker->config.rssi_ewma_divisor, true);
    update_metadata(track, observation);
    ++tracker->track_count;
    emit_track(tracker, track, NGN_BLE_EXPIRE_NONE);
    return true;
}

void ngn_ble_tracker_tick(ngn_ble_tracker_t *tracker, uint64_t now_ms)
{
    size_t i;

    if (tracker == NULL) {
        return;
    }

    for (i = 0u; i < tracker->config.max_tracks; ++i) {
        ngn_ble_track_t *track = &tracker->tracks[i];
        uint64_t age_ms;

        if (!track->occupied || now_ms < track->last_seen_ms) {
            continue;
        }

        age_ms = now_ms - track->last_seen_ms;
        if (age_ms >= tracker->config.expire_after_ms) {
            track->state = NGN_BLE_TRACK_EXPIRED;
            emit_track(tracker, track, NGN_BLE_EXPIRE_TIMEOUT);
            memset(track, 0, sizeof(*track));
            --tracker->track_count;
        } else if (age_ms >= tracker->config.aging_after_ms &&
                   track->state != NGN_BLE_TRACK_AGING) {
            track->state = NGN_BLE_TRACK_AGING;
            emit_track(tracker, track, NGN_BLE_EXPIRE_NONE);
        }
    }
}

size_t ngn_ble_tracker_count(const ngn_ble_tracker_t *tracker)
{
    return tracker == NULL ? 0u : tracker->track_count;
}

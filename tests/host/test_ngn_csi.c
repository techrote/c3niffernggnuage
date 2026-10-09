#include <stdio.h>
#include <string.h>

#include "ngn_csi.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                    __FILE__, __LINE__, #condition);                            \
            return false;                                                       \
        }                                                                       \
    } while (0)

static const uint8_t BROADCAST[NGN_TRANSPORT_MAC_SIZE] =
    {0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu};

static const uint8_t MACS[3][NGN_TRANSPORT_MAC_SIZE] = {
    {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x01u},
    {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x02u},
    {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0x03u}
};

typedef struct {
    bool accept;
    bool called;
    ngn_csi_raw_t record;
} queue_mock_t;

static bool mock_enqueue(void *context, const ngn_csi_raw_t *record)
{
    queue_mock_t *mock = context;
    mock->called = true;
    if (!mock->accept) {
        return false;
    }
    mock->record = *record;
    return true;
}

static ngn_csi_rx_meta_t metadata(uint64_t received_ms)
{
    ngn_csi_rx_meta_t meta = {0};
    meta.rssi = -47;
    meta.noise_floor = -96;
    meta.rate = 11u;
    meta.sig_mode = 1u;
    meta.mcs = 3u;
    meta.cwb = 0u;
    meta.smoothing = 1u;
    meta.not_sounding = 0u;
    meta.aggregation = 0u;
    meta.stbc = 0u;
    meta.fec_coding = 0u;
    meta.sgi = 0u;
    meta.ampdu_count = 1u;
    meta.channel = 6u;
    meta.secondary_channel = 0u;
    meta.antenna = 0u;
    meta.rx_state = 0u;
    meta.signal_length = 30u;
    meta.rx_sequence = 123u;
    meta.hardware_timestamp_us = 123456u;
    meta.received_ms = received_ms;
    return meta;
}

static bool test_binding_and_source_filter(void)
{
    ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT] = {0};
    ngn_node_id_t source = NGN_NODE_UNCONFIGURED;
    const uint8_t multicast[6] = {0x01u, 2u, 3u, 4u, 5u, 6u};
    const uint8_t zero[6] = {0};

    CHECK(ngn_csi_binding_set(bindings, NGN_NODE_A, MACS[0]));
    CHECK(ngn_csi_binding_set(bindings, NGN_NODE_B, MACS[1]));
    CHECK(ngn_csi_binding_set(bindings, NGN_NODE_A, MACS[0]));
    CHECK(!ngn_csi_binding_set(bindings, NGN_NODE_A, MACS[2]));
    CHECK(!ngn_csi_binding_set(bindings, NGN_NODE_C, MACS[1]));
    CHECK(!ngn_csi_binding_set(bindings, NGN_NODE_C, multicast));
    CHECK(!ngn_csi_binding_set(bindings, NGN_NODE_C, zero));

    CHECK(ngn_csi_source_for_mac(bindings, MACS[0], &source));
    CHECK(source == NGN_NODE_A);
    CHECK(ngn_csi_source_for_mac(bindings, MACS[1], &source));
    CHECK(source == NGN_NODE_B);
    CHECK(!ngn_csi_source_for_mac(bindings, MACS[2], &source));
    CHECK(!ngn_csi_source_for_mac(bindings, multicast, &source));
    CHECK(!ngn_csi_source_for_mac(NULL, MACS[0], &source));
    CHECK(!ngn_csi_source_for_mac(bindings, MACS[0], NULL));
    return true;
}

static bool test_bounded_capture_and_queue_drop(void)
{
    queue_mock_t mock = {.accept = true};
    ngn_csi_capture_stats_t stats = {0};
    ngn_csi_rx_meta_t meta = metadata(1000u);
    const int8_t data[4] = {3, 4, -4, 3};
    int8_t oversize[NGN_CSI_MAX_RAW_BYTES + 1u] = {0};

    CHECK(ngn_csi_capture(NGN_NODE_A, MACS[0], BROADCAST, &meta, data, sizeof(data),
                          mock_enqueue, &mock, &stats));
    CHECK(mock.called);
    CHECK(stats.captured == 1u);
    CHECK(stats.invalid_input == 0u);
    CHECK(stats.oversize == 0u);
    CHECK(stats.queue_drops == 0u);
    CHECK(mock.record.source == NGN_NODE_A);
    CHECK(mock.record.length == sizeof(data));
    CHECK(mock.record.meta.rssi == -47);
    CHECK(mock.record.meta.rx_sequence == 123u);
    CHECK(memcmp(mock.record.source_mac, MACS[0], sizeof(MACS[0])) == 0);
    CHECK(memcmp(mock.record.destination_mac, BROADCAST, sizeof(BROADCAST)) == 0);
    CHECK(memcmp(mock.record.bytes, data, sizeof(data)) == 0);

    mock.accept = false;
    mock.called = false;
    CHECK(!ngn_csi_capture(NGN_NODE_A, MACS[0], BROADCAST, &meta, data, sizeof(data),
                           mock_enqueue, &mock, &stats));
    CHECK(mock.called);
    CHECK(stats.queue_drops == 1u);

    CHECK(!ngn_csi_capture(NGN_NODE_A, MACS[0], BROADCAST, &meta, oversize,
                           sizeof(oversize), mock_enqueue, &mock, &stats));
    CHECK(stats.oversize == 1u);
    CHECK(!ngn_csi_capture(NGN_NODE_UNCONFIGURED, MACS[0], BROADCAST, &meta, data,
                           sizeof(data), mock_enqueue, &mock, &stats));
    CHECK(!ngn_csi_capture(NGN_NODE_A, MACS[0], BROADCAST, &meta, NULL,
                           sizeof(data), mock_enqueue, &mock, &stats));
    CHECK(stats.invalid_input == 2u);
    return true;
}

static bool test_iq_decode_metadata_and_first_word(void)
{
    ngn_csi_raw_t raw = {0};
    ngn_csi_packet_t packet;
    const int8_t data[8] = {99, 99, 99, 99, 3, 4, -4, 3};

    raw.source = NGN_NODE_B;
    memcpy(raw.source_mac, MACS[1], sizeof(raw.source_mac));
    memcpy(raw.destination_mac, BROADCAST, sizeof(raw.destination_mac));
    raw.meta = metadata(2000u);
    raw.meta.first_word_invalid = true;
    raw.length = sizeof(data);
    memcpy(raw.bytes, data, sizeof(data));

    CHECK(ngn_csi_decode(&raw, 6u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_OK);
    CHECK(packet.source == NGN_NODE_B);
    CHECK(packet.raw_length == 8u);
    CHECK(packet.valid_offset == 4u);
    CHECK(packet.sample_count == 2u);
    CHECK(packet.power[0] == 25u);
    CHECK(packet.power[1] == 25u);
    CHECK(packet.meta.rssi == -47);
    CHECK(packet.meta.noise_floor == -96);
    CHECK(packet.meta.hardware_timestamp_us == 123456u);
    CHECK(packet.meta.rx_sequence == 123u);
    CHECK((packet.quality_flags & NGN_CSI_QUALITY_FIRST_WORD_SKIPPED) != 0u);
    CHECK((packet.quality_flags & NGN_CSI_QUALITY_UNATTRIBUTED) != 0u);
    CHECK(!packet.probe_attributed);
    CHECK(memcmp(packet.destination_mac, BROADCAST, sizeof(BROADCAST)) == 0);
    CHECK(memcmp(packet.raw_iq, data, sizeof(data)) == 0);
    return true;
}

static bool test_probe_attribution_uses_nearest_observation(void)
{
    ngn_csi_raw_t raw = {0};
    ngn_csi_packet_t packet;
    ngn_csi_probe_observation_t history[4] = {0};
    const int8_t data[4] = {1, 2, 3, 4};

    raw.source = NGN_NODE_A;
    memcpy(raw.source_mac, MACS[0], sizeof(raw.source_mac));
    memcpy(raw.destination_mac, BROADCAST, sizeof(raw.destination_mac));
    raw.meta = metadata(1109u);
    raw.length = sizeof(data);
    memcpy(raw.bytes, data, sizeof(data));

    history[0] = (ngn_csi_probe_observation_t){
        .valid = true, .source = NGN_NODE_A, .session_id = 11u,
        .epoch = 7u, .sequence = 100u, .received_ms = 1100u
    };
    history[1] = (ngn_csi_probe_observation_t){
        .valid = true, .source = NGN_NODE_A, .session_id = 11u,
        .epoch = 7u, .sequence = 101u, .received_ms = 1110u
    };
    history[2] = (ngn_csi_probe_observation_t){
        .valid = true, .source = NGN_NODE_B, .session_id = 11u,
        .epoch = 7u, .sequence = 55u, .received_ms = 1109u
    };

    CHECK(ngn_csi_decode(&raw, 6u, history, 4u, 10u, &packet) ==
          NGN_CSI_DECODE_OK);
    CHECK(packet.probe_attributed);
    CHECK(packet.session_id == 11u);
    CHECK(packet.epoch == 7u);
    CHECK(packet.probe_sequence == 101u);
    CHECK(packet.attribution_delta_ms == 1u);
    CHECK((packet.quality_flags & NGN_CSI_QUALITY_UNATTRIBUTED) == 0u);

    CHECK(ngn_csi_decode(&raw, 6u, history, 4u, 1u, &packet) ==
          NGN_CSI_DECODE_OK);
    CHECK(packet.probe_attributed);
    raw.meta.received_ms = 1120u;
    CHECK(ngn_csi_decode(&raw, 6u, history, 4u, 5u, &packet) ==
          NGN_CSI_DECODE_OK);
    CHECK(!packet.probe_attributed);
    CHECK((packet.quality_flags & NGN_CSI_QUALITY_UNATTRIBUTED) != 0u);
    return true;
}

static bool test_malformed_and_metadata_rejection(void)
{
    ngn_csi_raw_t raw = {0};
    ngn_csi_packet_t packet;
    ngn_csi_packet_t before;
    const int8_t data[5] = {1, 2, 3, 4, 5};

    raw.source = NGN_NODE_C;
    memcpy(raw.source_mac, MACS[2], sizeof(raw.source_mac));
    memcpy(raw.destination_mac, BROADCAST, sizeof(raw.destination_mac));
    raw.meta = metadata(200u);
    raw.length = sizeof(data);
    memcpy(raw.bytes, data, sizeof(data));
    memset(&packet, 0x5a, sizeof(packet));
    before = packet;

    CHECK(ngn_csi_decode(&raw, 6u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_INVALID_LENGTH);
    CHECK(memcmp(&packet, &before, sizeof(packet)) == 0);

    raw.length = 4u;
    raw.meta.first_word_invalid = true;
    CHECK(ngn_csi_decode(&raw, 6u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_INVALID_LENGTH);
    raw.meta.first_word_invalid = false;
    raw.meta.rx_state = 1u;
    CHECK(ngn_csi_decode(&raw, 6u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_RX_ERROR);
    raw.meta.rx_state = 0u;
    raw.meta.channel = 11u;
    CHECK(ngn_csi_decode(&raw, 6u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_CHANNEL_MISMATCH);
    CHECK(ngn_csi_decode(NULL, 6u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_INVALID_ARGUMENT);
    CHECK(ngn_csi_decode(&raw, 0u, NULL, 0u, 5u, &packet) ==
          NGN_CSI_DECODE_INVALID_ARGUMENT);
    CHECK(ngn_csi_decode(&raw, 6u, NULL, 0u, 0u, &packet) ==
          NGN_CSI_DECODE_INVALID_ARGUMENT);
    CHECK(ngn_csi_decode(&raw, 6u, NULL, 1u, 5u, &packet) ==
          NGN_CSI_DECODE_INVALID_ARGUMENT);
    CHECK(ngn_csi_decode(&raw, 6u, NULL, NGN_CSI_PROBE_HISTORY + 1u,
                         5u, &packet) == NGN_CSI_DECODE_INVALID_ARGUMENT);
    return true;
}

int main(void)
{
    if (!test_binding_and_source_filter() ||
        !test_bounded_capture_and_queue_drop() ||
        !test_iq_decode_metadata_and_first_word() ||
        !test_probe_attribution_uses_nearest_observation() ||
        !test_malformed_and_metadata_rejection()) {
        return 1;
    }
    puts("ngn_csi: binding, bounded capture, I/Q decode and attribution passed");
    return 0;
}

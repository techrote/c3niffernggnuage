#include "ngn_csi.h"

#include <limits.h>
#include <string.h>

static bool mac_valid(const uint8_t mac[NGN_TRANSPORT_MAC_SIZE])
{
    uint8_t any = 0u;
    size_t i;

    if (mac == NULL || (mac[0] & 1u) != 0u) {
        return false;
    }
    for (i = 0u; i < NGN_TRANSPORT_MAC_SIZE; ++i) {
        any |= mac[i];
    }
    return any != 0u;
}

bool ngn_csi_binding_set(ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT],
                         ngn_node_id_t node,
                         const uint8_t mac[NGN_TRANSPORT_MAC_SIZE])
{
    size_t i;
    ngn_csi_binding_t *binding;

    if (bindings == NULL || !ngn_node_id_is_valid(node) || !mac_valid(mac)) {
        return false;
    }
    binding = &bindings[(size_t)node];
    if (binding->bound &&
        memcmp(binding->mac, mac, NGN_TRANSPORT_MAC_SIZE) != 0) {
        return false;
    }
    for (i = 0u; i < NGN_CSI_NODE_COUNT; ++i) {
        if (i != (size_t)node && bindings[i].bound &&
            memcmp(bindings[i].mac, mac, NGN_TRANSPORT_MAC_SIZE) == 0) {
            return false;
        }
    }
    binding->bound = true;
    memcpy(binding->mac, mac, NGN_TRANSPORT_MAC_SIZE);
    return true;
}

bool ngn_csi_source_for_mac(
    const ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT],
    const uint8_t mac[NGN_TRANSPORT_MAC_SIZE],
    ngn_node_id_t *out_source)
{
    size_t i;

    if (bindings == NULL || !mac_valid(mac) || out_source == NULL) {
        return false;
    }
    for (i = 0u; i < NGN_CSI_NODE_COUNT; ++i) {
        if (bindings[i].bound &&
            memcmp(bindings[i].mac, mac, NGN_TRANSPORT_MAC_SIZE) == 0) {
            *out_source = (ngn_node_id_t)i;
            return true;
        }
    }
    return false;
}

bool ngn_csi_capture(ngn_node_id_t source,
                     uint64_t session_id,
                     const uint8_t source_mac[NGN_TRANSPORT_MAC_SIZE],
                     const uint8_t destination_mac[NGN_TRANSPORT_MAC_SIZE],
                     const ngn_csi_rx_meta_t *meta,
                     const int8_t *data,
                     size_t length,
                     ngn_csi_enqueue_fn enqueue,
                     void *enqueue_context,
                     ngn_csi_capture_stats_t *stats)
{
    ngn_csi_raw_t record;

    if (stats == NULL) {
        return false;
    }
    if (!ngn_node_id_is_valid(source) || session_id == 0u ||
        !mac_valid(source_mac) ||
        destination_mac == NULL || meta == NULL || data == NULL ||
        enqueue == NULL || length == 0u) {
        ++stats->invalid_input;
        return false;
    }
    if (length > NGN_CSI_MAX_RAW_BYTES || length > UINT16_MAX) {
        ++stats->oversize;
        return false;
    }

    memset(&record, 0, sizeof(record));
    record.source = source;
    record.session_id = session_id;
    memcpy(record.source_mac, source_mac, sizeof(record.source_mac));
    memcpy(record.destination_mac, destination_mac,
           sizeof(record.destination_mac));
    record.meta = *meta;
    record.length = (uint16_t)length;
    memcpy(record.bytes, data, length);

    if (!enqueue(enqueue_context, &record)) {
        ++stats->queue_drops;
        return false;
    }
    ++stats->captured;
    return true;
}

static uint64_t time_distance(uint64_t a, uint64_t b)
{
    return a >= b ? a - b : b - a;
}

static bool best_probe(const ngn_csi_raw_t *raw,
                       const ngn_csi_probe_observation_t *history,
                       size_t history_count,
                       uint16_t window_ms,
                       ngn_csi_probe_observation_t *out,
                       uint16_t *out_delta)
{
    bool found = false;
    uint64_t best_delta = UINT64_MAX;
    size_t i;

    if (history == NULL || out == NULL || out_delta == NULL) {
        return false;
    }
    for (i = 0u; i < history_count; ++i) {
        const ngn_csi_probe_observation_t *candidate = &history[i];
        uint64_t delta;
        if (!candidate->valid || candidate->source != raw->source ||
            candidate->session_id != raw->session_id) {
            continue;
        }
        delta = time_distance(raw->meta.received_ms, candidate->received_ms);
        if (delta > window_ms) {
            continue;
        }
        if (!found || delta < best_delta ||
            (delta == best_delta && candidate->received_ms > out->received_ms)) {
            *out = *candidate;
            best_delta = delta;
            found = true;
        }
    }
    if (found) {
        *out_delta = (uint16_t)best_delta;
    }
    return found;
}

ngn_csi_decode_result_t ngn_csi_decode(
    const ngn_csi_raw_t *raw,
    uint8_t expected_channel,
    const ngn_csi_probe_observation_t *history,
    size_t history_count,
    uint16_t attribution_window_ms,
    ngn_csi_packet_t *out_packet)
{
    ngn_csi_packet_t packet;
    ngn_csi_probe_observation_t observation = {0};
    uint16_t offset = 0u;
    uint16_t delta = 0u;
    uint16_t valid_length;
    size_t i;

    if (raw == NULL || out_packet == NULL || expected_channel < 1u ||
        expected_channel > 11u || attribution_window_ms == 0u ||
        history_count > NGN_CSI_PROBE_HISTORY ||
        (history_count != 0u && history == NULL) ||
        !ngn_node_id_is_valid(raw->source) || !mac_valid(raw->source_mac)) {
        return NGN_CSI_DECODE_INVALID_ARGUMENT;
    }
    if (raw->meta.rx_state != 0u) {
        return NGN_CSI_DECODE_RX_ERROR;
    }
    if (raw->meta.channel != expected_channel) {
        return NGN_CSI_DECODE_CHANNEL_MISMATCH;
    }
    if (raw->length == 0u || raw->length > NGN_CSI_MAX_RAW_BYTES) {
        return NGN_CSI_DECODE_INVALID_LENGTH;
    }
    if (raw->meta.first_word_invalid) {
        if (raw->length <= 4u) {
            return NGN_CSI_DECODE_INVALID_LENGTH;
        }
        offset = 4u;
    }
    valid_length = (uint16_t)(raw->length - offset);
    if ((valid_length & 1u) != 0u || valid_length == 0u) {
        return NGN_CSI_DECODE_INVALID_LENGTH;
    }

    memset(&packet, 0, sizeof(packet));
    packet.source = raw->source;
    packet.session_id = raw->session_id;
    memcpy(packet.source_mac, raw->source_mac, sizeof(packet.source_mac));
    memcpy(packet.destination_mac, raw->destination_mac,
           sizeof(packet.destination_mac));
    packet.meta = raw->meta;
    packet.raw_length = raw->length;
    packet.valid_offset = offset;
    packet.sample_count = (uint16_t)(valid_length / 2u);
    memcpy(packet.raw_iq, raw->bytes, raw->length);
    if (offset != 0u) {
        packet.quality_flags |= NGN_CSI_QUALITY_FIRST_WORD_SKIPPED;
    }

    for (i = 0u; i < packet.sample_count; ++i) {
        const int imag = (int)raw->bytes[offset + 2u * i];
        const int real = (int)raw->bytes[offset + 2u * i + 1u];
        packet.power[i] = (uint16_t)(imag * imag + real * real);
    }

    if (best_probe(raw, history, history_count, attribution_window_ms,
                   &observation, &delta)) {
        packet.probe_attributed = true;
        packet.epoch = observation.epoch;
        packet.probe_sequence = observation.sequence;
        packet.attribution_delta_ms = delta;
    } else {
        packet.quality_flags |= NGN_CSI_QUALITY_UNATTRIBUTED;
    }

    *out_packet = packet;
    return NGN_CSI_DECODE_OK;
}

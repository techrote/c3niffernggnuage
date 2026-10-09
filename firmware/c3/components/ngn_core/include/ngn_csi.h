#ifndef NGN_CSI_H
#define NGN_CSI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ngn_node.h"
#include "ngn_transport.h"

#define NGN_CSI_MAX_RAW_BYTES 612u
#define NGN_CSI_MAX_COMPLEX_SAMPLES (NGN_CSI_MAX_RAW_BYTES / 2u)
#define NGN_CSI_PROBE_HISTORY 16u
#define NGN_CSI_NODE_COUNT 3u

#define NGN_CSI_QUALITY_FIRST_WORD_SKIPPED (1u << 0)
#define NGN_CSI_QUALITY_UNATTRIBUTED        (1u << 1)

typedef struct {
    int8_t rssi;
    int8_t noise_floor;
    uint8_t rate;
    uint8_t sig_mode;
    uint8_t mcs;
    uint8_t cwb;
    uint8_t smoothing;
    uint8_t not_sounding;
    uint8_t aggregation;
    uint8_t stbc;
    uint8_t fec_coding;
    uint8_t sgi;
    uint8_t ampdu_count;
    uint8_t channel;
    uint8_t secondary_channel;
    uint8_t antenna;
    uint8_t rx_state;
    uint16_t signal_length;
    uint16_t rx_sequence;
    uint32_t hardware_timestamp_us;
    uint64_t received_ms;
    bool first_word_invalid;
} ngn_csi_rx_meta_t;

typedef struct {
    bool bound;
    uint8_t mac[NGN_TRANSPORT_MAC_SIZE];
} ngn_csi_binding_t;

typedef struct {
    bool valid;
    ngn_node_id_t source;
    uint64_t session_id;
    uint32_t epoch;
    uint32_t sequence;
    uint64_t received_ms;
} ngn_csi_probe_observation_t;

typedef struct {
    ngn_node_id_t source;
    uint64_t session_id;
    uint8_t source_mac[NGN_TRANSPORT_MAC_SIZE];
    uint8_t destination_mac[NGN_TRANSPORT_MAC_SIZE];
    ngn_csi_rx_meta_t meta;
    uint16_t length;
    int8_t bytes[NGN_CSI_MAX_RAW_BYTES];
} ngn_csi_raw_t;

typedef struct {
    uint32_t captured;
    uint32_t invalid_input;
    uint32_t oversize;
    uint32_t queue_drops;
} ngn_csi_capture_stats_t;

typedef bool (*ngn_csi_enqueue_fn)(void *context, const ngn_csi_raw_t *record);

typedef enum {
    NGN_CSI_DECODE_OK = 0,
    NGN_CSI_DECODE_INVALID_ARGUMENT,
    NGN_CSI_DECODE_INVALID_LENGTH,
    NGN_CSI_DECODE_RX_ERROR,
    NGN_CSI_DECODE_CHANNEL_MISMATCH
} ngn_csi_decode_result_t;

typedef struct {
    ngn_node_id_t source;
    uint8_t source_mac[NGN_TRANSPORT_MAC_SIZE];
    uint8_t destination_mac[NGN_TRANSPORT_MAC_SIZE];
    ngn_csi_rx_meta_t meta;

    bool probe_attributed;
    uint64_t session_id;
    uint32_t epoch;
    uint32_t probe_sequence;
    uint16_t attribution_delta_ms;

    uint8_t quality_flags;
    uint16_t raw_length;
    uint16_t valid_offset;
    uint16_t sample_count;
    int8_t raw_iq[NGN_CSI_MAX_RAW_BYTES];
    uint16_t power[NGN_CSI_MAX_COMPLEX_SAMPLES];
} ngn_csi_packet_t;

/* Bindings follow the same first-valid-observation model as ngn_radio. A
 * logical node cannot change MAC and one MAC cannot claim two logical nodes. */
bool ngn_csi_binding_set(ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT],
                         ngn_node_id_t node,
                         const uint8_t mac[NGN_TRANSPORT_MAC_SIZE]);

/* Returns false for unknown, zero or multicast source MACs. */
bool ngn_csi_source_for_mac(
    const ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT],
    const uint8_t mac[NGN_TRANSPORT_MAC_SIZE],
    ngn_node_id_t *out_source);

/* Bounded capture helper used by the ESP callback. It performs only validation,
 * a fixed-size copy and one caller-supplied nonblocking enqueue operation. */
bool ngn_csi_capture(ngn_node_id_t source,
                     uint64_t session_id,
                     const uint8_t source_mac[NGN_TRANSPORT_MAC_SIZE],
                     const uint8_t destination_mac[NGN_TRANSPORT_MAC_SIZE],
                     const ngn_csi_rx_meta_t *meta,
                     const int8_t *data,
                     size_t length,
                     ngn_csi_enqueue_fn enqueue,
                     void *enqueue_context,
                     ngn_csi_capture_stats_t *stats);

/* Worker-side decoding. CSI bytes are interpreted as [imaginary, real] pairs.
 * If first_word_invalid is set, exactly the first four raw bytes are preserved
 * for debugging but excluded from the magnitude vector. Probe attribution uses
 * the closest accepted-probe receive timestamp inside attribution_window_ms.
 * Known-source CSI without a matching probe remains a valid diagnostic record
 * but carries NGN_CSI_QUALITY_UNATTRIBUTED and is not primary sensing evidence. */
ngn_csi_decode_result_t ngn_csi_decode(
    const ngn_csi_raw_t *raw,
    uint8_t expected_channel,
    const ngn_csi_probe_observation_t *history,
    size_t history_count,
    uint16_t attribution_window_ms,
    ngn_csi_packet_t *out_packet);

#endif

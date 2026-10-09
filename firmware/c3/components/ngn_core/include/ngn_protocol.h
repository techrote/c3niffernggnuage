#ifndef NGN_PROTOCOL_H
#define NGN_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ngn_node.h"
#include "ngn_schedule.h"
#include "ngn_version.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NGN_PROTOCOL_MAGIC 0x4e47u
#define NGN_PROTOCOL_HEADER_SIZE 24u
#define NGN_PROTOCOL_CRC_SIZE 2u
#define NGN_PROTOCOL_MAX_FRAME_SIZE 96u
#define NGN_PROTOCOL_SYNC_PAYLOAD_SIZE 20u
#define NGN_PROTOCOL_PROBE_PAYLOAD_SIZE 4u
#define NGN_PROTOCOL_HEALTH_PAYLOAD_SIZE 52u

#define NGN_HEALTH_TX_SEQUENCE_VALID 0x01u
#define NGN_HEALTH_RX_SEQUENCE_VALID 0x02u

typedef enum {
    NGN_MESSAGE_SYNC = 1,
    NGN_MESSAGE_PROBE = 2,
    NGN_MESSAGE_NODE_HEALTH = 3
} ngn_message_type_t;

typedef struct {
    uint8_t version;
    ngn_message_type_t type;
    ngn_node_id_t source;
    uint16_t payload_length;
    uint64_t session_id;
    uint32_t epoch;
    uint32_t sequence;
} ngn_protocol_header_t;

typedef struct {
    uint8_t schedule_version;
    ngn_schedule_config_t config;
    uint32_t epoch_elapsed_ms;
} ngn_sync_payload_t;

typedef struct {
    uint8_t index;
    uint8_t count;
} ngn_probe_payload_t;

typedef struct {
    uint32_t uptime_ms;
    uint32_t last_probe_tx_seq;
    uint32_t last_probe_rx_seq;
    uint32_t tx_completed;
    uint32_t rx_packets;
    uint32_t rx_queue_drops;
    uint32_t tx_queue_drops;
    uint32_t status_queue_drops;
    uint32_t tx_errors;
    uint32_t rx_rejected;
    uint32_t late_tx_drops;
    uint8_t channel;
    uint8_t present_mask;
    uint8_t firmware_major;
    uint8_t firmware_minor;
    uint8_t firmware_patch;
    uint8_t flags;
    ngn_node_id_t last_rx_node;
} ngn_health_payload_t;

typedef struct {
    ngn_protocol_header_t header;
    union {
        ngn_sync_payload_t sync;
        ngn_probe_payload_t probe;
        ngn_health_payload_t health;
    } payload;
} ngn_message_t;

typedef enum {
    NGN_PROTOCOL_OK = 0,
    NGN_PROTOCOL_ARGUMENT,
    NGN_PROTOCOL_LENGTH,
    NGN_PROTOCOL_MAGIC_ERROR,
    NGN_PROTOCOL_VERSION_ERROR,
    NGN_PROTOCOL_TYPE_ERROR,
    NGN_PROTOCOL_SOURCE_ERROR,
    NGN_PROTOCOL_RESERVED_ERROR,
    NGN_PROTOCOL_SESSION_ERROR,
    NGN_PROTOCOL_CRC_ERROR,
    NGN_PROTOCOL_PAYLOAD_ERROR
} ngn_protocol_result_t;

/* Zero means an unknown type. No native-struct bytes are put on the wire. */
size_t ngn_protocol_payload_size(ngn_message_type_t type);
uint16_t ngn_protocol_crc16(const uint8_t *data, size_t length);

/* Strict serial ordering: equality and exactly half-range are not newer. */
bool ngn_serial32_newer(uint32_t candidate, uint32_t reference);

/* All failure paths leave caller outputs unchanged. */
ngn_protocol_result_t ngn_protocol_encode(const ngn_message_t *message,
                                         uint8_t *out_frame,
                                         size_t capacity,
                                         size_t *out_length);
ngn_protocol_result_t ngn_protocol_decode(const uint8_t *frame,
                                         size_t length,
                                         ngn_message_t *out_message);

#ifdef __cplusplus
}
#endif

#endif

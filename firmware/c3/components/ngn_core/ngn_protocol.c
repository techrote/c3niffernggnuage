#include "ngn_protocol.h"

#include <string.h>

static void put_u16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8u);
    bytes[1] = (uint8_t)value;
}

static void put_u32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static void put_u64(uint8_t *bytes, uint64_t value)
{
    put_u32(bytes, (uint32_t)(value >> 32u));
    put_u32(bytes + 4u, (uint32_t)value);
}

static uint16_t get_u16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t get_u32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | bytes[3];
}

static uint64_t get_u64(const uint8_t *bytes)
{
    return ((uint64_t)get_u32(bytes) << 32u) | get_u32(bytes + 4u);
}

size_t ngn_protocol_payload_size(ngn_message_type_t type)
{
    switch (type) {
    case NGN_MESSAGE_SYNC:
        return NGN_PROTOCOL_SYNC_PAYLOAD_SIZE;
    case NGN_MESSAGE_PROBE:
        return NGN_PROTOCOL_PROBE_PAYLOAD_SIZE;
    case NGN_MESSAGE_NODE_HEALTH:
        return NGN_PROTOCOL_HEALTH_PAYLOAD_SIZE;
    default:
        return 0u;
    }
}

uint16_t ngn_protocol_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;
    size_t i;

    if (data == NULL && length != 0u) {
        return 0u;
    }
    for (i = 0u; i < length; ++i) {
        uint8_t bit;
        crc ^= (uint16_t)((uint16_t)data[i] << 8u);
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 0x8000u) != 0u
                ? (uint16_t)((uint16_t)(crc << 1u) ^ 0x1021u)
                : (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

bool ngn_serial32_newer(uint32_t candidate, uint32_t reference)
{
    const uint32_t difference = candidate - reference;
    return difference != 0u && difference < UINT32_C(0x80000000);
}

static bool health_valid(const ngn_health_payload_t *health)
{
    if (health->channel == 0u ||
        health->channel > NGN_SCHEDULE_MAX_CHANNEL ||
        (health->present_mask & 0xf8u) != 0u ||
        (health->flags & 0xfcu) != 0u) {
        return false;
    }
    if ((health->flags & NGN_HEALTH_TX_SEQUENCE_VALID) == 0u &&
        health->last_probe_tx_seq != 0u) {
        return false;
    }
    if ((health->flags & NGN_HEALTH_RX_SEQUENCE_VALID) == 0u) {
        return health->last_probe_rx_seq == 0u &&
               health->last_rx_node == NGN_NODE_UNCONFIGURED;
    }
    return ngn_node_id_is_valid(health->last_rx_node);
}

static ngn_protocol_result_t validate_header(const ngn_protocol_header_t *header)
{
    const size_t payload_size = ngn_protocol_payload_size(header->type);
    if (header->version != NGN_PROTOCOL_VERSION) {
        return NGN_PROTOCOL_VERSION_ERROR;
    }
    if (payload_size == 0u) {
        return NGN_PROTOCOL_TYPE_ERROR;
    }
    if (!ngn_node_id_is_valid(header->source) ||
        (header->type == NGN_MESSAGE_SYNC && header->source != NGN_NODE_C)) {
        return NGN_PROTOCOL_SOURCE_ERROR;
    }
    if (header->payload_length != payload_size) {
        return NGN_PROTOCOL_LENGTH;
    }
    if (header->session_id == 0u) {
        return NGN_PROTOCOL_SESSION_ERROR;
    }
    return NGN_PROTOCOL_OK;
}

static bool payload_valid(const ngn_message_t *message)
{
    switch (message->header.type) {
    case NGN_MESSAGE_SYNC:
        return message->payload.sync.schedule_version == NGN_SCHEDULE_VERSION &&
               ngn_schedule_config_valid(&message->payload.sync.config) &&
               message->payload.sync.epoch_elapsed_ms <
                   message->payload.sync.config.sync_slot_ms;
    case NGN_MESSAGE_PROBE:
        return message->payload.probe.count >= 1u &&
               message->payload.probe.count <= NGN_SCHEDULE_MAX_BURST &&
               message->payload.probe.index < message->payload.probe.count;
    case NGN_MESSAGE_NODE_HEALTH:
        return health_valid(&message->payload.health);
    default:
        return false;
    }
}

static void encode_payload(const ngn_message_t *message, uint8_t *bytes)
{
    switch (message->header.type) {
    case NGN_MESSAGE_SYNC: {
        const ngn_sync_payload_t *sync = &message->payload.sync;
        bytes[0] = sync->schedule_version;
        bytes[1] = sync->config.channel;
        bytes[2] = sync->config.burst_count;
        bytes[3] = sync->config.missing_epochs;
        put_u16(bytes + 4u, sync->config.probe_spacing_ms);
        put_u16(bytes + 6u, sync->config.sync_slot_ms);
        put_u16(bytes + 8u, sync->config.probe_slot_ms);
        put_u16(bytes + 10u, sync->config.coexist_ms);
        put_u16(bytes + 12u, sync->config.health_slot_ms);
        put_u32(bytes + 16u, sync->epoch_elapsed_ms);
        break;
    }
    case NGN_MESSAGE_PROBE:
        bytes[0] = message->payload.probe.index;
        bytes[1] = message->payload.probe.count;
        break;
    case NGN_MESSAGE_NODE_HEALTH: {
        const ngn_health_payload_t *health = &message->payload.health;
        put_u32(bytes, health->uptime_ms);
        put_u32(bytes + 4u, health->last_probe_tx_seq);
        put_u32(bytes + 8u, health->last_probe_rx_seq);
        put_u32(bytes + 12u, health->tx_completed);
        put_u32(bytes + 16u, health->rx_packets);
        put_u32(bytes + 20u, health->rx_queue_drops);
        put_u32(bytes + 24u, health->tx_queue_drops);
        put_u32(bytes + 28u, health->status_queue_drops);
        put_u32(bytes + 32u, health->tx_errors);
        put_u32(bytes + 36u, health->rx_rejected);
        put_u32(bytes + 40u, health->late_tx_drops);
        bytes[44] = health->channel;
        bytes[45] = health->present_mask;
        bytes[46] = health->firmware_major;
        bytes[47] = health->firmware_minor;
        bytes[48] = health->firmware_patch;
        bytes[49] = health->flags;
        bytes[50] = (uint8_t)health->last_rx_node;
        break;
    }
    default:
        break;
    }
}

ngn_protocol_result_t ngn_protocol_encode(const ngn_message_t *message,
                                         uint8_t *out_frame,
                                         size_t capacity,
                                         size_t *out_length)
{
    uint8_t frame[NGN_PROTOCOL_MAX_FRAME_SIZE] = {0};
    size_t length;
    ngn_protocol_result_t result;

    if (message == NULL || out_frame == NULL || out_length == NULL) {
        return NGN_PROTOCOL_ARGUMENT;
    }
    result = validate_header(&message->header);
    if (result != NGN_PROTOCOL_OK) {
        return result;
    }
    length = NGN_PROTOCOL_HEADER_SIZE + message->header.payload_length +
             NGN_PROTOCOL_CRC_SIZE;
    if (capacity < length) {
        return NGN_PROTOCOL_LENGTH;
    }
    if (!payload_valid(message)) {
        return NGN_PROTOCOL_PAYLOAD_ERROR;
    }

    put_u16(frame, NGN_PROTOCOL_MAGIC);
    frame[2] = message->header.version;
    frame[3] = (uint8_t)message->header.type;
    frame[4] = (uint8_t)message->header.source;
    put_u16(frame + 6u, message->header.payload_length);
    put_u64(frame + 8u, message->header.session_id);
    put_u32(frame + 16u, message->header.epoch);
    put_u32(frame + 20u, message->header.sequence);
    encode_payload(message, frame + NGN_PROTOCOL_HEADER_SIZE);
    put_u16(frame + length - NGN_PROTOCOL_CRC_SIZE,
            ngn_protocol_crc16(frame, length - NGN_PROTOCOL_CRC_SIZE));
    memcpy(out_frame, frame, length);
    *out_length = length;
    return NGN_PROTOCOL_OK;
}

static ngn_protocol_result_t decode_payload(const uint8_t *bytes,
                                            ngn_message_t *message)
{
    switch (message->header.type) {
    case NGN_MESSAGE_SYNC: {
        ngn_sync_payload_t *sync = &message->payload.sync;
        if (get_u16(bytes + 14u) != 0u) {
            return NGN_PROTOCOL_RESERVED_ERROR;
        }
        sync->schedule_version = bytes[0];
        sync->config.channel = bytes[1];
        sync->config.burst_count = bytes[2];
        sync->config.missing_epochs = bytes[3];
        sync->config.probe_spacing_ms = get_u16(bytes + 4u);
        sync->config.sync_slot_ms = get_u16(bytes + 6u);
        sync->config.probe_slot_ms = get_u16(bytes + 8u);
        sync->config.coexist_ms = get_u16(bytes + 10u);
        sync->config.health_slot_ms = get_u16(bytes + 12u);
        sync->epoch_elapsed_ms = get_u32(bytes + 16u);
        break;
    }
    case NGN_MESSAGE_PROBE:
        if (get_u16(bytes + 2u) != 0u) {
            return NGN_PROTOCOL_RESERVED_ERROR;
        }
        message->payload.probe.index = bytes[0];
        message->payload.probe.count = bytes[1];
        break;
    case NGN_MESSAGE_NODE_HEALTH: {
        ngn_health_payload_t *health = &message->payload.health;
        if (bytes[51] != 0u) {
            return NGN_PROTOCOL_RESERVED_ERROR;
        }
        health->uptime_ms = get_u32(bytes);
        health->last_probe_tx_seq = get_u32(bytes + 4u);
        health->last_probe_rx_seq = get_u32(bytes + 8u);
        health->tx_completed = get_u32(bytes + 12u);
        health->rx_packets = get_u32(bytes + 16u);
        health->rx_queue_drops = get_u32(bytes + 20u);
        health->tx_queue_drops = get_u32(bytes + 24u);
        health->status_queue_drops = get_u32(bytes + 28u);
        health->tx_errors = get_u32(bytes + 32u);
        health->rx_rejected = get_u32(bytes + 36u);
        health->late_tx_drops = get_u32(bytes + 40u);
        health->channel = bytes[44];
        health->present_mask = bytes[45];
        health->firmware_major = bytes[46];
        health->firmware_minor = bytes[47];
        health->firmware_patch = bytes[48];
        health->flags = bytes[49];
        health->last_rx_node = (ngn_node_id_t)bytes[50];
        break;
    }
    default:
        return NGN_PROTOCOL_TYPE_ERROR;
    }
    return payload_valid(message) ? NGN_PROTOCOL_OK : NGN_PROTOCOL_PAYLOAD_ERROR;
}

ngn_protocol_result_t ngn_protocol_decode(const uint8_t *frame,
                                         size_t length,
                                         ngn_message_t *out_message)
{
    ngn_message_t message = {0};
    ngn_protocol_result_t result;

    if (frame == NULL || out_message == NULL) {
        return NGN_PROTOCOL_ARGUMENT;
    }
    if (length < NGN_PROTOCOL_HEADER_SIZE + NGN_PROTOCOL_CRC_SIZE ||
        length > NGN_PROTOCOL_MAX_FRAME_SIZE) {
        return NGN_PROTOCOL_LENGTH;
    }
    if (get_u16(frame) != NGN_PROTOCOL_MAGIC) {
        return NGN_PROTOCOL_MAGIC_ERROR;
    }
    message.header.version = frame[2];
    message.header.type = (ngn_message_type_t)frame[3];
    message.header.source = (ngn_node_id_t)frame[4];
    message.header.payload_length = get_u16(frame + 6u);
    message.header.session_id = get_u64(frame + 8u);
    message.header.epoch = get_u32(frame + 16u);
    message.header.sequence = get_u32(frame + 20u);
    result = validate_header(&message.header);
    if (result != NGN_PROTOCOL_OK) {
        return result;
    }
    if (frame[5] != 0u) {
        return NGN_PROTOCOL_RESERVED_ERROR;
    }
    if (length != NGN_PROTOCOL_HEADER_SIZE + message.header.payload_length +
                  NGN_PROTOCOL_CRC_SIZE) {
        return NGN_PROTOCOL_LENGTH;
    }
    if (get_u16(frame + length - NGN_PROTOCOL_CRC_SIZE) !=
        ngn_protocol_crc16(frame, length - NGN_PROTOCOL_CRC_SIZE)) {
        return NGN_PROTOCOL_CRC_ERROR;
    }
    result = decode_payload(frame + NGN_PROTOCOL_HEADER_SIZE, &message);
    if (result != NGN_PROTOCOL_OK) {
        return result;
    }
    *out_message = message;
    return NGN_PROTOCOL_OK;
}

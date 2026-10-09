#include <stdio.h>
#include <string.h>

#include "ngn_protocol.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                           \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* These complete frames were independently produced using Python struct with
 * explicit '>' fields and binascii.crc_hqx(bytes, 0xffff). They are literals so
 * a codec round-trip cannot conceal shared encoder/decoder layout mistakes. */
static const uint8_t golden_probe[] = {
    0x4e, 0x47, 0x01, 0x02, 0x01, 0x00, 0x00, 0x04,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0xfe, 0xdc, 0xba, 0x98, 0xff, 0xff, 0xff, 0xff,
    0x02, 0x04, 0x00, 0x00, 0xe4, 0x84
};
static const uint8_t golden_sync[] = {
    0x4e, 0x47, 0x01, 0x01, 0x02, 0x00, 0x00, 0x14,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00,
    0x01, 0x06, 0x04, 0x04, 0x00, 0x0a, 0x00, 0x28,
    0x00, 0x50, 0x00, 0x50, 0x00, 0x0a, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x07, 0x29, 0x9c
};
static const uint8_t golden_health[] = {
    0x4e, 0x47, 0x01, 0x03, 0x00, 0x00, 0x00, 0x34,
    0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04,
    0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0x01, 0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x00, 0x00, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x0b,
    0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0d,
    0x00, 0x00, 0x00, 0x0e, 0x06, 0x05, 0x00, 0x02,
    0x00, 0x03, 0x02, 0x00, 0x8c, 0x40
};

static ngn_message_t probe_message(void)
{
    ngn_message_t message = {0};
    message.header.version = 1u;
    message.header.type = NGN_MESSAGE_PROBE;
    message.header.source = NGN_NODE_B;
    message.header.payload_length = 4u;
    message.header.session_id = UINT64_C(0x0123456789abcdef);
    message.header.epoch = UINT32_C(0xfedcba98);
    message.header.sequence = UINT32_MAX;
    message.payload.probe.index = 2u;
    message.payload.probe.count = 4u;
    return message;
}

static ngn_message_t sync_message(void)
{
    ngn_message_t message = {0};
    message.header.version = 1u;
    message.header.type = NGN_MESSAGE_SYNC;
    message.header.source = NGN_NODE_C;
    message.header.payload_length = 20u;
    message.header.session_id = UINT64_C(0x0102030405060708);
    message.header.sequence = UINT32_C(0x80000000);
    message.payload.sync.schedule_version = 1u;
    message.payload.sync.config = ngn_schedule_default_config();
    message.payload.sync.epoch_elapsed_ms = 7u;
    return message;
}

static ngn_message_t health_message(void)
{
    ngn_message_t message = {0};
    message.header.version = 1u;
    message.header.type = NGN_MESSAGE_NODE_HEALTH;
    message.header.source = NGN_NODE_A;
    message.header.payload_length = 52u;
    message.header.session_id = UINT64_C(0xfedcba9876543210);
    message.header.epoch = UINT32_MAX;
    message.header.sequence = UINT32_C(0x01020304);
    message.payload.health.uptime_ms = UINT32_C(0x12345678);
    message.payload.health.last_probe_tx_seq = 0u;
    message.payload.health.last_probe_rx_seq = UINT32_MAX;
    message.payload.health.tx_completed = UINT32_C(0x01020304);
    message.payload.health.rx_packets = UINT32_C(0x05060708);
    message.payload.health.rx_queue_drops = 9u;
    message.payload.health.tx_queue_drops = 10u;
    message.payload.health.status_queue_drops = 11u;
    message.payload.health.tx_errors = 12u;
    message.payload.health.rx_rejected = 13u;
    message.payload.health.late_tx_drops = 14u;
    message.payload.health.channel = 6u;
    message.payload.health.present_mask = 5u;
    message.payload.health.firmware_major = 0u;
    message.payload.health.firmware_minor = 2u;
    message.payload.health.firmware_patch = 0u;
    message.payload.health.flags = 3u;
    message.payload.health.last_rx_node = NGN_NODE_C;
    return message;
}

static bool header_equal(const ngn_protocol_header_t *left,
                         const ngn_protocol_header_t *right)
{
    return left->version == right->version && left->type == right->type &&
           left->source == right->source &&
           left->payload_length == right->payload_length &&
           left->session_id == right->session_id && left->epoch == right->epoch &&
           left->sequence == right->sequence;
}

static bool health_equal(const ngn_health_payload_t *left,
                         const ngn_health_payload_t *right)
{
    return left->uptime_ms == right->uptime_ms &&
           left->last_probe_tx_seq == right->last_probe_tx_seq &&
           left->last_probe_rx_seq == right->last_probe_rx_seq &&
           left->tx_completed == right->tx_completed &&
           left->rx_packets == right->rx_packets &&
           left->rx_queue_drops == right->rx_queue_drops &&
           left->tx_queue_drops == right->tx_queue_drops &&
           left->status_queue_drops == right->status_queue_drops &&
           left->tx_errors == right->tx_errors &&
           left->rx_rejected == right->rx_rejected &&
           left->late_tx_drops == right->late_tx_drops &&
           left->channel == right->channel && left->present_mask == right->present_mask &&
           left->firmware_major == right->firmware_major &&
           left->firmware_minor == right->firmware_minor &&
           left->firmware_patch == right->firmware_patch &&
           left->flags == right->flags && left->last_rx_node == right->last_rx_node;
}

static bool message_equal(const ngn_message_t *left, const ngn_message_t *right)
{
    if (!header_equal(&left->header, &right->header)) {
        return false;
    }
    switch (left->header.type) {
    case NGN_MESSAGE_SYNC:
        return left->payload.sync.schedule_version == right->payload.sync.schedule_version &&
               ngn_schedule_config_equal(&left->payload.sync.config, &right->payload.sync.config) &&
               left->payload.sync.epoch_elapsed_ms == right->payload.sync.epoch_elapsed_ms;
    case NGN_MESSAGE_PROBE:
        return left->payload.probe.index == right->payload.probe.index &&
               left->payload.probe.count == right->payload.probe.count;
    case NGN_MESSAGE_NODE_HEALTH:
        return health_equal(&left->payload.health, &right->payload.health);
    default:
        return false;
    }
}

static int golden_case(ngn_message_t message, const uint8_t *expected, size_t size)
{
    uint8_t guarded[NGN_PROTOCOL_MAX_FRAME_SIZE + 2u];
    size_t length = 12345u;
    ngn_message_t decoded;
    size_t i;

    memset(guarded, 0xa5, sizeof(guarded));
    CHECK(ngn_protocol_encode(&message, guarded + 1u,
                             NGN_PROTOCOL_MAX_FRAME_SIZE, &length) == NGN_PROTOCOL_OK);
    CHECK(length == size);
    CHECK(memcmp(guarded + 1u, expected, size) == 0);
    CHECK(guarded[0] == 0xa5u);
    for (i = size + 1u; i < sizeof(guarded); ++i) {
        CHECK(guarded[i] == 0xa5u);
    }
    CHECK(ngn_protocol_decode(expected, size, &decoded) == NGN_PROTOCOL_OK);
    CHECK(message_equal(&message, &decoded));
    return 0;
}

static int expect_rejection(const uint8_t *frame, size_t length,
                            ngn_protocol_result_t expected)
{
    ngn_message_t output;
    uint8_t saved[sizeof(output)];
    memset(&output, 0xa5, sizeof(output));
    memcpy(saved, &output, sizeof(saved));
    CHECK(ngn_protocol_decode(frame, length, &output) == expected);
    /* This byte comparison tests failure non-mutation, not struct equality. */
    CHECK(memcmp(saved, &output, sizeof(saved)) == 0);
    return 0;
}

static int expect_encode_rejection(ngn_message_t message,
                                   ngn_protocol_result_t expected)
{
    uint8_t output[NGN_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t saved[sizeof(output)];
    size_t length = 12345u;
    memset(output, 0xa5, sizeof(output));
    memcpy(saved, output, sizeof(saved));
    CHECK(ngn_protocol_encode(&message, output, sizeof(output), &length) == expected);
    CHECK(length == 12345u);
    CHECK(memcmp(saved, output, sizeof(saved)) == 0);
    return 0;
}

static void repair_crc(uint8_t *frame, size_t length)
{
    /* Golden literals above independently constrain the CRC implementation.
     * Re-signing here isolates semantic validation from corruption checks. */
    const uint16_t crc = ngn_protocol_crc16(frame, length - 2u);
    frame[length - 2u] = (uint8_t)(crc >> 8u);
    frame[length - 1u] = (uint8_t)crc;
}

static int envelope_rejection(void)
{
    uint8_t frame[NGN_PROTOCOL_MAX_FRAME_SIZE + 1u] = {0};
    ngn_message_t output;
    ngn_message_t message = probe_message();
    size_t length = 777u;
    unsigned value;
    size_t i;

    CHECK(ngn_protocol_decode(NULL, 0u, &output) == NGN_PROTOCOL_ARGUMENT);
    CHECK(ngn_protocol_decode(golden_probe, sizeof(golden_probe), NULL) == NGN_PROTOCOL_ARGUMENT);
    CHECK(ngn_protocol_encode(NULL, frame, sizeof(frame), &length) == NGN_PROTOCOL_ARGUMENT);
    CHECK(ngn_protocol_encode(&message, NULL, sizeof(frame), &length) == NGN_PROTOCOL_ARGUMENT);
    CHECK(ngn_protocol_encode(&message, frame, sizeof(frame), NULL) == NGN_PROTOCOL_ARGUMENT);
    CHECK(length == 777u);

    memcpy(frame, golden_probe, sizeof(golden_probe));
    for (i = 0u; i <= sizeof(frame); ++i) {
        if (i != sizeof(golden_probe)) {
            CHECK(expect_rejection(frame, i, NGN_PROTOCOL_LENGTH) == 0);
        }
    }
    for (i = 0u; i < sizeof(golden_probe); ++i) {
        CHECK(ngn_protocol_encode(&message, frame, i, &length) == NGN_PROTOCOL_LENGTH);
        CHECK(length == 777u);
    }
    frame[0] ^= 1u;
    CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_MAGIC_ERROR) == 0);
    memcpy(frame, golden_probe, sizeof(golden_probe));

    for (value = 0u; value <= UINT8_MAX; ++value) {
        if (value != 1u) {
            frame[2] = (uint8_t)value;
            CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_VERSION_ERROR) == 0);
        }
    }
    frame[2] = 1u;
    for (value = 0u; value <= UINT8_MAX; ++value) {
        if (value < 1u || value > 3u) {
            frame[3] = (uint8_t)value;
            CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_TYPE_ERROR) == 0);
        }
    }
    frame[3] = 2u;
    for (value = 3u; value <= UINT8_MAX; ++value) {
        frame[4] = (uint8_t)value;
        CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_SOURCE_ERROR) == 0);
    }
    frame[4] = 1u;
    for (value = 1u; value <= UINT8_MAX; ++value) {
        frame[5] = (uint8_t)value;
        CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_RESERVED_ERROR) == 0);
    }
    frame[5] = 0u;
    for (value = 0u; value <= UINT16_MAX; ++value) {
        if (value != 4u) {
            frame[6] = (uint8_t)(value >> 8u);
            frame[7] = (uint8_t)value;
            CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_LENGTH) == 0);
        }
    }
    memcpy(frame, golden_probe, sizeof(golden_probe));
    memset(frame + 8u, 0, 8u);
    CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_SESSION_ERROR) == 0);

    /* Every single-bit corruption of the golden frame must be rejected. */
    for (i = 0u; i < sizeof(golden_probe); ++i) {
        unsigned bit;
        for (bit = 0u; bit < 8u; ++bit) {
            memcpy(frame, golden_probe, sizeof(golden_probe));
            frame[i] ^= (uint8_t)(1u << bit);
            CHECK(ngn_protocol_decode(frame, sizeof(golden_probe), &output) != NGN_PROTOCOL_OK);
        }
    }
    memcpy(frame, golden_probe, sizeof(golden_probe));
    frame[28] ^= 1u;
    CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_CRC_ERROR) == 0);

    message.header.version = 0u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_VERSION_ERROR) == 0);
    message = probe_message();
    message.header.type = (ngn_message_type_t)256;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_TYPE_ERROR) == 0);
    message = probe_message();
    message.header.source = NGN_NODE_UNCONFIGURED;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_SOURCE_ERROR) == 0);
    message = probe_message();
    message.header.session_id = 0u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_SESSION_ERROR) == 0);
    return 0;
}

static int payload_lengths_and_values(void)
{
    ngn_message_t messages[3] = {sync_message(), probe_message(), health_message()};
    uint8_t frame[NGN_PROTOCOL_MAX_FRAME_SIZE];
    ngn_message_t decoded;
    size_t length;
    size_t type;
    unsigned value;

    CHECK(ngn_protocol_payload_size((ngn_message_type_t)0) == 0u);
    CHECK(ngn_protocol_payload_size((ngn_message_type_t)256) == 0u);
    for (type = 0u; type < 3u; ++type) {
        const ngn_message_t valid = messages[type];
        CHECK(ngn_protocol_payload_size(valid.header.type) == valid.header.payload_length);
        for (value = 0u; value <= UINT16_MAX; ++value) {
            if (value != valid.header.payload_length) {
                messages[type].header.payload_length = (uint16_t)value;
                CHECK(expect_encode_rejection(messages[type], NGN_PROTOCOL_LENGTH) == 0);
            }
        }
        messages[type] = valid;
        messages[type].header.session_id = UINT64_MAX;
        messages[type].header.epoch = 0u;
        messages[type].header.sequence = 0u;
        CHECK(ngn_protocol_encode(&messages[type], frame, sizeof(frame), &length) == NGN_PROTOCOL_OK);
        CHECK(ngn_protocol_decode(frame, length, &decoded) == NGN_PROTOCOL_OK);
        CHECK(message_equal(&messages[type], &decoded));
    }

    for (value = 0u; value <= UINT8_MAX; ++value) {
        ngn_message_t message = probe_message();
        message.payload.probe.count = (uint8_t)value;
        message.payload.probe.index = 0u;
        if (value >= 1u && value <= 16u) {
            unsigned index;
            for (index = 0u; index <= UINT8_MAX; ++index) {
                message.payload.probe.index = (uint8_t)index;
                if (index < value) {
                    CHECK(ngn_protocol_encode(&message, frame, sizeof(frame), &length) == NGN_PROTOCOL_OK);
                    CHECK(ngn_protocol_decode(frame, length, &decoded) == NGN_PROTOCOL_OK);
                    CHECK(message_equal(&message, &decoded));
                } else {
                    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
                }
            }
        } else {
            CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
        }
    }
    memcpy(frame, golden_probe, sizeof(golden_probe));
    frame[24] = frame[25];
    repair_crc(frame, sizeof(golden_probe));
    CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    memcpy(frame, golden_probe, sizeof(golden_probe));
    frame[26] = 1u;
    repair_crc(frame, sizeof(golden_probe));
    CHECK(expect_rejection(frame, sizeof(golden_probe), NGN_PROTOCOL_RESERVED_ERROR) == 0);
    return 0;
}

static int sync_validation(void)
{
    ngn_message_t message = sync_message();
    ngn_message_t decoded;
    uint8_t frame[NGN_PROTOCOL_MAX_FRAME_SIZE];
    size_t length;
    unsigned value;

    message.header.source = NGN_NODE_A;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_SOURCE_ERROR) == 0);
    memcpy(frame, golden_sync, sizeof(golden_sync));
    frame[4] = 1u;
    CHECK(expect_rejection(frame, sizeof(golden_sync), NGN_PROTOCOL_SOURCE_ERROR) == 0);
    message = sync_message();
    for (value = 0u; value <= UINT8_MAX; ++value) {
        if (value != 1u) {
            message.payload.sync.schedule_version = (uint8_t)value;
            CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
        }
    }
    message = sync_message();
    message.payload.sync.config.channel = 0u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    message = sync_message();
    message.payload.sync.epoch_elapsed_ms = 39u;
    CHECK(ngn_protocol_encode(&message, frame, sizeof(frame), &length) == NGN_PROTOCOL_OK);
    CHECK(ngn_protocol_decode(frame, length, &decoded) == NGN_PROTOCOL_OK);
    CHECK(message_equal(&message, &decoded));
    message.payload.sync.epoch_elapsed_ms = 40u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    message.payload.sync.epoch_elapsed_ms = UINT32_MAX;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);

    memcpy(frame, golden_sync, sizeof(golden_sync));
    frame[38] = 1u;
    repair_crc(frame, sizeof(golden_sync));
    CHECK(expect_rejection(frame, sizeof(golden_sync), NGN_PROTOCOL_RESERVED_ERROR) == 0);
    memcpy(frame, golden_sync, sizeof(golden_sync));
    frame[24] = 2u;
    repair_crc(frame, sizeof(golden_sync));
    CHECK(expect_rejection(frame, sizeof(golden_sync), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    memcpy(frame, golden_sync, sizeof(golden_sync));
    frame[25] = 12u;
    repair_crc(frame, sizeof(golden_sync));
    CHECK(expect_rejection(frame, sizeof(golden_sync), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    memcpy(frame, golden_sync, sizeof(golden_sync));
    frame[43] = 40u;
    repair_crc(frame, sizeof(golden_sync));
    CHECK(expect_rejection(frame, sizeof(golden_sync), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    return 0;
}

static int health_validation(void)
{
    ngn_message_t message;
    ngn_message_t decoded;
    uint8_t frame[NGN_PROTOCOL_MAX_FRAME_SIZE];
    size_t length;
    unsigned value;

    for (value = 0u; value <= UINT8_MAX; ++value) {
        message = health_message();
        message.payload.health.channel = (uint8_t)value;
        if (value < 1u || value > 11u) {
            CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
        }
        message = health_message();
        message.payload.health.present_mask = (uint8_t)value;
        if (value > 7u) {
            CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
        }
        message = health_message();
        message.payload.health.flags = (uint8_t)value;
        if (value > 3u) {
            CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
        }
        message = health_message();
        message.payload.health.last_rx_node = (ngn_node_id_t)value;
        if (value > 2u) {
            CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
        }
    }
    message = health_message();
    message.payload.health.flags = 0u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    message.payload.health.last_probe_rx_seq = 0u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    message.payload.health.last_rx_node = NGN_NODE_UNCONFIGURED;
    CHECK(ngn_protocol_encode(&message, frame, sizeof(frame), &length) == NGN_PROTOCOL_OK);
    CHECK(ngn_protocol_decode(frame, length, &decoded) == NGN_PROTOCOL_OK);
    CHECK(message_equal(&message, &decoded));
    message.payload.health.last_probe_tx_seq = 1u;
    CHECK(expect_encode_rejection(message, NGN_PROTOCOL_PAYLOAD_ERROR) == 0);

    /* Sequence zero with a validity flag represents wrap, not missing data. */
    message = health_message();
    message.payload.health.last_probe_rx_seq = 0u;
    CHECK(ngn_protocol_encode(&message, frame, sizeof(frame), &length) == NGN_PROTOCOL_OK);
    CHECK(ngn_protocol_decode(frame, length, &decoded) == NGN_PROTOCOL_OK);
    CHECK(message_equal(&message, &decoded));

    memcpy(frame, golden_health, sizeof(golden_health));
    frame[75] = 1u;
    repair_crc(frame, sizeof(golden_health));
    CHECK(expect_rejection(frame, sizeof(golden_health), NGN_PROTOCOL_RESERVED_ERROR) == 0);
    memcpy(frame, golden_health, sizeof(golden_health));
    frame[73] = 4u;
    repair_crc(frame, sizeof(golden_health));
    CHECK(expect_rejection(frame, sizeof(golden_health), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    memcpy(frame, golden_health, sizeof(golden_health));
    frame[69] = 8u;
    repair_crc(frame, sizeof(golden_health));
    CHECK(expect_rejection(frame, sizeof(golden_health), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    memcpy(frame, golden_health, sizeof(golden_health));
    frame[74] = 0xffu;
    repair_crc(frame, sizeof(golden_health));
    CHECK(expect_rejection(frame, sizeof(golden_health), NGN_PROTOCOL_PAYLOAD_ERROR) == 0);
    return 0;
}

static int crc_and_serial(void)
{
    static const uint8_t check[] = "123456789";
    CHECK(NGN_PROTOCOL_VERSION == 1u);
    CHECK(ngn_protocol_crc16(check, sizeof(check) - 1u) == 0x29b1u);
    CHECK(ngn_protocol_crc16(NULL, 0u) == 0xffffu);
    CHECK(ngn_protocol_crc16(NULL, 1u) == 0u);
    CHECK(!ngn_serial32_newer(0u, 0u));
    CHECK(!ngn_serial32_newer(UINT32_MAX, UINT32_MAX));
    CHECK(ngn_serial32_newer(1u, 0u));
    CHECK(ngn_serial32_newer(0u, UINT32_MAX));
    CHECK(ngn_serial32_newer(1u, UINT32_MAX));
    CHECK(!ngn_serial32_newer(UINT32_MAX, 0u));
    CHECK(!ngn_serial32_newer(UINT32_MAX, 1u));
    CHECK(ngn_serial32_newer(UINT32_C(0x7fffffff), 0u));
    CHECK(!ngn_serial32_newer(UINT32_C(0x80000000), 0u));
    CHECK(!ngn_serial32_newer(0u, UINT32_C(0x80000000)));
    CHECK(!ngn_serial32_newer(UINT32_C(0x80000001), 0u));
    CHECK(ngn_serial32_newer(UINT32_C(0x7ffffffe), UINT32_MAX));
    CHECK(!ngn_serial32_newer(UINT32_C(0x7fffffff), UINT32_MAX));
    return 0;
}

int main(void)
{
    CHECK(crc_and_serial() == 0);
    CHECK(golden_case(probe_message(), golden_probe, sizeof(golden_probe)) == 0);
    CHECK(golden_case(sync_message(), golden_sync, sizeof(golden_sync)) == 0);
    CHECK(golden_case(health_message(), golden_health, sizeof(golden_health)) == 0);
    CHECK(envelope_rejection() == 0);
    CHECK(payload_lengths_and_values() == 0);
    CHECK(sync_validation() == 0);
    CHECK(health_validation() == 0);
    puts("ngn_protocol: v1 golden frames, bounded parsing, payload validation and serial wrap passed");
    return 0;
}

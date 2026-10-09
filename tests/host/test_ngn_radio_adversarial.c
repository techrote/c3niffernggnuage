#include <stdio.h>
#include <string.h>

#include "ngn_radio.h"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                     \
                    __FILE__, __LINE__, #condition);                            \
            return false;                                                       \
        }                                                                       \
    } while (0)

#define MOCK_RX_CAPACITY 64u
#define MOCK_TX_CAPACITY 128u
#define MOCK_STATUS_CAPACITY 64u

static const uint8_t MACS[3][NGN_TRANSPORT_MAC_SIZE] = {
    {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0xa0u},
    {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0xb0u},
    {0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0xc0u}
};
static const uint8_t OTHER_MAC[NGN_TRANSPORT_MAC_SIZE] = {
    0x02u, 0x11u, 0x22u, 0x33u, 0x44u, 0xd0u
};

typedef struct {
    ngn_transport_rx_t rx[MOCK_RX_CAPACITY];
    size_t rx_written;
    size_t rx_read;
    ngn_transport_tx_t tx[MOCK_TX_CAPACITY];
    uint64_t queued_ms[MOCK_TX_CAPACITY];
    size_t tx_count;
    size_t delivered;
    ngn_transport_status_t statuses[MOCK_STATUS_CAPACITY];
    size_t status_written;
    size_t status_read;
    uint64_t now_ms;
    uint64_t active_session;
    size_t session_changes;
    bool auto_complete;
    ngn_transport_stats_t stats;
} mock_t;

typedef struct {
    ngn_radio_t radio;
    mock_t mock;
} fixture_t;

static bool mock_send(void *context, const ngn_transport_tx_t *frame)
{
    mock_t *mock = context;
    if (mock->tx_count >= MOCK_TX_CAPACITY) {
        ++mock->stats.tx_queue_drops;
        return false;
    }
    mock->tx[mock->tx_count] = *frame;
    mock->queued_ms[mock->tx_count++] = mock->now_ms;
    return true;
}

static bool mock_receive(void *context, ngn_transport_rx_t *frame)
{
    mock_t *mock = context;
    if (mock->rx_read == mock->rx_written) {
        return false;
    }
    *frame = mock->rx[mock->rx_read++ % MOCK_RX_CAPACITY];
    return true;
}

static bool mock_poll_status(void *context, ngn_transport_status_t *status)
{
    mock_t *mock = context;
    if (mock->status_read == mock->status_written) {
        return false;
    }
    *status = mock->statuses[mock->status_read++ % MOCK_STATUS_CAPACITY];
    return true;
}

static void mock_get_stats(void *context, ngn_transport_stats_t *stats)
{
    const mock_t *mock = context;
    *stats = mock->stats;
}

static void mock_set_session(void *context, uint64_t session_id)
{
    mock_t *mock = context;
    mock->active_session = session_id;
    ++mock->session_changes;
}

static bool mock_complete(mock_t *mock, uint32_t token,
                           ngn_transport_tx_result_t result)
{
    const ngn_transport_status_t status = {.token = token, .result = result};
    CHECK(mock->status_written - mock->status_read < MOCK_STATUS_CAPACITY);
    mock->statuses[mock->status_written++ % MOCK_STATUS_CAPACITY] = status;
    return true;
}

/* The mock models an adapter that owns a pending queue until explicit dispatch.
 * The core must change its session identity before that dispatch can proceed. */
static bool mock_submit(mock_t *mock, ngn_transport_tx_t *frame,
                          uint64_t now_ms, bool *submitted)
{
    *submitted = false;
    if (frame->session_id != mock->active_session || frame->session_id == 0u) {
        ++mock->stats.tx_canceled;
        return mock_complete(mock, frame->token, NGN_TRANSPORT_TX_CANCELED);
    }
    CHECK(now_ms < frame->expires_ms);
    if (frame->prepare != NULL) {
        CHECK(frame->prepare(frame->bytes, frame->length, now_ms,
                              frame->reference_ms));
    }
    *submitted = true;
    if (mock->auto_complete) {
        ++mock->stats.tx_completed;
        CHECK(mock_complete(mock, frame->token, NGN_TRANSPORT_TX_COMPLETE));
    }
    return true;
}

static bool initialize(fixture_t *fixture, ngn_node_id_t role,
                        uint64_t session, uint64_t now_ms)
{
    ngn_transport_t transport;
    const ngn_schedule_config_t config = ngn_schedule_default_config();
    memset(fixture, 0, sizeof(*fixture));
    transport = (ngn_transport_t){
        .context = &fixture->mock,
        .send = mock_send,
        .receive = mock_receive,
        .poll_status = mock_poll_status,
        .get_stats = mock_get_stats,
        .set_session = mock_set_session
    };
    fixture->mock.now_ms = now_ms;
    return ngn_radio_init(&fixture->radio, role, MACS[(unsigned)role],
                          &config, &transport, session, now_ms, NULL, NULL);
}

static bool service(fixture_t *fixture, uint64_t now_ms)
{
    fixture->mock.now_ms = now_ms;
    return ngn_radio_service(&fixture->radio, now_ms);
}

static ngn_message_t sync_message(uint64_t session, uint32_t epoch,
                                   uint32_t sequence)
{
    ngn_message_t message = {0};
    message.header = (ngn_protocol_header_t){
        .version = NGN_PROTOCOL_VERSION,
        .type = NGN_MESSAGE_SYNC,
        .source = NGN_NODE_C,
        .payload_length = NGN_PROTOCOL_SYNC_PAYLOAD_SIZE,
        .session_id = session,
        .epoch = epoch,
        .sequence = sequence
    };
    message.payload.sync.schedule_version = NGN_SCHEDULE_VERSION;
    message.payload.sync.config = ngn_schedule_default_config();
    return message;
}

static ngn_message_t probe_message(ngn_node_id_t source, uint64_t session,
                                    uint32_t epoch, uint32_t sequence)
{
    ngn_message_t message = {0};
    message.header = (ngn_protocol_header_t){
        .version = NGN_PROTOCOL_VERSION,
        .type = NGN_MESSAGE_PROBE,
        .source = source,
        .payload_length = NGN_PROTOCOL_PROBE_PAYLOAD_SIZE,
        .session_id = session,
        .epoch = epoch,
        .sequence = sequence
    };
    message.payload.probe.count = 4u;
    return message;
}

static bool enqueue_raw(mock_t *mock, const uint8_t *mac,
                         const uint8_t *bytes, size_t length,
                         uint64_t received_ms)
{
    ngn_transport_rx_t frame = {0};
    if (length > sizeof(frame.bytes) ||
        mock->rx_written - mock->rx_read >= MOCK_RX_CAPACITY) {
        return false;
    }
    memcpy(frame.source_mac, mac, sizeof(frame.source_mac));
    memcpy(frame.bytes, bytes, length);
    frame.length = length;
    frame.received_ms = received_ms;
    mock->rx[mock->rx_written++ % MOCK_RX_CAPACITY] = frame;
    return true;
}

static bool receive_message(fixture_t *fixture, const ngn_message_t *message,
                             const uint8_t *mac, uint64_t received_ms,
                             uint64_t processed_ms)
{
    uint8_t frame[NGN_PROTOCOL_MAX_FRAME_SIZE];
    size_t length = 0u;
    CHECK(ngn_protocol_encode(message, frame, sizeof(frame), &length) ==
          NGN_PROTOCOL_OK);
    CHECK(enqueue_raw(&fixture->mock, mac, frame, length, received_ms));
    return service(fixture, processed_ms);
}

static bool test_rejections_do_not_poison_identity_or_sequence(void)
{
    fixture_t fixture;
    ngn_message_t message;
    CHECK(initialize(&fixture, NGN_NODE_A, 0u, 1000u));
    message = sync_message(11u, 10u, 100u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1000u, 1000u));
    CHECK(fixture.radio.state == NGN_RADIO_SYNCHRONIZED);

    message = probe_message(NGN_NODE_B, 11u, 11u, 1000000u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_B], 1001u, 1001u));
    CHECK(!fixture.radio.peers[NGN_NODE_B].bound);
    CHECK(fixture.radio.epoch == 10u);

    message.header.epoch = 10u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_A], 1002u, 1002u));
    CHECK(!fixture.radio.peers[NGN_NODE_B].bound);

    message.payload.probe.count = 3u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_B], 1003u, 1003u));
    CHECK(!fixture.radio.peers[NGN_NODE_B].bound);

    message.payload.probe.count = 4u;
    message.header.sequence = 101u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_B], 1004u, 1004u));
    CHECK(fixture.radio.peers[NGN_NODE_B].bound);
    CHECK(fixture.radio.peers[NGN_NODE_B].last_sequence == 101u);
    message.header.sequence = UINT32_C(0x10000000);
    CHECK(receive_message(&fixture, &message, OTHER_MAC, 1005u, 1005u));
    CHECK(fixture.radio.peers[NGN_NODE_B].last_sequence == 101u);
    CHECK(fixture.radio.peers[NGN_NODE_B].last_seen_ms == 1004u);
    message.header.sequence = 102u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_B], 1006u, 1006u));
    CHECK(fixture.radio.peers[NGN_NODE_B].last_sequence == 102u);

    message = sync_message(22u, 0u, 0u);
    CHECK(receive_message(&fixture, &message, OTHER_MAC, 1007u, 1007u));
    CHECK(fixture.radio.session_id == 11u);
    CHECK(fixture.radio.peers[NGN_NODE_C].last_sequence == 100u);

    message = sync_message(11u, 11u, 1000000u);
    ++message.payload.sync.config.coexist_ms;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1390u, 1390u));
    CHECK(fixture.radio.last_sync_epoch == 10u);
    CHECK(fixture.radio.peers[NGN_NODE_C].last_sequence == 100u);
    message = sync_message(11u, 11u, 101u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1391u, 1391u));
    CHECK(fixture.radio.last_sync_epoch == 11u);
    CHECK(fixture.radio.peers[NGN_NODE_C].last_sequence == 101u);
    CHECK(fixture.radio.config.coexist_ms == 80u);
    return true;
}

static bool test_wrap_half_range_and_retired_session(void)
{
    fixture_t fixture;
    ngn_message_t message;
    CHECK(initialize(&fixture, NGN_NODE_B, 0u, 100u));
    message = sync_message(101u, UINT32_MAX - 1u, UINT32_MAX - 1u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 100u, 100u));
    message = sync_message(101u, UINT32_MAX, UINT32_MAX);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 490u, 490u));
    message = sync_message(101u, 0u, 0u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 880u, 880u));
    CHECK(fixture.radio.last_sync_epoch == 0u);
    CHECK(fixture.radio.peers[NGN_NODE_C].last_sequence == 0u);

    message = sync_message(101u, 0u, 17u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 881u, 881u));
    CHECK(fixture.radio.last_sync_ms == 880u);
    message = sync_message(101u, UINT32_C(0x80000000), 1u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 882u, 882u));
    CHECK(fixture.radio.last_sync_epoch == 0u);
    message = sync_message(101u, 1u, UINT32_C(0x80000000));
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 883u, 883u));
    CHECK(fixture.radio.last_sync_epoch == 0u);
    CHECK(fixture.radio.peers[NGN_NODE_C].last_sequence == 0u);
    message = sync_message(101u, 1u, 1u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1270u, 1270u));
    CHECK(fixture.radio.last_sync_epoch == 1u);

    message = sync_message(202u, 0u, 0u);
    message.payload.sync.config.coexist_ms = 0u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1280u, 1280u));
    CHECK(fixture.radio.session_id == 202u);
    CHECK(fixture.radio.config.coexist_ms == 0u);
    CHECK(fixture.radio.plan.epoch_ms == 310u);

    message = sync_message(101u, 2u, UINT32_C(0x70000000));
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1281u, 1281u));
    CHECK(fixture.radio.session_id == 202u);
    CHECK(fixture.radio.last_sync_ms == 1280u);
    CHECK(fixture.radio.peers[NGN_NODE_C].last_sequence == 0u);
    message = sync_message(202u, 1u, 1u);
    message.payload.sync.config.coexist_ms = 0u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1590u, 1590u));
    CHECK(fixture.radio.last_sync_epoch == 1u);
    return true;
}

static bool test_same_mac_rejoin_at_missing_threshold(void)
{
    fixture_t fixture;
    ngn_message_t message;
    CHECK(initialize(&fixture, NGN_NODE_C, 77u, 1000u));
    message = probe_message(NGN_NODE_A, 77u, 0u, 100u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_A], 1040u, 1040u));
    CHECK(fixture.radio.peers[NGN_NODE_A].present);
    CHECK(service(&fixture, 2599u));
    CHECK(fixture.radio.peers[NGN_NODE_A].present);

    /* Four complete 390 ms periods since the last accepted observation. */
    message = probe_message(NGN_NODE_A, 77u, 4u, 0u);
    CHECK(receive_message(&fixture, &message, OTHER_MAC, 2600u, 2600u));
    CHECK(!fixture.radio.peers[NGN_NODE_A].present);
    CHECK(fixture.radio.peers[NGN_NODE_A].bound);
    CHECK(memcmp(fixture.radio.peers[NGN_NODE_A].mac,
                  MACS[NGN_NODE_A], NGN_TRANSPORT_MAC_SIZE) == 0);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_A], 2601u, 2601u));
    CHECK(fixture.radio.peers[NGN_NODE_A].present);
    CHECK(fixture.radio.peers[NGN_NODE_A].last_sequence == 0u);
    CHECK(fixture.radio.peers[NGN_NODE_A].last_seen_ms == 2601u);
    return true;
}

static bool test_silent_wait_timeout_and_fresh_sync_rejoin(void)
{
    fixture_t fixture;
    ngn_message_t message;
    size_t sends_after_wait;
    CHECK(initialize(&fixture, NGN_NODE_A, 0u, 1000u));
    message = sync_message(77u, 0u, 0u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1000u, 1000u));
    CHECK(service(&fixture, 1389u));
    CHECK(fixture.radio.state == NGN_RADIO_SYNCHRONIZED);
    CHECK(service(&fixture, 1390u));
    CHECK(fixture.radio.state == NGN_RADIO_WAIT_SYNC);
    sends_after_wait = fixture.mock.tx_count;

    message = probe_message(NGN_NODE_C, 77u, 1u, 1000u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1400u, 1400u));
    CHECK(fixture.radio.state == NGN_RADIO_WAIT_SYNC);
    CHECK(fixture.radio.last_sync_epoch == 0u);
    CHECK(service(&fixture, 2559u));
    CHECK(fixture.radio.state == NGN_RADIO_WAIT_SYNC);
    CHECK(service(&fixture, 2560u));
    CHECK(fixture.radio.state == NGN_RADIO_DISCOVERING);
    CHECK(fixture.radio.stats.sync_timeouts == 1u);
    CHECK(fixture.mock.tx_count == sends_after_wait);

    /* Capture time, rather than queue-drain time, must control freshness. */
    message = sync_message(77u, 4u, 10000u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1000u, 2561u));
    CHECK(fixture.radio.state == NGN_RADIO_DISCOVERING);
    CHECK(fixture.radio.last_sync_ms == 1000u);
    message = sync_message(77u, 4u, 1u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 2600u, 2600u));
    CHECK(fixture.radio.state == NGN_RADIO_SYNCHRONIZED);
    CHECK(fixture.radio.last_sync_epoch == 4u);
    CHECK(fixture.radio.last_sync_ms == 2600u);
    return true;
}

static bool test_future_old_and_backwards_timestamps(void)
{
    fixture_t fixture;
    ngn_message_t message = sync_message(55u, 0u, 0u);
    CHECK(initialize(&fixture, NGN_NODE_A, 0u, 0u));
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 100u, 50u));
    CHECK(fixture.radio.state == NGN_RADIO_DISCOVERING);
    CHECK(!fixture.radio.peers[NGN_NODE_C].bound);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 0u, 1560u));
    CHECK(fixture.radio.state == NGN_RADIO_DISCOVERING);
    CHECK(!fixture.radio.peers[NGN_NODE_C].bound);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1561u, 1561u));
    CHECK(fixture.radio.state == NGN_RADIO_SYNCHRONIZED);
    CHECK(fixture.radio.last_sync_ms == 1561u);
    CHECK(!service(&fixture, 1560u));
    CHECK(fixture.radio.last_sync_ms == 1561u);
    CHECK(fixture.radio.last_service_ms == 1561u);
    CHECK(fixture.radio.stats.clock_rejections == 1u);
    return true;
}

static bool test_delayed_sync_preparation_uses_captured_epoch(void)
{
    fixture_t coordinator;
    fixture_t follower;
    ngn_transport_tx_t queued;
    ngn_message_t decoded;
    CHECK(initialize(&coordinator, NGN_NODE_C, 500u, 1000u));
    CHECK(service(&coordinator, 1000u));
    CHECK(coordinator.mock.tx_count == 1u);
    queued = coordinator.mock.tx[0];
    CHECK(queued.prepare != NULL);
    CHECK(queued.reference_ms == 1000u);
    CHECK(queued.expires_ms == 1040u);
    CHECK(queued.prepare(queued.bytes, queued.length, 1027u, queued.reference_ms));
    CHECK(ngn_protocol_decode(queued.bytes, queued.length, &decoded) ==
          NGN_PROTOCOL_OK);
    CHECK(decoded.header.epoch == 0u);
    CHECK(decoded.payload.sync.epoch_elapsed_ms == 27u);

    CHECK(initialize(&follower, NGN_NODE_A, 0u, 5000u));
    CHECK(enqueue_raw(&follower.mock, MACS[NGN_NODE_C], queued.bytes,
                       queued.length, 5029u));
    CHECK(service(&follower, 5034u));
    CHECK(follower.radio.epoch_start_ms == 5002u);
    CHECK(follower.radio.last_sync_ms == 5029u);
    CHECK(service(&follower, 5042u));
    CHECK(follower.mock.tx_count == 1u);
    CHECK(ngn_protocol_decode(follower.mock.tx[0].bytes,
                              follower.mock.tx[0].length, &decoded) ==
          NGN_PROTOCOL_OK);
    CHECK(decoded.header.type == NGN_MESSAGE_PROBE);
    CHECK(decoded.payload.probe.index == 0u);

    /* A queued hook never reads the coordinator's subsequently advanced state. */
    CHECK(service(&coordinator, 1390u));
    CHECK(coordinator.radio.epoch == 1u);
    CHECK(queued.prepare(queued.bytes, queued.length, 1030u, queued.reference_ms));
    CHECK(ngn_protocol_decode(queued.bytes, queued.length, &decoded) ==
          NGN_PROTOCOL_OK);
    CHECK(decoded.header.epoch == 0u);
    CHECK(decoded.payload.sync.epoch_elapsed_ms == 30u);
    CHECK(!queued.prepare(queued.bytes, queued.length, 1040u, queued.reference_ms));
    CHECK(!queued.prepare(queued.bytes, queued.length, 999u, queued.reference_ms));
    return true;
}

static bool test_bounded_drain_and_no_late_burst(void)
{
    fixture_t fixture;
    uint8_t malformed[4] = {0};
    size_t i;
    CHECK(initialize(&fixture, NGN_NODE_C, 1u, 0u));
    for (i = 0u; i < 32u; ++i) {
        const ngn_transport_status_t status = {
            .token = (uint32_t)i,
            .result = NGN_TRANSPORT_TX_COMPLETE
        };
        CHECK(enqueue_raw(&fixture.mock, MACS[NGN_NODE_A], malformed,
                           sizeof(malformed), 0u));
        fixture.mock.statuses[fixture.mock.status_written++] = status;
    }
    CHECK(service(&fixture, 0u));
    CHECK(fixture.mock.rx_read == NGN_RADIO_RX_BUDGET);
    CHECK(fixture.mock.status_read == NGN_RADIO_STATUS_BUDGET);
    CHECK(service(&fixture, 1u));
    CHECK(fixture.mock.rx_read == 32u);
    CHECK(fixture.mock.status_read == 32u);

    /* A worker first visiting C's slot after every probe deadline sends none. */
    CHECK(service(&fixture, 280u));
    CHECK(fixture.mock.tx_count == 1u); /* Only the initial SYNC. */
    CHECK(fixture.radio.stats.tx_late_drops >= 4u);
    CHECK(service(&fixture, UINT64_C(390000000000)));
    CHECK(fixture.radio.epoch == UINT32_C(1000000000));
    CHECK(fixture.mock.tx_count == 2u); /* One current SYNC, no replayed epochs. */
    return true;
}

static bool test_session_change_invalidates_pending_transport_work(void)
{
    fixture_t fixture;
    ngn_message_t message;
    ngn_transport_tx_t pending;
    bool submitted;
    CHECK(initialize(&fixture, NGN_NODE_A, 0u, 1000u));
    CHECK(fixture.mock.active_session == 0u);
    CHECK(fixture.mock.session_changes == 1u);
    message = sync_message(55u, 0u, 0u);
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1000u, 1000u));
    CHECK(fixture.mock.active_session == 55u);
    CHECK(service(&fixture, 1040u));
    CHECK(fixture.mock.tx_count == 1u);
    pending = fixture.mock.tx[0];
    CHECK(pending.session_id == 55u);
    CHECK(pending.expires_ms == 1050u);

    message = sync_message(66u, 0u, 0u);
    message.payload.sync.config.coexist_ms = 0u;
    CHECK(receive_message(&fixture, &message, MACS[NGN_NODE_C], 1041u, 1041u));
    CHECK(fixture.mock.active_session == 66u);
    CHECK(fixture.mock.session_changes == 3u);
    CHECK(mock_submit(&fixture.mock, &pending, 1042u, &submitted));
    CHECK(!submitted); /* The former deadline has not yet expired. */
    CHECK(fixture.mock.stats.tx_canceled == 1u);
    CHECK(service(&fixture, 1081u));
    CHECK(fixture.radio.stats.tx_status_canceled == 1u);
    CHECK(fixture.mock.tx_count == 2u);
    pending = fixture.mock.tx[1];
    CHECK(pending.session_id == 66u);
    CHECK(mock_submit(&fixture.mock, &pending, 1081u, &submitted));
    CHECK(submitted);
    return true;
}

static bool deliver_bus(fixture_t fixtures[3], ngn_node_id_t source,
                         uint64_t now_ms)
{
    mock_t *sender = &fixtures[(unsigned)source].mock;
    while (sender->delivered < sender->tx_count) {
        ngn_transport_tx_t frame = sender->tx[sender->delivered++];
        unsigned target;
        bool submitted;
        CHECK(mock_submit(sender, &frame, now_ms, &submitted));
        CHECK(submitted);
        for (target = 0u; target < 3u; ++target) {
            if (target != (unsigned)source) {
                CHECK(enqueue_raw(&fixtures[target].mock, MACS[(unsigned)source],
                                   frame.bytes, frame.length, now_ms));
            }
        }
    }
    return true;
}

static bool test_three_node_bus_obeys_nominal_contract(void)
{
    fixture_t fixtures[3];
    uint64_t now;
    unsigned node;
    for (node = 0u; node < 3u; ++node) {
        CHECK(initialize(&fixtures[node], (ngn_node_id_t)node,
                           node == (unsigned)NGN_NODE_C ? 99u : 0u, 10000u));
        fixtures[node].mock.auto_complete = true;
    }
    for (now = 10000u; now < 10780u; ++now) {
        CHECK(service(&fixtures[NGN_NODE_C], now));
        CHECK(deliver_bus(fixtures, NGN_NODE_C, now));
        CHECK(service(&fixtures[NGN_NODE_A], now));
        CHECK(deliver_bus(fixtures, NGN_NODE_A, now));
        CHECK(service(&fixtures[NGN_NODE_B], now));
        CHECK(deliver_bus(fixtures, NGN_NODE_B, now));
        CHECK(service(&fixtures[NGN_NODE_C], now));
    }
    for (node = 0u; node < 3u; ++node) {
        const mock_t *mock = &fixtures[node].mock;
        size_t index;
        unsigned probes = 0u;
        unsigned health = 0u;
        unsigned syncs = 0u;
        CHECK(ngn_radio_present_mask(&fixtures[node].radio) == 7u);
        CHECK(fixtures[node].radio.stats.rx_rejected == 0u);
        CHECK(mock->tx_count == (node == (unsigned)NGN_NODE_C ? 12u : 10u));
        for (index = 0u; index < mock->tx_count; ++index) {
            ngn_message_t message;
            uint64_t offset;
            CHECK(ngn_protocol_decode(mock->tx[index].bytes,
                                      mock->tx[index].length, &message) ==
                  NGN_PROTOCOL_OK);
            CHECK(message.header.source == (ngn_node_id_t)node);
            CHECK(message.header.session_id == 99u);
            CHECK(message.header.epoch <= 1u);
            CHECK(message.header.sequence == (uint32_t)index);
            offset = mock->queued_ms[index] - 10000u -
                     (uint64_t)message.header.epoch * 390u;
            if (message.header.type == NGN_MESSAGE_PROBE) {
                CHECK(offset == 40u + 80u * node +
                      10u * message.payload.probe.index);
                ++probes;
            } else if (message.header.type == NGN_MESSAGE_NODE_HEALTH) {
                CHECK(offset == 360u + 10u * node);
                CHECK(message.payload.health.channel == 6u);
                ++health;
            } else {
                CHECK(node == (unsigned)NGN_NODE_C);
                CHECK(offset == 0u);
                ++syncs;
            }
        }
        CHECK(probes == 8u);
        CHECK(health == 2u);
        CHECK(syncs == (node == (unsigned)NGN_NODE_C ? 2u : 0u));
    }
    return true;
}

int main(void)
{
    if (!test_rejections_do_not_poison_identity_or_sequence() ||
        !test_wrap_half_range_and_retired_session() ||
        !test_same_mac_rejoin_at_missing_threshold() ||
        !test_silent_wait_timeout_and_fresh_sync_rejoin() ||
        !test_future_old_and_backwards_timestamps() ||
        !test_delayed_sync_preparation_uses_captured_epoch() ||
        !test_bounded_drain_and_no_late_burst() ||
        !test_session_change_invalidates_pending_transport_work() ||
        !test_three_node_bus_obeys_nominal_contract()) {
        return 1;
    }
    puts("ngn_radio adversarial and three-node mock-bus tests passed");
    return 0;
}

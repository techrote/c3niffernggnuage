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

#define RX_CAPACITY 16u
#define TX_CAPACITY 8u
#define STATUS_CAPACITY 8u
#define TEST_SESSION UINT64_C(0x9012345678abcdef)

static const uint8_t macs[3][NGN_TRANSPORT_MAC_SIZE] = {
    {0x02u, 0x90u, 0x20u, 0x30u, 0x40u, 0x01u},
    {0x02u, 0x90u, 0x20u, 0x30u, 0x40u, 0x02u},
    {0x02u, 0x90u, 0x20u, 0x30u, 0x40u, 0x03u}
};

typedef struct {
    ngn_radio_t radio;
    ngn_transport_rx_t rx[RX_CAPACITY];
    ngn_transport_tx_t tx[TX_CAPACITY];
    ngn_transport_status_t statuses[STATUS_CAPACITY];
    size_t rx_head, rx_count, tx_head, tx_count, status_head, status_count;
    size_t tx_limit, status_limit;
    uint64_t active_session;
    bool fail_driver;
    ngn_transport_stats_t stats;
    uint32_t present_events[3];
    uint32_t missing_events[3];
    uint32_t coexist_events;
    uint32_t probe_rx_events;
    ngn_radio_event_t last_probe_rx;
} node_t;

typedef struct {
    node_t nodes[3];
    bool online[3];
    uint64_t now_ms;
    uint32_t sent[3][4];
    uint32_t prepare_calls[3];
    ngn_message_t latest[3];
    ngn_health_payload_t latest_health[3];
} bus_t;

/* All fixtures are synthetic. No driver, BLE, CSI or display entry point is
 * linked into this test's transport; opportunities are counted as events only. */
static bus_t bus;

static bool mock_send(void *context, const ngn_transport_tx_t *frame)
{
    node_t *node = context;
    if (node->tx_count == node->tx_limit) {
        ++node->stats.tx_queue_drops;
        return false;
    }
    node->tx[(node->tx_head + node->tx_count) % TX_CAPACITY] = *frame;
    ++node->tx_count;
    return true;
}

static bool mock_receive(void *context, ngn_transport_rx_t *frame)
{
    node_t *node = context;
    if (node->rx_count == 0u) {
        return false;
    }
    *frame = node->rx[node->rx_head];
    node->rx_head = (node->rx_head + 1u) % RX_CAPACITY;
    --node->rx_count;
    return true;
}

static bool mock_status(void *context, ngn_transport_status_t *status)
{
    node_t *node = context;
    if (node->status_count == 0u) {
        return false;
    }
    *status = node->statuses[node->status_head];
    node->status_head = (node->status_head + 1u) % STATUS_CAPACITY;
    --node->status_count;
    return true;
}

static void mock_stats(void *context, ngn_transport_stats_t *stats)
{
    *stats = ((const node_t *)context)->stats;
}

static void mock_set_session(void *context, uint64_t session)
{
    ((node_t *)context)->active_session = session;
}

static ngn_transport_t transport_for(node_t *node)
{
    const ngn_transport_t transport = {
        .context = node,
        .send = mock_send,
        .receive = mock_receive,
        .poll_status = mock_status,
        .get_stats = mock_stats,
        .set_session = mock_set_session
    };
    return transport;
}

static void observe_event(void *context, const ngn_radio_event_t *event)
{
    node_t *node = context;
    if (event->kind == NGN_RADIO_EVENT_SCHEDULE &&
        event->schedule.kind == NGN_SCHEDULE_COEXIST_OPPORTUNITY) {
        ++node->coexist_events;
    } else if (event->kind == NGN_RADIO_EVENT_PROBE_RX) {
        ++node->probe_rx_events;
        node->last_probe_rx = *event;
    } else if (ngn_node_id_is_valid(event->node)) {
        if (event->kind == NGN_RADIO_EVENT_PRESENT) {
            ++node->present_events[(size_t)event->node];
        } else if (event->kind == NGN_RADIO_EVENT_MISSING) {
            ++node->missing_events[(size_t)event->node];
        }
    }
}

static bool initialize_node(ngn_node_id_t id,
                             const ngn_schedule_config_t *config)
{
    node_t *node = &bus.nodes[(size_t)id];
    ngn_transport_t transport;
    memset(node, 0, sizeof(*node));
    node->tx_limit = TX_CAPACITY;
    node->status_limit = STATUS_CAPACITY;
    transport = transport_for(node);
    return ngn_radio_init(&node->radio, id, macs[(size_t)id], config,
                           &transport, id == NGN_NODE_C ? TEST_SESSION : 0u,
                           bus.now_ms, observe_event, node);
}

static bool initialize_bus(const ngn_schedule_config_t *coordinator_config)
{
    const ngn_schedule_config_t defaults = ngn_schedule_default_config();
    size_t i;
    memset(&bus, 0, sizeof(bus));
    bus.now_ms = 1000u;
    for (i = 0u; i < 3u; ++i) {
        bus.online[i] = true;
        CHECK(initialize_node((ngn_node_id_t)i,
                               i == (size_t)NGN_NODE_C ? coordinator_config :
                                                          &defaults));
    }
    return true;
}

static bool enqueue_rx(node_t *node, const ngn_transport_rx_t *frame)
{
    if (node->rx_count == RX_CAPACITY) {
        ++node->stats.rx_queue_drops;
        return false;
    }
    node->rx[(node->rx_head + node->rx_count) % RX_CAPACITY] = *frame;
    ++node->rx_count;
    return true;
}

static void enqueue_status(node_t *node, uint32_t token,
                            ngn_transport_tx_result_t result)
{
    const ngn_transport_status_t status = {.token = token, .result = result};
    if (node->status_count == node->status_limit) {
        ++node->stats.status_queue_drops;
        return;
    }
    node->statuses[(node->status_head + node->status_count) % STATUS_CAPACITY] =
        status;
    ++node->status_count;
}

static bool dispatch(ngn_node_id_t id)
{
    node_t *node = &bus.nodes[(size_t)id];
    size_t budget;
    for (budget = 0u; budget < TX_CAPACITY && node->tx_count != 0u; ++budget) {
        ngn_transport_tx_t frame = node->tx[node->tx_head];
        ngn_message_t decoded;
        ngn_transport_tx_result_t result = NGN_TRANSPORT_TX_COMPLETE;
        size_t receiver;
        node->tx_head = (node->tx_head + 1u) % TX_CAPACITY;
        --node->tx_count;
        if (frame.session_id == 0u || frame.session_id != node->active_session) {
            ++node->stats.tx_canceled;
            result = NGN_TRANSPORT_TX_CANCELED;
        } else if (bus.now_ms >= frame.expires_ms) {
            ++node->stats.tx_expired;
            result = NGN_TRANSPORT_TX_EXPIRED;
        } else {
            if (frame.prepare != NULL) {
                ++bus.prepare_calls[(size_t)id];
                if (!frame.prepare(frame.bytes, frame.length, bus.now_ms,
                                    frame.reference_ms)) {
                    result = NGN_TRANSPORT_TX_FAILED;
                }
            }
            if (node->fail_driver) {
                result = NGN_TRANSPORT_TX_FAILED;
            }
            if (result == NGN_TRANSPORT_TX_FAILED) {
                ++node->stats.tx_errors;
            } else {
                CHECK(ngn_protocol_decode(frame.bytes, frame.length, &decoded) ==
                      NGN_PROTOCOL_OK);
                CHECK(decoded.header.source == id);
                CHECK(decoded.header.session_id == frame.session_id);
                CHECK(decoded.header.type >= NGN_MESSAGE_SYNC &&
                      decoded.header.type <= NGN_MESSAGE_NODE_HEALTH);
                ++bus.sent[(size_t)id][(size_t)decoded.header.type];
                bus.latest[(size_t)id] = decoded;
                if (decoded.header.type == NGN_MESSAGE_NODE_HEALTH) {
                    bus.latest_health[(size_t)id] = decoded.payload.health;
                }
                ++node->stats.tx_completed;
                for (receiver = 0u; receiver < 3u; ++receiver) {
                    ngn_transport_rx_t rx = {0};
                    if (!bus.online[receiver] || receiver == (size_t)id) {
                        continue;
                    }
                    memcpy(rx.source_mac, macs[(size_t)id], sizeof(rx.source_mac));
                    memcpy(rx.bytes, frame.bytes, frame.length);
                    rx.length = frame.length;
                    rx.received_ms = bus.now_ms;
                    (void)enqueue_rx(&bus.nodes[receiver], &rx);
                }
            }
        }
        enqueue_status(node, frame.token, result);
    }
    return true;
}

static bool run_until(uint64_t end_ms)
{
    static const ngn_node_id_t order[3] = {NGN_NODE_C, NGN_NODE_A, NGN_NODE_B};
    for (; bus.now_ms < end_ms; ++bus.now_ms) {
        size_t i;
        for (i = 0u; i < 3u; ++i) {
            const ngn_node_id_t id = order[i];
            if (bus.online[(size_t)id]) {
                CHECK(ngn_radio_service(&bus.nodes[(size_t)id].radio, bus.now_ms));
                CHECK(dispatch(id));
            }
        }
    }
    return true;
}

static bool test_initialization_validation(void)
{
    const ngn_schedule_config_t config = ngn_schedule_default_config();
    ngn_schedule_config_t bad_config = config;
    node_t node = {0};
    ngn_radio_t radio = {0};
    ngn_transport_t transport = transport_for(&node);
    uint8_t before[sizeof(radio)];
    const uint8_t multicast[6] = {1u, 2u, 3u, 4u, 5u, 6u};
    const uint8_t zero_mac[6] = {0};
    unsigned missing;
    CHECK(ngn_radio_init(&radio, NGN_NODE_A, macs[0], &config, &transport,
                          0u, 1000u, NULL, NULL));
    CHECK(radio.state == NGN_RADIO_DISCOVERING);
    CHECK(ngn_radio_present_mask(&radio) == 1u);
    CHECK(node.active_session == 0u);
    memcpy(before, &radio, sizeof(before));
    CHECK(!ngn_radio_init(NULL, NGN_NODE_A, macs[0], &config, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_UNCONFIGURED, macs[0], &config,
                           &transport, 0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, multicast, &config, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, zero_mac, &config, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, NULL, &config, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_C, macs[2], &config, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, macs[0], &config, &transport,
                           TEST_SESSION, 1000u, NULL, NULL));
    bad_config.probe_slot_ms = 1u;
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, macs[0], &bad_config, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, macs[0], NULL, &transport,
                           0u, 1000u, NULL, NULL));
    CHECK(!ngn_radio_init(&radio, NGN_NODE_A, macs[0], &config, NULL,
                           0u, 1000u, NULL, NULL));
    for (missing = 0u; missing < 5u; ++missing) {
        ngn_transport_t incomplete = transport;
        switch (missing) {
        case 0u: incomplete.send = NULL; break;
        case 1u: incomplete.receive = NULL; break;
        case 2u: incomplete.poll_status = NULL; break;
        case 3u: incomplete.get_stats = NULL; break;
        default: incomplete.set_session = NULL; break;
        }
        CHECK(!ngn_radio_init(&radio, NGN_NODE_A, macs[0], &config, &incomplete,
                               0u, 1000u, NULL, NULL));
    }
    /* Byte comparison here checks the promised unchanged output object, not
     * semantic equality between separately initialized padded structures. */
    CHECK(memcmp(before, &radio, sizeof(before)) == 0);
    CHECK(ngn_radio_peer(NULL, NGN_NODE_A) == NULL);
    CHECK(ngn_radio_peer(&radio, NGN_NODE_UNCONFIGURED) == NULL);
    CHECK(ngn_radio_present_mask(NULL) == 0u);
    return true;
}

static bool test_missing_node_health_and_rejoin(void)
{
    const ngn_schedule_config_t config = ngn_schedule_default_config();
    uint32_t before[3][4];
    size_t id;
    CHECK(initialize_bus(&config));
    CHECK(run_until(1000u + 3u * 390u));
    for (id = 0u; id < 3u; ++id) {
        CHECK(ngn_radio_present_mask(&bus.nodes[id].radio) == 7u);
    }
    memcpy(before, bus.sent, sizeof(before));
    bus.online[NGN_NODE_B] = false;
    CHECK(run_until(bus.now_ms + 6u * 390u));
    CHECK(memcmp(before[NGN_NODE_B], bus.sent[NGN_NODE_B],
                  sizeof(before[NGN_NODE_B])) == 0);
    for (id = 0u; id < 3u; id += 2u) {
        const ngn_radio_peer_t *absent =
            ngn_radio_peer(&bus.nodes[id].radio, NGN_NODE_B);
        CHECK(ngn_radio_present_mask(&bus.nodes[id].radio) == 5u);
        CHECK(absent != NULL && absent->bound && !absent->present);
        CHECK(memcmp(absent->mac, macs[NGN_NODE_B], sizeof(absent->mac)) == 0);
        CHECK(bus.nodes[id].missing_events[NGN_NODE_B] == 1u);
        CHECK(bus.latest_health[id].present_mask == 5u);
        CHECK(bus.sent[id][NGN_MESSAGE_PROBE] - before[id][NGN_MESSAGE_PROBE] ==
              24u);
        CHECK(bus.sent[id][NGN_MESSAGE_NODE_HEALTH] -
              before[id][NGN_MESSAGE_NODE_HEALTH] == 6u);
    }
    CHECK(bus.sent[NGN_NODE_C][NGN_MESSAGE_SYNC] -
          before[NGN_NODE_C][NGN_MESSAGE_SYNC] == 6u);

    /* A real reboot loses B's RAM/sequence; A and C keep its validated MAC. */
    CHECK(initialize_node(NGN_NODE_B, &config));
    bus.online[NGN_NODE_B] = true;
    CHECK(run_until(bus.now_ms + 2u * 390u));
    for (id = 0u; id < 3u; ++id) {
        size_t peer;
        CHECK(ngn_radio_present_mask(&bus.nodes[id].radio) == 7u);
        CHECK(bus.latest_health[id].present_mask == 7u);
        CHECK(bus.nodes[id].radio.session_id == TEST_SESSION);
        for (peer = 0u; peer < 3u; ++peer) {
            const ngn_radio_peer_t *mapped =
                ngn_radio_peer(&bus.nodes[id].radio, (ngn_node_id_t)peer);
            CHECK(mapped != NULL && mapped->bound && mapped->present);
            CHECK(memcmp(mapped->mac, macs[peer], sizeof(mapped->mac)) == 0);
        }
    }
    CHECK(bus.nodes[NGN_NODE_A].present_events[NGN_NODE_B] == 2u);
    CHECK(bus.nodes[NGN_NODE_C].present_events[NGN_NODE_B] == 2u);
    return true;
}

static bool test_coordinator_schedule_adoption(void)
{
    ngn_schedule_config_t config = ngn_schedule_default_config();
    size_t id;
    config.burst_count = 2u;
    config.probe_spacing_ms = 7u;
    config.sync_slot_ms = 20u;
    config.probe_slot_ms = 30u;
    config.coexist_ms = 17u;
    config.health_slot_ms = 5u;
    CHECK(initialize_bus(&config));
    CHECK(bus.nodes[NGN_NODE_A].radio.config.burst_count == 4u);
    CHECK(run_until(1000u + 4u * 142u));
    for (id = 0u; id < 3u; ++id) {
        CHECK(ngn_schedule_config_equal(&bus.nodes[id].radio.config, &config));
        CHECK(bus.nodes[id].radio.plan.epoch_ms == 142u);
        CHECK(bus.nodes[id].radio.epoch == 3u);
        CHECK(bus.nodes[id].active_session == TEST_SESSION);
        CHECK(ngn_radio_present_mask(&bus.nodes[id].radio) == 7u);
        CHECK(bus.sent[id][NGN_MESSAGE_PROBE] == 8u);
        CHECK(bus.sent[id][NGN_MESSAGE_NODE_HEALTH] == 4u);
        CHECK(bus.nodes[id].coexist_events == 4u);
        CHECK(bus.nodes[id].stats.tx_queue_drops == 0u);
        CHECK(bus.nodes[id].stats.rx_queue_drops == 0u);
        CHECK(bus.nodes[id].stats.status_queue_drops == 0u);
        CHECK(bus.nodes[id].probe_rx_events == 16u);
        CHECK(bus.nodes[id].last_probe_rx.session_id == TEST_SESSION);
        CHECK(bus.nodes[id].last_probe_rx.epoch == 3u);
        CHECK(ngn_node_id_is_valid(bus.nodes[id].last_probe_rx.node));
        CHECK(bus.nodes[id].last_probe_rx.node != (ngn_node_id_t)id);
        CHECK(bus.nodes[id].last_probe_rx.observed_ms >= 1000u);
    }
    CHECK(bus.sent[NGN_NODE_C][NGN_MESSAGE_SYNC] == 4u);
    CHECK(bus.sent[NGN_NODE_A][NGN_MESSAGE_SYNC] == 0u);
    CHECK(bus.sent[NGN_NODE_B][NGN_MESSAGE_SYNC] == 0u);
    CHECK(bus.prepare_calls[NGN_NODE_C] == 4u);
    return true;
}

static bool test_queue_pressure_and_health_accounting(void)
{
    const ngn_schedule_config_t config = ngn_schedule_default_config();
    ngn_transport_tx_t probe = {0};
    ngn_transport_rx_t malformed = {0};
    ngn_message_t message = {0};
    ngn_message_t decoded;
    node_t *node;
    size_t i;
    CHECK(initialize_bus(&config));
    bus.online[NGN_NODE_A] = false;
    bus.online[NGN_NODE_B] = false;
    node = &bus.nodes[NGN_NODE_C];
    node->tx_limit = 1u;
    node->status_limit = 1u;
    CHECK(ngn_radio_service(&node->radio, 1000u)); /* Hold the queued SYNC. */
    CHECK(node->tx_count == 1u);
    CHECK(ngn_radio_service(&node->radio, 1200u)); /* C's first probe: full. */
    CHECK(node->radio.stats.tx_queue_failures == 1u);
    bus.now_ms = 1200u;
    CHECK(dispatch(NGN_NODE_C)); /* The queued SYNC expired; never sent. */
    CHECK(node->stats.tx_expired == 1u);
    CHECK(bus.sent[NGN_NODE_C][NGN_MESSAGE_SYNC] == 0u);

    message.header = (ngn_protocol_header_t){
        .version = NGN_PROTOCOL_VERSION, .type = NGN_MESSAGE_PROBE,
        .source = NGN_NODE_C, .payload_length = NGN_PROTOCOL_PROBE_PAYLOAD_SIZE,
        .session_id = TEST_SESSION, .epoch = 0u, .sequence = 100u
    };
    message.payload.probe.count = 4u;
    probe.session_id = TEST_SESSION;
    probe.expires_ms = 1300u;
    CHECK(ngn_protocol_encode(&message, probe.bytes, sizeof(probe.bytes),
                              &probe.length) == NGN_PROTOCOL_OK);
    /* A full status queue never blocks driver completions or a later send. */
    for (i = 0u; i < 3u; ++i) {
        probe.token = (uint32_t)i;
        node->fail_driver = i == 2u;
        CHECK(mock_send(node, &probe));
        CHECK(dispatch(NGN_NODE_C));
    }
    node->fail_driver = false;
    CHECK(node->stats.tx_completed == 2u);
    CHECK(node->stats.tx_errors == 1u);
    CHECK(node->stats.status_queue_drops == 3u);
    CHECK(node->tx_count == 0u);
    malformed.length = 1u;
    malformed.received_ms = 1200u;
    for (i = 0u; i < RX_CAPACITY; ++i) {
        CHECK(enqueue_rx(node, &malformed));
    }
    CHECK(!enqueue_rx(node, &malformed));
    node->stats.rx_invalid = 3u; /* Separately captured adapter-invalid frames. */
    CHECK(ngn_radio_service(&node->radio, 1380u));
    CHECK(node->tx_count == 1u);
    CHECK(ngn_protocol_decode(node->tx[node->tx_head].bytes,
                              node->tx[node->tx_head].length, &decoded) ==
          NGN_PROTOCOL_OK);
    CHECK(decoded.header.type == NGN_MESSAGE_NODE_HEALTH);
    CHECK(decoded.payload.health.uptime_ms == 380u);
    CHECK(decoded.payload.health.tx_completed == 2u);
    CHECK(decoded.payload.health.rx_packets == 0u);
    CHECK(decoded.payload.health.rx_queue_drops == 1u);
    CHECK(decoded.payload.health.tx_queue_drops == 1u);
    CHECK(decoded.payload.health.status_queue_drops == 3u);
    CHECK(decoded.payload.health.tx_errors == 1u);
    CHECK(decoded.payload.health.rx_rejected == RX_CAPACITY + 3u);
    CHECK(decoded.payload.health.late_tx_drops == 4u);
    CHECK(decoded.payload.health.present_mask == 4u);
    CHECK(decoded.payload.health.channel == 6u);
    CHECK(decoded.payload.health.flags == 0u);
    CHECK(decoded.payload.health.last_rx_node == NGN_NODE_UNCONFIGURED);
    CHECK(decoded.payload.health.firmware_major == NGN_FIRMWARE_VERSION_MAJOR);
    CHECK(decoded.payload.health.firmware_minor == NGN_FIRMWARE_VERSION_MINOR);
    CHECK(decoded.payload.health.firmware_patch == NGN_FIRMWARE_VERSION_PATCH);
    CHECK(node->radio.stats.tx_status_expired == 1u);
    bus.now_ms = 1380u;
    CHECK(dispatch(NGN_NODE_C));
    CHECK(ngn_radio_service(&node->radio, 1381u));
    CHECK(node->radio.stats.tx_status_complete == 1u);
    CHECK(node->radio.stats.tx_status_failed == 0u); /* Dropped status was counted. */
    return true;
}

int main(void)
{
    if (!test_initialization_validation() ||
        !test_missing_node_health_and_rejoin() ||
        !test_coordinator_schedule_adoption() ||
        !test_queue_pressure_and_health_accounting()) {
        return 1;
    }
    puts("ngn_radio: initialization, dropout/rejoin, schedule adoption and queue health passed");
    return 0;
}

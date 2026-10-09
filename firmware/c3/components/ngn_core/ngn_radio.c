#include "ngn_radio.h"

#include <string.h>

static bool mac_valid(const uint8_t *mac)
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

static void emit(ngn_radio_t *radio, ngn_radio_event_kind_t kind,
                 ngn_node_id_t node, const ngn_schedule_event_t *schedule)
{
    if (radio->event_sink != NULL) {
        ngn_radio_event_t event = {0};
        event.kind = kind;
        event.session_id = radio->session_id;
        event.epoch = radio->epoch;
        event.node = node;
        if (schedule != NULL) {
            event.schedule = *schedule;
        }
        radio->event_sink(radio->event_context, &event);
    }
}

uint8_t ngn_radio_present_mask(const ngn_radio_t *radio)
{
    uint8_t mask = 0u;
    size_t i;
    if (radio == NULL || !radio->initialized) {
        return 0u;
    }
    for (i = 0u; i < NGN_RADIO_PEER_COUNT; ++i) {
        if (radio->peers[i].present) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

const ngn_radio_peer_t *ngn_radio_peer(const ngn_radio_t *radio,
                                     ngn_node_id_t node)
{
    if (radio == NULL || !radio->initialized || !ngn_node_id_is_valid(node)) {
        return NULL;
    }
    return &radio->peers[(size_t)node];
}

bool ngn_radio_init(ngn_radio_t *radio, ngn_node_id_t self,
                    const uint8_t local_mac[NGN_TRANSPORT_MAC_SIZE],
                    const ngn_schedule_config_t *config,
                    const ngn_transport_t *transport,
                    uint64_t coordinator_session_id, uint64_t now_ms,
                    ngn_radio_event_sink_t event_sink, void *event_context)
{
    ngn_schedule_config_t saved_config;
    ngn_transport_t saved_transport;
    uint8_t saved_mac[NGN_TRANSPORT_MAC_SIZE];
    ngn_radio_peer_t *local;

    if (radio == NULL || !ngn_node_id_is_valid(self) || !mac_valid(local_mac) ||
        !ngn_schedule_config_valid(config) || transport == NULL ||
        transport->send == NULL || transport->receive == NULL ||
        transport->poll_status == NULL || transport->get_stats == NULL ||
        transport->set_session == NULL ||
        (self == NGN_NODE_C ? coordinator_session_id == 0u :
                              coordinator_session_id != 0u)) {
        return false;
    }
    saved_config = *config;
    saved_transport = *transport;
    memcpy(saved_mac, local_mac, sizeof(saved_mac));
    memset(radio, 0, sizeof(*radio));
    radio->self = self;
    radio->config = saved_config;
    radio->transport = saved_transport;
    radio->event_sink = event_sink;
    radio->event_context = event_context;
    /* Validation above makes this infallible; building directly avoids a
     * second large plan on an embedded task's stack. */
    (void)ngn_schedule_build(&radio->config, &radio->plan);
    radio->session_id = coordinator_session_id;
    radio->started_ms = now_ms;
    radio->last_service_ms = now_ms;
    radio->epoch_start_ms = now_ms;
    radio->last_probe_rx_node = NGN_NODE_UNCONFIGURED;
    radio->state = self == NGN_NODE_C ? NGN_RADIO_SYNCHRONIZED :
                                       NGN_RADIO_DISCOVERING;
    local = &radio->peers[(size_t)self];
    local->bound = true;
    local->present = true;
    local->last_seen_ms = now_ms;
    memcpy(local->mac, saved_mac, sizeof(local->mac));
    radio->initialized = true;
    radio->transport.set_session(radio->transport.context, radio->session_id);
    emit(radio, NGN_RADIO_EVENT_BOUND, self, NULL);
    if (self == NGN_NODE_C) {
        ++radio->stats.session_changes;
        emit(radio, NGN_RADIO_EVENT_SESSION, self, NULL);
        emit(radio, NGN_RADIO_EVENT_EPOCH, self, NULL);
    }
    return true;
}

static uint64_t timeout_ms(const ngn_radio_t *radio)
{
    return (uint64_t)radio->plan.epoch_ms * radio->config.missing_epochs;
}

static void maintain(ngn_radio_t *radio, uint64_t now_ms)
{
    const uint64_t timeout = timeout_ms(radio);
    size_t i;

    if (radio->self == NGN_NODE_C) {
        const uint64_t elapsed = now_ms - radio->epoch_start_ms;
        const uint64_t epochs = elapsed / radio->plan.epoch_ms;
        if (epochs != 0u) {
            radio->epoch += (uint32_t)epochs;
            radio->epoch_start_ms += epochs * radio->plan.epoch_ms;
            radio->next_event = 0u;
            radio->stats.epochs_skipped += (uint32_t)(epochs - 1u);
            emit(radio, NGN_RADIO_EVENT_EPOCH, radio->self, NULL);
        }
    } else if (radio->session_id != 0u) {
        if (now_ms - radio->last_sync_ms >= timeout) {
            if (radio->state != NGN_RADIO_DISCOVERING) {
                radio->state = NGN_RADIO_DISCOVERING;
                ++radio->stats.sync_timeouts;
                emit(radio, NGN_RADIO_EVENT_SYNC_TIMEOUT, NGN_NODE_C, NULL);
            }
        } else if (radio->state == NGN_RADIO_SYNCHRONIZED &&
                   now_ms - radio->epoch_start_ms >= radio->plan.epoch_ms) {
            radio->state = NGN_RADIO_WAIT_SYNC;
        }
    }
    /* Expire before RX sequence checks. A missing peer may have rebooted its
     * sequence counter; its station-MAC binding remains pinned. */
    for (i = 0u; i < NGN_RADIO_PEER_COUNT; ++i) {
        ngn_radio_peer_t *peer = &radio->peers[i];
        if (i != (size_t)radio->self && peer->present &&
            now_ms - peer->last_seen_ms >= timeout) {
            peer->present = false;
            peer->sequence_valid = false;
            emit(radio, NGN_RADIO_EVENT_MISSING, (ngn_node_id_t)i, NULL);
        }
    }
}

static void reject(ngn_radio_t *radio, uint32_t *reason)
{
    ++radio->stats.rx_rejected;
    ++*reason;
}

static bool identity_valid(const ngn_radio_t *radio, ngn_node_id_t source,
                            const uint8_t *mac)
{
    size_t i;
    const ngn_radio_peer_t *peer = &radio->peers[(size_t)source];
    if (source == radio->self || !mac_valid(mac) ||
        (peer->bound && memcmp(peer->mac, mac, sizeof(peer->mac)) != 0)) {
        return false;
    }
    for (i = 0u; i < NGN_RADIO_PEER_COUNT; ++i) {
        if (i != (size_t)source && radio->peers[i].bound &&
            memcmp(radio->peers[i].mac, mac, NGN_TRANSPORT_MAC_SIZE) == 0) {
            return false;
        }
    }
    return true;
}

static bool retired(const ngn_radio_t *radio, uint64_t session_id)
{
    size_t i;
    for (i = 0u; i < NGN_RADIO_RETIRED_SESSIONS; ++i) {
        if (radio->retired_sessions[i] == session_id) {
            return true;
        }
    }
    return false;
}

static void adopt_session(ngn_radio_t *radio, const ngn_message_t *message)
{
    size_t i;
    if (radio->session_id != 0u) {
        radio->retired_sessions[radio->retired_next] = radio->session_id;
        radio->retired_next = (radio->retired_next + 1u) %
                              NGN_RADIO_RETIRED_SESSIONS;
    }
    for (i = 0u; i < NGN_RADIO_PEER_COUNT; ++i) {
        if (i != (size_t)radio->self) {
            /* The accepted C MAC has already been checked against the pinned
             * mapping; it is rebound below before any callback is emitted. */
            memset(&radio->peers[i], 0, sizeof(radio->peers[i]));
        }
    }
    radio->peers[(size_t)radio->self].sequence_valid = false;
    radio->peers[(size_t)radio->self].health_valid = false;
    radio->session_id = message->header.session_id;
    radio->transport.set_session(radio->transport.context, radio->session_id);
    radio->config = message->payload.sync.config;
    (void)ngn_schedule_build(&radio->config, &radio->plan);
    radio->next_sequence = 0u;
    radio->last_probe_tx_valid = false;
    radio->last_probe_rx_valid = false;
    radio->last_probe_tx_sequence = 0u;
    radio->last_probe_rx_sequence = 0u;
    radio->last_probe_rx_node = NGN_NODE_UNCONFIGURED;
    ++radio->stats.session_changes;
}

static void accept_rx(ngn_radio_t *radio, const ngn_transport_rx_t *frame,
                       uint64_t now_ms)
{
    ngn_message_t message;
    ngn_radio_peer_t *peer;
    bool new_session = false;
    bool was_bound;
    bool was_present;

    if (frame->length > sizeof(frame->bytes) ||
        ngn_protocol_decode(frame->bytes, frame->length, &message) !=
            NGN_PROTOCOL_OK) {
        reject(radio, &radio->stats.reject_protocol);
        return;
    }
    if (frame->received_ms > now_ms || frame->received_ms < radio->started_ms) {
        reject(radio, &radio->stats.reject_time);
        return;
    }
    if (!identity_valid(radio, message.header.source, frame->source_mac)) {
        reject(radio, &radio->stats.reject_identity);
        return;
    }
    peer = &radio->peers[(size_t)message.header.source];
    if (peer->bound && frame->received_ms < peer->last_seen_ms) {
        reject(radio, &radio->stats.reject_time);
        return;
    }

    if (message.header.type == NGN_MESSAGE_SYNC) {
        const ngn_sync_payload_t *sync = &message.payload.sync;
        const uint32_t duration = (uint32_t)sync->config.sync_slot_ms +
            3u * sync->config.probe_slot_ms + sync->config.coexist_ms +
            3u * sync->config.health_slot_ms;
        if (sync->config.channel != radio->config.channel ||
            (message.header.session_id == radio->session_id &&
             !ngn_schedule_config_equal(&sync->config, &radio->config))) {
            reject(radio, &radio->stats.reject_config);
            return;
        }
        if (sync->epoch_elapsed_ms > frame->received_ms ||
            now_ms - frame->received_ms >=
                (uint64_t)duration * sync->config.missing_epochs) {
            reject(radio, &radio->stats.reject_time);
            return;
        }
        new_session = message.header.session_id != radio->session_id;
        if (new_session && retired(radio, message.header.session_id)) {
            reject(radio, &radio->stats.reject_session);
            return;
        }
        if (!new_session && radio->sync_epoch_valid &&
            !ngn_serial32_newer(message.header.epoch, radio->last_sync_epoch)) {
            reject(radio, &radio->stats.reject_epoch);
            return;
        }
    } else {
        if (message.header.session_id != radio->session_id ||
            radio->state == NGN_RADIO_DISCOVERING) {
            reject(radio, &radio->stats.reject_session);
            return;
        }
        if (radio->state != NGN_RADIO_SYNCHRONIZED ||
            message.header.epoch != radio->epoch) {
            reject(radio, &radio->stats.reject_epoch);
            return;
        }
        if (frame->received_ms < radio->epoch_start_ms ||
            frame->received_ms - radio->epoch_start_ms >= radio->plan.epoch_ms) {
            reject(radio, &radio->stats.reject_time);
            return;
        }
        if ((message.header.type == NGN_MESSAGE_PROBE &&
             message.payload.probe.count != radio->config.burst_count) ||
            (message.header.type == NGN_MESSAGE_NODE_HEALTH &&
             message.payload.health.channel != radio->config.channel)) {
            reject(radio, &radio->stats.reject_config);
            return;
        }
    }
    if (!new_session && peer->sequence_valid &&
        !ngn_serial32_newer(message.header.sequence, peer->last_sequence)) {
        reject(radio, &radio->stats.reject_sequence);
        return;
    }

    /* All validation precedes binding, sequence, presence and session changes. */
    if (new_session) {
        adopt_session(radio, &message);
        peer = &radio->peers[(size_t)message.header.source];
    }
    if (message.header.type == NGN_MESSAGE_SYNC) {
        radio->epoch = message.header.epoch;
        radio->epoch_start_ms = frame->received_ms -
                                message.payload.sync.epoch_elapsed_ms;
        radio->last_sync_ms = frame->received_ms;
        radio->last_sync_epoch = message.header.epoch;
        radio->sync_epoch_valid = true;
        radio->next_event = 0u;
        radio->state = NGN_RADIO_SYNCHRONIZED;
    }
    was_bound = peer->bound;
    was_present = peer->present;
    peer->bound = true;
    peer->present = true;
    memcpy(peer->mac, frame->source_mac, sizeof(peer->mac));
    peer->sequence_valid = true;
    peer->last_sequence = message.header.sequence;
    peer->last_epoch = message.header.epoch;
    peer->last_seen_ms = frame->received_ms;
    if (message.header.type == NGN_MESSAGE_PROBE) {
        radio->last_probe_rx_valid = true;
        radio->last_probe_rx_sequence = message.header.sequence;
        radio->last_probe_rx_node = message.header.source;
    } else if (message.header.type == NGN_MESSAGE_NODE_HEALTH) {
        peer->health = message.payload.health;
        peer->health_valid = true;
    }
    ++radio->stats.rx_accepted;
    if (new_session) {
        emit(radio, NGN_RADIO_EVENT_SESSION, NGN_NODE_C, NULL);
    }
    if (!was_bound) {
        emit(radio, NGN_RADIO_EVENT_BOUND, message.header.source, NULL);
    }
    if (!was_present) {
        emit(radio, NGN_RADIO_EVENT_PRESENT, message.header.source, NULL);
    }
    if (message.header.type == NGN_MESSAGE_SYNC) {
        emit(radio, NGN_RADIO_EVENT_EPOCH, NGN_NODE_C, NULL);
    }
}

static bool prepare_sync(uint8_t *bytes, size_t length, uint64_t submit_ms,
                          uint64_t reference_ms)
{
    ngn_message_t message;
    size_t encoded_length = 0u;
    if (submit_ms < reference_ms ||
        ngn_protocol_decode(bytes, length, &message) != NGN_PROTOCOL_OK ||
        message.header.type != NGN_MESSAGE_SYNC ||
        submit_ms - reference_ms >= message.payload.sync.config.sync_slot_ms) {
        return false;
    }
    message.payload.sync.epoch_elapsed_ms = (uint32_t)(submit_ms - reference_ms);
    return ngn_protocol_encode(&message, bytes, length, &encoded_length) ==
               NGN_PROTOCOL_OK && encoded_length == length;
}

static ngn_health_payload_t health_snapshot(ngn_radio_t *radio, uint64_t now_ms)
{
    ngn_transport_stats_t transport = {0};
    ngn_health_payload_t health = {0};
    radio->transport.get_stats(radio->transport.context, &transport);
    health.uptime_ms = (uint32_t)(now_ms - radio->started_ms);
    health.last_probe_tx_seq = radio->last_probe_tx_sequence;
    health.last_probe_rx_seq = radio->last_probe_rx_sequence;
    health.tx_completed = transport.tx_completed;
    health.rx_packets = radio->stats.rx_accepted;
    health.rx_queue_drops = transport.rx_queue_drops;
    health.tx_queue_drops = transport.tx_queue_drops;
    health.status_queue_drops = transport.status_queue_drops;
    health.tx_errors = transport.tx_errors + radio->stats.tx_encode_errors;
    health.rx_rejected = radio->stats.rx_rejected + transport.rx_invalid;
    health.late_tx_drops = radio->stats.tx_late_drops + transport.tx_expired;
    health.channel = radio->config.channel;
    health.present_mask = ngn_radio_present_mask(radio);
    health.firmware_major = NGN_FIRMWARE_VERSION_MAJOR;
    health.firmware_minor = NGN_FIRMWARE_VERSION_MINOR;
    health.firmware_patch = NGN_FIRMWARE_VERSION_PATCH;
    health.flags = (radio->last_probe_tx_valid ? NGN_HEALTH_TX_SEQUENCE_VALID : 0u) |
                   (radio->last_probe_rx_valid ? NGN_HEALTH_RX_SEQUENCE_VALID : 0u);
    health.last_rx_node = radio->last_probe_rx_node;
    return health;
}

static void transmit(ngn_radio_t *radio, const ngn_schedule_event_t *event,
                      uint64_t now_ms, uint32_t elapsed_ms)
{
    ngn_message_t message = {0};
    ngn_transport_tx_t frame = {0};
    ngn_radio_peer_t *local = &radio->peers[(size_t)radio->self];

    if (UINT64_MAX - radio->epoch_start_ms < event->deadline_ms) {
        ++radio->stats.tx_late_drops;
        return;
    }
    message.header.version = NGN_PROTOCOL_VERSION;
    message.header.source = radio->self;
    message.header.session_id = radio->session_id;
    message.header.epoch = radio->epoch;
    message.header.sequence = radio->next_sequence++;
    switch (event->kind) {
    case NGN_SCHEDULE_SYNC_TX:
        message.header.type = NGN_MESSAGE_SYNC;
        message.payload.sync.schedule_version = NGN_SCHEDULE_VERSION;
        message.payload.sync.config = radio->config;
        message.payload.sync.epoch_elapsed_ms = elapsed_ms;
        frame.prepare = prepare_sync;
        frame.reference_ms = radio->epoch_start_ms;
        break;
    case NGN_SCHEDULE_PROBE_TX:
        message.header.type = NGN_MESSAGE_PROBE;
        message.payload.probe.index = event->probe_index;
        message.payload.probe.count = radio->config.burst_count;
        break;
    case NGN_SCHEDULE_HEALTH_TX:
        message.header.type = NGN_MESSAGE_NODE_HEALTH;
        message.payload.health = health_snapshot(radio, now_ms);
        break;
    default:
        return;
    }
    message.header.payload_length =
        (uint16_t)ngn_protocol_payload_size(message.header.type);
    frame.token = message.header.sequence;
    frame.session_id = radio->session_id;
    frame.expires_ms = radio->epoch_start_ms + event->deadline_ms;
    if (ngn_protocol_encode(&message, frame.bytes, sizeof(frame.bytes),
                            &frame.length) != NGN_PROTOCOL_OK) {
        ++radio->stats.tx_encode_errors;
        return;
    }
    if (!radio->transport.send(radio->transport.context, &frame)) {
        ++radio->stats.tx_queue_failures;
        return;
    }
    ++radio->stats.tx_queued;
    local->sequence_valid = true;
    local->last_sequence = message.header.sequence;
    local->last_epoch = radio->epoch;
    local->last_seen_ms = now_ms;
    if (message.header.type == NGN_MESSAGE_PROBE) {
        radio->last_probe_tx_valid = true;
        radio->last_probe_tx_sequence = message.header.sequence;
    } else if (message.header.type == NGN_MESSAGE_NODE_HEALTH) {
        local->health = message.payload.health;
        local->health_valid = true;
    }
    emit(radio, NGN_RADIO_EVENT_SCHEDULE, radio->self, event);
}

bool ngn_radio_service(ngn_radio_t *radio, uint64_t now_ms)
{
    size_t i;
    uint32_t elapsed_ms;
    if (radio == NULL || !radio->initialized) {
        return false;
    }
    if (now_ms < radio->last_service_ms) {
        ++radio->stats.clock_rejections;
        return false;
    }
    radio->last_service_ms = now_ms;
    maintain(radio, now_ms);
    for (i = 0u; i < NGN_RADIO_RX_BUDGET; ++i) {
        ngn_transport_rx_t frame;
        if (!radio->transport.receive(radio->transport.context, &frame)) {
            break;
        }
        accept_rx(radio, &frame, now_ms);
        maintain(radio, now_ms);
    }
    for (i = 0u; i < NGN_RADIO_STATUS_BUDGET; ++i) {
        ngn_transport_status_t status;
        if (!radio->transport.poll_status(radio->transport.context, &status)) {
            break;
        }
        switch (status.result) {
        case NGN_TRANSPORT_TX_COMPLETE:
            ++radio->stats.tx_status_complete;
            break;
        case NGN_TRANSPORT_TX_FAILED:
            ++radio->stats.tx_status_failed;
            break;
        case NGN_TRANSPORT_TX_EXPIRED:
            ++radio->stats.tx_status_expired;
            break;
        case NGN_TRANSPORT_TX_CANCELED:
            ++radio->stats.tx_status_canceled;
            break;
        default:
            break;
        }
    }
    if (radio->state != NGN_RADIO_SYNCHRONIZED) {
        return true;
    }
    elapsed_ms = (uint32_t)(now_ms - radio->epoch_start_ms);
    while (radio->next_event < radio->plan.count) {
        const ngn_schedule_event_t *event = &radio->plan.events[radio->next_event];
        if (event->offset_ms > elapsed_ms) {
            break;
        }
        ++radio->next_event;
        if (ngn_schedule_event_is_tx(event->kind)) {
            if (event->source == radio->self) {
                if (ngn_schedule_tx_due(event, elapsed_ms)) {
                    transmit(radio, event, now_ms, elapsed_ms);
                } else {
                    ++radio->stats.tx_late_drops;
                }
            }
        } else if (elapsed_ms < event->deadline_ms) {
            emit(radio, NGN_RADIO_EVENT_SCHEDULE, event->source, event);
        }
    }
    return true;
}

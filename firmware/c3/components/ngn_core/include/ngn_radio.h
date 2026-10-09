#ifndef NGN_RADIO_H
#define NGN_RADIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ngn_protocol.h"
#include "ngn_transport.h"

#define NGN_RADIO_PEER_COUNT 3u
#define NGN_RADIO_RX_BUDGET 16u
#define NGN_RADIO_STATUS_BUDGET 16u
#define NGN_RADIO_RETIRED_SESSIONS 8u

typedef enum {
    NGN_RADIO_DISCOVERING = 0,
    NGN_RADIO_SYNCHRONIZED,
    NGN_RADIO_WAIT_SYNC
} ngn_radio_state_t;

typedef enum {
    NGN_RADIO_EVENT_SESSION = 0,
    NGN_RADIO_EVENT_EPOCH,
    NGN_RADIO_EVENT_SCHEDULE,
    NGN_RADIO_EVENT_BOUND,
    NGN_RADIO_EVENT_PRESENT,
    NGN_RADIO_EVENT_MISSING,
    NGN_RADIO_EVENT_SYNC_TIMEOUT
} ngn_radio_event_kind_t;

typedef struct {
    ngn_radio_event_kind_t kind;
    uint64_t session_id;
    uint32_t epoch;
    ngn_node_id_t node;
    ngn_schedule_event_t schedule;
} ngn_radio_event_t;

/* Called only from init/service, never a radio callback. Must not block or
 * re-enter the radio. Schedule notifications do not start sensing work. */
typedef void (*ngn_radio_event_sink_t)(void *context,
                                       const ngn_radio_event_t *event);

typedef struct {
    bool bound;
    bool present;
    uint8_t mac[NGN_TRANSPORT_MAC_SIZE];
    bool sequence_valid;
    uint32_t last_sequence;
    uint32_t last_epoch;
    uint64_t last_seen_ms;
    bool health_valid;
    ngn_health_payload_t health;
} ngn_radio_peer_t;

/* Unsigned diagnostics wrap modulo 2^32. */
typedef struct {
    uint32_t rx_accepted;
    uint32_t rx_rejected;
    uint32_t reject_protocol;
    uint32_t reject_time;
    uint32_t reject_identity;
    uint32_t reject_session;
    uint32_t reject_epoch;
    uint32_t reject_sequence;
    uint32_t reject_config;
    uint32_t tx_queued;
    uint32_t tx_queue_failures;
    uint32_t tx_late_drops;
    uint32_t tx_encode_errors;
    uint32_t epochs_skipped;
    uint32_t sync_timeouts;
    uint32_t session_changes;
    uint32_t tx_status_complete;
    uint32_t tx_status_failed;
    uint32_t tx_status_expired;
    uint32_t tx_status_canceled;
    uint32_t clock_rejections;
} ngn_radio_stats_t;

/* Fixed storage. Fields are observable but owned by init/service. Only one
 * normal worker may service a radio; the transport synchronizes its queues. */
typedef struct {
    bool initialized;
    ngn_node_id_t self;
    ngn_radio_state_t state;
    ngn_schedule_config_t config;
    ngn_schedule_plan_t plan;
    ngn_transport_t transport;
    ngn_radio_event_sink_t event_sink;
    void *event_context;
    ngn_radio_peer_t peers[NGN_RADIO_PEER_COUNT];
    uint64_t session_id;
    uint32_t epoch;
    uint32_t next_sequence;
    uint64_t epoch_start_ms;
    uint64_t started_ms;
    uint64_t last_service_ms;
    uint64_t last_sync_ms;
    bool sync_epoch_valid;
    uint32_t last_sync_epoch;
    size_t next_event;
    uint64_t retired_sessions[NGN_RADIO_RETIRED_SESSIONS];
    size_t retired_next;
    bool last_probe_tx_valid;
    bool last_probe_rx_valid;
    uint32_t last_probe_tx_sequence;
    uint32_t last_probe_rx_sequence;
    ngn_node_id_t last_probe_rx_node;
    ngn_radio_stats_t stats;
} ngn_radio_t;

/* C requires a nonzero freshly generated session ID; A/B require zero and
 * discover C. Local MAC must be a nonzero unicast station address. Failure
 * leaves radio unchanged. All transport operations are required/nonblocking. */
bool ngn_radio_init(ngn_radio_t *radio, ngn_node_id_t self,
                    const uint8_t local_mac[NGN_TRANSPORT_MAC_SIZE],
                    const ngn_schedule_config_t *config,
                    const ngn_transport_t *transport,
                    uint64_t coordinator_session_id, uint64_t now_ms,
                    ngn_radio_event_sink_t event_sink, void *event_context);

/* Bounded: at most 16 RX, 16 status and 59 schedule events per call. Uses
 * monotonic milliseconds. C advances epochs; followers wait silently for a
 * strictly newer SYNC after each epoch. Backwards time returns false. */
bool ngn_radio_service(ngn_radio_t *radio, uint64_t now_ms);

uint8_t ngn_radio_present_mask(const ngn_radio_t *radio);
const ngn_radio_peer_t *ngn_radio_peer(const ngn_radio_t *radio,
                                     ngn_node_id_t node);

#endif

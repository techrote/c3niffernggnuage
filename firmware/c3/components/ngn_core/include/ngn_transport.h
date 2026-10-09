#ifndef NGN_TRANSPORT_H
#define NGN_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NGN_TRANSPORT_FRAME_CAPACITY 96u
#define NGN_TRANSPORT_MAC_SIZE 6u

/* All calls are nonblocking. The adapter copies queued frame bytes. */
typedef struct {
    uint8_t source_mac[NGN_TRANSPORT_MAC_SIZE];
    uint8_t bytes[NGN_TRANSPORT_FRAME_CAPACITY];
    size_t length;
    uint64_t received_ms;
} ngn_transport_rx_t;

typedef struct {
    uint8_t bytes[NGN_TRANSPORT_FRAME_CAPACITY];
    size_t length;
    uint64_t expires_ms; /* Exclusive local monotonic transmission deadline. */
    uint32_t token;      /* Per-source protocol sequence, echoed in status. */
    uint64_t session_id; /* Adapter invalidates queued records at session change. */
    uint64_t reference_ms;
    /* Optional pure, bounded worker hook immediately before actual submission.
     * It operates only on this copied frame and captured reference, never on
     * mutable owner state. Never invoke it in a radio callback. */
    bool (*prepare)(uint8_t *bytes, size_t length, uint64_t submit_ms,
                    uint64_t reference_ms);
} ngn_transport_tx_t;

typedef enum {
    NGN_TRANSPORT_TX_COMPLETE = 0,
    NGN_TRANSPORT_TX_FAILED,
    NGN_TRANSPORT_TX_EXPIRED,
    NGN_TRANSPORT_TX_CANCELED
} ngn_transport_tx_result_t;

typedef struct {
    uint32_t token;
    ngn_transport_tx_result_t result;
} ngn_transport_status_t;

/* Unsigned diagnostic counters wrap modulo 2^32. */
typedef struct {
    uint32_t rx_queue_drops;
    uint32_t tx_queue_drops;
    uint32_t status_queue_drops;
    uint32_t rx_invalid;
    uint32_t tx_errors;
    uint32_t tx_completed;
    uint32_t tx_expired;
    uint32_t tx_callbacks_unexpected;
    uint32_t tx_stalled;
    uint32_t tx_canceled;
} ngn_transport_stats_t;

typedef struct {
    void *context;
    bool (*send)(void *context, const ngn_transport_tx_t *frame);
    bool (*receive)(void *context, ngn_transport_rx_t *frame);
    bool (*poll_status)(void *context, ngn_transport_status_t *status);
    void (*get_stats)(void *context, ngn_transport_stats_t *stats);
    /* Atomically changes admission identity. Previously claimed in-flight
     * work may complete; obsolete queued records must not newly claim TX. */
    void (*set_session)(void *context, uint64_t session_id);
} ngn_transport_t;

#endif

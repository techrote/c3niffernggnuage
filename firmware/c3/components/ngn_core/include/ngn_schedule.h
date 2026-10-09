#ifndef NGN_SCHEDULE_H
#define NGN_SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ngn_node.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NGN_SCHEDULE_VERSION 1u
#define NGN_SCHEDULE_MAX_CHANNEL 11u
#define NGN_SCHEDULE_MAX_BURST 16u
#define NGN_SCHEDULE_MAX_MISSING_EPOCHS 32u
#define NGN_SCHEDULE_MAX_EPOCH_MS 60000u
#define NGN_SCHEDULE_MAX_EVENTS (11u + 3u * NGN_SCHEDULE_MAX_BURST)

typedef struct {
    uint8_t channel;
    uint8_t burst_count;
    uint8_t missing_epochs;
    uint16_t probe_spacing_ms;
    uint16_t sync_slot_ms;
    uint16_t probe_slot_ms;
    uint16_t coexist_ms;
    uint16_t health_slot_ms;
} ngn_schedule_config_t;

typedef enum {
    NGN_SCHEDULE_SYNC_TX = 0,
    NGN_SCHEDULE_PROBE_SLOT,
    NGN_SCHEDULE_PROBE_TX,
    NGN_SCHEDULE_COEXIST_OPPORTUNITY,
    NGN_SCHEDULE_HEALTH_SLOT,
    NGN_SCHEDULE_HEALTH_TX
} ngn_schedule_event_kind_t;

typedef struct {
    ngn_schedule_event_kind_t kind;
    ngn_node_id_t source;
    uint8_t probe_index;
    uint32_t offset_ms;
    uint32_t deadline_ms;
} ngn_schedule_event_t;

typedef struct {
    uint32_t epoch_ms;
    size_t count;
    ngn_schedule_event_t events[NGN_SCHEDULE_MAX_EVENTS];
} ngn_schedule_plan_t;

ngn_schedule_config_t ngn_schedule_default_config(void);
bool ngn_schedule_config_valid(const ngn_schedule_config_t *config);
bool ngn_schedule_config_equal(const ngn_schedule_config_t *left,
                               const ngn_schedule_config_t *right);

/* Invalid input leaves the output unchanged. No allocation or clock access. */
bool ngn_schedule_build(const ngn_schedule_config_t *config,
                        ngn_schedule_plan_t *out_plan);

bool ngn_schedule_event_is_tx(ngn_schedule_event_kind_t kind);

/* TX eligibility is [offset_ms, deadline_ms); expired sends must be dropped. */
bool ngn_schedule_tx_due(const ngn_schedule_event_t *event, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif

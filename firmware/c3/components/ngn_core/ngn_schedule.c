#include "ngn_schedule.h"

ngn_schedule_config_t ngn_schedule_default_config(void)
{
    const ngn_schedule_config_t config = {
        .channel = 6u,
        .burst_count = 4u,
        .missing_epochs = 4u,
        .probe_spacing_ms = 10u,
        .sync_slot_ms = 40u,
        .probe_slot_ms = 80u,
        .coexist_ms = 80u,
        .health_slot_ms = 10u
    };
    return config;
}

bool ngn_schedule_config_valid(const ngn_schedule_config_t *config)
{
    uint32_t epoch_ms;

    if (config == NULL || config->channel == 0u ||
        config->channel > NGN_SCHEDULE_MAX_CHANNEL ||
        config->burst_count == 0u ||
        config->burst_count > NGN_SCHEDULE_MAX_BURST ||
        config->missing_epochs == 0u ||
        config->missing_epochs > NGN_SCHEDULE_MAX_MISSING_EPOCHS ||
        config->probe_spacing_ms == 0u || config->sync_slot_ms == 0u ||
        config->probe_slot_ms == 0u || config->health_slot_ms == 0u) {
        return false;
    }

    /* Every probe owns a full spacing interval, including the last one. */
    if ((uint32_t)config->burst_count * config->probe_spacing_ms >
        config->probe_slot_ms) {
        return false;
    }
    epoch_ms = (uint32_t)config->sync_slot_ms +
               3u * config->probe_slot_ms + config->coexist_ms +
               3u * config->health_slot_ms;
    return epoch_ms <= NGN_SCHEDULE_MAX_EPOCH_MS;
}

bool ngn_schedule_config_equal(const ngn_schedule_config_t *left,
                               const ngn_schedule_config_t *right)
{
    return left != NULL && right != NULL &&
           left->channel == right->channel &&
           left->burst_count == right->burst_count &&
           left->missing_epochs == right->missing_epochs &&
           left->probe_spacing_ms == right->probe_spacing_ms &&
           left->sync_slot_ms == right->sync_slot_ms &&
           left->probe_slot_ms == right->probe_slot_ms &&
           left->coexist_ms == right->coexist_ms &&
           left->health_slot_ms == right->health_slot_ms;
}

static void add_event(ngn_schedule_plan_t *plan,
                      ngn_schedule_event_kind_t kind,
                      ngn_node_id_t source,
                      uint8_t probe_index,
                      uint32_t offset_ms,
                      uint32_t deadline_ms)
{
    ngn_schedule_event_t *event = &plan->events[plan->count];
    event->kind = kind;
    event->source = source;
    event->probe_index = probe_index;
    event->offset_ms = offset_ms;
    event->deadline_ms = deadline_ms;
    ++plan->count;
}

bool ngn_schedule_build(const ngn_schedule_config_t *config,
                        ngn_schedule_plan_t *out_plan)
{
    ngn_schedule_plan_t plan = {0};
    uint32_t offset;
    uint8_t node;

    if (out_plan == NULL || !ngn_schedule_config_valid(config)) {
        return false;
    }

    add_event(&plan, NGN_SCHEDULE_SYNC_TX, NGN_NODE_C, 0u, 0u,
              config->sync_slot_ms);
    offset = config->sync_slot_ms;

    for (node = 0u; node < 3u; ++node) {
        uint8_t probe;
        add_event(&plan, NGN_SCHEDULE_PROBE_SLOT, (ngn_node_id_t)node,
                  0u, offset, offset + config->probe_slot_ms);
        for (probe = 0u; probe < config->burst_count; ++probe) {
            const uint32_t probe_offset = offset +
                (uint32_t)probe * config->probe_spacing_ms;
            add_event(&plan, NGN_SCHEDULE_PROBE_TX, (ngn_node_id_t)node,
                      probe, probe_offset,
                      probe_offset + config->probe_spacing_ms);
        }
        offset += config->probe_slot_ms;
    }

    add_event(&plan, NGN_SCHEDULE_COEXIST_OPPORTUNITY,
              NGN_NODE_UNCONFIGURED, 0u, offset,
              offset + config->coexist_ms);
    offset += config->coexist_ms;
    for (node = 0u; node < 3u; ++node) {
        add_event(&plan, NGN_SCHEDULE_HEALTH_SLOT, (ngn_node_id_t)node,
                  0u, offset, offset + config->health_slot_ms);
        add_event(&plan, NGN_SCHEDULE_HEALTH_TX, (ngn_node_id_t)node,
                  0u, offset, offset + config->health_slot_ms);
        offset += config->health_slot_ms;
    }

    plan.epoch_ms = offset;
    *out_plan = plan;
    return true;
}

bool ngn_schedule_event_is_tx(ngn_schedule_event_kind_t kind)
{
    return kind == NGN_SCHEDULE_SYNC_TX || kind == NGN_SCHEDULE_PROBE_TX ||
           kind == NGN_SCHEDULE_HEALTH_TX;
}

bool ngn_schedule_tx_due(const ngn_schedule_event_t *event, uint32_t elapsed_ms)
{
    return event != NULL && ngn_schedule_event_is_tx(event->kind) &&
           elapsed_ms >= event->offset_ms && elapsed_ms < event->deadline_ms;
}

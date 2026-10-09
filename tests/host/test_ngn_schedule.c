#include <stdio.h>

#include "ngn_schedule.h"

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n",                   \
                    __FILE__, __LINE__, #condition);                           \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static bool event_equal(const ngn_schedule_event_t *left,
                        const ngn_schedule_event_t *right)
{
    return left->kind == right->kind && left->source == right->source &&
           left->probe_index == right->probe_index &&
           left->offset_ms == right->offset_ms &&
           left->deadline_ms == right->deadline_ms;
}

static int default_plan(void)
{
    /* Explicit expected chronology, independent of schedule construction. */
    static const ngn_schedule_event_t expected[] = {
        {NGN_SCHEDULE_SYNC_TX, NGN_NODE_C, 0u, 0u, 40u},
        {NGN_SCHEDULE_PROBE_SLOT, NGN_NODE_A, 0u, 40u, 120u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_A, 0u, 40u, 50u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_A, 1u, 50u, 60u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_A, 2u, 60u, 70u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_A, 3u, 70u, 80u},
        {NGN_SCHEDULE_PROBE_SLOT, NGN_NODE_B, 0u, 120u, 200u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_B, 0u, 120u, 130u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_B, 1u, 130u, 140u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_B, 2u, 140u, 150u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_B, 3u, 150u, 160u},
        {NGN_SCHEDULE_PROBE_SLOT, NGN_NODE_C, 0u, 200u, 280u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_C, 0u, 200u, 210u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_C, 1u, 210u, 220u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_C, 2u, 220u, 230u},
        {NGN_SCHEDULE_PROBE_TX, NGN_NODE_C, 3u, 230u, 240u},
        {NGN_SCHEDULE_COEXIST_OPPORTUNITY, NGN_NODE_UNCONFIGURED, 0u, 280u, 360u},
        {NGN_SCHEDULE_HEALTH_SLOT, NGN_NODE_A, 0u, 360u, 370u},
        {NGN_SCHEDULE_HEALTH_TX, NGN_NODE_A, 0u, 360u, 370u},
        {NGN_SCHEDULE_HEALTH_SLOT, NGN_NODE_B, 0u, 370u, 380u},
        {NGN_SCHEDULE_HEALTH_TX, NGN_NODE_B, 0u, 370u, 380u},
        {NGN_SCHEDULE_HEALTH_SLOT, NGN_NODE_C, 0u, 380u, 390u},
        {NGN_SCHEDULE_HEALTH_TX, NGN_NODE_C, 0u, 380u, 390u}
    };
    const ngn_schedule_config_t config = ngn_schedule_default_config();
    ngn_schedule_plan_t plan;
    size_t i;

    CHECK(config.channel == 6u);
    CHECK(config.burst_count == 4u);
    CHECK(config.missing_epochs == 4u);
    CHECK(config.probe_spacing_ms == 10u);
    CHECK(config.sync_slot_ms == 40u);
    CHECK(config.probe_slot_ms == 80u);
    CHECK(config.coexist_ms == 80u);
    CHECK(config.health_slot_ms == 10u);
    CHECK(ngn_schedule_config_valid(&config));
    CHECK(ngn_schedule_build(&config, &plan));
    CHECK(plan.epoch_ms == 390u);
    CHECK(plan.count == sizeof(expected) / sizeof(expected[0]));
    for (i = 0u; i < plan.count; ++i) {
        CHECK(event_equal(&plan.events[i], &expected[i]));
    }

    /* A late worker is never allowed to replay a completed spacing interval. */
    CHECK(!ngn_schedule_tx_due(&plan.events[2], 39u));
    CHECK(ngn_schedule_tx_due(&plan.events[2], 40u));
    CHECK(ngn_schedule_tx_due(&plan.events[2], 49u));
    CHECK(!ngn_schedule_tx_due(&plan.events[2], 50u));
    CHECK(!ngn_schedule_tx_due(&plan.events[2], 120u));
    CHECK(!ngn_schedule_tx_due(&plan.events[2], UINT32_MAX));
    CHECK(!ngn_schedule_tx_due(&plan.events[1], 40u));
    CHECK(!ngn_schedule_tx_due(&plan.events[16], 280u));
    CHECK(!ngn_schedule_tx_due(NULL, 0u));
    CHECK(!ngn_schedule_event_is_tx((ngn_schedule_event_kind_t)99));
    return 0;
}

static int validation_boundaries(void)
{
    ngn_schedule_config_t config = ngn_schedule_default_config();
    const ngn_schedule_config_t baseline = config;
    ngn_schedule_plan_t plan = {0};

    CHECK(!ngn_schedule_config_valid(NULL));
    CHECK(!ngn_schedule_config_equal(NULL, &config));
    CHECK(!ngn_schedule_config_equal(&config, NULL));
    CHECK(!ngn_schedule_build(&config, NULL));
    plan.epoch_ms = 123u;
    plan.count = 7u;
    CHECK(!ngn_schedule_build(NULL, &plan));
    CHECK(plan.epoch_ms == 123u && plan.count == 7u);

#define INVALID(field, value)                                                  \
    do {                                                                       \
        config = baseline;                                                     \
        config.field = (value);                                                \
        CHECK(!ngn_schedule_config_valid(&config));                             \
        CHECK(!ngn_schedule_build(&config, &plan));                              \
        CHECK(plan.epoch_ms == 123u && plan.count == 7u);                        \
        CHECK(!ngn_schedule_config_equal(&baseline, &config));                  \
    } while (0)
    INVALID(channel, 0u);
    INVALID(channel, 12u);
    INVALID(channel, UINT8_MAX);
    INVALID(burst_count, 0u);
    INVALID(burst_count, 17u);
    INVALID(burst_count, UINT8_MAX);
    INVALID(missing_epochs, 0u);
    INVALID(missing_epochs, 33u);
    INVALID(missing_epochs, UINT8_MAX);
    INVALID(probe_spacing_ms, 0u);
    INVALID(sync_slot_ms, 0u);
    INVALID(probe_slot_ms, 0u);
    INVALID(health_slot_ms, 0u);
    INVALID(probe_spacing_ms, 21u);
    INVALID(probe_slot_ms, 39u);
    INVALID(probe_spacing_ms, UINT16_MAX);
    INVALID(sync_slot_ms, UINT16_MAX);
    INVALID(probe_slot_ms, UINT16_MAX);
    INVALID(coexist_ms, UINT16_MAX);
    INVALID(health_slot_ms, UINT16_MAX);
#undef INVALID

    config = baseline;
    config.probe_slot_ms = 40u;
    CHECK(ngn_schedule_config_valid(&config));
    config.coexist_ms = 0u;
    CHECK(ngn_schedule_config_valid(&config));
    config.burst_count = 1u;
    config.probe_spacing_ms = 1u;
    config.probe_slot_ms = 1u;
    config.sync_slot_ms = 1u;
    config.health_slot_ms = 1u;
    CHECK(ngn_schedule_build(&config, &plan));
    CHECK(plan.epoch_ms == 7u && plan.count == 14u);

    config.sync_slot_ms = 59994u;
    CHECK(ngn_schedule_build(&config, &plan));
    CHECK(plan.epoch_ms == NGN_SCHEDULE_MAX_EPOCH_MS);
    ++config.sync_slot_ms;
    CHECK(!ngn_schedule_config_valid(&config));
    config = baseline;
    CHECK(ngn_schedule_config_equal(&config, &baseline));
    ++config.coexist_ms;
    CHECK(!ngn_schedule_config_equal(&config, &baseline));
    return 0;
}

static int bounded_plans(void)
{
    static const uint16_t spacings[] = {1u, 17u, 1000u};
    uint8_t channel;
    uint8_t burst;
    size_t spacing;

    /* All supported channels and burst counts, including the capacity edge. */
    for (channel = 1u; channel <= 11u; ++channel) {
        for (burst = 1u; burst <= 16u; ++burst) {
            for (spacing = 0u; spacing < 3u; ++spacing) {
                ngn_schedule_config_t config = ngn_schedule_default_config();
                ngn_schedule_plan_t plan;
                ngn_schedule_plan_t again;
                unsigned probes[3] = {0u, 0u, 0u};
                unsigned health[3] = {0u, 0u, 0u};
                uint32_t previous_tx_deadline = 0u;
                size_t i;

                config.channel = channel;
                config.burst_count = burst;
                config.missing_epochs = 32u;
                config.probe_spacing_ms = spacings[spacing];
                config.probe_slot_ms = (uint16_t)(burst * spacings[spacing]);
                CHECK(ngn_schedule_build(&config, &plan));
                CHECK(ngn_schedule_build(&config, &again));
                CHECK(plan.count == 11u + 3u * burst);
                CHECK(plan.count <= NGN_SCHEDULE_MAX_EVENTS);
                CHECK(plan.epoch_ms == again.epoch_ms);
                CHECK(plan.count == again.count);
                for (i = 0u; i < plan.count; ++i) {
                    const ngn_schedule_event_t *event = &plan.events[i];
                    CHECK(event_equal(event, &again.events[i]));
                    CHECK(event->offset_ms <= event->deadline_ms);
                    CHECK(event->deadline_ms <= plan.epoch_ms);
                    CHECK(i == 0u || event->offset_ms >= plan.events[i - 1u].offset_ms);
                    if (ngn_schedule_event_is_tx(event->kind)) {
                        CHECK(event->offset_ms >= previous_tx_deadline);
                        CHECK(event->offset_ms < event->deadline_ms);
                        CHECK(ngn_schedule_tx_due(event, event->offset_ms));
                        CHECK(ngn_schedule_tx_due(event, event->deadline_ms - 1u));
                        CHECK(!ngn_schedule_tx_due(event, event->deadline_ms));
                        previous_tx_deadline = event->deadline_ms;
                    }
                    if (event->kind == NGN_SCHEDULE_PROBE_TX) {
                        CHECK(event->source <= NGN_NODE_C);
                        CHECK(event->probe_index == probes[event->source]);
                        ++probes[event->source];
                    } else if (event->kind == NGN_SCHEDULE_HEALTH_TX) {
                        CHECK(event->source <= NGN_NODE_C);
                        ++health[event->source];
                    }
                }
                CHECK(probes[0] == burst && probes[1] == burst && probes[2] == burst);
                CHECK(health[0] == 1u && health[1] == 1u && health[2] == 1u);
            }
        }
    }
    return 0;
}

int main(void)
{
    CHECK(default_plan() == 0);
    CHECK(validation_boundaries() == 0);
    CHECK(bounded_plans() == 0);
    puts("ngn_schedule: default chronology, deadline boundaries and 528 plans passed");
    return 0;
}

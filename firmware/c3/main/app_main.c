#include "sdkconfig.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "ngn_radio.h"
#include "ngn_radio_esp.h"

static const char *TAG = "nggunage";
static ngn_radio_t radio;
static ngn_transport_t transport;
static ngn_schedule_config_t schedule;
static uint8_t station_mac[NGN_TRANSPORT_MAC_SIZE];
static uint64_t coordinator_session;
static QueueHandle_t diagnostic_queue;
static bool diagnostic_due;

typedef struct {
    uint64_t session;
    uint32_t epoch;
    ngn_radio_state_t state;
    uint8_t present_mask;
    uint8_t bound_mask;
    uint8_t mac[NGN_RADIO_PEER_COUNT][NGN_TRANSPORT_MAC_SIZE];
    ngn_radio_stats_t core;
    ngn_transport_stats_t adapter;
} radio_diagnostic_t;

static ngn_node_id_t configured_node_id(void)
{
#if CONFIG_NGN_NODE_ROLE_A
    return NGN_NODE_A;
#elif CONFIG_NGN_NODE_ROLE_B
    return NGN_NODE_B;
#elif CONFIG_NGN_NODE_ROLE_C
    return NGN_NODE_C;
#else
    return NGN_NODE_UNCONFIGURED;
#endif
}

static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000u;
}

static void radio_event(void *context, const ngn_radio_event_t *event)
{
    (void)context;
    if (event->kind != NGN_RADIO_EVENT_EPOCH &&
        event->kind != NGN_RADIO_EVENT_SCHEDULE) {
        diagnostic_due = true;
    }
}

static void publish_diagnostic(void)
{
    radio_diagnostic_t snapshot = {0};
    size_t i;
    snapshot.session = radio.session_id;
    snapshot.epoch = radio.epoch;
    snapshot.state = radio.state;
    snapshot.present_mask = ngn_radio_present_mask(&radio);
    snapshot.core = radio.stats;
    transport.get_stats(transport.context, &snapshot.adapter);
    for (i = 0u; i < NGN_RADIO_PEER_COUNT; ++i) {
        if (radio.peers[i].bound) {
            snapshot.bound_mask |= (uint8_t)(1u << i);
            memcpy(snapshot.mac[i], radio.peers[i].mac, sizeof(snapshot.mac[i]));
        }
    }
    /* Latest snapshot wins. Console work runs in a separate lower-priority
     * task and cannot block the runtime or its bounded event sink. */
    (void)xQueueOverwrite(diagnostic_queue, &snapshot);
}

static void diagnostic_worker(void *argument)
{
    radio_diagnostic_t previous = {0};
    radio_diagnostic_t snapshot;
    (void)argument;
    for (;;) {
        size_t i;
        if (xQueueReceive(diagnostic_queue, &snapshot, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        ESP_LOGI(TAG,
                 "session=%016" PRIx64 " epoch=%" PRIu32
                 " state=%u present=0x%02x rx=%" PRIu32 " rejected=%" PRIu32
                 " queued=%" PRIu32 " completed=%" PRIu32 " late=%" PRIu32,
                 snapshot.session, snapshot.epoch, (unsigned)snapshot.state,
                 (unsigned)snapshot.present_mask, snapshot.core.rx_accepted,
                 snapshot.core.rx_rejected, snapshot.core.tx_queued,
                 snapshot.adapter.tx_completed,
                 snapshot.core.tx_late_drops + snapshot.adapter.tx_expired);
        for (i = 0u; i < NGN_RADIO_PEER_COUNT; ++i) {
            const uint8_t *mac = snapshot.mac[i];
            if ((snapshot.bound_mask & (1u << i)) != 0u &&
                ((previous.bound_mask & (1u << i)) == 0u ||
                 memcmp(mac, previous.mac[i], NGN_TRANSPORT_MAC_SIZE) != 0)) {
                ESP_LOGI(TAG, "node=%s station=%02x:%02x:%02x:%02x:%02x:%02x",
                         ngn_node_id_name((ngn_node_id_t)i),
                         (unsigned)mac[0], (unsigned)mac[1], (unsigned)mac[2],
                         (unsigned)mac[3], (unsigned)mac[4], (unsigned)mac[5]);
            }
        }
        previous = snapshot;
    }
}

static void radio_worker(void *argument)
{
    uint64_t last_diagnostic;
    (void)argument;
    last_diagnostic = monotonic_ms();
    if (!ngn_radio_init(&radio, configured_node_id(), station_mac, &schedule,
                         &transport, coordinator_session, last_diagnostic,
                         radio_event, NULL)) {
        ESP_LOGE(TAG, "radio core initialization rejected");
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        const uint64_t now = monotonic_ms();
        (void)ngn_radio_service(&radio, now);
        if (diagnostic_due || now - last_diagnostic >= 5000u) {
            publish_diagnostic();
            diagnostic_due = false;
            last_diagnostic = now;
        }
        /* Always yield at least one tick, including user-modified tick rates. */
        vTaskDelay(1u);
    }
}

void app_main(void)
{
    const ngn_node_id_t node_id = configured_node_id();
    const ngn_radio_esp_config_t adapter = {
        .channel = CONFIG_NGN_RADIO_CHANNEL,
        .rx_queue_depth = CONFIG_NGN_RADIO_RX_QUEUE_DEPTH,
        .tx_queue_depth = CONFIG_NGN_RADIO_TX_QUEUE_DEPTH,
        .status_queue_depth = CONFIG_NGN_RADIO_STATUS_QUEUE_DEPTH
    };
    esp_err_t result;

    ESP_LOGI(TAG, "firmware=%s protocol=%u esp-idf=%s node=%s board=%s",
             NGN_FIRMWARE_VERSION, (unsigned)NGN_PROTOCOL_VERSION,
             esp_get_idf_version(), ngn_node_id_name(node_id),
             CONFIG_NGN_BOARD_PROFILE);
    if (!ngn_node_id_is_valid(node_id)) {
        ESP_LOGW(TAG, "role unconfigured; select A, B or C to start the radio");
        return;
    }
    schedule = (ngn_schedule_config_t){
        .channel = CONFIG_NGN_RADIO_CHANNEL,
        .burst_count = CONFIG_NGN_RADIO_BURST_COUNT,
        .missing_epochs = CONFIG_NGN_RADIO_MISSING_EPOCHS,
        .probe_spacing_ms = CONFIG_NGN_RADIO_PROBE_SPACING_MS,
        .sync_slot_ms = CONFIG_NGN_RADIO_SYNC_SLOT_MS,
        .probe_slot_ms = CONFIG_NGN_RADIO_PROBE_SLOT_MS,
        .coexist_ms = CONFIG_NGN_RADIO_COEXIST_MS,
        .health_slot_ms = CONFIG_NGN_RADIO_HEALTH_SLOT_MS
    };
    if (!ngn_schedule_config_valid(&schedule)) {
        ESP_LOGE(TAG, "invalid radio schedule: check burst/spacing and 60000 ms epoch limit");
        return;
    }
    diagnostic_queue = xQueueCreate(1u, sizeof(radio_diagnostic_t));
    if (diagnostic_queue == NULL) {
        ESP_LOGE(TAG, "cannot allocate radio diagnostic queue");
        return;
    }
    result = ngn_radio_esp_start(&adapter, &transport, station_mac);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW startup failed: %s", esp_err_to_name(result));
        vQueueDelete(diagnostic_queue);
        diagnostic_queue = NULL;
        return;
    }
    if (node_id == NGN_NODE_C) {
        unsigned attempt;
        /* Wi-Fi is active before obtaining the coordinator's fresh nonce. */
        for (attempt = 0u; attempt < 4u && coordinator_session == 0u; ++attempt) {
            esp_fill_random(&coordinator_session, sizeof(coordinator_session));
        }
        if (coordinator_session == 0u) {
            ESP_LOGE(TAG, "no nonzero session nonce; protocol transmissions disabled");
            vQueueDelete(diagnostic_queue);
            diagnostic_queue = NULL;
            return;
        }
    }
    if (xTaskCreate(diagnostic_worker, "ngn_radio_log", 3072u, NULL, 1u, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "cannot create radio diagnostic task");
        vQueueDelete(diagnostic_queue);
        diagnostic_queue = NULL;
        return;
    }
    if (xTaskCreate(radio_worker, "ngn_radio", 6144u, NULL, 5u, NULL) != pdPASS) {
        ESP_LOGE(TAG, "cannot create radio scheduler task; restart required");
    }
}

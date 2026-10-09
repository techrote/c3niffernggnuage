#include "ngn_csi_esp.h"

#include <string.h>

#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define NGN_CSI_ESP_MAX_RAW_QUEUE_DEPTH 32u
#define NGN_CSI_ESP_MAX_PACKET_QUEUE_DEPTH 16u
#define NGN_CSI_ESP_MAX_ATTRIBUTION_WINDOW_MS 50u
#define NGN_CSI_ESP_WORKER_STACK_BYTES 6144u
#define NGN_CSI_ESP_WORKER_PRIORITY 2u

typedef struct {
    bool active;
    uint64_t session_id;
    ngn_csi_esp_config_t config;
    QueueHandle_t raw_queue;
    QueueHandle_t packet_queue;
    TaskHandle_t worker;
    ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT];
    ngn_csi_probe_observation_t
        history[NGN_CSI_NODE_COUNT][NGN_CSI_PROBE_HISTORY];
    uint8_t history_next[NGN_CSI_NODE_COUNT];
    ngn_csi_esp_stats_t stats;
} ngn_csi_esp_context_t;

static ngn_csi_esp_context_t s_ctx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static uint64_t monotonic_ms(void)
{
    return (uint64_t)esp_timer_get_time() / 1000u;
}

static bool raw_enqueue(void *context, const ngn_csi_raw_t *record)
{
    QueueHandle_t queue = context;
    return xQueueSend(queue, record, 0u) == pdTRUE;
}

static void add_capture_stats(const ngn_csi_capture_stats_t *delta)
{
    portENTER_CRITICAL(&s_lock);
    s_ctx.stats.capture.captured += delta->captured;
    s_ctx.stats.capture.invalid_input += delta->invalid_input;
    s_ctx.stats.capture.oversize += delta->oversize;
    s_ctx.stats.capture.queue_drops += delta->queue_drops;
    portEXIT_CRITICAL(&s_lock);
}

static void csi_callback(void *context, wifi_csi_info_t *info)
{
    ngn_csi_binding_t bindings[NGN_CSI_NODE_COUNT];
    ngn_csi_capture_stats_t delta = {0};
    ngn_csi_rx_meta_t meta = {0};
    ngn_node_id_t source = NGN_NODE_UNCONFIGURED;
    bool active;
    (void)context;

    portENTER_CRITICAL(&s_lock);
    active = s_ctx.active;
    memcpy(bindings, s_ctx.bindings, sizeof(bindings));
    portEXIT_CRITICAL(&s_lock);

    if (!active) {
        return;
    }
    if (info == NULL) {
        delta.invalid_input = 1u;
        add_capture_stats(&delta);
        return;
    }
    if (!ngn_csi_source_for_mac(bindings, info->mac, &source)) {
        portENTER_CRITICAL(&s_lock);
        ++s_ctx.stats.callback_unknown_source;
        portEXIT_CRITICAL(&s_lock);
        return;
    }

    meta.rssi = (int8_t)info->rx_ctrl.rssi;
    meta.noise_floor = (int8_t)info->rx_ctrl.noise_floor;
    meta.rate = (uint8_t)info->rx_ctrl.rate;
    meta.sig_mode = (uint8_t)info->rx_ctrl.sig_mode;
    meta.mcs = (uint8_t)info->rx_ctrl.mcs;
    meta.cwb = (uint8_t)info->rx_ctrl.cwb;
    meta.smoothing = (uint8_t)info->rx_ctrl.smoothing;
    meta.not_sounding = (uint8_t)info->rx_ctrl.not_sounding;
    meta.aggregation = (uint8_t)info->rx_ctrl.aggregation;
    meta.stbc = (uint8_t)info->rx_ctrl.stbc;
    meta.fec_coding = (uint8_t)info->rx_ctrl.fec_coding;
    meta.sgi = (uint8_t)info->rx_ctrl.sgi;
    meta.ampdu_count = (uint8_t)info->rx_ctrl.ampdu_cnt;
    meta.channel = (uint8_t)info->rx_ctrl.channel;
    meta.secondary_channel = (uint8_t)info->rx_ctrl.secondary_channel;
    meta.antenna = (uint8_t)info->rx_ctrl.ant;
    meta.rx_state = (uint8_t)info->rx_ctrl.rx_state;
    meta.signal_length = (uint16_t)info->rx_ctrl.sig_len;
    meta.rx_sequence = info->rx_seq;
    meta.hardware_timestamp_us = (uint32_t)info->rx_ctrl.timestamp;
    meta.received_ms = monotonic_ms();
    meta.first_word_invalid = info->first_word_invalid;

    (void)ngn_csi_capture(source, info->mac, &meta, info->buf, info->len,
                          raw_enqueue, s_ctx.raw_queue, &delta);
    add_capture_stats(&delta);
}

static void worker(void *argument)
{
    ngn_csi_raw_t raw;
    (void)argument;

    for (;;) {
        ngn_csi_probe_observation_t history[NGN_CSI_PROBE_HISTORY];
        ngn_csi_packet_t packet;
        ngn_csi_decode_result_t result;
        uint16_t window_ms;
        uint8_t channel;

        if (xQueueReceive(s_ctx.raw_queue, &raw, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        portENTER_CRITICAL(&s_lock);
        memcpy(history, s_ctx.history[(size_t)raw.source], sizeof(history));
        window_ms = s_ctx.config.attribution_window_ms;
        channel = s_ctx.config.channel;
        portEXIT_CRITICAL(&s_lock);

        result = ngn_csi_decode(&raw, channel, history,
                                NGN_CSI_PROBE_HISTORY, window_ms, &packet);

        portENTER_CRITICAL(&s_lock);
        ++s_ctx.stats.processed;
        switch (result) {
        case NGN_CSI_DECODE_OK:
            if (!packet.probe_attributed) {
                ++s_ctx.stats.unattributed;
            }
            break;
        case NGN_CSI_DECODE_INVALID_ARGUMENT:
            ++s_ctx.stats.decode_invalid_argument;
            break;
        case NGN_CSI_DECODE_INVALID_LENGTH:
            ++s_ctx.stats.decode_invalid_length;
            break;
        case NGN_CSI_DECODE_RX_ERROR:
            ++s_ctx.stats.decode_rx_error;
            break;
        case NGN_CSI_DECODE_CHANNEL_MISMATCH:
            ++s_ctx.stats.decode_channel_mismatch;
            break;
        }
        portEXIT_CRITICAL(&s_lock);

        if (result != NGN_CSI_DECODE_OK) {
            continue;
        }
        if (xQueueSend(s_ctx.packet_queue, &packet, 0u) != pdTRUE) {
            portENTER_CRITICAL(&s_lock);
            ++s_ctx.stats.packet_queue_drops;
            portEXIT_CRITICAL(&s_lock);
            continue;
        }
        portENTER_CRITICAL(&s_lock);
        ++s_ctx.stats.emitted;
        portEXIT_CRITICAL(&s_lock);
    }
}

static bool config_valid(const ngn_csi_esp_config_t *config)
{
    return config != NULL &&
           config->channel >= 1u && config->channel <= 11u &&
           config->raw_queue_depth >= 1u &&
           config->raw_queue_depth <= NGN_CSI_ESP_MAX_RAW_QUEUE_DEPTH &&
           config->packet_queue_depth >= 1u &&
           config->packet_queue_depth <= NGN_CSI_ESP_MAX_PACKET_QUEUE_DEPTH &&
           config->attribution_window_ms >= 1u &&
           config->attribution_window_ms <=
               NGN_CSI_ESP_MAX_ATTRIBUTION_WINDOW_MS;
}

static void reset_failed_start(void)
{
    if (s_ctx.worker != NULL) {
        vTaskDelete(s_ctx.worker);
    }
    if (s_ctx.raw_queue != NULL) {
        vQueueDelete(s_ctx.raw_queue);
    }
    if (s_ctx.packet_queue != NULL) {
        vQueueDelete(s_ctx.packet_queue);
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
}

esp_err_t ngn_csi_esp_start(const ngn_csi_esp_config_t *config)
{
    wifi_mode_t mode;
    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = false,
        .channel_filter_en = false,
        .manu_scale = false,
        .shift = 0u,
        .dump_ack_en = false
    };
    esp_err_t err;

    if (!config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    if (s_ctx.active || s_ctx.raw_queue != NULL || s_ctx.packet_queue != NULL) {
        portEXIT_CRITICAL(&s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    portEXIT_CRITICAL(&s_lock);

    err = esp_wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return err;
    }
    if (mode != WIFI_MODE_STA) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.config = *config;
    s_ctx.raw_queue =
        xQueueCreate(config->raw_queue_depth, sizeof(ngn_csi_raw_t));
    s_ctx.packet_queue =
        xQueueCreate(config->packet_queue_depth, sizeof(ngn_csi_packet_t));
    if (s_ctx.raw_queue == NULL || s_ctx.packet_queue == NULL) {
        reset_failed_start();
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(worker, "ngn_csi", NGN_CSI_ESP_WORKER_STACK_BYTES,
                    NULL, NGN_CSI_ESP_WORKER_PRIORITY, &s_ctx.worker) != pdPASS) {
        reset_failed_start();
        return ESP_ERR_NO_MEM;
    }

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        reset_failed_start();
        return err;
    }
    /* The scheduled baseline has no AP association. Promiscuous receive lets
     * CSI be delivered for ambient fixed-channel packets, after which the
     * callback admits only already bound A/B/C station MACs. */
    err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) {
        reset_failed_start();
        return err;
    }
    err = esp_wifi_set_csi_rx_cb(csi_callback, NULL);
    if (err != ESP_OK) {
        (void)esp_wifi_set_promiscuous(false);
        reset_failed_start();
        return err;
    }

    portENTER_CRITICAL(&s_lock);
    s_ctx.active = true;
    portEXIT_CRITICAL(&s_lock);
    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&s_lock);
        s_ctx.active = false;
        portEXIT_CRITICAL(&s_lock);
        (void)esp_wifi_set_csi_rx_cb(NULL, NULL);
        (void)esp_wifi_set_promiscuous(false);
        reset_failed_start();
        return err;
    }
    return ESP_OK;
}

bool ngn_csi_esp_active(void)
{
    bool active;
    portENTER_CRITICAL(&s_lock);
    active = s_ctx.active;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool ngn_csi_esp_bind(ngn_node_id_t node,
                      const uint8_t mac[NGN_TRANSPORT_MAC_SIZE])
{
    bool ok;
    portENTER_CRITICAL(&s_lock);
    if (!s_ctx.active) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    ok = ngn_csi_binding_set(s_ctx.bindings, node, mac);
    portEXIT_CRITICAL(&s_lock);
    return ok;
}

bool ngn_csi_esp_set_session(uint64_t session_id)
{
    if (session_id == 0u) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    if (!s_ctx.active) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    if (s_ctx.session_id != session_id) {
        s_ctx.session_id = session_id;
        memset(s_ctx.history, 0, sizeof(s_ctx.history));
        memset(s_ctx.history_next, 0, sizeof(s_ctx.history_next));
    }
    portEXIT_CRITICAL(&s_lock);
    return true;
}

bool ngn_csi_esp_note_probe(ngn_node_id_t source,
                            uint64_t session_id,
                            uint32_t epoch,
                            uint32_t sequence,
                            uint64_t received_ms)
{
    ngn_csi_probe_observation_t *slot;
    size_t index;

    if (!ngn_node_id_is_valid(source) || session_id == 0u) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    if (!s_ctx.active || s_ctx.session_id != session_id) {
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    index = s_ctx.history_next[(size_t)source];
    slot = &s_ctx.history[(size_t)source][index];
    *slot = (ngn_csi_probe_observation_t){
        .valid = true,
        .source = source,
        .session_id = session_id,
        .epoch = epoch,
        .sequence = sequence,
        .received_ms = received_ms
    };
    s_ctx.history_next[(size_t)source] =
        (uint8_t)((index + 1u) % NGN_CSI_PROBE_HISTORY);
    portEXIT_CRITICAL(&s_lock);
    return true;
}

bool ngn_csi_esp_receive(ngn_csi_packet_t *packet)
{
    if (packet == NULL || !ngn_csi_esp_active()) {
        return false;
    }
    return xQueueReceive(s_ctx.packet_queue, packet, 0u) == pdTRUE;
}

void ngn_csi_esp_get_stats(ngn_csi_esp_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *stats = s_ctx.stats;
    portEXIT_CRITICAL(&s_lock);
}

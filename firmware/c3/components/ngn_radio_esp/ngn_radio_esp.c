#include "ngn_radio_esp.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* ESP-IDF v5.5.5 callbacks execute in the Wi-Fi task, not an ISR. */
#define NGN_RADIO_WORKER_PRIORITY 5u
#define NGN_RADIO_WORKER_IDLE_MS 50u

_Static_assert(NGN_TRANSPORT_FRAME_CAPACITY <= ESP_NOW_MAX_DATA_LEN,
               "NGN frames must fit the ESP-NOW v1 transport limit");
_Static_assert(NGN_TRANSPORT_MAC_SIZE == ESP_NOW_ETH_ALEN,
               "transport and ESP-NOW MAC widths must agree");

typedef struct {
    bool active;
    bool in_flight;
    bool completion_ready;
    bool stall_reported;
    uint32_t in_flight_token;
    uint64_t in_flight_since_ms;
    uint64_t active_session_id;
    ngn_transport_tx_result_t completion_result;
    ngn_transport_stats_t stats;
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    QueueHandle_t status_queue;
    TaskHandle_t worker;
} ngn_radio_esp_context_t;

static ngn_radio_esp_context_t s_ctx;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
/* esp_netif_deinit() is unsupported in the pinned SDK. */
static bool s_netif_initialized;
static const uint8_t s_broadcast[NGN_TRANSPORT_MAC_SIZE] = {
    0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu
};

static uint64_t monotonic_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static void increment(uint32_t *counter)
{
    portENTER_CRITICAL(&s_lock);
    ++*counter;
    portEXIT_CRITICAL(&s_lock);
}

static bool context_is_active(const void *context)
{
    bool active;
    if (context != &s_ctx) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    active = s_ctx.active;
    portEXIT_CRITICAL(&s_lock);
    return active;
}

static bool transport_send(void *context, const ngn_transport_tx_t *frame)
{
    if (!context_is_active(context) || frame == NULL) {
        return false;
    }
    if (frame->length == 0u || frame->length > NGN_TRANSPORT_FRAME_CAPACITY) {
        increment(&s_ctx.stats.tx_errors);
        return false;
    }
    if (xQueueSend(s_ctx.tx_queue, frame, 0) != pdTRUE) {
        increment(&s_ctx.stats.tx_queue_drops);
        return false;
    }
    xTaskNotifyGive(s_ctx.worker);
    return true;
}

static bool transport_receive(void *context, ngn_transport_rx_t *frame)
{
    return context_is_active(context) && frame != NULL &&
           xQueueReceive(s_ctx.rx_queue, frame, 0) == pdTRUE;
}

static bool transport_poll_status(void *context, ngn_transport_status_t *status)
{
    return context_is_active(context) && status != NULL &&
           xQueueReceive(s_ctx.status_queue, status, 0) == pdTRUE;
}

static void transport_get_stats(void *context, ngn_transport_stats_t *stats)
{
    if (stats == NULL) {
        return;
    }
    if (context != &s_ctx) {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    portENTER_CRITICAL(&s_lock);
    *stats = s_ctx.stats;
    portEXIT_CRITICAL(&s_lock);
}

static void transport_set_session(void *context, uint64_t session_id)
{
    if (!context_is_active(context)) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_ctx.active_session_id = session_id;
    portEXIT_CRITICAL(&s_lock);
    /* The worker owns queue drainage; changing identity never blocks on it. */
    xTaskNotifyGive(s_ctx.worker);
}

static bool session_is_current(uint64_t session_id)
{
    bool current;
    portENTER_CRITICAL(&s_lock);
    current = session_id != 0u && session_id == s_ctx.active_session_id;
    portEXIT_CRITICAL(&s_lock);
    return current;
}

static void receive_callback(const esp_now_recv_info_t *info,
                              const uint8_t *data,
                              int length)
{
    ngn_transport_rx_t frame;
    if (!context_is_active(&s_ctx)) {
        return;
    }
    if (info == NULL || info->src_addr == NULL || info->des_addr == NULL ||
        data == NULL || length <= 0 ||
        (size_t)length > NGN_TRANSPORT_FRAME_CAPACITY ||
        memcmp(info->des_addr, s_broadcast, sizeof(s_broadcast)) != 0) {
        increment(&s_ctx.stats.rx_invalid);
        return;
    }

    memset(&frame, 0, sizeof(frame));
    memcpy(frame.source_mac, info->src_addr, sizeof(frame.source_mac));
    memcpy(frame.bytes, data, (size_t)length);
    frame.length = (size_t)length;
    frame.received_ms = monotonic_ms();
    if (xQueueSend(s_ctx.rx_queue, &frame, 0) != pdTRUE) {
        increment(&s_ctx.stats.rx_queue_drops);
    }
}

static void send_callback(const esp_now_send_info_t *info,
                           esp_now_send_status_t status)
{
    bool notify = false;
    const bool valid_info =
        info != NULL && info->des_addr != NULL && info->ifidx == WIFI_IF_STA &&
        memcmp(info->des_addr, s_broadcast, sizeof(s_broadcast)) == 0;

    portENTER_CRITICAL(&s_lock);
    if (s_ctx.active) {
        if (!valid_info || !s_ctx.in_flight || s_ctx.completion_ready) {
            ++s_ctx.stats.tx_callbacks_unexpected;
        } else {
            s_ctx.completion_result = status == ESP_NOW_SEND_SUCCESS
                                          ? NGN_TRANSPORT_TX_COMPLETE
                                          : NGN_TRANSPORT_TX_FAILED;
            s_ctx.completion_ready = true;
            notify = true;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (notify) {
        xTaskNotifyGive(s_ctx.worker);
    }
}

static void publish_status(uint32_t token, ngn_transport_tx_result_t result)
{
    const ngn_transport_status_t status = {.token = token, .result = result};
    if (xQueueSend(s_ctx.status_queue, &status, 0) != pdTRUE) {
        increment(&s_ctx.stats.status_queue_drops);
    }
}

/* Only the worker releases an in-flight slot. Status-queue space is irrelevant. */
static bool finish_or_observe_in_flight(uint64_t now_ms)
{
    bool in_flight;
    bool finished = false;
    uint32_t token = 0u;
    ngn_transport_tx_result_t result = NGN_TRANSPORT_TX_FAILED;

    portENTER_CRITICAL(&s_lock);
    if (s_ctx.in_flight && s_ctx.completion_ready) {
        token = s_ctx.in_flight_token;
        result = s_ctx.completion_result;
        s_ctx.in_flight = false;
        s_ctx.completion_ready = false;
        finished = true;
        if (result == NGN_TRANSPORT_TX_COMPLETE) {
            ++s_ctx.stats.tx_completed;
        } else {
            ++s_ctx.stats.tx_errors;
        }
    } else if (s_ctx.in_flight && !s_ctx.stall_reported &&
               now_ms >= s_ctx.in_flight_since_ms &&
               now_ms - s_ctx.in_flight_since_ms >= NGN_RADIO_ESP_STALL_AFTER_MS) {
        ++s_ctx.stats.tx_stalled;
        s_ctx.stall_reported = true;
        /* Keep ownership until the actual callback, even after a timeout. */
    }
    in_flight = s_ctx.in_flight;
    portEXIT_CRITICAL(&s_lock);

    if (finished) {
        publish_status(token, result);
    }
    return in_flight;
}

static void send_from_worker(ngn_transport_tx_t *frame, uint64_t now_ms)
{
    esp_err_t err;

    if (!session_is_current(frame->session_id)) {
        increment(&s_ctx.stats.tx_canceled);
        publish_status(frame->token, NGN_TRANSPORT_TX_CANCELED);
        return;
    }
    if (now_ms >= frame->expires_ms) {
        increment(&s_ctx.stats.tx_expired);
        publish_status(frame->token, NGN_TRANSPORT_TX_EXPIRED);
        return;
    }
    if (frame->prepare != NULL &&
        !frame->prepare(frame->bytes, frame->length, now_ms, frame->reference_ms)) {
        increment(&s_ctx.stats.tx_errors);
        publish_status(frame->token, NGN_TRANSPORT_TX_FAILED);
        return;
    }
    /* Preparation is bounded, but a preemption may have crossed the deadline. */
    now_ms = monotonic_ms();
    if (now_ms >= frame->expires_ms) {
        increment(&s_ctx.stats.tx_expired);
        publish_status(frame->token, NGN_TRANSPORT_TX_EXPIRED);
        return;
    }

    /* Session admission and in-flight ownership are one atomic decision.
     * A session change after this claim may not withdraw this one send. */
    portENTER_CRITICAL(&s_lock);
    if (frame->session_id == 0u ||
        frame->session_id != s_ctx.active_session_id) {
        ++s_ctx.stats.tx_canceled;
        portEXIT_CRITICAL(&s_lock);
        publish_status(frame->token, NGN_TRANSPORT_TX_CANCELED);
        return;
    }
    /* Publish the token before entering the driver: completion can be early. */
    s_ctx.in_flight = true;
    s_ctx.in_flight_token = frame->token;
    s_ctx.in_flight_since_ms = now_ms;
    s_ctx.completion_ready = false;
    s_ctx.stall_reported = false;
    portEXIT_CRITICAL(&s_lock);

    err = esp_now_send(s_broadcast, frame->bytes, frame->length);
    if (err != ESP_OK) {
        /* A synchronous driver rejection did not accept a pending send. */
        portENTER_CRITICAL(&s_lock);
        s_ctx.in_flight = false;
        s_ctx.completion_ready = false;
        ++s_ctx.stats.tx_errors;
        portEXIT_CRITICAL(&s_lock);
        publish_status(frame->token, NGN_TRANSPORT_TX_FAILED);
    }
}

static void radio_worker(void *argument)
{
    ngn_transport_tx_t frame;
    TickType_t idle_ticks = pdMS_TO_TICKS(NGN_RADIO_WORKER_IDLE_MS);
    (void)argument;
    if (idle_ticks == 0u) {
        idle_ticks = 1u;
    }

    /* start() enables callback-visible state before this initial notification. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
        if (!finish_or_observe_in_flight(monotonic_ms()) &&
            xQueueReceive(s_ctx.tx_queue, &frame, 0) == pdTRUE) {
            send_from_worker(&frame, monotonic_ms());
            continue;
        }
        (void)ulTaskNotifyTake(pdTRUE, idle_ticks);
    }
}

static bool queue_depth_is_valid(uint16_t depth)
{
    return depth >= 1u && depth <= NGN_RADIO_ESP_MAX_QUEUE_DEPTH;
}

esp_err_t ngn_radio_esp_start(const ngn_radio_esp_config_t *config,
                             ngn_transport_t *out_transport,
                             uint8_t out_station_mac[NGN_TRANSPORT_MAC_SIZE])
{
    esp_err_t err;
    wifi_mode_t existing_mode;
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_now_peer_info_t peer;
    nvs_stats_t nvs_stats;
    uint8_t station_mac[NGN_TRANSPORT_MAC_SIZE];
    bool owns_nvs = false;
    bool owns_event_loop = false;
    bool owns_wifi = false;
    bool wifi_started = false;
    bool owns_espnow = false;

    if (config == NULL || out_transport == NULL || out_station_mac == NULL ||
        config->channel < 1u || config->channel > 11u ||
        !queue_depth_is_valid(config->rx_queue_depth) ||
        !queue_depth_is_valid(config->tx_queue_depth) ||
        !queue_depth_is_valid(config->status_queue_depth)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (context_is_active(&s_ctx)) {
        return ESP_ERR_INVALID_STATE;
    }
    err = esp_wifi_get_mode(&existing_mode);
    if (err != ESP_ERR_WIFI_NOT_INIT) {
        return err == ESP_OK ? ESP_ERR_INVALID_STATE : err;
    }

    memset(out_transport, 0, sizeof(*out_transport));
    memset(out_station_mac, 0, NGN_TRANSPORT_MAC_SIZE);
    memset(&s_ctx, 0, sizeof(s_ctx));

    s_ctx.rx_queue = xQueueCreate(config->rx_queue_depth, sizeof(ngn_transport_rx_t));
    s_ctx.tx_queue = xQueueCreate(config->tx_queue_depth, sizeof(ngn_transport_tx_t));
    s_ctx.status_queue =
        xQueueCreate(config->status_queue_depth, sizeof(ngn_transport_status_t));
    if (s_ctx.rx_queue == NULL || s_ctx.tx_queue == NULL ||
        s_ctx.status_queue == NULL) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    err = nvs_get_stats(NULL, &nvs_stats);
    if (err == ESP_ERR_NVS_NOT_INITIALIZED || err == ESP_ERR_NVS_PART_NOT_FOUND) {
        err = nvs_flash_init();
        if (err != ESP_OK) {
            goto fail;
        }
        owns_nvs = true;
    } else if (err != ESP_OK) {
        goto fail;
    }

    if (!s_netif_initialized) {
        err = esp_netif_init();
        if (err != ESP_OK) {
            goto fail;
        }
        s_netif_initialized = true;
    }
    err = esp_event_loop_create_default();
    if (err == ESP_OK) {
        owns_event_loop = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        goto fail;
    }

    /* Never read or persist a saved AP configuration for the no-router path. */
    wifi_config.nvs_enable = 0;
    err = esp_wifi_init(&wifi_config);
    if (err != ESP_OK) {
        goto fail;
    }
    owns_wifi = true;
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        goto fail;
    }
    wifi_started = true;
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_wifi_set_channel(config->channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_wifi_get_mac(WIFI_IF_STA, station_mac);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_now_init();
    if (err != ESP_OK) {
        goto fail;
    }
    owns_espnow = true;
    memset(&peer, 0, sizeof(peer));
    memcpy(peer.peer_addr, s_broadcast, sizeof(peer.peer_addr));
    peer.channel = config->channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_now_register_recv_cb(receive_callback);
    if (err != ESP_OK) {
        goto fail;
    }
    err = esp_now_register_send_cb(send_callback);
    if (err != ESP_OK) {
        goto fail;
    }
    if (xTaskCreate(radio_worker,
                    "ngn_radio_tx",
                    NGN_RADIO_ESP_WORKER_STACK_BYTES,
                    NULL,
                    NGN_RADIO_WORKER_PRIORITY,
                    &s_ctx.worker) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    portENTER_CRITICAL(&s_lock);
    s_ctx.active = true;
    portEXIT_CRITICAL(&s_lock);
    *out_transport = (ngn_transport_t){
        .context = &s_ctx,
        .send = transport_send,
        .receive = transport_receive,
        .poll_status = transport_poll_status,
        .get_stats = transport_get_stats,
        .set_session = transport_set_session
    };
    memcpy(out_station_mac, station_mac, sizeof(station_mac));
    xTaskNotifyGive(s_ctx.worker);
    return ESP_OK;

fail:
    /* Callbacks remain inactive until all fallible startup operations succeed. */
    if (owns_espnow) {
        (void)esp_now_unregister_recv_cb();
        (void)esp_now_unregister_send_cb();
        (void)esp_now_deinit();
    }
    if (wifi_started) {
        (void)esp_wifi_stop();
    }
    if (owns_wifi) {
        (void)esp_wifi_deinit();
    }
    if (owns_event_loop) {
        (void)esp_event_loop_delete_default();
    }
    if (owns_nvs) {
        (void)nvs_flash_deinit();
    }
    if (s_ctx.rx_queue != NULL) {
        vQueueDelete(s_ctx.rx_queue);
    }
    if (s_ctx.tx_queue != NULL) {
        vQueueDelete(s_ctx.tx_queue);
    }
    if (s_ctx.status_queue != NULL) {
        vQueueDelete(s_ctx.status_queue);
    }
    memset(&s_ctx, 0, sizeof(s_ctx));
    return err;
}

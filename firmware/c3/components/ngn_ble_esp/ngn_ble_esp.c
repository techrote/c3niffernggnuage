#include "ngn_ble_esp.h"

#include <string.h>

#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "mbedtls/md.h"

#define NGN_BLE_LEGACY_ADV_MAX_LEN 31u

static const char *TAG = "ngn_ble";

typedef struct {
    uint64_t timestamp_ms;
    uint8_t address_type;
    uint8_t address[NGN_BLE_ADDRESS_LEN];
    int8_t rssi_dbm;
    uint8_t advertisement_type;
    uint8_t payload_length;
    uint8_t payload[NGN_BLE_LEGACY_ADV_MAX_LEN];
} ngn_ble_scan_report_t;

typedef struct {
    bool started;
    ngn_node_id_t node_id;
    uint8_t session_nonce[NGN_BLE_SESSION_NONCE_MAX_LEN];
    size_t session_nonce_len;
    QueueHandle_t queue;
    ngn_ble_tracker_t tracker;
    ngn_ble_esp_stats_t stats;
} ngn_ble_esp_context_t;

static ngn_ble_esp_context_t s_ctx;

static uint64_t monotonic_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static bool sha256_digest(void *ctx,
                          const uint8_t *data,
                          size_t data_len,
                          uint8_t digest[NGN_BLE_DIGEST_LEN])
{
    const mbedtls_md_info_t *info;
    (void)ctx;

    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info != NULL && mbedtls_md(info, data, data_len, digest) == 0;
}

static void log_raw_address_if_enabled(const ngn_ble_scan_report_t *report)
{
#if CONFIG_NGN_BLE_DIAGNOSTIC_RAW_ADDRESS
    ESP_LOGD(TAG,
             "RAW-ADDRESS-DIAGNOSTIC type=%u addr=%02x:%02x:%02x:%02x:%02x:%02x rssi=%d",
             (unsigned)report->address_type,
             report->address[5], report->address[4], report->address[3],
             report->address[2], report->address[1], report->address[0],
             (int)report->rssi_dbm);
#else
    (void)report;
#endif
}

static bool report_to_observation(const ngn_ble_scan_report_t *report,
                                  ngn_ble_observation_t *observation)
{
    struct ble_hs_adv_fields fields;
    size_t i;
    int rc;

    memset(observation, 0, sizeof(*observation));
    observation->timestamp_ms = report->timestamp_ms;
    observation->receiver = s_ctx.node_id;
    observation->address_type = report->address_type;
    memcpy(observation->address, report->address, sizeof(observation->address));
    observation->rssi_dbm = report->rssi_dbm;
    observation->advertisement_type = report->advertisement_type;
    observation->payload_length = report->payload_length;
    observation->payload_signature =
        ngn_ble_payload_signature(report->payload, report->payload_length);

    memset(&fields, 0, sizeof(fields));
    rc = ble_hs_adv_parse_fields(&fields, report->payload, report->payload_length);
    if (rc != 0) {
        return false;
    }

    observation->flags_present = fields.flags != 0u;
    observation->flags = fields.flags;

    if (fields.mfg_data != NULL && fields.mfg_data_len >= 2u) {
        observation->company_id_present = true;
        observation->company_id = (uint16_t)fields.mfg_data[0] |
                                  ((uint16_t)fields.mfg_data[1] << 8u);
    }

    {
        const size_t uuid16_count = (size_t)fields.num_uuids16;
        const size_t uuid32_count = (size_t)fields.num_uuids32;
        const size_t uuid128_count = (size_t)fields.num_uuids128;

        observation->service_uuid16_count =
            uuid16_count < NGN_BLE_MAX_SERVICE_UUID16
                ? (uint8_t)uuid16_count
                : (uint8_t)NGN_BLE_MAX_SERVICE_UUID16;
        for (i = 0u; i < observation->service_uuid16_count; ++i) {
            observation->service_uuid16[i] = ble_uuid_u16(&fields.uuids16[i].u);
        }
        observation->service_uuid32_count =
            uuid32_count > UINT8_MAX ? UINT8_MAX : (uint8_t)uuid32_count;
        observation->service_uuid128_count =
            uuid128_count > UINT8_MAX ? UINT8_MAX : (uint8_t)uuid128_count;
    }

    return ngn_ble_observation_normalize(observation);
}

static void ble_worker_task(void *arg)
{
    ngn_ble_scan_report_t report;
    (void)arg;

    for (;;) {
        if (xQueueReceive(s_ctx.queue, &report, pdMS_TO_TICKS(100)) == pdTRUE) {
            ngn_ble_observation_t observation;
            ngn_ble_track_key_t key;

            log_raw_address_if_enabled(&report);
            if (!report_to_observation(&report, &observation)) {
                ++s_ctx.stats.parse_drops;
                continue;
            }
            if (!ngn_ble_derive_session_key(s_ctx.session_nonce,
                                            s_ctx.session_nonce_len,
                                            &observation,
                                            sha256_digest,
                                            NULL,
                                            &key)) {
                ++s_ctx.stats.key_failures;
                continue;
            }
            if (!ngn_ble_tracker_observe(&s_ctx.tracker, key, &observation)) {
                ++s_ctx.stats.tracker_failures;
            }
        }

        ngn_ble_tracker_tick(&s_ctx.tracker, monotonic_ms());
    }
}

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    ngn_ble_scan_report_t report;
    const ble_addr_t *current_addr;
    (void)arg;

    if (event->type != BLE_GAP_EVENT_DISC) {
        return 0;
    }

    if (event->disc.length_data > NGN_BLE_LEGACY_ADV_MAX_LEN) {
        ++s_ctx.stats.parse_drops;
        return 0;
    }

    current_addr = &event->disc.addr;
#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)
    /* Never substitute a resolved identity for the current over-the-air RPA. */
    current_addr = &event->disc.ota_addr;
#endif

    memset(&report, 0, sizeof(report));
    report.timestamp_ms = monotonic_ms();
    report.address_type = current_addr->type;
    memcpy(report.address, current_addr->val, sizeof(report.address));
    report.rssi_dbm = event->disc.rssi;
    report.advertisement_type = event->disc.event_type;
    report.payload_length = event->disc.length_data;
    memcpy(report.payload, event->disc.data, report.payload_length);

    ++s_ctx.stats.scan_reports;
    if (xQueueSend(s_ctx.queue, &report, 0) != pdTRUE) {
        ++s_ctx.stats.queue_drops;
    }
    return 0;
}

static int start_passive_scan(void)
{
    struct ble_gap_disc_params params;
    uint8_t own_addr_type;
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to infer local address type: rc=%d", rc);
        return rc;
    }

    memset(&params, 0, sizeof(params));
    params.passive = 1;
    params.filter_duplicates = 0;
    params.itvl = 0;
    params.window = 0;
    params.filter_policy = 0;
    params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to start passive scan: rc=%d", rc);
    }
    return rc;
}

static void ble_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
}

static void ble_on_sync(void)
{
    const int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to establish local BLE identity: rc=%d", rc);
        return;
    }

    (void)start_passive_scan();
}

static void ble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ngn_ble_esp_start(const ngn_ble_esp_config_t *config)
{
    esp_err_t err;
    BaseType_t task_result;

    if (config == NULL ||
        !ngn_node_id_is_valid(config->node_id) ||
        config->session_nonce == NULL ||
        config->session_nonce_len < NGN_BLE_SESSION_NONCE_MIN_LEN ||
        config->session_nonce_len > NGN_BLE_SESSION_NONCE_MAX_LEN ||
        config->sink == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ctx.started) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.node_id = config->node_id;
    s_ctx.session_nonce_len = config->session_nonce_len;
    memcpy(s_ctx.session_nonce, config->session_nonce, config->session_nonce_len);

    if (!ngn_ble_tracker_init(&s_ctx.tracker,
                              &config->tracker,
                              config->sink,
                              config->sink_ctx)) {
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx.queue = xQueueCreate(CONFIG_NGN_BLE_QUEUE_DEPTH,
                               sizeof(ngn_ble_scan_report_t));
    if (s_ctx.queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return err;
    }

    task_result = xTaskCreate(ble_worker_task,
                              "ngn_ble_worker",
                              CONFIG_NGN_BLE_WORKER_STACK_SIZE,
                              NULL,
                              5,
                              NULL);
    if (task_result != pdPASS) {
        (void)nimble_port_deinit();
        vQueueDelete(s_ctx.queue);
        s_ctx.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

#if CONFIG_NGN_BLE_DIAGNOSTIC_RAW_ADDRESS
    ESP_LOGW(TAG,
             "RAW BLE ADDRESS DIAGNOSTICS ENABLED: console output is transient debug data, not a default experiment record");
#endif

    s_ctx.started = true;
    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

bool ngn_ble_esp_is_started(void)
{
    return s_ctx.started;
}

void ngn_ble_esp_get_stats(ngn_ble_esp_stats_t *out_stats)
{
    if (out_stats != NULL) {
        *out_stats = s_ctx.stats;
    }
}

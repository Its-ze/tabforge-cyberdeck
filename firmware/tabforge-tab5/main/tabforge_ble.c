#include "tabforge_ble.h"

#include <string.h>
#include "esp_hosted.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TF_BLE_NAME "TabForge Tab5"
#define TF_BLE_PACKET_MAX 384
#define TF_BLE_RESPONSE_MAX 256
#define TF_BLE_QUEUE_DEPTH 8

typedef enum {
    TF_BLE_EVENT_RX = 0,
    TF_BLE_EVENT_CONNECTED,
    TF_BLE_EVENT_DISCONNECTED,
} tf_ble_event_kind_t;

typedef struct {
    tf_ble_event_kind_t kind;
    char line[TF_BLE_PACKET_MAX];
} tf_ble_event_t;

static const char *TAG = "tabforge_ble";

/* UUID bytes are stored least-significant byte first by NimBLE. */
static const ble_uuid128_t s_service_uuid =
    BLE_UUID128_INIT(0x00, 0x50, 0xdf, 0x8f, 0x9b, 0x4c, 0x1c, 0x9b,
                     0x77, 0x4c, 0x7b, 0x2e, 0x01, 0xa0, 0x2e, 0x7d);
static const ble_uuid128_t s_rx_uuid =
    BLE_UUID128_INIT(0x01, 0x50, 0xdf, 0x8f, 0x9b, 0x4c, 0x1c, 0x9b,
                     0x77, 0x4c, 0x7b, 0x2e, 0x01, 0xa0, 0x2e, 0x7d);
static const ble_uuid128_t s_tx_uuid =
    BLE_UUID128_INIT(0x02, 0x50, 0xdf, 0x8f, 0x9b, 0x4c, 0x1c, 0x9b,
                     0x77, 0x4c, 0x7b, 0x2e, 0x01, 0xa0, 0x2e, 0x7d);

static QueueHandle_t s_event_queue;
static tabforge_ble_rx_cb_t s_rx_cb;
static tabforge_ble_link_cb_t s_link_cb;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_response[TF_BLE_RESPONSE_MAX] =
    "{\"tabforge\":\"card.display.ack\",\"state\":\"ready\",\"detail\":\"TabForge Bluetooth ready.\"}";
static uint16_t s_tx_value_handle;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_synced;
static bool s_connected;
static uint32_t s_rx_packets;
static uint32_t s_connections;

static void queue_link_event(tf_ble_event_kind_t kind)
{
    if (!s_event_queue) return;
    tf_ble_event_t event = {.kind = kind};
    (void)xQueueSend(s_event_queue, &event, 0);
}

static int gatt_access_cb(uint16_t conn_handle,
                          uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    uintptr_t characteristic = (uintptr_t)arg;
    if (characteristic == 1U && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        tf_ble_event_t event = {.kind = TF_BLE_EVENT_RX};
        uint16_t length = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om,
                                     event.line,
                                     sizeof(event.line) - 1U,
                                     &length);
        if (rc != 0 || length == 0U || length >= sizeof(event.line)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        event.line[length] = '\0';
        if (!s_event_queue || xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return 0;
    }

    if (characteristic == 2U && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char response[TF_BLE_RESPONSE_MAX];
        portENTER_CRITICAL(&s_lock);
        strlcpy(response, s_response, sizeof(response));
        portEXIT_CRITICAL(&s_lock);
        return os_mbuf_append(ctxt->om, response, strlen(response)) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_rx_uuid.u,
                .access_cb = gatt_access_cb,
                .arg = (void *)1U,
                .flags = BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_WRITE_NO_RSP |
                         BLE_GATT_CHR_F_WRITE_ENC,
            },
            {
                .uuid = &s_tx_uuid.u,
                .access_cb = gatt_access_cb,
                .arg = (void *)2U,
                .val_handle = &s_tx_value_handle,
                .flags = BLE_GATT_CHR_F_READ |
                         BLE_GATT_CHR_F_READ_ENC |
                         BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

static void advertise(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            portENTER_CRITICAL(&s_lock);
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            s_connections++;
            portEXIT_CRITICAL(&s_lock);
            queue_link_event(TF_BLE_EVENT_CONNECTED);
            (void)ble_gap_security_initiate(event->connect.conn_handle);
            ESP_LOGI(TAG, "Cardputer BLE link connected handle=%u", event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "BLE connection failed status=%d", event->connect.status);
            advertise();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&s_lock);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected = false;
        portEXIT_CRITICAL(&s_lock);
        queue_link_event(TF_BLE_EVENT_DISCONNECTED);
        ESP_LOGI(TAG, "Cardputer BLE link disconnected reason=%d", event->disconnect.reason);
        advertise();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "BLE encryption status=%d", event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG,
                 "BLE response subscription notify=%u indicate=%u",
                 event->subscribe.cur_notify,
                 event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE MTU=%u", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

static void advertise(void)
{
    if (!s_synced) return;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE advertising fields failed rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields response = {0};
    response.name = (uint8_t *)TF_BLE_NAME;
    response.name_len = strlen(TF_BLE_NAME);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "BLE scan response failed rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "BLE advertising start failed rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising as %s", TF_BLE_NAME);
    }
}

static void on_reset(int reason)
{
    s_synced = false;
    ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "NimBLE address setup failed rc=%d", rc);
        return;
    }
    s_synced = true;
    advertise();
}

static void host_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void event_task(void *arg)
{
    (void)arg;
    tf_ble_event_t event;
    for (;;) {
        if (xQueueReceive(s_event_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (event.kind == TF_BLE_EVENT_RX) {
            portENTER_CRITICAL(&s_lock);
            s_rx_packets++;
            portEXIT_CRITICAL(&s_lock);
            if (s_rx_cb) (void)s_rx_cb(event.line);
        } else if (event.kind == TF_BLE_EVENT_CONNECTED) {
            if (s_link_cb) s_link_cb(true);
        } else if (event.kind == TF_BLE_EVENT_DISCONNECTED) {
            if (s_link_cb) s_link_cb(false);
        }
    }
}

esp_err_t tabforge_ble_start(tabforge_ble_rx_cb_t rx_cb, tabforge_ble_link_cb_t link_cb)
{
    if (s_event_queue) return ESP_ERR_INVALID_STATE;
    s_rx_cb = rx_cb;
    s_link_cb = link_cb;
    s_event_queue = xQueueCreate(TF_BLE_QUEUE_DEPTH, sizeof(tf_ble_event_t));
    if (!s_event_queue) return ESP_ERR_NO_MEM;

    esp_err_t err = esp_hosted_connect_to_slave();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-Hosted slave connection failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_hosted_bt_controller_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hosted Bluetooth controller init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_hosted_bt_controller_enable();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Hosted Bluetooth controller enable failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(TF_BLE_NAME);
    int rc = ble_gatts_count_cfg(s_gatt_services);
    if (rc == 0) rc = ble_gatts_add_svcs(s_gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT service registration failed rc=%d", rc);
        return ESP_FAIL;
    }

    if (xTaskCreate(event_task, "tabforge-ble-events", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void tabforge_ble_set_response(const char *response)
{
    if (!response) return;
    portENTER_CRITICAL(&s_lock);
    strlcpy(s_response, response, sizeof(s_response));
    uint16_t conn_handle = s_conn_handle;
    bool connected = s_connected;
    portEXIT_CRITICAL(&s_lock);

    if (connected && conn_handle != BLE_HS_CONN_HANDLE_NONE && s_tx_value_handle != 0U) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(response, strlen(response));
        if (om) (void)ble_gatts_notify_custom(conn_handle, s_tx_value_handle, om);
    }
}

bool tabforge_ble_connected(void)
{
    portENTER_CRITICAL(&s_lock);
    bool connected = s_connected;
    portEXIT_CRITICAL(&s_lock);
    return connected;
}

uint32_t tabforge_ble_rx_count(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t count = s_rx_packets;
    portEXIT_CRITICAL(&s_lock);
    return count;
}

uint32_t tabforge_ble_connection_count(void)
{
    portENTER_CRITICAL(&s_lock);
    uint32_t count = s_connections;
    portEXIT_CRITICAL(&s_lock);
    return count;
}

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_heap_caps.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "usb/cdc_acm_host.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"

#ifndef TABFORGE_VERSION
#define TABFORGE_VERSION "0.1.0"
#endif

#ifndef LV_SYMBOL_ENVELOPE
#define LV_SYMBOL_ENVELOPE "[MSG]"
#endif
#ifndef LV_SYMBOL_WIFI
#define LV_SYMBOL_WIFI "[RF]"
#endif
#ifndef LV_SYMBOL_SHUFFLE
#define LV_SYMBOL_SHUFFLE "[MESH]"
#endif
#ifndef LV_SYMBOL_KEYBOARD
#define LV_SYMBOL_KEYBOARD "[KEY]"
#endif
#ifndef LV_SYMBOL_EYE_OPEN
#define LV_SYMBOL_EYE_OPEN "[IR]"
#endif
#ifndef LV_SYMBOL_AUDIO
#define LV_SYMBOL_AUDIO "[MIC]"
#endif
#ifndef LV_SYMBOL_USB
#define LV_SYMBOL_USB "[USB]"
#endif
#ifndef LV_SYMBOL_SD_CARD
#define LV_SYMBOL_SD_CARD "[SD]"
#endif
#ifndef LV_SYMBOL_DOWNLOAD
#define LV_SYMBOL_DOWNLOAD "[OTA]"
#endif
#ifndef LV_SYMBOL_LIST
#define LV_SYMBOL_LIST "[APPS]"
#endif
#ifndef LV_SYMBOL_SETTINGS
#define LV_SYMBOL_SETTINGS "[SET]"
#endif
#ifndef LV_SYMBOL_REFRESH
#define LV_SYMBOL_REFRESH "[ROT]"
#endif

#define TABFORGE_TAG "TabForge"
#define TABFORGE_SD_ROOT BSP_SD_MOUNT_POINT
#define TABFORGE_CONFIG_PATH TABFORGE_SD_ROOT "/tabforge/config.json"
#define TABFORGE_EVENT_LOG_PATH TABFORGE_SD_ROOT "/tabforge/logs/events.jsonl"
#define TABFORGE_MANIFEST_URL "https://its-ze.github.io/tabforge-cyberdeck/manifest.json"

#define IMU_SAMPLE_PERIOD_MS 100
#define ROTATION_CHECK_MS 150
#define ROTATION_CONFIRM_SAMPLES 2
#define STATS_REFRESH_MS 1000
#define MIC_SAMPLE_RATE 16000
#define MIC_SAMPLES 512
#define USB_CDC_BAUDRATE 115200
#define USB_CDC_SCAN_TIMEOUT_MS 2500
#define TABFORGE_EXT5V_EN IO_EXPANDER_PIN_NUM_2
#define TABFORGE_CHARGE_ENABLE IO_EXPANDER_PIN_NUM_7
#define TABFORGE_GROVE_TX_GPIO GPIO_NUM_53
#define TABFORGE_GROVE_RX_GPIO GPIO_NUM_54
#define TABFORGE_IR_TX_GPIO TABFORGE_GROVE_TX_GPIO
#define TABFORGE_IR_RX_GPIO TABFORGE_GROVE_RX_GPIO
#define GROVE_UART_NUM UART_NUM_1
#define GROVE_UART_BAUDRATE 115200
#define GROVE_UART_RX_BUF_SIZE 2048
#define GROVE_UART_READ_CHUNK 128
#define TABFORGE_SD_MAX_FILES 8
#define TABFORGE_SD_ALLOC_UNIT_SIZE (16 * 1024)
#define TABFORGE_I2C_SPEED_HZ 400000
#define TABFORGE_BATTERY_I2C_ADDR 0x41
#define TABFORGE_BATTERY_REG_BUS_VOLTAGE 0x02
#define TABFORGE_BATTERY_PRESENT_MV 5000
#define TABFORGE_BATTERY_MIN_MV 6000
#define TABFORGE_BATTERY_MAX_MV 8400
#define TABFORGE_BATTERY_POLL_MS 10000
#define TABFORGE_NVS_NAMESPACE "tabforge"
#define TABFORGE_NVS_SCREEN_TIMEOUT_KEY "scr_to_idx"
#define TABFORGE_NVS_WIFI_SSID_KEY "wifi_ssid"
#define TABFORGE_NVS_WIFI_PASS_KEY "wifi_pass"
#define TABFORGE_SCREEN_WAKE_GRACE_MS 2000
#define TABFORGE_SCREEN_DIM_GRACE_MS 10000
#define TABFORGE_WIFI_MAX_APS 8
#define TABFORGE_WIFI_MAX_SSID 33
#define TABFORGE_WIFI_MAX_PASSWORD 65
#define TABFORGE_WIFI_CONNECT_TIMEOUT_MS 15000
#define TABFORGE_OTA_MANIFEST_MAX_BYTES 8192
#define TABFORGE_OTA_HTTP_CHUNK_SIZE 4096
#define TABFORGE_PAHUB_I2C_ADDR 0x70

typedef enum {
    FEATURE_ACTIVE,
    FEATURE_PLANNED,
} feature_state_t;

typedef enum {
    APP_NONE,
    APP_WIFI,
    APP_MESSAGES,
    APP_MESHCORE,
    APP_TDECK,
    APP_IR,
    APP_RECORDER,
    APP_USB,
    APP_FILES,
    APP_UPDATE,
} app_id_t;

typedef struct {
    const char *code;
    const char *name;
    const char *summary;
    const char *metric;
    const char *event_name;
    app_id_t app_id;
    feature_state_t state;
    uint32_t accent;
} feature_tile_t;

typedef enum {
    MIC_STATE_PENDING,
    MIC_STATE_ONLINE,
    MIC_STATE_INIT_FAILED,
    MIC_STATE_OPEN_FAILED,
    MIC_STATE_READ_FAILED,
} mic_state_t;

typedef enum {
    USB_STATE_OFF,
    USB_STATE_POWERED,
    USB_STATE_SCANNING,
    USB_STATE_OPEN,
    USB_STATE_DISCONNECTED,
    USB_STATE_ERROR,
} usb_state_t;

typedef enum {
    WIFI_STATE_OFF,
    WIFI_STATE_READY,
    WIFI_STATE_SCANNING,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_ERROR,
} wifi_state_t;

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_FETCHING_MANIFEST,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_READY_REBOOT,
    OTA_STATE_ERROR,
} ota_state_t;

typedef enum {
    NAV_PAGE_APPS,
    NAV_PAGE_SETTINGS,
    NAV_PAGE_UPDATE,
    NAV_PAGE_APP,
} nav_page_t;

typedef struct {
    const char *label;
    uint32_t seconds;
} screen_timeout_option_t;

typedef struct {
    lv_obj_t *button;
    lv_obj_t *icon;
    lv_obj_t *label;
} nav_button_refs_t;

typedef struct {
    lv_obj_t *status_label;
    lv_obj_t *battery_label;
    lv_obj_t *battery_card_label;
    lv_obj_t *sd_label;
    lv_obj_t *addon_label;
    lv_obj_t *mode_label;
    lv_obj_t *mode_detail_label;
    lv_obj_t *uptime_label;
    lv_obj_t *heap_label;
    lv_obj_t *psram_label;
    lv_obj_t *mic_label;
    lv_obj_t *tilt_label;
    lv_obj_t *heart_label;
    lv_obj_t *screen_label;
    lv_obj_t *settings_screen_label;
    lv_obj_t *wifi_label;
    lv_obj_t *wifi_scan_label;
    lv_obj_t *ota_label;
    lv_obj_t *wifi_ssid_textarea;
    lv_obj_t *wifi_password_textarea;
    lv_obj_t *wifi_keyboard;
    lv_obj_t *accessory_label;
    lv_obj_t *usb_label;
    lv_obj_t *ir_label;
    lv_obj_t *gyro_label;
    lv_obj_t *rotation_label;
    lv_obj_t *activity_title_label;
    lv_obj_t *activity_detail_label;
    lv_obj_t *dock_mode_label;
    lv_obj_t *dock_auto_label;
    lv_obj_t *page_apps;
    lv_obj_t *page_settings;
    lv_obj_t *page_update;
    lv_obj_t *page_app;
    nav_button_refs_t nav_apps;
    nav_button_refs_t nav_settings;
    nav_button_refs_t nav_mode;
    nav_button_refs_t nav_auto;
    nav_button_refs_t nav_update;
    lv_obj_t *heap_bar;
    lv_obj_t *psram_bar;
    lv_obj_t *mic_bar;
} ui_refs_t;

typedef struct {
    char ssid[TABFORGE_WIFI_MAX_SSID];
    char password[TABFORGE_WIFI_MAX_PASSWORD];
    bool configured;
    bool auto_connect;
} wifi_credentials_t;

static const feature_tile_t g_tiles[] = {
    {LV_SYMBOL_WIFI, "Wi-Fi", "Scan, connect, and prepare internet OTA.", "Internet", "tile_wifi", APP_WIFI, FEATURE_ACTIVE, 0x70a7ff},
    {LV_SYMBOL_ENVELOPE, "Messages", "Meshtastic C6L channel text and direct sends.", "Grove", "tile_meshtastic", APP_MESSAGES, FEATURE_ACTIVE, 0x43d17a},
    {LV_SYMBOL_SHUFFLE, "MeshCore", "Switchable command profile for MeshCore console work.", "Profile", "tile_meshcore", APP_MESHCORE, FEATURE_ACTIVE, 0x61d5f0},
    {LV_SYMBOL_KEYBOARD, "T-Deck", "Companion bridge for the LilyGO T-Deck/Z-Deck flow.", "Bridge", "tile_tdeck", APP_TDECK, FEATURE_ACTIVE, 0xf0bf4f},
    {LV_SYMBOL_EYE_OPEN, "IR Lab", "Learn, label, replay, and store IR macros on SD.", "38 kHz", "tile_ir", APP_IR, FEATURE_ACTIVE, 0xff7a66},
    {LV_SYMBOL_AUDIO, "Recorder", "Live mic level now, push-to-record WAV flow next.", "Live", "tile_mic", APP_RECORDER, FEATURE_ACTIVE, 0xb982ff},
    {LV_SYMBOL_USB, "USB Bay", "Host-side CDC serial workbench for add-ons.", "Host", "tile_usb", APP_USB, FEATURE_ACTIVE, 0x70a7ff},
    {LV_SYMBOL_SD_CARD, "Files", "Runtime config, event journal, audio, and backups.", "SD", "tile_sd", APP_FILES, FEATURE_ACTIVE, 0x77dd88},
    {LV_SYMBOL_DOWNLOAD, "Update", "Internet OTA package checks and confirm button.", "OTA", "tile_update", APP_UPDATE, FEATURE_ACTIVE, 0xffc857},
};

static const screen_timeout_option_t g_screen_timeouts[] = {
    {"Never", 0},
    {"30 sec", 30},
    {"1 min", 60},
    {"2 min", 120},
    {"5 min", 300},
};

static ui_refs_t g_ui;
static lv_display_t *g_display;
static lv_disp_rotation_t g_rotation = LV_DISPLAY_ROTATION_90;
static lv_disp_rotation_t g_pending_rotation = LV_DISPLAY_ROTATION_90;
static uint8_t g_pending_rotation_count;
static bool g_auto_rotate = true;
static bool g_sd_ready;
static bool g_sd_recovered;
static esp_err_t g_sd_last_error = ESP_OK;
static bool g_meshcore_mode;
static bool g_mesh_module_ready;
static nav_page_t g_nav_page = NAV_PAGE_APPS;
static app_id_t g_active_app = APP_NONE;
static uint32_t g_heartbeat_count;
static bool g_ext_power_ready;
static esp_err_t g_ext_power_error = ESP_OK;
static bool g_usb_power_ready;
static esp_err_t g_usb_power_error = ESP_OK;
static bool g_charge_enable_ready;
static esp_err_t g_charge_enable_error = ESP_OK;
static bool g_ir_probe_ready;
static int g_ir_level = -1;
static uint32_t g_ir_edges;
static bool g_grove_uart_ready;
static esp_err_t g_grove_uart_error = ESP_OK;
static uint32_t g_grove_rx_packets;
static uint32_t g_grove_rx_bytes;
static uint64_t g_grove_last_rx_ms;
static bool g_usb_host_ready;
static usb_state_t g_usb_state = USB_STATE_OFF;
static esp_err_t g_usb_last_error = ESP_OK;
static uint32_t g_usb_open_count;
static uint32_t g_usb_disconnect_count;
static uint32_t g_usb_rx_packets;
static uint32_t g_usb_rx_bytes;
static uint64_t g_usb_last_rx_ms;
static cdc_acm_dev_hdl_t g_usb_cdc_handle;
static bool g_usb_cdc_disconnected;
static i2c_master_dev_handle_t g_battery_monitor;
static bool g_battery_online;
static esp_err_t g_battery_last_error = ESP_ERR_NOT_FOUND;
static int g_battery_mv = -1;
static int g_battery_percent = -1;
static uint32_t g_battery_reads;
static uint8_t g_screen_timeout_index = 3;
static bool g_screen_sleeping;
static esp_err_t g_screen_power_error = ESP_OK;
static uint32_t g_screen_sleep_count;
static uint32_t g_screen_sleep_start_ms;
static bool g_screen_dimmed;
static uint32_t g_screen_dim_start_ms;
static volatile uint32_t g_input_activity_seq;
static uint32_t g_sleep_input_seq;
static bool g_wifi_started;
static bool g_wifi_has_ip;
static bool g_wifi_boot_scan_started;
static wifi_state_t g_wifi_state = WIFI_STATE_OFF;
static esp_err_t g_wifi_last_error = ESP_OK;
static wifi_credentials_t g_wifi_credentials;
static wifi_ap_record_t g_wifi_aps[TABFORGE_WIFI_MAX_APS];
static uint16_t g_wifi_ap_count;
static uint16_t g_wifi_selected_ap;
static char g_wifi_ip[16] = "--";
static char g_ota_manifest_url[256] = TABFORGE_MANIFEST_URL;
static char g_ota_firmware_url[256] = "";
static char g_ota_version[32] = "";
static char g_ota_sha256[65] = "";
static uint32_t g_ota_size;
static ota_state_t g_ota_state = OTA_STATE_IDLE;
static esp_err_t g_ota_last_error = ESP_OK;
static bool g_ota_reboot_pending;
static bool g_accessory_probe_started;
static uint8_t g_accessory_i2c_count;
static bool g_accessory_pahub_ready;
static char g_accessory_i2c_summary[96] = "not scanned";

#if BSP_CAPS_IMU
static sensor_handle_t g_imu_sensor_handle;
static sensor_event_handler_instance_t g_imu_handler_instance;
#endif
static bool g_imu_online;
static bool g_imu_ready;

static void render_active_app_page_locked(void);
static void init_ir_probe(void);
static void init_grove_uart_probe(void);
static void poll_grove_uart(void);
static axis3_t g_last_acce;
static axis3_t g_last_gyro;
static uint64_t g_last_imu_ms;

static mic_state_t g_mic_state = MIC_STATE_PENDING;
static int g_mic_average = -1;
static int g_mic_peak = -1;
static uint32_t g_mic_reads;

static lv_color_t color_hex(uint32_t value)
{
    return lv_color_hex(value);
}

static uint32_t clamp_percent_u32(uint32_t value)
{
    if (value > 100U) {
        return 100U;
    }
    return value;
}

static float axis_abs(float value)
{
    return value < 0.0f ? -value : value;
}

static bool is_landscape_rotation(lv_disp_rotation_t rotation)
{
    return rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270;
}

static int32_t deck_width(void)
{
    return is_landscape_rotation(g_rotation) ? BSP_LCD_V_RES : BSP_LCD_H_RES;
}

static int32_t deck_height(void)
{
    return is_landscape_rotation(g_rotation) ? BSP_LCD_H_RES : BSP_LCD_V_RES;
}

static const char *active_mode_name(void)
{
    if (!g_mesh_module_ready) {
        return "NO MESH";
    }
    return g_meshcore_mode ? "MESHCORE" : "MESHTASTIC";
}

static const char *active_mode_detail(void)
{
    if (!g_mesh_module_ready) {
        return "No mesh radio detected";
    }
    return g_meshcore_mode ? "MeshCore console profile armed" : "Meshtastic C6L profile armed";
}

static const char *selected_mesh_profile_name(void)
{
    return g_meshcore_mode ? "MeshCore" : "Meshtastic";
}

static const char *mic_state_text(void)
{
    switch (g_mic_state) {
    case MIC_STATE_ONLINE:
        return "online";
    case MIC_STATE_INIT_FAILED:
        return "init failed";
    case MIC_STATE_OPEN_FAILED:
        return "open failed";
    case MIC_STATE_READ_FAILED:
        return "read failed";
    case MIC_STATE_PENDING:
    default:
        return "pending";
    }
}

static const char *usb_state_text(void)
{
    switch (g_usb_state) {
    case USB_STATE_POWERED:
        return "powered";
    case USB_STATE_SCANNING:
        return "scanning";
    case USB_STATE_OPEN:
        return "cdc open";
    case USB_STATE_DISCONNECTED:
        return "disconnected";
    case USB_STATE_ERROR:
        return "error";
    case USB_STATE_OFF:
    default:
        return "off";
    }
}

static const char *wifi_state_text(void)
{
    switch (g_wifi_state) {
    case WIFI_STATE_READY:
        return "ready";
    case WIFI_STATE_SCANNING:
        return "scanning";
    case WIFI_STATE_CONNECTING:
        return "connecting";
    case WIFI_STATE_CONNECTED:
        return "connected";
    case WIFI_STATE_ERROR:
        return "error";
    case WIFI_STATE_OFF:
    default:
        return "off";
    }
}

static const char *ota_state_text(void)
{
    switch (g_ota_state) {
    case OTA_STATE_FETCHING_MANIFEST:
        return "manifest";
    case OTA_STATE_DOWNLOADING:
        return "downloading";
    case OTA_STATE_READY_REBOOT:
        return "ready";
    case OTA_STATE_ERROR:
        return "error";
    case OTA_STATE_IDLE:
    default:
        return "idle";
    }
}

static const char *rotation_name(lv_disp_rotation_t rotation)
{
    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        return "portrait";
    case LV_DISPLAY_ROTATION_90:
        return "landscape";
    case LV_DISPLAY_ROTATION_180:
        return "portrait flip";
    case LV_DISPLAY_ROTATION_270:
        return "landscape flip";
    default:
        return "unknown";
    }
}

static int rotation_degrees(lv_disp_rotation_t rotation)
{
    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        return 0;
    case LV_DISPLAY_ROTATION_90:
        return 90;
    case LV_DISPLAY_ROTATION_180:
        return 180;
    case LV_DISPLAY_ROTATION_270:
        return 270;
    default:
        return -1;
    }
}

static void format_uptime(char *buffer, size_t buffer_size)
{
    uint64_t seconds = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    uint64_t hours = seconds / 3600ULL;
    uint64_t minutes = (seconds / 60ULL) % 60ULL;
    uint64_t secs = seconds % 60ULL;
    snprintf(buffer, buffer_size, "%02llu:%02llu:%02llu",
             (unsigned long long)hours,
             (unsigned long long)minutes,
             (unsigned long long)secs);
}

static size_t screen_timeout_count(void)
{
    return sizeof(g_screen_timeouts) / sizeof(g_screen_timeouts[0]);
}

static const screen_timeout_option_t *active_screen_timeout(void)
{
    if (g_screen_timeout_index >= screen_timeout_count()) {
        g_screen_timeout_index = 3;
    }
    return &g_screen_timeouts[g_screen_timeout_index];
}

static const char *screen_timeout_label(void)
{
    return active_screen_timeout()->label;
}

static uint32_t screen_timeout_seconds(void)
{
    return active_screen_timeout()->seconds;
}

static uint32_t now_ms_u32(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void format_screen_status(char *buffer, size_t buffer_size)
{
    if (g_screen_power_error != ESP_OK) {
        snprintf(buffer, buffer_size, "err %s", esp_err_to_name(g_screen_power_error));
        return;
    }

    snprintf(buffer, buffer_size, "%s | %s",
             g_screen_sleeping ? "sleep" : (g_screen_dimmed ? "dim" : "awake"),
             screen_timeout_label());
}

static void refresh_screen_widgets_locked(void)
{
    char screen_status[40];
    format_screen_status(screen_status, sizeof(screen_status));

    if (g_ui.screen_label != NULL) {
        lv_label_set_text(g_ui.screen_label, screen_status);
        lv_obj_set_style_text_color(g_ui.screen_label,
                                    (g_screen_sleeping || g_screen_dimmed) ? color_hex(0xffc857) : color_hex(0x6ee7a2),
                                    0);
    }

    if (g_ui.settings_screen_label != NULL) {
        lv_label_set_text(g_ui.settings_screen_label, screen_status);
        lv_obj_set_style_text_color(g_ui.settings_screen_label,
                                    (g_screen_sleeping || g_screen_dimmed) ? color_hex(0xffc857) : color_hex(0xf1f7f3),
                                    0);
    }
}

static int battery_percent_from_mv(int mv)
{
    if (mv < TABFORGE_BATTERY_PRESENT_MV || mv <= TABFORGE_BATTERY_MIN_MV) {
        return 0;
    }
    if (mv >= TABFORGE_BATTERY_MAX_MV) {
        return 100;
    }

    return ((mv - TABFORGE_BATTERY_MIN_MV) * 100 + ((TABFORGE_BATTERY_MAX_MV - TABFORGE_BATTERY_MIN_MV) / 2)) /
           (TABFORGE_BATTERY_MAX_MV - TABFORGE_BATTERY_MIN_MV);
}

static void format_battery_status(char *buffer, size_t buffer_size, bool compact)
{
    if (!g_battery_online) {
        if (compact) {
            snprintf(buffer, buffer_size, "BAT --");
        } else {
            snprintf(buffer, buffer_size, "%s", esp_err_to_name(g_battery_last_error));
        }
        return;
    }

    if (g_battery_mv < TABFORGE_BATTERY_PRESENT_MV) {
        if (compact) {
            snprintf(buffer, buffer_size, "USB");
        } else {
            snprintf(buffer, buffer_size, "USB power");
        }
        return;
    }

    if (compact) {
        snprintf(buffer, buffer_size, "BAT %d%%", g_battery_percent);
    } else {
        snprintf(buffer, buffer_size, "%d%% | %d.%02dV",
                 g_battery_percent,
                 g_battery_mv / 1000,
                 (g_battery_mv % 1000) / 10);
    }
}

static esp_err_t set_expander_output(esp_io_expander_handle_t expander, uint32_t pin, bool level)
{
    if (expander == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = ESP_OK;
    err |= esp_io_expander_set_dir(expander, pin, IO_EXPANDER_OUTPUT);
    err |= esp_io_expander_set_level(expander, pin, level);
    err |= esp_io_expander_set_output_mode(expander, pin, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    return err;
}

static void load_screen_settings(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TABFORGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen settings load failed: %s", esp_err_to_name(err));
        return;
    }

    uint8_t saved_index = g_screen_timeout_index;
    err = nvs_get_u8(handle, TABFORGE_NVS_SCREEN_TIMEOUT_KEY, &saved_index);
    if (err == ESP_OK && saved_index < screen_timeout_count()) {
        g_screen_timeout_index = saved_index;
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TABFORGE_TAG, "screen timeout setting invalid: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
}

static void save_screen_timeout_setting(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TABFORGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen timeout save failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u8(handle, TABFORGE_NVS_SCREEN_TIMEOUT_KEY, g_screen_timeout_index);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen timeout save failed: %s", esp_err_to_name(err));
    }
}

static void enter_screen_dim(const char *event_name)
{
    if (g_screen_sleeping || g_screen_dimmed) {
        return;
    }

    esp_err_t err = bsp_display_brightness_set(12);
    g_screen_power_error = err;
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen dim failed: %s", esp_err_to_name(err));
        return;
    }

    g_screen_dimmed = true;
    g_screen_dim_start_ms = now_ms_u32();
    append_event(event_name);
    ESP_LOGI(TABFORGE_TAG, "screen dim entered before sleep");
}

static void exit_screen_dim(const char *event_name)
{
    if (!g_screen_dimmed) {
        return;
    }

    esp_err_t err = bsp_display_brightness_set(100);
    g_screen_power_error = err;
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen undim failed: %s", esp_err_to_name(err));
        return;
    }

    g_screen_dimmed = false;
    lv_display_trigger_activity(g_display);
    append_event(event_name);
}

static void enter_screen_sleep(const char *event_name)
{
    if (g_screen_sleeping) {
        return;
    }

    esp_err_t err = bsp_display_backlight_off();
    g_screen_power_error = err;
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen sleep failed: %s", esp_err_to_name(err));
        return;
    }

    g_screen_sleeping = true;
    g_screen_dimmed = false;
    g_screen_sleep_count++;
    g_screen_sleep_start_ms = now_ms_u32();
    g_sleep_input_seq = g_input_activity_seq;
    append_event(event_name);
    ESP_LOGI(TABFORGE_TAG, "screen sleep entered: timeout=%s reason=%s",
             screen_timeout_label(),
             event_name);
}

static void exit_screen_sleep(const char *event_name)
{
    if (!g_screen_sleeping) {
        return;
    }

    esp_err_t err = bsp_display_backlight_on();
    g_screen_power_error = err;
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "screen wake failed: %s", esp_err_to_name(err));
        return;
    }
    bsp_display_brightness_set(100);

    g_screen_sleeping = false;
    g_screen_dimmed = false;
    lv_display_trigger_activity(g_display);
    append_event(event_name);
    ESP_LOGI(TABFORGE_TAG, "screen woke: %s", event_name);
}

static void ensure_dir(const char *path)
{
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TABFORGE_TAG, "mkdir failed for %s", path);
    }
}

static void write_default_config_if_missing(void)
{
    FILE *existing = fopen(TABFORGE_CONFIG_PATH, "r");
    if (existing != NULL) {
        fclose(existing);
        return;
    }

    FILE *f = fopen(TABFORGE_CONFIG_PATH, "w");
    if (f == NULL) {
        ESP_LOGW(TABFORGE_TAG, "could not create %s", TABFORGE_CONFIG_PATH);
        return;
    }

    fprintf(f,
            "{\n"
            "  \"schema\": \"tabforge.runtime-config.v0\",\n"
            "  \"deckName\": \"TabForge Cyberdeck\",\n"
            "  \"operatorLabel\": \"Tab5\",\n"
            "  \"defaultMode\": \"meshtastic\",\n"
            "  \"updateChannel\": \"stable\",\n"
            "  \"ui\": { \"home\": \"tablet\", \"autoRotate\": true, \"liveStats\": true, \"screenTimeoutSeconds\": 120, \"sleepOnTimeout\": true },\n"
            "  \"wifi\": { \"ssid\": \"\", \"password\": \"\", \"autoConnect\": false },\n"
            "  \"devices\": {\n"
            "    \"unit-c6l\": { \"enabled\": true, \"preferredTransport\": \"grove-uart\", \"mode\": \"meshtastic\" },\n"
            "    \"tdeck\": { \"enabled\": true, \"preferredTransport\": \"usb-cdc\", \"mode\": \"zdeck-meshtastic\" },\n"
            "    \"unit-ir\": { \"enabled\": true, \"preferredTransport\": \"grove-port-b\" }\n"
            "  },\n"
            "  \"ota\": { \"manifestUrl\": \"%s\", \"requireButtonConfirm\": true }\n"
            "}\n",
            TABFORGE_MANIFEST_URL);
    fclose(f);
    ESP_LOGI(TABFORGE_TAG, "created default config at %s", TABFORGE_CONFIG_PATH);
}

static void copy_json_string(cJSON *object, const char *name, char *buffer, size_t buffer_size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsString(item) && item->valuestring != NULL && buffer_size > 0) {
        strlcpy(buffer, item->valuestring, buffer_size);
    }
}

static void load_wifi_credentials_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TABFORGE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return;
    }

    size_t ssid_len = sizeof(g_wifi_credentials.ssid);
    size_t pass_len = sizeof(g_wifi_credentials.password);
    bool have_ssid = nvs_get_str(handle, TABFORGE_NVS_WIFI_SSID_KEY, g_wifi_credentials.ssid, &ssid_len) == ESP_OK;
    bool have_pass = nvs_get_str(handle, TABFORGE_NVS_WIFI_PASS_KEY, g_wifi_credentials.password, &pass_len) == ESP_OK;
    g_wifi_credentials.configured = have_ssid && g_wifi_credentials.ssid[0] != '\0';
    if (have_pass) {
        g_wifi_credentials.password[sizeof(g_wifi_credentials.password) - 1] = '\0';
    }

    nvs_close(handle);
}

static void save_wifi_credentials_to_nvs(void)
{
    if (!g_wifi_credentials.configured || g_wifi_credentials.ssid[0] == '\0') {
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(TABFORGE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "wifi credentials save failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_str(handle, TABFORGE_NVS_WIFI_SSID_KEY, g_wifi_credentials.ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, TABFORGE_NVS_WIFI_PASS_KEY, g_wifi_credentials.password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "wifi credentials save failed: %s", esp_err_to_name(err));
    }
}

static void load_runtime_config(void)
{
    load_wifi_credentials_from_nvs();

    FILE *f = fopen(TABFORGE_CONFIG_PATH, "rb");
    if (f == NULL) {
        return;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return;
    }
    long file_size = ftell(f);
    if (file_size <= 0 || file_size > 8192 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return;
    }

    char *json = heap_caps_malloc((size_t)file_size + 1U, MALLOC_CAP_INTERNAL);
    if (json == NULL) {
        fclose(f);
        return;
    }

    size_t read_len = fread(json, 1, (size_t)file_size, f);
    fclose(f);
    json[read_len] = '\0';

    cJSON *root = cJSON_Parse(json);
    heap_caps_free(json);
    if (root == NULL) {
        ESP_LOGW(TABFORGE_TAG, "config JSON parse failed");
        return;
    }

    cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi");
    if (cJSON_IsObject(wifi)) {
        copy_json_string(wifi, "ssid", g_wifi_credentials.ssid, sizeof(g_wifi_credentials.ssid));
        copy_json_string(wifi, "password", g_wifi_credentials.password, sizeof(g_wifi_credentials.password));
        cJSON *auto_connect = cJSON_GetObjectItemCaseSensitive(wifi, "autoConnect");
        if (cJSON_IsBool(auto_connect)) {
            g_wifi_credentials.auto_connect = cJSON_IsTrue(auto_connect);
        }
        g_wifi_credentials.configured = g_wifi_credentials.ssid[0] != '\0';
        if (g_wifi_credentials.configured) {
            save_wifi_credentials_to_nvs();
        }
    }

    cJSON *ota = cJSON_GetObjectItemCaseSensitive(root, "ota");
    if (cJSON_IsObject(ota)) {
        copy_json_string(ota, "manifestUrl", g_ota_manifest_url, sizeof(g_ota_manifest_url));
    }

    cJSON_Delete(root);
}

static void append_event(const char *event)
{
    if (!g_sd_ready) {
        return;
    }

    FILE *f = fopen(TABFORGE_EVENT_LOG_PATH, "a");
    if (f == NULL) {
        return;
    }

    fprintf(f, "{\"event\":\"%s\",\"version\":\"%s\",\"mode\":\"%s\"}\n",
            event,
            TABFORGE_VERSION,
            active_mode_name());
    fclose(f);
}

static esp_err_t mount_sdcard_with_config(bool format_if_mount_failed)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = format_if_mount_failed,
        .max_files = TABFORGE_SD_MAX_FILES,
        .allocation_unit_size = TABFORGE_SD_ALLOC_UNIT_SIZE,
    };
    bsp_sdcard_cfg_t cfg = {
        .mount = &mount_config,
    };

    return bsp_sdcard_sdmmc_mount(&cfg);
}

static void write_sd_recovery_marker(void)
{
    FILE *f = fopen(TABFORGE_SD_ROOT "/tabforge/SD_RECOVERY.txt", "w");
    if (f == NULL) {
        ESP_LOGW(TABFORGE_TAG, "could not create SD recovery marker");
        return;
    }

    fprintf(f,
            "TabForge recovered this SD card filesystem on boot.\n"
            "Firmware: %s\n"
            "Runtime root: /tabforge\n",
            TABFORGE_VERSION);
    fclose(f);
}

static void init_sdcard(void)
{
    g_sd_recovered = false;
    esp_err_t err = mount_sdcard_with_config(false);
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "SD mount failed: %s", esp_err_to_name(err));
        ESP_LOGW(TABFORGE_TAG, "retrying SD mount with filesystem recovery enabled");
        (void)bsp_sdcard_unmount();
        err = mount_sdcard_with_config(true);
        g_sd_recovered = (err == ESP_OK);
        if (g_sd_recovered) {
            ESP_LOGW(TABFORGE_TAG, "SD filesystem recovered; previous card contents may have been erased");
        }
    }

    g_sd_last_error = err;
    g_sd_ready = (err == ESP_OK);

    if (!g_sd_ready) {
        ESP_LOGW(TABFORGE_TAG, "SD recovery failed: %s", esp_err_to_name(err));
        return;
    }

    ensure_dir(TABFORGE_SD_ROOT "/tabforge");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/audio");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/backups");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/ir");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/logs");
    write_default_config_if_missing();
    if (g_sd_recovered) {
        write_sd_recovery_marker();
    }
    append_event(g_sd_recovered ? "sd_recovered_boot" : "boot");

    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (card != NULL) {
        ESP_LOGI(TABFORGE_TAG, "SD mounted%s: %s",
                 g_sd_recovered ? " after recovery" : "",
                 card->cid.name);
    }
}

static esp_err_t battery_read_u16(uint8_t reg, uint16_t *value)
{
    if (g_battery_monitor == NULL || value == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t rx[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(g_battery_monitor, &reg, 1, rx, sizeof(rx), 100);
    if (err == ESP_OK) {
        *value = ((uint16_t)rx[0] << 8) | rx[1];
    }
    return err;
}

static esp_err_t read_battery_sample(void)
{
    uint16_t raw_bus_mv = 0;
    esp_err_t err = battery_read_u16(TABFORGE_BATTERY_REG_BUS_VOLTAGE, &raw_bus_mv);
    g_battery_last_error = err;
    if (err != ESP_OK) {
        g_battery_online = false;
        return err;
    }

    g_battery_mv = (int)(((uint32_t)raw_bus_mv * 125U + 50U) / 100U);
    g_battery_percent = battery_percent_from_mv(g_battery_mv);
    g_battery_reads++;
    g_battery_online = true;
    return ESP_OK;
}

static void init_battery_monitor(void)
{
    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) {
        g_battery_last_error = err;
        ESP_LOGW(TABFORGE_TAG, "battery monitor I2C init failed: %s", esp_err_to_name(err));
        return;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        g_battery_last_error = ESP_FAIL;
        ESP_LOGW(TABFORGE_TAG, "battery monitor failed: I2C bus unavailable");
        return;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TABFORGE_BATTERY_I2C_ADDR,
        .scl_speed_hz = TABFORGE_I2C_SPEED_HZ,
    };

    err = i2c_master_bus_add_device(bus, &dev_cfg, &g_battery_monitor);
    if (err != ESP_OK) {
        g_battery_last_error = err;
        ESP_LOGW(TABFORGE_TAG, "battery monitor add failed: %s", esp_err_to_name(err));
        return;
    }

    err = read_battery_sample();
    if (err == ESP_OK) {
        append_event("battery_monitor_started");
        ESP_LOGI(TABFORGE_TAG, "battery monitor ready: %dmV %d%%",
                 g_battery_mv,
                 g_battery_percent);
    } else {
        ESP_LOGW(TABFORGE_TAG, "battery monitor read failed: %s", esp_err_to_name(err));
    }
}

static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, uint32_t bg, uint32_t border)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, color_hex(border), 0);
    lv_obj_set_style_bg_color(panel, color_hex(bg), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static lv_obj_t *make_text(lv_obj_t *parent, const char *text, uint32_t color, lv_coord_t width)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    if (width > 0) {
        lv_obj_set_width(label, width);
    }
    lv_obj_set_style_text_color(label, color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, lv_coord_t width, const char *label_text, lv_event_cb_t cb)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_style_radius(button, 24, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x20313a), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color_hex(0x38505a), 0);
    lv_obj_set_style_shadow_width(button, 10, 0);
    lv_obj_set_style_shadow_color(button, color_hex(0x05080a), 0);
    lv_obj_set_style_shadow_opa(button, LV_OPA_30, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, label_text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width - 10);
    lv_obj_set_style_text_color(label, color_hex(0xf1f7f3), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, cb, LV_EVENT_CLICKED, NULL);
    return label;
}

static nav_button_refs_t make_nav_button(lv_obj_t *parent,
                                         lv_coord_t width,
                                         const char *icon_text,
                                         const char *label_text,
                                         lv_event_cb_t cb)
{
    nav_button_refs_t refs = {0};
    lv_obj_t *button = lv_button_create(parent);
    refs.button = button;
    lv_obj_set_size(button, width, 64);
    lv_obj_set_style_radius(button, 22, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x10161a), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 6, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(button, 3, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *icon = lv_label_create(button);
    refs.icon = icon;
    lv_label_set_text(icon, icon_text);
    lv_obj_set_style_text_color(icon, color_hex(0x6ee7a2), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(icon, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    refs.label = label;
    lv_label_set_text(label, label_text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width - 8);
    lv_obj_set_style_text_color(label, color_hex(0xf1f7f3), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, cb, LV_EVENT_CLICKED, NULL);
    return refs;
}

static lv_obj_t *add_stat_card(lv_obj_t *parent,
                               const char *title,
                               lv_obj_t **value_label,
                               lv_obj_t **bar,
                               lv_coord_t width,
                               lv_coord_t height,
                               uint32_t accent)
{
    lv_obj_t *card = make_panel(parent, width, height, 0x151d22, accent);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 5, 0);

    make_text(card, title, 0x8fb0bb, width - 24);
    *value_label = make_text(card, "--", 0xf1f7f3, width - 24);

    if (bar != NULL) {
        *bar = lv_bar_create(card);
        lv_bar_set_range(*bar, 0, 100);
        lv_obj_set_size(*bar, width - 28, 8);
        lv_obj_set_style_bg_color(*bar, color_hex(0x283238), 0);
        lv_obj_set_style_bg_color(*bar, color_hex(accent), LV_PART_INDICATOR);
        lv_bar_set_value(*bar, 0, LV_ANIM_OFF);
    }

    return card;
}

static void set_activity(const char *title, const char *detail)
{
    if (g_ui.activity_title_label != NULL) {
        lv_label_set_text(g_ui.activity_title_label, title);
    }
    if (g_ui.activity_detail_label != NULL) {
        lv_label_set_text(g_ui.activity_detail_label, detail);
    }
}

static void set_obj_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == NULL) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void style_nav_button(const nav_button_refs_t *nav, bool active, uint32_t accent)
{
    if (nav == NULL || nav->button == NULL) {
        return;
    }

    uint32_t bg = active ? 0x20313a : 0x10161a;
    uint32_t text = active ? 0xf1f7f3 : 0x93a6ad;
    uint32_t icon = active ? accent : 0x6ee7a2;

    lv_obj_set_style_bg_color(nav->button, color_hex(bg), 0);
    lv_obj_set_style_bg_opa(nav->button, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(nav->button, active ? 1 : 0, 0);
    lv_obj_set_style_border_color(nav->button, color_hex(accent), 0);
    if (nav->icon != NULL) {
        lv_obj_set_style_text_color(nav->icon, color_hex(icon), 0);
    }
    if (nav->label != NULL) {
        lv_obj_set_style_text_color(nav->label, color_hex(text), 0);
    }
}

static void refresh_nav_styles(void)
{
    style_nav_button(&g_ui.nav_apps, g_nav_page == NAV_PAGE_APPS || g_nav_page == NAV_PAGE_APP, 0x6ee7a2);
    style_nav_button(&g_ui.nav_settings, g_nav_page == NAV_PAGE_SETTINGS, 0x61d5f0);
    style_nav_button(&g_ui.nav_mode, g_mesh_module_ready, 0xf0bf4f);
    style_nav_button(&g_ui.nav_auto, g_auto_rotate, 0x77dd88);
    style_nav_button(&g_ui.nav_update, g_nav_page == NAV_PAGE_UPDATE, 0xffc857);
}

static void show_nav_page(nav_page_t page)
{
    g_nav_page = page;
    set_obj_hidden(g_ui.page_apps, page != NAV_PAGE_APPS);
    set_obj_hidden(g_ui.page_settings, page != NAV_PAGE_SETTINGS);
    set_obj_hidden(g_ui.page_update, page != NAV_PAGE_UPDATE);
    set_obj_hidden(g_ui.page_app, page != NAV_PAGE_APP);

    switch (page) {
    case NAV_PAGE_SETTINGS:
        set_activity("Settings", "Wi-Fi, display, sleep, mesh profile, rotation, SD, battery, and updates.");
        break;
    case NAV_PAGE_UPDATE:
        set_activity("Wi-Fi & Internet", "Scan networks, type a password, connect, then run internet OTA.");
        break;
    case NAV_PAGE_APP:
        render_active_app_page_locked();
        break;
    case NAV_PAGE_APPS:
    default:
        g_active_app = APP_NONE;
        set_activity("Apps", "Launcher grid active: Wi-Fi, mesh profiles, T-Deck, mic, SD, and updates.");
        break;
    }

    refresh_nav_styles();
}

static void refresh_mode_widgets(void)
{
    if (g_ui.status_label != NULL) {
        lv_label_set_text_fmt(g_ui.status_label, "TabForge");
    }

    if (g_ui.mode_label != NULL) {
        lv_label_set_text_fmt(g_ui.mode_label, "%s", active_mode_name());
    }

    if (g_ui.mode_detail_label != NULL) {
        lv_label_set_text(g_ui.mode_detail_label, active_mode_detail());
    }

    if (g_ui.dock_mode_label != NULL) {
        lv_label_set_text(g_ui.dock_mode_label, g_mesh_module_ready ? selected_mesh_profile_name() : "Mesh");
    }

    refresh_nav_styles();
}

static void refresh_rotation_widgets(void)
{
    if (g_ui.rotation_label != NULL) {
        lv_label_set_text_fmt(g_ui.rotation_label,
                              "%s  %d deg",
                              g_auto_rotate ? "Auto" : "Locked",
                              rotation_degrees(g_rotation));
    }

    if (g_ui.dock_auto_label != NULL) {
        lv_label_set_text(g_ui.dock_auto_label, g_auto_rotate ? "Auto" : "Locked");
    }

    refresh_nav_styles();
}

static void format_wifi_status(char *buffer, size_t buffer_size)
{
    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        snprintf(buffer, buffer_size, "%s | %s",
                 g_wifi_credentials.ssid[0] != '\0' ? g_wifi_credentials.ssid : "Wi-Fi",
                 g_wifi_ip);
    } else if (g_wifi_last_error != ESP_OK) {
        snprintf(buffer, buffer_size, "%s | %s", wifi_state_text(), esp_err_to_name(g_wifi_last_error));
    } else if (g_wifi_credentials.configured) {
        snprintf(buffer, buffer_size, "%s | saved %s", wifi_state_text(), g_wifi_credentials.ssid);
    } else {
        snprintf(buffer, buffer_size, "%s | no saved AP", wifi_state_text());
    }
}

static void format_wifi_scan_status(char *buffer, size_t buffer_size)
{
    if (g_wifi_ap_count == 0) {
        snprintf(buffer, buffer_size, "No scan results yet");
        return;
    }

    wifi_ap_record_t *ap = &g_wifi_aps[g_wifi_selected_ap % g_wifi_ap_count];
    snprintf(buffer, buffer_size, "%u/%u %s %ddBm %s",
             (unsigned)(g_wifi_selected_ap + 1U),
             (unsigned)g_wifi_ap_count,
             (const char *)ap->ssid,
             (int)ap->rssi,
             ap->authmode == WIFI_AUTH_OPEN ? "open" : "secured");
}

static void format_accessory_status(char *buffer, size_t buffer_size)
{
    snprintf(buffer, buffer_size, "Grove %s | USB-A %s | I2C %s",
             g_ext_power_ready ? "on" : "off",
             g_usb_power_ready ? "on" : "off",
             g_accessory_i2c_summary);
}

static void refresh_wifi_widgets_locked(void)
{
    char status[96];
    char scan[96];
    format_wifi_status(status, sizeof(status));
    format_wifi_scan_status(scan, sizeof(scan));

    if (g_ui.wifi_label != NULL) {
        lv_label_set_text(g_ui.wifi_label, status);
        lv_obj_set_style_text_color(g_ui.wifi_label,
                                    g_wifi_state == WIFI_STATE_CONNECTED ? color_hex(0x6ee7a2) : color_hex(0xffc857),
                                    0);
    }
    if (g_ui.wifi_scan_label != NULL) {
        lv_label_set_text(g_ui.wifi_scan_label, scan);
    }
    if (g_ui.ota_label != NULL) {
        if (g_ota_state == OTA_STATE_ERROR) {
            lv_label_set_text_fmt(g_ui.ota_label, "%s | %s", ota_state_text(), esp_err_to_name(g_ota_last_error));
        } else if (g_ota_firmware_url[0] != '\0') {
            lv_label_set_text_fmt(g_ui.ota_label, "%s | %s", ota_state_text(), g_ota_version[0] != '\0' ? g_ota_version : "firmware found");
        } else {
            lv_label_set_text_fmt(g_ui.ota_label, "%s | %s", ota_state_text(), g_ota_manifest_url);
        }
    }
}

static void refresh_accessory_widgets_locked(void)
{
    if (g_ui.accessory_label != NULL) {
        char status[64];
        format_accessory_status(status, sizeof(status));
        lv_label_set_text(g_ui.accessory_label, status);
        lv_obj_set_style_text_color(g_ui.accessory_label,
                                    (g_ext_power_ready || g_usb_power_ready) ? color_hex(0x6ee7a2) : color_hex(0xffc857),
                                    0);
    }
}

static void set_accessory_power(bool enable)
{
    esp_io_expander_handle_t grove_expander = bsp_io_expander_init();
    esp_io_expander_handle_t power_expander = bsp_io_expander1_init();

    if (grove_expander != NULL) {
        g_ext_power_error = set_expander_output(grove_expander, TABFORGE_EXT5V_EN, enable);
        g_ext_power_ready = enable && (g_ext_power_error == ESP_OK);
    } else {
        g_ext_power_error = ESP_ERR_NOT_SUPPORTED;
        g_ext_power_ready = false;
    }

    if (power_expander != NULL) {
        g_usb_power_error = set_expander_output(power_expander, BSP_USB_EN, enable);
        g_usb_power_ready = enable && (g_usb_power_error == ESP_OK);
    } else {
        g_usb_power_error = ESP_ERR_NOT_SUPPORTED;
        g_usb_power_ready = false;
    }

    if (!enable) {
        g_usb_state = USB_STATE_OFF;
        g_mesh_module_ready = false;
    } else if (g_usb_power_ready) {
        g_usb_state = USB_STATE_POWERED;
    }

    ESP_LOGI(TABFORGE_TAG, "accessory rails %s: grove=%s usb=%s",
             enable ? "enabled" : "disabled",
             g_ext_power_ready ? "on" : esp_err_to_name(g_ext_power_error),
             g_usb_power_ready ? "on" : esp_err_to_name(g_usb_power_error));
}

static void start_accessory_probe_tasks(void)
{
    if (g_accessory_probe_started) {
        return;
    }
    g_accessory_probe_started = true;
    init_ir_probe();
    init_grove_uart_probe();
    ESP_LOGI(TABFORGE_TAG, "IR probe and C6L Grove UART link armed");
}

static void refresh_live_stats_locked(void)
{
    char uptime[32];
    char battery_status[32];
    format_uptime(uptime, sizeof(uptime));
    format_battery_status(battery_status, sizeof(battery_status), true);

    if (g_ui.battery_label != NULL) {
        lv_label_set_text(g_ui.battery_label, battery_status);
        lv_obj_set_style_text_color(g_ui.battery_label,
                                    g_battery_online ? color_hex(0x6ee7a2) : color_hex(0xffc857),
                                    0);
    }
    if (g_ui.battery_card_label != NULL) {
        char battery_detail[32];
        format_battery_status(battery_detail, sizeof(battery_detail), false);
        lv_label_set_text(g_ui.battery_card_label, battery_detail);
        lv_obj_set_style_text_color(g_ui.battery_card_label,
                                    g_battery_online ? color_hex(0x6ee7a2) : color_hex(0xffc857),
                                    0);
    }

    if (g_ui.uptime_label != NULL) {
        lv_label_set_text(g_ui.uptime_label, uptime);
    }

    size_t heap_free = esp_get_free_heap_size();
    size_t heap_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    uint32_t heap_percent = heap_total > 0 ? (uint32_t)((heap_free * 100U) / heap_total) : 0U;
    heap_percent = clamp_percent_u32(heap_percent);
    if (g_ui.heap_label != NULL) {
        lv_label_set_text_fmt(g_ui.heap_label, "%u KB free", (unsigned)(heap_free / 1024U));
    }
    if (g_ui.heap_bar != NULL) {
        lv_bar_set_value(g_ui.heap_bar, (int32_t)heap_percent, LV_ANIM_OFF);
    }

    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    uint32_t psram_percent = psram_total > 0 ? (uint32_t)((psram_free * 100U) / psram_total) : 0U;
    psram_percent = clamp_percent_u32(psram_percent);
    if (g_ui.psram_label != NULL) {
        lv_label_set_text_fmt(g_ui.psram_label, "%u KB free", (unsigned)(psram_free / 1024U));
    }
    if (g_ui.psram_bar != NULL) {
        lv_bar_set_value(g_ui.psram_bar, (int32_t)psram_percent, LV_ANIM_OFF);
    }

    if (g_ui.sd_label != NULL) {
        lv_label_set_text(g_ui.sd_label, g_sd_ready ? (g_sd_recovered ? "recovered" : "mounted") : "mount fail");
        lv_obj_set_style_text_color(g_ui.sd_label, g_sd_ready ? color_hex(0x6ee7a2) : color_hex(0xff7a66), 0);
    }

    if (g_ui.mic_label != NULL) {
        if (g_mic_state == MIC_STATE_ONLINE) {
            lv_label_set_text_fmt(g_ui.mic_label, "avg %d | peak %d", g_mic_average, g_mic_peak);
        } else {
            lv_label_set_text_fmt(g_ui.mic_label, "%s", mic_state_text());
        }
    }
    if (g_ui.mic_bar != NULL) {
        uint32_t mic_percent = 0U;
        if (g_mic_peak > 0) {
            mic_percent = clamp_percent_u32((uint32_t)((g_mic_peak * 100) / 12000));
        }
        lv_bar_set_value(g_ui.mic_bar, (int32_t)mic_percent, LV_ANIM_OFF);
    }

    if (g_ui.tilt_label != NULL) {
        if (g_imu_ready) {
            lv_label_set_text_fmt(g_ui.tilt_label,
                                  "x %.2f y %.2f z %.2f",
                                  (double)g_last_acce.x,
                                  (double)g_last_acce.y,
                                  (double)g_last_acce.z);
        } else {
            lv_label_set_text(g_ui.tilt_label, g_imu_online ? "waiting" : "offline");
        }
    }

    if (g_ui.usb_label != NULL) {
        if (!g_usb_power_ready) {
            lv_label_set_text_fmt(g_ui.usb_label, "USB-A 5V %s", esp_err_to_name(g_usb_power_error));
        } else if (g_usb_state == USB_STATE_OPEN) {
            uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
            uint64_t age_ms = g_usb_last_rx_ms > 0 ? now_ms - g_usb_last_rx_ms : 0;
            lv_label_set_text_fmt(g_ui.usb_label,
                                  "open %lu | %luB | %llums",
                                  (unsigned long)g_usb_open_count,
                                  (unsigned long)g_usb_rx_bytes,
                                  (unsigned long long)age_ms);
        } else if (g_usb_last_error != ESP_OK) {
            lv_label_set_text_fmt(g_ui.usb_label, "%s | %s", usb_state_text(), esp_err_to_name(g_usb_last_error));
        } else {
            lv_label_set_text_fmt(g_ui.usb_label, "%s", usb_state_text());
        }
    }

    if (g_ui.ir_label != NULL) {
        if (g_grove_uart_ready) {
            uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
            uint64_t age_s = g_grove_last_rx_ms > 0 ? (now_ms - g_grove_last_rx_ms) / 1000ULL : 0;
            if (g_grove_rx_packets > 0) {
                lv_label_set_text_fmt(g_ui.ir_label,
                                      "C6L Grove linked | %luB/%lu | %llus",
                                      (unsigned long)g_grove_rx_bytes,
                                      (unsigned long)g_grove_rx_packets,
                                      (unsigned long long)age_s);
            } else {
                lv_label_set_text(g_ui.ir_label, "C6L Grove ready | waiting for traffic");
            }
        } else if (g_ext_power_error != ESP_OK) {
            lv_label_set_text_fmt(g_ui.ir_label, "power %s", esp_err_to_name(g_ext_power_error));
        } else if (g_grove_uart_error != ESP_OK) {
            lv_label_set_text_fmt(g_ui.ir_label, "uart %s", esp_err_to_name(g_grove_uart_error));
        } else {
            lv_label_set_text(g_ui.ir_label, "C6L Grove pending");
        }
    }

    if (g_ui.gyro_label != NULL) {
        if (g_imu_ready) {
            uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
            uint64_t age_ms = g_last_imu_ms > 0 ? now_ms - g_last_imu_ms : 0;
            lv_label_set_text_fmt(g_ui.gyro_label,
                                  "gyro %.1f %.1f %.1f | %llums",
                                  (double)g_last_gyro.x,
                                  (double)g_last_gyro.y,
                                  (double)g_last_gyro.z,
                                  (unsigned long long)age_ms);
        } else {
            lv_label_set_text(g_ui.gyro_label, "gyro pending");
        }
    }

    if (g_ui.heart_label != NULL) {
        lv_label_set_text_fmt(g_ui.heart_label, "beat %lu | %s | C6L %lu",
                              (unsigned long)g_heartbeat_count,
                              battery_status,
                              (unsigned long)g_grove_rx_packets);
    }

    refresh_screen_widgets_locked();
    refresh_wifi_widgets_locked();
    refresh_accessory_widgets_locked();
    refresh_mode_widgets();
    refresh_rotation_widgets();
}

static void feature_tile_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    const feature_tile_t *tile = (const feature_tile_t *)lv_event_get_user_data(event);
    if (tile == NULL) {
        return;
    }

    g_active_app = tile->app_id;
    show_nav_page(NAV_PAGE_APP);
    append_event(tile->event_name);
    ESP_LOGI(TABFORGE_TAG, "tile selected: %s", tile->name);
}

static void add_app_tile(lv_obj_t *parent, const feature_tile_t *tile, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x090d10), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(button, 6, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, feature_tile_event_cb, LV_EVENT_CLICKED, (void *)tile);

    lv_obj_t *icon_box = lv_obj_create(button);
    lv_coord_t icon_size = height > 108 ? 68 : 58;
    lv_obj_set_size(icon_box, icon_size, icon_size);
    lv_obj_set_style_radius(icon_box, 18, 0);
    lv_obj_set_style_border_width(icon_box, 0, 0);
    lv_obj_set_style_bg_color(icon_box, color_hex(tile->accent), 0);
    lv_obj_set_style_bg_opa(icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(icon_box, 14, 0);
    lv_obj_set_style_shadow_color(icon_box, color_hex(0x030506), 0);
    lv_obj_set_style_shadow_opa(icon_box, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(icon_box, 0, 0);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(icon_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(icon_box, feature_tile_event_cb, LV_EVENT_CLICKED, (void *)tile);

    lv_obj_t *icon = lv_label_create(icon_box);
    lv_label_set_text(icon, tile->code);
    lv_obj_set_style_text_color(icon, color_hex(0x081014), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
    lv_obj_center(icon);
    lv_obj_add_flag(icon, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(icon, feature_tile_event_cb, LV_EVENT_CLICKED, (void *)tile);

    lv_obj_t *name = make_text(button, tile->name, 0xf1f7f3, width - 12);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(name, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(name, feature_tile_event_cb, LV_EVENT_CLICKED, (void *)tile);
}

static void update_activity_from_task(const char *title, const char *detail)
{
    if (bsp_display_lock(1000)) {
        set_activity(title, detail);
        refresh_wifi_widgets_locked();
        bsp_display_unlock();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_has_ip = false;
        g_wifi_ip[0] = '-';
        g_wifi_ip[1] = '-';
        g_wifi_ip[2] = '\0';
        if (g_wifi_state == WIFI_STATE_CONNECTED || g_wifi_state == WIFI_STATE_CONNECTING) {
            g_wifi_state = WIFI_STATE_READY;
        }
        append_event("wifi_disconnected");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_has_ip = true;
        g_wifi_state = WIFI_STATE_CONNECTED;
        g_wifi_last_error = ESP_OK;
        append_event("wifi_connected");
        ESP_LOGI(TABFORGE_TAG, "wifi connected: %s %s", g_wifi_credentials.ssid, g_wifi_ip);
    }
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    if (!g_wifi_started) {
        g_wifi_state = WIFI_STATE_ERROR;
        g_wifi_last_error = ESP_ERR_INVALID_STATE;
        vTaskDelete(NULL);
        return;
    }

    g_wifi_state = WIFI_STATE_SCANNING;
    update_activity_from_task("Wi-Fi Scan", "Scanning nearby access points.");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err == ESP_OK) {
        uint16_t ap_count = TABFORGE_WIFI_MAX_APS;
        err = esp_wifi_scan_get_ap_records(&ap_count, g_wifi_aps);
        if (err == ESP_OK) {
            g_wifi_ap_count = ap_count;
            g_wifi_selected_ap = 0;
            g_wifi_state = g_wifi_has_ip ? WIFI_STATE_CONNECTED : WIFI_STATE_READY;
            append_event("wifi_scan_done");
            ESP_LOGI(TABFORGE_TAG, "wifi scan found %u network%s",
                     (unsigned)ap_count,
                     ap_count == 1 ? "" : "s");
            for (uint16_t i = 0; i < ap_count; ++i) {
                ESP_LOGI(TABFORGE_TAG, "wifi ap %u: ssid=\"%s\" rssi=%d auth=%s",
                         (unsigned)(i + 1U),
                         (const char *)g_wifi_aps[i].ssid,
                         (int)g_wifi_aps[i].rssi,
                         g_wifi_aps[i].authmode == WIFI_AUTH_OPEN ? "open" : "secured");
            }
            if (ap_count > 0 && bsp_display_lock(1000)) {
                if (g_ui.wifi_ssid_textarea != NULL) {
                    lv_textarea_set_text(g_ui.wifi_ssid_textarea, (const char *)g_wifi_aps[0].ssid);
                }
                refresh_wifi_widgets_locked();
                bsp_display_unlock();
            }
            update_activity_from_task("Wi-Fi Scan", ap_count > 0 ? "Select a network, then connect." : "No networks found.");
        }
    }

    if (err != ESP_OK) {
        g_wifi_last_error = err;
        g_wifi_state = WIFI_STATE_ERROR;
        ESP_LOGW(TABFORGE_TAG, "wifi scan failed: %s", esp_err_to_name(err));
        update_activity_from_task("Wi-Fi Scan", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}

static void wifi_connect_task(void *arg)
{
    (void)arg;
    if (!g_wifi_started) {
        g_wifi_state = WIFI_STATE_ERROR;
        g_wifi_last_error = ESP_ERR_INVALID_STATE;
        vTaskDelete(NULL);
        return;
    }

    if (g_wifi_ap_count > 0 && g_wifi_credentials.ssid[0] == '\0') {
        wifi_ap_record_t *ap = &g_wifi_aps[g_wifi_selected_ap % g_wifi_ap_count];
        strlcpy(g_wifi_credentials.ssid, (const char *)ap->ssid, sizeof(g_wifi_credentials.ssid));
        g_wifi_credentials.configured = true;
        if (ap->authmode != WIFI_AUTH_OPEN && g_wifi_credentials.password[0] == '\0') {
            g_wifi_state = WIFI_STATE_ERROR;
            g_wifi_last_error = ESP_ERR_INVALID_ARG;
            update_activity_from_task("Wi-Fi Password Needed", "Add wifi.ssid and wifi.password to /tabforge/config.json, then connect again.");
            append_event("wifi_password_needed");
            vTaskDelete(NULL);
            return;
        }
    }

    if (g_wifi_ap_count > 0 && g_wifi_credentials.ssid[0] != '\0' && g_wifi_credentials.password[0] == '\0') {
        wifi_ap_record_t *ap = &g_wifi_aps[g_wifi_selected_ap % g_wifi_ap_count];
        if (strcmp((const char *)ap->ssid, g_wifi_credentials.ssid) == 0 && ap->authmode != WIFI_AUTH_OPEN) {
            g_wifi_state = WIFI_STATE_ERROR;
            g_wifi_last_error = ESP_ERR_INVALID_ARG;
            update_activity_from_task("Wi-Fi Password Needed", "Type the password, then press Connect Wi-Fi.");
            append_event("wifi_password_needed");
            vTaskDelete(NULL);
            return;
        }
    }

    if (!g_wifi_credentials.configured || g_wifi_credentials.ssid[0] == '\0') {
        g_wifi_state = WIFI_STATE_ERROR;
        g_wifi_last_error = ESP_ERR_NOT_FOUND;
        update_activity_from_task("Wi-Fi", "No saved SSID. Scan first or add wifi.ssid to config.");
        vTaskDelete(NULL);
        return;
    }

    g_wifi_state = WIFI_STATE_CONNECTING;
    update_activity_from_task("Wi-Fi Connect", g_wifi_credentials.ssid);

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, g_wifi_credentials.ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, g_wifi_credentials.password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = g_wifi_credentials.password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        g_wifi_last_error = err;
        g_wifi_state = WIFI_STATE_ERROR;
        update_activity_from_task("Wi-Fi Connect", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    uint32_t start_ms = now_ms_u32();
    while (!g_wifi_has_ip && (uint32_t)(now_ms_u32() - start_ms) < TABFORGE_WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    if (!g_wifi_has_ip) {
        g_wifi_last_error = ESP_ERR_TIMEOUT;
        g_wifi_state = WIFI_STATE_ERROR;
        update_activity_from_task("Wi-Fi Connect", "Timed out waiting for IP.");
    } else {
        save_wifi_credentials_to_nvs();
        update_activity_from_task("Wi-Fi Connected", g_wifi_ip);
    }

    vTaskDelete(NULL);
}

static esp_err_t http_get_to_buffer(const char *url, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length >= (int)buffer_size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    int total = 0;
    while (total < (int)buffer_size - 1) {
        int read_len = esp_http_client_read(client, buffer + total, (int)buffer_size - 1 - total);
        if (read_len < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break;
        }
        total += read_len;
    }
    buffer[total] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        return err;
    }
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool parse_sha256_hex(const char *hex, uint8_t digest[32])
{
    if (hex == NULL || strlen(hex) != 64U) {
        return false;
    }

    for (size_t i = 0; i < 32U; ++i) {
        int high = hex_value(hex[i * 2U]);
        int low = hex_value(hex[(i * 2U) + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        digest[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void digest_to_hex(const uint8_t digest[32], char *buffer, size_t buffer_size)
{
    static const char k_hex[] = "0123456789abcdef";
    if (buffer == NULL || buffer_size < 65U) {
        return;
    }

    for (size_t i = 0; i < 32U; ++i) {
        buffer[i * 2U] = k_hex[(digest[i] >> 4) & 0x0f];
        buffer[(i * 2U) + 1U] = k_hex[digest[i] & 0x0f];
    }
    buffer[64] = '\0';
}

static bool parse_version_triplet(const char *version, int out[3])
{
    if (version == NULL || out == NULL) {
        return false;
    }

    const char *cursor = version;
    if (*cursor == 'v' || *cursor == 'V') {
        cursor++;
    }

    for (int part = 0; part < 3; ++part) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        int value = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            value = (value * 10) + (*cursor - '0');
            cursor++;
        }
        out[part] = value;
        if (part < 2) {
            if (*cursor != '.') {
                return false;
            }
            cursor++;
        }
    }

    return true;
}

static int compare_versions(const char *left, const char *right)
{
    int left_parts[3] = {0};
    int right_parts[3] = {0};
    if (!parse_version_triplet(left, left_parts) || !parse_version_triplet(right, right_parts)) {
        return strcmp(left != NULL ? left : "", right != NULL ? right : "");
    }

    for (int i = 0; i < 3; ++i) {
        if (left_parts[i] != right_parts[i]) {
            return left_parts[i] > right_parts[i] ? 1 : -1;
        }
    }
    return 0;
}

static esp_err_t fetch_manifest_firmware_url(void)
{
    char *manifest = heap_caps_malloc(TABFORGE_OTA_MANIFEST_MAX_BYTES, MALLOC_CAP_INTERNAL);
    if (manifest == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = http_get_to_buffer(g_ota_manifest_url, manifest, TABFORGE_OTA_MANIFEST_MAX_BYTES);
    if (err != ESP_OK) {
        heap_caps_free(manifest);
        return err;
    }

    cJSON *root = cJSON_Parse(manifest);
    heap_caps_free(manifest);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *latest = cJSON_GetObjectItemCaseSensitive(root, "latest");
    cJSON *version = cJSON_IsObject(latest) ? cJSON_GetObjectItemCaseSensitive(latest, "version") : NULL;
    cJSON *firmware = cJSON_IsObject(latest) ? cJSON_GetObjectItemCaseSensitive(latest, "firmware") : NULL;
    cJSON *available = cJSON_IsObject(firmware) ? cJSON_GetObjectItemCaseSensitive(firmware, "available") : NULL;
    cJSON *url = cJSON_IsObject(firmware) ? cJSON_GetObjectItemCaseSensitive(firmware, "url") : NULL;
    cJSON *sha256 = cJSON_IsObject(firmware) ? cJSON_GetObjectItemCaseSensitive(firmware, "sha256") : NULL;
    cJSON *size = cJSON_IsObject(firmware) ? cJSON_GetObjectItemCaseSensitive(firmware, "size") : NULL;

    if (cJSON_IsString(version) && version->valuestring != NULL) {
        strlcpy(g_ota_version, version->valuestring, sizeof(g_ota_version));
    }
    if (!cJSON_IsTrue(available) || !cJSON_IsString(url) || url->valuestring == NULL || url->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return ESP_ERR_NOT_FOUND;
    }
    if (g_ota_version[0] == '\0' || compare_versions(g_ota_version, TABFORGE_VERSION) <= 0) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_STATE;
    }
    if (!cJSON_IsString(sha256) || sha256->valuestring == NULL || !parse_sha256_hex(sha256->valuestring, (uint8_t[32]){0})) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    strlcpy(g_ota_firmware_url, url->valuestring, sizeof(g_ota_firmware_url));
    strlcpy(g_ota_sha256, sha256->valuestring, sizeof(g_ota_sha256));
    g_ota_size = cJSON_IsNumber(size) && size->valuedouble > 0 ? (uint32_t)size->valuedouble : 0U;
    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t download_and_apply_verified_ota(void)
{
    uint8_t expected_digest[32];
    if (!parse_sha256_hex(g_ota_sha256, expected_digest)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    esp_http_client_config_t http_config = {
        .url = g_ota_firmware_url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length > 0 && g_ota_size > 0U && (uint32_t)content_length != g_ota_size) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    uint8_t *buffer = heap_caps_malloc(TABFORGE_OTA_HTTP_CHUNK_SIZE, MALLOC_CAP_INTERNAL);
    if (buffer == NULL) {
        esp_ota_abort(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    mbedtls_sha256_context sha_context;
    mbedtls_sha256_init(&sha_context);
    mbedtls_sha256_starts(&sha_context, 0);

    uint32_t total = 0;
    while (true) {
        int read_len = esp_http_client_read(client, (char *)buffer, TABFORGE_OTA_HTTP_CHUNK_SIZE);
        if (read_len < 0) {
            err = ESP_FAIL;
            break;
        }
        if (read_len == 0) {
            break;
        }

        err = esp_ota_write(ota_handle, buffer, (size_t)read_len);
        if (err != ESP_OK) {
            break;
        }
        mbedtls_sha256_update(&sha_context, buffer, (size_t)read_len);
        total += (uint32_t)read_len;
    }

    uint8_t actual_digest[32];
    mbedtls_sha256_finish(&sha_context, actual_digest);
    mbedtls_sha256_free(&sha_context);
    heap_caps_free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && g_ota_size > 0U && total != g_ota_size) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK && memcmp(actual_digest, expected_digest, sizeof(actual_digest)) != 0) {
        char actual_hex[65];
        digest_to_hex(actual_digest, actual_hex, sizeof(actual_hex));
        ESP_LOGW(TABFORGE_TAG, "OTA SHA256 mismatch expected=%s actual=%s", g_ota_sha256, actual_hex);
        err = ESP_ERR_INVALID_RESPONSE;
    }

    if (err != ESP_OK) {
        esp_ota_abort(ota_handle);
        return err;
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err == ESP_OK) {
        ESP_LOGI(TABFORGE_TAG, "OTA verified %lu bytes sha256=%s", (unsigned long)total, g_ota_sha256);
    }
    return err;
}

static void ota_update_task(void *arg)
{
    (void)arg;
    if (!g_wifi_has_ip) {
        g_ota_state = OTA_STATE_ERROR;
        g_ota_last_error = ESP_ERR_INVALID_STATE;
        update_activity_from_task("OTA", "Connect Wi-Fi before OTA.");
        vTaskDelete(NULL);
        return;
    }

    g_ota_state = OTA_STATE_FETCHING_MANIFEST;
    g_ota_last_error = ESP_OK;
    update_activity_from_task("OTA", "Fetching manifest.");
    esp_err_t err = fetch_manifest_firmware_url();
    if (err != ESP_OK) {
        g_ota_state = OTA_STATE_ERROR;
        g_ota_last_error = err;
        update_activity_from_task("OTA Manifest",
                                  err == ESP_ERR_INVALID_STATE ? "No newer firmware in manifest." : esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    g_ota_state = OTA_STATE_DOWNLOADING;
    update_activity_from_task("OTA Download", g_ota_version[0] != '\0' ? g_ota_version : g_ota_firmware_url);
    err = download_and_apply_verified_ota();
    if (err == ESP_OK) {
        g_ota_state = OTA_STATE_READY_REBOOT;
        g_ota_reboot_pending = true;
        append_event("ota_update_ready");
        update_activity_from_task("OTA Ready", "Firmware written. Reboot from Update page.");
    } else {
        g_ota_state = OTA_STATE_ERROR;
        g_ota_last_error = err;
        ESP_LOGW(TABFORGE_TAG, "OTA failed: %s", esp_err_to_name(err));
        update_activity_from_task("OTA Failed", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}

static void wifi_scan_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    xTaskCreate(wifi_scan_task, "tabforge-wifi-scan", 6144, NULL, 5, NULL);
}

static void wifi_next_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (g_wifi_ap_count > 0) {
        g_wifi_selected_ap = (uint16_t)((g_wifi_selected_ap + 1U) % g_wifi_ap_count);
        if (g_ui.wifi_ssid_textarea != NULL) {
            wifi_ap_record_t *ap = &g_wifi_aps[g_wifi_selected_ap % g_wifi_ap_count];
            lv_textarea_set_text(g_ui.wifi_ssid_textarea, (const char *)ap->ssid);
        }
        refresh_wifi_widgets_locked();
        append_event("wifi_next_ap");
    }
}

static void wifi_connect_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (g_ui.wifi_ssid_textarea != NULL) {
        const char *ssid = lv_textarea_get_text(g_ui.wifi_ssid_textarea);
        strlcpy(g_wifi_credentials.ssid, ssid != NULL ? ssid : "", sizeof(g_wifi_credentials.ssid));
    }
    if (g_ui.wifi_password_textarea != NULL) {
        const char *password = lv_textarea_get_text(g_ui.wifi_password_textarea);
        strlcpy(g_wifi_credentials.password, password != NULL ? password : "", sizeof(g_wifi_credentials.password));
    }
    g_wifi_credentials.configured = g_wifi_credentials.ssid[0] != '\0';
    g_wifi_credentials.auto_connect = g_wifi_credentials.configured;
    xTaskCreate(wifi_connect_task, "tabforge-wifi-connect", 6144, NULL, 5, NULL);
}

static void wifi_textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target(event);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        if (g_ui.wifi_keyboard != NULL) {
            lv_keyboard_set_textarea(g_ui.wifi_keyboard, target);
            lv_obj_clear_flag(g_ui.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (g_ui.wifi_keyboard != NULL) {
            lv_obj_add_flag(g_ui.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ota_start_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    xTaskCreate(ota_update_task, "tabforge-ota", 8192, NULL, 5, NULL);
}

static void ota_reboot_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }
    if (g_ota_reboot_pending) {
        append_event("ota_reboot");
        esp_restart();
    }
    set_activity("OTA", "No completed OTA is waiting for reboot.");
}

static void grove_uart_send_line(const char *line)
{
    if (!g_grove_uart_ready || line == NULL || line[0] == '\0') {
        return;
    }
    uart_write_bytes(GROVE_UART_NUM, line, strlen(line));
    uart_write_bytes(GROVE_UART_NUM, "\n", 1);
    append_event("grove_uart_tx");
    ESP_LOGI(TABFORGE_TAG, "Grove UART TX: %s", line);
}

static void mesh_probe_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    if (!g_ext_power_ready) {
        set_accessory_power(true);
        start_accessory_probe_tasks();
    }
    const char *probe = g_meshcore_mode ? "help" : "TabForge Meshtastic link test";
    grove_uart_send_line(probe);
    set_activity("Mesh Probe", probe);
    append_event("mesh_probe_sent");
}

static void mode_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    g_meshcore_mode = !g_meshcore_mode;
    refresh_mode_widgets();
    if (g_mesh_module_ready) {
        set_activity(active_mode_name(), active_mode_detail());
    } else {
        set_activity("Mesh profile", selected_mesh_profile_name());
    }
    append_event(g_meshcore_mode ? "mode_meshcore" : "mode_meshtastic");
    grove_uart_send_line(g_meshcore_mode ? "help" : "TabForge Meshtastic link test");
    ESP_LOGI(TABFORGE_TAG, "mesh profile switched to %s", selected_mesh_profile_name());
}

static void auto_rotate_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    g_auto_rotate = !g_auto_rotate;
    g_pending_rotation_count = 0;
    refresh_rotation_widgets();
    set_activity("Rotation", g_auto_rotate ? "IMU auto-rotate is active" : "Auto-rotate paused on current view");
    append_event(g_auto_rotate ? "auto_rotate_on" : "auto_rotate_off");
}

static void screen_timeout_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    g_screen_timeout_index = (uint8_t)((g_screen_timeout_index + 1U) % screen_timeout_count());
    save_screen_timeout_setting();
    lv_display_trigger_activity(g_display);
    refresh_screen_widgets_locked();
    set_activity("Screen Timeout", screen_timeout_seconds() == 0 ? "Screen timeout disabled" : screen_timeout_label());
    append_event("screen_timeout_changed");
    ESP_LOGI(TABFORGE_TAG, "screen timeout set to %s", screen_timeout_label());
}

static void sleep_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    set_activity("Sleep", "Display sleep active. Touch the screen to wake.");
    refresh_screen_widgets_locked();
    enter_screen_sleep("screen_sleep_button");
}

static void accessory_power_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    bool enable = !(g_ext_power_ready || g_usb_power_ready);
    set_accessory_power(enable);
    if (enable) {
        start_accessory_probe_tasks();
    }
    refresh_accessory_widgets_locked();
    refresh_live_stats_locked();
    set_activity(enable ? "Accessories On" : "Accessories Off",
                 enable ? "Add-ons powered after boot." : "Add-on rails powered down.");
    append_event(enable ? "accessories_power_on" : "accessories_power_off");
}

static void update_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    show_nav_page(NAV_PAGE_UPDATE);
    append_event("update_center_selected");
    ESP_LOGI(TABFORGE_TAG, "update center selected: %s", TABFORGE_MANIFEST_URL);
}

static void apps_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    g_active_app = APP_NONE;
    show_nav_page(NAV_PAGE_APPS);
    append_event("apps_button_selected");
    ESP_LOGI(TABFORGE_TAG, "apps button selected");
}

static void back_to_apps_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    g_active_app = APP_NONE;
    show_nav_page(NAV_PAGE_APPS);
    append_event("app_back_to_apps");
}

static void settings_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    show_nav_page(NAV_PAGE_SETTINGS);
    append_event("settings_button_selected");
    ESP_LOGI(TABFORGE_TAG, "settings button selected");
}

static void build_top_bar(lv_obj_t *screen, lv_coord_t width, lv_coord_t height, bool landscape)
{
    (void)landscape;

    lv_obj_t *top = lv_obj_create(screen);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, width, height);
    lv_obj_set_style_bg_color(top, color_hex(0x080c0f), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top, 10, 0);
    lv_obj_set_style_pad_ver(top, 6, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_ui.status_label = make_text(top, "", 0xf1f7f3, (width * 36) / 100);
    g_ui.rotation_label = make_text(top, "", 0x93a6ad, (width * 36) / 100);
    lv_obj_set_style_text_align(g_ui.rotation_label, LV_TEXT_ALIGN_RIGHT, 0);
    g_ui.battery_label = make_text(top, "BAT --", 0xffc857, (width * 20) / 100);
    lv_obj_set_style_text_align(g_ui.battery_label, LV_TEXT_ALIGN_RIGHT, 0);
}

static void build_home_header(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *header = make_panel(parent, width, height, 0x11191e, 0x263b45);
    lv_obj_set_style_radius(header, 22, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 5, 0);

    g_ui.mode_label = make_text(header, "", 0xf1f7f3, width - 28);
    lv_obj_set_style_text_align(g_ui.mode_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.mode_detail_label = make_text(header, "", 0x93a6ad, width - 28);
    lv_obj_set_style_text_align(g_ui.mode_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.activity_title_label = make_text(header, "Home", 0x70a7ff, width - 28);
    lv_obj_set_style_text_align(g_ui.activity_title_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.activity_detail_label = make_text(header, "Ready", 0xf1f7f3, width - 28);
    lv_obj_set_style_text_align(g_ui.activity_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.gyro_label = make_text(header, "gyro pending", 0x61d5f0, width - 28);
    lv_obj_set_style_text_align(g_ui.gyro_label, LV_TEXT_ALIGN_CENTER, 0);
}

static void build_quick_status(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *stat_grid = lv_obj_create(parent);
    lv_obj_remove_style_all(stat_grid);
    lv_obj_set_size(stat_grid, width, height);
    lv_obj_set_flex_flow(stat_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(stat_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(stat_grid, 8, 0);
    lv_obj_set_style_pad_column(stat_grid, 8, 0);

    lv_coord_t columns = landscape ? 6 : 3;
    lv_coord_t card_w = (width - ((columns - 1) * 8)) / columns;
    lv_coord_t card_h = landscape ? height : (height - 8) / 2;
    if (card_h < 56) {
        card_h = 56;
    }

    add_stat_card(stat_grid, "Wi-Fi", &g_ui.wifi_label, NULL, card_w, card_h, 0x70a7ff);
    add_stat_card(stat_grid, "Battery", &g_ui.battery_card_label, NULL, card_w, card_h, 0xffc857);
    add_stat_card(stat_grid, "Mic", &g_ui.mic_label, &g_ui.mic_bar, card_w, card_h, 0xb982ff);
    add_stat_card(stat_grid, "SD", &g_ui.sd_label, NULL, card_w, card_h, 0xff7a66);
    add_stat_card(stat_grid, "Uptime", &g_ui.uptime_label, NULL, card_w, card_h, 0x43d17a);
    add_stat_card(stat_grid, "Screen", &g_ui.screen_label, NULL, card_w, card_h, 0x77dd88);
}

static void build_app_grid(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, width, height);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_column(grid, 14, 0);

    size_t tile_count = sizeof(g_tiles) / sizeof(g_tiles[0]);
    lv_coord_t columns = landscape ? 6 : 4;
    lv_coord_t rows = (lv_coord_t)((tile_count + (size_t)columns - 1U) / (size_t)columns);
    lv_coord_t tile_w = (width - ((columns - 1) * 14)) / columns;
    lv_coord_t tile_h = (height - ((rows - 1) * 14)) / rows;
    if (tile_h > 118) {
        tile_h = 118;
    }
    if (tile_h < 84) {
        tile_h = 84;
    }
    for (size_t i = 0; i < tile_count; ++i) {
        add_app_tile(grid, &g_tiles[i], tile_w, tile_h);
    }
}

static lv_obj_t *make_page(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, width, height);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static lv_obj_t *add_info_tile(lv_obj_t *parent,
                               const char *title,
                               const char *value,
                               const char *hint,
                               lv_coord_t width,
                               lv_coord_t height,
                               uint32_t accent)
{
    lv_obj_t *card = make_panel(parent, width, height, 0x11181d, accent);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 4, 0);

    make_text(card, title, 0x8fb0bb, width - 24);
    lv_obj_t *value_label = make_text(card, value, 0xf1f7f3, width - 24);
    if (hint != NULL && hint[0] != '\0') {
        make_text(card, hint, 0x93a6ad, width - 24);
    }
    return value_label;
}

static void build_settings_page(lv_obj_t *page, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(page, 10, 0);

    lv_coord_t controls_h = landscape ? 164 : 216;
    lv_obj_t *controls = lv_obj_create(page);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, width, controls_h);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(controls, 10, 0);
    lv_obj_set_style_pad_column(controls, 10, 0);

    lv_coord_t control_cols = landscape ? 3 : 2;
    lv_coord_t control_w = (width - ((control_cols - 1) * 10)) / control_cols;
    make_button(controls, control_w, "Wi-Fi & Internet", update_button_event_cb);
    make_button(controls, control_w, "Display", screen_timeout_button_event_cb);
    make_button(controls, control_w, "Sleep Now", sleep_button_event_cb);
    make_button(controls, control_w, "Mesh Profile", mode_button_event_cb);
    make_button(controls, control_w, "Accessories", accessory_power_button_event_cb);
    make_button(controls, control_w, "Auto Rotate", auto_rotate_button_event_cb);
    make_button(controls, control_w, "OTA Update", update_button_event_cb);

    lv_obj_t *grid = lv_obj_create(page);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, width, height - controls_h - 10);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);

    lv_coord_t columns = landscape ? 3 : 2;
    lv_coord_t card_w = (width - ((columns - 1) * 10)) / columns;
    lv_coord_t card_h = landscape ? 86 : 96;
    char sd_value[48];
    char rotation_value[48];
    char battery_value[48];
    char screen_value[48];
    char wifi_value[96];
    char accessory_value[64];

    snprintf(sd_value, sizeof(sd_value), "%s",
             g_sd_ready ? (g_sd_recovered ? "recovered" : "mounted") : esp_err_to_name(g_sd_last_error));
    snprintf(rotation_value, sizeof(rotation_value), "%s at %d deg",
             g_auto_rotate ? "Auto" : "Locked",
             rotation_degrees(g_rotation));
    format_battery_status(battery_value, sizeof(battery_value), false);
    format_screen_status(screen_value, sizeof(screen_value));
    format_wifi_status(wifi_value, sizeof(wifi_value));
    format_accessory_status(accessory_value, sizeof(accessory_value));

    g_ui.wifi_label = add_info_tile(grid, "Wi-Fi & Internet", wifi_value, "Tap Wi-Fi & Internet above to scan, type a password, and connect.", card_w, card_h, 0x70a7ff);
    g_ui.accessory_label = add_info_tile(grid, "Accessories", accessory_value, "Tap Accessories after boot to power Grove/PA and USB-A add-ons.", card_w, card_h, 0x43d17a);
    add_info_tile(grid, "SD", sd_value, "Creates /tabforge config, logs, audio, backups, and IR folders.", card_w, card_h, 0x61d5f0);
    g_ui.settings_screen_label = add_info_tile(grid, "Display", screen_value, "Timeout button cycles values; Sleep turns display off.", card_w, card_h, 0x77dd88);
    add_info_tile(grid, "Rotation", rotation_value, "Gyro auto-rotate can be locked from the dock.", card_w, card_h, 0x77dd88);
    add_info_tile(grid, "Battery", battery_value, "Runtime and charge state.", card_w, card_h, 0xffc857);
    add_info_tile(grid, "Updates", ota_state_text(), "Internet OTA uses Wi-Fi.", card_w, card_h, 0xffc857);
}

static void build_update_page(lv_obj_t *page, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *card = make_panel(page, width, height, 0x10161a, 0xffc857);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, 0);

    char wifi_value[96];
    char scan_value[96];
    format_wifi_status(wifi_value, sizeof(wifi_value));
    format_wifi_scan_status(scan_value, sizeof(scan_value));

    make_text(card, "Wi-Fi + OTA", 0xffc857, width - 32);
    g_ui.wifi_label = make_text(card, wifi_value, 0xf1f7f3, width - 32);
    g_ui.wifi_scan_label = make_text(card, scan_value, 0x93a6ad, width - 32);
    g_ui.ota_label = make_text(card, g_ota_manifest_url, 0x93a6ad, width - 32);

    g_ui.wifi_ssid_textarea = lv_textarea_create(card);
    lv_obj_set_size(g_ui.wifi_ssid_textarea, width - 32, 42);
    lv_textarea_set_one_line(g_ui.wifi_ssid_textarea, true);
    lv_textarea_set_max_length(g_ui.wifi_ssid_textarea, TABFORGE_WIFI_MAX_SSID - 1);
    lv_textarea_set_placeholder_text(g_ui.wifi_ssid_textarea, "Wi-Fi name");
    lv_textarea_set_text(g_ui.wifi_ssid_textarea, g_wifi_credentials.ssid);
    lv_obj_set_style_radius(g_ui.wifi_ssid_textarea, 8, 0);
    lv_obj_set_style_bg_color(g_ui.wifi_ssid_textarea, color_hex(0x172126), 0);
    lv_obj_set_style_text_color(g_ui.wifi_ssid_textarea, color_hex(0xf1f7f3), 0);
    lv_obj_set_style_border_color(g_ui.wifi_ssid_textarea, color_hex(0x3c525a), 0);
    lv_obj_add_event_cb(g_ui.wifi_ssid_textarea, wifi_textarea_event_cb, LV_EVENT_ALL, NULL);

    g_ui.wifi_password_textarea = lv_textarea_create(card);
    lv_obj_set_size(g_ui.wifi_password_textarea, width - 32, 42);
    lv_textarea_set_one_line(g_ui.wifi_password_textarea, true);
    lv_textarea_set_password_mode(g_ui.wifi_password_textarea, true);
    lv_textarea_set_max_length(g_ui.wifi_password_textarea, TABFORGE_WIFI_MAX_PASSWORD - 1);
    lv_textarea_set_placeholder_text(g_ui.wifi_password_textarea, "Password");
    lv_textarea_set_text(g_ui.wifi_password_textarea, g_wifi_credentials.password);
    lv_obj_set_style_radius(g_ui.wifi_password_textarea, 8, 0);
    lv_obj_set_style_bg_color(g_ui.wifi_password_textarea, color_hex(0x172126), 0);
    lv_obj_set_style_text_color(g_ui.wifi_password_textarea, color_hex(0xf1f7f3), 0);
    lv_obj_set_style_border_color(g_ui.wifi_password_textarea, color_hex(0x3c525a), 0);
    lv_obj_add_event_cb(g_ui.wifi_password_textarea, wifi_textarea_event_cb, LV_EVENT_ALL, NULL);

    lv_coord_t button_w = landscape ? (width - 54) / 3 : width - 32;
    lv_obj_t *buttons = lv_obj_create(card);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, width - 24, landscape ? 102 : 272);
    lv_obj_clear_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(buttons, 10, 0);
    lv_obj_set_style_pad_column(buttons, 10, 0);

    make_button(buttons, button_w, "Scan Wi-Fi", wifi_scan_button_event_cb);
    make_button(buttons, button_w, "Next Network", wifi_next_button_event_cb);
    make_button(buttons, button_w, "Connect Wi-Fi", wifi_connect_button_event_cb);
    make_button(buttons, button_w, "Run Internet OTA", ota_start_button_event_cb);
    make_button(buttons, button_w, "Reboot After OTA", ota_reboot_button_event_cb);

    g_ui.wifi_keyboard = lv_keyboard_create(card);
    lv_obj_set_size(g_ui.wifi_keyboard, width - 32, landscape ? 150 : 180);
    lv_keyboard_set_mode(g_ui.wifi_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(g_ui.wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static const feature_tile_t *find_tile_by_app(app_id_t app_id)
{
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (g_tiles[i].app_id == app_id) {
            return &g_tiles[i];
        }
    }
    return NULL;
}

static void add_app_status_line(lv_obj_t *parent,
                                const char *title,
                                const char *value,
                                lv_coord_t width,
                                uint32_t accent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text_fmt(label, "%s: %s", title, value);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_color(label, color_hex(accent), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
}

static void add_app_actions(lv_obj_t *parent,
                            lv_coord_t width,
                            bool landscape,
                            app_id_t app_id)
{
    lv_obj_t *actions = lv_obj_create(parent);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, width, landscape ? 112 : 236);
    lv_obj_clear_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(actions, 10, 0);
    lv_obj_set_style_pad_column(actions, 10, 0);

    lv_coord_t columns = landscape ? 3 : 2;
    lv_coord_t button_w = (width - ((columns - 1) * 10)) / columns;
    if (!landscape && button_w < 120) {
        columns = 1;
        button_w = width;
    }

    make_button(actions, button_w, "Back", back_to_apps_button_event_cb);

    switch (app_id) {
    case APP_WIFI:
        make_button(actions, button_w, "Scan", wifi_scan_button_event_cb);
        make_button(actions, button_w, "Next AP", wifi_next_button_event_cb);
        make_button(actions, button_w, "Connect", wifi_connect_button_event_cb);
        make_button(actions, button_w, "Wi-Fi Center", update_button_event_cb);
        break;
    case APP_MESSAGES:
        make_button(actions, button_w, "Send Test", mesh_probe_button_event_cb);
        make_button(actions, button_w, "Accessories", accessory_power_button_event_cb);
        make_button(actions, button_w, "Wi-Fi Center", update_button_event_cb);
        break;
    case APP_MESHCORE:
        make_button(actions, button_w, "Switch Mode", mode_button_event_cb);
        make_button(actions, button_w, "Accessories", accessory_power_button_event_cb);
        make_button(actions, button_w, "Send Help", mesh_probe_button_event_cb);
        break;
    case APP_TDECK:
        make_button(actions, button_w, "Accessories", accessory_power_button_event_cb);
        make_button(actions, button_w, "Mesh Mode", mode_button_event_cb);
        make_button(actions, button_w, "Wi-Fi Center", update_button_event_cb);
        break;
    case APP_IR:
        make_button(actions, button_w, "Accessories", accessory_power_button_event_cb);
        make_button(actions, button_w, "Settings", settings_button_event_cb);
        break;
    case APP_RECORDER:
        make_button(actions, button_w, "Sleep", sleep_button_event_cb);
        make_button(actions, button_w, "Settings", settings_button_event_cb);
        break;
    case APP_USB:
        make_button(actions, button_w, "Accessories", accessory_power_button_event_cb);
        make_button(actions, button_w, "Mesh Mode", mode_button_event_cb);
        break;
    case APP_FILES:
        make_button(actions, button_w, "Settings", settings_button_event_cb);
        make_button(actions, button_w, "Wi-Fi Center", update_button_event_cb);
        break;
    case APP_UPDATE:
        make_button(actions, button_w, "Wi-Fi Center", update_button_event_cb);
        make_button(actions, button_w, "Run OTA", ota_start_button_event_cb);
        make_button(actions, button_w, "Reboot OTA", ota_reboot_button_event_cb);
        break;
    case APP_NONE:
    default:
        make_button(actions, button_w, "Settings", settings_button_event_cb);
        break;
    }
}

static void render_active_app_page_locked(void)
{
    if (g_ui.page_app == NULL) {
        return;
    }

    lv_obj_clean(g_ui.page_app);
    lv_coord_t width = lv_obj_get_width(g_ui.page_app);
    lv_coord_t height = lv_obj_get_height(g_ui.page_app);
    if (width <= 0 || height <= 0) {
        return;
    }

    bool landscape = is_landscape_rotation(g_rotation);
    const feature_tile_t *tile = find_tile_by_app(g_active_app);
    if (tile == NULL) {
        tile = &g_tiles[0];
        g_active_app = tile->app_id;
    }

    lv_obj_t *card = make_panel(g_ui.page_app, width, height, 0x10161a, tile->accent);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_obj_t *title = make_text(card, tile->name, tile->accent, width - 32);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    make_text(card, tile->summary, 0xf1f7f3, width - 32);

    char line_a[128];
    char line_b[128];
    char line_c[128];
    char battery_status[32];
    char screen_status[48];
    format_battery_status(battery_status, sizeof(battery_status), false);
    format_screen_status(screen_status, sizeof(screen_status));

    switch (g_active_app) {
    case APP_WIFI:
        format_wifi_status(line_a, sizeof(line_a));
        format_wifi_scan_status(line_b, sizeof(line_b));
        snprintf(line_c, sizeof(line_c), "OTA %s | %s", ota_state_text(), g_ota_version[0] ? g_ota_version : "manifest");
        add_app_status_line(card, "Network", line_a, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Scan", line_b, width - 32, 0x93a6ad);
        add_app_status_line(card, "Update", line_c, width - 32, 0xffc857);
        break;
    case APP_MESSAGES:
        snprintf(line_a, sizeof(line_a), "%s | %s", active_mode_name(), active_mode_detail());
        snprintf(line_b, sizeof(line_b), "Grove RX %lu packets / %lu bytes",
                 (unsigned long)g_grove_rx_packets,
                 (unsigned long)g_grove_rx_bytes);
        snprintf(line_c, sizeof(line_c), "USB %s | radio %s", usb_state_text(), g_mesh_module_ready ? "heard" : "waiting");
        add_app_status_line(card, "Mode", line_a, width - 32, 0xf1f7f3);
        add_app_status_line(card, "C6L", line_b, width - 32, 0x43d17a);
        add_app_status_line(card, "Link", line_c, width - 32, 0x93a6ad);
        break;
    case APP_MESHCORE:
        snprintf(line_a, sizeof(line_a), "Selected %s", selected_mesh_profile_name());
        snprintf(line_b, sizeof(line_b), "Grove %s | USB %s",
                 g_grove_uart_ready ? "ready" : "pending",
                 usb_state_text());
        snprintf(line_c, sizeof(line_c), "Use Switch Mode to flip profiles and send a serial probe.");
        add_app_status_line(card, "Profile", line_a, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Transport", line_b, width - 32, 0x61d5f0);
        add_app_status_line(card, "Action", line_c, width - 32, 0x93a6ad);
        break;
    case APP_TDECK:
        snprintf(line_a, sizeof(line_a), "USB %s | opens %lu | disconnects %lu",
                 usb_state_text(),
                 (unsigned long)g_usb_open_count,
                 (unsigned long)g_usb_disconnect_count);
        snprintf(line_b, sizeof(line_b), "RX %lu packets / %lu bytes",
                 (unsigned long)g_usb_rx_packets,
                 (unsigned long)g_usb_rx_bytes);
        snprintf(line_c, sizeof(line_c), "T-Deck companion profile stays on USB CDC.");
        add_app_status_line(card, "Bridge", line_a, width - 32, 0xf0bf4f);
        add_app_status_line(card, "Serial", line_b, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Profile", line_c, width - 32, 0x93a6ad);
        break;
    case APP_IR:
        snprintf(line_a, sizeof(line_a), "IR %s | level %d | edges %lu",
                 g_ir_probe_ready ? "ready" : "pending",
                 g_ir_level,
                 (unsigned long)g_ir_edges);
        snprintf(line_b, sizeof(line_b), "Grove RX GPIO%u TX GPIO%u",
                 (unsigned)TABFORGE_IR_RX_GPIO,
                 (unsigned)TABFORGE_IR_TX_GPIO);
        snprintf(line_c, sizeof(line_c), "Event log: %s", g_sd_ready ? "enabled" : "SD missing");
        add_app_status_line(card, "Receiver", line_a, width - 32, 0xff7a66);
        add_app_status_line(card, "Pins", line_b, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Storage", line_c, width - 32, 0x93a6ad);
        break;
    case APP_RECORDER:
        snprintf(line_a, sizeof(line_a), "%s | avg %d | peak %d", mic_state_text(), g_mic_average, g_mic_peak);
        snprintf(line_b, sizeof(line_b), "reads %lu | clips path /tabforge/audio", (unsigned long)g_mic_reads);
        snprintf(line_c, sizeof(line_c), "Screen %s | battery %s", screen_status, battery_status);
        add_app_status_line(card, "Mic", line_a, width - 32, 0xb982ff);
        add_app_status_line(card, "Capture", line_b, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Power", line_c, width - 32, 0x93a6ad);
        break;
    case APP_USB:
        snprintf(line_a, sizeof(line_a), "USB-A 5V %s | host %s",
                 g_usb_power_ready ? "on" : "off",
                 g_usb_host_ready ? "ready" : "waiting");
        snprintf(line_b, sizeof(line_b), "%s | RX %lu packets / %lu bytes",
                 usb_state_text(),
                 (unsigned long)g_usb_rx_packets,
                 (unsigned long)g_usb_rx_bytes);
        snprintf(line_c, sizeof(line_c), "Last error %s", esp_err_to_name(g_usb_last_error));
        add_app_status_line(card, "Power", line_a, width - 32, 0x70a7ff);
        add_app_status_line(card, "CDC", line_b, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Driver", line_c, width - 32, 0x93a6ad);
        break;
    case APP_FILES:
        snprintf(line_a, sizeof(line_a), "SD %s | %s",
                 g_sd_ready ? (g_sd_recovered ? "recovered" : "mounted") : "missing",
                 g_sd_ready ? TABFORGE_SD_ROOT : esp_err_to_name(g_sd_last_error));
        snprintf(line_b, sizeof(line_b), "Config /tabforge/config.json");
        snprintf(line_c, sizeof(line_c), "Logs, audio, IR, and backups stay on SD.");
        add_app_status_line(card, "Card", line_a, width - 32, 0x77dd88);
        add_app_status_line(card, "Config", line_b, width - 32, 0xf1f7f3);
        add_app_status_line(card, "Paths", line_c, width - 32, 0x93a6ad);
        break;
    case APP_UPDATE:
        snprintf(line_a, sizeof(line_a), "%s | manifest %s",
                 ota_state_text(),
                 g_ota_manifest_url[0] ? "configured" : "missing");
        snprintf(line_b, sizeof(line_b), "remote %s | running %s",
                 g_ota_version[0] ? g_ota_version : "unknown",
                 TABFORGE_VERSION);
        snprintf(line_c, sizeof(line_c), "hash %s", g_ota_sha256[0] ? g_ota_sha256 : "not fetched");
        add_app_status_line(card, "OTA", line_a, width - 32, 0xffc857);
        add_app_status_line(card, "Version", line_b, width - 32, 0xf1f7f3);
        add_app_status_line(card, "SHA256", line_c, width - 32, 0x93a6ad);
        break;
    case APP_NONE:
    default:
        add_app_status_line(card, "Launcher", "Choose an app from Home.", width - 32, 0xf1f7f3);
        break;
    }

    add_app_actions(card, width - 32, landscape, g_active_app);
    set_activity(tile->name, tile->summary);
}

static void build_pages(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *pages = lv_obj_create(parent);
    lv_obj_remove_style_all(pages);
    lv_obj_set_size(pages, width, height);
    lv_obj_clear_flag(pages, LV_OBJ_FLAG_SCROLLABLE);

    g_ui.page_apps = make_page(pages, width, height);
    g_ui.page_settings = make_page(pages, width, height);
    g_ui.page_update = make_page(pages, width, height);
    g_ui.page_app = make_page(pages, width, height);

    build_app_grid(g_ui.page_apps, width, height, landscape);
    build_settings_page(g_ui.page_settings, width, height, landscape);
    build_update_page(g_ui.page_update, width, height, landscape);
    render_active_app_page_locked();
}

static void build_body(lv_obj_t *screen, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, width, height);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body, 12, 0);

    lv_coord_t header_h = landscape ? 106 : 148;
    lv_coord_t quick_h = landscape ? 72 : 118;
    lv_coord_t apps_h = height - header_h - quick_h - 24;
    if (apps_h < 220) {
        apps_h = 220;
    }

    build_home_header(body, width, header_h);
    build_quick_status(body, width, quick_h, landscape);
    build_pages(body, width, apps_h, landscape);
}

static void build_dock(lv_obj_t *screen, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *dock = make_panel(screen, width, height, 0x11171c, 0x26333b);
    lv_obj_set_style_radius(dock, 24, 0);
    lv_obj_set_style_bg_opa(dock, LV_OPA_90, 0);
    lv_obj_clear_flag(dock, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dock, 8, 0);

    lv_coord_t button_w = (width - 64) / 5;
    g_ui.nav_apps = make_nav_button(dock, button_w, LV_SYMBOL_LIST, "Home", apps_button_event_cb);
    g_ui.nav_settings = make_nav_button(dock, button_w, LV_SYMBOL_SETTINGS, "Settings", settings_button_event_cb);
    g_ui.nav_mode = make_nav_button(dock, button_w, LV_SYMBOL_SHUFFLE, "Mesh", mode_button_event_cb);
    g_ui.nav_auto = make_nav_button(dock, button_w, LV_SYMBOL_REFRESH, "Auto", auto_rotate_button_event_cb);
    g_ui.nav_update = make_nav_button(dock, button_w, LV_SYMBOL_WIFI, "Wi-Fi", update_button_event_cb);

    g_ui.dock_mode_label = g_ui.nav_mode.label;
    g_ui.dock_auto_label = g_ui.nav_auto.label;
    lv_obj_move_foreground(dock);
}

static void build_dashboard(lv_obj_t *screen)
{
    memset(&g_ui, 0, sizeof(g_ui));

    bool landscape = is_landscape_rotation(g_rotation);
    lv_coord_t screen_w = (lv_coord_t)deck_width();
    lv_coord_t screen_h = (lv_coord_t)deck_height();
    lv_coord_t pad = landscape ? 14 : 10;
    lv_coord_t gap = landscape ? 10 : 8;
    lv_coord_t top_h = landscape ? 38 : 42;
    lv_coord_t dock_h = landscape ? 82 : 88;
    lv_coord_t content_w = screen_w - (pad * 2);
    lv_coord_t body_h = screen_h - (pad * 2) - top_h - dock_h - (gap * 2);

    lv_obj_set_size(screen, screen_w, screen_h);
    lv_obj_set_style_bg_color(screen, color_hex(0x090d10), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, pad, 0);
    lv_obj_set_style_pad_row(screen, gap, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    build_top_bar(screen, content_w, top_h, landscape);
    build_body(screen, content_w, body_h, landscape);
    build_dock(screen, content_w, dock_h);
    show_nav_page(g_nav_page);

    refresh_live_stats_locked();
}

static bool target_rotation_from_accel(lv_disp_rotation_t *target)
{
    if (!g_imu_ready) {
        return false;
    }

    float ax = g_last_acce.x;
    float ay = g_last_acce.y;
    float abs_x = axis_abs(ax);
    float abs_y = axis_abs(ay);
    if (abs_x < 0.45f && abs_y < 0.45f) {
        return false;
    }
    if (axis_abs(abs_x - abs_y) < 0.12f) {
        return false;
    }

    if (abs_x > abs_y) {
        *target = ax > 0.0f ? LV_DISPLAY_ROTATION_90 : LV_DISPLAY_ROTATION_270;
    } else {
        *target = ay > 0.0f ? LV_DISPLAY_ROTATION_0 : LV_DISPLAY_ROTATION_180;
    }

    return true;
}

static void apply_rotation_locked(lv_disp_rotation_t target)
{
    if (g_display == NULL || target == g_rotation) {
        return;
    }

    g_rotation = target;
    bsp_display_rotate(g_display, g_rotation);
    lv_obj_t *screen = lv_display_get_screen_active(g_display);
    lv_obj_clean(screen);
    build_dashboard(screen);
    ESP_LOGI(TABFORGE_TAG, "display rotated to %s (%d deg)",
             rotation_name(g_rotation),
             rotation_degrees(g_rotation));
}

static void maybe_apply_auto_rotation_locked(void)
{
    if (!g_auto_rotate) {
        return;
    }

    lv_disp_rotation_t target = g_rotation;
    if (!target_rotation_from_accel(&target)) {
        g_pending_rotation_count = 0;
        return;
    }

    if (target != g_pending_rotation) {
        g_pending_rotation = target;
        g_pending_rotation_count = 1;
        return;
    }

    if (g_pending_rotation_count < ROTATION_CONFIRM_SAMPLES) {
        g_pending_rotation_count++;
        return;
    }

    if (target != g_rotation) {
        apply_rotation_locked(target);
        append_event("auto_rotate");
    }
    g_pending_rotation_count = 0;
}

#if BSP_CAPS_IMU
static void sensor_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)handler_args;
    (void)base;
    sensor_data_t *sensor_data = (sensor_data_t *)event_data;
    if (sensor_data == NULL) {
        return;
    }

    switch (id) {
    case SENSOR_STARTED:
        g_imu_online = true;
        ESP_LOGI(TABFORGE_TAG, "IMU sensor started: %s_0x%x",
                 sensor_data->sensor_name,
                 sensor_data->sensor_addr);
        break;
    case SENSOR_ACCE_DATA_READY:
        g_last_acce = sensor_data->acce;
        g_imu_ready = true;
        g_last_imu_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        break;
    case SENSOR_GYRO_DATA_READY:
        g_last_gyro = sensor_data->gyro;
        g_imu_ready = true;
        g_last_imu_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        break;
    default:
        break;
    }
}

static void input_activity_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
        code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
        g_input_activity_seq++;
    }
}

static void check_screen_power_locked(void)
{
    if (g_display == NULL) {
        return;
    }

    uint32_t now_ms = now_ms_u32();
    if (g_screen_sleeping) {
        if (g_input_activity_seq != g_sleep_input_seq) {
            if ((uint32_t)(now_ms - g_screen_sleep_start_ms) < TABFORGE_SCREEN_WAKE_GRACE_MS) {
                g_sleep_input_seq = g_input_activity_seq;
            } else {
                exit_screen_sleep("screen_touch_wake");
                refresh_screen_widgets_locked();
            }
        }
        return;
    }

    if (g_screen_dimmed) {
        if (lv_display_get_inactive_time(g_display) < 1000U) {
            exit_screen_dim("screen_touch_undim");
            refresh_screen_widgets_locked();
            return;
        }
        if ((uint32_t)(now_ms - g_screen_dim_start_ms) >= TABFORGE_SCREEN_DIM_GRACE_MS) {
            enter_screen_sleep("screen_timeout_sleep");
            refresh_screen_widgets_locked();
        }
        return;
    }

    uint32_t timeout_seconds = screen_timeout_seconds();
    if (timeout_seconds == 0) {
        return;
    }

    uint32_t inactive_ms = lv_display_get_inactive_time(g_display);
    if (inactive_ms >= (timeout_seconds * 1000U)) {
        enter_screen_dim("screen_timeout_dim");
        refresh_screen_widgets_locked();
    }
}

static void init_imu(void)
{
    bsp_sensor_config_t imu_config = {
        .type = IMU_ID,
        .mode = MODE_POLLING,
        .period = IMU_SAMPLE_PERIOD_MS,
    };

    esp_err_t err = bsp_sensor_init(&imu_config, &g_imu_sensor_handle);
    if (err != ESP_OK) {
        g_imu_online = false;
        ESP_LOGW(TABFORGE_TAG, "IMU init failed: %s", esp_err_to_name(err));
        return;
    }

    err = iot_sensor_handler_register(g_imu_sensor_handle, sensor_event_handler, &g_imu_handler_instance);
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "IMU handler register failed: %s", esp_err_to_name(err));
        return;
    }

    err = iot_sensor_start(g_imu_sensor_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TABFORGE_TAG, "IMU start failed: %s", esp_err_to_name(err));
        return;
    }

    g_imu_online = true;
    append_event("imu_started");
}
#else
static void init_imu(void)
{
    g_imu_online = false;
    ESP_LOGW(TABFORGE_TAG, "IMU not available in this BSP build");
}
#endif

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void init_network_stack(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void init_wifi_station(void)
{
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        g_wifi_state = WIFI_STATE_ERROR;
        g_wifi_last_error = err;
        ESP_LOGW(TABFORGE_TAG, "wifi init failed: %s", esp_err_to_name(err));
        return;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        g_wifi_state = WIFI_STATE_ERROR;
        g_wifi_last_error = err;
        ESP_LOGW(TABFORGE_TAG, "wifi start failed: %s", esp_err_to_name(err));
        return;
    }

    g_wifi_started = true;
    g_wifi_state = WIFI_STATE_READY;
    append_event("wifi_station_started");
    ESP_LOGI(TABFORGE_TAG, "wifi station ready");

    if (g_wifi_credentials.auto_connect && g_wifi_credentials.configured) {
        xTaskCreate(wifi_connect_task, "tabforge-wifi-autoconnect", 6144, NULL, 5, NULL);
    } else if (!g_wifi_boot_scan_started) {
        g_wifi_boot_scan_started = true;
        xTaskCreate(wifi_scan_task, "tabforge-wifi-boot-scan", 6144, NULL, 5, NULL);
    }
}

static void log_boot_status(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TABFORGE_TAG, "TabForge Cyberdeck %s booting", TABFORGE_VERSION);
    ESP_LOGI(TABFORGE_TAG, "ESP-IDF app version: %s", desc ? desc->version : "unknown");
    ESP_LOGI(TABFORGE_TAG, "Running partition: %s", running ? running->label : "unknown");
    ESP_LOGI(TABFORGE_TAG, "Config path: %s", TABFORGE_CONFIG_PATH);
    ESP_LOGI(TABFORGE_TAG, "Event log: %s", TABFORGE_EVENT_LOG_PATH);
}

static void init_power_management(void)
{
    esp_io_expander_handle_t io_expander = bsp_io_expander_init();
    if (io_expander == NULL) {
        g_ext_power_ready = false;
        g_ext_power_error = ESP_ERR_NOT_SUPPORTED;
    } else {
        esp_err_t err = set_expander_output(io_expander, TABFORGE_EXT5V_EN, false);
        g_ext_power_error = err;
        g_ext_power_ready = false;
        if (err == ESP_OK) {
            ESP_LOGI(TABFORGE_TAG, "PA/Grove add-on rail left off");
        } else {
            ESP_LOGW(TABFORGE_TAG, "PA/Grove add-on rail control failed: %s", esp_err_to_name(err));
        }
    }

    esp_io_expander_handle_t power_expander = bsp_io_expander1_init();
    if (power_expander == NULL) {
        g_usb_power_ready = false;
        g_usb_power_error = ESP_ERR_NOT_SUPPORTED;
        g_charge_enable_ready = false;
        g_charge_enable_error = ESP_FAIL;
        ESP_LOGW(TABFORGE_TAG, "power IO expander unavailable");
        return;
    }

    esp_err_t usb_err = set_expander_output(power_expander, BSP_USB_EN, false);
    g_usb_power_error = usb_err;
    g_usb_power_ready = false;
    if (usb_err == ESP_OK) {
        ESP_LOGI(TABFORGE_TAG, "USB-A add-on rail left off");
    } else {
        ESP_LOGW(TABFORGE_TAG, "USB-A add-on rail control failed: %s", esp_err_to_name(usb_err));
    }

    esp_err_t wifi_power_err = set_expander_output(power_expander, BSP_WIFI_EN, true);
    if (wifi_power_err == ESP_OK) {
        ESP_LOGI(TABFORGE_TAG, "Wi-Fi co-processor rail enabled");
        vTaskDelay(pdMS_TO_TICKS(250));
    } else {
        ESP_LOGW(TABFORGE_TAG, "Wi-Fi co-processor rail enable failed: %s", esp_err_to_name(wifi_power_err));
    }

    esp_err_t charge_err = set_expander_output(power_expander, TABFORGE_CHARGE_ENABLE, true);
    g_charge_enable_error = charge_err;
    g_charge_enable_ready = (charge_err == ESP_OK);
    if (g_charge_enable_ready) {
        ESP_LOGI(TABFORGE_TAG, "battery charge enable asserted");
    } else {
        ESP_LOGW(TABFORGE_TAG, "battery charge enable failed: %s", esp_err_to_name(charge_err));
    }
}

static void init_ir_probe(void)
{
    gpio_config_t rx_config = {
        .pin_bit_mask = (1ULL << (uint32_t)TABFORGE_IR_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&rx_config);
    if (err != ESP_OK) {
        g_ir_probe_ready = false;
        ESP_LOGW(TABFORGE_TAG, "IR RX probe config failed: %s", esp_err_to_name(err));
        return;
    }

    gpio_config_t tx_config = {
        .pin_bit_mask = (1ULL << (uint32_t)TABFORGE_IR_TX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&tx_config);
    if (err != ESP_OK) {
        g_ir_probe_ready = false;
        ESP_LOGW(TABFORGE_TAG, "IR TX idle config failed: %s", esp_err_to_name(err));
        return;
    }

    gpio_set_level(TABFORGE_IR_TX_GPIO, 0);
    g_ir_level = gpio_get_level(TABFORGE_IR_RX_GPIO);
    g_ir_probe_ready = true;
    append_event("ir_probe_started");
    ESP_LOGI(TABFORGE_TAG, "IR Grove probe ready on RX GPIO%u TX GPIO%u",
             (unsigned)TABFORGE_IR_RX_GPIO,
             (unsigned)TABFORGE_IR_TX_GPIO);
}

static void init_grove_uart_probe(void)
{
    const uart_config_t uart_config = {
        .baud_rate = GROVE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(GROVE_UART_NUM, GROVE_UART_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        g_grove_uart_error = err;
        ESP_LOGW(TABFORGE_TAG, "Grove UART RX driver install failed: %s", esp_err_to_name(err));
        return;
    }

    err = uart_param_config(GROVE_UART_NUM, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(GROVE_UART_NUM,
                           (int)TABFORGE_GROVE_TX_GPIO,
                           (int)TABFORGE_GROVE_RX_GPIO,
                           UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }

    g_grove_uart_error = err;
    g_grove_uart_ready = (err == ESP_OK);
    if (g_grove_uart_ready) {
        append_event("grove_uart_rx_started");
        ESP_LOGI(TABFORGE_TAG, "Grove UART ready on RX GPIO%u TX GPIO%u at %u baud",
                 (unsigned)TABFORGE_GROVE_RX_GPIO,
                 (unsigned)TABFORGE_GROVE_TX_GPIO,
                 (unsigned)GROVE_UART_BAUDRATE);
    } else {
        ESP_LOGW(TABFORGE_TAG, "Grove UART RX config failed: %s", esp_err_to_name(err));
    }
}

static bool usb_cdc_rx_cb(const uint8_t *data, size_t data_len, void *user_arg)
{
    (void)data;
    (void)user_arg;

    g_usb_rx_packets++;
    g_usb_rx_bytes += (uint32_t)data_len;
    g_usb_last_rx_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    g_mesh_module_ready = true;
    return true;
}

static void usb_cdc_event_cb(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
    case CDC_ACM_HOST_ERROR:
        g_usb_last_error = event->data.error;
        g_usb_state = USB_STATE_ERROR;
        ESP_LOGW(TABFORGE_TAG, "USB CDC error: %s", esp_err_to_name(g_usb_last_error));
        break;
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        g_usb_cdc_disconnected = true;
        g_usb_disconnect_count++;
        g_usb_state = USB_STATE_DISCONNECTED;
        ESP_LOGI(TABFORGE_TAG, "USB CDC device disconnected");
        break;
    case CDC_ACM_HOST_SERIAL_STATE:
        ESP_LOGI(TABFORGE_TAG, "USB CDC serial state: 0x%04x", event->data.serial_state.val);
        break;
    case CDC_ACM_HOST_NETWORK_CONNECTION:
    default:
        break;
    }
}

static void mic_monitor_task(void *arg)
{
    (void)arg;

    esp_codec_dev_handle_t mic = bsp_audio_codec_microphone_init();
    if (mic == NULL) {
        g_mic_state = MIC_STATE_INIT_FAILED;
        ESP_LOGW(TABFORGE_TAG, "mic monitor failed: microphone codec init returned NULL");
        vTaskDelete(NULL);
        return;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .sample_rate = MIC_SAMPLE_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };

    int codec_err = esp_codec_dev_set_in_gain(mic, 36.0f);
    if (codec_err != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TABFORGE_TAG, "mic gain set failed: %d", codec_err);
    }

    codec_err = esp_codec_dev_open(mic, &sample_info);
    if (codec_err != ESP_CODEC_DEV_OK) {
        g_mic_state = MIC_STATE_OPEN_FAILED;
        ESP_LOGW(TABFORGE_TAG, "mic open failed: %d", codec_err);
        vTaskDelete(NULL);
        return;
    }

    int16_t samples[MIC_SAMPLES] = {0};
    g_mic_state = MIC_STATE_ONLINE;
    append_event("mic_monitor_started");

    while (true) {
        codec_err = esp_codec_dev_read(mic, samples, sizeof(samples));
        if (codec_err != ESP_CODEC_DEV_OK) {
            g_mic_state = MIC_STATE_READ_FAILED;
            ESP_LOGW(TABFORGE_TAG, "mic read failed: %d", codec_err);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        int64_t sum = 0;
        int peak = 0;
        for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
            int sample = samples[i];
            if (sample < 0) {
                sample = -sample;
            }
            sum += sample;
            if (sample > peak) {
                peak = sample;
            }
        }

        g_mic_average = (int)(sum / (sizeof(samples) / sizeof(samples[0])));
        g_mic_peak = peak;
        g_mic_reads++;
        g_mic_state = MIC_STATE_ONLINE;
        vTaskDelay(pdMS_TO_TICKS(800));
    }
}

static void battery_monitor_task(void *arg)
{
    (void)arg;

    while (true) {
        bool was_online = g_battery_online;
        esp_err_t err = read_battery_sample();
        if (err == ESP_OK) {
            if (!was_online) {
                ESP_LOGI(TABFORGE_TAG, "battery monitor recovered: %dmV %d%%",
                         g_battery_mv,
                         g_battery_percent);
            }
        } else if (was_online) {
            ESP_LOGW(TABFORGE_TAG, "battery monitor lost: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(TABFORGE_BATTERY_POLL_MS));
    }
}

static void poll_grove_uart(void)
{
    if (!g_grove_uart_ready) {
        return;
    }

    uint8_t uart_data[GROVE_UART_READ_CHUNK];
    int rx_len = uart_read_bytes(GROVE_UART_NUM, uart_data, sizeof(uart_data), 0);
    if (rx_len > 0) {
        g_grove_rx_packets++;
        g_grove_rx_bytes += (uint32_t)rx_len;
        g_grove_last_rx_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        g_mesh_module_ready = true;
        ESP_LOGI(TABFORGE_TAG, "C6L Grove UART RX %d bytes", rx_len);
    }
}

static void poll_ir_probe(void)
{
    if (!g_ir_probe_ready) {
        return;
    }

    int level = gpio_get_level(TABFORGE_IR_RX_GPIO);
    if (g_ir_level >= 0 && level != g_ir_level) {
        g_ir_edges++;
    }
    g_ir_level = level;
}

static void usb_cdc_task(void *arg)
{
    (void)arg;

    while (!g_usb_power_ready) {
        g_usb_state = USB_STATE_OFF;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    g_usb_state = USB_STATE_POWERED;
    esp_err_t err = bsp_usb_host_start(BSP_USB_HOST_POWER_MODE_USB_DEV, true);
    if (err != ESP_OK) {
        g_usb_last_error = err;
        g_usb_state = USB_STATE_ERROR;
        ESP_LOGW(TABFORGE_TAG, "USB host start failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    g_usb_host_ready = true;
    append_event("usb_host_started");
    ESP_LOGI(TABFORGE_TAG, "USB host power/library started");

    err = cdc_acm_host_install(NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        g_usb_last_error = err;
        g_usb_state = USB_STATE_ERROR;
        ESP_LOGW(TABFORGE_TAG, "USB CDC host install failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    const cdc_acm_line_coding_t line_coding = {
        .dwDTERate = USB_CDC_BAUDRATE,
        .bCharFormat = 0,
        .bParityType = 0,
        .bDataBits = 8,
    };

    while (true) {
        if (!g_usb_power_ready) {
            if (g_usb_cdc_handle != NULL) {
                cdc_acm_host_close(g_usb_cdc_handle);
                g_usb_cdc_handle = NULL;
            }
            g_usb_state = USB_STATE_OFF;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (g_usb_cdc_handle != NULL) {
            if (g_usb_cdc_disconnected) {
                cdc_acm_host_close(g_usb_cdc_handle);
                g_usb_cdc_handle = NULL;
                g_usb_cdc_disconnected = false;
                g_usb_state = USB_STATE_SCANNING;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        g_usb_state = USB_STATE_SCANNING;
        cdc_acm_host_device_config_t dev_config = {
            .connection_timeout_ms = USB_CDC_SCAN_TIMEOUT_MS,
            .out_buffer_size = 512,
            .in_buffer_size = 512,
            .event_cb = usb_cdc_event_cb,
            .data_cb = usb_cdc_rx_cb,
            .user_arg = NULL,
        };

        err = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID, 0, &dev_config, &g_usb_cdc_handle);
        if (err == ESP_OK) {
            g_usb_state = USB_STATE_OPEN;
            g_usb_last_error = ESP_OK;
            g_usb_open_count++;
            cdc_acm_host_line_coding_set(g_usb_cdc_handle, &line_coding);
            cdc_acm_host_set_control_line_state(g_usb_cdc_handle, true, true);
            cdc_acm_host_desc_print(g_usb_cdc_handle);
            append_event("usb_cdc_open");
            ESP_LOGI(TABFORGE_TAG, "USB CDC device opened at %u baud", (unsigned)USB_CDC_BAUDRATE);
        } else {
            g_usb_last_error = err;
            if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_TIMEOUT) {
                ESP_LOGW(TABFORGE_TAG, "USB CDC open failed: %s", esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(1200));
        }
    }
}

static void accessory_auto_power_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(15000));
    if (!g_ext_power_ready && !g_usb_power_ready) {
        set_accessory_power(true);
        start_accessory_probe_tasks();
        append_event("accessories_auto_power_on");
        update_activity_from_task("Accessories On", "Add-on power enabled after boot.");
    }
    vTaskDelete(NULL);
}

static void stats_task(void *arg)
{
    (void)arg;

    while (true) {
        if (bsp_display_lock(1000)) {
            check_screen_power_locked();
            refresh_live_stats_locked();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(STATS_REFRESH_MS));
    }
}

static void rotation_task(void *arg)
{
    (void)arg;

    while (true) {
        poll_grove_uart();
        poll_ir_probe();
        if (bsp_display_lock(100)) {
            maybe_apply_auto_rotation_locked();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(ROTATION_CHECK_MS));
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;

    while (true) {
        char battery_status[32];
        format_battery_status(battery_status, sizeof(battery_status), false);
        ESP_LOGI(TABFORGE_TAG, "heartbeat=%lu mode=%s sd=%s imu=%s mic=%s usb=%s grove=%s ir=%s screen=%s timeout=%s sleeps=%lu charge=%s bat=%s wifi=%s rotation=%s",
                 (unsigned long)g_heartbeat_count++,
                 active_mode_name(),
                 g_sd_ready ? (g_sd_recovered ? "recovered" : "ready") : "missing",
                 g_imu_ready ? "ready" : "pending",
                 mic_state_text(),
                 usb_state_text(),
                 g_grove_uart_ready ? "rx-ready" : "off",
                 g_ir_probe_ready ? "ready" : "off",
                 g_screen_sleeping ? "sleep" : "awake",
                 screen_timeout_label(),
                 (unsigned long)g_screen_sleep_count,
                 g_charge_enable_ready ? "on" : "err",
                 battery_status,
                 wifi_state_text(),
                 rotation_name(g_rotation));
        append_event("heartbeat");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    init_nvs();
    load_screen_settings();
    init_network_stack();
    log_boot_status();
    init_sdcard();
    load_runtime_config();
    init_power_management();
    init_battery_monitor();

    g_display = bsp_display_start();
    bsp_display_backlight_on();

    if (bsp_display_lock(0)) {
        bsp_display_rotate(g_display, g_rotation);
        lv_indev_t *touch = bsp_display_get_input_dev();
        if (touch != NULL) {
            lv_indev_add_event_cb(touch, input_activity_event_cb, LV_EVENT_ALL, NULL);
        }
        lv_obj_t *screen = lv_display_get_screen_active(g_display);
        lv_obj_clean(screen);
        build_dashboard(screen);
        bsp_display_unlock();
    }

    init_wifi_station();
    init_imu();

    xTaskCreate(battery_monitor_task, "tabforge-battery", 4096, NULL, 4, NULL);
    xTaskCreate(heartbeat_task, "tabforge-heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(mic_monitor_task, "tabforge-mic-monitor", 6144, NULL, 4, NULL);
    xTaskCreate(accessory_auto_power_task, "tabforge-accessory-power", 3072, NULL, 4, NULL);
    xTaskCreate(usb_cdc_task, "tabforge-usb-cdc", 8192, NULL, 6, NULL);
    xTaskCreate(stats_task, "tabforge-stats", 6144, NULL, 4, NULL);
    xTaskCreate(rotation_task, "tabforge-rotate", 4096, NULL, 4, NULL);
}

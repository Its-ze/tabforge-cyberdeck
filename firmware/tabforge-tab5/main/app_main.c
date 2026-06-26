#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "usb/cdc_acm_host.h"

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

#define IMU_SAMPLE_PERIOD_MS 250
#define STATS_REFRESH_MS 1000
#define MIC_SAMPLE_RATE 16000
#define MIC_SAMPLES 512
#define USB_CDC_BAUDRATE 115200
#define USB_CDC_SCAN_TIMEOUT_MS 2500
#define TABFORGE_EXT5V_EN IO_EXPANDER_PIN_NUM_2
#define TABFORGE_GROVE_TX_GPIO GPIO_NUM_53
#define TABFORGE_GROVE_RX_GPIO GPIO_NUM_54
#define TABFORGE_IR_TX_GPIO TABFORGE_GROVE_TX_GPIO
#define TABFORGE_IR_RX_GPIO TABFORGE_GROVE_RX_GPIO
#define GROVE_UART_NUM UART_NUM_1
#define GROVE_UART_BAUDRATE 115200
#define GROVE_UART_RX_BUF_SIZE 2048
#define GROVE_UART_READ_CHUNK 128

typedef enum {
    FEATURE_ACTIVE,
    FEATURE_PLANNED,
} feature_state_t;

typedef struct {
    const char *code;
    const char *name;
    const char *summary;
    const char *metric;
    const char *event_name;
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
    NAV_PAGE_APPS,
    NAV_PAGE_SETTINGS,
    NAV_PAGE_UPDATE,
} nav_page_t;

typedef struct {
    lv_obj_t *button;
    lv_obj_t *icon;
    lv_obj_t *label;
} nav_button_refs_t;

typedef struct {
    lv_obj_t *status_label;
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
    nav_button_refs_t nav_apps;
    nav_button_refs_t nav_settings;
    nav_button_refs_t nav_mode;
    nav_button_refs_t nav_auto;
    nav_button_refs_t nav_update;
    lv_obj_t *heap_bar;
    lv_obj_t *psram_bar;
    lv_obj_t *mic_bar;
} ui_refs_t;

static const feature_tile_t g_tiles[] = {
    {LV_SYMBOL_ENVELOPE, "Meshtastic", "C6L serial dashboard, node status, channel text, direct sends.", "USB CDC", "tile_meshtastic", FEATURE_ACTIVE, 0x43d17a},
    {LV_SYMBOL_SHUFFLE, "MeshCore", "Switchable command profile for MeshCore console work.", "Profile", "tile_meshcore", FEATURE_ACTIVE, 0x61d5f0},
    {LV_SYMBOL_KEYBOARD, "T-Deck Link", "Companion status bridge for the LilyGO T-Deck/Z-Deck flow.", "Bridge", "tile_tdeck", FEATURE_ACTIVE, 0xf0bf4f},
    {LV_SYMBOL_EYE_OPEN, "IR Lab", "Learn, label, replay, and store IR macros on SD.", "38 kHz", "tile_ir", FEATURE_ACTIVE, 0xff7a66},
    {LV_SYMBOL_AUDIO, "Mic Deck", "Live level meter now, push-to-record WAV flow next.", "Live", "tile_mic", FEATURE_ACTIVE, 0xb982ff},
    {LV_SYMBOL_USB, "USB Bay", "Host-side CDC, keyboard, mouse, and storage workbench.", "Host", "tile_usb", FEATURE_ACTIVE, 0x70a7ff},
    {LV_SYMBOL_SD_CARD, "SD Field Log", "Runtime config, event journal, audio, IR, and backups.", "SD", "tile_sd", FEATURE_ACTIVE, 0x77dd88},
    {LV_SYMBOL_DOWNLOAD, "Update Center", "GitHub manifest, SHA256 package checks, and confirm button.", "Stable", "tile_update", FEATURE_ACTIVE, 0xffc857},
};

static ui_refs_t g_ui;
static lv_display_t *g_display;
static lv_disp_rotation_t g_rotation = LV_DISPLAY_ROTATION_90;
static lv_disp_rotation_t g_pending_rotation = LV_DISPLAY_ROTATION_90;
static uint8_t g_pending_rotation_count;
static bool g_auto_rotate = true;
static bool g_sd_ready;
static bool g_meshcore_mode;
static nav_page_t g_nav_page = NAV_PAGE_APPS;
static uint32_t g_heartbeat_count;
static bool g_ext_power_ready;
static esp_err_t g_ext_power_error = ESP_OK;
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

#if BSP_CAPS_IMU
static sensor_handle_t g_imu_sensor_handle;
#endif
static bool g_imu_online;
static bool g_imu_ready;
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
    return g_meshcore_mode ? "MESHCORE" : "MESHTASTIC";
}

static const char *active_mode_detail(void)
{
    return g_meshcore_mode ? "MeshCore console profile armed" : "Meshtastic C6L profile armed";
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
            "  \"ui\": { \"home\": \"tablet\", \"autoRotate\": true, \"liveStats\": true },\n"
            "  \"devices\": {\n"
            "    \"unit-c6l\": { \"enabled\": true, \"preferredTransport\": \"usb-cdc\", \"mode\": \"meshtastic\" },\n"
            "    \"tdeck\": { \"enabled\": true, \"preferredTransport\": \"usb-cdc\", \"mode\": \"zdeck-meshtastic\" },\n"
            "    \"unit-ir\": { \"enabled\": true, \"preferredTransport\": \"grove-port-b\" }\n"
            "  },\n"
            "  \"ota\": { \"manifestUrl\": \"%s\", \"requireButtonConfirm\": true }\n"
            "}\n",
            TABFORGE_MANIFEST_URL);
    fclose(f);
    ESP_LOGI(TABFORGE_TAG, "created default config at %s", TABFORGE_CONFIG_PATH);
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

static void init_sdcard(void)
{
    esp_err_t err = bsp_sdcard_mount();
    g_sd_ready = (err == ESP_OK);

    if (!g_sd_ready) {
        ESP_LOGW(TABFORGE_TAG, "SD mount failed: %s", esp_err_to_name(err));
        return;
    }

    ensure_dir(TABFORGE_SD_ROOT "/tabforge");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/audio");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/backups");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/ir");
    ensure_dir(TABFORGE_SD_ROOT "/tabforge/logs");
    write_default_config_if_missing();
    append_event("boot");

    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (card != NULL) {
        ESP_LOGI(TABFORGE_TAG, "SD mounted: %s", card->cid.name);
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
    lv_obj_set_size(button, width, 44);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x202b31), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color_hex(0x3c525a), 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, label_text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, width - 10);
    lv_obj_set_style_text_color(label, color_hex(0xf1f7f3), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
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
    lv_obj_set_size(button, width, 68);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x151b20), 0);
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
    lv_obj_t *card = make_panel(parent, width, height, 0x13181c, accent);
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

    uint32_t bg = active ? 0x20312b : 0x151b20;
    uint32_t text = active ? 0xf1f7f3 : 0x93a6ad;
    uint32_t icon = active ? accent : 0x6ee7a2;

    lv_obj_set_style_bg_color(nav->button, color_hex(bg), 0);
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
    style_nav_button(&g_ui.nav_apps, g_nav_page == NAV_PAGE_APPS, 0x6ee7a2);
    style_nav_button(&g_ui.nav_settings, g_nav_page == NAV_PAGE_SETTINGS, 0x61d5f0);
    style_nav_button(&g_ui.nav_mode, g_meshcore_mode, 0xf0bf4f);
    style_nav_button(&g_ui.nav_auto, g_auto_rotate, 0x77dd88);
    style_nav_button(&g_ui.nav_update, g_nav_page == NAV_PAGE_UPDATE, 0xffc857);
}

static void show_nav_page(nav_page_t page)
{
    g_nav_page = page;
    set_obj_hidden(g_ui.page_apps, page != NAV_PAGE_APPS);
    set_obj_hidden(g_ui.page_settings, page != NAV_PAGE_SETTINGS);
    set_obj_hidden(g_ui.page_update, page != NAV_PAGE_UPDATE);

    switch (page) {
    case NAV_PAGE_SETTINGS:
        set_activity("Settings", "Touch-ready controls: mesh profile, rotation, add-on power, SD, audio, USB, and OTA status.");
        break;
    case NAV_PAGE_UPDATE:
        set_activity("Update Center", "GitHub Pages manifest selected. OTA staging waits for explicit button confirmation.");
        break;
    case NAV_PAGE_APPS:
    default:
        set_activity("Apps", "Launcher grid active: Meshtastic, MeshCore, T-Deck, IR, mic, USB, SD, and updates.");
        break;
    }

    refresh_nav_styles();
}

static void refresh_mode_widgets(void)
{
    if (g_ui.status_label != NULL) {
        lv_label_set_text_fmt(g_ui.status_label,
                              "TabForge %s   %s",
                              TABFORGE_VERSION,
                              active_mode_name());
    }

    if (g_ui.mode_label != NULL) {
        lv_label_set_text_fmt(g_ui.mode_label, "%s mode", active_mode_name());
    }

    if (g_ui.mode_detail_label != NULL) {
        lv_label_set_text(g_ui.mode_detail_label, active_mode_detail());
    }

    if (g_ui.dock_mode_label != NULL) {
        lv_label_set_text(g_ui.dock_mode_label, g_meshcore_mode ? "Mesh" : "Core");
    }

    refresh_nav_styles();
}

static void refresh_rotation_widgets(void)
{
    if (g_ui.rotation_label != NULL) {
        lv_label_set_text_fmt(g_ui.rotation_label,
                              "%s   %d deg   auto %s",
                              rotation_name(g_rotation),
                              rotation_degrees(g_rotation),
                              g_auto_rotate ? "on" : "off");
    }

    if (g_ui.dock_auto_label != NULL) {
        lv_label_set_text(g_ui.dock_auto_label, g_auto_rotate ? "Auto" : "Locked");
    }

    refresh_nav_styles();
}

static void refresh_live_stats_locked(void)
{
    char uptime[32];
    format_uptime(uptime, sizeof(uptime));
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
        lv_label_set_text(g_ui.sd_label, g_sd_ready ? "mounted" : "mount fail");
        lv_obj_set_style_text_color(g_ui.sd_label, g_sd_ready ? color_hex(0x6ee7a2) : color_hex(0xff7a66), 0);
    }

    if (g_ui.addon_label != NULL) {
        const char *ext = g_ext_power_ready ? "5V on" : "5V err";
        const char *grove = g_grove_uart_ready ? "Grove RX" : (g_ir_probe_ready ? "IR ready" : "Grove wait");
        lv_label_set_text_fmt(g_ui.addon_label,
                              "%s | %s",
                              ext,
                              grove);
        lv_obj_set_style_text_color(g_ui.addon_label,
                                    (g_ext_power_ready && g_usb_host_ready) ? color_hex(0x6ee7a2) : color_hex(0xffc857),
                                    0);
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
        if (g_usb_state == USB_STATE_OPEN) {
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
        if (g_ir_probe_ready) {
            uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
            uint64_t age_s = g_grove_last_rx_ms > 0 ? (now_ms - g_grove_last_rx_ms) / 1000ULL : 0;
            lv_label_set_text_fmt(g_ui.ir_label,
                                  "IR L%d E%lu | UART %luB/%lu %llus",
                                  g_ir_level,
                                  (unsigned long)g_ir_edges,
                                  (unsigned long)g_grove_rx_bytes,
                                  (unsigned long)g_grove_rx_packets,
                                  (unsigned long long)age_s);
        } else if (g_ext_power_error != ESP_OK) {
            lv_label_set_text_fmt(g_ui.ir_label, "power %s", esp_err_to_name(g_ext_power_error));
        } else if (g_grove_uart_error != ESP_OK) {
            lv_label_set_text_fmt(g_ui.ir_label, "uart %s", esp_err_to_name(g_grove_uart_error));
        } else {
            lv_label_set_text(g_ui.ir_label, "probe pending");
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
        lv_label_set_text_fmt(g_ui.heart_label, "beat %lu | usb drop %lu",
                              (unsigned long)g_heartbeat_count,
                              (unsigned long)g_usb_disconnect_count);
    }

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

    set_activity(tile->name, tile->summary);
    append_event(tile->event_name);
    ESP_LOGI(TABFORGE_TAG, "tile selected: %s", tile->name);
}

static void add_app_tile(lv_obj_t *parent, const feature_tile_t *tile, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x0f151a), 0);
    lv_obj_set_style_pad_all(button, 8, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(button, 7, 0);
    lv_obj_add_event_cb(button, feature_tile_event_cb, LV_EVENT_CLICKED, (void *)tile);

    lv_obj_t *icon_box = lv_obj_create(button);
    lv_obj_set_size(icon_box, 58, 58);
    lv_obj_set_style_radius(icon_box, 8, 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_border_color(icon_box, color_hex(tile->accent), 0);
    lv_obj_set_style_bg_color(icon_box, color_hex(0x182127), 0);
    lv_obj_set_style_bg_opa(icon_box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(icon_box, 0, 0);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(icon_box);
    lv_label_set_text(icon, tile->code);
    lv_obj_set_style_text_color(icon, color_hex(tile->accent), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
    lv_obj_center(icon);

    lv_obj_t *name = make_text(button, tile->name, 0xf1f7f3, width - 12);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *metric = make_text(button, tile->metric, tile->state == FEATURE_ACTIVE ? 0x6ee7a2 : 0xffc857, width - 12);
    lv_obj_set_style_text_align(metric, LV_TEXT_ALIGN_CENTER, 0);
}

static void mode_button_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
        return;
    }

    g_meshcore_mode = !g_meshcore_mode;
    refresh_mode_widgets();
    set_activity(active_mode_name(), active_mode_detail());
    append_event(g_meshcore_mode ? "mode_meshcore" : "mode_meshtastic");
    ESP_LOGI(TABFORGE_TAG, "mode switched to %s", active_mode_name());
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

    show_nav_page(NAV_PAGE_APPS);
    append_event("apps_button_selected");
    ESP_LOGI(TABFORGE_TAG, "apps button selected");
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
    lv_obj_set_style_bg_color(top, color_hex(0x090d10), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(top, 10, 0);
    lv_obj_set_style_pad_ver(top, 6, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_ui.status_label = make_text(top, "", 0x6ee7a2, (width * 48) / 100);
    g_ui.rotation_label = make_text(top, "", 0x93a6ad, (width * 48) / 100);
    lv_obj_set_style_text_align(g_ui.rotation_label, LV_TEXT_ALIGN_RIGHT, 0);
}

static void build_home_header(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *header = make_panel(parent, width, height, 0x10161a, 0x293943);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(header, 8, 0);

    g_ui.mode_label = make_text(header, "", 0xf1f7f3, width - 28);
    lv_obj_set_style_text_align(g_ui.mode_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.mode_detail_label = make_text(header, "", 0x93a6ad, width - 28);
    lv_obj_set_style_text_align(g_ui.mode_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.activity_title_label = make_text(header, "Home", 0x6ee7a2, width - 28);
    lv_obj_set_style_text_align(g_ui.activity_title_label, LV_TEXT_ALIGN_CENTER, 0);
    g_ui.activity_detail_label = make_text(header, "Tap an app icon or bottom control.", 0xf1f7f3, width - 28);
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

    add_stat_card(stat_grid, "Uptime", &g_ui.uptime_label, NULL, card_w, card_h, 0x43d17a);
    add_stat_card(stat_grid, "SD", &g_ui.sd_label, NULL, card_w, card_h, 0xff7a66);
    add_stat_card(stat_grid, "USB", &g_ui.usb_label, NULL, card_w, card_h, 0x70a7ff);
    add_stat_card(stat_grid, "Mic", &g_ui.mic_label, &g_ui.mic_bar, card_w, card_h, 0xb982ff);
    add_stat_card(stat_grid, "Add-ons", &g_ui.addon_label, NULL, card_w, card_h, 0xffc857);
    add_stat_card(stat_grid, "Pulse", &g_ui.heart_label, NULL, card_w, card_h, 0x77dd88);
}

static void build_app_grid(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, width, height);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_column(grid, 14, 0);

    lv_coord_t columns = landscape ? 4 : 4;
    lv_coord_t tile_w = (width - ((columns - 1) * 14)) / columns;
    lv_coord_t tile_h = (height - 18) / 2;
    if (tile_h > 144) {
        tile_h = 144;
    }
    if (tile_h < 112) {
        tile_h = 112;
    }
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        add_app_tile(grid, &g_tiles[i], tile_w, tile_h);
    }
}

static void build_body(lv_obj_t *screen, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, width, height);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body, 12, 0);

    lv_coord_t header_h = landscape ? 136 : 210;
    lv_coord_t quick_h = landscape ? 76 : 128;
    lv_coord_t apps_h = height - header_h - quick_h - 24;
    if (apps_h < 240) {
        apps_h = 240;
    }

    build_home_header(body, width, header_h);
    build_quick_status(body, width, quick_h, landscape);
    build_app_grid(body, width, apps_h, landscape);
}

static void build_dock(lv_obj_t *screen, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *dock = make_panel(screen, width, height, 0x11171c, 0x26333b);
    lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(dock, 8, 0);

    lv_coord_t button_w = (width - 72) / 5;
    make_nav_button(dock, button_w, LV_SYMBOL_LIST, "Apps", apps_button_event_cb);
    make_nav_button(dock, button_w, LV_SYMBOL_SETTINGS, "Settings", settings_button_event_cb);
    g_ui.dock_mode_label = make_nav_button(dock, button_w, LV_SYMBOL_SHUFFLE, "Core", mode_button_event_cb);
    g_ui.dock_auto_label = make_nav_button(dock, button_w, LV_SYMBOL_REFRESH, "Auto", auto_rotate_button_event_cb);
    make_nav_button(dock, button_w, LV_SYMBOL_DOWNLOAD, "OTA", update_button_event_cb);
}

static void build_dashboard(lv_obj_t *screen)
{
    memset(&g_ui, 0, sizeof(g_ui));

    bool landscape = is_landscape_rotation(g_rotation);
    lv_coord_t screen_w = (lv_coord_t)deck_width();
    lv_coord_t screen_h = (lv_coord_t)deck_height();
    lv_coord_t pad = landscape ? 16 : 12;
    lv_coord_t gap = landscape ? 12 : 10;
    lv_coord_t top_h = landscape ? 46 : 48;
    lv_coord_t dock_h = landscape ? 78 : 82;
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
    if (abs_x < 0.65f && abs_y < 0.65f) {
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

    if (g_pending_rotation_count < 3) {
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

    err = iot_sensor_handler_register(g_imu_sensor_handle, sensor_event_handler, NULL);
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

static void init_expansion_power(void)
{
    esp_io_expander_handle_t io_expander = bsp_io_expander_init();
    if (io_expander == NULL) {
        g_ext_power_ready = false;
        g_ext_power_error = ESP_FAIL;
        ESP_LOGW(TABFORGE_TAG, "expansion 5V enable failed: IO expander unavailable");
        return;
    }

    esp_err_t err = ESP_OK;
    err |= esp_io_expander_set_dir(io_expander, TABFORGE_EXT5V_EN, IO_EXPANDER_OUTPUT);
    err |= esp_io_expander_set_level(io_expander, TABFORGE_EXT5V_EN, true);
    err |= esp_io_expander_set_output_mode(io_expander, TABFORGE_EXT5V_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
    g_ext_power_error = err;
    g_ext_power_ready = (err == ESP_OK);

    if (g_ext_power_ready) {
        ESP_LOGI(TABFORGE_TAG, "expansion 5V enabled for M5-Bus/Grove/GPIO_EXT");
        append_event("expansion_5v_enabled");
    } else {
        ESP_LOGW(TABFORGE_TAG, "expansion 5V enable failed: %s", esp_err_to_name(err));
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
                           UART_PIN_NO_CHANGE,
                           (int)TABFORGE_GROVE_RX_GPIO,
                           UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }

    g_grove_uart_error = err;
    g_grove_uart_ready = (err == ESP_OK);
    if (g_grove_uart_ready) {
        append_event("grove_uart_rx_started");
        ESP_LOGI(TABFORGE_TAG, "Grove UART passive RX ready on GPIO%u at %u baud",
                 (unsigned)TABFORGE_GROVE_RX_GPIO,
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

static void ir_probe_task(void *arg)
{
    (void)arg;

    init_ir_probe();
    init_grove_uart_probe();
    uint8_t uart_data[GROVE_UART_READ_CHUNK];
    while (true) {
        if (g_grove_uart_ready) {
            int rx_len = uart_read_bytes(GROVE_UART_NUM, uart_data, sizeof(uart_data), 0);
            if (rx_len > 0) {
                g_grove_rx_packets++;
                g_grove_rx_bytes += (uint32_t)rx_len;
                g_grove_last_rx_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
            }
        }

        if (g_ir_probe_ready) {
            int level = gpio_get_level(TABFORGE_IR_RX_GPIO);
            if (g_ir_level >= 0 && level != g_ir_level) {
                g_ir_edges++;
            }
            g_ir_level = level;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void usb_cdc_task(void *arg)
{
    (void)arg;

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

static void stats_task(void *arg)
{
    (void)arg;

    while (true) {
        if (bsp_display_lock(1000)) {
            maybe_apply_auto_rotation_locked();
            refresh_live_stats_locked();
            bsp_display_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(STATS_REFRESH_MS));
    }
}

static void heartbeat_task(void *arg)
{
    (void)arg;

    while (true) {
        ESP_LOGI(TABFORGE_TAG, "heartbeat=%lu mode=%s sd=%s imu=%s mic=%s usb=%s usb_rx=%lu grove_rx=%lu/%lu ir_edges=%lu rotation=%s",
                 (unsigned long)g_heartbeat_count++,
                 active_mode_name(),
                 g_sd_ready ? "ready" : "missing",
                 g_imu_ready ? "ready" : "pending",
                 mic_state_text(),
                 usb_state_text(),
                 (unsigned long)g_usb_rx_bytes,
                 (unsigned long)g_grove_rx_bytes,
                 (unsigned long)g_grove_rx_packets,
                 (unsigned long)g_ir_edges,
                 rotation_name(g_rotation));
        append_event("heartbeat");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void app_main(void)
{
    init_nvs();
    init_network_stack();
    log_boot_status();
    init_sdcard();
    init_expansion_power();

    g_display = bsp_display_start();
    bsp_display_backlight_on();

    if (bsp_display_lock(0)) {
        bsp_display_rotate(g_display, g_rotation);
        lv_obj_t *screen = lv_display_get_screen_active(g_display);
        lv_obj_clean(screen);
        build_dashboard(screen);
        bsp_display_unlock();
    }

    init_imu();

    xTaskCreate(usb_cdc_task, "tabforge-usb-cdc", 8192, NULL, 6, NULL);
    xTaskCreate(ir_probe_task, "tabforge-ir-probe", 4096, NULL, 4, NULL);
    xTaskCreate(heartbeat_task, "tabforge-heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(mic_monitor_task, "tabforge-mic-monitor", 6144, NULL, 4, NULL);
    xTaskCreate(stats_task, "tabforge-stats", 6144, NULL, 4, NULL);
}

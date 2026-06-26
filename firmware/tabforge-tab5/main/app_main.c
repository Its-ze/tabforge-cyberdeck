#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
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

#ifndef TABFORGE_VERSION
#define TABFORGE_VERSION "0.1.0"
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

typedef struct {
    lv_obj_t *status_label;
    lv_obj_t *sd_label;
    lv_obj_t *mode_label;
    lv_obj_t *mode_detail_label;
    lv_obj_t *uptime_label;
    lv_obj_t *heap_label;
    lv_obj_t *psram_label;
    lv_obj_t *mic_label;
    lv_obj_t *tilt_label;
    lv_obj_t *heart_label;
    lv_obj_t *gyro_label;
    lv_obj_t *rotation_label;
    lv_obj_t *activity_title_label;
    lv_obj_t *activity_detail_label;
    lv_obj_t *dock_mode_label;
    lv_obj_t *dock_auto_label;
    lv_obj_t *heap_bar;
    lv_obj_t *psram_bar;
    lv_obj_t *mic_bar;
} ui_refs_t;

static const feature_tile_t g_tiles[] = {
    {"MSG", "Meshtastic", "C6L serial dashboard, node status, channel text, direct sends.", "USB CDC", "tile_meshtastic", FEATURE_ACTIVE, 0x43d17a},
    {"CORE", "MeshCore", "Switchable command profile for MeshCore console work.", "Profile", "tile_meshcore", FEATURE_ACTIVE, 0x61d5f0},
    {"TDEK", "T-Deck Link", "Companion status bridge for the LilyGO T-Deck/Z-Deck flow.", "Bridge", "tile_tdeck", FEATURE_ACTIVE, 0xf0bf4f},
    {"IR", "IR Lab", "Learn, label, replay, and store IR macros on SD.", "38 kHz", "tile_ir", FEATURE_ACTIVE, 0xff7a66},
    {"MIC", "Mic Deck", "Live level meter now, push-to-record WAV flow next.", "Live", "tile_mic", FEATURE_ACTIVE, 0xb982ff},
    {"USB", "USB Bay", "Host-side CDC, keyboard, mouse, and storage workbench.", "Host", "tile_usb", FEATURE_ACTIVE, 0x70a7ff},
    {"LOG", "SD Field Log", "Runtime config, event journal, audio, IR, and backups.", "SD", "tile_sd", FEATURE_ACTIVE, 0x77dd88},
    {"OTA", "Update Center", "GitHub manifest, SHA256 package checks, and confirm button.", "Stable", "tile_update", FEATURE_ACTIVE, 0xffc857},
};

static ui_refs_t g_ui;
static lv_display_t *g_display;
static lv_disp_rotation_t g_rotation = LV_DISPLAY_ROTATION_90;
static lv_disp_rotation_t g_pending_rotation = LV_DISPLAY_ROTATION_90;
static uint8_t g_pending_rotation_count;
static bool g_auto_rotate = true;
static bool g_sd_ready;
static bool g_meshcore_mode;
static uint32_t g_heartbeat_count;

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
    lv_obj_set_style_text_color(label, color_hex(0xf1f7f3), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
    return label;
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

static void refresh_mode_widgets(void)
{
    if (g_ui.status_label != NULL) {
        lv_label_set_text_fmt(g_ui.status_label,
                              "TabForge %s | %s | OTA stable",
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
        lv_label_set_text(g_ui.dock_mode_label, g_meshcore_mode ? "Use Meshtastic" : "Use MeshCore");
    }
}

static void refresh_rotation_widgets(void)
{
    if (g_ui.rotation_label != NULL) {
        lv_label_set_text_fmt(g_ui.rotation_label,
                              "%s | %d deg | auto %s",
                              rotation_name(g_rotation),
                              rotation_degrees(g_rotation),
                              g_auto_rotate ? "on" : "off");
    }

    if (g_ui.dock_auto_label != NULL) {
        lv_label_set_text(g_ui.dock_auto_label, g_auto_rotate ? "Auto Rotate On" : "Auto Rotate Off");
    }
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
        lv_label_set_text(g_ui.sd_label, g_sd_ready ? "SD mounted | config/log/audio/IR folders ready" : "SD missing or mount failed");
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
        lv_label_set_text_fmt(g_ui.heart_label, "beat %lu | mic %lu",
                              (unsigned long)g_heartbeat_count,
                              (unsigned long)g_mic_reads);
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
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, color_hex(tile->accent), 0);
    lv_obj_set_style_bg_color(button, color_hex(0x141b21), 0);
    lv_obj_set_style_pad_all(button, 10, 0);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(button, 5, 0);
    lv_obj_add_event_cb(button, feature_tile_event_cb, LV_EVENT_CLICKED, (void *)tile);

    make_text(button, tile->code, tile->accent, width - 20);
    make_text(button, tile->name, 0xf1f7f3, width - 20);
    make_text(button, tile->summary, 0x93a6ad, width - 20);
    make_text(button, tile->metric, tile->state == FEATURE_ACTIVE ? 0x6ee7a2 : 0xffc857, width - 20);
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

    set_activity("Update Center", "Ready to fetch GitHub Pages manifest and stage OTA with button confirm.");
    append_event("update_center_selected");
    ESP_LOGI(TABFORGE_TAG, "update center selected: %s", TABFORGE_MANIFEST_URL);
}

static void build_top_bar(lv_obj_t *screen, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *top = make_panel(screen, width, height, 0x11171c, 0x26333b);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_coord_t title_w = landscape ? (width * 50) / 100 : (width * 47) / 100;
    lv_obj_t *title_box = lv_obj_create(top);
    lv_obj_remove_style_all(title_box);
    lv_obj_set_size(title_box, title_w, height - 20);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(title_box, 5, 0);
    make_text(title_box, "TabForge", 0xf1f7f3, title_w);
    make_text(title_box, "M5Stack Tab5 field deck", 0x93a6ad, title_w);

    lv_coord_t status_w = width - title_w - 34;
    lv_obj_t *status_box = lv_obj_create(top);
    lv_obj_remove_style_all(status_box);
    lv_obj_set_size(status_box, status_w, height - 20);
    lv_obj_set_flex_flow(status_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(status_box, 5, 0);
    g_ui.status_label = make_text(status_box, "", 0x6ee7a2, status_w);
    g_ui.sd_label = make_text(status_box, "", 0x93a6ad, status_w);
    g_ui.rotation_label = make_text(status_box, "", 0x61d5f0, status_w);
}

static void build_live_surface(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *live = make_panel(parent, width, height, 0x10161a, 0x2c3d45);
    lv_obj_set_flex_flow(live, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(live, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(live, 10, 0);

    g_ui.mode_label = make_text(live, "", 0xf1f7f3, width - 24);
    g_ui.mode_detail_label = make_text(live, "", 0x93a6ad, width - 24);

    lv_obj_t *stat_grid = lv_obj_create(live);
    lv_obj_remove_style_all(stat_grid);
    lv_obj_set_width(stat_grid, width - 24);
    lv_obj_set_height(stat_grid, landscape ? 220 : 206);
    lv_obj_set_flex_flow(stat_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(stat_grid, 10, 0);
    lv_obj_set_style_pad_column(stat_grid, 10, 0);

    lv_coord_t card_w = landscape ? (width - 46) / 2 : (width - 56) / 4;
    if (card_w < 140) {
        card_w = (width - 46) / 2;
    }
    lv_coord_t card_h = landscape ? 96 : 92;

    add_stat_card(stat_grid, "Uptime", &g_ui.uptime_label, NULL, card_w, card_h, 0x43d17a);
    add_stat_card(stat_grid, "Heap", &g_ui.heap_label, &g_ui.heap_bar, card_w, card_h, 0x61d5f0);
    add_stat_card(stat_grid, "PSRAM", &g_ui.psram_label, &g_ui.psram_bar, card_w, card_h, 0xb982ff);
    add_stat_card(stat_grid, "Mic", &g_ui.mic_label, &g_ui.mic_bar, card_w, card_h, 0xff7a66);
    add_stat_card(stat_grid, "Tilt", &g_ui.tilt_label, NULL, card_w, card_h, 0xf0bf4f);
    add_stat_card(stat_grid, "Pulse", &g_ui.heart_label, NULL, card_w, card_h, 0x77dd88);

    lv_obj_t *activity = make_panel(live, width - 24, landscape ? 126 : 112, 0x141b21, 0x344955);
    lv_obj_set_flex_flow(activity, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(activity, 6, 0);
    g_ui.activity_title_label = make_text(activity, "Home", 0xf1f7f3, width - 50);
    g_ui.activity_detail_label = make_text(activity, "Tap an app tile or dock control to drive the deck.", 0x93a6ad, width - 50);
    g_ui.gyro_label = make_text(activity, "gyro pending", 0x61d5f0, width - 50);
}

static void build_app_grid(lv_obj_t *parent, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, width, height);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 10, 0);
    lv_obj_set_style_pad_column(grid, 10, 0);

    lv_coord_t tile_w = landscape ? (width - 20) / 3 : (width - 10) / 2;
    lv_coord_t tile_h = landscape ? 126 : 118;
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        add_app_tile(grid, &g_tiles[i], tile_w, tile_h);
    }
}

static void build_body(lv_obj_t *screen, lv_coord_t width, lv_coord_t height, bool landscape)
{
    lv_obj_t *body = lv_obj_create(screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, width, height);
    lv_obj_set_flex_flow(body, landscape ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_style_pad_column(body, 12, 0);

    if (landscape) {
        lv_coord_t live_w = 400;
        lv_coord_t grid_w = width - live_w - 12;
        build_live_surface(body, live_w, height, true);
        build_app_grid(body, grid_w, height, true);
    } else {
        lv_coord_t live_h = 414;
        build_live_surface(body, width, live_h, false);
        build_app_grid(body, width, height - live_h - 12, false);
    }
}

static void build_dock(lv_obj_t *screen, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *dock = make_panel(screen, width, height, 0x11171c, 0x26333b);
    lv_obj_set_flex_flow(dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dock, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_coord_t button_w = (width - 52) / 3;
    g_ui.dock_mode_label = make_button(dock, button_w, "Use MeshCore", mode_button_event_cb);
    g_ui.dock_auto_label = make_button(dock, button_w, "Auto Rotate On", auto_rotate_button_event_cb);
    make_button(dock, button_w, "Update Center", update_button_event_cb);
}

static void build_dashboard(lv_obj_t *screen)
{
    memset(&g_ui, 0, sizeof(g_ui));

    bool landscape = is_landscape_rotation(g_rotation);
    lv_coord_t screen_w = (lv_coord_t)deck_width();
    lv_coord_t screen_h = (lv_coord_t)deck_height();
    lv_coord_t pad = landscape ? 16 : 12;
    lv_coord_t gap = landscape ? 12 : 10;
    lv_coord_t top_h = landscape ? 82 : 92;
    lv_coord_t dock_h = landscape ? 70 : 68;
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
        *target = ax > 0.0f ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90;
    } else {
        *target = ay > 0.0f ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0;
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
        ESP_LOGI(TABFORGE_TAG, "heartbeat=%lu mode=%s sd=%s imu=%s mic=%s rotation=%s",
                 (unsigned long)g_heartbeat_count++,
                 active_mode_name(),
                 g_sd_ready ? "ready" : "missing",
                 g_imu_ready ? "ready" : "pending",
                 mic_state_text(),
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

    xTaskCreate(heartbeat_task, "tabforge-heartbeat", 4096, NULL, 5, NULL);
    xTaskCreate(mic_monitor_task, "tabforge-mic-monitor", 6144, NULL, 4, NULL);
    xTaskCreate(stats_task, "tabforge-stats", 6144, NULL, 4, NULL);
}

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "bsp/esp-bsp.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
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

typedef enum {
    FEATURE_ACTIVE,
    FEATURE_PLANNED,
} feature_state_t;

typedef struct {
    const char *name;
    const char *summary;
    feature_state_t state;
} feature_tile_t;

static const feature_tile_t g_tiles[] = {
    {"Meshtastic", "C6L USB serial, nodes, channel text, direct sends", FEATURE_ACTIVE},
    {"MeshCore", "Command console and OTA AP upload assist", FEATURE_PLANNED},
    {"T-Deck Link", "Z-Deck status and safe companion actions", FEATURE_ACTIVE},
    {"IR Lab", "Learn and replay 38 kHz IR macros", FEATURE_ACTIVE},
    {"Mic Deck", "Push-to-record WAV clips and sound meter", FEATURE_ACTIVE},
    {"USB Host", "CDC serial, keyboard, mouse, and storage bay", FEATURE_ACTIVE},
    {"SD Field Log", "Runtime config, redacted events, backups", FEATURE_ACTIVE},
    {"Update Center", "GitHub manifest, SHA256 check, OTA apply", FEATURE_ACTIVE},
};

static lv_obj_t *g_status_label;
static lv_obj_t *g_sd_label;
static bool g_sd_ready;

static const char *state_text(feature_state_t state)
{
    return state == FEATURE_ACTIVE ? "ACTIVE" : "PLANNED";
}

static lv_color_t color_hex(uint32_t value)
{
    return lv_color_hex(value);
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

    fprintf(f, "{\"event\":\"%s\",\"version\":\"%s\"}\n", event, TABFORGE_VERSION);
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

static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, color_hex(0x344144), 0);
    lv_obj_set_style_bg_color(panel, color_hex(0x171d1d), 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    return panel;
}

static void add_label(lv_obj_t *parent, const char *text, uint32_t color, int font_large)
{
    (void)font_large;

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, color_hex(color), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
}

static void add_tile(lv_obj_t *parent, const feature_tile_t *tile)
{
    lv_obj_t *box = make_panel(parent, 292, 116);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    add_label(box, tile->name, 0xedf6f0, 0);

    lv_obj_t *summary = lv_label_create(box);
    lv_label_set_text(summary, tile->summary);
    lv_label_set_long_mode(summary, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(summary, 260);
    lv_obj_set_style_text_color(summary, color_hex(0x9ab0aa), 0);
    lv_obj_set_style_text_font(summary, &lv_font_montserrat_14, 0);

    lv_obj_t *status = lv_label_create(box);
    lv_label_set_text(status, state_text(tile->state));
    lv_obj_set_style_text_color(status, tile->state == FEATURE_ACTIVE ? color_hex(0x5ce08a) : color_hex(0xe9ca60), 0);
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
}

static void build_dashboard(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, color_hex(0x101313), 0);
    lv_obj_set_style_pad_all(screen, 18, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *top = make_panel(screen, 1244, 104);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_box = lv_obj_create(top);
    lv_obj_remove_style_all(title_box);
    lv_obj_set_size(title_box, 650, 74);
    lv_obj_set_flex_flow(title_box, LV_FLEX_FLOW_COLUMN);
    add_label(title_box, "TabForge Cyberdeck", 0xedf6f0, 1);
    add_label(title_box, "M5Stack Tab5 control deck for C6L, T-Deck, IR, mic, USB host, and OTA", 0x9ab0aa, 0);

    lv_obj_t *status_box = lv_obj_create(top);
    lv_obj_remove_style_all(status_box);
    lv_obj_set_size(status_box, 420, 74);
    lv_obj_set_flex_flow(status_box, LV_FLEX_FLOW_COLUMN);
    g_status_label = lv_label_create(status_box);
    lv_label_set_text_fmt(g_status_label, "Version %s | Mode MESHTASTIC | USB CDC READY", TABFORGE_VERSION);
    lv_obj_set_style_text_color(g_status_label, color_hex(0x5ce08a), 0);
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_14, 0);
    g_sd_label = lv_label_create(status_box);
    lv_label_set_text(g_sd_label, g_sd_ready ? "SD mounted: /tabforge config/log folders ready" : "SD not mounted: insert card and reboot");
    lv_obj_set_style_text_color(g_sd_label, g_sd_ready ? color_hex(0x6ed6e8) : color_hex(0xff786d), 0);
    lv_obj_set_style_text_font(g_sd_label, &lv_font_montserrat_14, 0);

    lv_obj_t *grid = lv_obj_create(screen);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, 1244, 500);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);

    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        add_tile(grid, &g_tiles[i]);
    }

    lv_obj_t *footer = make_panel(screen, 1244, 66);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    add_label(footer, "Next: attach C6L over USB host, then wire Meshtastic serial and MeshCore mode profiles.", 0x9ab0aa, 0);
    add_label(footer, "Update manifest: its-ze.github.io/tabforge-cyberdeck", 0x6ed6e8, 0);
}

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

static void heartbeat_task(void *arg)
{
    uint32_t beat = 0;
    while (true) {
        ESP_LOGI(TABFORGE_TAG, "heartbeat=%lu mode=meshtastic sd=%s",
                 (unsigned long)beat++,
                 g_sd_ready ? "ready" : "missing");
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

    bsp_display_start();
    bsp_display_backlight_on();

    bsp_display_lock(0);
    lv_obj_t *screen = lv_screen_active();
    build_dashboard(screen);
    bsp_display_unlock();

    xTaskCreate(heartbeat_task, "tabforge-heartbeat", 4096, NULL, 5, NULL);
}

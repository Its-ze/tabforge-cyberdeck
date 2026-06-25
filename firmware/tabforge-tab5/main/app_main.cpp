#include <array>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#ifndef TABFORGE_VERSION
#define TABFORGE_VERSION "0.1.0"
#endif

namespace {

constexpr const char *TAG = "TabForge";
constexpr const char *kRuntimeConfigPath = "/sdcard/tabforge/config.json";
constexpr const char *kEventLogPath = "/sdcard/tabforge/logs/events.jsonl";
constexpr const char *kManifestUrl = "https://its-ze.github.io/tabforge-cyberdeck/manifest.json";

enum class FeatureState {
    Active,
    Planned
};

struct Feature {
    const char *id;
    const char *name;
    const char *group;
    FeatureState state;
};

constexpr std::array<Feature, 15> kFeatures = {{
    {"mesh-dashboard", "Mesh Dashboard", "mesh", FeatureState::Active},
    {"mode-switch", "Mode Switch", "mesh", FeatureState::Active},
    {"meshtastic-console", "Meshtastic Console", "mesh", FeatureState::Active},
    {"meshcore-console", "MeshCore Console", "mesh", FeatureState::Planned},
    {"c6l-manager", "C6L Manager", "devices", FeatureState::Active},
    {"tdeck-link", "T-Deck Link", "devices", FeatureState::Active},
    {"ir-lab", "IR Lab", "tools", FeatureState::Active},
    {"mic-deck", "Mic Deck", "tools", FeatureState::Active},
    {"field-notes", "Field Notes", "data", FeatureState::Active},
    {"usb-host", "USB Host", "ports", FeatureState::Active},
    {"rs485-console", "RS485 Console", "ports", FeatureState::Planned},
    {"camera-watch", "Camera Watch", "sensors", FeatureState::Planned},
    {"imu-gestures", "IMU Gestures", "sensors", FeatureState::Planned},
    {"update-center", "Update Center", "system", FeatureState::Active},
    {"power-center", "Power Center", "system", FeatureState::Planned},
}};

const char *state_name(FeatureState state)
{
    return state == FeatureState::Active ? "active" : "planned";
}

void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void init_network_stack()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

void log_boot_status()
{
    const esp_app_desc_t *desc = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(TAG, "TabForge Cyberdeck %s booting", TABFORGE_VERSION);
    ESP_LOGI(TAG, "ESP-IDF app version: %s", desc ? desc->version : "unknown");
    ESP_LOGI(TAG, "Running partition: %s", running ? running->label : "unknown");
    ESP_LOGI(TAG, "Runtime config: %s", kRuntimeConfigPath);
    ESP_LOGI(TAG, "Event log: %s", kEventLogPath);
    ESP_LOGI(TAG, "Update manifest: %s", kManifestUrl);
}

void register_features()
{
    for (const auto &feature : kFeatures) {
        ESP_LOGI(TAG, "feature id=%s name=\"%s\" group=%s state=%s",
                 feature.id,
                 feature.name,
                 feature.group,
                 state_name(feature.state));
    }
}

void start_mesh_services()
{
    ESP_LOGI(TAG, "mesh services staged: Meshtastic driver first, MeshCore driver selectable");
    ESP_LOGI(TAG, "transport priority: USB CDC, BLE, Wi-Fi profile");
}

void start_device_services()
{
    ESP_LOGI(TAG, "device services staged: C6L manager, T-Deck link, IR Lab, USB host");
}

void start_audio_services()
{
    ESP_LOGI(TAG, "audio services staged: push-to-record WAV, sound meter, voice macro metadata");
}

void start_update_center()
{
    ESP_LOGI(TAG, "update center staged: manifest check, SHA256 verify, on-screen confirm, OTA apply");
}

void heartbeat_task(void *)
{
    uint32_t beat = 0;
    while (true) {
        ESP_LOGI(TAG, "heartbeat=%lu mode=meshtastic update=idle", static_cast<unsigned long>(beat++));
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

} // namespace

extern "C" void app_main(void)
{
    init_nvs();
    init_network_stack();
    log_boot_status();
    register_features();
    start_mesh_services();
    start_device_services();
    start_audio_services();
    start_update_center();

    xTaskCreate(heartbeat_task, "tabforge-heartbeat", 4096, nullptr, 5, nullptr);
}

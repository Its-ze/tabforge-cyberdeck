#include "tabforge_layer1_device.h"

#include <stdio.h>
#include <string.h>

void tabforge_device_data_init(tabforge_device_data_t *data,
                               const char *device_id,
                               const char *firmware_version)
{
    if (data == NULL) {
        return;
    }
    memset(data, 0, sizeof(*data));
    data->device_id = device_id != NULL ? device_id : "tabforge-tab5";
    data->board = "M5Stack Tab5 / ESP32-P4";
    data->firmware_version = firmware_version != NULL ? firmware_version : "unknown";
    data->battery_percent = -1;
    data->health = TABFORGE_DEVICE_UNKNOWN;
}

void tabforge_device_data_update_health(tabforge_device_data_t *data)
{
    if (data == NULL) {
        return;
    }
    if (!data->display_ready || !data->touch_ready) {
        data->health = TABFORGE_DEVICE_OFFLINE;
    } else if (!data->sd_ready || !data->wifi_ready || !data->mic_ready || !data->usb_host_ready) {
        data->health = TABFORGE_DEVICE_DEGRADED;
    } else {
        data->health = TABFORGE_DEVICE_READY;
    }
    data->sequence++;
}

const char *tabforge_device_health_text(tabforge_device_health_t health)
{
    switch (health) {
    case TABFORGE_DEVICE_READY:
        return "ready";
    case TABFORGE_DEVICE_DEGRADED:
        return "degraded";
    case TABFORGE_DEVICE_OFFLINE:
        return "offline";
    case TABFORGE_DEVICE_UNKNOWN:
    default:
        return "unknown";
    }
}

size_t tabforge_device_data_format_json(const tabforge_device_data_t *data,
                                        char *buffer,
                                        size_t buffer_size)
{
    if (data == NULL || buffer == NULL || buffer_size == 0U) {
        return 0U;
    }
    int written = snprintf(
        buffer,
        buffer_size,
        "{\"schema\":\"tabforge.device.v1\",\"deviceId\":\"%s\","
        "\"board\":\"%s\",\"firmware\":\"%s\",\"sequence\":%lu,"
        "\"uptimeMs\":%lu,\"health\":\"%s\",\"battery\":{\"percent\":%d,"
        "\"millivolts\":%lu,\"charging\":%s},\"capabilities\":{\"display\":%s,"
        "\"touch\":%s,\"sd\":%s,\"wifi\":%s,\"mic\":%s,\"usbHost\":%s}}",
        data->device_id,
        data->board,
        data->firmware_version,
        (unsigned long)data->sequence,
        (unsigned long)data->uptime_ms,
        tabforge_device_health_text(data->health),
        data->battery_percent,
        (unsigned long)data->battery_mv,
        data->charging ? "true" : "false",
        data->display_ready ? "true" : "false",
        data->touch_ready ? "true" : "false",
        data->sd_ready ? "true" : "false",
        data->wifi_ready ? "true" : "false",
        data->mic_ready ? "true" : "false",
        data->usb_host_ready ? "true" : "false");
    if (written < 0) {
        buffer[0] = '\0';
        return 0U;
    }
    return (size_t)written < buffer_size ? (size_t)written : buffer_size - 1U;
}

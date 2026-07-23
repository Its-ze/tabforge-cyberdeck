#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TABFORGE_DEVICE_UNKNOWN = 0,
    TABFORGE_DEVICE_READY,
    TABFORGE_DEVICE_DEGRADED,
    TABFORGE_DEVICE_OFFLINE,
} tabforge_device_health_t;

typedef struct {
    const char *device_id;
    const char *board;
    const char *firmware_version;
    uint32_t sequence;
    uint32_t uptime_ms;
    int battery_percent;
    uint32_t battery_mv;
    bool charging;
    bool display_ready;
    bool touch_ready;
    bool sd_ready;
    bool wifi_ready;
    bool mic_ready;
    bool usb_host_ready;
    tabforge_device_health_t health;
} tabforge_device_data_t;

void tabforge_device_data_init(tabforge_device_data_t *data,
                               const char *device_id,
                               const char *firmware_version);
void tabforge_device_data_update_health(tabforge_device_data_t *data);
const char *tabforge_device_health_text(tabforge_device_health_t health);
size_t tabforge_device_data_format_json(const tabforge_device_data_t *data,
                                        char *buffer,
                                        size_t buffer_size);

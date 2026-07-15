#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef bool (*tabforge_ble_rx_cb_t)(const char *line);
typedef void (*tabforge_ble_link_cb_t)(bool connected);

esp_err_t tabforge_ble_start(tabforge_ble_rx_cb_t rx_cb, tabforge_ble_link_cb_t link_cb);
void tabforge_ble_set_response(const char *response);
bool tabforge_ble_connected(void);
uint32_t tabforge_ble_rx_count(void);
uint32_t tabforge_ble_connection_count(void);

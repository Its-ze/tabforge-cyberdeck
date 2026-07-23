#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TABFORGE_THEME_DARK = 0,
    TABFORGE_THEME_LIGHT,
    TABFORGE_THEME_HIGH_CONTRAST,
} tabforge_theme_t;

typedef enum {
    TABFORGE_SAVE_IDLE = 0,
    TABFORGE_SAVE_IN_PROGRESS,
    TABFORGE_SAVE_SUCCEEDED,
    TABFORGE_SAVE_FAILED,
} tabforge_save_state_t;

typedef struct {
    const char *route;
    char clock_text[24];
    char battery_text[32];
    tabforge_theme_t theme;
    tabforge_save_state_t save_state;
    char save_message[80];
    uint32_t save_visible_until_ms;
} tabforge_shell_t;

void tabforge_shell_init(tabforge_shell_t *shell);
void tabforge_shell_set_route(tabforge_shell_t *shell, const char *route);
void tabforge_shell_set_clock(tabforge_shell_t *shell, const char *clock_text);
void tabforge_shell_set_battery(tabforge_shell_t *shell,
                                int percent,
                                uint32_t millivolts,
                                bool charging);
void tabforge_shell_save_begin(tabforge_shell_t *shell,
                               const char *subject,
                               uint32_t now_ms);
void tabforge_shell_save_finish(tabforge_shell_t *shell,
                                const char *subject,
                                bool success,
                                uint32_t now_ms);
bool tabforge_shell_save_feedback_visible(const tabforge_shell_t *shell,
                                          uint32_t now_ms);
const char *tabforge_shell_save_state_text(tabforge_save_state_t state);

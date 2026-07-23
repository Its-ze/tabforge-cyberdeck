#include "tabforge_layer2_shell.h"

#include <stdio.h>
#include <string.h>

#define TABFORGE_SAVE_FEEDBACK_MS 4500U

static void copy_text(char *destination, size_t destination_size, const char *text)
{
    if (destination == NULL || destination_size == 0U) {
        return;
    }
    snprintf(destination, destination_size, "%s", text != NULL ? text : "");
}

void tabforge_shell_init(tabforge_shell_t *shell)
{
    if (shell == NULL) {
        return;
    }
    memset(shell, 0, sizeof(*shell));
    shell->route = "home";
    shell->theme = TABFORGE_THEME_DARK;
    shell->save_state = TABFORGE_SAVE_IDLE;
    copy_text(shell->clock_text, sizeof(shell->clock_text), "--:--");
    copy_text(shell->battery_text, sizeof(shell->battery_text), "battery pending");
}

void tabforge_shell_set_route(tabforge_shell_t *shell, const char *route)
{
    if (shell != NULL) {
        shell->route = route != NULL ? route : "home";
    }
}

void tabforge_shell_set_clock(tabforge_shell_t *shell, const char *clock_text)
{
    if (shell != NULL) {
        copy_text(shell->clock_text, sizeof(shell->clock_text), clock_text);
    }
}

void tabforge_shell_set_battery(tabforge_shell_t *shell,
                                int percent,
                                uint32_t millivolts,
                                bool charging)
{
    if (shell == NULL) {
        return;
    }
    if (percent >= 0) {
        snprintf(shell->battery_text,
                 sizeof(shell->battery_text),
                 "%d%% | %.2fV%s",
                 percent,
                 (double)millivolts / 1000.0,
                 charging ? " | charging" : "");
    } else {
        snprintf(shell->battery_text,
                 sizeof(shell->battery_text),
                 "%.2fV%s",
                 (double)millivolts / 1000.0,
                 charging ? " | charging" : "");
    }
}

void tabforge_shell_save_begin(tabforge_shell_t *shell,
                               const char *subject,
                               uint32_t now_ms)
{
    if (shell == NULL) {
        return;
    }
    shell->save_state = TABFORGE_SAVE_IN_PROGRESS;
    snprintf(shell->save_message,
             sizeof(shell->save_message),
             "Saving %s...",
             subject != NULL ? subject : "changes");
    shell->save_visible_until_ms = now_ms + TABFORGE_SAVE_FEEDBACK_MS;
}

void tabforge_shell_save_finish(tabforge_shell_t *shell,
                                const char *subject,
                                bool success,
                                uint32_t now_ms)
{
    if (shell == NULL) {
        return;
    }
    shell->save_state = success ? TABFORGE_SAVE_SUCCEEDED : TABFORGE_SAVE_FAILED;
    snprintf(shell->save_message,
             sizeof(shell->save_message),
             success ? "%s saved." : "%s was not saved.",
             subject != NULL ? subject : "Changes");
    shell->save_visible_until_ms = now_ms + TABFORGE_SAVE_FEEDBACK_MS;
}

bool tabforge_shell_save_feedback_visible(const tabforge_shell_t *shell,
                                          uint32_t now_ms)
{
    if (shell == NULL || shell->save_state == TABFORGE_SAVE_IDLE) {
        return false;
    }
    return (int32_t)(shell->save_visible_until_ms - now_ms) > 0;
}

const char *tabforge_shell_save_state_text(tabforge_save_state_t state)
{
    switch (state) {
    case TABFORGE_SAVE_IN_PROGRESS:
        return "saving";
    case TABFORGE_SAVE_SUCCEEDED:
        return "saved";
    case TABFORGE_SAVE_FAILED:
        return "failed";
    case TABFORGE_SAVE_IDLE:
    default:
        return "idle";
    }
}

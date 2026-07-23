#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    TABFORGE_APP_BOUNDARY_PLACEHOLDER = 0,
    TABFORGE_APP_BOUNDARY_LOCAL,
    TABFORGE_APP_BOUNDARY_API_REQUIRED,
    TABFORGE_APP_BOUNDARY_FUTURE_MODULE,
} tabforge_app_boundary_t;

typedef struct {
    const char *id;
    const char *name;
    const char *summary;
    tabforge_app_boundary_t boundary;
    bool enabled;
} tabforge_app_descriptor_t;

typedef struct {
    const char *id;
    const char *title;
    const char *status;
    bool configured;
} tabforge_dashboard_card_t;

typedef struct {
    bool api_configured;
    bool authenticated;
    bool online;
    uint32_t pending_count;
    uint32_t completed_count;
    char status[96];
} tabforge_scribe_tasks_t;

typedef enum {
    TABFORGE_VOICE_IDLE = 0,
    TABFORGE_VOICE_RECORDING,
    TABFORGE_VOICE_SAVED_LOCAL,
    TABFORGE_VOICE_QUEUED,
    TABFORGE_VOICE_UPLOADED,
    TABFORGE_VOICE_FAILED,
} tabforge_voice_state_t;

typedef struct {
    tabforge_voice_state_t state;
    char local_path[112];
    char ingest_id[48];
    char status[96];
} tabforge_voice_memo_t;

typedef struct {
    bool https_required;
    bool sha256_required;
    bool signature_verification_available;
    bool rollback_enabled;
    bool rollback_available;
    bool pending_boot_validation;
    char status[112];
} tabforge_ota_security_t;

typedef struct {
    bool receive_only;
    bool usb_driver_available;
    bool flight_views_planned;
    char status[96];
} tabforge_sdr_hook_t;

typedef struct {
    tabforge_scribe_tasks_t tasks;
    tabforge_voice_memo_t voice;
    tabforge_ota_security_t ota;
    tabforge_sdr_hook_t sdr;
} tabforge_apps_t;

void tabforge_apps_init(tabforge_apps_t *apps);
const tabforge_app_descriptor_t *tabforge_apps_catalog(size_t *count);
const tabforge_dashboard_card_t *tabforge_dashboard_placeholders(size_t *count);
const char *tabforge_app_boundary_text(tabforge_app_boundary_t boundary);
void tabforge_tasks_set_status(tabforge_apps_t *apps,
                               bool api_configured,
                               bool authenticated,
                               bool online,
                               uint32_t pending_count,
                               uint32_t completed_count);
void tabforge_voice_set_local(tabforge_apps_t *apps, const char *local_path);
void tabforge_voice_set_ingest_result(tabforge_apps_t *apps,
                                      bool success,
                                      const char *ingest_id);
const char *tabforge_voice_state_text(tabforge_voice_state_t state);
void tabforge_ota_set_boot_status(tabforge_apps_t *apps,
                                  bool rollback_enabled,
                                  bool rollback_available,
                                  bool pending_boot_validation,
                                  bool signature_verification_available);

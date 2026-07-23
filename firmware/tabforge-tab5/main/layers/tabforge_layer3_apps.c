#include "tabforge_layer3_apps.h"

#include <stdio.h>
#include <string.h>

static const tabforge_app_descriptor_t k_apps[] = {
    {"dashboard", "Dashboard", "Reserved cards for later Unraid and personal-program readouts.", TABFORGE_APP_BOUNDARY_PLACEHOLDER, true},
    {"scribe-tasks", "Scribe Tasks", "Authenticated task read/write through the shared Scribe API.", TABFORGE_APP_BOUNDARY_API_REQUIRED, true},
    {"voice-memo", "Voice Memo", "Capture locally, then submit through normal Scribe ingest and transcription.", TABFORGE_APP_BOUNDARY_API_REQUIRED, true},
    {"update", "Update", "HTTPS, digest verification, progress, and boot rollback status.", TABFORGE_APP_BOUNDARY_LOCAL, true},
    {"sdr", "Field SDR", "Receive-only USB module boundary; tuning and flight views remain future work.", TABFORGE_APP_BOUNDARY_FUTURE_MODULE, true},
};

static const tabforge_dashboard_card_t k_dashboard_cards[] = {
    {"unraid", "Unraid", "Waiting for authenticated dashboard API requirements.", false},
    {"personal-programs", "Personal Programs", "Cards will be defined later; no guessed controls.", false},
    {"scribe", "Scribe Suite", "Reserved for connector and queue health.", false},
};

void tabforge_apps_init(tabforge_apps_t *apps)
{
    if (apps == NULL) {
        return;
    }
    memset(apps, 0, sizeof(*apps));
    snprintf(apps->tasks.status, sizeof(apps->tasks.status), "Scribe API not configured.");
    apps->voice.state = TABFORGE_VOICE_IDLE;
    snprintf(apps->voice.status, sizeof(apps->voice.status), "Ready for a local recording.");
    apps->ota.https_required = true;
    apps->ota.sha256_required = true;
    snprintf(apps->ota.status, sizeof(apps->ota.status), "Boot rollback status not checked.");
    apps->sdr.receive_only = true;
    apps->sdr.flight_views_planned = true;
    snprintf(apps->sdr.status, sizeof(apps->sdr.status), "Receive-only USB driver boundary; no transmitter path.");
}

const tabforge_app_descriptor_t *tabforge_apps_catalog(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(k_apps) / sizeof(k_apps[0]);
    }
    return k_apps;
}

const tabforge_dashboard_card_t *tabforge_dashboard_placeholders(size_t *count)
{
    if (count != NULL) {
        *count = sizeof(k_dashboard_cards) / sizeof(k_dashboard_cards[0]);
    }
    return k_dashboard_cards;
}

const char *tabforge_app_boundary_text(tabforge_app_boundary_t boundary)
{
    switch (boundary) {
    case TABFORGE_APP_BOUNDARY_LOCAL:
        return "local";
    case TABFORGE_APP_BOUNDARY_API_REQUIRED:
        return "api-required";
    case TABFORGE_APP_BOUNDARY_FUTURE_MODULE:
        return "future-module";
    case TABFORGE_APP_BOUNDARY_PLACEHOLDER:
    default:
        return "placeholder";
    }
}

void tabforge_tasks_set_status(tabforge_apps_t *apps,
                               bool api_configured,
                               bool authenticated,
                               bool online,
                               uint32_t pending_count,
                               uint32_t completed_count)
{
    if (apps == NULL) {
        return;
    }
    apps->tasks.api_configured = api_configured;
    apps->tasks.authenticated = authenticated;
    apps->tasks.online = online;
    apps->tasks.pending_count = pending_count;
    apps->tasks.completed_count = completed_count;
    if (!api_configured) {
        snprintf(apps->tasks.status, sizeof(apps->tasks.status), "Waiting for the shared Scribe API.");
    } else if (!authenticated) {
        snprintf(apps->tasks.status, sizeof(apps->tasks.status), "Scribe identity is not authenticated.");
    } else if (!online) {
        snprintf(apps->tasks.status, sizeof(apps->tasks.status), "Offline; changes must remain queued locally.");
    } else {
        snprintf(apps->tasks.status,
                 sizeof(apps->tasks.status),
                 "%lu pending | %lu completed",
                 (unsigned long)pending_count,
                 (unsigned long)completed_count);
    }
}

void tabforge_voice_set_local(tabforge_apps_t *apps, const char *local_path)
{
    if (apps == NULL) {
        return;
    }
    apps->voice.state = TABFORGE_VOICE_SAVED_LOCAL;
    snprintf(apps->voice.local_path,
             sizeof(apps->voice.local_path),
             "%s",
             local_path != NULL ? local_path : "");
    snprintf(apps->voice.status,
             sizeof(apps->voice.status),
             "Saved locally; waiting for authenticated Scribe ingest.");
}

void tabforge_voice_set_ingest_result(tabforge_apps_t *apps,
                                      bool success,
                                      const char *ingest_id)
{
    if (apps == NULL) {
        return;
    }
    apps->voice.state = success ? TABFORGE_VOICE_UPLOADED : TABFORGE_VOICE_FAILED;
    snprintf(apps->voice.ingest_id,
             sizeof(apps->voice.ingest_id),
             "%s",
             ingest_id != NULL ? ingest_id : "");
    snprintf(apps->voice.status,
             sizeof(apps->voice.status),
             success ? "Accepted by Scribe ingest." : "Scribe ingest failed; local audio was preserved.");
}

const char *tabforge_voice_state_text(tabforge_voice_state_t state)
{
    switch (state) {
    case TABFORGE_VOICE_RECORDING:
        return "recording";
    case TABFORGE_VOICE_SAVED_LOCAL:
        return "saved-local";
    case TABFORGE_VOICE_QUEUED:
        return "queued";
    case TABFORGE_VOICE_UPLOADED:
        return "uploaded";
    case TABFORGE_VOICE_FAILED:
        return "failed";
    case TABFORGE_VOICE_IDLE:
    default:
        return "idle";
    }
}

void tabforge_ota_set_boot_status(tabforge_apps_t *apps,
                                  bool rollback_enabled,
                                  bool rollback_available,
                                  bool pending_boot_validation,
                                  bool signature_verification_available)
{
    if (apps == NULL) {
        return;
    }
    apps->ota.rollback_enabled = rollback_enabled;
    apps->ota.rollback_available = rollback_available;
    apps->ota.pending_boot_validation = pending_boot_validation;
    apps->ota.signature_verification_available = signature_verification_available;
    snprintf(apps->ota.status,
             sizeof(apps->ota.status),
             "HTTPS %s | SHA256 %s | signature %s | rollback %s%s",
             apps->ota.https_required ? "required" : "off",
             apps->ota.sha256_required ? "required" : "off",
             signature_verification_available ? "enabled" : "key needed",
             rollback_enabled ? (rollback_available ? "available" : "armed") : "off",
             pending_boot_validation ? " | pending validation" : "");
}

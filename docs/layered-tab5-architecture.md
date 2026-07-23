# Layered Tab5 Architecture

The new architecture extends the proven TabForge runtime in place. It does not discard the working TabOS or TabForge sources, and it does not claim unfinished server integrations are live.

## Layer 1: HAL and Device Data

`firmware/tabforge-tab5/main/layers/tabforge_layer1_device.*`

- Normalizes display, touch, battery, SD, Wi-Fi, mic, and USB-host state.
- Publishes a versioned `tabforge.device.v1` snapshot instead of exposing driver globals to apps.
- Separates hardware readiness from product pages so a replacement driver does not require rewriting the shell.

## Layer 2: Shell

`firmware/tabforge-tab5/main/layers/tabforge_layer2_shell.*`

- Owns Home, Apps, Settings, Update, and App routes.
- Owns theme choice, clock text, battery text, and visible save state.
- The clock falls back to uptime until valid wall time exists.
- Save feedback remains visible for 4.5 seconds and distinguishes saving, saved, and failed.

The existing LVGL UI remains the renderer. Screen-timeout, Wi-Fi, and Mobile Base pairing persistence now update the shell save model and show explicit result text.

## Layer 3: Apps and Pages

`firmware/tabforge-tab5/main/layers/tabforge_layer3_apps.*`

### Dashboard

Three non-actionable placeholders are present:

- Unraid
- Personal Programs
- Scribe Suite

They intentionally do not guess endpoints, credentials, or controls.

### Scribe Tasks

The page exposes API/auth/network/queue state. Its safety boundary is:

- authenticated shared Scribe API only;
- no direct SQLite/database writes;
- deduplicated local queue for future offline edits;
- no task mutation until identity and API work from `SCR-008`/`SCR-009` exists.

### Voice Memo

The lifecycle is modeled as:

`recording -> saved-local -> queued -> uploaded`

Failures preserve the local WAV. Upload must use normal authenticated Scribe ingest so the recording follows the same transcription, action extraction, deduplication, and server-retention rules as recorder audio. The Tab5 page does not pretend that API exists yet.

### Update

- HTTPS is mandatory for manifest and firmware URLs.
- Firmware size and SHA256 must match before the boot partition changes.
- Dual OTA slots and bootloader rollback are enabled.
- The page exposes whether rollback is available and whether a boot is pending validation.
- Image-signature enforcement remains blocked until the signing key and recovery process are provisioned; the UI reports that fact instead of calling hash verification a signature.

### SDR

SDR is a receive-only future module boundary. Existing USB descriptor detection and presets remain available, but no transmitter path, deep DSP, aircraft decoder, or ungrounded direction/location estimate was added.

## Migration Rule

New features must enter through the three layer interfaces. Existing `app_main.c` hardware code can be migrated incrementally after each behavior is verified on a positively identified Tab5. The source archives in `tab5-source-preservation.md` are the rollback point for code; the known physical image remains the device recovery evidence.

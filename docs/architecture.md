# TabForge Architecture

TabForge keeps the Tab5 as the operator surface and treats the C6L and T-Deck as managed radios. This avoids one firmware image trying to own every radio stack at the same time.

## Product Layers

1. Layer 1: HAL and device data
   - Board drivers and device adapters stay below the UI.
   - `tabforge_layer1_device.*` emits a versioned, digestible device snapshot for the shell, apps, and future authenticated APIs.
   - Existing C6L, T-Deck, IR, USB, SD, mic, battery, IMU, display, and touch code remains intact while it is migrated behind this boundary.

2. Layer 2: shell
   - `tabforge_layer2_shell.*` owns routes, theme selection, clock text, battery text, and persistent save feedback.
   - The current LVGL launcher remains the renderer. It reads the shell model instead of owning persisted state.
   - Every Save flow must show `saving`, `saved`, or `failed`; persistence must never be visually ambiguous.

3. Layer 3: apps and pages
   - `tabforge_layer3_apps.*` defines Dashboard, Scribe Tasks, Voice Memo, Update, and receive-only SDR boundaries.
   - Dashboard cards remain placeholders until their authenticated read APIs and exact requirements are supplied.
   - Scribe Tasks and Voice Memo may use only the shared authenticated Scribe API. Direct database writes are forbidden.
   - SDR remains receive-only. Deep DSP, aircraft decoding, and direction/location views are future modules.

## Device Adapters

- `unit-c6l`: Meshtastic serial, MeshCore console, or TabForge bridge profile.
- `tdeck`: Z-Deck/Meshtastic serial and safe update helper.
- `unit-ir`: Grove IR receive/transmit.
- `usb-host`: keyboards, serial devices, and removable storage.

## Update Path

   - Tab5 checks GitHub Pages manifest.
   - Manifest and firmware URLs must use HTTPS.
   - Firmware is written only after size and SHA256 validation metadata is available.
   - Dual OTA slots and bootloader rollback are enabled, and the Update page exposes rollback state.
   - Image-signature enforcement is not claimed until an offline signing key and a tested recovery procedure exist.
   - The Update button requires on-screen confirmation before writing flash.
   - Companion-device update helpers never erase private storage by default.

## Runtime State

Runtime state belongs on SD under `/tabforge`:

- `/tabforge/config.json`: selected radios, mode defaults, update channel, UI preferences.
- `/tabforge/logs/events.jsonl`: redacted event log.
- `/tabforge/audio/*.wav`: mic recordings.
- `/tabforge/ir/*.json`: learned IR codes and macros.
- `/tabforge/backups/`: local profile backups. Do not publish this folder.

## Why C6L Modes Stay Separate

The Unit C6L can run official Meshtastic, MeshCore, or custom firmware, but those modes own the radio stack differently. TabForge stores the desired mode and transport profile, then reconnects to the radio using the matching driver. Switching mode may require flashing the C6L; switching the Tab5 UI does not silently reflash it.

## First Functional Slice

The first slice should work without replacing C6L firmware:

1. Tab5 boots the TabForge shell.
2. USB host finds the C6L as a CDC serial device.
3. Meshtastic driver reads device status and node list.
4. Operator sends one channel message.
5. Mic Deck records a WAV to SD.
6. IR Lab learns and replays a NEC code.
7. Update Center checks GitHub Pages and reports whether an update exists.

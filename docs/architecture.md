# TabForge Architecture

TabForge keeps the Tab5 as the operator surface and treats the C6L and T-Deck as managed radios. This avoids one firmware image trying to own every radio stack at the same time.

## Layers

1. Tab5 shell
   - LVGL touchscreen launcher.
   - USB host device picker.
   - SD-backed config and logs.
   - Mic recorder, IR app, update center, and system dashboard.

2. Device adapters
   - `unit-c6l`: Meshtastic serial, MeshCore console, or TabForge bridge profile.
   - `tdeck`: Z-Deck/Meshtastic serial and safe update helper.
   - `unit-ir`: Grove IR receive/transmit.
   - `usb-host`: keyboards, serial devices, and removable storage.

3. Mode drivers
   - Meshtastic driver: node list, text messages, direct sends, GPS handoff, canned messages, and status.
   - MeshCore driver: command console, contact/room metadata, `start ota`, and browser upload helper.
   - Bridge driver: newline JSON packets for devices flashed with TabForge bridge firmware.

4. Update path
   - Tab5 checks GitHub Pages manifest.
   - Firmware URLs are downloaded only after SHA256 validation metadata is available.
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


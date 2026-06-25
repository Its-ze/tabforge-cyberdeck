# Meshtastic and MeshCore Mode Switch

TabForge exposes a single Mesh app with two mode drivers.

## Meshtastic Mode

Primary target: Unit C6L running official Meshtastic.

Initial feature set:

- Radio identity and firmware status.
- Node list.
- Channel message send/receive.
- Direct message target picker.
- Canned message send.
- GPS/location handoff when the operator allows it.
- Basic channel summary with secrets redacted.

Transport priority:

1. USB CDC serial.
2. BLE if available and not blocked by Wi-Fi mode.
3. Wi-Fi/client route only when the radio is configured for it and the operator accepts the Bluetooth tradeoff.

## MeshCore Mode

Primary target: ESP32-based MeshCore companion or repeater profile.

Initial feature set:

- Command Line tab.
- Contact/room summary.
- Send visible commands only.
- `start ota` helper.
- Browser/upload handoff for `http://192.168.4.1/update` after the MeshCore OTA AP is active.

MeshCore OTA rule:

- TabForge can trigger or guide OTA, but it does not push firmware through LoRa.
- ESP32 MeshCore OTA uses the target's temporary Wi-Fi AP and upload page.
- Upload only non-merged application binaries intended for the exact board/profile.

## Switch Behavior

Changing the TabForge mode does this:

1. Save current driver state to SD.
2. Disconnect the current transport.
3. Load the selected mode profile.
4. Reconnect using the selected transport priority.
5. Show any required physical/firmware action.

Changing the TabForge mode does not do this:

- It does not silently flash the C6L.
- It does not erase Meshtastic channels.
- It does not publish channel URLs or keys.
- It does not send MeshCore admin commands without explicit confirmation.


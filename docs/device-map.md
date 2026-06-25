# Device Map

## M5Stack Tab5

Role: primary cyberdeck.

Use:

- 5 inch touch UI for all normal operations.
- USB-A host for C6L, T-Deck, keyboards, serial boards, and mass storage.
- USB-C OTG for flashing and desktop development.
- Dual mic array for voice notes, push-to-record, sound meter, and voice macro triggers.
- Speaker/headphone for alerts and playback.
- SD card for config, logs, IR codes, audio, update staging, and profile backups.
- Camera for future QR/device-label scan.
- BMI270 IMU for orientation and gesture shortcuts.
- RS485 for industrial/field terminal work.

## M5Stack Unit C6L

Role: detachable LoRa radio.

Supported modes:

- `meshtastic`: default and safest first mode.
- `meshcore`: companion or repeater/room profile when supported by the chosen firmware.
- `tabforge-bridge`: optional future firmware for direct JSON control, OLED status, RGB, buzzer, and button events.

Important constraints:

- Treat Meshtastic Wi-Fi and Bluetooth mode changes carefully on ESP32-C6.
- Back up any radio configuration before changing firmware family.
- Use USB CDC first. It is more deterministic than wireless setup during bring-up.

## LilyGO T-Deck

Role: existing mesh companion.

Use:

- Query Z-Deck/Meshtastic status.
- Launch compatible on-device payloads.
- Send safe remote commands through explicit profiles.
- Offer OTA guidance without erasing private settings or LittleFS unless intentionally requested.

Important constraints:

- Do not assume a generic ESP32-S3 USB serial device is safe to flash.
- Keep T-Deck private/admin channel material out of this public project.
- Do not flash LittleFS as part of routine updates.

## M5Stack Unit IR

Role: Grove IR learner/blaster.

Use:

- Learn 38 kHz demodulated IR codes.
- Replay NEC-style commands.
- Save named IR macros to SD.
- Trigger IR actions from the launcher, voice macros, or mesh messages that are explicitly allowed.

Important constraints:

- Effective range is short.
- Store learned codes as device-agnostic JSON, not as private account credentials.


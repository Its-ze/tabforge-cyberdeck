# TabForge Tab5 Firmware

ESP-IDF firmware shell for M5Stack Tab5.

## Build

```powershell
idf.py set-target esp32p4
idf.py build
```

The installed firmware uses the official M5Stack Tab5 ESP-BSP, starts LVGL in landscape orientation, shows a TabForge dashboard, mounts the SD card, creates runtime folders/config under `/sdcard/tabforge`, probes the onboard microphone, and provides a touch button to switch the active Meshtastic/MeshCore profile label.

## Wi-Fi and Internet OTA

Add Wi-Fi credentials to `/sdcard/tabforge/config.json`:

```json
"wifi": {
  "ssid": "your-network",
  "password": "your-password",
  "autoConnect": true
}
```

Use `Update Center` on the Tab5 to scan nearby networks, select the next scanned network, connect, run internet OTA from the GitHub Pages manifest, and reboot after a completed OTA write. Open networks can connect directly from scan results; secured networks need the password saved in config first.

## Runtime Paths

- Config: `/sdcard/tabforge/config.json`
- Event log: `/sdcard/tabforge/logs/events.jsonl`
- Audio: `/sdcard/tabforge/audio`
- IR: `/sdcard/tabforge/ir`

## Update Policy

TabForge OTA uses the `ota_0` and `ota_1` partitions in `partitions.csv`. The application must verify SHA256 from the GitHub Pages manifest before calling ESP-IDF OTA write APIs.

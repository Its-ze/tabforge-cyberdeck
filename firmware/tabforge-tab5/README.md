# TabForge Tab5 Firmware

ESP-IDF firmware shell for M5Stack Tab5.

## Build

```powershell
idf.py set-target esp32p4
idf.py build
```

The first installed version uses the official M5Stack Tab5 ESP-BSP, starts LVGL, shows a TabForge dashboard, mounts the SD card, and creates runtime folders/config under `/sdcard/tabforge`.

## Runtime Paths

- Config: `/sdcard/tabforge/config.json`
- Event log: `/sdcard/tabforge/logs/events.jsonl`
- Audio: `/sdcard/tabforge/audio`
- IR: `/sdcard/tabforge/ir`

## Update Policy

TabForge OTA uses the `ota_0` and `ota_1` partitions in `partitions.csv`. The application must verify SHA256 from the GitHub Pages manifest before calling ESP-IDF OTA write APIs.

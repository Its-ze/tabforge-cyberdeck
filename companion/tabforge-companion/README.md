# TabForge Companion

Android companion APK for TabForge Cyberdeck.

It pairs with the Tab5 local API, shares the phone GPS fix to the Tab, checks firmware updates, manages the GitHub-backed TabForge app catalog, installs or updates apps on the Tab SD card, and reads diagnostics.

Build from the repo root:

```powershell
.\tools\build-companion-apk.ps1
```

The debug APK is copied to `docs\downloads\tabforge-companion-0.1.0-debug.apk`.

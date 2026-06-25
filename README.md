# TabForge Cyberdeck

TabForge Cyberdeck is a standalone M5Stack Tab5 field console for managing a small mesh and sensor kit from the Tab5 screen.

Target hardware:

- M5Stack Tab5 as the primary UI, update controller, audio recorder, USB host, SD logger, camera/IMU surface, and power/status dashboard.
- M5Stack Unit C6L as a detachable LoRa radio running Meshtastic, MeshCore, or an optional TabForge bridge profile.
- LilyGO T-Deck as an existing Z-Deck/Meshtastic companion that can be queried, launched, and updated without overwriting private settings.
- M5Stack Unit IR as a Grove IR learner/blaster for short-range control and IR macro capture.

The project name, folder, and intended public repo are:

- Local folder: `F:\Dropbox\Dev Ops\TabForge Cyberdeck`
- GitHub repo target: `Its-ze/tabforge-cyberdeck`
- Pages/update URL target: `https://its-ze.github.io/tabforge-cyberdeck/`

## Current Build Shape

This repo is intentionally split into a Tab5 shell and companion-device profiles. The C6L and T-Deck can keep running their own Meshtastic or MeshCore images while TabForge talks to them through USB CDC, BLE/Wi-Fi where available, or a small JSON bridge when you choose to flash a bridge profile.

Included now:

- ESP-IDF Tab5 firmware shell with OTA partitions and a feature registry.
- Feature catalog for mesh, IR, mic/audio, T-Deck, C6L, USB host, SD, camera, IMU, RS485, and updates.
- Meshtastic/MeshCore mode-switch contract.
- GitHub Pages OTA manifests and update-channel metadata.
- Local cyberdeck console preview under `web/console`.
- Verification and publish scripts.

## Commands

Validate the repo:

```powershell
.\tools\verify-project.ps1
```

Open the local console preview:

```powershell
Start-Process "F:\Dropbox\Dev Ops\TabForge Cyberdeck\web\console\index.html"
```

Build the Tab5 firmware after ESP-IDF is installed:

```powershell
cd "F:\Dropbox\Dev Ops\TabForge Cyberdeck\firmware\tabforge-tab5"
idf.py set-target esp32p4
idf.py build
```

Publish or update the GitHub repo after a token is available:

```powershell
$env:GITHUB_TOKEN = "<token with repo permission>"
.\tools\publish-github.ps1
```

The publish script creates or reuses `Its-ze/tabforge-cyberdeck`, commits the local scaffold, pushes `main`, and enables GitHub Pages with Actions deployments. It does not print the token.

## First Flash Plan

1. Build and USB-flash `firmware/tabforge-tab5` to the Tab5.
2. Copy `sd/tabforge/config.example.json` to the Tab5 SD card as `/tabforge/config.json`.
3. Keep C6L on official Meshtastic first and connect it to the Tab5 by USB host.
4. Use TabForge `Mesh > C6L > Meshtastic` to read node status and send a test message.
5. Switch the C6L profile to MeshCore only after backing up its current radio image/config.
6. Use `Update` from the Tab5 to read the Pages manifest and apply a signed/hash-checked TabForge update.

## Public-Safe Boundary

Do not commit:

- Meshtastic channel URLs, PSKs, private keys, admin keys, owner-specific raw exports, or full `--info` logs.
- MeshCore channel/contact secrets.
- Recorded mic audio, raw camera frames, location trails, or SD logs.
- GitHub tokens or Wi-Fi passwords.

## Reference Docs

- M5Stack Tab5 docs: https://docs.m5stack.com/en/core/Tab5
- M5Stack Unit C6L docs: https://docs.m5stack.com/en/unit/Unit_C6L
- M5Stack Unit IR docs: https://docs.m5stack.com/en/unit/ir
- Unit C6L Meshtastic guide: https://docs.m5stack.com/en/guide/lora/meshtastic/unit_c6l
- MeshCore OTA FAQ: https://github.com/meshcore-dev/MeshCore/blob/main/docs/faq.md

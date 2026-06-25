# OTA and GitHub Sync

TabForge uses GitHub for public code, public Pages metadata, and update manifests.

## Targets

- Repository: `https://github.com/Its-ze/tabforge-cyberdeck`
- Pages: `https://its-ze.github.io/tabforge-cyberdeck/`
- Stable manifest: `https://its-ze.github.io/tabforge-cyberdeck/manifest.json`

## Tab5 OTA Flow

1. Operator presses `Update`.
2. Tab5 downloads the selected channel manifest.
3. Tab5 checks target, version, firmware URL, file size, and SHA256.
4. Tab5 asks for on-screen confirmation.
5. Tab5 downloads to a staging area.
6. Tab5 verifies SHA256 before writing OTA flash.
7. Tab5 reboots and records the new version to SD.

## Companion Update Flow

Companion devices use explicit helpers:

- C6L Meshtastic: prefer official Meshtastic flasher/client flow unless a TabForge bridge image is intentionally selected.
- C6L MeshCore: use MeshCore `start ota` plus the target AP/upload page.
- T-Deck/Z-Deck: use the existing Z-Deck OTA path or verified USB flash path. Do not flash LittleFS by default.

## GitHub Publish Script

`tools/publish-github.ps1` is adapted from the existing Dev Ops public repo helper. It:

- Reads a token from `GITHUB_TOKEN`, `GITHUB_TOKEN_FILE`, or `.secrets/github-token.clixml`.
- Creates or reuses `Its-ze/tabforge-cyberdeck`.
- Initializes local git if needed.
- Commits the scaffold.
- Pushes `main` with a temporary `GIT_ASKPASS` helper.
- Enables GitHub Pages with Actions deployments.

The script does not print tokens.

## Dev Ops Sync

After the repo exists, run this helper to add it to the existing Dev Ops GitHub sync manifest:

```powershell
.\tools\register-devops-sync.ps1
```

The helper refuses to modify the shared manifest until this folder is a git repo and `origin` points at `Its-ze/tabforge-cyberdeck`.

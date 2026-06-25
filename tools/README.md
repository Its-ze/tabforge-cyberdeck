# TabForge Tools

## Verify

```powershell
.\tools\verify-project.ps1
```

Validates required files, JSON, PowerShell syntax, JavaScript syntax when Node is available, and public manifest shape.

## Package Release

```powershell
.\tools\package-release.ps1 -Version 0.2.0 -FirmwareBin .\firmware\tabforge-tab5\build\tabforge-tab5.bin
```

Copies a built firmware binary into `docs/firmware/`, computes SHA256, and updates the selected manifest.

## Publish GitHub

```powershell
.\tools\save-github-token.ps1
.\tools\publish-github.ps1
```

Creates or reuses `Its-ze/tabforge-cyberdeck`, pushes `main`, and enables GitHub Pages.

## Register Dev Ops Sync

```powershell
.\tools\register-devops-sync.ps1
```

Adds TabForge to the shared Dev Ops GitHub sync manifest after the local repo has a valid `origin`.

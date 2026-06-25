param(
  [string]$Version = "0.1.0",
  [string]$FirmwareBin = "",
  [string]$Channel = "stable"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($FirmwareBin)) {
  $FirmwareBin = Join-Path $Root "firmware\tabforge-tab5\build\tabforge-tab5.bin"
}

if (-not (Test-Path -LiteralPath $FirmwareBin)) {
  throw "Firmware binary not found: $FirmwareBin"
}

$docsDir = Join-Path $Root "docs"
$firmwareDir = Join-Path $docsDir "firmware"
New-Item -ItemType Directory -Force -Path $firmwareDir | Out-Null

$targetName = "tabforge-tab5-$Version.bin"
$targetPath = Join-Path $firmwareDir $targetName
Copy-Item -LiteralPath $FirmwareBin -Destination $targetPath -Force

$hash = (Get-FileHash -LiteralPath $targetPath -Algorithm SHA256).Hash.ToLowerInvariant()
$size = (Get-Item -LiteralPath $targetPath).Length
$manifestName = if ($Channel -eq "lab") { "manifest-lab.json" } else { "manifest.json" }
$manifestPath = Join-Path $docsDir $manifestName
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

$manifest.generatedAt = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
$manifest.latest.version = $Version
$manifest.latest.name = "TabForge Cyberdeck $Version"
$manifest.latest.status = "firmware"
$manifest.latest.firmware.available = $true
$manifest.latest.firmware.url = "https://its-ze.github.io/tabforge-cyberdeck/firmware/$targetName"
$manifest.latest.firmware.sha256 = $hash
$manifest.latest.firmware.size = $size

$manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host "Packaged $targetName"
Write-Host "SHA256 $hash"
Write-Host "Size $size"
Write-Host "Updated $manifestName"

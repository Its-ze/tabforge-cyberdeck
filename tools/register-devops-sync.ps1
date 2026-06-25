param(
  [string]$ManifestPath = "F:\Dropbox\Dev Ops\scripts\github-devops-sync.repos.json"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

if (-not (Test-Path -LiteralPath (Join-Path $Root ".git"))) {
  throw "TabForge is not a git repository yet. Run tools\publish-github.ps1 first."
}

$remoteUrl = (& git -C $Root remote get-url origin) 2>$null
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($remoteUrl)) {
  throw "TabForge origin remote is not configured yet."
}

if ($remoteUrl -notmatch "github.com[:/]Its-ze/tabforge-cyberdeck(\.git)?$") {
  throw "Unexpected origin remote: $remoteUrl"
}

if (-not (Test-Path -LiteralPath $ManifestPath)) {
  throw "Dev Ops sync manifest not found: $ManifestPath"
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$existing = @($manifest.repos | Where-Object { $_.github -eq "Its-ze/tabforge-cyberdeck" })
if ($existing.Count -gt 0) {
  Write-Host "TabForge is already registered in $ManifestPath"
  exit 0
}

$entry = [pscustomobject]@{
  name = "TabForge Cyberdeck"
  path = "F:\Dropbox\Dev Ops\TabForge Cyberdeck"
  remote = "origin"
  branch = "main"
  github = "Its-ze/tabforge-cyberdeck"
  visibility = "public"
  enabled = $true
}

$manifest.repos += $entry
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8
Write-Host "Registered TabForge Cyberdeck in $ManifestPath"

param(
  [string]$Channel = "stable"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$DocsDir = Join-Path $Root "docs"
$AppsDir = Join-Path $DocsDir "apps"
$CatalogPath = Join-Path $DocsDir "app-store.json"
$BaseUrl = "https://its-ze.github.io/tabforge-cyberdeck"

function Copy-Value {
  param($Object, [string]$Name, [object]$Default = "")
  if ($null -ne $Object.PSObject.Properties[$Name] -and $null -ne $Object.$Name) {
    return $Object.$Name
  }
  return $Default
}

function Assert-SafeId {
  param([string]$Id, [string]$Path)
  if ([string]::IsNullOrWhiteSpace($Id) -or $Id -notmatch '^[A-Za-z0-9_-]+$') {
    throw "Unsafe or missing app id '$Id' in $Path"
  }
}

$apps = New-Object System.Collections.Generic.List[object]
$packageFiles = Get-ChildItem -LiteralPath $AppsDir -File -Filter "*.json" | Sort-Object Name

foreach ($file in $packageFiles) {
  $raw = Get-Content -LiteralPath $file.FullName -Raw
  $package = $raw | ConvertFrom-Json
  Assert-SafeId -Id $package.id -Path $file.FullName

  if ($package.schema -notmatch '^tabforge\.mini-app\.v[0-9]+$') {
    throw "Unsupported package schema in $($file.Name): $($package.schema)"
  }
  if ([string]::IsNullOrWhiteSpace($package.version)) {
    throw "Package $($file.Name) is missing version."
  }
  if ([string]::IsNullOrWhiteSpace($package.name)) {
    throw "Package $($file.Name) is missing name."
  }
  if ([string]::IsNullOrWhiteSpace($package.summary)) {
    throw "Package $($file.Name) is missing summary."
  }

  $expectedName = "$($package.id).json"
  if ($file.Name -ne $expectedName) {
    throw "Package filename $($file.Name) must match id as $expectedName"
  }

  $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
  $size = (Get-Item -LiteralPath $file.FullName).Length
  $actions = @()
  if ($package.actions) {
    $actions = @($package.actions)
  }

  $store = $package.store
  $apps.Add([ordered]@{
      id = $package.id
      name = $package.name
      summary = $package.summary
      version = $package.version
      surface = (Copy-Value $package "surface" "tab5")
      category = (Copy-Value $store "category" "General")
      icon = (Copy-Value $store "icon" "APP")
      status = (Copy-Value $store "status" "stable")
      minFirmware = (Copy-Value $store "minFirmware" "0.1.21")
      releaseNotes = (Copy-Value $store "releaseNotes" "Package update.")
      actionCount = $actions.Count
      packageUrl = "$BaseUrl/apps/$($file.Name)"
      installedPath = "/tabforge/apps/$($file.Name)"
      sha256 = $hash
      size = $size
    })
}

$appArray = $apps.ToArray()
$catalog = [ordered]@{
  schema = "tabforge.app-store.v1"
  project = "tabforge-cyberdeck"
  channel = $Channel
  updated = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd")
  libraryUrl = "$BaseUrl/apps/"
  notes = "Data-driven TabForge mini-app packages. Packages are JSON only, SHA256-checked, and do not contain native executable code."
  updatePolicy = [ordered]@{
    mode = "per-app"
    installRoot = "/tabforge/apps"
    indexPath = "/tabforge/apps/installed.jsonl"
    hash = "sha256"
  }
  apps = $appArray
}

$catalogJson = $catalog | ConvertTo-Json -Depth 10
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($CatalogPath, $catalogJson + [Environment]::NewLine, $utf8NoBom)
Write-Host "Updated docs/app-store.json with $($apps.Count) apps."

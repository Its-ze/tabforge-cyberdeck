param(
  [switch]$Ci
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$Errors = New-Object System.Collections.Generic.List[string]
$Warnings = New-Object System.Collections.Generic.List[string]

function Add-CheckError {
  param([string]$Message)
  $Errors.Add($Message)
}

function Add-CheckWarning {
  param([string]$Message)
  $Warnings.Add($Message)
}

function Resolve-ProjectPath {
  param([string]$Path)
  Join-Path $Root $Path
}

$requiredPaths = @(
  "README.md",
  "config\app-catalog.json",
  "config\device-profiles.json",
  "config\update-channels.json",
  "docs\app-store.json",
  "docs\index.html",
  "docs\apps\index.html",
  "docs\manifest.json",
  "docs\manifest-lab.json",
  "firmware\tabforge-tab5\CMakeLists.txt",
  "firmware\tabforge-tab5\main\app_main.c",
  "firmware\tabforge-tab5\main\idf_component.yml",
  "firmware\tabforge-tab5\partitions.csv",
  "protocol\tabforge-link-v0.md",
  "protocol\tabforge-link.schema.json",
  "sd\tabforge\config.example.json",
  "tools\update-app-catalog.ps1",
  "web\console\index.html",
  "web\console\app.js",
  "web\console\styles.css"
)

foreach ($path in $requiredPaths) {
  if (-not (Test-Path -LiteralPath (Resolve-ProjectPath $path))) {
    Add-CheckError "Missing required path: $path"
  }
}

if (Test-Path -LiteralPath (Resolve-ProjectPath ".git")) {
  foreach ($path in $requiredPaths) {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
      & git -C $Root ls-files --error-unmatch $path 2>$null | Out-Null
      $gitExit = $LASTEXITCODE
    } finally {
      $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($gitExit -ne 0) {
      Add-CheckError "Required path exists locally but is not tracked by git: $path"
    }
  }
}

$jsonFiles = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "*.json" |
  Where-Object { $_.FullName -notmatch "\\(node_modules|\.git|build|dist)\\" }

foreach ($file in $jsonFiles) {
  try {
    Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json | Out-Null
  } catch {
    Add-CheckError "JSON parse failed: $($file.FullName) - $($_.Exception.Message)"
  }
}

$psFiles = Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "*.ps1" |
  Where-Object { $_.FullName -notmatch "\\(node_modules|\.git|build|dist)\\" }

foreach ($file in $psFiles) {
  $tokens = $null
  $parseErrors = $null
  [System.Management.Automation.Language.Parser]::ParseFile($file.FullName, [ref]$tokens, [ref]$parseErrors) | Out-Null
  if ($parseErrors.Count -gt 0) {
    foreach ($err in $parseErrors) {
      Add-CheckError "PowerShell parse failed: $($file.FullName):$($err.Extent.StartLineNumber) $($err.Message)"
    }
  }
}

$node = Get-Command node -ErrorAction SilentlyContinue
if ($node) {
  $jsFiles = Get-ChildItem -LiteralPath (Resolve-ProjectPath "web") -Recurse -File -Filter "*.js"
  foreach ($file in $jsFiles) {
    & node --check $file.FullName | Out-Null
    if ($LASTEXITCODE -ne 0) {
      Add-CheckError "node --check failed: $($file.FullName)"
    }
  }
} else {
  Add-CheckWarning "node is not available; skipped JS syntax checks."
}

$manifestPath = Resolve-ProjectPath "docs\manifest.json"
if (Test-Path -LiteralPath $manifestPath) {
  $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
  if ($manifest.project -ne "tabforge-cyberdeck") {
    Add-CheckError "docs\manifest.json project must be tabforge-cyberdeck."
  }
  if ($manifest.latest.target -ne "tab5-esp32p4") {
    Add-CheckError "docs\manifest.json latest.target must be tab5-esp32p4."
  }
  if ($manifest.latest.firmware.available -eq $true) {
    if ([string]::IsNullOrWhiteSpace($manifest.latest.firmware.url)) {
      Add-CheckError "Firmware is available but url is empty."
    }
    if ([string]::IsNullOrWhiteSpace($manifest.latest.firmware.sha256)) {
      Add-CheckError "Firmware is available but sha256 is empty."
    }
    if (-not $manifest.latest.firmware.size) {
      Add-CheckError "Firmware is available but size is empty."
    }
  } else {
    Add-CheckWarning "Stable manifest has no firmware binary yet."
  }
}

$appCatalogPath = Resolve-ProjectPath "docs\app-store.json"
if (Test-Path -LiteralPath $appCatalogPath) {
  $catalog = Get-Content -LiteralPath $appCatalogPath -Raw | ConvertFrom-Json
  if ($catalog.project -ne "tabforge-cyberdeck") {
    Add-CheckError "docs\app-store.json project must be tabforge-cyberdeck."
  }
  if ($catalog.schema -notmatch "^tabforge\.app-store\.v[0-9]+$") {
    Add-CheckError "docs\app-store.json has unsupported schema: $($catalog.schema)"
  }
  if (-not $catalog.apps -or @($catalog.apps).Count -lt 1) {
    Add-CheckError "docs\app-store.json must publish at least one app."
  }

  foreach ($app in @($catalog.apps)) {
    if ([string]::IsNullOrWhiteSpace($app.id) -or $app.id -notmatch "^[A-Za-z0-9_-]+$") {
      Add-CheckError "App catalog entry has invalid id: $($app.id)"
      continue
    }

    $packagePath = Resolve-ProjectPath "docs\apps\$($app.id).json"
    if (-not (Test-Path -LiteralPath $packagePath)) {
      Add-CheckError "App package missing for catalog entry: $($app.id)"
      continue
    }

    $package = Get-Content -LiteralPath $packagePath -Raw | ConvertFrom-Json
    if ($package.id -ne $app.id) {
      Add-CheckError "App package id mismatch for $($app.id)."
    }
    if ($package.version -ne $app.version) {
      Add-CheckError "App package version mismatch for $($app.id)."
    }
    if ($package.permissions.nativeCode -ne $false) {
      Add-CheckError "App package $($app.id) must keep permissions.nativeCode=false."
    }

    $actualHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash.ToLowerInvariant()
    $actualSize = (Get-Item -LiteralPath $packagePath).Length
    if ($actualHash -ne $app.sha256) {
      Add-CheckError "App package hash mismatch for $($app.id)."
    }
    if ([int64]$app.size -ne $actualSize) {
      Add-CheckError "App package size mismatch for $($app.id)."
    }
  }
}

$forbiddenPatterns = @(
  ([string]::Concat("-----BEGIN ", "PRIVATE KEY-----")),
  ([string]::Concat("meshtastic", "://")),
  ([string]::Concat("meshcore", "://channel/add")),
  ([string]::Concat("meshcore", "://contact/add"))
)

$textFiles = Get-ChildItem -LiteralPath $Root -Recurse -File |
  Where-Object {
    $_.FullName -notmatch "\\(node_modules|\.git|build|dist|\.secrets)\\" -and
    $_.Extension -in @(".md", ".json", ".js", ".css", ".html", ".cpp", ".h", ".txt", ".yml", ".yaml", ".ps1")
  }

foreach ($file in $textFiles) {
  $content = Get-Content -LiteralPath $file.FullName -Raw
  foreach ($pattern in $forbiddenPatterns) {
    if ($content.Contains($pattern)) {
      Add-CheckError "Forbidden private material marker found in $($file.FullName): $pattern"
    }
  }
}

if ($Warnings.Count -gt 0) {
  foreach ($warning in $Warnings) {
    Write-Warning $warning
  }
}

if ($Errors.Count -gt 0) {
  foreach ($errorMessage in $Errors) {
    Write-Error $errorMessage
  }
  exit 1
}

Write-Host "TabForge project verification passed."
if ($Ci) {
  Write-Host "CI mode complete."
}

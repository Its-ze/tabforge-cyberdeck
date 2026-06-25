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
  "docs\index.html",
  "docs\manifest.json",
  "docs\manifest-lab.json",
  "firmware\tabforge-tab5\CMakeLists.txt",
  "firmware\tabforge-tab5\main\app_main.cpp",
  "firmware\tabforge-tab5\partitions.csv",
  "protocol\tabforge-link-v0.md",
  "protocol\tabforge-link.schema.json",
  "sd\tabforge\config.example.json",
  "web\console\index.html",
  "web\console\app.js",
  "web\console\styles.css"
)

foreach ($path in $requiredPaths) {
  if (-not (Test-Path -LiteralPath (Resolve-ProjectPath $path))) {
    Add-CheckError "Missing required path: $path"
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

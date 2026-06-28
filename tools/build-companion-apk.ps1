param(
  [switch]$Release
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$AppDir = Join-Path $Root "companion\tabforge-companion"
$PackageJsonPath = Join-Path $AppDir "package.json"
$ToolingCandidates = @(
  (Join-Path $env:LOCALAPPDATA "RoadLensScoutTooling"),
  (Join-Path $Root ".tooling")
)
$ToolingDir = $ToolingCandidates |
  Where-Object { Test-Path -LiteralPath $_ } |
  Select-Object -First 1

if (-not $ToolingDir) {
  $ToolingDir = Join-Path $env:LOCALAPPDATA "RoadLensScoutTooling"
}

$LocalSdk = Join-Path $ToolingDir "android-sdk"
$LocalJdkRoot = Join-Path $ToolingDir "jdk21"
if (-not (Test-Path -LiteralPath $LocalJdkRoot)) {
  $LocalJdkRoot = Join-Path $ToolingDir "jdk17"
}

if (Test-Path -LiteralPath $LocalJdkRoot) {
  $localJdk = Get-ChildItem -LiteralPath $LocalJdkRoot -Directory -ErrorAction SilentlyContinue |
    Select-Object -First 1 -ExpandProperty FullName
  if ($localJdk) {
    $env:JAVA_HOME = $localJdk
    $env:Path = "$localJdk\bin;$env:Path"
  }
}

if (Test-Path -LiteralPath $LocalSdk) {
  $cmdlineLatest = Join-Path $LocalSdk "cmdline-tools\latest\bin"
  $platformTools = Join-Path $LocalSdk "platform-tools"
  $env:ANDROID_HOME = $LocalSdk
  $env:ANDROID_SDK_ROOT = $LocalSdk
  $env:Path = "$platformTools;$cmdlineLatest;$env:Path"
}

function Assert-Command {
  param(
    [string]$Name,
    [string]$InstallHint
  )
  if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
    throw "$Name is not available. $InstallHint"
  }
}

function Invoke-Checked {
  param(
    [string]$Command,
    [string[]]$Arguments
  )
  & $Command @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Command failed with exit code $LASTEXITCODE"
  }
}

function Add-AndroidPermission {
  param(
    [string]$ManifestPath,
    [string]$Permission
  )

  $text = Get-Content -LiteralPath $ManifestPath -Raw
  if ($text.Contains($Permission)) {
    return
  }

  $manifestClose = $text.IndexOf(">")
  if ($manifestClose -lt 0) {
    throw "Could not patch AndroidManifest.xml; <manifest> tag not found."
  }

  $permissionLine = "`r`n    <uses-permission android:name=`"$Permission`" />"
  $text = $text.Insert($manifestClose + 1, $permissionLine)
  Set-Content -LiteralPath $ManifestPath -Encoding UTF8 -Value $text
}

function Patch-AndroidManifest {
  $manifestPath = Join-Path $AppDir "android\app\src\main\AndroidManifest.xml"
  if (-not (Test-Path -LiteralPath $manifestPath)) {
    throw "AndroidManifest.xml was not generated: $manifestPath"
  }

  Add-AndroidPermission -ManifestPath $manifestPath -Permission "android.permission.INTERNET"
  Add-AndroidPermission -ManifestPath $manifestPath -Permission "android.permission.ACCESS_NETWORK_STATE"
  Add-AndroidPermission -ManifestPath $manifestPath -Permission "android.permission.ACCESS_FINE_LOCATION"
  Add-AndroidPermission -ManifestPath $manifestPath -Permission "android.permission.ACCESS_COARSE_LOCATION"

  $text = Get-Content -LiteralPath $manifestPath -Raw
  if ($text -notmatch "usesCleartextTraffic") {
    $text = [regex]::Replace($text, "<application\b", "<application android:usesCleartextTraffic=`"true`"", 1)
    Set-Content -LiteralPath $manifestPath -Encoding UTF8 -Value $text
  }
}

Assert-Command "node" "Install Node.js or add it to PATH."
Assert-Command "npm" "Install npm or add it to PATH."
Assert-Command "java" "Install JDK 17+ or reuse the RoadLensScoutTooling JDK cache."

if (-not $env:ANDROID_HOME -and -not $env:ANDROID_SDK_ROOT) {
  throw "ANDROID_HOME or ANDROID_SDK_ROOT is not set. Install Android SDK or reuse the RoadLensScoutTooling SDK cache."
}

if (-not (Test-Path -LiteralPath $PackageJsonPath)) {
  throw "Companion package.json not found: $PackageJsonPath"
}

$package = Get-Content -LiteralPath $PackageJsonPath -Raw | ConvertFrom-Json
$version = [string]$package.version
$flavor = if ($Release) { "release" } else { "debug" }

Push-Location $AppDir
try {
  if (-not (Test-Path -LiteralPath "node_modules")) {
    Invoke-Checked "npm" @("install")
  }

  Invoke-Checked "npm" @("run", "build")

  if (-not (Test-Path -LiteralPath "android")) {
    Invoke-Checked "npx" @("cap", "add", "android")
  }

  Invoke-Checked "npx" @("cap", "sync", "android")
  Patch-AndroidManifest

  Push-Location "android"
  try {
    if ($Release) {
      Invoke-Checked ".\gradlew.bat" @("--no-daemon", "assembleRelease")
    } else {
      Invoke-Checked ".\gradlew.bat" @("--no-daemon", "assembleDebug")
    }
  } finally {
    Pop-Location
  }
} finally {
  Pop-Location
}

$apkSource = Join-Path $AppDir "android\app\build\outputs\apk\$flavor\app-$flavor.apk"
if (-not (Test-Path -LiteralPath $apkSource)) {
  throw "APK not found after build: $apkSource"
}

$downloadsDir = Join-Path $Root "docs\downloads"
New-Item -ItemType Directory -Force -Path $downloadsDir | Out-Null
$apkName = "tabforge-companion-$version-$flavor.apk"
$apkTarget = Join-Path $downloadsDir $apkName
Copy-Item -LiteralPath $apkSource -Destination $apkTarget -Force

$hash = (Get-FileHash -LiteralPath $apkTarget -Algorithm SHA256).Hash.ToLowerInvariant()
$shaPath = "$apkTarget.sha256.txt"
Set-Content -LiteralPath $shaPath -Encoding UTF8 -Value "$hash  $apkName"

Write-Host "Built $apkName"
Write-Host "APK $apkTarget"
Write-Host "SHA256 $hash"

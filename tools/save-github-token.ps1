param(
  [string]$TokenFile = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

if ([string]::IsNullOrWhiteSpace($TokenFile)) {
  $TokenFile = Join-Path $Root ".secrets\github-token.clixml"
}

$dir = Split-Path -Parent $TokenFile
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$token = Read-Host "GitHub token with repo permission" -AsSecureString
$token | Export-Clixml -LiteralPath $TokenFile
Write-Host "Saved encrypted token for this Windows user: $TokenFile"

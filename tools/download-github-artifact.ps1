param(
  [Parameter(Mandatory = $true)]
  [long]$RunId,
  [string]$ArtifactName = "tabforge-tab5-firmware",
  [string]$Owner = "Its-ze",
  [string]$Repo = "tabforge-cyberdeck",
  [string]$Destination = ""
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path

function ConvertFrom-SecureToken {
  param([securestring]$SecureToken)
  $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($SecureToken)
  try {
    [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
  } finally {
    [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
  }
}

function Get-GitHubToken {
  $token = [Environment]::GetEnvironmentVariable("GITHUB_TOKEN")
  if (-not [string]::IsNullOrWhiteSpace($token)) {
    return $token
  }

  $tokenFile = [Environment]::GetEnvironmentVariable("GITHUB_TOKEN_FILE")
  if ([string]::IsNullOrWhiteSpace($tokenFile)) {
    $tokenFile = Join-Path $Root ".secrets\github-token.clixml"
  }
  if (-not (Test-Path -LiteralPath $tokenFile)) {
    throw "Set GITHUB_TOKEN or GITHUB_TOKEN_FILE before downloading an Actions artifact."
  }
  $stored = Import-Clixml -LiteralPath $tokenFile
  if ($stored -isnot [securestring]) {
    throw "The GitHub token file does not contain an encrypted SecureString."
  }
  ConvertFrom-SecureToken $stored
}

if ([string]::IsNullOrWhiteSpace($Destination)) {
  $Destination = Join-Path $Root "artifacts\gha-$RunId"
} elseif (-not [IO.Path]::IsPathRooted($Destination)) {
  $Destination = Join-Path $Root $Destination
}

$token = Get-GitHubToken
$headers = @{
  Authorization = "Bearer $token"
  Accept = "application/vnd.github+json"
  "X-GitHub-Api-Version" = "2022-11-28"
}
$artifacts = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/$Owner/$Repo/actions/runs/$RunId/artifacts"
$artifact = $artifacts.artifacts | Where-Object { $_.name -eq $ArtifactName } | Select-Object -First 1
if (-not $artifact) {
  throw "Artifact '$ArtifactName' was not found for Actions run $RunId."
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$zipPath = Join-Path $Destination "$ArtifactName.zip"
try {
  Invoke-WebRequest -Headers $headers -Uri $artifact.archive_download_url -OutFile $zipPath
  Expand-Archive -LiteralPath $zipPath -DestinationPath $Destination -Force
} finally {
  Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue
}

Write-Host "Downloaded $ArtifactName from Actions run $RunId"
Write-Host "Destination: $Destination"
Get-ChildItem -LiteralPath $Destination -Recurse -File |
  Select-Object FullName, Length |
  Format-Table -AutoSize

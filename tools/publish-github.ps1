param(
  [string]$Owner = "Its-ze",
  [string]$Repo = "tabforge-cyberdeck",
  [string]$Description = "M5Stack Tab5 cyberdeck for C6L Meshtastic, MeshCore, T-Deck, IR, mic, and OTA updates.",
  [string]$DefaultBranch = "main"
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
  $envToken = [Environment]::GetEnvironmentVariable("GITHUB_TOKEN")
  if (-not [string]::IsNullOrWhiteSpace($envToken)) {
    return $envToken
  }

  $tokenFile = [Environment]::GetEnvironmentVariable("GITHUB_TOKEN_FILE")
  if ([string]::IsNullOrWhiteSpace($tokenFile)) {
    $tokenFile = Join-Path $Root ".secrets\github-token.clixml"
  }

  if (Test-Path -LiteralPath $tokenFile) {
    $storedToken = Import-Clixml -LiteralPath $tokenFile
    if ($storedToken -is [securestring]) {
      return ConvertFrom-SecureToken $storedToken
    }
  }

  throw "Set GITHUB_TOKEN or run tools\save-github-token.ps1 first. The token needs repo permission."
}

function Invoke-Git {
  param([string[]]$Arguments)
  & git -C $Root @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
  }
}

function Invoke-GitPushWithToken {
  param(
    [string]$Token,
    [string]$Branch
  )

  $askPassCmd = Join-Path ([System.IO.Path]::GetTempPath()) "tabforge-git-askpass-$PID.cmd"
  $askPassPs1 = Join-Path ([System.IO.Path]::GetTempPath()) "tabforge-git-askpass-$PID.ps1"
@'
param([string]$Prompt)
if ($Prompt -match "Username") {
  [Console]::Out.WriteLine("x-access-token")
} else {
  [Console]::Out.WriteLine($env:TABFORGE_GITHUB_TOKEN)
}
'@ | Set-Content -LiteralPath $askPassPs1 -Encoding UTF8
@"
@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$askPassPs1" "%~1"
"@ | Set-Content -LiteralPath $askPassCmd -Encoding ASCII

  $oldAskPass = [Environment]::GetEnvironmentVariable("GIT_ASKPASS", "Process")
  $oldPrompt = [Environment]::GetEnvironmentVariable("GIT_TERMINAL_PROMPT", "Process")
  $oldToken = [Environment]::GetEnvironmentVariable("TABFORGE_GITHUB_TOKEN", "Process")

  try {
    [Environment]::SetEnvironmentVariable("GIT_ASKPASS", $askPassCmd, "Process")
    [Environment]::SetEnvironmentVariable("GIT_TERMINAL_PROMPT", "0", "Process")
    [Environment]::SetEnvironmentVariable("TABFORGE_GITHUB_TOKEN", $Token, "Process")
    Invoke-Git @("push", "-u", "origin", $Branch)
  } finally {
    [Environment]::SetEnvironmentVariable("GIT_ASKPASS", $oldAskPass, "Process")
    [Environment]::SetEnvironmentVariable("GIT_TERMINAL_PROMPT", $oldPrompt, "Process")
    [Environment]::SetEnvironmentVariable("TABFORGE_GITHUB_TOKEN", $oldToken, "Process")
    Remove-Item -LiteralPath $askPassCmd -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $askPassPs1 -Force -ErrorAction SilentlyContinue
  }
}

function Enable-GitHubPages {
  param(
    [hashtable]$Headers,
    [string]$RepoOwner,
    [string]$RepoName
  )

  $pagesBody = @{ build_type = "workflow" } | ConvertTo-Json
  try {
    Invoke-RestMethod -Headers $Headers -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/pages" -Method Post -Body $pagesBody -ContentType "application/json" | Out-Null
    Write-Host "Enabled GitHub Pages with GitHub Actions deployments."
  } catch {
    $message = $_.Exception.Message
    if ($message -match "already") {
      Write-Host "GitHub Pages already enabled."
    } else {
      Write-Warning "Could not enable GitHub Pages automatically: $message"
    }
  }
}

& (Join-Path $PSScriptRoot "verify-project.ps1")

$token = Get-GitHubToken
$headers = @{
  Authorization = "Bearer $token"
  Accept = "application/vnd.github+json"
  "X-GitHub-Api-Version" = "2022-11-28"
}

$me = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/user"
$body = @{
  name = $Repo
  description = $Description
  private = $false
  has_issues = $true
  has_projects = $false
  has_wiki = $false
  auto_init = $false
} | ConvertTo-Json

try {
  if ($Owner -eq $me.login) {
    $created = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/user/repos" -Method Post -Body $body -ContentType "application/json"
  } else {
    $created = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/orgs/$Owner/repos" -Method Post -Body $body -ContentType "application/json"
  }
} catch {
  $statusCode = $_.Exception.Response.StatusCode.value__
  if ($statusCode -ne 422) {
    throw
  }
  Write-Warning "Repository $Owner/$Repo already exists. Reusing it."
  $created = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/$Owner/$Repo"
}

if (-not (Test-Path -LiteralPath (Join-Path $Root ".git"))) {
  Invoke-Git @("init", "-b", $DefaultBranch)
} else {
  Invoke-Git @("branch", "-M", $DefaultBranch)
}

$gitName = (& git -C $Root config user.name) 2>$null
if ([string]::IsNullOrWhiteSpace($gitName)) {
  Invoke-Git @("config", "user.name", $me.login)
}

$gitEmail = (& git -C $Root config user.email) 2>$null
if ([string]::IsNullOrWhiteSpace($gitEmail)) {
  Invoke-Git @("config", "user.email", "$($me.id)+$($me.login)@users.noreply.github.com")
}

Invoke-Git @("add", ".")
$pendingChanges = & git -C $Root status --porcelain
if ($pendingChanges) {
  Invoke-Git @("commit", "-m", "Create TabForge Cyberdeck scaffold")
} else {
  Write-Host "No local changes to commit."
}

$remoteExists = (& git -C $Root remote) -contains "origin"
if ($remoteExists) {
  Invoke-Git @("remote", "set-url", "origin", $created.clone_url)
} else {
  Invoke-Git @("remote", "add", "origin", $created.clone_url)
}

Invoke-GitPushWithToken -Token $token -Branch $DefaultBranch
Enable-GitHubPages -Headers $headers -RepoOwner $created.owner.login -RepoName $created.name

Write-Host "Published public repository: $($created.html_url)"
Write-Host "Pages URL target: https://$($created.owner.login).github.io/$($created.name)/"

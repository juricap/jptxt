# jptxt installer for Windows (PowerShell 5.1+)
#   irm https://raw.githubusercontent.com/juricap/jptxt/main/install.ps1 | iex
# Pin:  $env:JPTXT_VERSION='v0.1.2'; irm ... | iex
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'

$Repo = 'juricap/jptxt'
$Asset = 'jptxt-windows-x64.exe'
$Version = $env:JPTXT_VERSION
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Url = "https://github.com/$Repo/releases/latest/download/$Asset"
} else {
    if ($Version -notmatch '^v') { $Version = "v$Version" }
    $Url = "https://github.com/$Repo/releases/download/$Version/$Asset"
}

$Dir = Join-Path $env:LOCALAPPDATA 'Programs\jptxt'
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$Dest = Join-Path $Dir 'jptxt.exe'

Write-Host "Downloading $Url"
Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
if (-not (Test-Path $Dest) -or (Get-Item $Dest).Length -lt 10000) {
    throw "Download failed or file too small: $Dest"
}

$UserPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ([string]::IsNullOrEmpty($UserPath)) { $UserPath = '' }
$parts = $UserPath -split ';' | Where-Object { $_ -ne '' }
if ($parts -notcontains $Dir) {
    $NewPath = ($parts + $Dir) -join ';'
    [Environment]::SetEnvironmentVariable('Path', $NewPath, 'User')
    Write-Host "Added $Dir to your user PATH (new terminals pick this up)."
}
if ($env:Path -notlike "*$Dir*") {
    $env:Path = "$Dir;$env:Path"
}

Write-Host ""
Write-Host "Installed $Dest"
Write-Host "Run:  jptxt"
Write-Host "This process PATH is updated; if 'jptxt' is not found, open a new terminal."
)

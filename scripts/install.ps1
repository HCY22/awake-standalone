<#
.SYNOPSIS
    Installs standalone Awake for the current user (no admin rights needed).
.DESCRIPTION
    Copies the built files to %LOCALAPPDATA%\Programs\Awake and creates a
    Start menu shortcut that launches Awake in tray mode (Awake.exe -c).
.PARAMETER Source
    Folder containing Awake.exe. Defaults to the folder this script is in
    (release zip layout), falling back to .\dist\win-x64.
.PARAMETER Startup
    Also start Awake automatically when you sign in.
#>
[CmdletBinding()]
param(
    [string]$Source,
    [switch]$Startup
)

$ErrorActionPreference = 'Stop'

if (-not $Source) {
    if (Test-Path (Join-Path $PSScriptRoot 'Awake.exe')) {
        $Source = $PSScriptRoot
    } else {
        $Source = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\win-x64'
    }
}

if (-not (Test-Path (Join-Path $Source 'Awake.exe'))) {
    throw "Awake.exe not found in '$Source'. Build first with scripts\build.ps1."
}

$target = Join-Path $env:LOCALAPPDATA 'Programs\Awake'

# Stop a running instance so files can be replaced.
Get-Process -Name Awake -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($target, [StringComparison]::OrdinalIgnoreCase) } |
    Stop-Process -Force

New-Item -ItemType Directory -Force -Path $target | Out-Null
Copy-Item -Path (Join-Path $Source '*') -Destination $target -Recurse -Force

$exe = Join-Path $target 'Awake.exe'
$shell = New-Object -ComObject WScript.Shell

function New-AwakeShortcut([string]$path) {
    $lnk = $shell.CreateShortcut($path)
    $lnk.TargetPath = $exe
    $lnk.Arguments = '-c'
    $lnk.WorkingDirectory = $target
    $lnk.IconLocation = "$exe,0"
    $lnk.Description = 'Awake - keep your PC awake'
    $lnk.Save()
}

$programs = [Environment]::GetFolderPath('Programs')
New-AwakeShortcut (Join-Path $programs 'Awake.lnk')

if ($Startup) {
    New-AwakeShortcut (Join-Path ([Environment]::GetFolderPath('Startup')) 'Awake.lnk')
}

Write-Host "Installed to $target" -ForegroundColor Green
Write-Host "Start menu shortcut created$(if ($Startup) { ' (and added to startup)' })."
Start-Process -FilePath $exe -ArgumentList '-c' -WorkingDirectory $target

<#
.SYNOPSIS
    Removes standalone Awake installed by install.ps1.
.PARAMETER KeepSettings
    Keep %LOCALAPPDATA%\AwakeStandalone (settings and logs).
#>
[CmdletBinding()]
param([switch]$KeepSettings)

$target = Join-Path $env:LOCALAPPDATA 'Programs\Awake'

Get-Process -Name Awake -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and $_.Path.StartsWith($target, [StringComparison]::OrdinalIgnoreCase) } |
    Stop-Process -Force

Remove-Item (Join-Path ([Environment]::GetFolderPath('Programs')) 'Awake.lnk') -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath('Startup')) 'Awake.lnk') -ErrorAction SilentlyContinue
Remove-Item $target -Recurse -Force -ErrorAction SilentlyContinue

if (-not $KeepSettings) {
    Remove-Item (Join-Path $env:LOCALAPPDATA 'AwakeStandalone') -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host 'Awake removed.' -ForegroundColor Green

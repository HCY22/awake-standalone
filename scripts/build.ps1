<#
.SYNOPSIS
    Builds standalone Awake into .\dist\<rid>.
.PARAMETER Runtime
    win-x64 (default) or win-arm64.
.PARAMETER SelfContained
    Bundle the .NET runtime (~80 MB, no prerequisites). Without it the output is
    ~4 MB but requires the .NET 10 Runtime to be installed.
#>
[CmdletBinding()]
param(
    [ValidateSet('win-x64', 'win-arm64')]
    [string]$Runtime = 'win-x64',
    [switch]$SelfContained
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root "dist\$Runtime"

if (Test-Path $out) { Remove-Item $out -Recurse -Force }

dotnet publish (Join-Path $root 'Awake.csproj') `
    -c Release `
    -r $Runtime `
    --self-contained:$($SelfContained.IsPresent.ToString().ToLower()) `
    -o $out
if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed ($LASTEXITCODE)" }

Remove-Item (Join-Path $out '*.pdb') -ErrorAction SilentlyContinue
Write-Host "Built to $out" -ForegroundColor Green

<#
.SYNOPSIS
    End-to-end smoke test for Awake.exe on a real Windows machine (used by CI).
    Needs an elevated shell for `powercfg /requests`.
#>
param([Parameter(Mandatory)][string]$Exe)

$ErrorActionPreference = 'Stop'
$Exe = (Resolve-Path $Exe).Path
$failures = 0

function Check([bool]$condition, [string]$message) {
    if ($condition) { Write-Host "PASS  $message" -ForegroundColor Green }
    else { Write-Host "FAIL  $message" -ForegroundColor Red; $script:failures++ }
}

function Start-Awake([string[]]$Arguments) {
    if ($Arguments) { Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru }
    else { Start-Process -FilePath $Exe -PassThru }
}

function Test-AwakeRequest {
    $out = powercfg /requests | Out-String
    return $out -match 'Awake\.exe'
}

# Isolate settings/logs from the real profile.
$env:LOCALAPPDATA = Join-Path ([IO.Path]::GetTempPath()) ("awake-smoke-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $env:LOCALAPPDATA | Out-Null
$settingsPath = Join-Path $env:LOCALAPPDATA 'AwakeStandalone\settings.json'

# 1. Invalid argument -> exit code 1 (prints to the parent console, no dialog).
$p = Start-Process -FilePath $Exe -ArgumentList '--bogus' -PassThru -NoNewWindow
Check ($p.WaitForExit(10000) -and $p.ExitCode -eq 1) 'invalid argument exits with code 1'

# 2. --version -> exit code 0.
$p = Start-Process -FilePath $Exe -ArgumentList '--version' -PassThru -NoNewWindow
Check ($p.WaitForExit(10000) -and $p.ExitCode -eq 0) '--version exits with code 0'

# 3. Timed mode keeps the system awake, then exits by itself.
$p = Start-Awake @('-t', '4')
Start-Sleep -Seconds 2
Check (Test-AwakeRequest) '-t 4: power request is active'
Check ($p.WaitForExit(15000) -and $p.ExitCode -eq 0) '-t 4: exits after the time limit'
Start-Sleep -Milliseconds 500
Check (-not (Test-AwakeRequest)) '-t 4: power request released'

# 4. Process binding: exits when the bound process exits.
$target = Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 3' -PassThru -WindowStyle Hidden
$p = Start-Awake @('-p', "$($target.Id)")
Start-Sleep -Seconds 1
Check (Test-AwakeRequest) '-p: power request is active'
Check ($p.WaitForExit(15000)) '-p: exits when the bound process exits'

# 5. Tray mode with settings.json.
$p = Start-Awake @('-c')
Start-Sleep -Seconds 2
Check (Test-Path $settingsPath) '-c: settings.json scaffolded'
$json = Get-Content $settingsPath -Raw | ConvertFrom-Json
Check ($json.properties.mode -eq 0) '-c: default mode is passive'
Check (-not (Test-AwakeRequest)) '-c: passive mode has no power request'

# Second instance must refuse to start.
$second = Start-Awake @('-c')
Check ($second.WaitForExit(10000) -and $second.ExitCode -eq 1) 'second instance exits with code 1'

# Editing the file switches the mode.
$json.properties.mode = 1
$json.properties.keepDisplayOn = $true
[IO.File]::WriteAllText($settingsPath, ($json | ConvertTo-Json -Depth 5))
Start-Sleep -Seconds 2
$requests = powercfg /requests | Out-String
Check ($requests -match 'Awake\.exe') '-c: indefinite mode after editing settings.json'
Check (($requests -split 'SYSTEM:')[0] -match 'Awake\.exe') '-c: display request active (keepDisplayOn)'

# Exit event stops it cleanly.
$ev = [System.Threading.EventWaitHandle]::OpenExisting('Local\AwakeStandaloneExitEvent-5b1f3c2e-8d0a-4f5e-9b7a-1c2d3e4f5a6b')
[void]$ev.Set()
Check ($p.WaitForExit(10000)) '-c: exits on the exit event'

$log = Get-ChildItem (Join-Path $env:LOCALAPPDATA 'AwakeStandalone\Logs') -Filter 'Awake_*.log' | Select-Object -First 1
Check ($null -ne $log) 'log file written'
if ($log) { Get-Content $log.FullName | Select-Object -Last 40 | ForEach-Object { Write-Host "  log: $_" } }

Get-Process -Name Awake -ErrorAction SilentlyContinue | Stop-Process -Force
if ($failures -gt 0) { throw "$failures smoke test(s) failed" }
Write-Host 'All smoke tests passed.' -ForegroundColor Green

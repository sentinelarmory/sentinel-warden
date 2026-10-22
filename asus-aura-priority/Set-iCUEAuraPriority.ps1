# Set-iCUEAuraPriority.ps1
# Aura SDK 3.08.01+ only hands lighting control to an app whose priority in
# LightingService\PriorityList.xml is higher than the current owner
# (LightingService = 3). iCUE ships with 1, so its colours are silently ignored.
# This raises iCUEDevicePluginHost.exe to $Priority, restarts LightingService
# and restarts iCUE as the normal (non-elevated) user.
#
# LightingService rewrites PriorityList.xml with the defaults when Windows
# boots (a manual service restart keeps our value), so this is also run by a
# logon task with -AtLogon: it waits until iCUE is up, then applies the fix.
# Run in an elevated PowerShell.

param([int]$Priority = 5, [switch]$AtLogon)

$ErrorActionPreference = 'Stop'
$file    = 'C:\Program Files (x86)\LightingService\PriorityList.xml'
$icueExe = 'C:\Program Files\Corsair\Corsair iCUE5 Software\iCUE.exe'
$log     = Join-Path $PSScriptRoot 'Set-iCUEAuraPriority.log'

function Log($msg) {
    $line = '{0:yyyy-MM-dd HH:mm:ss} {1}' -f (Get-Date), $msg
    Write-Host $line
    Add-Content -Path $log -Value $line
}

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script in an elevated PowerShell (Run as administrator).'
}

if ($AtLogon) {
    # Let LightingService write its boot defaults and iCUE start first.
    Start-Sleep -Seconds 45
    $deadline = (Get-Date).AddMinutes(3)
    while (-not (Get-Process iCUE -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 5
    }
    Start-Sleep -Seconds 15
}

# The file is UTF-16 LE without BOM; read and write it the same way.
$enc  = New-Object System.Text.UnicodeEncoding($false, $false)
$text = [IO.File]::ReadAllText($file, $enc)
$pattern = '(iCUEDevicePluginHost\.exe</AppPath>\s*<Priority>)(\d+)(</Priority>)'
$m = [regex]::Match($text, $pattern)
if (-not $m.Success) { throw 'iCUEDevicePluginHost.exe entry not found in PriorityList.xml' }

if ([int]$m.Groups[2].Value -eq $Priority -and $AtLogon) {
    Log "priority already $Priority, restarting LightingService + iCUE anyway"
} else {
    $backup = Join-Path $PSScriptRoot ("PriorityList.xml.bak-{0:yyyyMMdd-HHmmss}" -f (Get-Date))
    Copy-Item $file $backup
    $text = [regex]::Replace($text, $pattern, "`${1}$Priority`${3}")
    [IO.File]::WriteAllText($file, $text, $enc)
    Log "iCUEDevicePluginHost.exe priority $($m.Groups[2].Value) -> $Priority (backup $backup)"
}

Get-Process iCUE -ErrorAction SilentlyContinue | Stop-Process -Force

# LightingService sometimes refuses to stop in time; force it if needed.
try {
    Restart-Service LightingService -Force -ErrorAction Stop
} catch {
    $svcPid = (Get-CimInstance Win32_Service -Filter "Name='LightingService'").ProcessId
    if ($svcPid) { Stop-Process -Id $svcPid -Force }
    Start-Sleep -Seconds 3
    Start-Service LightingService
}
Log 'LightingService restarted'
Start-Sleep -Seconds 5

# Start iCUE through Explorer so it runs as the normal user, not elevated.
Start-Process explorer.exe -ArgumentList "`"$icueExe`""
Log 'iCUE started'

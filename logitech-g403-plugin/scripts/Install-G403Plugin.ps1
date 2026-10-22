# Install-G403Plugin.ps1
# Signs the G403 iCUE plugin with YOUR local code-signing certificate (see the
# README, "Create your signing certificate"), copies it into iCUE's plugins
# folder and restarts everything in the order the ASUS Aura priority fix needs:
# iCUE plugin host first, then LightingService, then iCUE (so iCUE takes the
# Aura token last).
#
# Run in an elevated PowerShell from the folder that contains .\Logitech:
#   .\Install-G403Plugin.ps1 -CertThumbprint <your certificate thumbprint>
#   .\Install-G403Plugin.ps1 -Remove

param(
    [string]$CertThumbprint,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$source  = Join-Path $PSScriptRoot 'Logitech'
if (-not $Remove -and -not $CertThumbprint) {
    throw 'Pass -CertThumbprint <thumbprint of your code-signing certificate> (see README).'
}
$target  = 'C:\Program Files\Corsair\Corsair iCUE5 Software\plugins\Logitech'
$icueExe = 'C:\Program Files\Corsair\Corsair iCUE5 Software\iCUE.exe'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script in an elevated PowerShell (Run as administrator).'
}

Get-Process iCUE -ErrorAction SilentlyContinue | Stop-Process -Force
Write-Host 'iCUE stopped'

if (-not $Remove) {
    $dll  = Join-Path $source 'g403_plugin.dll'
    $cert = Get-Item "Cert:\CurrentUser\My\$CertThumbprint"
    Set-AuthenticodeSignature -FilePath $dll -Certificate $cert -HashAlgorithm SHA256 | Out-Null
    $status = (Get-AuthenticodeSignature $dll).Status
    if ($status -ne 'Valid') { throw "Signature status: $status" }
    Write-Host 'Signed: Valid'
}

# Release the old DLL: stop the service and any plugin host processes it leaves behind.
Stop-Service iCUEDevicePluginHost -Force
# A hung plugin host may refuse to die; that is fine, the locked DLL gets renamed below.
cmd.exe /c "taskkill /F /IM iCUEDevicePluginHost.exe >nul 2>&1"
$deadline = (Get-Date).AddSeconds(15)
while ((Get-Process iCUEDevicePluginHost -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }

if (-not $Remove) {
    New-Item -ItemType Directory -Force $target | Out-Null
    Get-ChildItem $target -Filter '*.dll.old*' -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
    $installed = Join-Path $target 'g403_plugin.dll'
    if (Test-Path $installed) {
        # A loaded DLL cannot be overwritten but can be renamed; iCUE only loads *.dll.
        try { Remove-Item $installed -Force -ErrorAction Stop }
        catch { Rename-Item $installed ("g403_plugin.dll.old-{0:yyyyMMddHHmmss}" -f (Get-Date)); Write-Host 'Old DLL was in use: renamed' }
    }
    Copy-Item -Force (Join-Path $source '*') $target
    Write-Host "Copied to $target"
} elseif (Test-Path $target) {
    Remove-Item -Recurse -Force $target
    Write-Host "Removed $target"
}

Start-Service iCUEDevicePluginHost
Write-Host 'iCUE plugin host restarted'

# LightingService sometimes refuses to stop in time; force it if needed.
try {
    Restart-Service LightingService -Force -ErrorAction Stop
} catch {
    $svcPid = (Get-CimInstance Win32_Service -Filter "Name='LightingService'").ProcessId
    if ($svcPid) { Stop-Process -Id $svcPid -Force }
    Start-Sleep -Seconds 3
    Start-Service LightingService
}
Write-Host 'LightingService restarted'
Start-Sleep -Seconds 5

# Start iCUE through Explorer so it runs as the normal user, not elevated.
Start-Process explorer.exe -ArgumentList "`"$icueExe`""
Write-Host 'iCUE started. Give it ~30 seconds.'

<p align="center">
  <img src="assets/banner.png" alt="Sentinel Warden: every light on your desk, under one command: Corsair iCUE" width="100%">
</p>

<p align="center">
  <img alt="Windows 11" src="https://img.shields.io/badge/Windows-11-0078D4?style=for-the-badge">
  <img alt="Corsair iCUE 5" src="https://img.shields.io/badge/Corsair-iCUE%205-FFDE00?style=for-the-badge&logo=corsair&logoColor=black">
  <img alt="ASUS Aura SDK 3.08" src="https://img.shields.io/badge/ASUS-Aura%20SDK%203.08-00539B?style=for-the-badge&logo=asus&logoColor=white">
  <img alt="Logitech G HUB" src="https://img.shields.io/badge/Logitech-G%20HUB-00B8FC?style=for-the-badge">
  <img alt="License: MIT" src="https://img.shields.io/badge/License-MIT-22c55e?style=for-the-badge">
</p>

# Sentinel Warden

Two fixes for people who want **Corsair iCUE 5** to be the one app that runs all their lighting:

| Fix | Problem | Result |
|---|---|---|
| [ASUS Aura priority fix](#1-asus-aura-priority-fix) | After an Armoury Crate update to **Aura SDK 3.08**, iCUE still lists your ASUS devices but can no longer change their lighting. | iCUE controls your ASUS Aura RGB again, also after a reboot. |
| [Logitech mouse plugin](#2-logitech-mouse-plugin-for-icue) | iCUE has no Logitech support, so your mouse sits outside your iCUE effects and Murals. | Your Logitech mouse becomes a device in iCUE and can be placed on **Murals**. |

<p align="center">
  <img src="assets/sync.gif" alt="One iCUE Murals effect sweeping across an ASUS GPU, the motherboard I/O shroud and a Logitech mouse, all in sync" width="100%">
  <br><sub>Illustration: one iCUE Mural driving an ASUS GPU, the motherboard I/O shroud and a Logitech mouse in sync.</sub>
</p>

> **Not affiliated with Corsair, ASUS or Logitech.** All trademarks belong to their owners.
> No files from those vendors are included: the scripts change a setting in ASUS's own config file,
> and the plugin loads the Logitech LED SDK that G HUB already installed on your PC.
> Use at your own risk.

## Is this for you?

**ASUS fix: you probably have the problem if all of these are true:**

- [ ] You run iCUE 5 *and* Armoury Crate (iCUE talks to ASUS hardware through Armoury Crate's Aura service).
- [ ] iCUE shows your ASUS device (motherboard, graphics card, ...) but the colours you pick there don't show up on the hardware.
- [ ] Armoury Crate itself **can** still change those lights (so the hardware and drivers are fine).
- [ ] `C:\ProgramData\ASUS\ARMOURY CRATE Diagnosis\LightingService\AuraSDK.log` contains lines tagged `[AuraSDK][3.08.01]` (or newer)
      and a line like `[guidToken::CheckPriority] m_priority = 1 CurrentPriority = 3.`

**Logitech plugin: it is meant for you if:**

- [ ] You have a Logitech mouse with RGB lighting, controlled by **G HUB**.
- [ ] You want it to follow iCUE (and iCUE Murals) instead of running its own effects in G HUB.

## Tested on

This was worked out and tested on **one** PC:

| Part | Model / version |
|---|---|
| OS | Windows 11 (64-bit) |
| Motherboard | ASUS ROG Strix Z890-F Gaming WiFi (Aura: I/O shroud) |
| Graphics card | ASUS ROG Strix GeForce RTX 5070 Ti OC (Aura: 23 LEDs) |
| Mouse | Logitech G403 HERO (USB) |
| Corsair | iCUE 5.52, plugin host as shipped with it |
| ASUS | Armoury Crate 6.7.6, AURA Service (LightingService) 3.11.08, Aura SDK 3.08.01 |
| Logitech | G HUB 2026.6 |

### Why it may not work on similar hardware

"Same brand, same series" is not enough to promise anything, because each fix leans on a moving part:

- **The ASUS fix relies on how Aura SDK 3.08.01 reads `PriorityList.xml`.** ASUS can change the file, the
  priority numbers or the whole mechanism in any Armoury Crate update, and older SDKs (3.07 and below) don't
  have the check at all. Then this fix is pointless.
- **Different ASUS devices go through different Aura components** (motherboard HAL, VGA HAL, DRAM, extension
  cards). They all sit behind the same priority check, so they *should* behave the same, but only the
  motherboard and GPU above were tested.
- **The plugin uses an undocumented iCUE interface.** Corsair can change it in any iCUE update. The
  plugin then stops loading until it is updated.
- **Logitech mice differ in zones.** Logitech's LED SDK numbers each mouse's lighting zones differently,
  newer mice are not in Logitech's documentation, and some G HUB profiles (locked LIGHTSYNC, game profiles)
  override SDK colours.
- **Windows 10, ARM PCs, other iCUE/G HUB versions:** untested.

If it works (or doesn't) on your hardware, please open an issue with your models and versions so the table
above can grow.

---

## 1. ASUS Aura priority fix

### What is going on

iCUE drives ASUS RGB through its ASUS plugin, which talks to ASUS's Aura SDK. Since Aura SDK **3.08.01**
the SDK checks priorities before it hands over lighting control (with 3.07.05 the same log shows no such
check): an app only gets control if its priority in

```
C:\Program Files (x86)\LightingService\PriorityList.xml
```

is **higher** than the current owner's. After the update the file lists `LightingService.exe = 3` and
`iCUEDevicePluginHost.exe = 1`, so iCUE loses, and nothing tells you. The SDK's own log shows it,
`C:\ProgramData\ASUS\ARMOURY CRATE Diagnosis\LightingService\AuraSDK.log`:

```
[guidToken::CheckPriority] m_priority = 1 CurrentPriority = 3.
```

The fix raises iCUE's priority to 5. Two catches:

* **LightingService resets the file to its defaults at every Windows boot**, so the fix has to be re-applied at logon.
  (Restarting the service by hand keeps the new value.)
* **The order matters:** LightingService has to be restarted **before** iCUE, so iCUE asks for control last.

### Step 1: confirm it is this problem

1. Open `C:\ProgramData\ASUS\ARMOURY CRATE Diagnosis\LightingService\AuraSDK.log` (or run
   `sls -Path "<that path>" -Pattern "CheckPriority\] m_priority" | select -Last 3` in PowerShell).
2. Look at the newest `CheckPriority` line. `m_priority = 1 CurrentPriority = 3` means iCUE (1) loses
   to ASUS's own service (3). **That is the problem this fix solves.**
3. No `CheckPriority` lines at all, and the lines are tagged with an older SDK such as `[3.07.05]`? Then
   your iCUE/ASUS problem is something else, and this fix won't help.
4. Optional: open `PriorityList.xml` (it is UTF-16 text). You should see `iCUEDevicePluginHost.exe` with a
   lower `<Priority>` than `LightingService.exe`.

### Step 2: apply the fix

1. Download [`asus-aura-priority/Set-iCUEAuraPriority.ps1`](asus-aura-priority/Set-iCUEAuraPriority.ps1)
   and put it in a folder that stays, for example `C:\Tools\iCUE-Aura\` (the log is written next to it).
2. Open **Terminal (Admin)** and run it once:

   ```powershell
   powershell -ExecutionPolicy Bypass -File "C:\Tools\iCUE-Aura\Set-iCUEAuraPriority.ps1"
   ```

   The script backs up `PriorityList.xml` next to itself and sets iCUE to 5. It then restarts LightingService
   and starts iCUE again as your normal user. Your ASUS devices should follow iCUE within ~20 s.

   <img src="assets/demo-asus-fix.gif" alt="Running Set-iCUEAuraPriority.ps1, then AuraSDK.log shows m_priority = 5 and setGUID - done" width="100%">
   <br><sub>The terminal demos in this README are re-enacted with output from real runs. Paths are generic,
   the thumbprint is an example, and log line numbers are illustrative.</sub>

### Step 3: make it survive reboots

In the same admin terminal (change the path if you used another folder):

```powershell
$script  = 'C:\Tools\iCUE-Aura\Set-iCUEAuraPriority.ps1'
$action  = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$script`" -AtLogon"
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$princ   = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
Register-ScheduledTask -TaskName 'iCUE Aura priority' -Action $action -Trigger $trigger -Principal $princ
```

At every logon the script now waits until iCUE is running (about a minute), then applies the fix and restarts
LightingService and iCUE. iCUE's window may blink once.

<img src="assets/demo-logon-task.gif" alt="Registering the 'iCUE Aura priority' logon task; the task shows as Ready" width="100%">

### Step 4: check that it worked

- The newest lines in `AuraSDK.log` show `m_priority = 5 CurrentPriority = 3` followed by `setGUID - done`.
- `Set-iCUEAuraPriority.log` next to the script shows each run (`priority 1 -> 5` after a reboot is normal).
- After the next reboot, give it a minute and check your ASUS lights follow iCUE without doing anything.

### Troubleshooting

| Symptom | What to check |
|---|---|
| Lights still on ASUS's default after running the script | `AuraSDK.log`: is there a new `CheckPriority` line with `m_priority = 5`? If it still says 1, the file was not changed: run the script again as admin and read its error. |
| New `switchToken In/Out` lines in `AuraSDK.log` but **no** `CheckPriority` line | Aura still treats an older iCUE process as the owner (typically after only iCUE's plugin host was restarted). Restart LightingService first, then iCUE: run the script again. |
| `Restart-Service : ... stop failed` for LightingService | The service hung while stopping. The script kills it and starts it again. If you do it by hand, end the `LightingService` process in Task Manager and start the service. |
| Works until reboot, then not | The logon task is missing or failed: look at `Set-iCUEAuraPriority.log` and Task Scheduler. |
| Stopped working after an Armoury Crate update | Run the script again. If `CheckPriority` lines disappear or change, ASUS changed the mechanism and this fix no longer applies. |

### Undo

`Unregister-ScheduledTask -TaskName 'iCUE Aura priority' -Confirm:$false`. The next reboot puts ASUS's
default back.

---

## 2. Logitech mouse plugin for iCUE

### What it is

`logitech_plugin.dll` is an iCUE **device plugin**, the same kind of plugin iCUE uses for ASUS and NVIDIA
hardware. It shows your mouse in iCUE with its lighting zones, and forwards iCUE's colours to the mouse
through the **Logitech LED SDK that G HUB installs**. G HUB stays the mouse's driver and must keep running.

* The mouse and its zones are set in **`plugin.ini`** next to the DLL. The default is the G403 HERO.
* The plugin reports itself to iCUE as a **GPU-type device**. That is deliberate: when it reported itself as
  a mouse, iCUE did not offer it on Murals; as a GPU it is offered. In Murals it is therefore drawn with
  iCUE's GPU picture.
* Log: `C:\ProgramData\iCUE-Logitech-Plugin\plugin.log`.

### Which mice

Logitech's LED SDK addresses lighting by **zone number**, and each mouse has its own numbering. These come
from Logitech's LED SDK 9.00 documentation:

| Mouse | `plugin.ini` | Status |
|---|---|---|
| G403 / G403 HERO | `zones=2`, zone 0 = scroll wheel (`wheel`), zone 1 = logo (`logo`) | **Tested** (default) |
| G703 | same as G403 | Documented, untested |
| G900 / G903 | zone 0 = DPI indicator (`dpi`), zone 1 = logo (`logo`) | Documented, untested |
| G303 Daedalus Apex | zone 0 = side lighting (`side`), zone 1 = logo (`logo`) | Documented, untested |
| PRO Gaming Mouse | `zones=1`, zone 0 = logo and side lighting (`body`) | Documented, untested |
| Newer LIGHTSYNC mice (G502 HERO, G203, PRO X, ...) | not in the document | Try it, see below |

The SDK sends zone colours to **every** connected Logitech mouse, so with two Logitech mice both get the
same colours.

### Requirements

* Corsair iCUE 5 and Logitech G HUB, both running.
* In G HUB settings: **"Allow games & applications to control my lighting"** turned on.
* If you also have ASUS RGB: do fix 1 first. The installer below restarts LightingService in the right order.

### Why you need your own signing certificate

iCUE only loads plugin DLLs that carry a **valid Authenticode signature** that Windows trusts. A plugin from
a hobby project has no commercial signature, so you create a **local code-signing certificate on your own
PC**, tell Windows to trust it, and sign the DLL with it.

Security notes, read these before you do it:

* The private key is created **non-exportable**: Windows' own tools won't export it. That is not a vault,
  though. Malware or a person with admin rights on your PC could still dig it out, just as they could do
  far worse things already.
* Windows will trust **anything signed with this certificate on this PC**. Never share the certificate, and
  don't sign files you didn't build or download from here yourself.
* The certificate is limited to code signing. You can remove it at any time (see *Undo*).

### Install

1. Download `iCUE-Logitech-Plugin.zip` from [Releases](../../releases) and unzip it to a folder that stays,
   for example `C:\Tools\iCUE-Logitech-Plugin\`. You should have `Install-LogitechPlugin.ps1` and a
   `Logitech\` folder. (Or build it yourself, see below.)
2. **Set up your mouse** in `Logitech\plugin.ini` (skip this for a G403). See [Configure your mouse](#configure-your-mouse).
3. **Create your signing certificate** in **Terminal (Admin)**. You only do this once:

   ```powershell
   $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Local iCUE plugin signing' `
       -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable `
       -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10)
   Export-Certificate -Cert $cert -FilePath "$env:TEMP\icue-plugin-signing.cer" | Out-Null
   Import-Certificate -FilePath "$env:TEMP\icue-plugin-signing.cer" -CertStoreLocation Cert:\LocalMachine\Root
   Import-Certificate -FilePath "$env:TEMP\icue-plugin-signing.cer" -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
   $cert.Thumbprint
   ```

   Write down the **thumbprint** that is printed last (40 hex characters). The certificate goes in the
   *machine* stores because iCUE's plugin host runs as a Windows service. An elevated import into the
   machine store happens silently, so you won't get a confirmation dialog.

   <img src="assets/demo-cert.gif" alt="Creating the local code-signing certificate, trusting it on this PC and printing the thumbprint" width="100%">
4. **Install the plugin** (same admin terminal):

   ```powershell
   cd C:\Tools\iCUE-Logitech-Plugin
   powershell -ExecutionPolicy Bypass -File .\Install-LogitechPlugin.ps1 -CertThumbprint <YOUR_THUMBPRINT>
   ```

   The script signs the DLL and checks that the signature is `Valid`, then copies `Logitech\` to
   `C:\Program Files\Corsair\Corsair iCUE5 Software\plugins\`. After that it restarts the iCUE plugin host,
   LightingService and iCUE in that order.

   <img src="assets/demo-install.gif" alt="Install-LogitechPlugin.ps1 signs, copies and restarts; the plugin log shows the zones and ends with Logitech SDK ready" width="100%">
5. After ~30 s your mouse appears in iCUE under the name from `plugin.ini`. Set a colour on it or place it
   on a Mural.

To update, unzip the new release over the old folder (keep your `plugin.ini`) and run step 4 again. If the old
DLL is still in use, the script renames it to `*.dll.old-<time>`; iCUE ignores those, and a later run deletes them.

### Configure your mouse

`Logitech\plugin.ini` (edit it in your unzipped folder, then run step 4 again):

```ini
[mouse]
name=Logitech G403 HERO   ; what iCUE shows
zones=2                   ; 1 to 10

zone1_name=Scroll wheel   ; label in iCUE
zone1_id=0                ; zone number in Logitech's LED SDK
zone1_shape=wheel         ; where it is drawn: wheel, logo, dpi, side or body

zone2_name=Logo
zone2_id=1
zone2_shape=logo
```

**Mouse not in the table?** Start with `zones=2`, ids `0` and `1`, install, then set zone 1 to red and
zone 2 to blue in iCUE and look at the mouse. Swap ids or names until they match, or try `zones=1`. The plugin
log lists the zones it read (`config: ...`), and typos fall back to safe defaults with a message there.

### Troubleshooting

| Symptom | What to check |
|---|---|
| Mouse does not appear in iCUE | iCUE log `%LOCALAPPDATA%\Corsair\Logs\CUE5\<newest>.log`: search for `logitech_plugin`. `Signature verification failed` means the certificate is not in *LocalMachine\Root* or the DLL was not signed. |
| Mouse appears, lights don't change | Plugin log: `Logitech SDK ready` must be there. `init failed` is normal right after boot, because G HUB starts minutes after iCUE. The plugin retries every 5 s. Check the G HUB "allow applications" setting and that no locked LIGHTSYNC/game profile is active in G HUB. |
| Wrong part of the mouse lights up | Swap the `zoneN_id` values in `plugin.ini` and reinstall. |
| Mouse shows up twice | An old `g403_plugin.dll` from v1.0 is still in iCUE's `plugins\Logitech` folder. Run the installer again. It removes it. |
| ASUS devices lost iCUE colours after installing | Run the ASUS script again. Restarting only iCUE's plugin host without then restarting LightingService leaves Aura's control token with the old process. |
| Murals stopped on all devices | Setting a colour on a single device in iCUE turns Murals off. Click a Mural again. |

### Undo

```powershell
# in Terminal (Admin), from the plugin folder
powershell -ExecutionPolicy Bypass -File .\Install-LogitechPlugin.ps1 -Remove
Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\Root, Cert:\LocalMachine\TrustedPublisher |
    Where-Object Subject -eq 'CN=Local iCUE plugin signing' | Remove-Item
```

### Build it yourself

On Linux/WSL with MinGW-w64 (`apt install gcc-mingw-w64-x86-64 zip`):

```bash
cd logitech-mouse-plugin
./build.sh            # -> dist/iCUE-Logitech-Plugin.zip (unsigned; you sign it during install)
```

`tools/make_images.py` (Pillow) redraws the device pictures and prints the SVG zone outlines used in
`src/logitech_plugin.c`. Add `-DAS_MOUSE` to the gcc line to report the device as a plain mouse instead.
That works for colours but iCUE then leaves it out of Murals.

---

## Do & Don't

<img src="assets/demo-dont-do.gif" alt="DON'T: restarting only the iCUE plugin host leaves Aura's token stale and ASUS falls back to default lighting. DO: the scripts restart in order and iCUE wins the priority check" width="100%">

| ✅ Do this | ❌ Not this |
|---|---|
| Use `Install-LogitechPlugin.ps1`. It restarts plugin host, then LightingService, then iCUE. Already did it? Run `Set-iCUEAuraPriority.ps1`. | Restart only `iCUEDevicePluginHost` (or copy a new plugin DLL in by hand). Aura keeps the old process as owner and your ASUS lights fall back to default. |
| Leave Armoury Crate's services running (iCUE needs them), but don't change lighting in its Aura Sync page. If it grabbed control, run `Set-iCUEAuraPriority.ps1`. | Change lighting in Armoury Crate's **Aura Sync** page while iCUE is in charge. Armoury Crate can take the lights back. |
| Change lighting from the Murals page, or click a Mural again to bring it back. | Set a colour on a single device while you use **Murals**. iCUE switches Murals off for *every* device. |
| Keep G HUB running with *"Allow games & applications to control my lighting"* on. The plugin reconnects by itself when G HUB starts late after boot. | Turn off G HUB's *"Allow games & applications to control my lighting"*, or quit G HUB. |
| Let the plugin be the only SDK client for the mouse. | Run another app that drives the mouse's lighting through the Logitech SDK at the same time. The last one to connect wins. |
| Use the certificate only for this plugin. Remove it when you uninstall (see *Undo*). | Sign files you didn't build or get from this repo with your local certificate. Windows trusts everything it signs. |
| Use **Terminal (Admin)**. The scripts check this first and stop with a clear message before they change anything. | Run the scripts in a normal (non-admin) terminal. |

---

## How it was made

* **ASUS:** by reading Aura's own diagnostic log (`AuraSDK.log`) before and after the SDK update, which
  showed the new priority check and the values it compares, and then watching what each restart order does.
* **Logitech plugin:** iCUE has no public SDK for device plugins. The interface in `src/icue_plugin.h`
  (exported functions, the 19-entry function table, device/LED/view structures) was worked out by studying
  how iCUE's own ASUS and NVIDIA plugins talk to iCUE's plugin host, for the sole purpose of making this
  independent plugin work with iCUE. The mouse side only uses Logitech's documented LED SDK functions.
  A future iCUE update can change the plugin interface. If the plugin stops loading after an update, please
  open an issue with your iCUE log.

---

## License

[MIT](LICENSE)

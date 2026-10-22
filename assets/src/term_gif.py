"""Renders the README's terminal demos as animated GIFs in a Windows Terminal /
PowerShell look: commands are typed character by character, output appears with
realistic pauses. The scenes replay real output from a working setup (paths and
the certificate thumbprint are generic examples).

Usage: python3 assets/src/term_gif.py [scene ...]   (default: all scenes)
Needs Pillow and the Cascadia Mono + Rajdhani fonts.
"""
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent

W, H = 1100, 560
PAD_X, TOP = 18, 52
FONT_SIZE, LINE_H = 15, 21
COLS = (W - 2 * PAD_X) // 9           # Cascadia Mono 15 px is ~9 px wide
ROWS = (H - TOP - 14) // LINE_H

# Windows Terminal "Campbell" scheme
BG = (12, 12, 12)
TAB_BAR = (32, 32, 32)
TAB = (12, 12, 12)
FG = (204, 204, 204)
C = {
    "fg": FG, "dim": (118, 118, 118), "green": (22, 198, 12), "yellow": (249, 241, 165),
    "red": (231, 72, 86), "cyan": (97, 214, 214), "blue": (59, 120, 255), "white": (242, 242, 242),
    "prompt": FG, "warn": (249, 241, 165),
}


def font(name, size):
    p = Path(f"~/.local/share/fonts/{name}").expanduser()
    return ImageFont.truetype(str(p), size) if p.exists() else ImageFont.load_default()


MONO = font("CascadiaMono-Regular.ttf", FONT_SIZE)
MONO_B = font("CascadiaMono-Bold.ttf", FONT_SIZE)
UI = font("Rajdhani-SemiBold.ttf", 15)
BADGE = font("Rajdhani-SemiBold.ttf", 17)


class Term:
    """A tiny terminal model: wrapped coloured lines + a cursor, rendered to frames."""

    def __init__(self, title, badge=None):
        self.title, self.badge = title, badge
        self.lines = [[]]            # list of lines; each line = list of (text, colour, bold)
        self.frames, self.durations = [], []
        self.rng = random.Random(7)

    # -- text model ---------------------------------------------------------
    def _col(self):
        return sum(len(t) for t, _, _ in self.lines[-1])

    def write(self, text, colour="fg", bold=False):
        for ch in text:
            if ch == "\n":
                self.lines.append([])
                continue
            if self._col() >= COLS:
                self.lines.append([])
            line = self.lines[-1]
            if line and line[-1][1] == colour and line[-1][2] == bold:
                line[-1] = (line[-1][0] + ch, colour, bold)
            else:
                line.append((ch, colour, bold))

    # -- rendering ------------------------------------------------------------
    def frame(self, ms, cursor=True):
        img = Image.new("RGB", (W, H), BG)
        d = ImageDraw.Draw(img)
        self._chrome(d)
        visible = self.lines[-ROWS:]
        y = TOP
        for i, line in enumerate(visible):
            x = PAD_X
            for text, colour, bold in line:
                d.text((x, y), text, font=MONO_B if bold else MONO, fill=C[colour])
                x += d.textlength(text, font=MONO_B if bold else MONO)
            if cursor and i == len(visible) - 1:
                d.rectangle((x + 1, y + 2, x + 9, y + LINE_H - 2), fill=FG)
            y += LINE_H
        self.frames.append(img)
        self.durations.append(ms)

    def _chrome(self, d):
        d.rectangle((0, 0, W, 38), fill=TAB_BAR)
        # active tab
        d.rounded_rectangle((8, 6, 360, 44), 8, fill=TAB)
        d.rounded_rectangle((20, 14, 38, 30), 3, fill=(1, 36, 86))
        d.text((23, 13), ">_", font=font("CascadiaMono-Bold.ttf", 11), fill=(255, 255, 255))
        d.text((48, 11), self.title, font=UI, fill=(230, 230, 230))
        d.text((338, 10), "x", font=UI, fill=(150, 150, 150))
        d.text((376, 9), "+", font=font("Rajdhani-SemiBold.ttf", 20), fill=(170, 170, 170))
        # window buttons
        g = (200, 200, 200)
        bx = W - 128
        d.line((bx, 20, bx + 10, 20), fill=g, width=1)                    # minimise
        d.rectangle((bx + 44, 15, bx + 54, 25), outline=g, width=1)        # maximise
        d.line((bx + 88, 15, bx + 98, 25), fill=g, width=1)                # close
        d.line((bx + 88, 25, bx + 98, 15), fill=g, width=1)
        if self.badge:
            text, colour = self.badge
            tw = d.textlength(text, font=BADGE)
            x0 = W - 170 - tw
            d.rounded_rectangle((x0, 8, x0 + tw + 24, 32), 12, fill=colour)
            d.text((x0 + 12, 9), text, font=BADGE, fill=(255, 255, 255))

    # -- scripted actions -------------------------------------------------------
    def prompt(self, cwd):
        self.write(f"PS {cwd}> ", "prompt")

    def type(self, text, colour="fg"):
        for ch in text:
            self.write(ch, colour)
            self.frame(self.rng.randint(28, 70) if ch != " " else self.rng.randint(40, 110))
        self.frame(380)
        self.write("\n")

    def out(self, text="", colour="fg", ms=90, bold=False):
        self.write(text + "\n", colour, bold)
        self.frame(ms, cursor=False)

    def wait(self, ms):
        self.frame(ms, cursor=False)

    def blink(self, seconds=1.6):
        for i in range(int(seconds / 0.5)):
            self.frame(500, cursor=(i % 2 == 0))

    def save(self, name):
        pal = [f.convert("P", palette=Image.ADAPTIVE, colors=48) for f in self.frames]
        path = OUT / name
        pal[0].save(path, save_all=True, append_images=pal[1:], duration=self.durations, loop=0, optimize=True)
        print(f"wrote {path.name}: {len(self.frames)} frames, {sum(self.durations) / 1000:.1f} s, {path.stat().st_size // 1024} KiB")


# ---------------------------------------------------------------------------
# scenes

AURA_DIR = r"C:\Tools\iCUE-Aura"
PLUGIN_DIR = r"C:\Tools\iCUE-G403-Plugin"
AURA_LOG = r'"C:\ProgramData\ASUS\ARMOURY CRATE Diagnosis\LightingService\AuraSDK.log"'
THUMB = "8D2C41A7F0E36B9954C1AE07D3B8F26A1C5E94B0"   # example, not a real certificate


def scene_asus_fix():
    t = Term("Administrator: Windows PowerShell")
    t.out("# ASUS GPU + motherboard stuck on default lighting? Give iCUE the Aura priority.", "dim", 900)
    t.prompt(AURA_DIR)
    t.type(r"powershell -ExecutionPolicy Bypass -File .\Set-iCUEAuraPriority.ps1")
    t.wait(700)
    t.out(r"2026-10-21 16:14:05 iCUEDevicePluginHost.exe priority 1 -> 5 (backup C:\Tools\iCUE-Aura\PriorityList.xml.bak-20261021-161404)", "fg", 2600)
    t.out("2026-10-21 16:14:10 LightingService restarted", "fg", 2400)
    t.out("2026-10-21 16:14:15 iCUE started", "fg", 1200)
    t.out()
    t.out("# ~20 s later, iCUE asks Aura for control. Check that it won:", "dim", 1100)
    t.prompt(AURA_DIR)
    t.type(f'sls -Path {AURA_LOG} -Pattern "m_priority =|setGUID - done" | select -Last 2')
    t.wait(500)
    t.out("", "fg", 60)
    t.out("AuraSDK.log:51022:2026-10-21 16:14:33,966 [AuraSDK][3.08.01] - [guidToken::CheckPriority] m_priority = 5 CurrentPriority = 3.", "green", 500)
    t.out("AuraSDK.log:51031:2026-10-21 16:14:33,985 [AuraSDK][3.08.01] - [AuraToken::switchToken] setGUID - done.", "green", 900)
    t.out()
    t.out("# 5 > 3: iCUE owns your ASUS RGB again.", "cyan", 400, bold=True)
    t.prompt(AURA_DIR)
    t.blink(3)
    t.save("demo-asus-fix.gif")


def scene_logon_task():
    t = Term("Administrator: Windows PowerShell")
    t.out("# LightingService resets PriorityList.xml at every boot -> re-apply the fix at logon.", "dim", 1000)
    t.prompt(AURA_DIR)
    t.type("$script  = 'C:\\Tools\\iCUE-Aura\\Set-iCUEAuraPriority.ps1'")
    t.prompt(AURA_DIR)
    t.type("$action  = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument \"-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `\"$script`\" -AtLogon\"")
    t.prompt(AURA_DIR)
    t.type('$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\\$env:USERNAME"')
    t.prompt(AURA_DIR)
    t.type('$princ   = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\\$env:USERNAME" -LogonType Interactive -RunLevel Highest')
    t.prompt(AURA_DIR)
    t.type("Register-ScheduledTask -TaskName 'iCUE Aura priority' -Action $action -Trigger $trigger -Principal $princ")
    t.wait(900)
    t.out()
    t.out("TaskPath                                       TaskName                          State", "fg", 40)
    t.out("--------                                       --------                          -----", "fg", 40)
    t.out("\\                                              iCUE Aura priority                Ready", "green", 900)
    t.out()
    t.out("# Every logon: wait for iCUE, set priority 5, restart LightingService, then iCUE.", "cyan", 400, bold=True)
    t.prompt(AURA_DIR)
    t.blink(3)
    t.save("demo-logon-task.gif")


def scene_cert():
    t = Term("Administrator: Windows PowerShell")
    t.out("# One time only: a local code-signing certificate so iCUE accepts YOUR plugin build.", "dim", 1000)
    t.prompt(PLUGIN_DIR)
    t.type("$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Local iCUE plugin signing' `")
    t.write(">> ", "prompt")
    t.type("-CertStoreLocation Cert:\\CurrentUser\\My -KeyExportPolicy NonExportable `")
    t.write(">> ", "prompt")
    t.type("-KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -NotAfter (Get-Date).AddYears(10)")
    t.wait(900)
    t.prompt(PLUGIN_DIR)
    t.type('Export-Certificate -Cert $cert -FilePath "$env:TEMP\\icue-plugin-signing.cer" | Out-Null')
    t.prompt(PLUGIN_DIR)
    t.type('Import-Certificate -FilePath "$env:TEMP\\icue-plugin-signing.cer" -CertStoreLocation Cert:\\LocalMachine\\Root')
    t.wait(600)
    t.wait(700)   # elevated import into LocalMachine\Root: no dialog (verified)
    t.out()
    t.out("   PSParentPath: Microsoft.PowerShell.Security\\Certificate::LocalMachine\\Root", "fg", 60)
    t.out()
    t.out("Thumbprint                                Subject", "fg", 40)
    t.out("----------                                -------", "fg", 40)
    t.out(f"{THUMB}  CN=Local iCUE plugin signing", "fg", 800)
    t.out()
    t.prompt(PLUGIN_DIR)
    t.type('Import-Certificate -FilePath "$env:TEMP\\icue-plugin-signing.cer" -CertStoreLocation Cert:\\LocalMachine\\TrustedPublisher | Out-Null')
    t.prompt(PLUGIN_DIR)
    t.type("$cert.Thumbprint")
    t.wait(300)
    t.out(THUMB, "yellow", 900, bold=True)
    t.out()
    t.out("# Copy that thumbprint: the installer signs the plugin with it.", "cyan", 400, bold=True)
    t.prompt(PLUGIN_DIR)
    t.blink(3)
    t.save("demo-cert.gif")


def _security_dialog(t):
    """Windows' root-certificate warning, then the user clicks Yes."""
    base = t.frames[-1].copy()
    for clicked in (False, True):
        img = base.copy()
        d = ImageDraw.Draw(img, "RGBA")
        d.rectangle((0, 0, W, H), fill=(0, 0, 0, 120))
        x0, y0, x1, y1 = 190, 140, 790, 380
        d.rectangle((x0, y0, x1, y1), fill=(243, 243, 243), outline=(160, 160, 160))
        d.text((x0 + 16, y0 + 10), "Security Warning", font=font("Rajdhani-SemiBold.ttf", 18), fill=(20, 20, 20))
        d.ellipse((x0 + 20, y0 + 52, x0 + 56, y0 + 88), fill=(255, 185, 0))
        d.text((x0 + 33, y0 + 54), "!", font=font("CascadiaMono-Bold.ttf", 26), fill=(20, 20, 20))
        body = ("You are about to install a certificate from a certification\n"
                "authority (CA) claiming to represent:\n\n"
                "    Local iCUE plugin signing\n\n"
                "Do you want to install this certificate?")
        d.multiline_text((x0 + 72, y0 + 50), body, font=font("Rajdhani-SemiBold.ttf", 16), fill=(30, 30, 30), spacing=3)
        for i, label in enumerate(("Yes", "No")):
            bx = x1 - 200 + i * 96
            fill = (0, 95, 184) if (label == "Yes") else (253, 253, 253)
            if clicked and label == "Yes":
                fill = (0, 70, 140)
            d.rounded_rectangle((bx, y1 - 46, bx + 84, y1 - 16), 4, fill=fill, outline=(120, 120, 120))
            d.text((bx + 30 if label == "Yes" else bx + 33, y1 - 43), label,
                   font=font("Rajdhani-SemiBold.ttf", 17), fill=(255, 255, 255) if label == "Yes" else (20, 20, 20))
        t.frames.append(img)
        t.durations.append(2600 if not clicked else 350)


def scene_install():
    t = Term("Administrator: Windows PowerShell")
    t.out("# Signs the plugin, copies it into iCUE, restarts in the order Aura needs.", "dim", 1000)
    t.prompt(PLUGIN_DIR)
    t.type(f"powershell -ExecutionPolicy Bypass -File .\\Install-G403Plugin.ps1 -CertThumbprint {THUMB}")
    t.wait(500)
    t.out()
    t.out("iCUE stopped", "fg", 1500)
    t.out("Signed: Valid", "green", 1800)
    t.out("Copied to C:\\Program Files\\Corsair\\Corsair iCUE5 Software\\plugins\\Logitech", "fg", 2600)
    t.out("iCUE plugin host restarted", "fg", 3200)
    t.out("LightingService restarted", "fg", 5200)
    t.out("iCUE started. Give it ~30 seconds.", "fg", 1400)
    t.out()
    t.prompt(PLUGIN_DIR)
    t.type("gc C:\\ProgramData\\iCUE-G403-Plugin\\g403-plugin.log -Tail 6")
    t.wait(300)
    t.out("2026-10-21 16:25:28.709 subscribe (mode 0)", "fg", 40)
    t.out("2026-10-21 16:25:28.709 setMode 2 (was 0)", "fg", 40)
    t.out("2026-10-21 16:25:28.709 announce connected", "fg", 40)
    t.out("2026-10-21 16:25:28.722 getDeviceInfo -> Logitech G403 HERO", "fg", 40)
    t.out("2026-10-21 16:25:28.722 getDeviceView 0", "fg", 40)
    t.out("2026-10-21 16:25:29.220 Logitech SDK ready (after 0 failed attempts)", "green", 900, bold=True)
    t.out()
    t.out("# The G403 is now a device in iCUE: set a colour or drop it on a Mural.", "cyan", 400, bold=True)
    t.prompt(PLUGIN_DIR)
    t.blink(3)
    t.save("demo-install.gif")


def scene_dont_do():
    red, green = (196, 43, 28), (16, 124, 16)
    t = Term("Administrator: Windows PowerShell", badge=("DON'T", red))
    t.out("# DON'T restart only iCUE's plugin host (e.g. to swap the plugin DLL by hand):", "dim", 1100)
    t.prompt(PLUGIN_DIR)
    t.type("Restart-Service iCUEDevicePluginHost -Force")
    t.wait(2600)
    t.prompt(PLUGIN_DIR)
    t.type(f'gc {AURA_LOG} -Tail 4')
    t.wait(300)
    t.out("2026-10-21 15:25:08,509 [AuraSDK][3.08.01] - [AuraToken::requireToken] In.", "fg", 40)
    t.out("2026-10-21 15:25:08,509 [AuraSDK][3.08.01] - [AuraToken::switchToken] In.", "fg", 40)
    t.out("2026-10-21 15:25:08,510 [AuraSDK][3.08.01] - [AuraToken::switchToken] Out.", "red", 40)
    t.out("2026-10-21 15:25:08,510 [AuraSDK][3.08.01] - [AuraToken::requireToken] Out.", "fg", 1200)
    t.out()
    t.out("# No CheckPriority, no setGUID: Aura still thinks the OLD process owns the lights.", "red", 600, bold=True)
    t.out("# Result: ASUS GPU + motherboard drop back to their default effect.", "red", 2600, bold=True)

    t.badge = ("DO", green)
    t.lines = [[]]
    t.out("# DO let the scripts restart things in order: plugin host -> LightingService -> iCUE.", "dim", 1100)
    t.prompt(PLUGIN_DIR)
    t.type(f"powershell -ExecutionPolicy Bypass -File .\\Install-G403Plugin.ps1 -CertThumbprint {THUMB}")
    t.out("iCUE stopped", "fg", 700)
    t.out("Signed: Valid", "green", 700)
    t.out("Copied to C:\\Program Files\\Corsair\\Corsair iCUE5 Software\\plugins\\Logitech", "fg", 900)
    t.out("iCUE plugin host restarted", "fg", 1200)
    t.out("LightingService restarted", "fg", 1800)
    t.out("iCUE started. Give it ~30 seconds.", "fg", 1400)
    t.prompt(PLUGIN_DIR)
    t.type(f'sls -Path {AURA_LOG} -Pattern "m_priority =|setGUID - done" | select -Last 2')
    t.out("AuraSDK.log:51988:2026-10-21 16:25:33,658 [AuraSDK][3.08.01] - [guidToken::CheckPriority] m_priority = 5 CurrentPriority = 3.", "green", 400)
    t.out("AuraSDK.log:51997:2026-10-21 16:25:33,676 [AuraSDK][3.08.01] - [AuraToken::switchToken] setGUID - done.", "green", 900)
    t.out()
    t.out("# iCUE owns ASUS again. (Fixing it after a mistake: just run Set-iCUEAuraPriority.ps1.)", "cyan", 400, bold=True)
    t.prompt(PLUGIN_DIR)
    t.blink(3)
    t.save("demo-dont-do.gif")


SCENES = {
    "asus-fix": scene_asus_fix,
    "logon-task": scene_logon_task,
    "cert": scene_cert,
    "install": scene_install,
    "dont-do": scene_dont_do,
}

if __name__ == "__main__":
    for name in sys.argv[1:] or SCENES:
        SCENES[name]()

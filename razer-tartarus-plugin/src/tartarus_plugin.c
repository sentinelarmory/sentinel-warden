/* tartarus_plugin.c - iCUE device plugin that exposes a Razer Tartarus V2 as an iCUE
 * device (a 4x5 key grid) and forwards iCUE's colours to it over the Razer control
 * channel (HID interface mi_02, zone 0x05 = backlight). Same shape as the Logitech
 * plugin: iCUE owns the lighting (effects, Murals), this DLL only carries it to the keypad.
 *
 * LIGHTING ONLY: the only report ever sent is the extended-matrix static colour for the
 * backlight zone, volatile (nothing is stored). Key mappings, macros and profiles belong to
 * Synapse and are never touched.
 *
 * The Tartarus has one backlight zone, so the 22 LEDs (20 keys, thumbpad, wheel) are averaged into one colour
 * (plugin.ini [tartarus] led=N follows a single key instead).
 *
 * Files next to the DLL: plugin.ini (optional), device_view.png, mask.png, thumbnail.png, promo.png.
 * Log: C:\ProgramData\iCUE-Razer-Plugin\plugin.log */
#include <windows.h>
#include <mmsystem.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "icue_plugin.h"

#define EXPORT __declspec(dllexport)

/* Murals only offers plugin devices of the GPU type, so report a GPU-type device. */
#define DEVICE_ID "74617274617275737632706c7567696e31" /* "tartarusv2plugin1" in hex */
#define DEVICE_NAME "Razer Tartarus V2"
#define PLUGIN_DEVICE_TYPE CPDT_GPU
#define LED_ID(i) CPLGPU_ZONE((i) + 1)
#define ROWS 4
#define COLS 5
#define HW_COLS 6           /* columns the firmware accepts (key 20 sits in the 6th) */
#define NKEYS (ROWS * COLS)
#define NLEDS (NKEYS + 2)      /* 20 keys + thumbpad + scroll wheel (one backlight zone, so these two are virtual) */
#define REPORT_LEN 91          /* report id 0 + 90 bytes */
#define REFRESH_MS 1000        /* re-send unchanged colour: Synapse repaints its own effect every ~5 s */
#define MIN_GAP_MS 20          /* at most ~50 frames/s; iCUE hands over ~21/s, so this never throttles */

/* device_view.png geometry (tools/make_tartarus_images.py): key grid origin and pitch, in pixels */
#define GRID_X0 205
#define GRID_Y0 72
#define KEY_W 40
#define KEY_H 40
#define KEY_GAP 14

typedef struct { int r, g, b; } Rgb;

static struct {
    CRITICAL_SECTION lock;
    volatile LONG mode;
    void *cb_context;
    CPluginConnectionCallback cb;
    Rgb led[NLEDS];
    int dirty;
    HANDLE wake, worker;
    volatile LONG stop;
    int source_led;              /* 0 = average of all keys, 1..20 = one key */
    char dir[MAX_PATH];          /* plugin folder, UTF-8, trailing backslash */
    char hash_view[65], hash_thumb[65], hash_promo[65], hash_mask[65];
} g;

static HANDLE g_dev = INVALID_HANDLE_VALUE;
static volatile unsigned long g_sets;   /* how often iCUE has handed us colours (for the log) */

static void log_line(const char *fmt, ...)
{
    CreateDirectoryA("C:\\ProgramData\\iCUE-Razer-Plugin", NULL);
    FILE *f = fopen("C:\\ProgramData\\iCUE-Razer-Plugin\\plugin.log", "a");
    if (!f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* ---- image hashes ------------------------------------------------------------ */

static int sha256_file(const char *path, char out_hex[65])
{
    out_hex[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE h = NULL;
    unsigned char digest[32], buf[8192];
    int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0 &&
        BCryptCreateHash(alg, &h, NULL, 0, NULL, 0, 0) == 0) {
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) BCryptHashData(h, buf, (ULONG)n, 0);
        if (BCryptFinishHash(h, digest, sizeof(digest), 0) == 0) {
            for (int i = 0; i < 32; i++) sprintf(out_hex + i * 2, "%02x", digest[i]);
            ok = 1;
        }
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    fclose(f);
    return ok;
}

static void hash_images(void)
{
    static const struct { const char *name; char *out; } files[] = {
        { "device_view.png", g.hash_view }, { "thumbnail.png", g.hash_thumb },
        { "promo.png", g.hash_promo },      { "mask.png", g.hash_mask },
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s%s", g.dir, files[i].name);
        if (!sha256_file(path, files[i].out)) log_line("cannot hash %s", path);
    }
}

static CPluginImage *make_image(const char *name, const char *hash)
{
    CPluginImage *img = calloc(1, sizeof(*img));
    img->path = _strdup(name);
    img->hash = _strdup(hash);
    return img;
}

static void free_image(CPluginImage *img)
{
    if (!img) return;
    free(img->path);
    free(img->hash);
    free(img);
}

/* ---- Razer control channel ---------------------------------------------------- */

/* The control interface is a "mouse" collection: read/write access gives err 5, but a
 * zero-access handle still carries feature reports (verified 2026-11-08). */
static HANDLE open_control(void)
{
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
    SP_DEVICE_INTERFACE_DATA di;
    memset(&di, 0, sizeof(di));
    di.cbSize = sizeof(di);
    HANDLE found = INVALID_HANDLE_VALUE;
    for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, NULL, &hid, i, &di); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &di, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *d = malloc(need);
        if (!d) continue;
        d->cbSize = sizeof(*d);
        if (SetupDiGetDeviceInterfaceDetailW(set, &di, d, need, NULL, NULL) &&
            wcsstr(d->DevicePath, L"vid_1532&pid_022b&mi_02"))
            found = CreateFileW(d->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        free(d);
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

/* Razer 90-byte report: status, transaction id, remaining(2), protocol, size, class, id, args[80], crc, reserved.
 * Extended matrix static colour (class 0x0F id 0x02): NOSTORE, BACKLIGHT zone 0x05, effect 1, one colour. */
static int send_static(HANDLE h, Rgb c)
{
    unsigned char b[REPORT_LEN];
    memset(b, 0, sizeof(b));
    unsigned char *r = b + 1;
    r[1] = 0xFF;
    r[5] = 0x09;
    r[6] = 0x0F;
    r[7] = 0x02;
    static const unsigned char head[6] = { 0x00, 0x05, 0x01, 0x00, 0x00, 0x01 };
    memcpy(r + 8, head, sizeof(head));
    r[14] = (unsigned char)c.r;
    r[15] = (unsigned char)c.g;
    r[16] = (unsigned char)c.b;
    unsigned char crc = 0;
    for (int i = 2; i < 88; i++) crc ^= r[i];
    r[88] = crc;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (!HidD_SetFeature(h, b, REPORT_LEN)) return 0;
        Sleep(12);
        unsigned char resp[REPORT_LEN];
        memset(resp, 0, sizeof(resp));
        if (!HidD_GetFeature(h, resp, REPORT_LEN)) return 0;
        if (resp[1] == 2) return 1;
        if (resp[1] == 1 || resp[1] == 0) { Sleep(15); continue; } /* busy / not processed yet */
        log_line("device answered status %u", resp[1]);
        return 0;
    }
    return 1; /* still busy after retries: not a connection failure */
}

/* Custom frame (what Chroma apps use): the static effect makes the firmware fade to every new
 * colour, which blends fast changes into a steady tint (measured 2026-11-08: red/blue every 500 ms
 * showed as fading purple). A custom frame is shown at once: rows of colours (class 0x0F id 0x03,
 * 4 rows x 5 columns), then "apply effect 0x08" (class 0x0F id 0x02).
 * Sends are back to back (the firmware takes them without an answer between); the answer is only
 * read every 25th frame to notice problems. */
static int put_report(HANDLE h, unsigned char cls, unsigned char id, unsigned char size,
                      const unsigned char *args, int nargs, int read_answer)
{
    unsigned char b[REPORT_LEN];
    memset(b, 0, sizeof(b));
    unsigned char *r = b + 1;
    r[1] = 0xFF;
    r[5] = size;
    r[6] = cls;
    r[7] = id;
    memcpy(r + 8, args, (size_t)nargs);
    unsigned char crc = 0;
    for (int i = 2; i < 88; i++) crc ^= r[i];
    r[88] = crc;
    if (!HidD_SetFeature(h, b, REPORT_LEN)) return 0;
    if (!read_answer) { Sleep(2); return 1; }
    Sleep(12);
    unsigned char resp[REPORT_LEN];
    memset(resp, 0, sizeof(resp));
    if (!HidD_GetFeature(h, resp, REPORT_LEN)) return 0;
    if (resp[1] == 2 || resp[1] == 1 || resp[1] == 0) return 1; /* ok / busy: not a connection failure */
    log_line("device answered status %u", resp[1]);
    return 0;
}

static int send_frame(HANDLE h, const Rgb keys[NLEDS], int check)
{
    /* The firmware matrix is 6 columns wide: column 5 holds key 20 (the thumb button). */
    for (int row = 0; row < ROWS; row++) {
        unsigned char a[5 + HW_COLS * 3];
        memset(a, 0, sizeof(a));
        a[2] = (unsigned char)row; a[3] = 0; a[4] = HW_COLS - 1;
        for (int col = 0; col < HW_COLS; col++) {
            const Rgb *k = &keys[row * COLS + (col < COLS ? col : COLS - 1)];
            a[5 + col * 3] = (unsigned char)k->r; a[6 + col * 3] = (unsigned char)k->g; a[7 + col * 3] = (unsigned char)k->b;
        }
        if (!put_report(h, 0x0F, 0x03, HW_COLS * 3 + 5, a, (int)sizeof(a), check)) return 0;
    }
    const unsigned char apply[12] = { 0x00, 0x05, 0x08 }; /* NOSTORE, BACKLIGHT, custom frame */
    return put_report(h, 0x0F, 0x02, 0x0C, apply, 12, check);
}

static int g_boost = 0;
static int g_frame_mode = 1;     /* 1 = custom frame (default), 0 = static effect (fades) */
static int g_per_key = 0;        /* 1 = send each key's own iCUE colour instead of one averaged colour */
static int g_smooth_ms = 35;     /* time constant of the glide between iCUE frames (iCUE sends only ~17-21/s); 0 = off */

static void load_config(void)
{
    char ini[MAX_PATH];
    snprintf(ini, sizeof(ini), "%splugin.ini", g.dir);
    int led = (int)GetPrivateProfileIntA("tartarus", "led", 0, ini);
    g.source_led = (led >= 1 && led <= NLEDS) ? led : 0;
    g_boost = GetPrivateProfileIntA("tartarus", "boost", 0, ini);
    g_frame_mode = GetPrivateProfileIntA("tartarus", "frame", 1, ini);
    g_per_key = GetPrivateProfileIntA("tartarus", "per_key", 0, ini);
    g_smooth_ms = GetPrivateProfileIntA("tartarus", "smooth_ms", 35, ini);
    if (g_smooth_ms < 0 || g_smooth_ms > 500) g_smooth_ms = 35;
    log_line("config: %s, boost=%d, frame=%d, per_key=%d, smooth_ms=%d", g.source_led ? "single key" : "average of all keys",
             g_boost, g_frame_mode, g_per_key, g_smooth_ms);
}

/* The keypad is small and iCUE's colour at its spot on a Mural can be dark and move little.
 * boost=1 keeps the hue but stretches brightness over the range seen during the last ~10 s,
 * so the pulse is clearly visible (decays toward the current value). */
static Rgb boost_colour(Rgb c)
{
    static float lo = 255.f, hi = 0.f;
    float v = (float)(c.r > c.g ? (c.r > c.b ? c.r : c.b) : (c.g > c.b ? c.g : c.b));
    hi = v > hi ? v : hi + (v - hi) * 0.02f;
    lo = v < lo ? v : lo + (v - lo) * 0.02f;
    if (v > 0.f && hi - lo >= 12.f) {
        float k = (25.f + (v - lo) / (hi - lo) * 230.f) / v; /* never fully dark, peaks at full */
        c.r = (int)(c.r * k + 0.5f); c.g = (int)(c.g * k + 0.5f); c.b = (int)(c.b * k + 0.5f);
        if (c.r > 255) c.r = 255;
        if (c.g > 255) c.g = 255;
        if (c.b > 255) c.b = 255;
    }
    return c;
}

static Rgb current_colour(const Rgb *led)
{
    Rgb sum = { 0, 0, 0 };
    if (g.source_led) return led[g.source_led - 1];
    for (int i = 0; i < NLEDS; i++) { sum.r += led[i].r; sum.g += led[i].g; sum.b += led[i].b; }
    sum.r /= NLEDS; sum.g /= NLEDS; sum.b /= NLEDS;
    return sum;
}

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    timeBeginPeriod(1); /* default timer tick is ~15.6 ms: Sleep(2) would last 15 ms and a 5-command frame ~80 ms */
    DWORD next_try = 0, last_write = 0, last_stat = 0;
    unsigned long writes = 0, frames = 0;
    double frame_ms_sum = 0, frame_ms_max = 0;
    LARGE_INTEGER qpf;
    QueryPerformanceFrequency(&qpf);
    Rgb last = { -1, -1, -1 };
    Rgb raw = { 0, 0, 0 }, target = { 0, 0, 0 };
    int have_target = 0, have_disp = 0;
    float disp[3] = { 0, 0, 0 };
    LARGE_INTEGER tprev = { 0 };
    DWORD wait_ms = 250;
    while (!g.stop) {
        WaitForSingleObject(g.wake, wait_ms);
        if (g.stop) break;

        if (g.mode != CPM_ATTACHED) {
            if (g_dev != INVALID_HANDLE_VALUE) {
                CloseHandle(g_dev);
                g_dev = INVALID_HANDLE_VALUE;
                log_line("detached: lighting handed back to Synapse");
            }
            continue;
        }
        if (g_dev == INVALID_HANDLE_VALUE) {
            if (GetTickCount() < next_try) continue;
            g_dev = open_control();
            if (g_dev == INVALID_HANDLE_VALUE) { next_try = GetTickCount() + 5000; continue; } /* unplugged / not ready */
            log_line("Tartarus control channel open");
            last.r = -1;
        }

        Rgb leds[NLEDS];
        int fresh;
        EnterCriticalSection(&g.lock);
        fresh = g.dirty;
        g.dirty = 0;
        memcpy(leds, g.led, sizeof(leds));
        LeaveCriticalSection(&g.lock);
        if (fresh || !have_target) {
            /* boost only on new iCUE data, so its range memory does not depend on our own tick rate */
            raw = current_colour(leds);
            target = g_boost ? boost_colour(raw) : raw;
            have_target = 1;
        }
        Rgb c = target;
        int converging = 0;
        if (g_smooth_ms > 0 && !g_per_key) {
            /* glide toward iCUE's latest colour at our own tick rate: iCUE hands over ~17-21 frames/s,
             * a glide fills in the steps between them (costs about one time constant of delay) */
            LARGE_INTEGER tn;
            QueryPerformanceCounter(&tn);
            float dt = have_disp ? (float)((double)(tn.QuadPart - tprev.QuadPart) * 1000.0 / (double)qpf.QuadPart) : 1000.f;
            tprev = tn;
            if (!have_disp || dt > 500.f) {
                disp[0] = (float)target.r; disp[1] = (float)target.g; disp[2] = (float)target.b;
                have_disp = 1;
            } else {
                float alpha = 1.f - expf(-dt / (float)g_smooth_ms);
                disp[0] += ((float)target.r - disp[0]) * alpha;
                disp[1] += ((float)target.g - disp[1]) * alpha;
                disp[2] += ((float)target.b - disp[2]) * alpha;
            }
            c.r = (int)(disp[0] + 0.5f); c.g = (int)(disp[1] + 0.5f); c.b = (int)(disp[2] + 0.5f);
            converging = abs(c.r - target.r) > 1 || abs(c.g - target.g) > 1 || abs(c.b - target.b) > 1;
        }
        wait_ms = converging ? 3 : 250;

        DWORD now = GetTickCount();
        int changed = memcmp(&c, &last, sizeof(Rgb)) != 0;
        if (!changed && now - last_write < REFRESH_MS) continue;
        if (now - last_write < MIN_GAP_MS) { Sleep(MIN_GAP_MS - (now - last_write)); }

        int sent;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        if (g_frame_mode) {
            Rgb keys[NLEDS];
            for (int i = 0; i < NLEDS; i++) keys[i] = g_per_key ? leds[i] : c;
            sent = send_frame(g_dev, keys, (frames++ % 25) == 0);
        } else {
            sent = send_static(g_dev, c);
        }
        QueryPerformanceCounter(&t1);
        {
            double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qpf.QuadPart;
            frame_ms_sum += ms;
            if (ms > frame_ms_max) frame_ms_max = ms;
        }
        if (!sent) {
            log_line("write failed, reopening");
            CloseHandle(g_dev);
            g_dev = INVALID_HANDLE_VALUE;
            next_try = GetTickCount() + 2000;
            continue;
        }
        last = c;
        last_write = GetTickCount();
        writes++;
        if (last_write - last_stat >= 10000) {
            log_line("stats: %lu writes/10s (frame avg %.1f ms, max %.0f ms), input rgb(%d,%d,%d) -> sent rgb(%d,%d,%d), iCUE colour calls %lu",
                     writes, writes ? frame_ms_sum / (double)writes : 0.0, frame_ms_max, raw.r, raw.g, raw.b, c.r, c.g, c.b, g_sets);
            writes = 0;
            frame_ms_sum = 0;
            frame_ms_max = 0;
            last_stat = last_write;
        }
    }
    if (g_dev != INVALID_HANDLE_VALUE) { CloseHandle(g_dev); g_dev = INVALID_HANDLE_VALUE; }
    timeEndPeriod(1);
    return 0;
}

/* ---- plugin slots ------------------------------------------------------------- */

static void key_rect(char *out, size_t n, int i)
{
    int row = i / COLS, col = i % COLS;
    int x = GRID_X0 + col * (KEY_W + KEY_GAP), y = GRID_Y0 + row * (KEY_H + KEY_GAP);
    int w = KEY_W, h = KEY_H;
    if (i == NKEYS)     { x = GRID_X0;       y = GRID_Y0 + ROWS * (KEY_H + KEY_GAP) + 6; w = 2 * KEY_W + KEY_GAP; }  /* thumbpad */
    if (i == NKEYS + 1) { x = GRID_X0 + 3 * (KEY_W + KEY_GAP); y = GRID_Y0 + ROWS * (KEY_H + KEY_GAP) + 6; }       /* scroll wheel */
    snprintf(out, n, "M %d %d L %d %d L %d %d L %d %d Z", x, y, x + w, y, x + w, y + h, x, y + h);
}

static CPluginDeviceInfo *get_device_info(const char *device_id)
{
    if (g.mode != CPM_ATTACHED || !device_id || strcmp(device_id, DEVICE_ID) != 0) return NULL;
    if (!g.hash_view[0]) hash_images();

    CPluginDeviceInfo *info = calloc(1, sizeof(*info));
    info->name = _strdup(DEVICE_NAME);
    info->type = PLUGIN_DEVICE_TYPE;
    info->id = _strdup(DEVICE_ID);
    info->view_count = 1;
    info->image1 = make_image("thumbnail.png", g.hash_thumb);
    info->image2 = make_image("promo.png", g.hash_promo);

    info->leds = calloc(1, sizeof(*info->leds));
    info->leds->count = NLEDS;
    info->leds->items = calloc(NLEDS, sizeof(CPluginLedPosition));
    for (int i = 0; i < NLEDS; i++) {
        /* key pitch ~1.1 cm on a ~6 x 5 cm pad */
        double cx = 0.6 + (i % COLS) * 1.1, cy = 0.6 + (i / COLS) * 1.1;
        if (i == NKEYS)     { cx = 0.6 + 0.55; cy = 0.6 + ROWS * 1.1 + 0.1; }
        if (i == NKEYS + 1) { cx = 0.6 + 3 * 1.1; cy = 0.6 + ROWS * 1.1 + 0.1; }
        info->leds->items[i] = (CPluginLedPosition){ LED_ID(i), cx, cy, CPLP_CM };
    }
    log_line("getDeviceInfo -> %s", DEVICE_NAME);
    return info;
}

static void free_device_info(CPluginDeviceInfo *info)
{
    if (!info) return;
    free(info->name);
    free(info->id);
    free_image(info->image1);
    free_image(info->image2);
    if (info->leds) free(info->leds->items);
    free(info->leds);
    free(info);
}

static CPluginDeviceView *get_device_view(const char *device_id, int index)
{
    if (g.mode != CPM_ATTACHED || !device_id || strcmp(device_id, DEVICE_ID) != 0 || index != 0) return NULL;
    if (!g.hash_view[0]) hash_images();

    CPluginDeviceView *view = calloc(1, sizeof(*view));
    view->image1 = make_image("mask.png", g.hash_mask);
    view->image2 = make_image("device_view.png", g.hash_view);
    view->led_zones = calloc(1, sizeof(*view->led_zones));
    view->led_zones->count = NLEDS;
    view->led_zones->items = calloc(NLEDS, sizeof(CPluginLedZone));
    for (int i = 0; i < NLEDS; i++) {
        char path[160], label[16];
        key_rect(path, sizeof(path), i);
        if (i < NKEYS) snprintf(label, sizeof(label), "Key %02d", i + 1);
        else snprintf(label, sizeof(label), "%s", i == NKEYS ? "Thumbpad" : "Wheel");
        view->led_zones->items[i] = (CPluginLedZone){ LED_ID(i), _strdup(path), CPLZ_SHAPE, _strdup(label) };
    }
    log_line("getDeviceView %d", index);
    return view;
}

static void free_device_view(CPluginDeviceView *view)
{
    if (!view) return;
    free_image(view->image1);
    free_image(view->image2);
    if (view->led_zones) {
        for (int i = 0; i < view->led_zones->count; i++) {
            free(view->led_zones->items[i].shape);
            free(view->led_zones->items[i].text);
        }
        free(view->led_zones->items);
        free(view->led_zones);
    }
    free(view);
}

static void set_leds_colors(const char *device_id, int count, const CPluginLedColor *colors)
{
    if (!device_id || strcmp(device_id, DEVICE_ID) != 0 || !colors) return;
    g_sets++;
    EnterCriticalSection(&g.lock);
    for (int i = 0; i < count; i++) {
        int k = colors[i].led_id - LED_ID(0);
        if (k < 0 || k >= NLEDS) continue;
        Rgb c = { colors[i].r, colors[i].g, colors[i].b };
        if (memcmp(&c, &g.led[k], sizeof(c))) { g.led[k] = c; g.dirty = 1; }
    }
    int dirty = g.dirty;
    LeaveCriticalSection(&g.lock);
    if (dirty) SetEvent(g.wake);
}

static void announce(int connected)
{
    CPluginConnectionCallback cb;
    void *ctx;
    EnterCriticalSection(&g.lock);
    cb = g.cb;
    ctx = g.cb_context;
    LeaveCriticalSection(&g.lock);
    if (cb) {
        log_line("announce %s", connected ? "connected" : "disconnected");
        cb(ctx, DEVICE_ID, connected);
    }
}

static void subscribe_connection(void *context, CPluginConnectionCallback cb)
{
    EnterCriticalSection(&g.lock);
    g.cb_context = context;
    g.cb = cb;
    LeaveCriticalSection(&g.lock);
    log_line("subscribe (mode %ld)", g.mode);
    if (g.mode == CPM_ATTACHED) announce(1);
}

static void unsubscribe_connection(void)
{
    EnterCriticalSection(&g.lock);
    g.cb = NULL;
    g.cb_context = NULL;
    LeaveCriticalSection(&g.lock);
    log_line("unsubscribe");
}

static void set_mode(int mode)
{
    LONG old = InterlockedExchange(&g.mode, mode);
    log_line("setMode %d (was %ld)", mode, old);
    SetEvent(g.wake);
    if (mode == CPM_ATTACHED && old != CPM_ATTACHED) announce(1);
}

/* ---- exports -------------------------------------------------------------------- */

EXPORT int CorsairPluginGetAPIVersion(void) { return CPLUGIN_API_VERSION; }
EXPORT int CorsairPluginGetFeatures(void) { return CPF_DETACHED_MODE; }

EXPORT CPluginInstance *CorsairPluginGetInstance(void)
{
    CPluginInstance *inst = calloc(1, sizeof(*inst));
    if (!inst) return NULL;
    load_config();
    inst->get_device_info = get_device_info;
    inst->set_leds_colors = set_leds_colors;
    inst->subscribe_connection = subscribe_connection;
    inst->unsubscribe_connection = unsubscribe_connection;
    inst->get_device_view = get_device_view;
    inst->free_device_info = free_device_info;
    inst->free_device_view = free_device_view;
    inst->set_mode = set_mode;

    if (!g.worker) {
        /* Pin this DLL: the worker can be blocked in a HID call when the host unloads us,
         * and unmapping its code would hang the plugin host. */
        HMODULE self;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCWSTR)(void *)CorsairPluginGetInstance, &self);
        g.stop = 0;
        g.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        g.worker = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    }
    log_line("GetInstance (plugin dir %s)", g.dir);
    return inst;
}

EXPORT void CorsairPluginFreeInstance(CPluginInstance *inst)
{
    if (g.worker) {
        g.stop = 1;
        SetEvent(g.wake);
        WaitForSingleObject(g.worker, 3000);
        CloseHandle(g.worker);
        CloseHandle(g.wake);
        g.worker = NULL;
    }
    free(inst);
    log_line("FreeInstance");
}

BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(dll);
        InitializeCriticalSection(&g.lock);
        DWORD n = GetModuleFileNameA(dll, g.dir, MAX_PATH);
        while (n > 0 && g.dir[n - 1] != '\\') n--;
        g.dir[n] = 0;
    }
    return TRUE;
}

/* logitech_plugin.c - iCUE device plugin that exposes a Logitech mouse as an
 * iCUE device with up to 10 lighting zones and forwards iCUE's colours to it
 * through the Logitech LED SDK that G HUB installs. G HUB stays the driver.
 *
 * Files next to the DLL: plugin.ini (mouse name + zones; defaults to the
 * G403 HERO), device_view.png, mask.png, thumbnail.png, promo.png.
 * Log: C:\ProgramData\iCUE-Logitech-Plugin\plugin.log */
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "icue_plugin.h"

#define EXPORT __declspec(dllexport)

/* Murals (canvas) only offers plugin devices of the GPU type, not the mouse
 * type, so we report a GPU-type device. Build with -DAS_MOUSE for the mouse type. */
#ifdef AS_MOUSE
#define DEVICE_ID "6c6f676974656368673430336865726f" /* "logitechg403hero" in hex */
#define PLUGIN_DEVICE_TYPE CPDT_MOUSE
#define LED_ID(i) CPLM_ZONE((i) + 1)
#else
#define DEVICE_ID "6c6f6769673430336865726f636e7631" /* "logig403herocnv1" in hex */
#define PLUGIN_DEVICE_TYPE CPDT_GPU
#define LED_ID(i) CPLGPU_ZONE((i) + 1)
#endif
#define MAX_ZONES 10

/* Where a zone is drawn on device_view.png (SVG path in its pixels, see
 * tools/make_images.py) and where its LED sits on the mouse (cm). */
static const struct { const char *key, *path; double cx, cy; } SHAPES[] = {
    { "wheel", "M 299 70 L 316 70 L 316 130 L 299 130 Z", 3.4, 2.5 },
    { "logo",  "M 333 286 L 331 296 L 325 304 L 317 310 L 307 312 L 297 310 L 289 304 "
               "L 283 296 L 281 286 L 283 276 L 289 268 L 297 262 L 307 260 L 317 262 "
               "L 325 268 L 331 276 Z", 3.4, 8.8 },
    { "dpi",   "M 299 142 L 316 142 L 316 158 L 299 158 Z", 3.4, 4.2 },
    { "side",  "M 233 150 L 245 150 L 245 300 L 233 300 Z", 0.4, 6.0 },
    { "body",  "M 300 24 L 315 24 L 360 40 L 385 94 L 385 314 L 360 368 L 315 384 "
               "L 300 384 L 255 368 L 230 314 L 230 94 L 255 40 Z", 3.4, 5.5 },
};
#define SHAPE_COUNT (int)(sizeof(SHAPES) / sizeof(SHAPES[0]))

#define LOGI_SDK_CLSID_KEY L"SOFTWARE\\Classes\\CLSID\\{a6519e67-7632-4375-afdf-caa889744403}\\ServerBinary"
#define LOGI_DEVICETYPE_RGB 2
#define LOGI_MOUSE 0x3

typedef unsigned char logi_bool; /* C++ bool in the SDK ABI */

typedef struct { int r, g, b; } Rgb;

/* plugin.ini [mouse]; the defaults are the G403 HERO / G703 */
static struct {
    char name[64];
    int count;
    struct { char name[32]; int sdk_id; int shape; } zone[MAX_ZONES];
} cfg;

static struct {
    CRITICAL_SECTION lock;
    volatile LONG mode;
    void *cb_context;
    CPluginConnectionCallback cb;
    Rgb zone[MAX_ZONES];
    int dirty;
    HANDLE wake, worker;
    volatile LONG stop;
    char dir[MAX_PATH];          /* plugin folder, UTF-8, trailing backslash */
    char hash_view[65], hash_thumb[65], hash_promo[65], hash_mask[65];
} g;

static struct {
    HMODULE dll;
    logi_bool (*InitWithName)(const char *);
    logi_bool (*SetTargetDevice)(int);
    logi_bool (*SetZone)(int, int, int, int, int);
    void (*Shutdown)(void);
    int ready;
} logi;

static void log_line(const char *fmt, ...)
{
    CreateDirectoryA("C:\\ProgramData\\iCUE-Logitech-Plugin", NULL);
    FILE *f = fopen("C:\\ProgramData\\iCUE-Logitech-Plugin\\plugin.log", "a");
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

/* ---- Logitech side ------------------------------------------------------------ */

static int logi_load(void)
{
    wchar_t path[MAX_PATH];
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, LOGI_SDK_CLSID_KEY, NULL, RRF_RT_REG_SZ, NULL, path, &size) != ERROR_SUCCESS)
        return 0;
    logi.dll = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!logi.dll) return 0;
    logi.InitWithName = (void *)GetProcAddress(logi.dll, "LogiLedInitWithName");
    logi.SetTargetDevice = (void *)GetProcAddress(logi.dll, "LogiLedSetTargetDevice");
    logi.SetZone = (void *)GetProcAddress(logi.dll, "LogiLedSetLightingForTargetZone");
    logi.Shutdown = (void *)GetProcAddress(logi.dll, "LogiLedShutdown");
    return logi.InitWithName && logi.SetTargetDevice && logi.SetZone && logi.Shutdown;
}

/* A failed init (e.g. at boot, before G HUB is running) leaves the SDK unable to
 * connect later, so every retry starts from a freshly loaded DLL. */
static void logi_unload(void)
{
    if (logi.dll) {
        if (logi.Shutdown) logi.Shutdown();
        FreeLibrary(logi.dll);
    }
    memset(&logi, 0, sizeof(logi));
}

static int pct(int c) { return (c * 100 + 127) / 255; }

static int shape_index(const char *key)
{
    for (int i = 0; i < SHAPE_COUNT; i++)
        if (_stricmp(key, SHAPES[i].key) == 0) return i;
    return -1;
}

/* Reads plugin.ini [mouse] next to the DLL:
 *   name=Logitech G403 HERO
 *   zones=2
 *   zone1_name=Scroll wheel   zone1_id=0   zone1_shape=wheel
 *   zone2_name=Logo           zone2_id=1   zone2_shape=logo
 * zoneN_id is the zone number in Logitech's LED SDK; zoneN_shape is one of
 * wheel, logo, dpi, side, body. Missing file or keys = G403 defaults. */
static void load_config(void)
{
    static const struct { const char *name; int id; const char *shape; } defaults[] = {
        { "Scroll wheel", 0, "wheel" }, { "Logo", 1, "logo" },
    };
    char ini[MAX_PATH], key[32], shape[16];
    snprintf(ini, sizeof(ini), "%splugin.ini", g.dir);

    GetPrivateProfileStringA("mouse", "name", "Logitech G403 HERO", cfg.name, sizeof(cfg.name), ini);
    int count = GetPrivateProfileIntA("mouse", "zones", 2, ini);
    if (count < 1 || count > MAX_ZONES) {
        log_line("plugin.ini: zones=%d is out of range 1-%d, using 2", count, MAX_ZONES);
        count = 2;
    }
    cfg.count = 0;
    for (int i = 0; i < count; i++) {
        const char *def_name = i < 2 ? defaults[i].name : "Zone";
        const char *def_shape = i < 2 ? defaults[i].shape : "body";
        int def_id = i < 2 ? defaults[i].id : i;

        snprintf(key, sizeof(key), "zone%d_name", i + 1);
        GetPrivateProfileStringA("mouse", key, def_name, cfg.zone[cfg.count].name, sizeof(cfg.zone[0].name), ini);
        snprintf(key, sizeof(key), "zone%d_id", i + 1);
        cfg.zone[cfg.count].sdk_id = GetPrivateProfileIntA("mouse", key, def_id, ini);
        snprintf(key, sizeof(key), "zone%d_shape", i + 1);
        GetPrivateProfileStringA("mouse", key, def_shape, shape, sizeof(shape), ini);
        cfg.zone[cfg.count].shape = shape_index(shape);
        if (cfg.zone[cfg.count].shape < 0) {
            log_line("plugin.ini: zone%d_shape=%s is unknown (wheel, logo, dpi, side, body), using body", i + 1, shape);
            cfg.zone[cfg.count].shape = shape_index("body");
        }
        cfg.count++;
    }
    log_line("config: \"%s\", %d zone(s)", cfg.name, cfg.count);
    for (int i = 0; i < cfg.count; i++)
        log_line("  zone %d: \"%s\" -> SDK zone %d, shape %s", i + 1, cfg.zone[i].name, cfg.zone[i].sdk_id,
                 SHAPES[cfg.zone[i].shape].key);
}

static DWORD WINAPI worker(void *arg)
{
    (void)arg;
    DWORD next_try = 0;
    int failures = 0;
    while (!g.stop) {
        WaitForSingleObject(g.wake, 1000);
        if (g.stop) break;

        if (g.mode != CPM_ATTACHED) {
            if (logi.ready) { logi.Shutdown(); logi.ready = 0; log_line("detached: lighting handed back to G HUB"); }
            continue;
        }
        if (!logi.ready) {
            if (GetTickCount() < next_try) continue;
            if (!logi.dll && !logi_load()) {
                log_line("Logitech LED SDK not found (G HUB installed?)");
                logi_unload();
                next_try = GetTickCount() + 30000;
                continue;
            }
            if (logi.InitWithName("iCUE G403 plugin")) {
                Sleep(500); /* the SDK connects to G HUB asynchronously */
                logi.SetTargetDevice(LOGI_DEVICETYPE_RGB);
                logi.ready = 1;
                log_line("Logitech SDK ready (after %d failed attempts)", failures);
                failures = 0;
                EnterCriticalSection(&g.lock);
                g.dirty = 1;
                LeaveCriticalSection(&g.lock);
            } else {
                if (failures++ % 10 == 0) log_line("Logitech SDK init failed (G HUB not up yet?), retrying");
                logi_unload();
                next_try = GetTickCount() + 5000;
                continue;
            }
        }

        Rgb zone[MAX_ZONES];
        int dirty;
        EnterCriticalSection(&g.lock);
        dirty = g.dirty;
        g.dirty = 0;
        memcpy(zone, g.zone, sizeof(zone));
        LeaveCriticalSection(&g.lock);
        if (!dirty) continue;

        int ok = 1;
        for (int z = 0; z < cfg.count; z++)
            ok &= logi.SetZone(LOGI_MOUSE, cfg.zone[z].sdk_id, pct(zone[z].r), pct(zone[z].g), pct(zone[z].b));
        if (!ok) { log_line("Logitech write failed, reconnecting"); logi_unload(); }
    }
    logi_unload();
    return 0;
}

/* ---- plugin slots ------------------------------------------------------------- */

static CPluginDeviceInfo *get_device_info(const char *device_id)
{
    if (g.mode != CPM_ATTACHED || !device_id || strcmp(device_id, DEVICE_ID) != 0) return NULL;
    if (!g.hash_view[0]) hash_images();

    CPluginDeviceInfo *info = calloc(1, sizeof(*info));
    info->name = _strdup(cfg.name);
    info->type = PLUGIN_DEVICE_TYPE;
    info->id = _strdup(DEVICE_ID);
    info->view_count = 1;
    info->image1 = make_image("thumbnail.png", g.hash_thumb);
    info->image2 = make_image("promo.png", g.hash_promo);

    info->leds = calloc(1, sizeof(*info->leds));
    info->leds->count = cfg.count;
    info->leds->items = calloc(cfg.count, sizeof(CPluginLedPosition));
    for (int i = 0; i < cfg.count; i++) {
        const int s = cfg.zone[i].shape;
        info->leds->items[i] = (CPluginLedPosition){ LED_ID(i), SHAPES[s].cx, SHAPES[s].cy, CPLP_CM };
    }
    log_line("getDeviceInfo -> %s", cfg.name);
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
    view->led_zones->count = cfg.count;
    view->led_zones->items = calloc(cfg.count, sizeof(CPluginLedZone));
    for (int i = 0; i < cfg.count; i++)
        view->led_zones->items[i] = (CPluginLedZone){ LED_ID(i), _strdup(SHAPES[cfg.zone[i].shape].path), CPLZ_SHAPE,
                                                      _strdup(cfg.zone[i].name) };
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
    EnterCriticalSection(&g.lock);
    for (int i = 0; i < count; i++) {
        int z = colors[i].led_id - LED_ID(0);
        if (z < 0 || z >= cfg.count) continue;
        Rgb c = { colors[i].r, colors[i].g, colors[i].b };
        if (memcmp(&c, &g.zone[z], sizeof(c))) { g.zone[z] = c; g.dirty = 1; }
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
    if (!cfg.count) load_config();
    inst->get_device_info = get_device_info;
    inst->set_leds_colors = set_leds_colors;
    inst->subscribe_connection = subscribe_connection;
    inst->unsubscribe_connection = unsubscribe_connection;
    inst->get_device_view = get_device_view;
    inst->free_device_info = free_device_info;
    inst->free_device_view = free_device_view;
    inst->set_mode = set_mode;

    if (!g.worker) {
        /* Pin this DLL: the worker can be blocked inside the Logitech SDK when the
         * host unloads us, and unmapping its code would hang the plugin host. */
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

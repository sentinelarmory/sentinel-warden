/* icue_plugin.h - iCUE 5 device plugin ABI (api102_106 layout), as used by the
 * shipped ASUS (API 105) and NVIDIA (API 106) plugins. Derived from static
 * analysis of the shipped plugins and the iCUE plugin host. x64, natural alignment. */
#ifndef ICUE_PLUGIN_H
#define ICUE_PLUGIN_H

#include <stdint.h>

#define CPLUGIN_API_VERSION 106
#define CPF_DETACHED_MODE 0x04          /* mandatory feature bit */

enum { CPM_DETACHED = 1, CPM_ATTACHED = 2 };
enum { CPDT_MOUSE = 2, CPDT_GPU = 4 };
#define CPLM_ZONE(n) (0x10000 + (n))    /* mouse LED ids, n = 1..10 */
#define CPLGPU_ZONE(n) (0x30000 + (n))  /* GPU LED ids, n = 1..50 */

/* LED position unit: 0 = x10 (cm), 1 = x130 (logical), other = as is */
enum { CPLP_CM = 0 };
/* LED zone appearance in a device view: 0 = SVG path in `shape` */
enum { CPLZ_SHAPE = 0 };

typedef struct {                        /* 0x10; path relative to plugin dir, lowercase sha256 hex */
    char *path;
    char *hash;
} CPluginImage;

typedef struct {                        /* 0x20 */
    int32_t id;
    double cx, cy;
    int32_t unit;
} CPluginLedPosition;

typedef struct {                        /* 0x10 */
    int32_t count;
    CPluginLedPosition *items;
} CPluginLedPositions;

typedef struct {                        /* 0x38 */
    char *name;
    int32_t type;
    CPluginLedPositions *leds;
    CPluginImage *image1;               /* thumbnail / promo (we use one picture for both) */
    char *id;
    int32_t view_count;
    CPluginImage *image2;
} CPluginDeviceInfo;

typedef struct {                        /* 0x20 */
    int32_t led_id;
    char *shape;                        /* SVG path, device-view pixel coordinates */
    int32_t appearance;
    char *text;
} CPluginLedZone;

typedef struct {                        /* 0x10 */
    int32_t count;
    CPluginLedZone *items;
} CPluginLedZones;

typedef struct {                        /* 0x20 */
    CPluginImage *image1;               /* view / mask (one picture for both) */
    CPluginImage *image2;
    CPluginLedZones *led_zones;
    void *action_zones;                 /* unused (NULL) */
} CPluginDeviceView;

typedef struct {                        /* host-owned, valid during the call */
    int32_t led_id;
    int32_t r, g, b;                    /* 0..255 */
} CPluginLedColor;

typedef void (*CPluginConnectionCallback)(void *context, const char *device_id, int connected);

typedef struct {                        /* 19 slots, 0x98 bytes */
    CPluginDeviceInfo *(*get_device_info)(const char *device_id);                            /* 0 */
    void (*set_leds_colors)(const char *device_id, int count, const CPluginLedColor *colors); /* 1 */
    void (*subscribe_connection)(void *context, CPluginConnectionCallback cb);               /* 2 */
    void (*unsubscribe_connection)(void);                                                    /* 3 */
    CPluginDeviceView *(*get_device_view)(const char *device_id, int index);                 /* 4 */
    void (*free_device_info)(CPluginDeviceInfo *info);                                       /* 5 */
    void (*free_device_view)(CPluginDeviceView *view);                                       /* 6 */
    void *key_events[3];                                                                     /* 7-9 */
    void (*set_mode)(int mode);                                                              /* 10 */
    void *unused[8];                                                                         /* 11-18 */
} CPluginInstance;

_Static_assert(sizeof(CPluginImage) == 0x10, "image");
_Static_assert(sizeof(CPluginLedPosition) == 0x20, "led position");
_Static_assert(sizeof(CPluginLedPositions) == 0x10, "led positions");
_Static_assert(sizeof(CPluginDeviceInfo) == 0x38, "device info");
_Static_assert(sizeof(CPluginLedZone) == 0x20, "led zone");
_Static_assert(sizeof(CPluginDeviceView) == 0x20, "device view");
_Static_assert(sizeof(CPluginLedColor) == 0x10, "led color");
_Static_assert(sizeof(CPluginInstance) == 0x98, "instance");

#endif

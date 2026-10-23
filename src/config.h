/* config.h - runtime configuration for the pxc2_nt.dll shim.
 * Loaded from pxcshim.ini next to the DLL (optional; sane defaults otherwise),
 * so the shim works with ANY off-the-shelf S-Video/USB capture device without
 * a rebuild. See LoadConfig() for keys.
 */
#ifndef PXCSHIM_CONFIG_H
#define PXCSHIM_CONFIG_H
#include <windows.h>

typedef enum { STD_AUTO = 0, STD_NTSC = 1, STD_PAL = 2 } VideoStandard;

typedef struct {
    int           deviceIndex;   /* -1 = first available (default)                 */
    wchar_t       deviceName[256];/* substring match on friendly name; overrides idx*/
    VideoStandard standard;      /* NTSC (default - MicroMill is NTSC-only)         */
    int           captureWidth;  /* MF output width  (default 640)                  */
    int           captureHeight; /* MF output height (default 480)                  */
    int           promoteY8;     /* 1 = write RGB32 into sub-32-bit frames (default) */
    int           testPattern;   /* 1 = paint a diagnostic gradient instead of video */
    int           sliderMax;     /* MicroMill picture-slider max (for control scaling)*/
    wchar_t       logFile[260];  /* empty = logging off                             */
} ShimConfig;

/* Fills cfg with defaults, then overlays pxcshim.ini found next to hModule
 * (the shim DLL). Never fails; missing file = all defaults. */
void LoadConfig(HMODULE hModule, ShimConfig *cfg);

/* Append-to-file logger (no-op if cfg->logFile empty). printf-style. */
void ShimLog(const char *fmt, ...);
void ShimLogInit(const wchar_t *path);

#endif

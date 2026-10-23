/* pxcsupport.cpp - drop-in GDI replacement for NWR-ESI's PXC200_Support.dll.
 *
 * MicroMill.exe loads PXC200_Support.dll and calls the 17 PXC_* entry points
 * below to show live video. The original drives the Imagenation PXC200 card via
 * pxc2_nt.dll and displays through a DirectDraw hardware OVERLAY. On modern GPUs
 * that overlay path is dead (wrong format -> color noise, fixed screen position
 * that ignores the window). This replacement instead:
 *   - captures frames from a standard USB/S-Video device via Media Foundation
 *     (capture.cpp), and
 *   - renders them into MicroMill's video window with plain GDI, compositing
 *     MicroMill's crosshair/scale annotations on top.
 * No DirectDraw, no overlay, no palette, no color key: correct colors, tracks
 * the window, works over RDP. MicroMill.exe is unmodified; pure user-mode DLL.
 *
 * Crosshair model: MicroMill draws its crosshair/scale via the DC returned by
 * PXC_GetOverlayDC. We hand it a persistent offscreen 32-bit "annotation" DIB
 * (cleared to black). The display thread composites that DIB's non-black pixels
 * over each video frame, so the annotations survive the ~30fps video refresh.
 *
 * ABI: the exported PXC_* entry points are an independent, clean-room
 * reimplementation of the documented PXC200_Support interface; every export is
 * __stdcall (arg sizes in pxcsupport.def). Build 32-bit to match MicroMill.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include "capture.h"
#include "config.h"
#include "convert.h"
#include "pxcshim.h"   /* PBITS_* */

static HMODULE          g_hmod      = NULL;
static ShimConfig       g_cfg;
static CaptureEngine   *g_cap       = NULL;
static HWND             g_hwnd      = NULL;     /* MicroMill's video window */
static int              g_w = 640, g_h = 480;   /* frame size MicroMill expects (NTSC) */
static BYTE            *g_frame     = NULL;      /* 32-bit BGRX video, g_w*g_h*4 */
static BYTE            *g_compose   = NULL;      /* 32-bit BGRX composite, window-sized */
static CRITICAL_SECTION g_cs;
static volatile LONG    g_init      = 0;
static HANDLE           g_dispThread= NULL;
static HANDLE           g_dispStop  = NULL;

/* The annotation layer and composite are sized to MicroMill's VIDEO WINDOW
 * client rect, not the frame: MicroMill draws its crosshair in window
 * coordinates (center of the window), so the layer must match the window for
 * the crosshair to land correctly over the stretched video. */
static int      g_cw = 0, g_ch = 0;             /* current window client size */
static HDC      g_annotDC  = NULL;
static HBITMAP  g_annotBmp = NULL;
static BYTE    *g_annotBits= NULL;              /* BGRX, g_cw*g_ch*4 */
static volatile LONG g_annotDrawn = 0;          /* MicroMill has used the overlay DC */

/* (Re)create the annotation DIB + composite buffer at the window client size.
 * Recreated whenever the window is resized. */
static void EnsureLayers(void) {
    int cw = g_w, ch = g_h;
    if (g_hwnd) {
        RECT rc; if (GetClientRect(g_hwnd, &rc)) {
            if (rc.right - rc.left > 0) cw = rc.right - rc.left;
            if (rc.bottom - rc.top > 0) ch = rc.bottom - rc.top;
        }
    }
    if (g_annotDC && cw == g_cw && ch == g_ch) return;  /* unchanged */
    /* rebuild (serialize with the compositor; CRITICAL_SECTION is recursive) */
    EnterCriticalSection(&g_cs);
    if (g_annotBmp) { DeleteObject(g_annotBmp); g_annotBmp = NULL; g_annotBits = NULL; }
    if (!g_annotDC) { HDC wdc = g_hwnd ? GetDC(g_hwnd) : GetDC(NULL); g_annotDC = CreateCompatibleDC(wdc); if (g_hwnd) ReleaseDC(g_hwnd, wdc); else ReleaseDC(NULL, wdc); }
    BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw; bi.bmiHeader.biHeight = -ch;   /* top-down */
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    g_annotBmp = CreateDIBSection(g_annotDC, &bi, DIB_RGB_COLORS, (void **)&g_annotBits, NULL, 0);
    if (g_annotBmp) SelectObject(g_annotDC, g_annotBmp);
    if (g_annotBits) memset(g_annotBits, 0, (size_t)cw * ch * 4);
    free(g_compose); g_compose = (BYTE *)calloc((size_t)cw * ch, 4);
    g_cw = cw; g_ch = ch;
    InterlockedExchange(&g_annotDrawn, 0); /* MicroMill must redraw on the new layer */
    LeaveCriticalSection(&g_cs);
}

static void RefreshFrame(void) {
    if (!g_frame) return;
    EnterCriticalSection(&g_cs);
    CaptureReadInto(g_cap, g_frame, g_w, g_h, PBITS_RGB32);
    LeaveCriticalSection(&g_cs);
}

static void ComposeAndBlit(void) {
    if (!g_hwnd || !g_frame) return;
    EnsureLayers();                 /* sizes g_compose / annotation to the window */
    if (!g_compose || g_cw <= 0 || g_ch <= 0) return;
    GdiFlush();                     /* flush MicroMill's GDI into the annotation DIB */
    EnterCriticalSection(&g_cs);
    /* nearest-neighbor stretch video (g_w x g_h) into compose (g_cw x g_ch),
     * then overlay annotation (window-sized, 1:1) non-black pixels on top */
    for (int y = 0; y < g_ch; ++y) {
        int sy = y * g_h / g_ch; if (sy >= g_h) sy = g_h - 1;
        const BYTE *vrow = g_frame + (size_t)sy * g_w * 4;
        BYTE *drow = g_compose + (size_t)y * g_cw * 4;
        const BYTE *arow = (g_annotBits && g_annotDrawn) ? g_annotBits + (size_t)y * g_cw * 4 : NULL;
        for (int x = 0; x < g_cw; ++x) {
            BYTE *d = drow + (size_t)x * 4;
            if (arow) {
                const BYTE *a = arow + (size_t)x * 4;
                if (a[0] | a[1] | a[2]) { d[0]=a[0]; d[1]=a[1]; d[2]=a[2]; d[3]=0xFF; continue; }
            }
            int sx = x * g_w / g_cw; if (sx >= g_w) sx = g_w - 1;
            const BYTE *v = vrow + (size_t)sx * 4;
            d[0]=v[0]; d[1]=v[1]; d[2]=v[2]; d[3]=0xFF;
        }
    }
    LeaveCriticalSection(&g_cs);

    HDC dc = GetDC(g_hwnd);
    if (!dc) return;
    BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = g_cw; bi.bmiHeader.biHeight = -g_ch;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0,0, g_cw,g_ch, 0,0, 0,g_ch, g_compose, &bi, DIB_RGB_COLORS);
    ReleaseDC(g_hwnd, dc);
}

static DWORD WINAPI DispThread(LPVOID) {
    int n = 0;
    for (;;) {
        if (g_dispStop && WaitForSingleObject(g_dispStop, 0) == WAIT_OBJECT_0) break;
        RefreshFrame();
        ComposeAndBlit();
        if (((n++) % 60) == 30) { /* ~every 2s: log compositor state (if logging on) */
            size_t nz = 0;
            EnterCriticalSection(&g_cs);
            if (g_annotBits && g_cw > 0 && g_ch > 0) {
                size_t npx = (size_t)g_cw * g_ch;   /* annotation is window-sized */
                for (size_t i = 0; i < npx; ++i) { const BYTE *a = g_annotBits + i*4; if (a[0]|a[1]|a[2]) nz++; }
            }
            LeaveCriticalSection(&g_cs);
            ShimLog("disp: %dx%d annotDrawn=%d annot_nonblack_px=%u", g_cw, g_ch, (int)g_annotDrawn, (unsigned)nz);
        }
        Sleep(33); /* ~30 fps */
    }
    return 0;
}

static void StartDisplay(void) {
    if (!g_dispThread) {
        if (g_dispStop) ResetEvent(g_dispStop);
        g_dispThread = CreateThread(NULL, 0, DispThread, NULL, 0, NULL);
    }
}
static void StopDisplay(void) {
    if (g_dispThread) {
        if (g_dispStop) SetEvent(g_dispStop);
        WaitForSingleObject(g_dispThread, 2000);
        CloseHandle(g_dispThread);
        g_dispThread = NULL;
    }
}

extern "C" {

__declspec(dllexport) int __stdcall PXC_InitializeFG(void) {
    if (!InterlockedExchange(&g_init, 1)) {
        InitializeCriticalSection(&g_cs);
        g_dispStop = CreateEventW(NULL, TRUE, FALSE, NULL);
    }
    LoadConfig(g_hmod, &g_cfg);
    ShimLogInit(g_cfg.logFile);
    if (g_cfg.captureWidth)  g_w = g_cfg.captureWidth;
    if (g_cfg.captureHeight) g_h = g_cfg.captureHeight;
    if (!g_frame)   g_frame   = (BYTE *)calloc((size_t)g_w * g_h, 4);
    if (!g_cap)     g_cap     = CaptureCreate(&g_cfg);
    ShimLog("PXC_InitializeFG (GDI PXC200_Support): %dx%d", g_w, g_h);
    return 0;
}

__declspec(dllexport) int __stdcall PXC_InitializeDD(HWND hwnd) {
    g_hwnd = hwnd;
    EnsureLayers();
    ShimLog("PXC_InitializeDD hwnd=%p client=%dx%d", (void *)hwnd, g_cw, g_ch);
    return 0;
}

__declspec(dllexport) int  __stdcall PXC_InitializeBuffers(void) { return 0; }
__declspec(dllexport) short __stdcall PXC_GetFrameWidth(void)    { return (short)g_w; }
__declspec(dllexport) short __stdcall PXC_GetFrameHeight(void)   { return (short)g_h; }
__declspec(dllexport) int  __stdcall PXC_GetErrorCode(void)      { return 0; }
__declspec(dllexport) int  __stdcall PXC_VideoPresent(void)      { return CaptureIsLive(g_cap) ? 1 : 0; }

__declspec(dllexport) int __stdcall PXC_StartGrab(void) { StartDisplay(); ShimLog("PXC_StartGrab"); return 0; }
__declspec(dllexport) int __stdcall PXC_StopGrab(void)  { StopDisplay(); return 0; }
__declspec(dllexport) int __stdcall PXC_DoGrab(int idx) { (void)idx; RefreshFrame(); ComposeAndBlit(); return 0; }

/* MicroMill draws its crosshair/scale onto this DC (our annotation DIB). */
__declspec(dllexport) HDC __stdcall PXC_GetOverlayDC(void) {
    EnsureLayers();
    InterlockedExchange(&g_annotDrawn, 1);
    return g_annotDC;
}
__declspec(dllexport) void __stdcall PXC_ReleaseOverlayDC(void) { GdiFlush(); }

__declspec(dllexport) int __stdcall PXC_SetParameter(int param, int value) {
    ShimLog("PXC_SetParameter param=%d value=%d", param, value);
    /* param 0..3 = Brightness/Contrast/Hue/Saturation (matches VideoProcAmp_*).
     * Map MicroMill's slider value (0..sliderMax) to the camera control. */
    if (param >= 0 && param <= 3 && g_cap) {
        double maxv = (g_cfg.sliderMax > 0) ? (double)g_cfg.sliderMax : 255.0;
        CaptureSetControl(g_cap, param, (double)value / maxv);
        return 0;
    }
    if (param == 0x0B) return 0x103;
    if (param > 0x0A)  return -2;
    return 0;
}

__declspec(dllexport) int __stdcall PXC_SaveSingleFrame(void *buf) {
    if (!buf || !g_frame) return 0;
    RefreshFrame();
    EnterCriticalSection(&g_cs); memcpy(buf, g_frame, (size_t)g_w*g_h*4); LeaveCriticalSection(&g_cs);
    return 1;
}
__declspec(dllexport) int __stdcall PXC_SaveCaptureBuffer(void *buf) { return PXC_SaveSingleFrame(buf); }
__declspec(dllexport) int __stdcall PXC_GrabSingleFrame(void *buf, int size) {
    if (!buf || !g_frame) return 0;
    RefreshFrame();
    size_t need = (size_t)g_w*g_h*4; if (size>0 && (size_t)size<need) need=(size_t)size;
    EnterCriticalSection(&g_cs); memcpy(buf, g_frame, need); LeaveCriticalSection(&g_cs);
    return 1;
}

__declspec(dllexport) void __stdcall PXC_Shutdown(void) {
    StopDisplay();
    if (g_cap) { CaptureDestroy(g_cap); g_cap = NULL; }
    ShimLog("PXC_Shutdown");
}

} /* extern "C" */

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { g_hmod = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}

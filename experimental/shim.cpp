/* shim.cpp - user-mode drop-in replacement for pxc2_nt.dll.
 *
 * Presents the Imagenation PXC runtime interface (the TAGpxc vtable installed by
 * OpenLibrary) to MicroMill / PXC200_Support.dll, but sources frames from a
 * standard USB/S-Video capture device via Media Foundation (capture.cpp)
 * instead of the PXC200A PCI card and \\.\PXC driver. Pure user mode: no kernel
 * driver, no signing, Secure Boot stays on.
 *
 * NOTE: this is the EARLIER, SUPERSEDED approach. It makes MicroMill's capture
 * seam work, but MicroMill's *display* half (inside PXC200_Support.dll) still
 * renders through a legacy DirectDraw overlay that is dead on modern GPUs and
 * fails over RDP. The shipped solution instead replaces PXC200_Support.dll with
 * a GDI renderer (see ../src/). This file is kept for reference only.
 *
 * Binding: PXC200_Support.dll does imagenation_OpenLibrary("pxc2_nt.dll") ->
 * GetProcAddress("OpenLibrary") -> OpenLibrary(&vtable,size). Only OpenLibrary/
 * CloseLibrary are fetched by name (see pxc2_nt.def); everything else is called
 * through the vtable this file installs. Build 32-bit to match MicroMill.
 *
 * Display: PXC200_Support.dll reads frh->user.pointer and blits it. This shim
 * only has to keep that buffer filled with the latest frame in the requested
 * pixel format - which it does two ways:
 *   - Grab(frame): fill once, now.
 *   - GrabContinuous(frame,TRUE): a refill thread keeps `frame` updated ~30fps,
 *     so the app sees live video whether or not it calls Grab per displayed frame.
 */
#include "../src/pxcshim.h"
#include "../src/capture.h"
#include "../src/config.h"
#include "../src/convert.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------- shim state -------------------------------- */
typedef struct {
    short width, height;
    short pixfmt;
    long  fieldCount;
    volatile LONG ready;
} FGCTX;

static HMODULE        g_hmod       = NULL;
static ShimConfig     g_cfg;
static CaptureEngine *g_cap        = NULL;
static FGCTX          g_fg         = { 640, 480, PBITS_RGB32, 1, 1 };

/* continuous-refill thread */
static HANDLE          g_contThread = NULL;
static HANDLE          g_contStop   = NULL;
static CRITICAL_SECTION g_contCS;
static FRAME * volatile g_contFrame = NULL;
static volatile LONG    g_contInit  = 0;

static void FillOnce(FRAME *f) {
    if (!f || !f->user.pointer) return;
    /* During live display MicroMill repoints the frame at a locked 32-bit
     * DirectDraw surface before grabbing, yet labels the frame Y8/16-bit. Write
     * the surface's real depth (RGB32) for sub-32-bit types so pixels land
     * correctly; the buffer is sized 32-bit in AllocateBuffer so this never
     * overflows. */
    int writeType = f->type;
    if (g_cfg.promoteY8 && PbitsBytesPerPixel(f->type) < 4) writeType = PBITS_RGB32;
    if (g_cfg.testPattern) {
        /* diagnostic: horizontal black->white gradient + an 80px white grid.
         * Clean gray ramp + square grid = display path OK; tilted grid = pitch
         * mismatch; false colors = palette; garbage = format. */
        int bpp = PbitsBytesPerPixel(writeType); if (!bpp) bpp = 4;
        unsigned char *base = (unsigned char *)f->user.pointer;
        for (int y = 0; y < f->dy; ++y) {
            unsigned char *row = base + (size_t)y * f->dx * bpp;
            for (int x = 0; x < f->dx; ++x) {
                unsigned char v = (unsigned char)(x * 255 / (f->dx ? f->dx : 1));
                if ((x % 80) == 0 || (y % 80) == 0) v = 0xFF;
                unsigned char *p = row + (size_t)x * bpp;
                if (writeType == PBITS_Y8) p[0] = v;
                else { p[0] = v; p[1] = v; p[2] = v; if (bpp == 4) p[3] = 0xFF; }
            }
        }
        InterlockedExchange(&g_fg.ready, 1);
        return;
    }
    BOOL got = CaptureReadInto(g_cap, f->user.pointer, f->dx, f->dy, writeType);
    InterlockedExchange(&g_fg.ready, 1);
    /* periodic diagnostic: what are we filling, and how bright is it? */
    static volatile LONG n = 0;
    if ((InterlockedIncrement(&n) % 60) == 1) {
        int bpp = PbitsBytesPerPixel(writeType); if (!bpp) bpp = 4;
        const unsigned char *p = (const unsigned char *)f->user.pointer;
        unsigned long long sum = 0; unsigned mn = 255, mx = 0;
        size_t npx = (size_t)f->dx * f->dy;
        for (size_t i = 0; i < npx; ++i) {
            const unsigned char *q = p + i * bpp;
            unsigned l = (writeType == PBITS_Y8) ? q[0]
                       : (unsigned)((q[2]*299 + q[1]*587 + q[0]*114) / 1000);
            sum += l; if (l < mn) mn = l; if (l > mx) mx = l;
        }
        ShimLog("fill %dx%d type=0x%x write=0x%x got=%d cap=%dx%d ptr=%p lum[min=%u mean=%llu max=%u] live=%d",
                f->dx, f->dy, f->type, writeType, got, CaptureWidth(g_cap), CaptureHeight(g_cap),
                f->user.pointer, mn, npx ? sum / npx : 0, mx, CaptureIsLive(g_cap));
    }
}

static DWORD WINAPI ContThread(LPVOID) {
    for (;;) {
        if (g_contStop && WaitForSingleObject(g_contStop, 0) == WAIT_OBJECT_0) break;
        FRAME *f;
        EnterCriticalSection(&g_contCS);
        f = g_contFrame;
        LeaveCriticalSection(&g_contCS);
        if (f) FillOnce(f);
        Sleep(33); /* ~30 fps */
    }
    return 0;
}

static void ContStart(FRAME *f) {
    EnterCriticalSection(&g_contCS);
    g_contFrame = f;
    LeaveCriticalSection(&g_contCS);
    if (!g_contThread) {
        if (g_contStop) ResetEvent(g_contStop);
        g_contThread = CreateThread(NULL, 0, ContThread, NULL, 0, NULL);
    }
    if (f) FillOnce(f); /* immediate first frame */
}

static void ContStop(void) {
    EnterCriticalSection(&g_contCS);
    g_contFrame = NULL;
    LeaveCriticalSection(&g_contCS);
    if (g_contThread) {
        if (g_contStop) SetEvent(g_contStop);
        WaitForSingleObject(g_contThread, 2000);
        CloseHandle(g_contThread);
        g_contThread = NULL;
    }
}

/* ======================= PXC vtable implementations ======================= */
static long  __stdcall x_AllocateFG(short n)        { (void)n; return (long)(LONG_PTR)&g_fg; }
static void  __stdcall x_FreeFG(long fgh)           { (void)fgh; ContStop(); }
static short __stdcall x_ReadRevision(long fgh)     { (void)fgh; return 0x11; }
static void  __stdcall x_Reset(long fgh)            { (void)fgh; }
static long  __stdcall x_CheckError(long fgh)       { (void)fgh; return ERR_NONE; }
static short __stdcall x_ReadProtection(long fgh)   { (void)fgh; return 0; }
static long  __stdcall x_ReadSerial(long fgh)       { (void)fgh; return 0; }
static short __stdcall x_GetModelNumber(long fgh)   { (void)fgh; return PXC200_F; }
static short __stdcall x_VideoType(long fgh)        { (void)fgh; return NTSC_FORMAT; }
static short __stdcall x_GetVideoDetect(long fgh)   { (void)fgh; return CaptureIsLive(g_cap) ? 1 : 0; }
static short __stdcall x_SetVideoDetect(long fgh, short t){ (void)fgh;(void)t; return 0; }

static short __stdcall x_SetWidth(long fgh, short w) { (void)fgh; g_fg.width = w; ShimLog("SetWidth %d", w); return w; }
static short __stdcall x_SetHeight(long fgh, short h){ (void)fgh; g_fg.height = h; ShimLog("SetHeight %d", h); return h; }
static short __stdcall x_GetWidth(long fgh)         { (void)fgh; return g_fg.width; }
static short __stdcall x_GetHeight(long fgh)        { (void)fgh; return g_fg.height; }
static short __stdcall x_GetXResolution(long fgh)   { (void)fgh; return g_fg.width; }
static short __stdcall x_GetYResolution(long fgh)   { (void)fgh; return g_fg.height; }
static short __stdcall x_SetXResolution(long fgh, short r){ (void)fgh; g_fg.width = r; ShimLog("SetXResolution %d", r); return r; }
static short __stdcall x_SetYResolution(long fgh, short r){ (void)fgh; g_fg.height = r; ShimLog("SetYResolution %d", r); return r; }
static short __stdcall x_GetLeft(long fgh)          { (void)fgh; return 0; }
static short __stdcall x_GetTop(long fgh)           { (void)fgh; return 0; }
static short __stdcall x_SetLeft(long fgh, short l) { (void)fgh;(void)l; return 0; }
static short __stdcall x_SetTop(long fgh, short t)  { (void)fgh;(void)t; return 0; }
static short __stdcall x_SetPixelFormat(long fgh, short type){ (void)fgh; ShimLog("SetPixelFormat 0x%x", type); if (PbitsBytesPerPixel(type)) g_fg.pixfmt = type; return g_fg.pixfmt; }

static FRAME* __stdcall x_AllocateBuffer(short dx, short dy, short type) {
    int reqbpp = PbitsBytesPerPixel(type);
    if (reqbpp == 0) { type = PBITS_RGB32; reqbpp = 4; }
    /* Keep the app's requested type (its bookkeeping depends on it), but size the
     * buffer for 32-bit when the request is sub-32-bit: during live display
     * MicroMill repoints the frame at a 32-bit DirectDraw surface and the grabber
     * writes RGB32 (see FillOnce), so the backing store must hold 4 bpp. */
    int allocbpp = (g_cfg.promoteY8 && reqbpp < 4) ? 4 : reqbpp;
    FRAME *f = (FRAME*)calloc(1, sizeof(FRAME));
    if (!f) return NULL;
    f->dx = dx; f->dy = dy; f->type = type;
    f->user_type = 1 /*PTYPE_UFLAT*/; f->dma_type = 3 /*PTYPE_DVIRTUAL*/;
    f->user.pointer = calloc((size_t)dx * dy, allocbpp);
    if (!f->user.pointer) { free(f); return NULL; }
    f->dma = f->user;
    g_fg.pixfmt = type;
    ShimLog("AllocateBuffer dx=%d dy=%d type=0x%x allocbpp=%d", dx, dy, type, allocbpp);
    return f;
}
static void __stdcall x_FreeFrame(FRAME *frh) {
    if (!frh) return;
    EnterCriticalSection(&g_contCS);
    if (g_contFrame == frh) g_contFrame = NULL; /* stop refilling a freed frame */
    LeaveCriticalSection(&g_contCS);
    if (frh->user.pointer) free(frh->user.pointer);
    free(frh);
}

static long __stdcall x_Grab(long fgh, FRAME *frh, short flags) {
    (void)fgh; (void)flags;
    static volatile LONG gn = 0;
    if ((InterlockedIncrement(&gn) % 60) == 1)
        ShimLog("Grab frame=%p %dx%d type=0x%x flags=%d", (void*)frh, frh?frh->dx:0, frh?frh->dy:0, frh?frh->type:0, flags);
    InterlockedExchange(&g_fg.ready, 0);
    FillOnce(frh);
    return (long)(LONG_PTR)frh;
}
static long __stdcall x_GrabContinuous(long fgh, FRAME *frh, short state, short flags) {
    (void)fgh; (void)flags;
    ShimLog("GrabContinuous state=%d frame=%dx%d type=0x%x", state, frh?frh->dx:0, frh?frh->dy:0, frh?frh->type:0);
    if (state) ContStart(frh);
    else       ContStop();
    return (long)(LONG_PTR)frh;
}
static long __stdcall x_SwitchGrab(long fgh, FRAME *f0, FRAME *f1, FRAME *f2, FRAME *f3, short flags) {
    (void)fgh;(void)flags;(void)f1;(void)f2;(void)f3;
    ShimLog("SwitchGrab f0=%p f1=%p f2=%p f3=%p flags=%d", (void*)f0,(void*)f1,(void*)f2,(void*)f3,flags);
    ContStart(f0);
    return (long)(LONG_PTR)f0;
}
static short __stdcall x_WaitVB(long fgh)           { (void)fgh; Sleep(10); return 0; }
static long  __stdcall x_Wait(long fgh, short flags){ (void)fgh;(void)flags; return 1; }
static void  __stdcall x_WaitFinished(long fgh, long qh){ (void)fgh;(void)qh; }
static short __stdcall x_IsFinished(long fgh, long qh){ (void)fgh;(void)qh; return 1; }
static void  __stdcall x_KillQueue(long fgh)        { (void)fgh; ContStop(); }
static short __stdcall x_SetFieldCount(long fgh, long c){ (void)fgh; g_fg.fieldCount = c; return (short)c; }
static long  __stdcall x_GetFieldCount(long fgh)    { (void)fgh; return g_fg.fieldCount; }

/* picture controls: benign stand-ins (TODO: IAMVideoProcAmp for live control) */
static long  __stdcall x_SetContrast(long f, float c, short fl){ (void)f;(void)c;(void)fl; return 0; }
static long  __stdcall x_SetBrightness(long f, float b, short fl){ (void)f;(void)b;(void)fl; return 0; }
static float __stdcall x_GetContrast(long f)        { (void)f; return 0.5f; }
static float __stdcall x_GetBrightness(long f)      { (void)f; return 0.5f; }
static long  __stdcall x_SetHue(long f, float h, short fl){ (void)f;(void)h;(void)fl; return 0; }
static long  __stdcall x_SetSaturation(long f, float s, short fl){ (void)f;(void)s;(void)fl; return 0; }
static float __stdcall x_GetHue(long f)             { (void)f; return 0.5f; }
static float __stdcall x_GetSaturation(long f)      { (void)f; return 0.5f; }
static float __stdcall x_SetVideoLevel(long f, float w){ (void)f; return w; }
static float __stdcall x_GetVideoLevel(long f)      { (void)f; return 1.0f; }
static short __stdcall x_SetChromaControl(long f, short c){ (void)f; return c; }
static short __stdcall x_GetChromaControl(long f)   { (void)f; return 0; }
static short __stdcall x_SetLumaControl(long f, short c){ (void)f; return c; }
static short __stdcall x_GetLumaControl(long f)     { (void)f; return 0; }

/* camera / crossbar: single-input stand-in (TODO: IAMCrossbar for multi-input) */
static long  __stdcall x_SwitchCamera(long f, short fl){ (void)f;(void)fl; return 0; }
static short __stdcall x_GetSwitch(long f)          { (void)f; return 0; }
static long  __stdcall x_SetCamera(long f, short n, short fl){ (void)f;(void)n;(void)fl; return 0; }
static short __stdcall x_GetCamera(long f)          { (void)f; return 0; }

/* digital I/O + Control-Package (strobe/holdoff/...): benign no-ops */
static short __stdcall x_GetIOType(long f, short n) { (void)f;(void)n; return 4; }
static short __stdcall x_SetIOType(long f, short n, short t){ (void)f;(void)n;(void)t; return 0; }
static long  __stdcall x_ReadIO(long f)             { (void)f; return 0; }
static short __stdcall x_WriteImmediateIO(long f, long m, long s){ (void)f;(void)m;(void)s; return 0; }
static long  __stdcall x_WaitAnyEvent(long f,long i,long m,long s,short fl){ (void)f;(void)i;(void)m;(void)s;(void)fl; return 0; }
static long  __stdcall x_WaitAllEvents(long f,long i,long m,long s,short fl){ (void)f;(void)i;(void)m;(void)s;(void)fl; return 0; }
static float __stdcall x_SetStrobePeriod(long f, short n, float p){ (void)f;(void)n; return p; }
static float __stdcall x_GetStrobePeriod(long f, short n){ (void)f;(void)n; return 0.0f; }
static short __stdcall x_SetHoldoffStart(long f, short fd, short s){ (void)f;(void)fd;(void)s; return 0; }
static short __stdcall x_SetHoldoffWidth(long f, short fd, short w){ (void)f;(void)fd;(void)w; return 0; }
static short __stdcall x_SetHoldoffMask(long f, long m){ (void)f;(void)m; return 0; }
static short __stdcall x_GetHoldoffStart(long f, short fd){ (void)f;(void)fd; return 0; }
static short __stdcall x_GetHoldoffWidth(long f, short fd){ (void)f;(void)fd; return 0; }
static long  __stdcall x_GetHoldoffMask(long f)     { (void)f; return 0; }
static float __stdcall x_SetDoubleStrobe(long f, short n, float p){ (void)f;(void)n; return p; }
static float __stdcall x_GetDoubleStrobe(long f, short n){ (void)f;(void)n; return 0.0f; }
static short __stdcall x_TriggerStrobe(long f, short n, long m){ (void)f;(void)n;(void)m; return 0; }
static short __stdcall x_SyncStrobe(long f, short n, short fd, short l){ (void)f;(void)n;(void)fd;(void)l; return 0; }
static short __stdcall x_FireStrobe(long f, long m)  { (void)f;(void)m; return 0; }
static short __stdcall x_SetDecisionPoint(long f, short fd, short l){ (void)f;(void)fd;(void)l; return 0; }
static short __stdcall x_GetDecisionPoint(long f, short fd){ (void)f;(void)fd; return 0; }
static short __stdcall x_SetDebounce(long f, short n, short d){ (void)f;(void)n;(void)d; return 0; }
static short __stdcall x_GetDebounce(long f, short n){ (void)f;(void)n; return 0; }

static const PXC g_pxc = {
    0, 0,
    x_AllocateFG, x_FreeFG, x_ReadRevision, x_Reset, x_CheckError, x_ReadProtection, x_ReadSerial,
    x_GetIOType, x_SetIOType, x_ReadIO, x_WriteImmediateIO,
    x_Grab, x_Wait, x_WaitVB, x_WaitFinished, x_IsFinished, x_KillQueue, x_AllocateBuffer, x_FreeFrame, x_SetFieldCount, x_GetFieldCount,
    x_GrabContinuous, x_VideoType, x_SetXResolution, x_SetYResolution, x_SetWidth, x_SetHeight, x_SetLeft, x_SetTop,
    x_GetXResolution, x_GetYResolution, x_GetWidth, x_GetHeight, x_GetLeft, x_GetTop,
    x_WaitAnyEvent, x_WaitAllEvents, x_SwitchGrab, x_SwitchCamera, x_GetSwitch, x_SetCamera, x_GetCamera,
    x_SetContrast, x_SetBrightness, x_GetContrast, x_GetBrightness,
    x_SetHue, x_SetSaturation, x_GetHue, x_GetSaturation,
    x_SetPixelFormat, x_SetVideoLevel, x_GetVideoLevel, x_SetChromaControl, x_GetChromaControl, x_SetLumaControl, x_GetLumaControl, x_SetVideoDetect, x_GetVideoDetect,
    x_SetStrobePeriod, x_GetStrobePeriod, x_SetHoldoffStart, x_SetHoldoffWidth, x_SetHoldoffMask, x_GetHoldoffStart, x_GetHoldoffWidth, x_GetHoldoffMask,
    x_SetDoubleStrobe, x_GetDoubleStrobe, x_TriggerStrobe, x_SyncStrobe, x_FireStrobe, x_SetDecisionPoint, x_GetDecisionPoint, x_SetDebounce, x_GetDebounce,
    x_GetModelNumber
};

/* ===================== the two exports fetched by name ===================== */
extern "C" short __stdcall OpenLibrary(PXC *iface, short size) {
    if (!iface) return 0;
    if (!InterlockedExchange(&g_contInit, 1)) InitializeCriticalSection(&g_contCS);
    if (!g_contStop) g_contStop = CreateEventW(NULL, TRUE, FALSE, NULL);

    LoadConfig(g_hmod, &g_cfg);
    ShimLogInit(g_cfg.logFile);
    ShimLog("OpenLibrary: starting capture (dev idx=%d name='%ls' std=%d %dx%d)",
            g_cfg.deviceIndex, g_cfg.deviceName, (int)g_cfg.standard,
            g_cfg.captureWidth, g_cfg.captureHeight);
    if (!g_cap) g_cap = CaptureCreate(&g_cfg);

    size_t n = (size_t)(unsigned short)size;
    if (n > sizeof(PXC)) n = sizeof(PXC);
    memcpy(iface, &g_pxc, n);
    return 1;
}

extern "C" void __stdcall CloseLibrary(PXC *iface) {
    (void)iface;
    ContStop();
    if (g_cap) { CaptureDestroy(g_cap); g_cap = NULL; }
    ShimLog("CloseLibrary: capture stopped");
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) { g_hmod = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}

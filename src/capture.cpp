/* capture.cpp - see capture.h. Media Foundation capture on a dedicated thread. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <dshow.h>      /* IAMVideoProcAmp (brightness/contrast/hue/saturation) */
#include <stdlib.h>
#include <string.h>
#include "capture.h"
#include "convert.h"

struct CaptureEngine {
    ShimConfig        cfg;
    HANDLE            thread;
    HANDLE            stopEvt;
    CRITICAL_SECTION  cs;
    BYTE             *latest;     /* BGRX top-down, latestW*latestH*4 */
    int               latestW, latestH;
    DWORD             lastTick;   /* GetTickCount of last frame        */
    volatile LONG     haveFrame;
    void             *procAmp;    /* IAMVideoProcAmp* (owned by the capture thread) */
    double            ctrlNorm[10];/* pending control values, normalized 0..1       */
    int               ctrlSet[10];
    volatile LONG     ctrlDirty;
};

/* Apply any pending IAMVideoProcAmp control changes (runs on the capture thread,
 * which owns the interface, so no cross-apartment COM). prop = VideoProcAmp_*
 * (0=Brightness,1=Contrast,2=Hue,3=Saturation,...). */
static void ApplyControls(CaptureEngine *e) {
    IAMVideoProcAmp *pa = (IAMVideoProcAmp *)e->procAmp;
    if (!pa) return;
    if (!InterlockedExchange(&e->ctrlDirty, 0)) return;
    for (int p = 0; p < 10; ++p) {
        EnterCriticalSection(&e->cs);
        int set = e->ctrlSet[p]; double norm = e->ctrlNorm[p];
        LeaveCriticalSection(&e->cs);
        if (!set) continue;
        long mn = 0, mx = 0, step = 0, def = 0, flags = 0;
        if (SUCCEEDED(pa->GetRange((long)p, &mn, &mx, &step, &def, &flags))) {
            if (norm < 0) norm = 0; if (norm > 1) norm = 1;
            long v = mn + (long)(norm * (double)(mx - mn) + 0.5);
            pa->Set((long)p, v, VideoProcAmp_Flags_Manual);
            ShimLog("procamp prop=%d norm=%.3f -> %ld [%ld..%ld]", p, norm, v, mn, mx);
        }
    }
}

/* Pick the best native type for the requested standard (height 480 NTSC /
 * 576 PAL), preferring YUY2, and make it current so the analog decoder is set
 * to the right line standard before we ask for RGB32 output. */
static void ForceStandard(IMFSourceReader *rd, VideoStandard std) {
    if (std == STD_AUTO) return;
    UINT32 wantH = (std == STD_PAL) ? 576 : 480;
    IMFMediaType *best = NULL; int bestScore = -1;
    for (DWORD i = 0; ; ++i) {
        IMFMediaType *t = NULL;
        HRESULT hr = rd->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &t);
        if (FAILED(hr)) break;
        GUID sub; UINT32 w = 0, h = 0, num = 0, den = 1;
        ZeroMemory(&sub, sizeof(sub));
        t->GetGUID(MF_MT_SUBTYPE, &sub);
        MFGetAttributeSize(t, MF_MT_FRAME_SIZE, &w, &h);
        MFGetAttributeRatio(t, MF_MT_FRAME_RATE, &num, &den);
        int score = 0;
        if (h == wantH) score += 100;
        if (sub == MFVideoFormat_YUY2) score += 20;   /* native analog format */
        if (den && num / den >= 25) score += 5;        /* prefer >=25/30 fps  */
        if (score > bestScore) {
            bestScore = score;
            if (best) best->Release();
            best = t; t->AddRef();
        }
        t->Release();
    }
    if (best) {
        rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, best);
        best->Release();
    }
}

static IMFMediaSource *OpenDevice(const ShimConfig *cfg) {
    IMFAttributes *sel = NULL;
    IMFActivate **dev = NULL;
    UINT32 count = 0;
    IMFMediaSource *src = NULL;

    if (FAILED(MFCreateAttributes(&sel, 1))) return NULL;
    sel->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(MFEnumDeviceSources(sel, &dev, &count)) || count == 0) {
        if (sel) sel->Release();
        if (dev) CoTaskMemFree(dev);
        return NULL;
    }

    int chosen = -1;
    if (cfg->deviceName[0]) {
        for (UINT32 i = 0; i < count && chosen < 0; ++i) {
            wchar_t *nm = NULL; UINT32 nl = 0;
            dev[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nm, &nl);
            if (nm && StrStrIW(nm, cfg->deviceName)) chosen = (int)i;
            CoTaskMemFree(nm);
        }
    }
    if (chosen < 0) chosen = (cfg->deviceIndex >= 0 && cfg->deviceIndex < (int)count) ? cfg->deviceIndex : 0;
    dev[chosen]->ActivateObject(IID_PPV_ARGS(&src));

    for (UINT32 i = 0; i < count; ++i) dev[i]->Release();
    CoTaskMemFree(dev);
    sel->Release();
    return src;
}

static void StoreFrame(CaptureEngine *e, const BYTE *bgrx, int w, int h, int stride) {
    EnterCriticalSection(&e->cs);
    if (!e->latest || e->latestW != w || e->latestH != h) {
        free(e->latest);
        e->latest = (BYTE *)malloc((size_t)w * h * 4);
        e->latestW = w; e->latestH = h;
    }
    if (e->latest) {
        LONG abs = stride < 0 ? -stride : stride;
        for (int y = 0; y < h; ++y) {
            const BYTE *s = (stride < 0) ? bgrx + (size_t)(h - 1 - y) * abs
                                         : bgrx + (size_t)y * abs;
            memcpy(e->latest + (size_t)y * w * 4, s, (size_t)w * 4);
        }
        e->lastTick = GetTickCount();
        InterlockedExchange(&e->haveFrame, 1);
    }
    LeaveCriticalSection(&e->cs);
}

static DWORD WINAPI CaptureThread(LPVOID param) {
    CaptureEngine *e = (CaptureEngine *)param;
    IMFMediaSource *src = NULL;
    IMFAttributes *ra = NULL;
    IMFSourceReader *rd = NULL;
    IMFMediaType *outT = NULL, *cur = NULL;
    int outW = e->cfg.captureWidth, outH = e->cfg.captureHeight;
    LONG stride = outW * 4;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    BOOL comInit = SUCCEEDED(hr);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) goto cleanup;

    src = OpenDevice(&e->cfg);
    if (!src) { ShimLog("capture: no device opened"); goto cleanup; }

    {   /* camera picture controls (brightness/contrast/hue/saturation) */
        IAMVideoProcAmp *pa = NULL;
        if (SUCCEEDED(src->QueryInterface(IID_IAMVideoProcAmp, (void **)&pa)) && pa) {
            e->procAmp = pa;
            ShimLog("capture: IAMVideoProcAmp available");
        } else {
            ShimLog("capture: no IAMVideoProcAmp on this device");
        }
    }

    if (FAILED(MFCreateAttributes(&ra, 1))) goto cleanup;
    ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    if (FAILED(MFCreateSourceReaderFromMediaSource(src, ra, &rd))) { ShimLog("capture: reader failed"); goto cleanup; }

    ForceStandard(rd, e->cfg.standard);

    /* Ask for RGB32 at the configured output size; MF video processor converts
     * (YUY2/NV12/MJPG -> RGB32) and scales. */
    if (SUCCEEDED(MFCreateMediaType(&outT))) {
        outT->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        outT->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        MFSetAttributeSize(outT, MF_MT_FRAME_SIZE, (UINT32)outW, (UINT32)outH);
        if (FAILED(rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, outT))) {
            /* some devices reject an explicit size; retry size-less */
            outT->DeleteItem(MF_MT_FRAME_SIZE);
            rd->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, outT);
        }
    }
    if (SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
        UINT32 w = 0, h = 0, st = 0;
        MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
        if (w && h) { outW = (int)w; outH = (int)h; }
        if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) stride = (LONG)st;
        if (stride == 0) stride = outW * 4;
        cur->Release(); cur = NULL;
    }
    ShimLog("capture: streaming %dx%d stride %ld", outW, outH, stride);

    for (;;) {
        if (WaitForSingleObject(e->stopEvt, 0) == WAIT_OBJECT_0) break;
        ApplyControls(e);
        DWORD si = 0, flags = 0; LONGLONG ts = 0;
        IMFSample *s = NULL;
        hr = rd->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &si, &flags, &ts, &s);
        if (FAILED(hr)) { ShimLog("capture: ReadSample 0x%08lx", (unsigned long)hr); Sleep(50); if (s) s->Release(); continue; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (s) s->Release(); break; }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            if (SUCCEEDED(rd->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) {
                UINT32 w = 0, h = 0, st = 0;
                MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &w, &h);
                if (w && h) { outW = (int)w; outH = (int)h; }
                if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st)) && st) stride = (LONG)st;
                cur->Release(); cur = NULL;
            }
        }
        if (!s) { Sleep(5); continue; }
        IMFMediaBuffer *buf = NULL;
        if (SUCCEEDED(s->ConvertToContiguousBuffer(&buf))) {
            BYTE *p = NULL; DWORD ml = 0, cl = 0;
            if (SUCCEEDED(buf->Lock(&p, &ml, &cl))) {
                StoreFrame(e, p, outW, outH, stride);
                buf->Unlock();
            }
            buf->Release();
        }
        s->Release();
    }

cleanup:
    if (e->procAmp) { ((IUnknown *)e->procAmp)->Release(); e->procAmp = NULL; }
    if (cur) cur->Release();
    if (outT) outT->Release();
    if (rd) rd->Release();
    if (ra) ra->Release();
    if (src) { src->Shutdown(); src->Release(); }
    MFShutdown();
    if (comInit) CoUninitialize();
    return 0;
}

extern "C" CaptureEngine *CaptureCreate(const ShimConfig *cfg) {
    CaptureEngine *e = (CaptureEngine *)calloc(1, sizeof(CaptureEngine));
    if (!e) return NULL;
    e->cfg = *cfg;
    InitializeCriticalSection(&e->cs);
    e->stopEvt = CreateEventW(NULL, TRUE, FALSE, NULL);
    e->thread = CreateThread(NULL, 0, CaptureThread, e, 0, NULL);
    if (!e->thread) { CaptureDestroy(e); return NULL; }
    return e;
}

extern "C" void CaptureDestroy(CaptureEngine *e) {
    if (!e) return;
    if (e->stopEvt) SetEvent(e->stopEvt);
    if (e->thread) { WaitForSingleObject(e->thread, 3000); CloseHandle(e->thread); }
    if (e->stopEvt) CloseHandle(e->stopEvt);
    DeleteCriticalSection(&e->cs);
    free(e->latest);
    free(e);
}

extern "C" BOOL CaptureIsLive(CaptureEngine *e) {
    if (!e || !e->haveFrame) return FALSE;
    return (GetTickCount() - e->lastTick) < 1000 ? TRUE : FALSE;
}

extern "C" int CaptureWidth(CaptureEngine *e)  { return e ? e->latestW : 0; }
extern "C" int CaptureHeight(CaptureEngine *e) { return e ? e->latestH : 0; }

/* Queue a camera picture-control change. prop = VideoProcAmp_* (0=Brightness,
 * 1=Contrast, 2=Hue, 3=Saturation, ...); norm = 0..1. Applied on the capture
 * thread. Returns TRUE if the device exposes IAMVideoProcAmp. */
extern "C" BOOL CaptureSetControl(CaptureEngine *e, int prop, double norm) {
    if (!e || prop < 0 || prop > 9) return FALSE;
    EnterCriticalSection(&e->cs);
    e->ctrlNorm[prop] = norm; e->ctrlSet[prop] = 1;
    LeaveCriticalSection(&e->cs);
    InterlockedExchange(&e->ctrlDirty, 1);
    return e->procAmp ? TRUE : FALSE;
}

extern "C" BOOL CaptureReadInto(CaptureEngine *e, void *dest, int dw, int dh, int pbitsType) {
    if (!e || !dest) return FALSE;
    BOOL ok = FALSE;
    EnterCriticalSection(&e->cs);
    if (e->latest && e->latestW > 0 && e->latestH > 0) {
        ConvertRGB32(e->latest, e->latestW, e->latestH, e->latestW * 4,
                     (BYTE *)dest, dw, dh, pbitsType);
        ok = TRUE;
    }
    LeaveCriticalSection(&e->cs);
    return ok;
}

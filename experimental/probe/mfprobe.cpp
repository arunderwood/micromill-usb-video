/* mfprobe.cpp - Media Foundation capture probe for the video-shim project.
 *
 * Enumerates video-capture devices, prints each one's native media types
 * (FOURCC subtype + resolution + frame rate), then opens the chosen device,
 * captures one frame as RGB32, writes it to a .bmp, and prints luminance
 * stats (min/mean/max + stddev) so a dark-but-live signal is distinguishable
 * from a flat no-signal screen.
 *
 * Usage:  mfprobe.exe [deviceIndex] [out.bmp]
 *   no args  -> auto-pick the device whose symbolic link contains VID_534D
 *               (the mill's MacroSilicon dongle), else device 0; writes frame.bmp
 *
 * Build: see build-probe.cmd (x64, links mfplat/mf/mfreadwrite/mfuuid/ole32/shlwapi).
 * This is a diagnostic tool, not part of the shipped shim.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <cstdio>
#include <cstdint>
#include <cmath>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

#define CK(hr, msg) do { if (FAILED(hr)) { printf("FAIL %s hr=0x%08lx\n", msg, (unsigned long)(hr)); goto done; } } while(0)

static void PrintFourCC(const GUID *sub) {
    DWORD fcc = sub->Data1;
    char c[5] = { (char)(fcc & 0xFF), (char)((fcc >> 8) & 0xFF), (char)((fcc >> 16) & 0xFF), (char)((fcc >> 24) & 0xFF), 0 };
    int printable = 1;
    for (int i = 0; i < 4; ++i) if (c[i] < 32 || c[i] > 126) printable = 0;
    if (printable) printf("%-6s", c);
    else           printf("0x%08lX", (unsigned long)fcc);
}

static HRESULT WriteBmp32(const wchar_t *path, const BYTE *bgrx, LONG w, LONG h, LONG stride) {
    BITMAPFILEHEADER fh; BITMAPINFOHEADER ih;
    ZeroMemory(&fh, sizeof(fh)); ZeroMemory(&ih, sizeof(ih));
    LONG absStride = stride < 0 ? -stride : stride;
    DWORD imgBytes = (DWORD)w * 4 * (DWORD)h;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + imgBytes;
    ih.biSize = sizeof(ih);
    ih.biWidth = w;
    ih.biHeight = -h;             /* top-down output */
    ih.biPlanes = 1;
    ih.biBitCount = 32;
    ih.biCompression = BI_RGB;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD wr;
    WriteFile(f, &fh, sizeof(fh), &wr, NULL);
    WriteFile(f, &ih, sizeof(ih), &wr, NULL);
    for (LONG y = 0; y < h; ++y) {
        const BYTE *src = stride < 0 ? bgrx + (size_t)(h - 1 - y) * absStride
                                     : bgrx + (size_t)y * absStride;
        WriteFile(f, src, (DWORD)w * 4, &wr, NULL);
    }
    CloseHandle(f);
    return S_OK;
}

int wmain(int argc, wchar_t **argv) {
    /* all locals declared up front: C++ forbids `goto` over an initializer */
    int wantIndex = (argc > 1) ? _wtoi(argv[1]) : -1;
    const wchar_t *outPath = (argc > 2) ? argv[2] : L"frame.bmp";
    HRESULT hr = S_OK;
    IMFAttributes *pSel = NULL;
    IMFActivate **ppDev = NULL;
    UINT32 count = 0;
    IMFMediaSource *pSrc = NULL;
    IMFAttributes *pRAttr = NULL;
    IMFSourceReader *pReader = NULL;
    IMFMediaType *pOut = NULL;
    IMFMediaType *cur = NULL;
    IMFSample *pSample = NULL;
    IMFMediaBuffer *pBuf = NULL;
    int chosen = -1;
    UINT32 W = 0, H = 0;
    LONG stride = 0;
    int got = 0;
    BYTE *data = NULL;
    DWORD maxLen = 0, curLen = 0;

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CK(hr, "CoInitializeEx");
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    CK(hr, "MFStartup");

    hr = MFCreateAttributes(&pSel, 1);
    CK(hr, "MFCreateAttributes");
    hr = pSel->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    CK(hr, "SetGUID VIDCAP");
    hr = MFEnumDeviceSources(pSel, &ppDev, &count);
    CK(hr, "MFEnumDeviceSources");
    printf("Video capture devices: %u\n", count);
    if (count == 0) { printf("No video capture device found.\n"); goto done; }

    for (UINT32 i = 0; i < count; ++i) {
        wchar_t *name = NULL; UINT32 nl = 0;
        wchar_t *link = NULL; UINT32 ll = 0;
        ppDev[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nl);
        ppDev[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &ll);
        printf("  [%u] %ls\n       %ls\n", i, name ? name : L"(no name)", link ? link : L"");
        if (chosen < 0 && link && StrStrIW(link, L"vid_534d")) chosen = (int)i;
        CoTaskMemFree(name); CoTaskMemFree(link);
    }
    if (wantIndex >= 0 && wantIndex < (int)count) chosen = wantIndex;
    if (chosen < 0) chosen = 0;
    printf("Using device [%d]\n", chosen);

    hr = ppDev[chosen]->ActivateObject(IID_PPV_ARGS(&pSrc));
    CK(hr, "ActivateObject");
    hr = MFCreateAttributes(&pRAttr, 1);
    CK(hr, "MFCreateAttributes(reader)");
    pRAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE); /* YUY2/MJPEG -> RGB32 */
    hr = MFCreateSourceReaderFromMediaSource(pSrc, pRAttr, &pReader);
    CK(hr, "MFCreateSourceReaderFromMediaSource");

    printf("\nNative media types (stream 0):\n");
    for (DWORD t = 0; ; ++t) {
        IMFMediaType *nt = NULL;
        HRESULT h2 = pReader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, t, &nt);
        if (h2 == MF_E_NO_MORE_TYPES || FAILED(h2)) break;
        GUID sub; UINT32 w = 0, h3 = 0, num = 0, den = 1;
        ZeroMemory(&sub, sizeof(sub));
        nt->GetGUID(MF_MT_SUBTYPE, &sub);
        MFGetAttributeSize(nt, MF_MT_FRAME_SIZE, &w, &h3);
        MFGetAttributeRatio(nt, MF_MT_FRAME_RATE, &num, &den);
        printf("  [%lu] ", (unsigned long)t); PrintFourCC(&sub);
        printf("  %ux%u  @ %.2f fps\n", w, h3, den ? (double)num / den : 0.0);
        nt->Release();
    }

    hr = MFCreateMediaType(&pOut);
    CK(hr, "MFCreateMediaType");
    pOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pOut->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = pReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pOut);
    CK(hr, "SetCurrentMediaType RGB32");

    pReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur);
    MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, &W, &H);
    {
        UINT32 st = 0;
        if (SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &st))) stride = (LONG)st;
    }
    if (stride == 0) stride = (LONG)W * 4;
    printf("\nCapturing one RGB32 frame at %ux%u (stride %ld)...\n", W, H, stride);
    cur->Release(); cur = NULL;

    for (int tries = 0; tries < 120 && !got; ++tries) {
        DWORD streamIdx = 0, flags = 0; LONGLONG ts = 0;
        IMFSample *s = NULL;
        hr = pReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIdx, &flags, &ts, &s);
        if (FAILED(hr)) { printf("ReadSample hr=0x%08lx\n", (unsigned long)hr); break; }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (s) s->Release(); break; }
        if (!s) { Sleep(10); continue; }
        if (tries < 15) { s->Release(); continue; } /* warm-up: let AGC settle */
        pSample = s; got = 1;
    }
    if (!got || !pSample) { printf("No frame captured.\n"); goto done; }

    hr = pSample->ConvertToContiguousBuffer(&pBuf);
    CK(hr, "ConvertToContiguousBuffer");
    hr = pBuf->Lock(&data, &maxLen, &curLen);
    CK(hr, "Lock");

    {
        unsigned long long sum = 0, sumSq = 0;
        unsigned mn = 255, mx = 0;
        size_t npx = (size_t)W * H;
        LONG absStride = stride < 0 ? -stride : stride;
        for (UINT32 y = 0; y < H; ++y) {
            const BYTE *row = data + (size_t)y * absStride;
            for (UINT32 x = 0; x < W; ++x) {
                const BYTE *p = row + (size_t)x * 4;
                unsigned l = (unsigned)((p[2] * 299 + p[1] * 587 + p[0] * 114) / 1000);
                sum += l; sumSq += (unsigned long long)l * l;
                if (l < mn) mn = l;
                if (l > mx) mx = l;
            }
        }
        double mean = npx ? (double)sum / npx : 0;
        double var = npx ? ((double)sumSq / npx - mean * mean) : 0;
        printf("Luminance: min=%u mean=%.1f max=%u  stddev=%.1f\n", mn, mean, mx, var > 0 ? sqrt(var) : 0.0);
        printf("Interpretation: max>min with some stddev => a real (if dark) image;\n");
        printf("                a perfectly flat frame (max==min) => no live signal.\n");
    }

    hr = WriteBmp32(outPath, data, (LONG)W, (LONG)H, stride);
    if (SUCCEEDED(hr)) printf("Wrote %ls (%ux%u)\n", outPath, W, H);
    else printf("WriteBmp failed hr=0x%08lx\n", (unsigned long)hr);

done:
    if (pBuf) { pBuf->Unlock(); pBuf->Release(); }
    if (pSample) pSample->Release();
    if (cur) cur->Release();
    if (pOut) pOut->Release();
    if (pReader) pReader->Release();
    if (pRAttr) pRAttr->Release();
    if (pSrc) { pSrc->Shutdown(); pSrc->Release(); }
    if (ppDev) { for (UINT32 i = 0; i < count; ++i) if (ppDev[i]) ppDev[i]->Release(); CoTaskMemFree(ppDev); }
    if (pSel) pSel->Release();
    MFShutdown();
    CoUninitialize();
    return 0;
}

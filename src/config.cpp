/* config.cpp - see config.h */
#include "config.h"
#include <stdio.h>
#include <stdarg.h>

static wchar_t g_logPath[260] = L"";

void ShimLogInit(const wchar_t *path) {
    if (path && path[0]) { wcsncpy_s(g_logPath, path, _TRUNCATE); }
    else g_logPath[0] = 0;
}

void ShimLog(const char *fmt, ...) {
    if (!g_logPath[0]) return;
    FILE *f = NULL;
    if (_wfopen_s(&f, g_logPath, L"a") || !f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void IniStr(const wchar_t *ini, const wchar_t *sec, const wchar_t *key,
                   const wchar_t *def, wchar_t *out, DWORD cch) {
    GetPrivateProfileStringW(sec, key, def, out, cch, ini);
}
static int IniInt(const wchar_t *ini, const wchar_t *sec, const wchar_t *key, int def) {
    return (int)GetPrivateProfileIntW(sec, key, def, ini);
}

void LoadConfig(HMODULE hModule, ShimConfig *cfg) {
    /* defaults */
    cfg->deviceIndex  = -1;
    cfg->deviceName[0]= 0;
    cfg->standard     = STD_NTSC;
    cfg->captureWidth = 640;
    cfg->captureHeight= 480;
    cfg->promoteY8    = 1;
    cfg->testPattern  = 0;
    cfg->sliderMax    = 255;
    cfg->logFile[0]   = 0;

    /* locate pxcshim.ini next to the shim DLL */
    wchar_t dllPath[MAX_PATH] = L"";
    wchar_t ini[MAX_PATH] = L"";
    if (GetModuleFileNameW(hModule, dllPath, MAX_PATH)) {
        wchar_t *slash = wcsrchr(dllPath, L'\\');
        if (slash) {
            size_t n = (size_t)(slash - dllPath) + 1;
            wcsncpy_s(ini, dllPath, n);
            wcscat_s(ini, L"pxcshim.ini");
        }
    }
    if (!ini[0] || GetFileAttributesW(ini) == INVALID_FILE_ATTRIBUTES) {
        return; /* no ini - keep defaults */
    }

    cfg->deviceIndex  = IniInt(ini, L"device", L"index", cfg->deviceIndex);
    IniStr(ini, L"device", L"name", L"", cfg->deviceName, 256);
    cfg->captureWidth = IniInt(ini, L"video", L"width",  cfg->captureWidth);
    cfg->captureHeight= IniInt(ini, L"video", L"height", cfg->captureHeight);
    cfg->promoteY8    = IniInt(ini, L"video", L"promote_y8", cfg->promoteY8);
    cfg->testPattern  = IniInt(ini, L"video", L"test_pattern", cfg->testPattern);
    cfg->sliderMax    = IniInt(ini, L"video", L"slider_max", cfg->sliderMax);

    wchar_t std[32] = L"";
    IniStr(ini, L"video", L"standard", L"NTSC", std, 32);
    if      (!_wcsicmp(std, L"PAL"))  cfg->standard = STD_PAL;
    else if (!_wcsicmp(std, L"AUTO")) cfg->standard = STD_AUTO;
    else                              cfg->standard = STD_NTSC;

    IniStr(ini, L"log", L"file", L"", cfg->logFile, 260);
}

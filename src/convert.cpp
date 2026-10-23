/* convert.cpp - see convert.h */
#include "convert.h"
#include "pxcshim.h"

int PbitsBytesPerPixel(int t) {
    switch (t) {
        case PBITS_Y8:     return 1;
        case PBITS_RGB15:  case PBITS_RGB16: case PBITS_YUV422: return 2;
        case PBITS_RGB24:  return 3;
        case PBITS_RGB32:  return 4;
        default:           return 0;
    }
}

static inline BYTE Luma(BYTE b, BYTE g, BYTE r) {
    return (BYTE)((r * 299 + g * 587 + b * 114) / 1000);
}

void ConvertRGB32(const BYTE *src, int srcW, int srcH, int srcStride,
                  BYTE *dst, int dstW, int dstH, int pbitsType) {
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    for (int y = 0; y < dstH; ++y) {
        int sy = (int)((long long)y * srcH / dstH);
        if (sy >= srcH) sy = srcH - 1;
        const BYTE *srow = src + (size_t)sy * srcStride;

        if (pbitsType == PBITS_YUV422) {
            /* YUY2: Y0 U Y1 V per 2 pixels */
            BYTE *d = dst + (size_t)y * dstW * 2;
            for (int x = 0; x < dstW; x += 2) {
                int sx0 = (int)((long long)x * srcW / dstW); if (sx0 >= srcW) sx0 = srcW-1;
                int x1  = (x + 1 < dstW) ? x + 1 : x;
                int sx1 = (int)((long long)x1 * srcW / dstW); if (sx1 >= srcW) sx1 = srcW-1;
                const BYTE *p0 = srow + (size_t)sx0 * 4;
                const BYTE *p1 = srow + (size_t)sx1 * 4;
                int y0 = Luma(p0[0], p0[1], p0[2]);
                int y1 = Luma(p1[0], p1[1], p1[2]);
                /* BT.601 U/V from the first pixel of the pair */
                int u = ((-38 * p0[2] - 74 * p0[1] + 112 * p0[0]) >> 8) + 128;
                int v = ((112 * p0[2] - 94 * p0[1] - 18 * p0[0]) >> 8) + 128;
                if (u < 0) u = 0; if (u > 255) u = 255;
                if (v < 0) v = 0; if (v > 255) v = 255;
                d[0] = (BYTE)y0; d[1] = (BYTE)u; d[2] = (BYTE)y1; d[3] = (BYTE)v;
                d += 4;
            }
            continue;
        }

        int bpp = PbitsBytesPerPixel(pbitsType);
        if (bpp == 0) bpp = 4;
        BYTE *d = dst + (size_t)y * dstW * bpp;
        for (int x = 0; x < dstW; ++x) {
            int sx = (int)((long long)x * srcW / dstW);
            if (sx >= srcW) sx = srcW - 1;
            const BYTE *p = srow + (size_t)sx * 4;   /* BGRX */
            BYTE b = p[0], g = p[1], r = p[2];
            switch (pbitsType) {
                case PBITS_Y8:
                    *d++ = Luma(b, g, r);
                    break;
                case PBITS_RGB15: {
                    unsigned short v = (unsigned short)(((b >> 3) ) | ((g >> 3) << 5) | ((r >> 3) << 10));
                    d[0] = (BYTE)(v & 0xFF); d[1] = (BYTE)(v >> 8); d += 2;
                    break; }
                case PBITS_RGB16: {
                    unsigned short v = (unsigned short)(((b >> 3) ) | ((g >> 2) << 5) | ((r >> 3) << 11));
                    d[0] = (BYTE)(v & 0xFF); d[1] = (BYTE)(v >> 8); d += 2;
                    break; }
                case PBITS_RGB24:
                    d[0] = b; d[1] = g; d[2] = r; d += 3;
                    break;
                case PBITS_RGB32:
                default:
                    d[0] = b; d[1] = g; d[2] = r; d[3] = 0xFF; d += 4;
                    break;
            }
        }
    }
}

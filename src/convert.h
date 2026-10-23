/* convert.h - convert a top-down BGRX (RGB32) source frame into any PXC FRAME
 * pixel format, scaling (nearest-neighbor) from src size to dst size. */
#ifndef PXCSHIM_CONVERT_H
#define PXCSHIM_CONVERT_H
#include <windows.h>

/* src: top-down BGRX, srcStride bytes/row. dst: tight-packed in `pbitsType`
 * (PBITS_* from pxcshim.h), dstW*dstH pixels. */
void ConvertRGB32(const BYTE *src, int srcW, int srcH, int srcStride,
                  BYTE *dst, int dstW, int dstH, int pbitsType);

/* bytes per pixel for a PBITS_* type (0 if unknown). */
int  PbitsBytesPerPixel(int pbitsType);

#endif

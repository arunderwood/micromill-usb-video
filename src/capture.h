/* capture.h - Media Foundation capture engine for the shim.
 * Runs on its OWN thread with its own COM/MF init, so it is independent of the
 * host app's (MicroMill's) COM apartment. Keeps the latest frame as BGRX;
 * CaptureReadInto converts+scales it into a caller FRAME buffer. */
#ifndef PXCSHIM_CAPTURE_H
#define PXCSHIM_CAPTURE_H
#include <windows.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CaptureEngine CaptureEngine;

/* Create + start the capture thread. Returns NULL only on gross failure
 * (out of memory); a device that is absent/unopenable still returns a valid
 * engine whose CaptureIsLive() stays FALSE, so the shim can report "no signal"
 * instead of crashing. */
CaptureEngine *CaptureCreate(const ShimConfig *cfg);
void           CaptureDestroy(CaptureEngine *e);

/* TRUE if a frame arrived within the last ~1s. */
BOOL CaptureIsLive(CaptureEngine *e);

/* Native capture dimensions (the MF output size). */
int CaptureWidth(CaptureEngine *e);
int CaptureHeight(CaptureEngine *e);

/* Convert+scale the latest frame into dest (tight-packed pbitsType, dw*dh).
 * Returns TRUE if a frame was available. */
BOOL CaptureReadInto(CaptureEngine *e, void *dest, int dw, int dh, int pbitsType);

/* Set a camera picture control. prop = VideoProcAmp_* (0=Brightness,1=Contrast,
 * 2=Hue,3=Saturation,...); norm = 0..1. Returns TRUE if the device supports it. */
BOOL CaptureSetControl(CaptureEngine *e, int prop, double norm);

#ifdef __cplusplus
}
#endif
#endif

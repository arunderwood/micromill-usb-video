# How it works

## Why replace *this* DLL

MicroMill's video has two halves: a **capture** half (the PXC200 card + driver)
and a **display** half (a DirectDraw hardware overlay living inside
`PXC200_Support.dll`). On a modern GPU that overlay is emulated/broken — it picks
the wrong pixel format (color noise) and sits at a fixed screen position that
ignores the window, and it fails outright over RDP.

Because the broken display code lives in `PXC200_Support.dll`, replacing only the
capture seam is not enough; the display has to be redone too. This project
therefore replaces `PXC200_Support.dll` with a renderer that uses GDI instead of
DirectDraw, which fixes capture and display together.

(An earlier approach that replaced only the capture DLL, `pxc2_nt.dll`, lives in
[`../experimental/`](../experimental/) for reference. It works as a capture seam
but leaves MicroMill's broken DirectDraw display in place, so it is not the
shipped solution.)

## Flow

MicroMill drives the DLL roughly like this:

- `PXC_InitializeFG` — the shim loads `pxcshim.ini` and starts the Media
  Foundation capture engine on its own thread (its own COM/MF init, independent
  of MicroMill's apartment). The capture thread forces the NTSC line standard,
  requests RGB32 output (MF's video processor converts YUY2/NV12/MJPG → RGB32 and
  scales), and keeps the most recent frame.
- `PXC_InitializeDD(hwnd)` — the shim records MicroMill's video window and sizes
  its annotation + composite buffers to that window's client area.
- `PXC_GetOverlayDC` — MicroMill draws its crosshair/scale onto a persistent
  offscreen 32-bit DIB the shim hands back. A display thread composites that DIB's
  non-black pixels over each captured frame (video stretched to the window), then
  blits the result with GDI at ~30 fps. The annotations survive the video refresh
  and land correctly centered over the stretched image.
- `PXC_SetParameter` — maps MicroMill's picture sliders onto the device's
  `IAMVideoProcAmp` brightness/contrast/hue/saturation.
- `PXC_VideoPresent` / frame-grab entry points report live-signal status and copy
  the current frame when MicroMill asks for a still.

The capture engine and the pixel-format converter are shared with the
`experimental/` capture-only shim; see [`../src/`](../src/) for the source.

# micromill-usb-video

Give the 2009 **MicroMill** microscope/mill control application live video from a
**standard USB / S-Video capture device** — no PCI frame grabber, no kernel
driver, no driver signing, and **no changes to MicroMill.exe**.

MicroMill was built around the **Imagenation PXC200A** (Bt878) PCI frame grabber
and its XP-era `\\.\PXC` kernel driver. On modern 64-bit Windows that driver has
no working build, so the video pane is dead. This project replaces one DLL in the
MicroMill folder with a clean-room, **pure user-mode** DLL that captures from any
off-the-shelf UVC capture dongle via **Media Foundation** and paints the video
into MicroMill's window with **GDI**. Secure Boot stays on; nothing needs signing.

> **Clean-room / legal:** this is an independent reimplementation of a documented
> interface, built for interoperability. It contains **no** vendor code, binaries,
> headers, or decompilation. Please read [`NOTICE`](NOTICE) before using or
> redistributing.

---

## What it does

- **Drop-in replacement for `PXC200_Support.dll`.** MicroMill loads that DLL and
  calls a small set of `PXC_*` functions to show video. The replacement
  implements those same entry points, but instead of driving the PXC200 card and
  displaying through a DirectDraw hardware overlay, it:
  1. captures frames from a USB capture device with Media Foundation, and
  2. renders each frame into MicroMill's video window with plain GDI
     (`SetDIBitsToDevice`), compositing MicroMill's own crosshair/scale
     annotations on top.
- **Correct colors, tracks the window, works over RDP.** The original overlay
  path produced color-noise and a fixed-position image on modern GPUs; the GDI
  path has none of those problems.
- **Any standard capture device.** Anything that enumerates as a normal
  video-capture source (USB UVC S-Video/composite/HDMI dongle) works. Device,
  resolution, and TV standard are set in `pxcshim.ini` — no rebuild needed.
- **Picture controls.** MicroMill's brightness/contrast/hue/saturation are mapped
  to the camera's `IAMVideoProcAmp` controls when the device supports them.
- **Trivially reversible.** Install backs up the original DLL; uninstall restores
  it.

## Why replace *this* DLL

MicroMill's video has two halves: a **capture** half (the PXC200 card + driver)
and a **display** half (a DirectDraw hardware overlay living inside
`PXC200_Support.dll`). On a modern GPU that overlay is emulated/broken — it picks
the wrong pixel format (color noise) and sits at a fixed screen position that
ignores the window, and it fails outright over RDP. Because the broken display
code lives in `PXC200_Support.dll`, replacing only the capture seam is not enough;
the display has to be redone too. This project therefore replaces
`PXC200_Support.dll` with a renderer that uses GDI instead of DirectDraw, which
fixes capture and display together.

(An earlier approach that replaced only the capture DLL, `pxc2_nt.dll`, lives in
[`experimental/`](experimental/) for reference. It works as a capture seam but
leaves MicroMill's broken DirectDraw display in place, so it is not the shipped
solution.)

## Requirements

- Windows (tested on Windows 10 x64).
- A USB video-capture device that appears as a standard capture source (e.g. a
  generic USB S-Video/composite dongle). Windows' inbox `usbvideo.sys` drives
  these — no driver install.
- The analog video you want to show, wired into that dongle.
- To build: **Visual Studio 2022** with the **C++ x86 (32-bit) toolchain**. Media
  Foundation headers and libs ship with the Windows SDK. The DLL must be 32-bit
  to match MicroMill.

## Build

From [`src/`](src/):

```
build.cmd
```

This produces `src\build\PXC200_Support.dll` (x86) and prints its exports and
machine type so you can confirm it built 32-bit. If your Visual Studio is not at
the default path, set `VS` first, e.g.
`set VS=C:\Program Files\Microsoft Visual Studio\2022\Professional`.

## Install

1. **Identify a capture device and its formats (recommended first).** Build and
   run the probe in [`experimental/probe/`](experimental/probe/) to list the
   capture devices on the machine and the exact resolutions/formats each one
   offers, and to grab a single test frame. Use it to pick the right `index`/
   `name` and `width`/`height` for your `pxcshim.ini`.
2. **Configure (optional).** Copy [`pxcshim.ini.sample`](pxcshim.ini.sample) to
   `pxcshim.ini` and edit it (device, TV standard, size, slider range). Defaults
   (no file) are: first device, NTSC, 640×480. See **Configuration** below.
3. **Install.** From `src\`, with the DLL built:

   ```
   install.cmd "C:\Program Files (x86)\NWR-ESI\MicroMill"
   ```

   This backs up the stock `PXC200_Support.dll` to
   `PXC200_Support.dll.pxc-backup`, drops the shim in its place, and copies
   `pxcshim.ini` if you created one at the repo root. Run it from an elevated
   prompt if the target is under `Program Files`.
4. **Launch MicroMill at the physical console.** MicroMill's startup may command
   stage/Z motion — launch it only with the mill in a safe state. The video pane
   should now show live USB video with the crosshair/scale overlay.

Revert at any time:

```
uninstall.cmd "C:\Program Files (x86)\NWR-ESI\MicroMill"
```

## Configuration (`pxcshim.ini`)

Place `pxcshim.ini` in the MicroMill folder, next to the installed DLL. All keys
are optional; sensible defaults apply if the file is absent. See
[`pxcshim.ini.sample`](pxcshim.ini.sample) for the full annotated template.

| Section    | Key          | Default | Meaning |
|------------|--------------|---------|---------|
| `[device]` | `index`      | `-1`    | Capture device by enumeration order; `-1` = first available. |
| `[device]` | `name`       | (empty) | Case-insensitive substring of the device friendly name; overrides `index`. |
| `[video]`  | `standard`   | `NTSC`  | `NTSC` (MicroMill is NTSC-only), `PAL`, or `AUTO`. |
| `[video]`  | `width`      | `640`   | Capture/output width requested from the device. |
| `[video]`  | `height`     | `480`   | Capture/output height requested from the device. |
| `[video]`  | `slider_max` | `255`   | Max value of MicroMill's picture sliders, used to scale onto the camera's control range. |
| `[video]`  | `promote_y8` | `1`     | Write RGB32 into sub-32-bit frame buffers. Leave on. |
| `[video]`  | `test_pattern`| `0`    | Paint a diagnostic gradient/grid instead of video (debugging). |
| `[log]`    | `file`       | (empty) | Path to a diagnostic log; empty = logging off. |

## How it works (overview)

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
`experimental/` capture-only shim; see [`src/`](src/) for the source.

## Scope and limitations

- **Console vs RDP:** the GDI renderer works over RDP as well as at the console.
  (MicroMill's *original* DirectDraw display did not — that was one of the
  motivations for the GDI rewrite.)
- **Analog camera mux is gone.** The original PXC200 card did analog input
  switching (e.g. microscope vs drill camera) that the USB path cannot reproduce
  on its own. With a single dongle you get a single input. Multi-input dongles
  use the device's default input; explicit crossbar input selection
  (DirectShow `IAMCrossbar`) is a documented future addition, not yet wired.
- **NTSC-oriented.** MicroMill is NTSC-only; the engine forces NTSC by default.
  PAL/AUTO are selectable in the ini for other uses.
- **One application target.** This has been developed against MicroMill's use of
  `PXC200_Support.dll`. Other Imagenation-SDK applications use a different binding
  (see `experimental/`) and are not a direct target.
- **Hardware safety is on you.** MicroMill controls a real mill; see `NOTICE`.

## Repository layout

```
src/                     The shipped solution: GDI PXC200_Support.dll replacement
  pxcsupport.cpp           PXC_* entry points + GDI compositor/renderer
  pxcsupport.def           exported PXC_* names
  capture.cpp/.h           Media Foundation capture engine (own thread, NTSC)
  config.cpp/.h            pxcshim.ini loader + optional logger
  convert.cpp/.h           RGB32 -> Y8/RGB15/RGB16/RGB24/RGB32/YUY2 + scaling
  pxcshim.h                FRAME struct + PXC vtable interface (ABI) definitions
  build.cmd                builds build\PXC200_Support.dll (x86)
  install.cmd/uninstall.cmd  swap the shim into/out of a MicroMill folder (+backup)
experimental/            Earlier, superseded pxc2_nt.dll capture shim + tools
  shim.cpp, pxc2_nt.def, build.cmd
  probe/                   Media Foundation device/format probe (diagnostic)
  test/                    standalone end-to-end harness for the pxc2_nt shim
pxcshim.ini.sample       annotated config template
LICENSE                  MIT
NOTICE                   clean-room / interoperability / trademark statement
```

## License

[MIT](LICENSE). See [`NOTICE`](NOTICE) for the clean-room, interoperability, and
trademark statement, and for your responsibility to license any third-party
software you use this with.

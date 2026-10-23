# micromill-usb-video

Bring live video back to the **New Wave Research MicroMill** (originally
Merchantek) using a standard USB / S-Video capture device — no PCI frame grabber,
no kernel driver, no driver signing, and **no changes to the MicroMill
application**.

## The problem

The MicroMill is a computer-controlled micro-drilling / sample-preparation
system: an operator uses a **live microscope camera view** to position a fine
drill and mill out tiny powder samples (tens to hundreds of microns) from
carbonates, shells, corals and similar material for geochemical and isotopic
analysis. That live view is how you aim the drill — lose it and the instrument is
very hard to use.

The video originally came from an **Imagenation PXC200A** (Bt878) PCI frame
grabber driven by an XP-era `\\.\PXC` kernel driver. On modern 64-bit Windows
that driver has no working build, so the video pane goes dead.

## The solution

Replace one DLL in the MicroMill folder with a clean-room, **pure user-mode** DLL
that captures from any off-the-shelf USB (UVC) capture dongle via **Media
Foundation** and paints the video into MicroMill's window with **GDI**, including
the app's own crosshair/scale overlay. Feed the mill's existing analog camera
into an inexpensive USB S-Video dongle and the video pane comes back — with
correct colors, tracking the window, and working over RDP. Secure Boot stays on,
nothing needs signing, and `MicroMill.exe` is untouched.

> **Clean-room / legal:** this is an independent reimplementation of a documented
> interface, built for interoperability. It contains **no** vendor code, binaries,
> headers, or decompilation. Please read [`NOTICE`](NOTICE) before using or
> redistributing.

## What it does

- **Drop-in replacement for `PXC200_Support.dll`** — implements the same `PXC_*`
  entry points MicroMill calls, but captures from USB and renders with GDI
  instead of driving the PXC200 card and its dead DirectDraw overlay.
- **Any standard capture device** — anything that enumerates as a normal
  video-capture source (USB UVC S-Video/composite/HDMI dongle). Device,
  resolution, and TV standard are set in `pxcshim.ini`; no rebuild needed.
- **Keeps MicroMill's overlay** — the crosshair/scale annotations are composited
  on top of the live video.
- **Picture controls** — brightness/contrast/hue/saturation map to the camera's
  `IAMVideoProcAmp` controls where the device supports them.
- **Trivially reversible** — install backs up the original DLL; uninstall
  restores it.

For the design and the mechanics, see [`docs/how-it-works.md`](docs/how-it-works.md).

## Requirements

- Windows (tested on Windows 10 x64).
- A USB video-capture device that appears as a standard capture source (e.g. a
  generic USB S-Video/composite dongle). Windows' inbox `usbvideo.sys` drives
  these — no driver install.
- The mill's analog camera signal wired into that dongle.
- To build: **Visual Studio 2022** with the **C++ x86 (32-bit) toolchain** (Media
  Foundation ships with the Windows SDK). The DLL must be 32-bit to match
  MicroMill.

## Build

From [`src/`](src/):

```
build.cmd
```

Produces `src\build\PXC200_Support.dll` (x86) and prints its exports and machine
type. If Visual Studio isn't at the default path, set `VS` first, e.g.
`set VS=C:\Program Files\Microsoft Visual Studio\2022\Professional`.

## Install

1. (Recommended) Identify your capture device and its formats with the probe in
   [`experimental/probe/`](experimental/probe/), then set `index`/`name` and
   `width`/`height` accordingly.
2. (Optional) Copy [`pxcshim.ini.sample`](pxcshim.ini.sample) to `pxcshim.ini`
   and edit it. Defaults (no file): first device, NTSC, 640×480. Full key
   reference: [`docs/configuration.md`](docs/configuration.md).
3. From `src\`, with the DLL built (elevated prompt if the target is under
   `Program Files`):

   ```
   install.cmd "C:\Program Files (x86)\NWR-ESI\MicroMill"
   ```

   This backs up the stock `PXC200_Support.dll` to `….pxc-backup`, installs the
   shim, and copies `pxcshim.ini` if present.
4. Launch MicroMill **only with the mill in a safe state** (startup may command
   stage/Z motion). The video pane should show live USB video with the overlay.

Revert at any time:

```
uninstall.cmd "C:\Program Files (x86)\NWR-ESI\MicroMill"
```

## Documentation

- [`docs/how-it-works.md`](docs/how-it-works.md) — why this DLL, and the
  capture → composite → GDI render flow.
- [`docs/configuration.md`](docs/configuration.md) — full `pxcshim.ini` reference.
- [`docs/limitations.md`](docs/limitations.md) — scope, RDP, camera-mux, NTSC.

## Repository layout

```
src/                     The shipped solution: GDI PXC200_Support.dll replacement
  pxcsupport.cpp/.def      PXC_* entry points + GDI compositor/renderer
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
docs/                    Design notes, configuration reference, limitations
pxcshim.ini.sample       annotated config template
LICENSE                  MIT
NOTICE                   clean-room / interoperability / trademark statement
```

## License

[MIT](LICENSE). See [`NOTICE`](NOTICE) for the clean-room, interoperability, and
trademark statement, and for your responsibility to license any third-party
software you use this with.

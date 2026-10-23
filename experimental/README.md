# experimental / — earlier approaches and diagnostics

These files are kept for reference and are **not** the shipped solution. The
shipped solution is the GDI `PXC200_Support.dll` replacement in
[`../src/`](../src/). Everything here is original, clean-room code (see
[`../NOTICE`](../NOTICE)).

## `pxc2_nt.dll` capture shim — `shim.cpp` + `pxc2_nt.def`

A user-mode drop-in replacement for the **capture** DLL (`pxc2_nt.dll`). It
presents the Imagenation PXC runtime interface (the vtable that `OpenLibrary`
installs) to MicroMill / `PXC200_Support.dll`, but sources frames from a USB
capture device via the same Media Foundation engine used by the shipped solution.

**Why it was superseded:** replacing only the capture seam leaves MicroMill's
*display* half — a legacy DirectDraw hardware overlay inside `PXC200_Support.dll`
— in place. That overlay is broken on modern GPUs (wrong pixel format → color
noise; fixed screen position that ignores the window) and fails over RDP. No fix
for the display is reachable from the capture seam, so the project moved to
replacing `PXC200_Support.dll` with a GDI renderer instead. This capture shim is
still a correct, validated capture seam; it just isn't sufficient on its own.

Build (produces `build\pxc2_nt.dll`, x86):

```
build.cmd
```

## `probe/` — Media Foundation capture probe (diagnostic)

`mfprobe.cpp` is a small standalone x64 console tool. It enumerates the video-
capture devices on the machine, prints each one's native media types (FOURCC +
resolution + frame rate), opens the chosen device, grabs one RGB32 frame to a
`.bmp`, and prints luminance stats so a dark-but-live signal is distinguishable
from a flat no-signal screen.

Use it to pick the right device `index`/`name` and `width`/`height` for your
`pxcshim.ini`, and to confirm your analog source is actually reaching the dongle.

```
probe\build-probe.cmd
probe\build\mfprobe.exe            :: auto-pick, writes frame.bmp
probe\build\mfprobe.exe 0 out.bmp  :: device 0 -> out.bmp
```

## `test/` — end-to-end harness for the capture shim

`shimtest.cpp` loads `pxc2_nt.dll` exactly the way `PXC200_Support.dll` does
(`LoadLibrary` → `GetProcAddress("OpenLibrary")` → the PXC vtable →
`AllocateFG`/`AllocateBuffer`/`GrabContinuous`/`Grab`) and saves captured frames
to BMP. It never touches MicroMill, so it carries no mill-hardware risk — a quick
way to prove the capture shim works against a real dongle.

```
test\build-test.cmd
test\build\shimtest.exe ..\build\pxc2_nt.dll C:\Temp 32
```

(`type` arg: 32 = RGB32 (default), 24, 16, 15, 8.)

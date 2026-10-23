# Scope and limitations

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
  (see [`../experimental/`](../experimental/)) and are not a direct target.
- **Hardware safety is on you.** MicroMill controls a real mill; launch it only
  with the mill in a safe state, and see [`../NOTICE`](../NOTICE).

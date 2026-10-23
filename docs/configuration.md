# Configuration (`pxcshim.ini`)

Place `pxcshim.ini` in the MicroMill folder, next to the installed
`PXC200_Support.dll`. All keys are optional; sensible defaults apply if the file
is absent (first device, NTSC, 640×480). Start from
[`../pxcshim.ini.sample`](../pxcshim.ini.sample), which is the same template with
inline comments.

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

## Finding the right device and format

Use the Media Foundation probe in [`../experimental/probe/`](../experimental/probe/)
to list the capture devices on the machine and the exact resolutions/formats each
one offers, and to grab a single test frame. That tells you which `index`/`name`
to set and which `width`/`height` your dongle actually supports. See the
[experimental README](../experimental/README.md) for how to build and run it.

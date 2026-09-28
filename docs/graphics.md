# Graphics foundation (WP-19)

This covers `engine/include/as3d/{platform,gfx,math}.h`, the headless/windowed context
backends in `engine/src/platform/`, the GLES 3.0 rendering layer in `engine/src/render/`,
and `apps/viewer` (`as3d_viewer`). Everything here targets OpenGL ES 3.0
(`#version 300 es`) so it runs unchanged on Android once `engine/src/platform/` grows an
Android backend; `render` itself never touches SDL or EGL.

## Conventions

### Texture orientation

`as3d::Image` (see `as3d/image.h` and `docs/spec/tga.md`) always stores row 0 as the
**top** row of the image, regardless of the source file's on-disk orientation.
`Texture2D::create` uploads those rows to GL exactly as given, with no flip. GL's first
uploaded row becomes texture row 0, and this engine **defines that as v = 0**. The
consequence, spelled out because every later model/level/UI package depends on it:

- `v = 0` is the TOP of the texture, `v = 1` is the BOTTOM. This is the mirror of the
  "traditional" OpenGL convention (bottom-left origin) that most tutorials assume.
- UV `(0, 0)` is the visual top-left texel of the source image.
- Nothing in the pipeline flips V to compensate. There is exactly one convention (this
  one), applied consistently from `decodeTga` through to quad/mesh UVs.

`RenderTarget::readPixels` is the one place a flip *does* happen, and for an unrelated
reason: OpenGL's window/framebuffer coordinate system has y = 0 at the bottom by
specification, so `glReadPixels` always returns its first row as the bottom of what was
rendered. `readPixels` flips that back into an `as3d::Image` (row 0 = top) before
returning it, so a vertex placed at the top of clip space (NDC y = +1) ends up in the
low-numbered rows of the resulting `Image`. `apps/tests/gfx_test.cpp` checks this
end-to-end, both for a rendered triangle and for a textured quad built from a synthetic
2x2 image.

Verified visually (see "What the three PNGs show" below): `gfx\logo.tga` and
`menu\cursor_1.tga`, rendered through `as3d_viewer texture`, come out upright with no
red/blue channel swap.

### Matrix and vector conventions

- `Mat4`/`Mat3` are column-major, stored as a flat array (`m[col*4+row]` / `m[col*3+row]`),
  laid out exactly as `glUniformMatrix4fv(..., transpose=GL_FALSE, ...)` expects.
- Vectors are column vectors; `M * v` transforms `v`. `A * B` means "apply B, then A".
- `perspective()`, `ortho()` and `lookAt()` use the classic right-handed OpenGL
  convention: view space looks down -Z, +X is right, +Y is up, NDC z is in [-1, 1].
- `anglesToAxis()` reproduces id Software's original Quake `AngleVectors()` **exactly**,
  quirks included: at angles (0,0,0), forward = (1,0,0), up = (0,0,1) -- Quake's Z-up,
  X-forward world space. This is independent of the Y-up space `perspective()`/`lookAt()`
  use for rendering; reconciling the two (i.e. where the camera/world coordinate system
  used by levels and models actually lives) is left to the model/level specs that
  motivated this note in the work package -- **not decided by WP-19**. `anglesToAxis`'s
  `right` output points to the viewer's LEFT, not the right (`forward x right == -up`),
  faithfully preserving a well-known sign quirk in id's original code; the three vectors
  are still a mutually-orthogonal unit basis (see `math_test.cpp`), just left-handed.
  Callers that want an actual "strafe right" vector must negate it.

### Coordinate system (placeholder)

World/level/model space conventions (units, up axis for gameplay code, etc.) are left
for the level and model format work packages to fill in; nothing in WP-19 assumes one.
The renderer only assumes standard GL clip space and the texture convention above.

## Headless rendering on this machine

`as3d::createGraphicsContext({.headless = true, ...})` (`engine/src/platform/egl_headless.cpp`)
tries, in order, until one produces a working GLES 3.0 pbuffer context:

1. **`EGL_EXT_platform_device`**: enumerate every EGL device with `eglQueryDevicesEXT`
   (no window system involved at all) and try `eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, device, ...)`
   for each one.
2. **`EGL_PLATFORM_SURFACELESS_MESA`**: `eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, ...)`.
3. **Plain `eglGetDisplay(EGL_DEFAULT_DISPLAY)`**.

For each candidate, it calls `eglInitialize`, `eglChooseConfig` (GLES3-capable, pbuffer
surface, RGBA8 + 24-bit depth), `eglCreatePbufferSurface`, `eglCreateContext`
(`EGL_CONTEXT_CLIENT_VERSION = 3`) and `eglMakeCurrent`; the first candidate that gets
all the way through wins, and `GraphicsContext::description()` records which one it was.

**On this development machine**, method 1 succeeds immediately: `eglQueryDevicesEXT`
finds the NVIDIA GPU as an EGL device (`/dev/dri/card2`) via `EGL_EXT_device_drm`, and
`as3d_viewer info` / the test suite report:

```
headless-egl (platform=device[0] (/dev/dri/card2), EGL 1.5): vendor=NVIDIA Corporation
renderer=NVIDIA GeForce GTX 1060/PCIe/SSE2 version=OpenGL ES 3.2 NVIDIA 580.173.02
```

No `DISPLAY`, no X server, no Wayland compositor, and no `xvfb` are involved -- this is
real GPU-accelerated rendering through direct EGL device enumeration. This is also why
`tools/ci.sh` does not need Xvfb: headless EGL device enumeration works without any
window system at all, on any machine with a GPU exposed through `/dev/dri`.

### Guaranteed software fallback

If no GPU is reachable this way (e.g. a container with no `/dev/dri` access, or a
machine with only Mesa and no working DRI device), method 2 falls back to Mesa's
software rasterizer (`llvmpipe`). This was verified on this machine by forcing Mesa's
EGL vendor and disabling hardware rendering:

```
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json LIBGL_ALWAYS_SOFTWARE=1 \
    as3d_viewer info
```

which reports `vendor=Mesa Project ... renderer=llvmpipe (LLVM ..., 256 bits)`. These two
environment variables matter for CI on a machine/container where the GPU device path
either does not exist or must not be used:

- `LIBGL_ALWAYS_SOFTWARE=1` -- forces Mesa to use its software rasterizer instead of a
  DRI hardware driver.
- `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json` -- only
  needed on a machine that *also* has a proprietary vendor's EGL ICD installed (e.g.
  NVIDIA's, as here) with higher glvnd priority than Mesa's; it forces glvnd to resolve
  EGL calls to Mesa instead. Not needed on a Mesa-only machine.

WP-19 does not force these automatically: the device-enumeration path already finds and
uses real hardware on this machine without them, which is both faster and a more
faithful test of the actual GLES driver a later CI run will hit. A CI runner that has no
GPU access at all should export both variables before invoking `tools/ci.sh` or
`as3d_tests`/`as3d_viewer` directly.

### `--window` and the SDL backend

`createGraphicsContext({.headless = false, ...})` (`engine/src/platform/sdl_window.cpp`)
creates an SDL2 window with a GLES 3.0 context (`SDL_GL_CONTEXT_PROFILE_ES`, 3.0). On a
machine with a real X11/Wayland session this opens a visible window. **On this
development machine** (no `DISPLAY`, no `WAYLAND_DISPLAY`), SDL2 itself falls back to
its own built-in `offscreen` video driver (`SDL_GetCurrentVideoDriver()` reports
`offscreen`) rather than failing -- so `as3d_viewer info` and `--window` report a
successful "window" context even though nothing is actually visible anywhere. This is
SDL's own fallback, not something WP-19 added; it means `--window` mode should only be
relied on interactively, on a machine that actually has a display, where clicking the
window's close button (or `SDL_QUIT`) ends the loop. Do not invoke `--window` in an
automated/headless context: with the `offscreen` driver there is no way to generate a
close event, so the render loop never exits on its own.

## Running the viewer

```
as3d_viewer info                                   # context descriptions (both backends)
as3d_viewer triangle --out out/triangle.png [--size 640x480]
AS3D_DATA_ROOT=/path/to/checkout \
  as3d_viewer texture 'gfx\logo.tga' --out out/texture.png
as3d_viewer cube --out out/cube.png
as3d_viewer <command> --window                     # interactive; needs a real display
```

`AS3D_DATA_ROOT` (or the repository root, if unset) must contain
`third_party_local/original/data/pak{0,1,2}.apk` for the `texture` command; see the
repository README for `tools/setup_data.sh`.

`out/` is gitignored: rendered PNGs of game assets (and of the synthetic demos, for
convenience) belong there or in `testdata/golden_png/` (also gitignored), never in a
commit -- a rendering of `gfx\logo.tga` or any other game asset is a copyrighted
derivative work.

## Comparing rendered images

`tools/imgdiff.py` (PIL only, no numpy) compares two PNGs with a per-channel tolerance
and a maximum allowed fraction of differing pixels:

```
python3 tools/imgdiff.py out/triangle.png testdata/golden_png/triangle.png \
    --tolerance 8 --max-diff-fraction 0.01 --diff-out out/triangle.diff.png
```

It prints the differing-pixel count/fraction and the largest single-channel difference
seen, optionally writes a visual diff image, and exits non-zero on a mismatch (2 on a
usage/IO error, such as a size mismatch). The tolerance exists because different GPUs/
drivers (e.g. this machine's NVIDIA driver vs. the `llvmpipe` software fallback) rasterize
the same draw calls with small, expected per-pixel differences -- an exact byte comparison
would be too strict to be useful across machines. Later work packages that want
regression protection for rendered output should capture a golden PNG once (into the
gitignored `testdata/golden_png/`, an path unaffected by this instruction not to commit
it) and compare against it with `imgdiff.py` in their own test script, the same way
`tools/ref/test_*.py` scripts are picked up by `tools/ci.sh`.

## Sanitizer run (`AS3D_CMAKE_ARGS=-DAS3D_SANITIZE=ON`)

`apps/tests/as3d_tests` passes cleanly under `-fsanitize=address,undefined` on this
machine, with one caveat: LeakSanitizer (built into ASan) reports ~1.8KB leaked across 3
allocations, all inside `libdbus-1.so` (`dbus_bus_register` /
`_dbus_message_loader_queue_messages` / ...), triggered from *inside* the closed-source
NVIDIA EGL driver's device enumeration (`eglQueryDevicesEXT`/`eglInitialize`, called by
`engine/src/platform/egl_headless.cpp`) -- not from any code in this repository. This was
confirmed with a standalone C program outside the build that does nothing but
`eglQueryDevicesEXT` + `eglInitialize` under the same ASan flags, which reproduces the
identical leak trace.

Since `tools/ci.sh` invokes `as3d_tests` directly (no wrapper that could set
`LSAN_OPTIONS=suppressions=...`), the suppression is compiled in via LSAN's
`__lsan_default_suppressions()` hook -- the same suppression-file syntax, just delivered
as a string baked into the binary instead of a path read at runtime -- defined in
`apps/tests/gfx_test.cpp` under `#if defined(__SANITIZE_ADDRESS__)`, narrowly scoped to
`leak:libdbus`. It does not suppress anything in engine or viewer code, and the
sanitizers themselves are not disabled or weakened anywhere.

## What the three PNGs show

(Rendered on this machine via the NVIDIA headless-EGL path described above; not
committed, per the rule above.)

- **`out/triangle.png`**: a single triangle on a dark near-black background. The apex
  (top of the image) is red, the bottom-left corner is green, the bottom-right corner is
  blue, with smooth colour interpolation across the interior -- confirms vertex colours,
  clip-space-to-window orientation (top vertex ends up in the top rows), and that
  red/blue are not swapped.
- **`out/texture.png`**: `menu\cursor_1.tga` (a 32x32 cursor sprite with real alpha)
  drawn on a quad over a procedural grey checkerboard, alpha-blended. The cursor's shape
  (an arrow with a soft drop shadow) reads upright and correctly oriented, and the
  checkerboard is visible through its transparent pixels -- confirms texture upload,
  the v=0-is-top convention end to end, and alpha blending. (`gfx\logo.tga`, the
  DivoGames splash logo, was also rendered as a spot check: readable, upright text and
  correct colours, e.g. its red bar renders red, not blue.)
- **`out/cube.png`**: a cube with an orange/blue checkerboard texture, rotated to show
  three faces, lit by a fixed directional light (the top face is brightest, the two
  visible side faces are dimmer at different levels depending on their angle to the
  light) and perspective-projected with correct convergence towards the (offscreen)
  vanishing points. No gaps or overlaps are visible at any edge, confirming depth testing
  and back-face culling (with the winding documented in `apps/viewer/cmd_cube.cpp`) both
  work together correctly.

## Tests

- `apps/tests/math_test.cpp`: matrix/vector identities, an affine-inverse round trip
  through a non-uniform scale, `anglesToAxis` orthonormality and known values (including
  the documented left-handed quirk), and perspective/ortho/lookAt against known points.
- `apps/tests/gfx_test.cpp`: creates a real headless GLES 3.0 context (see above) and
  renders into an `as3d::RenderTarget`. Checks: the background colour in a corner; a
  dominant red/green/blue near the respective triangle corners; an explicit
  flip-detector (a point that would flip from "mostly blue/green" to "mostly red" if the
  readback's row order were wrong); a `ShaderProgram` compile failure reporting an error
  instead of crashing (and that the object is usable again after a subsequent successful
  compile); and a synthetic 2x2 four-colour texture landing in the expected on-screen
  quadrant, exercising the texture-orientation convention end to end. If no headless
  context can be created at all, every test prints a loud `SKIPPED (no headless GLES
  context available)` message and passes -- but on this machine (and in CI, see above) a
  context is always created successfully, and these tests actually run.

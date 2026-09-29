# Web version: feasibility spike

**Verdict: feasible, with high confidence.** The engine and the real game loop run in
Chromium as WebAssembly with WebGL 2, and nothing had to change in the renderer, the shaders
or the engine modules. The game loop needed one small refactor: its body became a per-frame
function. Mission 1 plays with the bot, the keyboard and touch, and the SDL sound effects and
libopenmpt music play. On an Intel HD 630 integrated GPU it holds 60 fps with 2.6 ms of CPU
time per frame, and a browser frame matches the native render of the same simulation frame
except for antialiasing. What this spike does not cover is the product work around the
engine: the user supplying the paks, saving the profile, the menus, full screen and Safari.
That is listed in "What a real web version still needs" with rough sizes. Nothing was tested
on a phone, in Safari or in Firefox.

Date: 2026-09-29. Emscripten 6.0.10 (emsdk `sdk-releases-666337b525e673e769121856d175f6f52b8ead64-64bit`,
node 24.19.0), Chromium 153.0.8010.12 (Playwright's headless shell), development machine
(Intel i7 Kaby Lake with HD 630, GTX 1060, 15 GB).

## What was built

`apps/web_spike/` is a separate CMake project. It is configured only by `tools/web_build.sh`
through `emcmake`, and it is not part of the desktop build, Android or `tools/ci.sh`.

| Target | What it is |
|---|---|
| `as3d_web_level` (Stage A) | Level 1 as `as3d_viewer level 1 --scroll 500` shows it, drawn in the canvas and scrolling 0.5 units per frame. It calls the viewer's `renderLevel` every frame, which rebuilds the whole level and renders it offscreen, then blits the result. |
| `as3d_web_game` (Stage B) | `apps/game`'s session, view, audio bridge, touch overlay and the windowed `game_loop.cpp`, going straight into a mission as with `--level N` (no menus). It takes the page's query string as arguments. |
| `web/index.html` | The page. It loads `game_data.js` (the file packager's loader), then the app. |
| `measure.py` | Playwright measurement: frame statistics from wrapped `requestAnimationFrame` callbacks, memory, audio output level, screenshots and the console. |

The game data is packed at build time from `$AS3D_DATA_ROOT/third_party_local/original/data/pak{0,1,2}.apk`
into `game_data.data` (mounted at `/data`). It is written only into the gitignored site
directory.

## Measurements

These numbers come from a headless browser on the development machine, with the page served
from localhost. **SwiftShader** means Chromium's CPU renderer. Its numbers say little about any
real device, and they are listed only as a floor. **GPU** means ANGLE on the machine's Intel HD
630 through EGL (`--use-angle=gl-egl`), which is real hardware rendering: roughly a 2017 laptop
iGPU. Headless Vulkan gave no WebGL 2, and the GTX 1060 was not reachable from the browser.
Runs marked "loaded" overlapped with other jobs on this shared machine (load average about 5).
Logs and screenshots are in `out/web/` of the main checkout (gitignored).

### WebGL 2

WebGL 2 initialised in every run. Renderer strings (unmasked):

- GPU: `ANGLE (Intel, Mesa Intel(R) HD Graphics 630 (KBL GT2), OpenGL ES 3.2)`
- SwiftShader: `ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero) (0x0000C0DE)), SwiftShader driver)`

Across all runs, including missions 4, 8, 12, 16 and 20 (rain, particles, lights), there were
no shader compile or link failures, no GL errors and no JavaScript errors. The console showed
only these warnings:

- `ScriptProcessorNode is deprecated`: from SDL2's Emscripten audio backend.
- `GPU stall due to ReadPixels`: a performance note. It comes from the shadow silhouettes
  being baked with a readback at level load, and from Stage A's per-frame readback.
- Two data warnings that native builds print as well: `scripts\items\i_help.scr not found`,
  and the known missing `models\misc\hlanno2.tga`.

### Download size

| File | Raw | gzip -9 |
|---|---|---|
| `as3d_web_game.wasm` | 2 583 513 | 945 193 |
| `as3d_web_game.js` | 249 461 | 49 885 |
| `as3d_web_level.wasm` / `.js` (Stage A only) | 630 463 / 188 896 | 215 678 / 43 520 |
| `game_data.data` (the three paks) | 24 967 841 | 13 778 927 |
| `game_data.js` | 5 578 | 1 954 |
| `index.html` | 3 715 | 1 712 |

The engine a real page would ship is about 2.8 MB raw, or **1.0 MB gzipped**; libopenmpt is
most of the wasm. The data would come from the user, not over the network.

### Startup (from navigation start, localhost, fresh browser each run)

| | GPU | SwiftShader |
|---|---|---|
| wasm compiled and runtime ready | 226 to 373 ms | 202 to 350 ms |
| 25 MB of paks fetched (localhost) | about 0.27 s | same |
| game session, renderer and mission 1 loaded (`load_ms`) | 633 to 1977 ms (upper end loaded) | 707 ms |
| end of the first drawn game frame | 0.93 to 2.56 s | 0.91 to 0.97 s |
| Stage A first frame | 1.09 s | 1.49 s |

On a real connection the paks dominate. Here they would be the user's own files, read
locally.

### Frame times, mission 1, bot playing, 60 s

"rAF" is the interval between `requestAnimationFrame` callbacks and the time spent inside
them. `AS3D_PERF` is the game's own 5-second statistic: displayed fps, CPU work per frame, and
simulation steps dropped by the bounded catch-up.

| Run | rAF/s | interval mean / p95 / max (ms) | callback mean / p95 / max (ms) | AS3D_PERF |
|---|---|---|---|---|
| GPU, quiet | 60.8 | 16.7 / 16.9 / 83 | 2.6 / 4.3 / 22 | 59.4 to 60 fps, work 2.9 to 3.6 ms, 0 dropped |
| GPU, loaded | 55.7 | 18.1 / 27.6 / 135 | 5.8 / 19.6 / 71 | 45 to 52 fps, work 7 to 8 ms, 0 to 1 dropped |
| SwiftShader, quiet | 27.2 | 37.4 / 100 / 1371 | 3.6 / 9.8 / 1345 | 20 to 23 fps, work 3 to 4 ms, about 10 % of steps dropped |
| SwiftShader, loaded | 20.5 | 49.1 / 146 / 1371 | 5.5 / 12.9 / 1345 | 15 to 19 fps, 15 to 20 % dropped |

Other runs:

- Keyboard play and touch play on the GPU ran at 60 fps with 1.1 to 2.9 ms of work.
- A short run of each of missions 4, 8, 12, 16 and 20 on the GPU gave 60 fps and 0.8 to
  3.6 ms of work, except mission 16 during a load spike (40 fps).

For comparison, docs/android.md measures 1.3 to 2.9 ms of work per frame for the native
desktop build on the GTX 1060, so the wasm CPU cost is in the same range. Under SwiftShader
the wasm side stays at 3 to 4 ms and the rest is software rasterisation in the GPU process.
The 1.2 to 1.3 s hitches under SwiftShader happen when a new model type is drawn for the first
time and its textures load, the same thing the Android notes describe.

Stage A costs 330 ms (GPU) to 760 ms (SwiftShader) per frame. This is by design and says
nothing about the game: `renderLevel` reloads the level, rebuilds the terrain and reads the
image back on every call.

### Memory

- WebAssembly heap: 64 MB, the initial size. It never grew in any run, including 60 s of
  play with the paks loaded.
- JavaScript heap: 28 to 54 MB. The preloaded paks sit in an ArrayBuffer outside that count.
- Resident memory of all headless-browser processes together: 580 to 720 MB. The largest
  single process (the renderer or the GPU process) used 175 to 236 MB.

These numbers are rough (`ps` on the browser's processes). On a phone the relevant figure is
the wasm heap plus the data plus GL textures, about 150 MB, well within limits.

### Audio

The game creates SDL2's Web Audio output, and libopenmpt, compiled to wasm with native
WebAssembly exceptions, loads the mission's MO3 music without a warning. Tapping the output
with an `AnalyserNode` gave a non-silent signal in every sample: RMS up to 0.16, 59 of 59
samples above silence over 60 s.

The autoplay rule is handled by SDL2's port. Its audio callback resumes the `AudioContext`
once `navigator.userActivation.hasBeenActive` is true (after the first key, click or tap). On
browsers without `userActivation` it adds keydown, mousedown and touchstart listeners
instead. Our code needed nothing for this. The headless shell did not enforce the autoplay
policy even with `--autoplay-policy=user-gesture-required`: the context was already
"running" before any input. So "silent until the first key or tap" is established from the
port's code, not observed. Music and sound effects were not measured separately.

### Input

- Keyboard (no bot): held arrows, Ctrl and Shift, and Return confirmed the tutorial hint.
  The mission ran on, and the score rose from firing.
- Touch (`touch=1`, touch events sent over the DevTools protocol): SDL finger events arrive
  with the correct normalised positions (`AS3D_TOUCH down id=1 x=0.500 y=0.700 on=field`), the
  on-screen buttons are drawn, and dragging moved the helicopter.
- Multi-touch was not tested.

### Screenshots compared with native

- **Stage A**: the browser under SwiftShader at scroll 500 (`level_hold_swiftshader_0.png`),
  compared with `as3d_viewer level 1 --scroll 500` on the GTX 1060 (`level_native.png`). Mean
  absolute difference 0.43 of 255 per channel, 0.08 % of pixels differ by more than 8, and
  0.009 % by more than 32. They look identical: the same geometry, textures, fog, shadows,
  sprites and environment maps. (`tools/imgdiff.py` reports 9.4 % because it compares alpha.
  The native PNG keeps alpha below 255 where blending wrote it; a browser screenshot is
  opaque.)
- **Stage B**: frame 1800 of mission 1 with the bot. The browser (`frames=1800`, GPU,
  `game_bot_f1800_gpu_end.png`) is compared with `as3d_game --headless --bot --frames 1800
  --screenshot-every 1800` (`native/frame_001800.png`).
  - The simulation state is the same: every object and bullet is in the same place, and the
    score matches. The wasm build reproduces the fixed-step simulation.
  - Mean difference 0.59; 1.5 % of pixels differ by more than 8, only along polygon edges
    (`game_f1800_diff_x4.png`). The native headless path renders into a 4x MSAA target,
    while the canvas, like the desktop window, has no MSAA.

## What had to change

- **`apps/game/game_loop.cpp`**, the only shared source touched.
  - `GameWindow::run()` is now `start()`, then `frame()` while running, then `finish()`.
  - The loop's locals (`dt`, `last`, `acc`) became members.
  - The two `SDL_Delay` calls go through `idleDelay()`, which does nothing under
    `__EMSCRIPTEN__`.
  - Under `__EMSCRIPTEN__`, `runGameWindow` copies the options to the heap and hands
    `frame()` to `emscripten_set_main_loop_arg`.
  - Desktop and Android run the same statements in the same order as before; `game_loop.h`
    is unchanged. Not used: `ASYNCIFY`, `-sFULL_ES3`, `-sFULL_ES2` and pthreads.
- **No engine source and no module CMake file changed.** The spike's own CMake handles all of
  it:
  - The modules' `pkg_check_modules(sdl2|egl|glesv2)` resolve through a pkg-config wrapper to
    stub `.pc` files that expand to `-sUSE_SDL=2` and the WebGL 2 flags. A plain
    `PKG_CONFIG_LIBDIR` does not work, because Emscripten's toolchain file resets it on every
    `try_compile`.
  - The headless EGL backend is excluded, and the platform module's null stand-in
    (`android_stub.cpp`) is built in its place.
  - `openmpt_static` gets `-fwasm-exceptions` (it throws and catches).
- **GL calls and shaders: nothing changed.** Everything the engine uses is valid WebGL 2 with
  Emscripten's plain mapping: VAOs, `glBlitFramebuffer`, `glRenderbufferStorageMultisample`,
  32-bit indices, `GL_DEPTH_COMPONENT24`, RGBA8 textures and `glReadPixels`. The
  vertex data is always in buffer objects, never in client-side arrays. The GLSL ES 3.00
  shaders compiled as they are.

Two WebGL behaviours matter for a product:

1. The canvas gets an alpha channel because the window asks for 8 alpha bits, and the
   compositor treats it as premultiplied. It looks right over the page's black background. A
   product should request no alpha on the web (`SDL_GL_ALPHA_SIZE 0`), or the page colour
   bleeds through where blending writes alpha below 255.
2. `glReadPixels` is a synchronous GPU stall in WebGL. Today it only happens at level load
   (shadow silhouettes).

## What a real web version still needs

Rough sizes: S is up to 1 day, M is 2 to 4 days, L is a week or more.

| Item | Size | Notes |
|---|---|---|
| Paks from the user through a file picker | M | `<input type=file multiple>` works in every browser (`showOpenFilePicker` only in Chromium). Write the files into MEMFS (25 MB copy) before `callMain`, then keep them in IndexedDB or OPFS so the picker is needed once. Check the pak table of each file and say which are missing. The page then ships only the engine (1 MB gzipped): the copyrighted data cannot be hosted. The loose front-end files (`Settings.xml`, `gfx/logo2s.tga`) come from the same folder; `texts_v170.txt` needs the exe step (issue 080) ported to the page, or picked the same way. |
| Profile in browser storage | S | `userDataDir()` is `$HOME/.local/share/airstrike3d` in MEMFS, lost on reload. Mount IDBFS there and call `FS.syncfs` after each save (the game already saves at the right moments), and on `visibilitychange` / `pagehide`. |
| Menus and text input | S to M | The front end already runs in `game_loop` (`o.frontend`). It needs the extra files above, the web flow config (as `android_main.cpp` does: no quit, no video options) and a test pass. On desktop browsers SDL's text input works. On phones the front end's own keyboard in touch mode avoids the system keyboard. |
| Keys the browser keeps | S | Pages cannot stop Ctrl+W, Ctrl+T, Ctrl+N or Ctrl+Tab, and Ctrl is the default fire key: Ctrl+W closes the tab. Web defaults need another fire key. Esc also leaves full screen and pointer lock before the page sees it. F12 opens the developer tools. |
| Level loads block the page | M | A load (0.4 to 2 s here, more on phones) runs inside one animation frame. Nothing is presented until it ends, so the loading screen never shows, and the audio callback (main thread) stalls. Fix: split the load over frames (a state machine around `beginLevel` / warm-up), or ASYNCIFY around the load only (costs wasm size and speed; not measured). |
| Canvas size, high DPI, resizing | S | Size the canvas to the window with `devicePixelRatio`, and pass the size changes to SDL (the game already handles any aspect ratio and the 4:3 bars). |
| Full screen and orientation on phones | S (Android) / M (iOS) | Android Chrome: `requestFullscreen` on a tap, then `screen.orientation.lock('landscape')` (only allowed in full screen). iPhone Safari has no element full-screen API and no orientation lock. Install to the home screen with `display: fullscreen` and show a "rotate your phone" message in portrait. |
| Installable web app | S | A manifest (name, icons, `display`, `orientation: landscape`), a service worker caching the engine files (not the paks, which are already in IndexedDB) and HTTPS hosting. The app icon is still missing, as on Android. |
| Background, focus, context loss | S | Pause and mute on `visibilitychange`. `requestAnimationFrame` already stops in hidden tabs, and the bounded catch-up drops the time away, but ScriptProcessor audio may keep playing. Map `webglcontextlost` / `restored` to the existing `rebuildGl` path (untested: SDL's Emscripten port does not send `SDL_RENDER_DEVICE_RESET`). |
| Audio backend | S to M | ScriptProcessorNode is deprecated but still works everywhere. An AudioWorklet (Emscripten `-sAUDIO_WORKLET`) needs wasm workers, SharedArrayBuffer and COOP/COEP headers. Not needed now. Latency on phones is untested. |
| Safari and iOS (from documentation, not tested) | M | WebGL 2 needs Safari 15 or later. Native wasm exceptions (libopenmpt) need Safari 15.2 or later; building with `-fexceptions` (JavaScript-based) instead would cover older versions at a size and speed cost. `navigator.userActivation` needs 16.4 or later; before that SDL uses its gesture listeners. On iOS, Web Audio is muted by the silent switch unless `navigator.audioSession.type = 'playback'` (Safari 17). Safari may evict IndexedDB after 7 days without a visit unless the app is installed to the home screen, so the paks and the profile could vanish: ask `navigator.storage.persist()` and warn the user. iOS limits a tab's memory more tightly than desktop, but about 150 MB is within the usual limits. No full-screen API on iPhone (see above). |
| Threading | none | The engine is single-threaded on the web. SDL's audio callback runs on the main thread, and libopenmpt builds without pthreads. No COOP/COEP headers are needed. |
| Firefox | S | Not tested: only the system Firefox is installed, without automation. Nothing in the GL usage is Chromium-specific. |
| GL | none known | See above: no call or shader construct had to change. Request no canvas alpha. MSAA on the canvas (`SDL_GL_MULTISAMPLEBUFFERS`) is optional, as on desktop. |
| Test and CI hook | S | `measure.py` already runs the bot and gathers numbers. A CI job would need emsdk and Playwright's Chromium (about 400 MB of tools). |

**Total for a playable, installable web version**, desktop browsers and Android Chrome first:
roughly 2 to 3 weeks of work, plus a Safari/iOS pass that needs a real iPhone and iPad.

## How to run it locally

```bash
# once: Emscripten in ~/tools/emsdk (never inside the repository)
git clone https://github.com/emscripten-core/emsdk.git ~/tools/emsdk
~/tools/emsdk/emsdk install 6.0.10 && ~/tools/emsdk/emsdk activate 6.0.10

AS3D_DATA_ROOT=/path/to/main/checkout tools/web_build.sh   # build-web/ and $AS3D_DATA_ROOT/out/web/site/
AS3D_DATA_ROOT=/path/to/main/checkout tools/web_serve.sh   # http://127.0.0.1:8080/ (localhost only)
```

Open `http://127.0.0.1:8080/` to play mission 1: arrows, Ctrl fires, Shift launches a
missile, Space a power-up, P pauses and Return confirms a hint. Click the canvas first so it
gets the keyboard. Sound starts with the first key or click.

- `?app=game&level=1&bot=1`: the bot plays.
- `&touch=1`: the touch controls (the mouse acts as a finger in desktop browsers).
- `&god=1`, `&noaudio=1`, `&frames=N`, `&difficulty=D` work as in `as3d_game`.
- `?app=level`: Stage A. Add `&hold=1` to stop the scrolling, or `&scroll=Y` to start
  elsewhere.

The site directory contains the original paks: it is for the owner only. Never upload it or
serve it beyond localhost. Measurements:
`python apps/web_spike/measure.py --url http://127.0.0.1:8080/ --query "app=game&level=1&bot=1" --seconds 60 --gl gpu --out out/web/run`
(needs `pip install playwright` and `python -m playwright install chromium` in
a virtual environment).

# 150: The web version

Status: open, choices made. Raised by WP-52 (the owner, after trying the web spike on an
Android phone and in Edge: "porting the whole game to WebGL is almost trivial now please do it
(only issue now is no full screen mode which is necessary to really be playable on mobile)").
Affects `apps/web/**`, `apps/game/game_loop.*`, `apps/game/game_flow.*`,
`engine/src/platform/{sdl_window,user_data}.cpp`, `engine/src/game/profile.cpp`,
`as3d/profile.h`, `as3d/platform.h`, `tools/web_build.sh`, `tools/web_serve.sh`. How to build,
serve, install and play it: `docs/web.md`.

Nothing here changes a game rule. The web version is the Android app's feature set in a
page; the original's menus and look stay as they are.

## 1. One app, the Android model

* `apps/web/web_main.cpp` is the Android entry point's twin: the front end in touch or mouse
  mode, no two-player mode, no video options, the Screen (Wide / 4:3) row always, the touch
  overlay's pause button opens the in-game menu. The spike's level viewer (Stage A) is gone;
  `apps/web_spike` became `apps/web`.
* **No two-player mode on the web.** One keyboard, and F is the page's full-screen key (the
  desktop's player 2 fires with F). Android has none either.
* The URL keeps the direct entries for tests: `?level=N`, `?bot=1`, `?touch=1|0`, `?fps=1`,
  `?god=1`, `?noaudio=1`, `?frames=N`, `?difficulty=D`; `?bot=1&menus=1` keeps the menus and
  lets the bot play the missions started from them (the browser test of Mission Complete).
* **Exit** on the main menu ends the loop; the page shows "Thanks for playing" and a Play
  again button (a reload). Leaving a browser tab is the browser's business.

## 2. Full screen and orientation

* A start page with a large **Play** button. Browsers grant full screen only inside a user
  gesture, and audio only after one, so Play does both at once: on touch devices it calls
  `requestFullscreen` on the whole document (so the page's own buttons stay visible) and then
  `screen.orientation.lock('landscape')` (allowed only in full screen; refused on iPhones
  and desktops, which is fine), and it calls the engine's `main` inside the same click, so SDL
  creates its `AudioContext` there. On desktops Play keeps the window, and a second button
  "Play full screen" asks for full screen.
* **During play and in menus**: in touch mode a round toggle sits beside the game's pause
  button, towards the middle of the screen (an HTML button placed from the touch layout the
  engine reports, so the touch overlay itself is unchanged); on desktops the F key, except
  while a name is typed or a key is bound, and the browser's own F11.
* **Leaving full screen pauses** (Esc, the system back gesture, the toggle): during play the
  in-game menu opens, as the pause button does. No play under the browser's bars.
* **Portrait on a touch device**: a "turn your device sideways" notice covers the page and
  the game pauses, instead of a squeezed game.
* **The system back gesture** (touch mode, not in full screen) is the game's Back key, as on
  Android: the page keeps one history entry of its own and turns `popstate` into Back.
* **Installed to the home screen**: `manifest.webmanifest` with `display: fullscreen`,
  `orientation: landscape` and the project's own icon (the Android launcher drawing,
  converted to SVG and PNG), plus Apple's meta tags: the only full-screen route on iPhones.
  No service worker: Chromium installs without one, and the site must be HTTPS for either.

## 3. Canvas and resolution

* The canvas covers the whole window (`position: fixed; inset: 0`, `100dvh`). The page
  sizes its drawing buffer: CSS size times `devicePixelRatio`, with the shorter side capped at
  1080 lines (`?maxlines=N`, `?dpr=X`). SDL's Emscripten port reports the window size as the
  drawable size, so the window backend asks the canvas for its buffer size on the web
  (`sdl_window.cpp`); the loop picks every change up the next frame, as it does on desktop.
* **Safe areas**: `viewport-fit=cover`, and a hidden probe element padded by
  `env(safe-area-inset-*)`; the page passes the insets in framebuffer pixels to the loop's
  `safeInsets`, the same path the Android cutout insets take.
* **Touch button size**: the loop's density becomes a query (`LoopOptions::dpiQuery`):
  framebuffer pixels per CSS pixel times 160 CSS px per inch on coarse-pointer devices (phones
  and tablets lay out about 160 per inch), 96 elsewhere.
* The canvas has no alpha (`SDL_GL_ALPHA_SIZE 0` under Emscripten): the page never shows
  through where blending leaves alpha below 1.

## 4. Touch and mouse

* **Touch mode** where the primary pointer is coarse (`matchMedia('(pointer: coarse)')`), as
  the Android app. Elsewhere the engine switches to touch mode at the first finger
  (`LoopOptions::autoTouch`, `GameWindow::setTouchMode`, `GameFlow::setTouchMode`): the touch
  controls appear, the front end's touch additions (issue 090) and its missing cursor follow,
  the system pointer stays for the mouse, which then acts as a finger. Keys work throughout.
  The Options rows that exist only in touch mode (Controls hand, touch speed) appear after a
  reload in that case: the front end's content is fixed at start (engine/src/ui is not
  touched by this work).
* Fingers never become synthetic mouse events (`SDL_HINT_TOUCH_MOUSE_EVENTS 0`), and SDL
  prevents the browser's default on touches, so no scrolling, zooming or emulated clicks.
  Multi-touch is SDL's: each finger is its own id.

## 5. Keys

* **Web key bindings for a fresh profile** (`as3d::applyWebKeyBindings`,
  `FlowConfig::webKeys`): fire Space, missile X, power-up C, for both players; the rest
  (arrows, 1 and 2, joystick slots) as in the original table. The original's fire key is Ctrl,
  and Ctrl+W closes the tab: pages cannot stop Ctrl+W, Ctrl+T, Ctrl+N or Ctrl+Tab. Existing
  profiles keep their bindings; rebinding works as everywhere.
* The engine takes keys only while the canvas has the focus
  (`SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT = #canvas`); SDL then prevents the browser's default
  for them (arrows and Space do not scroll). The page lets F5, F11, F12 and every
  Ctrl / Alt / Meta combination go to the browser untouched.

## 6. Level loads

A load blocks for 0.4 to 2 s (more on phones), and a browser presents nothing while script
runs, so the loading screen drawn at its start was never seen. With `FlowConfig::deferLoads`
(web only) the front end's `startMission` / `loadAttract` record the request; that frame
draws the game's loading screen and returns to the browser, which shows it; the next frame
runs the load (`GameFlow::runPendingLoad`). The world is neither stepped nor drawn meanwhile
(`worldRunning()` and `playing()` are false). Desktop and Android load at once, as before.
The direct `?level=N` entry still loads inside `main`.

## 7. Lifecycle

Host calls on the running window (`game_loop.h`, `host*`), made by the page:

* **Tab hidden** (`visibilitychange`, `pagehide`): the same as Android's background: the
  in-game menu opens during play, the profile is saved, the mixer pauses; the page also
  suspends the `AudioContext` (SDL's audio callback runs on the main thread and would go on).
  Shown again: the game waits in the in-game menu.
* **WebGL context loss**: the page prevents the default (so the browser may restore it), the
  game pauses and stops drawing; on `webglcontextrestored` Emscripten's extension table is
  rebuilt and the loop runs its existing `rebuildGl` path (the one Android uses after
  `SDL_RENDER_DEVICE_RESET`).

## 8. Game data: two builds

* **bundled** (`tools/web_build.sh`, default): the owner's paks, `Settings.xml`,
  `gfx/logo2s.tga` and `texts_v170.txt` in the site's `data/`, fetched with a progress bar.
  For the owner's own network only.
* **byo** (`tools/web_build.sh byo`): only the engine. On first start the page asks for the
  files (file picker, folder picker where supported, drag and drop of files or the whole game
  folder). Required: `pak0.apk`, `pak1.apk`, `pak2.apk`. Optional: `Settings.xml` (intro
  pages, version line, logo placement), `logo2s.tga` (menu logo), and the texts: either
  `texts_v170.txt` from `tools/extract_exe_texts.py` or **`AirStrike3D.exe` itself**: the page
  reads the texts out of it with a JavaScript port of that tool (`files.js`, checked against
  the Python output by `apps/web/test/files_check.js`) and keeps only the texts.
* Checking: names in any case and path, sizes and SHA-256 against `known_files.json`
  (committed: hashes and sizes are not game data). WebCrypto is used where the page is a
  secure context; plain http on a home network has none, so `files.js` has its own SHA-256.
  Files of another version are refused with a message.
* The accepted files are kept as Blobs in IndexedDB (`as3d-game-files`) and used on later
  visits; "Remove them" clears the store. IndexedDB rather than OPFS: OPFS needs a secure
  context, and the owner serves over plain http.
* Before `main`, the page writes the files into `/data` of Emscripten's memory file system
  (about 25 MB, once per visit); the engine reads them there like the APK's assets.
* `tools/web_build.sh byo` fails if the site holds any file it does not make, a pak header,
  a file equal to a known game file, or a line of the extracted texts.

## 9. The profile

`userDataDir()` is `/persist` under Emscripten; the page mounts IDBFS there and reads it
before `main`. `FlowConfig::profileSaved` runs after every profile save (the same moments as
on the other platforms: Options Back, name entry, mission end, background) and the page
writes IDBFS back to IndexedDB. The page asks `navigator.storage.persist()` at Play and goes
on if it is refused or missing (it needs a secure context).

## 10. The shadow read-back

The silhouette shadows are baked at level load with one `glReadPixels` per shadow map, a
synchronous stall in WebGL. Measured in `docs/web.md` ("Level loads"); see there for whether
it needed a change.

## 11. Log markers added

`AS3D_MENU name=S items= id@x,y,w,h[d] ...` with every `AS3D_SCREEN` (the top menu's items
in virtual 800x600, `d` = disabled), `AS3D_LOAD_MS`, `AS3D_TOUCH_MODE`, `AS3D_GL_LOST`, and
the page's `AS3D_WEB ...` lines. The browser tests tap menu items from `AS3D_MENU`.

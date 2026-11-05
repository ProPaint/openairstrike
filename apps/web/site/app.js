// AirStrike, web version: the page around the engine (docs/web.md, docs/spec/issues/150, 163).
// Start screen and game files, the canvas at the device's pixel ratio, full screen and
// orientation, touch detection, the profile in browser storage, and the lifecycle calls into
// the engine (apps/web/web_main.cpp). window.as3dState and window.as3dLog are read by the
// tests (apps/web/test/).
'use strict';

(function () {
  const q = new URLSearchParams(location.search);
  const BUILD = window.AS3D_BUILD || { mode: 'bundled', stamp: '' };
  const $ = (id) => document.getElementById(id);
  const canvas = $('canvas');
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

  // ---------------------------------------------------------------------------------------
  // Log and state (the engine's lines too, with page timestamps).
  // ---------------------------------------------------------------------------------------
  window.as3dLog = [];
  const state = window.as3dState = {
    mode: BUILD.mode, build: BUILD.stamp, ready: false, started: false, running: false, finished: false,
    firstFrame: false, screen: '', touch: false, fullscreen: false, fullscreenRequests: 0,
    orientationLocks: 0, pauses: [], profileSyncs: 0, persisted: null, canvas: null, failed: null,
  };
  function log(text) {
    window.as3dLog.push({ t: performance.now(), text: String(text) });
    console.log(text);
    if (/FATAL|^ABORT/.test(text) && !state.failed) fail(String(text));
  }
  window.addEventListener('error', (e) => log('PAGE_ERROR: ' + e.message));
  window.addEventListener('unhandledrejection', (e) => log('PAGE_ERROR: ' + ((e.reason && e.reason.message) || e.reason)));

  // Launcher mode: the desktop icon opens /airstrike/?game=..&autostart=1. The page then shows
  // no UI of its own, only a Win95 "Loading" dialog (98.css from the launcher) over black.
  const LAUNCH = location.pathname.indexOf('/airstrike/') === 0 && /^[a-z0-9]+$/.test(q.get('game') || '') &&
                 q.get('autostart') === '1' && BUILD.mode !== 'byo' && !!window.launcherSaves &&
                 typeof window.launcherSaves.quitToDesktop === 'function';
  if (!LAUNCH) { const early = document.getElementById('lw-early'); if (early) early.remove(); }
  const lw = { el: null, bar: null, msg: null, last: 0 };
  const TITLES = { as3d: 'AirStrike 3D', as2: 'AirStrike 2', gulf: 'Gulf Thunder' };
  function lwTitle() { return TITLES[q.get('game')] || q.get('game'); }
  function lwBuild() {
    if (!LAUNCH || lw.el) return;
    const link = document.createElement('link');
    link.rel = 'stylesheet';
    link.href = '/css/98/98.css';
    document.head.appendChild(link);
    const st = document.createElement('style');
    st.textContent = '#start, #ended, #failed { display: none !important; } ' +
      'html, body { background: #000 !important; } ' +
      '#lw { position: fixed; inset: 0; z-index: 50; background: #000; display: flex; align-items: center; justify-content: center; } ' +
      '#lw .window { width: 340px; max-width: 92vw; } ' +
      '#lw .window-body p { margin: 8px 0; } ' +
      '#lw .progress-indicator { width: 100%; } ' +
      '#lw .row { display: flex; justify-content: center; margin-top: 12px; } ' +
      '#lw button { min-width: 75px; }';
    document.head.appendChild(st);
    const el = document.createElement('div');
    el.id = 'lw';
    el.innerHTML = '<div class="window"><div class="title-bar"><div class="title-bar-text"></div></div>' +
      '<div class="window-body"><p class="lw-msg"></p>' +
      '<div class="progress-indicator segmented"><span class="progress-indicator-bar" style="width:0%"></span></div>' +
      '<div class="row lw-ok" hidden><button type="button">OK</button></div></div></div>';
    el.querySelector('.title-bar-text').textContent = lwTitle();
    lw.msg = el.querySelector('.lw-msg');
    lw.msg.textContent = 'Loading ' + lwTitle() + '\u2026';
    lw.bar = el.querySelector('.progress-indicator-bar');
    el.querySelector('.lw-ok button').addEventListener('click', () => {
      try { window.launcherSaves.quitToDesktop(); } catch (e) { location.replace('/'); }
    });
    document.body.appendChild(el);
    lw.el = el;
  }
  // f in 0..1; never goes backwards.
  function lwProgress(f) {
    if (!lw.el) return;
    lw.last = Math.max(lw.last, clamp(f, 0, 1));
    lw.bar.style.width = Math.round(lw.last * 100) + '%';
  }
  function lwHide() { if (lw.el) { lw.el.remove(); lw.el = null; } }
  function lwError(text) {
    lwBuild();
    lw.msg.textContent = text;
    lw.el.querySelector('.progress-indicator').hidden = true;
    lw.el.querySelector('.lw-ok').hidden = false;
  }
  lwBuild();
  lwProgress(0.02);

  function fail(text) {
    state.failed = text;
    if (LAUNCH) { lwError(text); return; }
    $('failed-text').textContent = text;
    $('failed').hidden = false;
  }
  function setStatus(text) { $('status').textContent = text; }

  // ---------------------------------------------------------------------------------------
  // Touch or mouse: touch mode where the primary pointer is coarse; elsewhere it switches on
  // at the first touch (the engine's --auto-touch). ?touch=1 / ?touch=0 force it.
  // ---------------------------------------------------------------------------------------
  const touchParam = q.get('touch');
  const touchAtStart = touchParam === '1' || (touchParam !== '0' && matchMedia('(pointer: coarse)').matches);
  const autoTouch = touchParam !== '0' && !touchAtStart;
  state.touch = touchAtStart;
  document.body.classList.toggle('touchdev', touchAtStart);

  const direct = q.has('level') || (q.get('bot') === '1' && q.get('menus') !== '1');
  const args = [];
  if (touchAtStart) args.push('--touch');
  else if (autoTouch) args.push('--auto-touch');
  const intParam = (name, lo, hi) => {
    const v = parseInt(q.get(name), 10);
    return Number.isFinite(v) ? String(clamp(v, lo, hi)) : null;
  };
  // The game: ?game=as3d|as2|gulf forces one. Without it the start screen offers the playable
  // games this page has (bundled: data/games.txt; bring your own: those whose files are
  // stored), when there are more than one, and only the chosen game's files are loaded
  // (docs/spec/issues/163). Its files live in data/<key>/ (bundled build) or under its own key
  // in browser storage (bring-your-own build). GAME is known once chosen.
  const FORCED = /^[a-z0-9]+$/.test(q.get('game') || '') ? q.get('game') : null;
  let GAME = FORCED;
  // ?level= and ?difficulty= are passed on as numbers; the engine checks them against the
  // rules of the chosen game (mission and difficulty counts differ) and ignores a bad one.
  if (intParam('level', 1, 9999)) args.push('--level', intParam('level', 1, 9999));
  if (q.get('unfinished') === '1') args.push('--unfinished');
  if (q.get('bot') === '1') args.push('--bot');
  if (q.get('menus') === '1') args.push('--menus');
  if (q.get('god') === '1') args.push('--god');
  if (q.get('noaudio') === '1') args.push('--no-audio');
  if (q.get('fps') === '1') args.push('--fps');
  if (intParam('frames', 1, 1e9)) args.push('--frames', intParam('frames', 1, 1e9));
  if (intParam('difficulty', 0, 9999)) args.push('--difficulty', intParam('difficulty', 0, 9999));

  // ---------------------------------------------------------------------------------------
  // The canvas: the whole window at the device's pixel ratio, the drawing buffer's shorter
  // side capped (?maxlines=, default 1080). The engine reads the buffer size every frame.
  // ---------------------------------------------------------------------------------------
  const maxLines = clamp(parseInt(q.get('maxlines'), 10) || 1080, 144, 4320);
  const dprParam = parseFloat(q.get('dpr'));
  const probe = $('insets-probe');
  let scale = 1;
  // CSS pixels per inch, for the touch buttons' size in millimetres: phones and tablets lay
  // out about 160 CSS px per inch, desktop screens 96.
  const cssPerInch = () => (matchMedia('(pointer: coarse)').matches ? 160 : 96);
  function fit() {
    const cw = canvas.clientWidth || innerWidth, ch = canvas.clientHeight || innerHeight;
    const dpr = dprParam > 0 ? dprParam : (window.devicePixelRatio || 1);
    scale = Math.min(dpr, maxLines / Math.max(1, Math.min(cw, ch)));
    const w = Math.max(1, Math.round(cw * scale)), h = Math.max(1, Math.round(ch * scale));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    const cs = getComputedStyle(probe);
    const px = (v) => Math.round((parseFloat(v) || 0) * scale);
    const insets = [px(cs.paddingLeft), px(cs.paddingTop), px(cs.paddingRight), px(cs.paddingBottom)];
    state.canvas = { cssW: cw, cssH: ch, w, h, scale, insets, dpi: scale * cssPerInch() };
    if (state.running) {
      Module._as3d_web_set_insets(insets[0], insets[1], insets[2], insets[3]);
      Module._as3d_web_set_dpi(scale * cssPerInch());
    }
    checkOrientation();
    placeFsButton();
  }
  window.addEventListener('resize', fit);
  window.addEventListener('orientationchange', () => setTimeout(fit, 100));
  if (window.visualViewport) visualViewport.addEventListener('resize', fit);
  setInterval(fit, 1000); // missed resize events (some browsers after leaving full screen)

  // ---------------------------------------------------------------------------------------
  // Full screen and orientation.
  // ---------------------------------------------------------------------------------------
  const root = document.documentElement;
  const fsApi = !!(root.requestFullscreen || root.webkitRequestFullscreen);
  const installed = matchMedia('(display-mode: fullscreen)').matches || matchMedia('(display-mode: standalone)').matches ||
                    navigator.standalone === true;
  const fsElement = () => document.fullscreenElement || document.webkitFullscreenElement || null;
  function lockLandscape() {
    if (!state.touch) return;
    const o = screen.orientation;
    if (!o || !o.lock) return;
    state.orientationLocks++;
    o.lock('landscape').then(() => log('AS3D_WEB orientation=locked'),
                             (e) => log('AS3D_WEB orientation lock refused: ' + (e && e.name)));
  }
  // Launcher: the desktop icon opens /airstrike/?game=..&autostart=1, a navigation that carries
  // no user gesture, so full screen (and the landscape lock on touch) waits for the first input.
  const underLauncher = () => location.pathname.indexOf('/airstrike/') === 0 && !!window.launcherSaves &&
                              typeof window.launcherSaves.quitToDesktop === 'function';
  const autoFsEvents = ['keydown', 'pointerdown', 'pointerup', 'touchend', 'mousedown'];
  let autoFsOn = null;
  function autoFsDisarm() {
    if (!autoFsOn) return;
    autoFsEvents.forEach((n) => window.removeEventListener(n, autoFsOn, true));
    autoFsOn = null;
  }
  // Passive listeners that never stop the event: the first input still reaches the game.
  function autoFsArm(full) {
    if (autoFsOn || !full || !fsApi || installed) return;
    autoFsOn = (e) => {
      if (e.type === 'keydown' && modifierOnly(e)) return;
      if (e.type === 'pointerdown' && e.pointerType === 'touch') return; // touch activates on release
      if (e.type === 'mousedown' && autoFsOn && window.PointerEvent) return; // pointerdown covers it
      autoFsDisarm();
      if (!fsElement()) enterFullscreen();
    };
    autoFsEvents.forEach((n) => window.addEventListener(n, autoFsOn, { capture: true, passive: true }));
    log('AS3D_WEB autostart_fullscreen_armed');
  }
  function modifierOnly(e) { return ['Control', 'Alt', 'Meta', 'Shift', 'AltGraph'].indexOf(e.key) >= 0; }
  // Must run inside a user gesture (a click, a tap, a key).
  function enterFullscreen() {
    if (!fsApi) return;
    state.fullscreenRequests++;
    let p;
    try {
      p = root.requestFullscreen ? root.requestFullscreen({ navigationUI: 'hide' }) : root.webkitRequestFullscreen();
    } catch (e) {
      p = Promise.reject(e);
    }
    log('AS3D_WEB fullscreen_request');
    Promise.resolve(p).then(lockLandscape, (e) => log('AS3D_WEB fullscreen refused: ' + (e && (e.message || e.name))));
  }
  function leaveFullscreen() {
    if (!fsElement()) return;
    const p = document.exitFullscreen ? document.exitFullscreen() : document.webkitExitFullscreen && document.webkitExitFullscreen();
    Promise.resolve(p).catch(() => {});
  }
  function toggleFullscreen() {
    autoFsDisarm();   // the player chose by hand
    if (fsElement()) leaveFullscreen();
    else enterFullscreen();
  }
  function onFullscreenChange() {
    const on = !!fsElement();
    state.fullscreen = on;
    document.body.classList.toggle('fullscreen', on);
    log('AS3D_WEB fullscreen=' + (on ? 1 : 0));
    // Leaving full screen (Esc, the back gesture) pauses: no play under the browser's bars.
    if (!on && state.running) pause(1);
    fit();
  }
  document.addEventListener('fullscreenchange', onFullscreenChange);
  document.addEventListener('webkitfullscreenchange', onFullscreenChange);

  // Launcher mode without touch: #fs-toggle stays hidden (it is placed by the touch pause button), so the
  // launcher's shared Win95 button (js/loading95.js) is used instead.
  function addLauncherFsButton() {
    if (!LAUNCH || state.touch || !fsApi || installed) return;
    const add = () => { try { window.Launch95.addFullscreenButton(root, { refocus: canvas }); } catch (e) { /* optional */ } };
    if (window.Launch95 && window.Launch95.addFullscreenButton) { add(); return; }
    const sc = document.createElement('script');
    sc.src = '/js/loading95.js';
    sc.onload = add;
    document.head.appendChild(sc);
  }

  // The toggle in touch mode, beside the game's pause button.
  const fsBtn = $('fs-toggle');
  let pauseButton = null; // framebuffer pixels: x, y, r, hit r, width, height
  function updateFsButton() {
    fsBtn.hidden = !(state.running && state.touch && fsApi && !installed);
    placeFsButton();
  }
  function placeFsButton() {
    if (fsBtn.hidden || !pauseButton) return;
    const [x, y, r, hitR, w] = pauseButton;
    const s = canvas.clientWidth / Math.max(1, w); // CSS px per framebuffer px
    const size = clamp(1.6 * r * s, 36, 52);
    const cx = x * s, cy = y * s, half = (canvas.clientWidth || innerWidth) / 2;
    const reach = Math.max(r, hitR) * s;
    const dir = Math.abs(cx - half) < reach || cx > half ? -1 : 1; // towards the middle
    const fx = cx + dir * (reach + size / 2 + 6);
    fsBtn.style.width = fsBtn.style.height = size + 'px';
    fsBtn.style.left = Math.round(fx - size / 2) + 'px';
    fsBtn.style.top = Math.round(Math.max(4, cy - size / 2)) + 'px';
  }
  fsBtn.addEventListener('click', (e) => {
    e.preventDefault();
    toggleFullscreen();
    canvas.focus();
  });

  // Portrait on a touch device: a notice instead of a squeezed game (and the game pauses).
  const rotate = $('rotate');
  function checkOrientation() {
    const portrait = innerHeight > innerWidth * 1.05;
    const show = state.started && state.touch && portrait && !state.finished;
    if (show === !rotate.hidden) return;
    rotate.hidden = !show;
    log('AS3D_WEB portrait_notice=' + (show ? 1 : 0));
    if (show && state.running) pause(2);
  }

  // ---------------------------------------------------------------------------------------
  // Lifecycle: hidden tab, back navigation, WebGL context loss.
  // ---------------------------------------------------------------------------------------
  function pause(reason) {
    state.pauses.push(reason);
    if (state.running) Module._as3d_web_pause(reason);
  }
  const audioContext = () => (window.Module && Module.SDL2 && Module.SDL2.audioContext) || null;
  function setHidden(hidden) {
    if (!state.running) return;
    log('AS3D_WEB visibility=' + (hidden ? 'hidden' : 'visible'));
    Module._as3d_web_background(hidden ? 1 : 0);
    const ac = audioContext();
    if (ac) (hidden ? ac.suspend() : ac.resume()).catch(() => {});
  }
  document.addEventListener('visibilitychange', () => setHidden(document.visibilityState === 'hidden'));
  window.addEventListener('pagehide', () => setHidden(true));
  window.addEventListener('pageshow', (e) => { if (e.persisted) setHidden(false); });
  // Touch mode: the system back gesture is the game's Back key, as in the Android app (the
  // page keeps one history entry of its own for it).
  window.addEventListener('popstate', () => {
    if (!state.running || !state.touch) return;
    log('AS3D_WEB back');
    Module._as3d_web_back();
    history.pushState({ as3d: 1 }, '');
  });
  canvas.addEventListener('webglcontextlost', (e) => {
    e.preventDefault(); // allows the restore
    log('AS3D_WEB context_lost');
    if (state.running) Module._as3d_web_context_lost();
  });
  canvas.addEventListener('webglcontextrestored', () => {
    log('AS3D_WEB context_restored');
    if (state.running) Module._as3d_web_context_restored();
  });

  // ---------------------------------------------------------------------------------------
  // Keys. The engine takes keys only while the canvas has the focus. The browser keeps F5,
  // F11 (its own full screen), F12 and every Ctrl/Alt/Meta combination; F toggles full screen
  // except while a name is typed or a key is bound.
  // ---------------------------------------------------------------------------------------
  const browserKeys = new Set(['F5', 'F11', 'F12']);
  const modifierKeys = new Set(['Control', 'Alt', 'Meta', 'Shift', 'AltGraph']);
  const keyScreens = new Set(['name', 'controls']);
  window.addEventListener('keydown', (e) => {
    if (browserKeys.has(e.key) || ((e.ctrlKey || e.altKey || e.metaKey) && !modifierKeys.has(e.key))) {
      e.stopImmediatePropagation();
      return;
    }
    if (e.code === 'KeyF' && state.running && !keyScreens.has(state.screen)) {
      e.stopImmediatePropagation();
      e.preventDefault();
      if (!e.repeat && fsApi && !installed) toggleFullscreen();
    }
  }, true);
  canvas.addEventListener('pointerdown', () => canvas.focus());
  window.addEventListener('focus', () => { if (state.running) canvas.focus(); });

  // ---------------------------------------------------------------------------------------
  // The profile: /persist in IndexedDB (IDBFS), read before main, written after every save.
  // ---------------------------------------------------------------------------------------
  let syncing = false, syncAgain = false;
  function syncProfile() {
    if (syncing) {
      syncAgain = true;
      return;
    }
    syncing = true;
    Module.FS.syncfs(false, (err) => {
      syncing = false;
      state.profileSyncs++;
      log(err ? 'AS3D_WEB profile_sync_failed ' + err : 'AS3D_WEB profile_synced');
      if (!err && window.launcherSaves && GAME) {
        try { window.launcherSaves.uploadFile(Module.FS, '/persist/' + GAME + '/profile.bin'); } catch (e) { /* never block the game */ }
      }
      if (syncAgain) {
        syncAgain = false;
        syncProfile();
      }
    });
  }
  function mountProfile() {
    Module.FS.mkdir('/persist');
    Module.FS.mount(Module.FS.filesystems.IDBFS, {}, '/persist');
    Module.addRunDependency('as3d-profile');
    profileLoaded = new Promise((ok) => {
      Module.FS.syncfs(true, (err) => {
        if (err) log('AS3D_WEB profile storage unavailable: ' + err);
        Module.removeRunDependency('as3d-profile');
        ok();
      });
    });
  }
  // The launcher's save-sync.js (when present: served under /airstrike/) swaps the profile to
  // the signed-in user and restores the cloud copy. Runs once the game is chosen and /persist
  // is populated, before the engine starts (a run dependency holds main back).
  let profileLoaded = Promise.resolve();
  // The engine's runtime starts before the game files are there (it unpacks the player's
  // archives, as3d_unpack); main waits for them instead: ready() runs once the runtime, the
  // files and the restored profile are all there.
  let restoreDone = Promise.resolve();
  function restoreProfile() {
    if (!window.launcherSaves) return;
    restoreDone = Promise.all([profileLoaded, filesReady]).then(() =>
      window.launcherSaves.restoreFile(Module.FS, '/persist/' + GAME + '/profile.bin'))
      .catch((e) => log('AS3D_WEB restore skipped: ' + e));
  }

  // ---------------------------------------------------------------------------------------
  // Game files: bundled (fetched from data/<key>/) or the player's own (IndexedDB, per game).
  // ---------------------------------------------------------------------------------------
  // Resolved with the chosen game's files ({name: bytes or Blob}); GAME is set by then.
  let resolveFiles;
  const filesReady = new Promise((ok) => { resolveFiles = ok; });
  let dataWritten = null;
  function writeGameFiles() {
    dataWritten = filesReady.then(async (files) => {
      Module.FS.mkdir('/data');
      Module.FS.mkdir('/data/' + GAME);
      for (const [name, v] of Object.entries(files)) {
        const bytes = v instanceof Uint8Array ? v : new Uint8Array(await v.arrayBuffer());
        Module.FS.writeFile('/data/' + GAME + '/' + name, bytes);
      }
      log('AS3D_WEB data=' + Object.keys(files).sort().join(',') + ' game=' + GAME);
    });
    dataWritten.catch((e) => fail('Cannot prepare the game files: ' + e.message));
  }

  // The player's archive or installer, through the engine's importer (as3d_unpack in
  // web_main.cpp, docs/spec/issues/170 section 5): the file goes into MEMFS under /unpack/in/,
  // the games' files the importer made come back from /unpack/out/<key>/ as Blobs, and the
  // staging files are removed. Resolves to { files: {key: {name: Blob}}, notes: [text] }.
  let runtimeUp;
  const runtimeReady = new Promise((ok) => { runtimeUp = ok; });
  function rmTree(path) {
    const FS = Module.FS;
    let st;
    try { st = FS.stat(path); } catch (e) { return; }
    if (FS.isDir(st.mode)) {
      for (const n of FS.readdir(path)) if (n !== '.' && n !== '..') rmTree(path + '/' + n);
      FS.rmdir(path);
    } else {
      FS.unlink(path);
    }
  }
  async function unpack(file) {
    await runtimeReady;
    const FS = Module.FS;
    const bytes = new Uint8Array(await file.arrayBuffer());
    rmTree('/unpack');
    FS.mkdirTree('/unpack/in');
    const safe = file.name.replace(/[\/\\]/g, '_') || 'download';
    FS.writeFile('/unpack/in/' + safe, bytes);
    try {
      const r = JSON.parse(Module.ccall('as3d_unpack', 'string', ['string'], ['/unpack/in']));
      const files = {};
      for (const [g, names] of Object.entries(r.files)) {
        for (const n of names) (files[g] = files[g] || {})[n] = new Blob([FS.readFile('/unpack/out/' + g + '/' + n)]);
      }
      log('AS3D_WEB unpack ' + safe + ' games=' + Object.keys(files).join(','));
      return { files, notes: r.notes };
    } finally {
      rmTree('/unpack');
    }
  }

  const bar = $('bar');
  function progress(f) {
    lwProgress(0.05 + 0.75 * f);   // game data download: 5..80%
    $('progress').hidden = false;
    bar.style.width = Math.round(100 * f) + '%';
  }

  // ---------------------------------------------------------------------------------------
  // Choosing the game (docs/spec/issues/163). The page chooses on the web, not the engine's
  // selector: the engine needs one game's files before it starts, and only the chosen game's
  // are downloaded (25 MB for AirStrike 3D, 48 MB for AirStrike 2). A card is the Play button
  // of its game; the last choice (localStorage) is marked, never started by itself.
  // ---------------------------------------------------------------------------------------
  const LAST_GAME = 'as3d-last-game';
  const lastGame = () => { try { return localStorage.getItem(LAST_GAME); } catch (e) { return null; } };
  let offered = [];       // the games offered on the start screen (more than one: the chooser)
  let wantStart = null;   // a card was clicked: start once the engine is ready ({ full })
  // A game controller on the start screen (docs/web.md "Controllers"): the engine reads the
  // controllers once it runs (SDL); before that the page does. Left / Right (D-pad or left
  // stick) move between the cards, A or Start plays. `nav` is the chooser's, while it is up.
  const padStart = { nav: null, prev: null, raf: 0 };
  const unfinished = q.get('unfinished') === '1';
  // ?games=as3d,as2 narrows the selector to those games (tests, screenshots).
  const only = /^[a-z0-9,]+$/.test(q.get('games') || '') ? q.get('games').split(',') : null;
  const offerable = (kn, g) => !!kn[g] && (kn[g].playable || unfinished) && (!only || only.includes(g));
  // One card, as the engine's selector draws it (docs/spec/issues/164): the marquee on its stage
  // (the game's own title, else the title in large text), the title, the version, a line of
  // news (the download, or what is stored; the save summary is the engine's to read), and what
  // a click does. `marquee`: a Promise of an element, or null.
  const hintText = () => (touchAtStart ? 'Tap to play' : 'Click to play');
  function makeCard(g, def, news, marquee) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'game';
    b.dataset.game = g;
    b.style.setProperty('--chars', String(def.title.length));
    const stage = document.createElement('span');
    stage.className = 'stage text';
    stage.textContent = def.title;
    if (marquee) {
      marquee.then((el) => {
        if (!el) return;
        stage.className = 'stage';
        stage.textContent = '';
        stage.appendChild(el);
        b.dataset.marquee = el.tagName.toLowerCase();
      }).catch(() => {});
    }
    const t = document.createElement('strong');
    t.textContent = def.title;
    const v = document.createElement('small');
    v.textContent = `Version ${def.version}`;
    const info = document.createElement('span');
    info.className = 'info';
    for (const n of news) {
      const l = document.createElement('span');
      l.textContent = n.text;
      if (n.cls) l.className = n.cls;
      info.appendChild(l);
    }
    const hint = document.createElement('span');
    hint.className = 'hint';
    hint.textContent = hintText();
    b.append(stage, t, v, info, hint);
    return b;
  }
  const mbOf = (def) => Object.values(def.files).filter((f) => f.required).reduce((s, f) => s + (f.size || 0), 0) / 1048576;
  // The marquee of a game's card: the build-time render (bundled) or a drawing from the
  // player's stored files (bring your own).
  const marqueeOf = (g) => (BUILD.mode === 'bundled' ? AS3DMarquee.fromRender(g) : AS3DMarquee.fromFiles(g, chosen[g]));

  // `avail`: the games this page can start; resolves with the one to start.
  function chooseGame(avail, kn) {
    offered = avail;
    state.offered = avail.slice();
    if (avail.length <= 1) return Promise.resolve(avail[0]);
    return new Promise((ok) => {
      const list = $('game-list');
      list.textContent = '';
      const last = lastGame();
      let current = Math.max(0, avail.indexOf(last));
      const cards = [];
      const setCurrent = (i) => {
        current = Math.max(0, Math.min(avail.length - 1, i));
        cards.forEach((c, k) => c.classList.toggle('current', k === current));
        state.currentGame = avail[current];
      };
      const play = (i) => {
        const g = avail[i], b = cards[i];
        if (GAME) return;
        try { localStorage.setItem(LAST_GAME, g); } catch (e) { /* not kept */ }
        // The click is the Play gesture: full screen must be asked for inside it.
        const full = touchAtStart || $('choose-full').checked;
        if (full && fsApi && !installed) enterFullscreen();
        wantStart = { full: false };
        for (const o of cards) o.disabled = o !== b;
        $('choose-play').disabled = true;
        b.classList.add('chosen');
        $('games').classList.add('chosen');
        document.removeEventListener('keydown', onKey);
        padStart.nav = null;
        log('AS3D_WEB game_chosen=' + g);
        ok(g);
      };
      const onKey = (e) => {
        if ($('games').hidden || e.ctrlKey || e.altKey || e.metaKey) return;
        if (e.key === 'ArrowLeft') setCurrent(current - 1);
        else if (e.key === 'ArrowRight') setCurrent(current + 1);
        else if (e.key === 'Enter' && document.activeElement && document.activeElement.tagName === 'BUTTON' &&
                 document.activeElement.id !== 'choose-play') return; // the focused card's own click
        else if (e.key === 'Enter') play(current);
        else return;
        e.preventDefault();
      };
      avail.forEach((g, i) => {
        const def = kn[g];
        const news = [];
        if (BUILD.mode === 'bundled' && mbOf(def)) news.push({ text: `${Math.round(mbOf(def))} MB to download` });
        else if (BUILD.mode !== 'bundled') news.push({ text: 'Files kept in this browser' });
        if (g === last) news.push({ text: 'Last played', cls: 'lastplayed' });
        const b = makeCard(g, def, news, marqueeOf(g));
        if (g === last) b.classList.add('last');
        b.addEventListener('click', () => play(i));
        b.addEventListener('pointerenter', () => setCurrent(i));
        b.addEventListener('focus', () => setCurrent(i));
        cards.push(b);
        list.appendChild(b);
      });
      $('choose-play').disabled = false;
      $('choose-play').onclick = () => play(current);
      document.addEventListener('keydown', onKey);
      padStart.nav = { move: (d) => setCurrent(current + d), play: () => play(current) };
      setCurrent(current);
      $('choose-full-label').hidden = touchAtStart || !fsApi || installed;
      $('games').hidden = false;
      $('start').classList.add('choosing');
      $('play-row').hidden = true;
      $('progress').hidden = true;
      setStatus('');
      state.chooser = true;
      log('AS3D_WEB chooser games=' + avail.join(','));
    });
  }

  // The bundled build: the games of data/games.txt (tools/web_build.sh; a build without it
  // has the first game only).
  async function bundledGames(kn) {
    let keys = ['as3d'];
    try {
      const r = await fetch('data/games.txt', { cache: 'no-cache' });
      if (r.ok) keys = (await r.text()).split(/\s+/).filter((k) => k);
    } catch (e) { /* the first game */ }
    return keys.filter((g) => offerable(kn, g));
  }

  async function bundledFiles() {
    const kn = await AS3DFiles.knownFiles();
    const game = FORCED || await chooseGame(await bundledGames(kn), kn) || 'as3d';
    GAME = game;
    state.game = game;
    setStatus('Loading the game data...');
    const files = await AS3DFiles.fetchBundled(GAME, progress);
    progress(1);
    resolveFiles(files);
  }

  // Bring your own: the stored files, else the picker until the game's required files (its
  // paks) are there. Files of the other games are recognised by their SHA-256 and stored too.
  // chosen: { gameKey: { name: Blob } }, what is stored plus what was given this time.
  let chosen = {}, games = {};
  const titleOf = (g) => (games[g] ? games[g].title : g);
  // The games whose files are all stored and that may be started.
  const startable = () => Object.keys(games).filter((g) => offerable(games, g) && chosen[g] &&
                                                         !AS3DFiles.missing(chosen[g], g).length);
  // Without ?game=: the one game with its files, or the chooser for several.
  async function startOwn() {
    const game = await chooseGame(startable(), games);
    GAME = game;
    state.game = game;
    $('files').hidden = true;
    resolveFiles(chosen[game]);
  }
  async function ownFiles() {
    games = await AS3DFiles.knownFiles();
    if (FORCED && !games[FORCED]) {
      fail(`Unknown game '${FORCED}' (${Object.keys(games).join(', ')}).`);
      return;
    }
    let stored = {};
    try {
      stored = await AS3DFiles.loadStored();
    } catch (e) {
      log('AS3D_WEB storage: ' + e);
      $('warn').hidden = false;
      $('warn').textContent = 'This browser does not let the page store files (private window?): ' +
        'the game files must be chosen again on every visit.';
    }
    chosen = stored;
    if (FORCED ? !AS3DFiles.missing(chosen[FORCED], FORCED).length : startable().length) {
      showStored(chosen);
      if (!FORCED) return startOwn();
      state.game = FORCED;
      resolveFiles(chosen[FORCED]);
      return;
    }
    if (Object.keys(chosen).length) showStored(chosen);
    setStatus('');
    $('files').hidden = false;
    await renderFileList([]);
  }
  function showStored(byGame) {
    const size = (b) => b.size || b.length || 0;
    const parts = [];
    let total = 0;
    for (const g of Object.keys(byGame).sort()) {
      const files = byGame[g];
      if (!Object.keys(files).length) continue;
      total += Object.values(files).reduce((s, b) => s + size(b), 0);
      parts.push(`${titleOf(g)}: ${Object.keys(files).sort().join(', ')}`);
    }
    $('stored').hidden = !parts.length;
    $('stored-text').textContent = `Your game files are kept in this browser (${(total / 1048576).toFixed(0)} MB; ` +
      parts.join('; ') + ').';
    // Another game's files can be added as long as a known game lacks them (docs/web.md).
    $('add-files').hidden = !!FORCED || !incompleteGames().length;
  }
  // The games this page plays whose files are not all stored yet.
  const incompleteGames = () => Object.keys(games).filter((g) => offerable(games, g) && AS3DFiles.missing(chosen[g], g).length);
  // The picker lists the files of the forced game, else of the first game still lacking files
  // (any known game's files are taken whichever is listed).
  const listed = () => FORCED || incompleteGames()[0] || 'as3d';
  // "Add another game's files": the picker over the chooser or the Play button; the files of
  // the listed game complete the set, or Back returns. A game added once the page has
  // started an engine needs a reload to be offered, so a completed addition reloads.
  let adding = false, addingOverChooser = false;
  $('add-files').addEventListener('click', async () => {
    adding = true;
    addingOverChooser = !$('games').hidden;
    $('games').hidden = true;
    $('play-row').hidden = true;
    $('files-back').hidden = false;
    $('files').hidden = false;
    $('files-status').textContent = '';
    await renderFileList([]);
  });
  $('files-back').addEventListener('click', () => {
    adding = false;
    $('files').hidden = true;
    $('files-back').hidden = true;
    if (addingOverChooser) $('games').hidden = false;
    else $('play-row').hidden = false;
  });
  // The games this page plays, as cards that cannot be played yet: title in text until the files
  // are there (then the chooser draws the marquees from them).
  function renderFileCards() {
    const row = $('files-cards');
    row.textContent = '';
    for (const g of Object.keys(games)) {
      if (!offerable(games, g)) continue;
      const miss = AS3DFiles.missing(chosen[g], g);
      const b = makeCard(g, games[g], [{ text: miss.length ? 'Files needed' : 'Files stored' }], null);
      b.disabled = true;
      row.appendChild(b);
    }
  }
  async function renderFileList(notes) {
    renderFileCards();
    const def = games[listed()];
    const ul = $('files-list');
    ul.textContent = '';
    const names = Object.keys(def.files).filter((n) => n !== def.exe);
    for (const n of names) {
      const li = document.createElement('li');
      const have = !!(chosen[listed()] && chosen[listed()][n]);
      const bad = notes.find((x) => !x.ok && (x.name === n || (n === def.texts && x.name === def.exe)));
      li.className = have ? 'ok' : bad ? 'bad' : 'missing';
      const what = def.files[n].gives ? ` (optional: ${def.files[n].gives})` : def.files[n].required ? ' (needed)' : ' (optional)';
      li.textContent = n + (have ? '' : what) + (bad ? ' - ' + bad.text : '');
      ul.appendChild(li);
    }
    // What the engine's importer said about an archive or installer (refused installers too).
    for (const x of notes.filter((y) => y.unpacked)) {
      const li = document.createElement('li');
      li.className = x.ok ? 'ok' : 'bad';
      li.textContent = x.text;
      ul.appendChild(li);
    }
  }
  async function takeFiles(list) {
    const status = $('files-status');
    const target = listed(); // the game whose files are being added (listed() moves on once complete)
    status.textContent = 'Checking...';
    try {
      const res = await AS3DFiles.check(list, (name, f) => {
        status.textContent = name ? `Checking ${name}...` : 'Checking...';
        progress(f);
      }, unpack);
      for (const [g, files] of Object.entries(res.games)) chosen[g] = Object.assign(chosen[g] || {}, files);
      await renderFileList(res.notes);
      // Forced: that game's files must all be there. Adding: the game being added must be
      // complete. Otherwise any playable game's will do.
      const ready = FORCED ? !AS3DFiles.missing(chosen[FORCED], FORCED).length
                  : adding ? !AS3DFiles.missing(chosen[target], target).length
                  : startable().length > 0;
      const miss = ready ? [] : AS3DFiles.missing(chosen[target], target);
      const bad = res.notes.filter((n) => !n.ok).map((n) => n.text);
      // The games whose files are all there are kept, whichever game the page starts.
      const complete = Object.keys(chosen).filter((g) => games[g] && !AS3DFiles.missing(chosen[g], g).length);
      const others = complete.filter((g) => g !== target && res.games[g]);
      const info = others.map((g) => `${titleOf(g)}: files recognised and stored` +
        (games[g].playable ? '' : '; the game is not playable yet'));
      if (!res.notes.length) status.textContent = 'None of these is a file of a game this page knows.';
      else status.textContent = bad.concat(info, miss.length ? ['Still needed for ' + titleOf(target) + ': ' + miss.join(', ')] : []).join('; ');
      if (complete.length) {
        try {
          const keep = {};
          for (const g of complete) keep[g] = chosen[g];
          await AS3DFiles.store(keep);
          showStored(keep);
        } catch (e) {
          log('AS3D_WEB storage: ' + e);
        }
      }
      if (!ready) return;
      if (adding) {
        // The engine may already hold the first game: start over with every game offered.
        status.textContent = 'Stored. Reloading...';
        log('AS3D_WEB files_added=' + target);
        location.reload();
        return;
      }
      $('files').hidden = true;
      if (!FORCED) {
        status.textContent = '';
        await startOwn();
      } else {
        status.textContent = '';
        state.game = FORCED;
        resolveFiles(chosen[FORCED]);
      }
      setStatus('Starting the engine...');
    } catch (e) {
      status.textContent = 'Cannot read the files: ' + e.message;
    }
  }
  $('pick-files').addEventListener('change', (e) => takeFiles(e.target.files));
  $('pick-dir').addEventListener('change', (e) => takeFiles(e.target.files));
  if (!('webkitdirectory' in document.createElement('input')) || touchAtStart) $('pick-dir-label').hidden = true;
  const drop = $('drop');
  for (const ev of ['dragenter', 'dragover']) {
    document.addEventListener(ev, (e) => {
      if ($('files').hidden) return;
      e.preventDefault();
      drop.classList.add('over');
    });
  }
  document.addEventListener('dragleave', () => drop.classList.remove('over'));
  document.addEventListener('drop', async (e) => {
    if ($('files').hidden) return;
    e.preventDefault();
    drop.classList.remove('over');
    takeFiles(await AS3DFiles.droppedFiles(e.dataTransfer));
  });
  $('forget').addEventListener('click', async () => {
    if (!confirm('Remove the game files stored in this browser (all games)? You will have to choose them again. ' +
                 'Your progress and settings stay.')) return;
    try {
      await AS3DFiles.clearStored();
    } catch (e) { /* nothing stored */ }
    location.reload();
  });

  // ---------------------------------------------------------------------------------------
  // Start.
  // ---------------------------------------------------------------------------------------
  const play = $('play'), playFull = $('play-full');
  if (!touchAtStart && fsApi && !installed) playFull.hidden = false;
  function ready() {
    state.ready = true;
    $('progress').hidden = true;
    // Gulf Thunder does not play yet (as3d::gameIsPlayable): its files are accepted and kept,
    // the page starts them only with ?unfinished=1.
    AS3DFiles.knownFiles().then((kn) => {
      const def = kn[GAME];
      if (def && !def.playable && q.get('unfinished') !== '1') {
        state.notPlayable = GAME;
        setStatus(def.title + ' is not playable yet. Its files are kept in this browser for when it is.');
        if (LAUNCH) lwError(def.title + ' is not playable yet.');
        return;
      }
      play.disabled = false;
      setStatus(direct ? 'Ready (test mode: mission ' + (q.get('level') || '1') + ')' : '');
      // A game card was clicked (the chooser): it was the Play button.
      if (wantStart) start(wantStart.full);
      else if (q.get('autostart') === '1') start(false);
    }).catch((e) => fail(e.message));
  }
  // `full`: ask for full screen (touch devices always; must run in the click itself).
  function start(full) {
    if (state.started || !state.ready) return;
    state.started = true;
    const autoLauncher = !full && q.get('autostart') === '1' && underLauncher();
    if (full && fsApi && !installed) enterFullscreen();
    else if (installed) lockLandscape();
    else if (autoLauncher) autoFsArm(true);
    if (navigator.storage && navigator.storage.persist) {
      navigator.storage.persist().then((granted) => {
        state.persisted = granted;
        log('AS3D_WEB storage_persist=' + (granted ? 1 : 0));
      }, () => {});
    }
    if (state.touch) history.pushState({ as3d: 1 }, '');
    $('start').classList.remove('choosing');
    $('start').classList.add('running');
    setStatus('Starting...');
    canvas.focus();
    fit();
    state.running = true;
    lwProgress(0.97);
    const a = args.concat(['--game', GAME, '--dpi', String(Math.round(scale * cssPerInch()))]);
    // More than one game offered: the main menu's "Change game" comes back here.
    if (!FORCED && offered.length > 1) a.push('--change-game');
    log('AS3D_WEB start args=' + a.join(' '));
    try {
      Module.callMain(a);
    } catch (e) {
      if (e !== 'unwind') log('PAGE_ERROR: main: ' + e);
    }
    fit();
    updateFsButton();
  }
  play.addEventListener('click', () => start(touchAtStart));
  playFull.addEventListener('click', () => start(true));
  $('again').addEventListener('click', () => location.reload());

  // The start screen's controller poll (padStart above), only while a pad is connected and the
  // game has not started. A press counts on its down edge: the press that made the browser
  // reveal the pad is not taken. (A pad press is no user gesture for the browser: full screen
  // still needs a click or a key.)
  function padPoll() {
    padStart.raf = 0;
    if (state.started) return;
    const pads = navigator.getGamepads ? Array.from(navigator.getGamepads()).filter((p) => p && p.connected) : [];
    if (!pads.length) { padStart.prev = null; return; }
    const now = { left: false, right: false, go: false };
    for (const p of pads) {
      const b = (i) => !!(p.buttons[i] && p.buttons[i].pressed);
      const x = p.axes[0] || 0;
      now.left = now.left || b(14) || x < -0.5;
      now.right = now.right || b(15) || x > 0.5;
      now.go = now.go || b(0) || b(9);
    }
    const prev = padStart.prev;
    padStart.prev = now;
    if (prev) {
      if (padStart.nav) {
        if (now.left && !prev.left) padStart.nav.move(-1);
        if (now.right && !prev.right) padStart.nav.move(1);
        if (now.go && !prev.go) padStart.nav.play();
      } else if (now.go && !prev.go && !play.disabled && !$('start').hidden) {
        log('AS3D_WEB pad_play');
        start(touchAtStart);
      }
    }
    if (!state.started) padStart.raf = requestAnimationFrame(padPoll);
  }
  window.addEventListener('gamepadconnected', (e) => {
    log('AS3D_WEB gamepad connected index=' + e.gamepad.index + ' mapping=' + (e.gamepad.mapping || 'none') +
        ' id=' + e.gamepad.id);
    if (!state.started && !padStart.raf) padStart.raf = requestAnimationFrame(padPoll);
  });
  window.addEventListener('gamepaddisconnected', (e) => log('AS3D_WEB gamepad disconnected index=' + e.gamepad.index));

  // Calls from the engine.
  window.as3dPage = {
    onScreen(name) {
      state.screen = name;
      lwHide();
    },
    onLayout(x, y, r, hitR, w, h) {
      pauseButton = [x, y, r, hitR, w, h];
      placeFsButton();
    },
    onTouchMode(on) {
      state.touch = on;
      document.body.classList.toggle('touchdev', on);
      const lb = document.getElementById('l95-fs');   // touch has its own #fs-toggle beside the pause button
      if (lb) lb.style.display = on ? 'none' : '';
      updateFsButton();
      fit();
    },
    onFirstFrame() {
      state.firstFrame = true;
      state.firstFrameMs = performance.now();
      addLauncherFsButton();
      $('start').hidden = true;
      lwHide();
    },
    onFinished() {
      state.running = false;
      state.finished = true;
      fsBtn.hidden = true;
      autoFsDisarm();
      leaveFullscreen();
      rotate.hidden = true;
      // Launcher: quitting goes straight back to the desktop, once the profile is flushed.
      if (underLauncher() && !state.failed) {
        const t0 = performance.now();
        const go = () => {
          if (syncing && performance.now() - t0 < 2500) { setTimeout(go, 50); return; }
          try { window.launcherSaves.quitToDesktop(); } catch (e) { location.replace('/'); }
        };
        go();
        return;
      }
      $('ended').hidden = false;
    },
    onProfileSaved: syncProfile,
    // "Change game" (the profile was just saved): once browser storage has it, the start
    // screen again, without ?game= (the choice is remembered there).
    onChangeGame() {
      log('AS3D_WEB change_game');
      state.changingGame = true;
      Module.FS.syncfs(false, (err) => {
        if (err) log('AS3D_WEB profile_sync_failed ' + err);
        const u = new URL(location.href);
        u.searchParams.delete('game');
        u.searchParams.delete('autostart');
        location.href = u.toString();
      });
    },
  };

  // ---------------------------------------------------------------------------------------
  // The engine.
  // ---------------------------------------------------------------------------------------
  const probeGl = document.createElement('canvas').getContext('webgl2');
  if (!probeGl) {
    fail('This browser has no WebGL 2, which the game needs. Try a current Chrome, Edge, Firefox or Safari.');
    return;
  }
  const lose = probeGl.getExtension('WEBGL_lose_context');
  if (lose) lose.loseContext(); // the probe's context is not needed any more

  window.Module = {
    canvas,
    noInitialRun: true,
    print: log,
    printErr: log,
    locateFile: (p) => p + '?v=' + BUILD.stamp,
    preRun: [mountProfile, writeGameFiles, restoreProfile],
    setStatus: () => {},
    onRuntimeInitialized: () => {
      runtimeUp();
      Promise.all([dataWritten, restoreDone]).then(() => {
        state.runtimeMs = performance.now();
        lwProgress(0.9);   // engine and data ready
        ready();
      }, () => {});
    },
    onAbort: (what) => fail('The engine stopped: ' + what),
    onExit: () => { if (LAUNCH && !state.failed) window.launcherSaves.quitToDesktop(); },
  };
  if (BUILD.mode === 'byo') ownFiles().catch((e) => fail('Cannot read the stored files: ' + e.message));
  else bundledFiles().catch((e) => fail(e.message));
  const s = document.createElement('script');
  s.src = 'as3d_web.js?v=' + BUILD.stamp;
  s.onerror = () => fail('Cannot load the engine (as3d_web.js).');
  document.body.appendChild(s);
  fit();
})();

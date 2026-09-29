// AirStrike 3D, web version: the page around the engine (docs/web.md, docs/spec/issues/150).
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

  function fail(text) {
    state.failed = text;
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
  if (intParam('level', 1, 20)) args.push('--level', intParam('level', 1, 20));
  if (q.get('bot') === '1') args.push('--bot');
  if (q.get('menus') === '1') args.push('--menus');
  if (q.get('god') === '1') args.push('--god');
  if (q.get('noaudio') === '1') args.push('--no-audio');
  if (q.get('fps') === '1') args.push('--fps');
  if (intParam('frames', 1, 1e9)) args.push('--frames', intParam('frames', 1, 1e9));
  if (intParam('difficulty', 0, 4)) args.push('--difficulty', intParam('difficulty', 0, 4));

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
    Module.FS.syncfs(true, (err) => {
      if (err) log('AS3D_WEB profile storage unavailable: ' + err);
      Module.removeRunDependency('as3d-profile');
    });
  }

  // ---------------------------------------------------------------------------------------
  // Game files: bundled (fetched from data/) or the player's own (IndexedDB).
  // ---------------------------------------------------------------------------------------
  let resolveFiles;
  const filesReady = new Promise((ok) => { resolveFiles = ok; });
  function writeGameFiles() {
    Module.addRunDependency('as3d-data');
    filesReady.then(async (files) => {
      Module.FS.mkdir('/data');
      for (const [name, v] of Object.entries(files)) {
        const bytes = v instanceof Uint8Array ? v : new Uint8Array(await v.arrayBuffer());
        Module.FS.writeFile('/data/' + name, bytes);
      }
      log('AS3D_WEB data=' + Object.keys(files).sort().join(','));
      Module.removeRunDependency('as3d-data');
    }).catch((e) => fail('Cannot prepare the game files: ' + e.message));
  }

  const bar = $('bar');
  function progress(f) {
    $('progress').hidden = false;
    bar.style.width = Math.round(100 * f) + '%';
  }

  async function bundledFiles() {
    setStatus('Loading the game data...');
    const files = await AS3DFiles.fetchBundled(progress);
    progress(1);
    resolveFiles(files);
  }

  // Bring your own: the stored files, else the picker until the three paks are there.
  let chosen = {};
  async function ownFiles() {
    let stored = {};
    try {
      stored = await AS3DFiles.loadStored();
    } catch (e) {
      log('AS3D_WEB storage: ' + e);
      $('warn').hidden = false;
      $('warn').textContent = 'This browser does not let the page store files (private window?): ' +
        'the game files must be chosen again on every visit.';
    }
    if (!AS3DFiles.missing(stored).length) {
      showStored(stored);
      resolveFiles(stored);
      return;
    }
    chosen = stored;
    setStatus('');
    $('files').hidden = false;
    await renderFileList([]);
  }
  function showStored(files) {
    const mb = Object.values(files).reduce((s, b) => s + (b.size || b.length || 0), 0) / 1048576;
    $('stored').hidden = false;
    $('stored-text').textContent = `Your game files are kept in this browser (${mb.toFixed(0)} MB: ` +
      Object.keys(files).sort().join(', ') + ').';
  }
  async function renderFileList(notes) {
    const kn = await AS3DFiles.knownFiles();
    const ul = $('files-list');
    ul.textContent = '';
    for (const n of ['pak0.apk', 'pak1.apk', 'pak2.apk', 'Settings.xml', 'logo2s.tga', 'texts_v170.txt']) {
      const li = document.createElement('li');
      const have = !!chosen[n];
      const bad = notes.find((x) => !x.ok && (x.name === n || (n === 'texts_v170.txt' && x.name === 'AirStrike3D.exe')));
      li.className = have ? 'ok' : bad ? 'bad' : 'missing';
      const what = kn[n] && kn[n].gives ? ` (optional: ${kn[n].gives})` : n.endsWith('.apk') ? ' (needed)' : '';
      li.textContent = n + (have ? '' : what) + (bad ? ' - ' + bad.text : '');
      ul.appendChild(li);
    }
  }
  async function takeFiles(list) {
    const status = $('files-status');
    status.textContent = 'Checking...';
    try {
      const res = await AS3DFiles.check(list, (name, f) => {
        status.textContent = name ? `Checking ${name}...` : 'Checking...';
        progress(f);
      });
      Object.assign(chosen, res.files);
      await renderFileList(res.notes);
      const miss = AS3DFiles.missing(chosen);
      const bad = res.notes.filter((n) => !n.ok).map((n) => n.text);
      if (!res.notes.length) status.textContent = 'None of these is a file of the game.';
      else status.textContent = bad.join('; ') || (miss.length ? 'Still needed: ' + miss.join(', ') : '');
      if (miss.length) return;
      status.textContent = 'Storing the files in this browser...';
      try {
        await AS3DFiles.store(chosen);
        showStored(chosen);
      } catch (e) {
        log('AS3D_WEB storage: ' + e);
      }
      status.textContent = '';
      $('files').hidden = true;
      resolveFiles(chosen);
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
    if (!confirm('Remove the game files stored in this browser? You will have to choose them again. ' +
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
    play.disabled = false;
    setStatus(direct ? 'Ready (test mode: mission ' + (q.get('level') || '1') + ')' : '');
    $('progress').hidden = true;
    if (q.get('autostart') === '1') start(false);
  }
  // `full`: ask for full screen (touch devices always; must run in the click itself).
  function start(full) {
    if (state.started || !state.ready) return;
    state.started = true;
    if (full && fsApi && !installed) enterFullscreen();
    else if (installed) lockLandscape();
    if (navigator.storage && navigator.storage.persist) {
      navigator.storage.persist().then((granted) => {
        state.persisted = granted;
        log('AS3D_WEB storage_persist=' + (granted ? 1 : 0));
      }, () => {});
    }
    if (state.touch) history.pushState({ as3d: 1 }, '');
    $('start').classList.add('running');
    setStatus('Starting...');
    canvas.focus();
    fit();
    state.running = true;
    const a = args.concat(['--dpi', String(Math.round(scale * cssPerInch()))]);
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

  // Calls from the engine.
  window.as3dPage = {
    onScreen(name) {
      state.screen = name;
    },
    onLayout(x, y, r, hitR, w, h) {
      pauseButton = [x, y, r, hitR, w, h];
      placeFsButton();
    },
    onTouchMode(on) {
      state.touch = on;
      document.body.classList.toggle('touchdev', on);
      updateFsButton();
      fit();
    },
    onFirstFrame() {
      state.firstFrame = true;
      state.firstFrameMs = performance.now();
      $('start').hidden = true;
    },
    onFinished() {
      state.running = false;
      state.finished = true;
      fsBtn.hidden = true;
      leaveFullscreen();
      rotate.hidden = true;
      $('ended').hidden = false;
    },
    onProfileSaved: syncProfile,
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
    preRun: [mountProfile, writeGameFiles],
    setStatus: () => {},
    onRuntimeInitialized: () => {
      state.runtimeMs = performance.now();
      ready();
    },
    onAbort: (what) => fail('The engine stopped: ' + what),
  };
  if (BUILD.mode === 'byo') ownFiles().catch((e) => fail('Cannot read the stored files: ' + e.message));
  else bundledFiles().catch((e) => fail(e.message));
  const s = document.createElement('script');
  s.src = 'as3d_web.js?v=' + BUILD.stamp;
  s.onerror = () => fail('Cannot load the engine (as3d_web.js).');
  document.body.appendChild(s);
  fit();
})();

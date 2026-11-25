// "Files for the 3DS" (docs/web.md "Files for the 3DS"): from the game files stored in this
// browser, one zip of the SD card folder the 3DS app reads, sdmc:/3ds/openairstrike/<key>/, per
// game: music/*.ogg (the pre-rendered music, docs/3ds-port.md "M2.4 Music"), the texts file
// (made from the game's exe when the files were given) and, when asked, the paks and
// Settings.xml. The music is rendered by the worker (music_worker.js, as3d_music.wasm: the code
// of audio_tool render-music), loaded only here; the zip is written here (stored, no
// compression: Ogg and paks do not compress). Nothing leaves the browser.
// window.as3dSd3ds is read by the tests (apps/web/test/walk.py, byo_3ds).
'use strict';

window.AS3DSd3ds = (function () {
  // ---------------------------------------------------------------------------------------
  // A store-only zip writer (PKWARE APPNOTE 4.3: local headers, central directory, end
  // record; no zip64, so the whole zip stays under 4 GB). Entries are Blobs or Uint8Arrays.
  // ---------------------------------------------------------------------------------------
  const CRC = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();
  function crcUpdate(crc, bytes) {
    let c = crc ^ 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = CRC[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }
  async function crcOf(data) {
    if (data instanceof Uint8Array) return crcUpdate(0, data);
    let crc = 0;
    const chunk = 8 << 20;
    for (let o = 0; o < data.size; o += chunk) {
      crc = crcUpdate(crc, new Uint8Array(await data.slice(o, o + chunk).arrayBuffer()));
    }
    return crc;
  }
  function dosTime(d) {
    return {
      time: (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1),
      date: (Math.max(0, d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate(),
    };
  }
  function ZipWriter() {
    const parts = [], central = [];
    let offset = 0, count = 0;
    const when = dosTime(new Date());
    this.add = async (name, data) => {
      const nameBytes = new TextEncoder().encode(name);
      const size = data instanceof Uint8Array ? data.length : data.size;
      const crc = await crcOf(data);
      if (offset + size + 1024 > 0xffffffff) throw new Error('the zip would exceed 4 GB');
      const lh = new DataView(new ArrayBuffer(30));
      lh.setUint32(0, 0x04034b50, true);
      lh.setUint16(4, 10, true);           // version needed: 1.0 (stored)
      lh.setUint16(6, 0x0800, true);       // UTF-8 names
      lh.setUint16(8, 0, true);            // stored
      lh.setUint16(10, when.time, true);
      lh.setUint16(12, when.date, true);
      lh.setUint32(14, crc, true);
      lh.setUint32(18, size, true);
      lh.setUint32(22, size, true);
      lh.setUint16(26, nameBytes.length, true);
      lh.setUint16(28, 0, true);
      parts.push(lh.buffer, nameBytes, data);
      const ch = new DataView(new ArrayBuffer(46));
      ch.setUint32(0, 0x02014b50, true);
      ch.setUint16(4, 0x0314, true);       // made by: Unix, 2.0
      ch.setUint16(6, 10, true);
      ch.setUint16(8, 0x0800, true);
      ch.setUint16(10, 0, true);
      ch.setUint16(12, when.time, true);
      ch.setUint16(14, when.date, true);
      ch.setUint32(16, crc, true);
      ch.setUint32(20, size, true);
      ch.setUint32(24, size, true);
      ch.setUint16(28, nameBytes.length, true);
      ch.setUint32(38, (0o100644 << 16) >>> 0, true);  // a regular file, rw-r--r--
      ch.setUint32(42, offset, true);
      central.push(ch.buffer, nameBytes);
      offset += 30 + nameBytes.length + size;
      count++;
    };
    this.finish = () => {
      const cdSize = central.reduce((s, b) => s + b.byteLength, 0);
      const end = new DataView(new ArrayBuffer(22));
      end.setUint32(0, 0x06054b50, true);
      end.setUint16(8, count, true);
      end.setUint16(10, count, true);
      end.setUint32(12, cdSize, true);
      end.setUint32(16, offset, true);
      return new Blob(parts.concat(central, [end.buffer]), { type: 'application/zip' });
    };
  }

  // ---------------------------------------------------------------------------------------
  // The music worker: one per zip, terminated when done or cancelled.
  // ---------------------------------------------------------------------------------------
  function startWorker(stamp) {
    const w = new Worker('music_worker.js?v=' + encodeURIComponent(stamp));
    const waiting = [];
    let fault = null;
    w.onmessage = (e) => {
      const m = e.data;
      const i = waiting.findIndex((x) => x.type === m.type || m.type === 'error');
      if (m.type === 'progress' && w.onprogress) { w.onprogress(m); return; }
      if (m.type === 'log' && w.onlog) { w.onlog(m.text); return; }
      if (i >= 0) {
        const x = waiting.splice(i, 1)[0];
        if (m.type === 'error') x.fail(new Error(m.message)); else x.ok(m);
      } else if (m.type === 'error') {
        fault = new Error(m.message);
      }
    };
    w.onerror = (e) => {
      const err = new Error(e.message || 'the music worker failed to load');
      for (const x of waiting.splice(0)) x.fail(err);
      fault = err;
      e.preventDefault();
    };
    // Resolves with the worker's next message of `type` (or rejects on its error).
    w.next = (type) => (fault ? Promise.reject(fault) : new Promise((ok, fail) => waiting.push({ type, ok, fail })));
    w.postMessage({ type: 'init', stamp });
    return w;
  }

  // ---------------------------------------------------------------------------------------
  // The dialog.
  // ---------------------------------------------------------------------------------------
  const $ = (id) => document.getElementById(id);
  const state = window.as3dSd3ds = { open: false, running: false, done: false, cancelled: false, error: null, files: [],
                                     renders: [], zipBytes: 0, ms: 0 };
  let ctx = null;        // { stored: () => {key: {name: Blob}}, known, log }
  let worker = null;
  let zipUrl = null;
  const mb = (n) => (n / 1048576).toFixed(1) + ' MB';

  // The games whose required files are all stored, in the order of known_files.json.
  function completeGames() {
    const stored = ctx.stored();
    return Object.keys(ctx.known).filter((g) => stored[g] && !AS3DFiles.missing(stored[g], g).length);
  }
  function setStatus(text) { $('sd3ds-status').textContent = text; }
  function setBar(f) {
    $('sd3ds-progress').hidden = f === null;
    if (f !== null) $('sd3ds-bar').style.width = Math.round(100 * Math.max(0, Math.min(1, f))) + '%';
  }
  function setRunning(on) {
    state.running = on;
    $('sd3ds-make').hidden = on;
    $('sd3ds-cancel').hidden = !on;
    $('sd3ds-close').hidden = on;
    $('sd3ds-paks').disabled = on;
    for (const c of $('sd3ds-games').querySelectorAll('input')) c.disabled = on;
  }
  function dropZip() {
    if (zipUrl) URL.revokeObjectURL(zipUrl);
    zipUrl = null;
    $('sd3ds-save').hidden = true;
    $('sd3ds-save').removeAttribute('href');
  }

  function open() {
    const games = completeGames();
    const box = $('sd3ds-games');
    box.textContent = '';
    for (const g of games) {
      const l = document.createElement('label');
      const c = document.createElement('input');
      c.type = 'checkbox';
      c.checked = true;
      c.value = g;
      const texts = ctx.known[g].texts;
      l.append(c, ' ' + ctx.known[g].title + (ctx.stored()[g][texts] ? '' : ' (no texts file)'));
      box.appendChild(l);
    }
    if (!games.length) {
      const s = document.createElement('span');
      s.className = 'none';
      s.textContent = 'No game has all its files in this browser yet.';
      box.appendChild(s);
    }
    $('sd3ds-make').disabled = !games.length;
    if (!state.running) {
      setStatus('');
      setBar(null);
    }
    const d = $('sd3ds');
    try { d.showModal(); } catch (e) { d.setAttribute('open', ''); }
    state.open = true;
    ctx.log('AS3D_WEB sd3ds_open games=' + games.join(','));
  }
  function close() {
    if (state.running) return;
    const d = $('sd3ds');
    try { d.close(); } catch (e) { d.removeAttribute('open'); }
    state.open = false;
  }
  function cancel() {
    if (!state.running) return;
    state.cancelled = true;
    if (worker) worker.terminate();
    worker = null;
    setRunning(false);
    setBar(null);
    setStatus('Cancelled.');
    ctx.log('AS3D_WEB sd3ds_cancelled');
  }

  async function make() {
    const games = Array.from($('sd3ds-games').querySelectorAll('input:checked')).map((c) => c.value);
    if (!games.length) { setStatus('Choose at least one game.'); return; }
    const withPaks = $('sd3ds-paks').checked;
    dropZip();
    $('sd3ds-make').className = 'primary';
    $('sd3ds-make').textContent = 'Make the zip';
    Object.assign(state, { done: false, cancelled: false, error: null, files: [], renders: [], zipBytes: 0, ms: 0 });
    setRunning(true);
    setBar(0);
    setStatus('Loading the music renderer...');
    const t0 = performance.now();
    const stamp = (window.AS3D_BUILD || {}).stamp || '';
    const me = worker = startWorker(stamp);
    me.onlog = (t) => ctx.log('AS3D_MUSIC ' + t);
    const zip = new ZipWriter();
    const stored = ctx.stored();
    const notes = [];
    try {
      await me.next('ready');
      // Every game's file list first, so the count covers them all ("Rendering music 3/27").
      const plan = [];
      for (const g of games) {
        const files = stored[g];
        const paks = Object.keys(files).filter((n) => /^pak\d+\.apk$/i.test(n)).sort();
        me.postMessage({ type: 'jobs', game: g, paks: paks.map((n) => ({ name: n, blob: files[n] })) });
        const r = await me.next('jobs');
        if (!r.jobs.length) notes.push(`${ctx.known[g].title}: no music found in its paks`);
        for (let i = 0; i < r.jobs.length; i++) plan.push({ game: g, index: i, file: r.jobs[i].file, module: r.jobs[i].module, order: r.jobs[i].order });
      }
      let current = null;
      me.onprogress = (m) => {
        if (current === null || worker !== me) return;
        setBar((current + Math.min(1, m.fraction)) / plan.length);
      };
      let game = null;
      for (let k = 0; k < plan.length; k++) {
        const job = plan[k];
        if (job.game !== game) {
          // The worker holds one game's paks at a time.
          game = job.game;
          const files = stored[game];
          const paks = Object.keys(files).filter((n) => /^pak\d+\.apk$/i.test(n)).sort();
          me.postMessage({ type: 'jobs', game, paks: paks.map((n) => ({ name: n, blob: files[n] })) });
          await me.next('jobs');
        }
        current = k;
        setStatus(`Rendering music ${k + 1}/${plan.length} (${ctx.known[game].title}: ${job.file})`);
        setBar(k / plan.length);
        const tr = performance.now();
        me.postMessage({ type: 'render', index: job.index });
        const r = await me.next('file');
        if (worker !== me) return;
        const ms = Math.round(performance.now() - tr);
        await zip.add(`3ds/openairstrike/${game}/music/${job.file}`, r.bytes);
        state.files.push(`3ds/openairstrike/${game}/music/${job.file}`);
        state.renders.push({ game, file: job.file, order: job.order, frames: r.info.frames, rate: r.info.rate,
                             seconds: r.info.seconds, peak: r.info.peak, bytes: r.info.bytes, ms });
        ctx.log(`AS3D_WEB sd3ds_music game=${game} file=${job.file} order=${job.order} frames=${r.info.frames} ` +
                `rate=${r.info.rate} bytes=${r.info.bytes} peak=${r.info.peak} ms=${ms}`);
      }
      me.terminate();
      if (worker === me) worker = null;
      // The texts files and, when asked, the game files.
      setStatus('Writing the zip...');
      for (const g of games) {
        const files = stored[g];
        const names = [ctx.known[g].texts];
        if (withPaks) names.push(...Object.keys(files).filter((n) => /^pak\d+\.apk$/i.test(n)).sort(), 'Settings.xml');
        for (const n of names) {
          if (!files[n]) {
            if (n === ctx.known[g].texts) notes.push(`${ctx.known[g].title}: no texts file stored (give the page the game's exe, or copy the exe into its 3DS folder)`);
            continue;
          }
          await zip.add(`3ds/openairstrike/${g}/${n}`, files[n]);
          state.files.push(`3ds/openairstrike/${g}/${n}`);
          if (state.cancelled) return;
        }
      }
      const blob = zip.finish();
      zipUrl = URL.createObjectURL(blob);
      const save = $('sd3ds-save');
      save.href = zipUrl;
      save.hidden = false;
      state.zipBytes = blob.size;
      state.ms = Math.round(performance.now() - t0);
      state.done = true;
      setBar(1);
      $('sd3ds-make').className = 'secondary';  // the download is the next step
      $('sd3ds-make').textContent = 'Make it again';
      const music = state.renders.length;
      setStatus(`Ready: ${music} music files for ${games.map((g) => ctx.known[g].title).join(', ')}, ${mb(blob.size)}, ` +
                `made in ${Math.round(state.ms / 1000)} s.` + (notes.length ? ' ' + notes.join('; ') + '.' : ''));
      ctx.log(`AS3D_WEB sd3ds_done games=${games.join(',')} music=${music} files=${state.files.length} bytes=${blob.size} ms=${state.ms}`);
    } catch (e) {
      if (state.cancelled || worker !== me) return;
      state.error = e.message;
      setStatus('Could not make the files: ' + e.message);
      setBar(null);
      ctx.log('AS3D_WEB sd3ds_failed ' + e.message);
      me.terminate();
      worker = null;
    } finally {
      if (worker === me || worker === null) setRunning(false);
    }
  }

  // `c`: { stored: () => {key: {name: Blob}}, known: known_files.json's games, log(text) }.
  function setup(c) {
    ctx = c;
    if (setup.done) return;
    setup.done = true;
    $('sd3ds-open').addEventListener('click', open);
    $('sd3ds-close').addEventListener('click', close);
    $('sd3ds-cancel').addEventListener('click', cancel);
    $('sd3ds-make').addEventListener('click', make);
    $('sd3ds').addEventListener('cancel', (e) => { if (state.running) e.preventDefault(); else state.open = false; });
  }
  // Whether the button is offered: a game has all its files stored.
  function available(c) { ctx = c; return completeGames().length > 0; }

  return { setup, available, open, ZipWriter, crcUpdate };
})();

// The game files of the web version (docs/web.md): fetched from the site (bundled build) or
// supplied by the player (bring-your-own build), checked against known_files.json and kept in
// IndexedDB. Also reads the front-end texts out of the player's AirStrike3D.exe, a port of
// tools/extract_exe_texts.py (keep the two in step). No game data is part of this file.
'use strict';

window.AS3DFiles = (function () {
  // ---------------------------------------------------------------------------------------
  // SHA-256: WebCrypto where the page is a secure context, else this small implementation
  // (plain http on a home network has no crypto.subtle).
  // ---------------------------------------------------------------------------------------
  const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);

  function sha256Js(bytes) {
    const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    const w = new Uint32Array(64);
    const n = bytes.length;
    const total = Math.ceil((n + 9) / 64) * 64;
    const tail = new Uint8Array(total - Math.floor(n / 64) * 64);
    const tailStart = Math.floor(n / 64) * 64;
    tail.set(bytes.subarray(tailStart));
    tail[n - tailStart] = 0x80;
    const bits = n * 8;
    const dv = new DataView(tail.buffer);
    dv.setUint32(tail.length - 8, Math.floor(bits / 0x100000000));
    dv.setUint32(tail.length - 4, bits >>> 0);
    const block = (src, off) => {
      for (let i = 0; i < 16; i++) {
        w[i] = (src[off + 4 * i] << 24) | (src[off + 4 * i + 1] << 16) | (src[off + 4 * i + 2] << 8) | src[off + 4 * i + 3];
      }
      for (let i = 16; i < 64; i++) {
        const a = w[i - 15], b = w[i - 2];
        const s0 = ((a >>> 7) | (a << 25)) ^ ((a >>> 18) | (a << 14)) ^ (a >>> 3);
        const s1 = ((b >>> 17) | (b << 15)) ^ ((b >>> 19) | (b << 13)) ^ (b >>> 10);
        w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
      }
      let a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
      for (let i = 0; i < 64; i++) {
        const S1 = ((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7));
        const ch = (e & f) ^ (~e & g);
        const t1 = (hh + S1 + ch + K[i] + w[i]) | 0;
        const S0 = ((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10));
        const mj = (a & b) ^ (a & c) ^ (b & c);
        const t2 = (S0 + mj) | 0;
        hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
      }
      h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    };
    for (let off = 0; off + 64 <= tailStart; off += 64) block(bytes, off);
    for (let off = 0; off < tail.length; off += 64) block(tail, off);
    return Array.from(h, (v) => v.toString(16).padStart(8, '0')).join('');
  }

  async function sha256(bytes) {
    if (window.crypto && crypto.subtle && window.isSecureContext) {
      try {
        const d = await crypto.subtle.digest('SHA-256', bytes);
        return Array.from(new Uint8Array(d), (v) => v.toString(16).padStart(2, '0')).join('');
      } catch (e) { /* fall through */ }
    }
    return sha256Js(bytes);
  }

  // ---------------------------------------------------------------------------------------
  // The front-end texts in the player's AirStrike3D.exe (tools/extract_exe_texts.py).
  // ---------------------------------------------------------------------------------------
  const PAGES = [
    [1, 0x449EA4, 0x44A1F8], [2, 0x44A210, 0x44A430], [3, 0x44A448, 0x44A688], [4, 0x44A694, 0x44A850],
    [5, 0x44A870, 0x44AA94], [6, 0x44AAB4, 0x44AB3C], [7, 0x44AB5C, 0x44AD90], [8, 0x44ADA8, 0x44AE28],
    [9, 0x44AE40, 0x44AFF0], [10, 0x44AFF8, 0x44B07C]];
  const CONGRATS = [[0, 0x449D6C], [2, 0x449D80], [3, 0x449DBC], [4, 0x449DF0]];
  const RANK_TABLE = 0x45650C;
  const PAGE_HINTS = [['info.hint.prev', 0x44B084], ['info.hint.next', 0x44B09C], ['info.page', 0x44B0B0]];
  const PAGE_VALUES = 0x449E50;

  function extractExeTexts(data) {
    const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const fail = (m) => { throw new Error(m); };
    if (data[0] !== 0x4D || data[1] !== 0x5A) fail('not an MZ executable');
    const pe = dv.getUint32(0x3C, true);
    if (pe + 24 > data.length || dv.getUint32(pe, true) !== 0x00004550) fail('no PE header');
    const nsec = dv.getUint16(pe + 6, true), opt = dv.getUint16(pe + 20, true);
    if (opt < 32 || nsec === 0 || nsec > 96) fail('unexpected PE layout');
    const base = dv.getUint32(pe + 24 + 28, true);
    if (base !== 0x400000) fail('unexpected image base');
    const sections = [];
    let o = pe + 24 + opt;
    for (let i = 0; i < nsec; i++, o += 40) {
      if (o + 40 > data.length) fail('truncated section table');
      let name = '';
      for (let k = 0; k < 8 && data[o + k]; k++) name += String.fromCharCode(data[o + k]);
      sections.push({ name, va: dv.getUint32(o + 12, true), vsize: dv.getUint32(o + 8, true),
                      rsize: dv.getUint32(o + 16, true), roff: dv.getUint32(o + 20, true) });
    }
    const offset = (addr) => {
      const rva = addr - base;
      for (const s of sections) {
        if (s.va <= rva && rva < s.va + Math.min(s.vsize, s.rsize)) {
          const off = s.roff + rva - s.va;
          if (off < data.length) return off;
        }
      }
      fail('address not in the file');
    };
    const cstr = (addr, limit = 256) => {
      const off = offset(addr);
      let end = -1;
      for (let i = off; i <= Math.min(off + limit, data.length - 1); i++) if (data[i] === 0) { end = i; break; }
      if (end < 0) fail('no string terminator');
      let s = '';
      for (let i = off; i < end; i++) {
        if (data[i] < 0x20 || data[i] > 0x7E) fail('non-text bytes');
        s += String.fromCharCode(data[i]);
      }
      return s;
    };
    const u32 = (addr) => dv.getUint32(offset(addr), true);
    const text = sections.find((s) => s.name === '.text') || fail('no .text section');
    const blob = data.subarray(text.roff, text.roff + text.rsize);
    const find = (needle, from) => {
      outer: for (let i = from; i + 4 <= blob.length; i++) {
        for (let k = 0; k < 4; k++) if (blob[i + k] !== needle[k]) continue outer;
        return i;
      }
      return -1;
    };
    const lineSlot = (addr) => {
      const needle = [addr & 255, (addr >>> 8) & 255, (addr >>> 16) & 255, (addr >>> 24) & 255];
      for (let i = find(needle, 0); i >= 0; i = find(needle, i + 1)) {
        let disp = null;
        if (i >= 4 && blob[i - 4] === 0xC7 && blob[i - 3] === 0x44 && blob[i - 2] === 0x24) disp = blob[i - 1];
        else if (i >= 3 && blob[i - 3] === 0xC7 && (blob[i - 2] & 0xF8) === 0x40 && (blob[i - 2] & 7) !== 4) disp = blob[i - 1];
        if (disp !== null && disp >= 0x14 && (disp - 0x14) % 4 === 0 && (disp - 0x14) / 4 < 32) return (disp - 0x14) / 4;
      }
      return null;
    };
    const entries = [];
    for (const [page, body, title] of PAGES) {
      entries.push([`info.${page}.title`, cstr(title)]);
      const lines = [];
      let a = body;
      while (a < title) {
        const s = cstr(a);
        lines.push([a, s]);
        a += s.length + 1;
        while (a < title && data[offset(a)] === 0) a++;
      }
      if (!lines.length) fail(`page ${page} has no text`);
      let next = 0;
      const used = new Set();
      for (const [addr, s] of lines) {
        let slot = lineSlot(addr);
        if (slot === null || used.has(slot)) slot = next;
        used.add(slot);
        next = slot + 1;
        entries.push([`info.${page}.${slot}`, s]);
      }
    }
    for (const [line, addr] of CONGRATS) entries.push([`congrats.${line}`, cstr(addr)]);
    for (let i = 0; i < 7; i++) entries.push([`rank.${i}`, cstr(u32(RANK_TABLE + 4 * i), 32)]);
    for (const [key, addr] of PAGE_HINTS) entries.push([key, cstr(addr, 64)]);
    for (let k = 0; k < 10; k++) entries.push([`info.pages.${10 - k}`, cstr(PAGE_VALUES + 8 * k + (k === 0 ? 0 : 4), 16)]);
    const quote = (s) => '"' + s.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
    let out = '# AirStrike 3D v1.70 front-end texts, read from the user\'s executable by\n' +
              '# tools/extract_exe_texts.py. Do not commit. Format: key = "value".\n';
    for (const [k, v] of entries) out += `${k} = ${quote(v)}\n`;
    return out;
  }

  // ---------------------------------------------------------------------------------------
  // IndexedDB: one store, file name -> Blob.
  // ---------------------------------------------------------------------------------------
  const DB = 'as3d-game-files', STORE = 'files';
  function openDb() {
    return new Promise((ok, fail) => {
      const r = indexedDB.open(DB, 1);
      r.onupgradeneeded = () => r.result.createObjectStore(STORE);
      r.onsuccess = () => ok(r.result);
      r.onerror = () => fail(r.error);
    });
  }
  async function tx(mode, fn) {
    const db = await openDb();
    try {
      return await new Promise((ok, fail) => {
        const t = db.transaction(STORE, mode);
        const res = fn(t.objectStore(STORE));
        t.oncomplete = () => ok(res && 'result' in res ? res.result : undefined);
        t.onerror = () => fail(t.error);
        t.onabort = () => fail(t.error || new Error('storage transaction aborted'));
      });
    } finally {
      db.close();
    }
  }
  async function loadStored() {
    const out = {};
    const db = await openDb();
    try {
      await new Promise((ok, fail) => {
        const t = db.transaction(STORE, 'readonly');
        const c = t.objectStore(STORE).openCursor();
        c.onsuccess = () => {
          const cur = c.result;
          if (!cur) return;
          out[cur.key] = cur.value;
          cur.continue();
        };
        t.oncomplete = ok;
        t.onerror = () => fail(t.error);
      });
    } finally {
      db.close();
    }
    return out;
  }
  const store = (files) => tx('readwrite', (s) => { for (const [k, v] of Object.entries(files)) s.put(v, k); });
  const clearStored = () => tx('readwrite', (s) => s.clear());

  // ---------------------------------------------------------------------------------------
  // Checking what the player gave.
  // ---------------------------------------------------------------------------------------
  let known = null;
  async function knownFiles() {
    if (!known) {
      const r = await fetch('known_files.json?v=' + ((window.AS3D_BUILD || {}).stamp || ''));
      known = (await r.json()).files;
    }
    return known;
  }
  const REQUIRED = ['pak0.apk', 'pak1.apk', 'pak2.apk'];

  // Canonical name of a known file from any path and case; null for other files.
  function canonical(path) {
    const base = path.split(/[\\/]/).pop().toLowerCase();
    for (const n of ['pak0.apk', 'pak1.apk', 'pak2.apk', 'Settings.xml', 'logo2s.tga', 'AirStrike3D.exe', 'texts_v170.txt']) {
      if (n.toLowerCase() === base) return n;
    }
    return null;
  }

  // Checks the given File objects. Returns { files: {name: Blob}, notes: [{name, ok, text}] }:
  // the files to keep (the exe becomes texts_v170.txt) and a line per file looked at.
  async function check(fileList, progress) {
    const kn = await knownFiles();
    const files = {}, notes = [];
    const list = Array.from(fileList).filter((f) => canonical(f.webkitRelativePath || f.name));
    // A logo only from gfx/ when a whole folder was given (the install has no other).
    let i = 0;
    for (const f of list) {
      const name = canonical(f.webkitRelativePath || f.name);
      const k = kn[name];
      if (progress) progress(name, i++ / Math.max(1, list.length));
      if (k && k.size && f.size !== k.size) {
        notes.push({ name, ok: false, text: `${name}: ${f.size} bytes, expected ${k.size} (not the v1.70 file)` });
        continue;
      }
      const bytes = new Uint8Array(await f.arrayBuffer());
      if (k && k.sha256) {
        const h = await sha256(bytes);
        if (h !== k.sha256) {
          notes.push({ name, ok: false, text: `${name}: the contents differ from the v1.70 file` });
          continue;
        }
      }
      if (name === 'AirStrike3D.exe') {
        try {
          const t = extractExeTexts(bytes);
          files['texts_v170.txt'] = new Blob([t], { type: 'text/plain' });
          notes.push({ name, ok: true, text: 'AirStrike3D.exe: texts read (the executable itself is not kept)' });
        } catch (e) {
          notes.push({ name, ok: false, text: `AirStrike3D.exe: ${e.message}` });
        }
        continue;
      }
      if (name === 'texts_v170.txt') {
        const t = new TextDecoder().decode(bytes);
        if (!/^info\.1\.title = "/m.test(t)) {
          notes.push({ name, ok: false, text: 'texts_v170.txt: not the file tools/extract_exe_texts.py writes' });
          continue;
        }
        if (files['texts_v170.txt']) continue; // the exe gave them already
      }
      files[name] = new Blob([bytes]);
      notes.push({ name, ok: true, text: `${name}: OK` });
    }
    if (progress) progress('', 1);
    return { files, notes };
  }

  function missing(files) { return REQUIRED.filter((n) => !files[n]); }

  // Every File in a drop, folders included (Chromium, Firefox, Safari).
  async function droppedFiles(dt) {
    const items = Array.from(dt.items || []);
    const entries = items.map((it) => (it.webkitGetAsEntry ? it.webkitGetAsEntry() : null)).filter(Boolean);
    if (!entries.length) return Array.from(dt.files || []);
    const out = [];
    const walk = async (e, depth) => {
      if (e.isFile) {
        if (canonical(e.name)) out.push(await new Promise((ok, fail) => e.file(ok, fail)));
      } else if (e.isDirectory && depth < 4) {
        const r = e.createReader();
        for (;;) {
          const batch = await new Promise((ok, fail) => r.readEntries(ok, fail));
          if (!batch.length) break;
          for (const c of batch) await walk(c, depth + 1);
        }
      }
    };
    for (const e of entries) await walk(e, 0);
    return out;
  }

  // The bundled build: the files in data/ of the site, with a progress callback (0..1).
  async function fetchBundled(progress) {
    const kn = await knownFiles();
    let names = ['pak0.apk', 'pak1.apk', 'pak2.apk', 'Settings.xml', 'logo2s.tga', 'texts_v170.txt'];
    try {
      const r = await fetch('data/index.txt', { cache: 'no-cache' });
      if (r.ok) names = (await r.text()).split(/\s+/).filter((n) => n && n !== 'index.txt');
    } catch (e) { /* the default list */ }
    const total = names.reduce((s, n) => s + ((kn[n] && kn[n].size) || 10000), 0);
    let done = 0;
    const out = {};
    for (const n of names) {
      const r = await fetch('data/' + n);
      if (!r.ok) {
        if (REQUIRED.includes(n)) throw new Error(`cannot load data/${n} (HTTP ${r.status})`);
        continue;
      }
      if (!r.body || !r.body.getReader) {
        const b = new Uint8Array(await r.arrayBuffer());
        done += b.length;
        out[n] = b;
      } else {
        const reader = r.body.getReader();
        const parts = [];
        let len = 0;
        for (;;) {
          const { done: end, value } = await reader.read();
          if (end) break;
          parts.push(value);
          len += value.length;
          done += value.length;
          progress(Math.min(1, done / total));
        }
        const b = new Uint8Array(len);
        let o = 0;
        for (const p of parts) { b.set(p, o); o += p.length; }
        out[n] = b;
      }
      progress(Math.min(1, done / total));
    }
    return out;
  }

  return { sha256, sha256Js, extractExeTexts, loadStored, store, clearStored, check, missing, canonical,
           droppedFiles, fetchBundled, knownFiles, REQUIRED };
})();

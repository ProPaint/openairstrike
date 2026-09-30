// The game files of the web version (docs/web.md): fetched from the site (bundled build) or
// supplied by the player (bring-your-own build), checked against known_files.json (grouped by
// game; a file belongs to the game whose SHA-256 it has) and kept in IndexedDB under
// "<game key>/<file name>". Also reads the front-end texts out of the player's game executable,
// a port of tools/extract_exe_texts.py (keep the two in step). No game data is part of this file.
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
  // The text addresses of each known executable, chosen by its SHA-256; null = the game's
  // addresses are not mapped yet (tools/extract_exe_texts.py, TABLES).
  const EXE_TABLES = {
    '3b371bc2a72dcf18c17b5efa1b7e08b85fef73cfdd28dce00ec0aa2f2e93df1d': { game: 'as3d', table: {
      pages: [
        [1, 0x449EA4, 0x44A1F8], [2, 0x44A210, 0x44A430], [3, 0x44A448, 0x44A688], [4, 0x44A694, 0x44A850],
        [5, 0x44A870, 0x44AA94], [6, 0x44AAB4, 0x44AB3C], [7, 0x44AB5C, 0x44AD90], [8, 0x44ADA8, 0x44AE28],
        [9, 0x44AE40, 0x44AFF0], [10, 0x44AFF8, 0x44B07C]],
      congrats: [[0, 0x449D6C], [2, 0x449D80], [3, 0x449DBC], [4, 0x449DF0]],
      rankTable: 0x45650C,
      hints: [['info.hint.prev', 0x44B084], ['info.hint.next', 0x44B09C], ['info.page', 0x44B0B0]],
      pageValues: 0x449E50 } },
    'b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b': { game: 'as2', table: null },
    '86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077': { game: 'gulf', table: { listed: 'gulf' } },
  };

  // `header`: the first comment line's "<title> v<version>" (games.json).
  function extractExeTexts(data, table, header) {
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
    // Gulf Thunder block (package F2): a listed table.
    if (table.listed) return textFile(listedTexts(data, offset, u32, LISTED_TEXTS[table.listed]), header);
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
    for (const [page, body, title] of table.pages) {
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
    for (const [line, addr] of table.congrats) entries.push([`congrats.${line}`, cstr(addr)]);
    for (let i = 0; i < 7; i++) entries.push([`rank.${i}`, cstr(u32(table.rankTable + 4 * i), 32)]);
    for (const [key, addr] of table.hints) entries.push([key, cstr(addr, 64)]);
    for (let k = 0; k < 10; k++) entries.push([`info.pages.${10 - k}`, cstr(table.pageValues + 8 * k + (k === 0 ? 0 : 4), 16)]);
    const quote = (s) => '"' + s.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
    let out = `# ${header} front-end texts, read from the user's executable by\n` +
              '# tools/extract_exe_texts.py. Do not commit. Format: key = "value".\n';
    for (const [k, v] of entries) out += `${k} = ${quote(v)}\n`;
    return out;
  }

  // ---- Gulf Thunder (package F2): tables listed as [key, address, kind] ----------------------
  // A port of extract_listed in tools/extract_exe_texts.py, keep the two in step. The table is
  // tools/exe_texts/gulf.json's entries in file order (kind 0 text, 1 text_ml, 2 u32);
  // apps/tests/gulf_rules_test.cpp checks that it is the same list. `text_ml` lines are joined
  // with '^', `u32` written in decimal; an entry that is not at the start of a string or holds
  // other bytes than text is left out (the game's built-in text is used for it).
  const LISTED_TEXTS = {
    gulf: [
      ['title.start_game', 0x48CBC4, 0], ['title.options', 0x48CA7C, 0],
      ['title.controls', 0x48B6A0, 0], ['title.heli', 0x48BE00, 0],
      ['title.mission_complete', 0x48B8C8, 0], ['title.top_scores', 0x48CDA8, 0],
      ['title.enter_name', 0x48CBF4, 0], ['title.exit', 0x48B93C, 0], ['title.hint', 0x48B918, 0],
      ['title.game_over', 0x48BC90, 0], ['label.exit', 0x48B91C, 0], ['label.difficulty', 0x48CD58, 0],
      ['label.game_mode', 0x48CD64, 0], ['label.player', 0x48BE14, 0], ['difficulty.0', 0x48CD4C, 0],
      ['difficulty.1', 0x48CD44, 0], ['difficulty.2', 0x48CD3C, 0], ['difficulty.3', 0x48CD30, 0],
      ['difficulty.4', 0x48CD24, 0], ['mode.0', 0x48CD14, 0], ['mode.1', 0x48CD08, 0],
      ['button.start_game', 0x48CBC4, 0], ['button.top_scores', 0x48CBD0, 0],
      ['button.options', 0x48CA7C, 0], ['button.information', 0x48CBDC, 0],
      ['button.credits', 0x48CBE8, 0], ['button.quit', 0x48CA8C, 0], ['button.quit_wide', 0x48B8EC, 0],
      ['button.yes', 0x48B94C, 0], ['button.no', 0x48B954, 0], ['button.back', 0x48B70C, 0],
      ['button.back_wide', 0x48B70C, 0], ['button.next', 0x48B910, 0],
      ['button.next_wide', 0x48B910, 0], ['button.start', 0x48BE34, 0],
      ['button.continue', 0x48BC80, 0], ['button.accept', 0x48BE1C, 0],
      ['button.restart', 0x48B8DC, 0], ['button.resume', 0x48CA74, 0],
      ['button.choose_heli', 0x48B8F8, 0], ['button.configure_controls', 0x48CCE8, 0],
      ['button.apply', 0x48CD00, 0], ['button.ok', 0x48CC04, 0], ['stat.enemies', 0x48B85C, 0],
      ['stat.stars', 0x48B878, 0], ['stat.rank', 0x48B894, 0], ['msg.new_heli', 0x48B8A8, 0],
      ['rank.0', 0x489F64, 0], ['rank.1', 0x489F5C, 0], ['rank.2', 0x489F4C, 0],
      ['rank.3', 0x489F44, 0], ['rank.4', 0x489F34, 0], ['rank.5', 0x489F28, 0],
      ['rank.6', 0x489F20, 0], ['heli.0', 0x48BDC0, 0], ['heli.1', 0x48BDB4, 0],
      ['heli.2', 0x48BD98, 0], ['heli.3', 0x48BD88, 0], ['heli.4', 0x48BD80, 0],
      ['heli.5', 0x48BD74, 0], ['heli.speed', 0x48BDF0, 0], ['heli.armor', 0x48BDF8, 0],
      ['heli.na', 0x48BDE0, 0], ['scores.number', 0x48CD8C, 0], ['scores.name', 0x48CD90, 0],
      ['scores.score', 0x48CD98, 0], ['scores.rank', 0x48CDA0, 0], ['opt.resolution', 0x48CC60, 0],
      ['opt.refresh', 0x48CC6C, 0], ['opt.depth', 0x48CC7C, 0], ['opt.fullscreen', 0x48CC8C, 0],
      ['opt.brightness', 0x48CC98, 0], ['opt.sfx', 0x48CCA4, 0], ['opt.music', 0x48CCB4, 0],
      ['opt.sound3d', 0x48CCC4, 0], ['opt.camera', 0x48CCD0, 0], ['opt.mouse', 0x48CCD8, 0],
      ['opt.refresh.default', 0x48CC50, 0], ['opt.off', 0x48CC4C, 0], ['opt.on', 0x48CC48, 0],
      ['opt.depth.0', 0x48CC34, 0], ['opt.depth.16', 0x48CC14, 0], ['opt.depth.32', 0x48CC0C, 0],
      ['camera.0', 0x48CC3C, 0], ['camera.1', 0x48CC34, 0], ['camera.2', 0x48CC28, 0],
      ['camera.3', 0x48CC1C, 0], ['ctl.set', 0x48B6FC, 0], ['ctl.player.1', 0x48B4A4, 0],
      ['ctl.player.2', 0x48B498, 0], ['ctl.row.0', 0x48B664, 0], ['ctl.row.1', 0x48B654, 0],
      ['ctl.row.2', 0x48B650, 0], ['ctl.row.3', 0x48B640, 0], ['ctl.row.4', 0x48B630, 0],
      ['ctl.row.5', 0x48B650, 0], ['ctl.row.6', 0x48B624, 0], ['ctl.row.7', 0x48B618, 0],
      ['ctl.row.8', 0x48B650, 0], ['ctl.row.9', 0x48B608, 0], ['info.page', 0x48CA6C, 0],
      ['info.hint.prev', 0x48CA40, 0], ['info.hint.next', 0x48CA58, 0], ['info.pages.1', 0x48BE80, 0],
      ['info.pages.2', 0x48BE78, 0], ['info.pages.3', 0x48BE78, 0], ['info.pages.4', 0x48BE70, 0],
      ['info.pages.5', 0x48BE68, 0], ['info.pages.6', 0x48BE60, 0], ['info.pages.7', 0x48BE58, 0],
      ['info.pages.8', 0x48BE50, 0], ['info.1.title', 0x48C0D0, 0], ['info.1.0', 0x48BE88, 0],
      ['info.1.1', 0x48BED0, 0], ['info.1.2', 0x48BF10, 0], ['info.1.3', 0x48BF50, 0],
      ['info.1.5', 0x48BF60, 0], ['info.1.6', 0x48BFA0, 0], ['info.1.8', 0x48BFE8, 0],
      ['info.1.9', 0x48C030, 0], ['info.1.10', 0x48C078, 0], ['info.1.11', 0x48C0BC, 0],
      ['info.2.title', 0x48C2C0, 0], ['info.2.0', 0x48C0DC, 0], ['info.2.1', 0x48C0EC, 0],
      ['info.2.2', 0x48C128, 0], ['info.2.3', 0x48C164, 0], ['info.2.5', 0x48C180, 0],
      ['info.2.6', 0x48C190, 0], ['info.2.7', 0x48C1CC, 0], ['info.2.9', 0x48C1E0, 0],
      ['info.2.10', 0x48C1F0, 0], ['info.2.11', 0x48C224, 0], ['info.2.13', 0x48C260, 0],
      ['info.2.14', 0x48C270, 0], ['info.2.15', 0x48C2A8, 0], ['info.3.title', 0x48C48C, 0],
      ['info.3.0', 0x48C2E0, 0], ['info.3.1', 0x48C2EC, 0], ['info.3.2', 0x48C32C, 0],
      ['info.3.3', 0x48C368, 0], ['info.3.4', 0x48C3A0, 0], ['info.3.6', 0x48C3C0, 0],
      ['info.3.7', 0x48C3D0, 0], ['info.3.8', 0x48C40C, 0], ['info.3.10', 0x48C43C, 0],
      ['info.3.11', 0x48C448, 0], ['info.3.12', 0x48C478, 0], ['info.4.0', 0x48C4AC, 0],
      ['info.5.title', 0x48C6E4, 0], ['info.5.0', 0x48C4AC, 0], ['info.5.1', 0x48C4C0, 0],
      ['info.5.2', 0x48C4FC, 0], ['info.5.4', 0x48C53C, 0], ['info.5.5', 0x48C54C, 0],
      ['info.5.7', 0x48C584, 0], ['info.5.8', 0x48C5A4, 0], ['info.5.9', 0x48C5E4, 0],
      ['info.5.10', 0x48C620, 0], ['info.5.12', 0x48C628, 0], ['info.5.13', 0x48C644, 0],
      ['info.5.14', 0x48C680, 0], ['info.5.15', 0x48C6B8, 0], ['info.6.title', 0x48C77C, 0],
      ['info.6.0', 0x48C6FC, 0], ['info.6.1', 0x48C710, 0], ['info.6.2', 0x48C748, 0],
      ['info.7.title', 0x48C940, 0], ['info.7.0', 0x48C794, 0], ['info.7.1', 0x48C7A4, 0],
      ['info.7.2', 0x48C7E0, 0], ['info.7.4', 0x48C80C, 0], ['info.7.5', 0x48C81C, 0],
      ['info.7.7', 0x48C848, 0], ['info.7.8', 0x48C858, 0], ['info.7.10', 0x48C894, 0],
      ['info.7.11', 0x48C8A8, 0], ['info.7.12', 0x48C8E0, 0], ['info.7.13', 0x48C918, 0],
      ['info.8.title', 0x48CA2C, 0], ['info.8.0', 0x48C954, 0], ['info.8.1', 0x48C968, 0],
      ['info.8.2', 0x48C99C, 0], ['info.8.4', 0x48C9CC, 0], ['info.8.5', 0x48C9DC, 0],
      ['info.8.6', 0x48CA08, 0], ['credits.0', 0x48B718, 0], ['credits.1', 0x48B728, 0],
      ['credits.3', 0x48B728, 0], ['credits.4', 0x48B734, 0], ['credits.6', 0x48B748, 0],
      ['credits.7', 0x48B758, 0], ['credits.8', 0x48B768, 0], ['credits.10', 0x48B784, 0],
      ['credits.11', 0x48B794, 0], ['credits.13', 0x48B7A8, 0], ['credits.14', 0x48B7BC, 0],
      ['credits.16', 0x48B7CC, 0], ['credits.17', 0x48B7DC, 0], ['credits.19', 0x48B7F8, 0],
      ['credits.20', 0x48B804, 0], ['congrats.0', 0x48BA68, 0], ['congrats.2', 0x48BA7C, 0],
      ['congrats.3', 0x48BAB8, 0], ['congrats.4', 0x48BAF4, 0], ['congrats.6', 0x48BB28, 0],
      ['congrats.7', 0x48BB6C, 0], ['congrats.8', 0x48BBA8, 0], ['congrats.10', 0x48BBC0, 0],
      ['loading.label', 0x4892EC, 0], ['cheat.god_on', 0x488FF8, 0], ['cheat.god_off', 0x48900C, 0],
      ['cheat.lives', 0x489034, 0], ['cheat.weapons', 0x48905C, 0], ['cheat.missiles', 0x489084, 0],
      ['cheat.powerups', 0x4890B0, 0], ['dialog.10.start.0', 0x48B3E0, 1],
      ['dialog.10.start.0.speaker', 0x49B324, 2], ['dialog.10.start.1', 0x48B340, 1],
      ['dialog.10.start.1.speaker', 0x49B32C, 2], ['dialog.10.start.2', 0x48B2B8, 1],
      ['dialog.10.start.2.speaker', 0x49B334, 2], ['dialog.10.end.0', 0x48B250, 1],
      ['dialog.10.end.0.speaker', 0x49B344, 2], ['dialog.10.end.1', 0x48B228, 1],
      ['dialog.10.end.1.speaker', 0x49B34C, 2], ['dialog.24.start.0', 0x48B160, 1],
      ['dialog.24.start.0.speaker', 0x49B35C, 2], ['dialog.24.start.1', 0x48B0E8, 1],
      ['dialog.24.start.1.speaker', 0x49B364, 2],
    ],
  };

  function listedTexts(data, offset, u32, list) {
    const entries = [];
    for (const [key, addr, kind] of list) {
      try {
        if (kind === 2) { entries.push([key, String(u32(addr))]); continue; }
        const off = offset(addr);
        if (off === 0 || data[off - 1] !== 0) throw new Error('not the start of a string');
        let end = -1;
        for (let i = off; i <= Math.min(off + 4096, data.length - 1); i++) if (data[i] === 0) { end = i; break; }
        if (end < 0) throw new Error('no string terminator');
        let s = '';
        for (let i = off; i < end; i++) {
          const b = data[i];
          if (kind === 1 && b === 0x0A) { s += '^'; continue; }
          if (b < 0x20 || b > 0x7E) throw new Error('non-text bytes');
          s += String.fromCharCode(b);
        }
        entries.push([key, s]);
      } catch (e) {
        console.warn(`extract texts: ${key} left out: ${e.message}`);
      }
    }
    if (!entries.length) throw new Error('no text read from the listed addresses');
    return entries;
  }

  function textFile(entries, header) {
    const quote = (s) => '"' + s.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';
    let out = `# ${header} front-end texts, read from the user's executable by\n` +
              '# tools/extract_exe_texts.py. Do not commit. Format: key = "value".\n';
    for (const [k, v] of entries) out += `${k} = ${quote(v)}\n`;
    return out;
  }
  // ---- end of the Gulf Thunder block ---------------------------------------------------------

  // The texts of a game's executable: { game, text } for a known executable with a table,
  // { game, text: null, notMapped: message } for a known one without (the sequels for now);
  // throws for any other file. `bytes`: the executable, `sha`: its SHA-256, `known`: the
  // games of known_files.json.
  function textsFromExe(bytes, sha, known) {
    const e = EXE_TABLES[sha];
    if (!e) throw new Error('not the executable of a known game');
    const g = known[e.game];
    if (!e.table) return { game: e.game, text: null, notMapped: `texts of ${g.title} are not mapped yet` };
    return { game: e.game, text: extractExeTexts(bytes, e.table, `${g.title} v${g.version}`) };
  }

  // ---------------------------------------------------------------------------------------
  // IndexedDB: one store, "<game key>/<file name>" -> Blob. Files stored by the first version
  // of the page have no game key (they were AirStrike 3D's): loadStored renames them.
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
  // { gameKey: { name: Blob } }
  async function loadStored() {
    const raw = {};
    const db = await openDb();
    try {
      await new Promise((ok, fail) => {
        const t = db.transaction(STORE, 'readonly');
        const c = t.objectStore(STORE).openCursor();
        c.onsuccess = () => {
          const cur = c.result;
          if (!cur) return;
          raw[cur.key] = cur.value;
          cur.continue();
        };
        t.oncomplete = ok;
        t.onerror = () => fail(t.error);
      });
    } finally {
      db.close();
    }
    const legacy = Object.keys(raw).filter((k) => !k.includes('/'));
    if (legacy.length) {
      await tx('readwrite', (s) => {
        for (const k of legacy) {
          s.put(raw[k], 'as3d/' + k);
          s.delete(k);
        }
      });
      for (const k of legacy) {
        raw['as3d/' + k] = raw[k];
        delete raw[k];
      }
    }
    const out = {};
    for (const [k, v] of Object.entries(raw)) {
      const i = k.indexOf('/');
      (out[k.slice(0, i)] = out[k.slice(0, i)] || {})[k.slice(i + 1)] = v;
    }
    return out;
  }
  // `games`: { gameKey: { name: Blob } }
  const store = (games) => tx('readwrite', (s) => {
    for (const [g, files] of Object.entries(games)) for (const [k, v] of Object.entries(files)) s.put(v, g + '/' + k);
  });
  const clearStored = () => tx('readwrite', (s) => s.clear());

  // ---------------------------------------------------------------------------------------
  // Checking what the player gave.
  // ---------------------------------------------------------------------------------------
  let known = null; // known_files.json's games, plus the lookups built from them
  let byName = null, byHash = null;
  async function knownFiles() {
    if (!known) {
      const r = await fetch('known_files.json?v=' + ((window.AS3D_BUILD || {}).stamp || ''));
      const k = (await r.json()).games;
      // A file name (lower case) -> what the games call it; a SHA-256 -> [{ game, name }].
      byName = {};
      byHash = {};
      for (const [g, def] of Object.entries(k)) {
        for (const [name, f] of Object.entries(def.files)) {
          (byName[name.toLowerCase()] = byName[name.toLowerCase()] || { name, games: [] }).games.push(g);
          if (f.sha256) (byHash[f.sha256] = byHash[f.sha256] || []).push({ game: g, name });
        }
      }
      known = k;
    }
    return known;
  }
  // The names of a game's required files (its paks).
  function requiredOf(game) {
    return Object.entries(known[game].files).filter(([, f]) => f.required).map(([n]) => n);
  }

  // Canonical name of a file of any known game from any path and case; null for other files.
  // knownFiles() must have been awaited.
  function canonical(path) {
    const base = path.split(/[\\/]/).pop().toLowerCase();
    return byName && byName[base] ? byName[base].name : null;
  }

  // Checks the given File objects. Returns { games: {key: {name: Blob}}, notes: [{name, ok,
  // text, game}] }: the files to keep, grouped by the game each belongs to (by SHA-256; the
  // exe becomes the game's texts file), and a line per file looked at.
  async function check(fileList, progress) {
    const kn = await knownFiles();
    const games = {}, notes = [];
    const keep = (g, name, blob) => { (games[g] = games[g] || {})[name] = blob; };
    const titles = Object.values(kn).map((g) => g.title).join(', ');
    const list = Array.from(fileList).filter((f) => canonical(f.webkitRelativePath || f.name));
    let i = 0;
    for (const f of list) {
      const name = canonical(f.webkitRelativePath || f.name);
      if (progress) progress(name, i++ / Math.max(1, list.length));
      const texts = Object.keys(kn).filter((g) => kn[g].texts === name);
      if (texts.length) {
        const g = texts[0];
        const t = new TextDecoder().decode(new Uint8Array(await f.arrayBuffer()));
        if (!/^info\.1\.title = "/m.test(t)) {
          notes.push({ name, game: g, ok: false, text: `${name}: not the file tools/extract_exe_texts.py writes` });
          continue;
        }
        if (!(games[g] && games[g][name])) keep(g, name, new Blob([t], { type: 'text/plain' }));
        notes.push({ name, game: g, ok: true, text: `${name}: OK` });
        continue;
      }
      // Size first: no need to hash a file no game has in that size.
      const cands = byName[name.toLowerCase()].games.filter((g) => {
        const k = kn[g].files[name];
        return !k.size || k.size === f.size;
      });
      if (!cands.length) {
        notes.push({ name, ok: false, text: `${name}: ${f.size} bytes, not the file of any game this page knows (${titles})` });
        continue;
      }
      const bytes = new Uint8Array(await f.arrayBuffer());
      const h = await sha256(bytes);
      const hits = (byHash[h] || []).filter((x) => x.name === name);
      if (!hits.length) {
        notes.push({ name, ok: false, text: `${name}: the contents differ from the file of every game this page knows (${titles})` });
        continue;
      }
      const isExe = hits.some((x) => kn[x.game].exe === name);
      if (isExe) {
        const g = hits[0].game, title = kn[g].title;
        try {
          const r = textsFromExe(bytes, h, kn);
          if (r.text === null) {
            notes.push({ name, game: g, ok: true, text: `${name}: the executable of ${title}; ${r.notMapped} (the executable itself is not kept)` });
          } else {
            keep(g, kn[g].texts, new Blob([r.text], { type: 'text/plain' }));
            notes.push({ name, game: g, ok: true, text: `${name}: the executable of ${title}, texts read (the executable itself is not kept)` });
          }
        } catch (e) {
          notes.push({ name, game: g, ok: false, text: `${name}: ${e.message}` });
        }
        continue;
      }
      const blob = new Blob([bytes]);
      for (const x of hits) keep(x.game, name, blob);
      const of = hits.map((x) => kn[x.game].title).join(' and ');
      notes.push({ name, game: hits[0].game, ok: true, text: `${name}: OK, ${of}` });
    }
    if (progress) progress('', 1);
    return { games, notes };
  }

  // The required files of `game` that `files` ({name: Blob}) lacks.
  function missing(files, game) { return requiredOf(game).filter((n) => !(files && files[n])); }

  // Every File in a drop, folders included (Chromium, Firefox, Safari).
  async function droppedFiles(dt) {
    await knownFiles();
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

  // The bundled build: the files in data/<game>/ of the site, with a progress callback (0..1).
  async function fetchBundled(game, progress) {
    const kn = (await knownFiles())[game];
    if (!kn) throw new Error(`unknown game '${game}'`);
    const dir = `data/${game}/`;
    let names = Object.keys(kn.files).filter((n) => n !== kn.exe);
    try {
      const r = await fetch(dir + 'index.txt', { cache: 'no-cache' });
      if (r.ok) names = (await r.text()).split(/\s+/).filter((n) => n && n !== 'index.txt');
    } catch (e) { /* the default list */ }
    const total = names.reduce((s, n) => s + ((kn.files[n] && kn.files[n].size) || 10000), 0);
    let done = 0;
    const out = {};
    for (const n of names) {
      const r = await fetch(dir + n);
      if (!r.ok) {
        if (requiredOf(game).includes(n)) throw new Error(`cannot load ${dir}${n} (HTTP ${r.status})`);
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

  return { sha256, sha256Js, extractExeTexts, textsFromExe, loadStored, store, clearStored, check, missing, canonical,
           droppedFiles, fetchBundled, knownFiles };
})();

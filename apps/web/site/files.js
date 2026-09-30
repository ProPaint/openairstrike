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
    // AirStrike 2 v2.51: the address list tools/exe_texts/as2.json (kinds t text, m text_ml,
    // u u32), with the control-row names corrected (tools/extract_exe_texts.py, issue as2/300).
    'b24b62b2c5b61cfa1cf0aad781788aa777a2e4f4a385c73ba53014b039e46f5b': { game: 'as2', table: { listed: [
      // BEGIN as2 address list (generated from tools/exe_texts/as2.json)
      ['title.start_game', 0x48EE84, 't'], ['title.options', 0x48EBC4, 't'], ['title.controls', 0x48D728, 't'],
      ['title.heli', 0x48DE98, 't'], ['title.mission_complete', 0x48D934, 't'], ['title.top_scores', 0x48EEDC, 't'],
      ['title.enter_name', 0x48ED24, 't'], ['title.exit', 0x48D9A0, 't'], ['title.hint', 0x48EEB0, 't'],
      ['title.game_over', 0x48DD34, 't'], ['label.exit', 0x48D980, 't'], ['label.difficulty', 0x48EE90, 't'],
      ['label.game_mode', 0x48EE9C, 't'], ['label.player', 0x48DEAC, 't'], ['difficulty.0', 0x48EE78, 't'],
      ['difficulty.1', 0x48EE70, 't'], ['difficulty.2', 0x48EE68, 't'], ['difficulty.3', 0x48EE60, 't'],
      ['difficulty.4', 0x48EE54, 't'], ['mode.0', 0x48EE44, 't'], ['mode.1', 0x48EE38, 't'],
      ['button.start_game', 0x48ECE8, 't'], ['button.top_scores', 0x48ECF8, 't'], ['button.options', 0x48EBC4, 't'],
      ['button.information', 0x48ED08, 't'], ['button.credits', 0x48ED18, 't'], ['button.quit', 0x48EBD0, 't'],
      ['button.quit_wide', 0x48D954, 't'], ['button.yes', 0x48D9B0, 't'], ['button.no', 0x48D9B8, 't'],
      ['button.back', 0x48D794, 't'], ['button.back_wide', 0x48D8B0, 't'], ['button.next', 0x48EEA8, 't'],
      ['button.next_wide', 0x48D974, 't'], ['button.start', 0x48DEC0, 't'], ['button.continue', 0x48DD28, 't'],
      ['button.accept', 0x48DEB4, 't'], ['button.restart', 0x48D948, 't'], ['button.resume', 0x48EBB8, 't'],
      ['button.choose_heli', 0x48D960, 't'], ['button.configure_controls', 0x48EE18, 't'],
      ['button.apply', 0x48EE30, 't'], ['button.ok', 0x48ED34, 't'], ['stat.enemies', 0x48D8C8, 't'],
      ['stat.stars', 0x48D8E4, 't'], ['stat.rank', 0x48D900, 't'], ['msg.new_heli', 0x48D914, 't'],
      ['rank.0', 0x48B674, 't'], ['rank.1', 0x48B66C, 't'], ['rank.2', 0x48B65C, 't'], ['rank.3', 0x48B654, 't'],
      ['rank.4', 0x48B644, 't'], ['rank.5', 0x48B638, 't'], ['rank.6', 0x48B630, 't'], ['heli.0', 0x48DE58, 't'],
      ['heli.1', 0x48DE4C, 't'], ['heli.2', 0x48DE3C, 't'], ['heli.3', 0x48DE2C, 't'], ['heli.4', 0x48DE24, 't'],
      ['heli.5', 0x48DE18, 't'], ['heli.speed', 0x48DE88, 't'], ['heli.armor', 0x48DE90, 't'],
      ['heli.na', 0x48DE78, 't'], ['scores.number', 0x48EEC0, 't'], ['scores.name', 0x48EEC4, 't'],
      ['scores.score', 0x48EECC, 't'], ['scores.rank', 0x48EED4, 't'], ['opt.resolution', 0x48ED90, 't'],
      ['opt.refresh', 0x48ED9C, 't'], ['opt.depth', 0x48EDAC, 't'], ['opt.fullscreen', 0x48EDBC, 't'],
      ['opt.brightness', 0x48EDC8, 't'], ['opt.sfx', 0x48EDD4, 't'], ['opt.music', 0x48EDE4, 't'],
      ['opt.sound3d', 0x48EDF4, 't'], ['opt.camera', 0x48EE00, 't'], ['opt.mouse', 0x48EE08, 't'],
      ['opt.refresh.default', 0x48ED80, 't'], ['opt.off', 0x48ED7C, 't'], ['opt.on', 0x48ED78, 't'],
      ['opt.depth.0', 0x48ED64, 't'], ['opt.depth.16', 0x48ED44, 't'], ['opt.depth.32', 0x48ED3C, 't'],
      ['camera.0', 0x48ED6C, 't'], ['camera.1', 0x48ED64, 't'], ['camera.2', 0x48ED58, 't'],
      ['camera.3', 0x48ED4C, 't'], ['ctl.set', 0x48D784, 't'], ['ctl.player.1', 0x48D52C, 't'],
      ['ctl.player.2', 0x48D520, 't'], ['ctl.row.0', 0x48D6EC, 't'], ['ctl.row.1', 0x48D6DC, 't'],
      ['ctl.row.2', 0x48D6D8, 't'], ['ctl.row.3', 0x48D6C8, 't'], ['ctl.row.4', 0x48D6B8, 't'],
      ['ctl.row.5', 0x48D6D8, 't'], ['ctl.row.6', 0x48D6AC, 't'], ['ctl.row.7', 0x48D6A0, 't'],
      ['ctl.row.8', 0x48D6D8, 't'], ['ctl.row.9', 0x48D690, 't'], ['info.page', 0x48EBB0, 't'],
      ['info.hint.prev', 0x48EB84, 't'], ['info.hint.next', 0x48EB9C, 't'], ['info.pages.1', 0x48DF10, 't'],
      ['info.pages.2', 0x48DF08, 't'], ['info.pages.3', 0x48DF00, 't'], ['info.pages.4', 0x48DEF8, 't'],
      ['info.pages.5', 0x48DEF0, 't'], ['info.pages.6', 0x48DEE8, 't'], ['info.pages.7', 0x48DEE0, 't'],
      ['info.pages.8', 0x48DED8, 't'], ['info.1.title', 0x48E150, 't'], ['info.1.0', 0x48DF18, 't'],
      ['info.1.1', 0x48DF60, 't'], ['info.1.2', 0x48DFA0, 't'], ['info.1.3', 0x48DFE0, 't'],
      ['info.1.5', 0x48DFF0, 't'], ['info.1.6', 0x48E020, 't'], ['info.1.8', 0x48E068, 't'],
      ['info.1.9', 0x48E0B0, 't'], ['info.1.10', 0x48E0F8, 't'], ['info.1.11', 0x48E13C, 't'],
      ['info.2.title', 0x48E340, 't'], ['info.2.0', 0x48E15C, 't'], ['info.2.1', 0x48E16C, 't'],
      ['info.2.2', 0x48E1A8, 't'], ['info.2.3', 0x48E1E4, 't'], ['info.2.5', 0x48E200, 't'],
      ['info.2.6', 0x48E210, 't'], ['info.2.7', 0x48E24C, 't'], ['info.2.9', 0x48E260, 't'],
      ['info.2.10', 0x48E270, 't'], ['info.2.11', 0x48E2A4, 't'], ['info.2.13', 0x48E2E0, 't'],
      ['info.2.14', 0x48E2F0, 't'], ['info.2.15', 0x48E328, 't'], ['info.3.title', 0x48E50C, 't'],
      ['info.3.0', 0x48E360, 't'], ['info.3.1', 0x48E36C, 't'], ['info.3.2', 0x48E3AC, 't'],
      ['info.3.3', 0x48E3E8, 't'], ['info.3.4', 0x48E420, 't'], ['info.3.6', 0x48E440, 't'],
      ['info.3.7', 0x48E450, 't'], ['info.3.8', 0x48E48C, 't'], ['info.3.10', 0x48E4BC, 't'],
      ['info.3.11', 0x48E4C8, 't'], ['info.3.12', 0x48E4F8, 't'], ['info.4.title', 0x48E5D0, 't'],
      ['info.4.0', 0x48E52C, 't'], ['info.4.1', 0x48E53C, 't'], ['info.4.2', 0x48E578, 't'],
      ['info.4.4', 0x48E5AC, 't'], ['info.4.5', 0x48E5BC, 't'], ['info.5.title', 0x48E828, 't'],
      ['info.5.0', 0x48E5F0, 't'], ['info.5.1', 0x48E604, 't'], ['info.5.2', 0x48E640, 't'],
      ['info.5.4', 0x48E680, 't'], ['info.5.5', 0x48E690, 't'], ['info.5.7', 0x48E6C8, 't'],
      ['info.5.8', 0x48E6E8, 't'], ['info.5.9', 0x48E728, 't'], ['info.5.10', 0x48E764, 't'],
      ['info.5.12', 0x48E76C, 't'], ['info.5.13', 0x48E788, 't'], ['info.5.14', 0x48E7C4, 't'],
      ['info.5.15', 0x48E7FC, 't'], ['info.6.title', 0x48E8C0, 't'], ['info.6.0', 0x48E840, 't'],
      ['info.6.1', 0x48E854, 't'], ['info.6.2', 0x48E88C, 't'], ['info.7.title', 0x48EA84, 't'],
      ['info.7.0', 0x48E8D8, 't'], ['info.7.1', 0x48E8E8, 't'], ['info.7.2', 0x48E924, 't'],
      ['info.7.4', 0x48E950, 't'], ['info.7.5', 0x48E960, 't'], ['info.7.7', 0x48E98C, 't'],
      ['info.7.8', 0x48E99C, 't'], ['info.7.10', 0x48E9D8, 't'], ['info.7.11', 0x48E9EC, 't'],
      ['info.7.12', 0x48EA24, 't'], ['info.7.13', 0x48EA5C, 't'], ['info.8.title', 0x48EB70, 't'],
      ['info.8.0', 0x48EA98, 't'], ['info.8.1', 0x48EAAC, 't'], ['info.8.2', 0x48EAE0, 't'],
      ['info.8.4', 0x48EB10, 't'], ['info.8.5', 0x48EB20, 't'], ['info.8.6', 0x48EB4C, 't'],
      ['credits.0', 0x48D79C, 't'], ['credits.1', 0x48D7AC, 't'], ['credits.3', 0x48D7C0, 't'],
      ['credits.4', 0x48D7CC, 't'], ['credits.6', 0x48D7DC, 't'], ['credits.7', 0x48D7F8, 't'],
      ['credits.8', 0x48D808, 't'], ['credits.10', 0x48D81C, 't'], ['credits.11', 0x48D830, 't'],
      ['credits.13', 0x48D840, 't'], ['credits.14', 0x48D850, 't'], ['credits.16', 0x48D864, 't'],
      ['credits.17', 0x48D86C, 't'], ['credits.19', 0x48D888, 't'], ['credits.20', 0x48D898, 't'],
      ['congrats.0', 0x48DB10, 't'], ['congrats.2', 0x48DB24, 't'], ['congrats.3', 0x48DB60, 't'],
      ['congrats.4', 0x48DB9C, 't'], ['congrats.6', 0x48DBD0, 't'], ['congrats.7', 0x48DC14, 't'],
      ['congrats.8', 0x48DC50, 't'], ['congrats.10', 0x48DC68, 't'], ['loading.label', 0x48A424, 't'],
      ['cheat.god_on', 0x48A018, 't'], ['cheat.god_off', 0x48A02C, 't'], ['cheat.lives', 0x48A054, 't'],
      ['cheat.weapons', 0x48A07C, 't'], ['cheat.missiles', 0x48A0A4, 't'], ['cheat.powerups', 0x48A0D0, 't'],
      ['dialog.1.start.0', 0x48D460, 'm'], ['dialog.1.start.0.speaker', 0x49D32C, 'u'],
      ['dialog.1.start.1', 0x48D41C, 'm'], ['dialog.1.start.1.speaker', 0x49D334, 'u'],
      ['dialog.1.end.0', 0x48D3EC, 'm'], ['dialog.1.end.0.speaker', 0x49D344, 'u'],
      ['dialog.2.start.0', 0x48D370, 'm'], ['dialog.2.start.0.speaker', 0x49D354, 'u'],
      ['dialog.2.start.1', 0x48D360, 't'], ['dialog.2.start.1.speaker', 0x49D35C, 'u'],
      ['dialog.2.end.0', 0x48D2E0, 'm'], ['dialog.2.end.0.speaker', 0x49D36C, 'u'],
      ['dialog.3.start.0', 0x48D258, 'm'], ['dialog.3.start.0.speaker', 0x49D37C, 'u'],
      ['dialog.3.start.1', 0x48D1E8, 'm'], ['dialog.3.start.1.speaker', 0x49D384, 'u'],
      ['dialog.3.start.2', 0x48D168, 'm'], ['dialog.3.start.2.speaker', 0x49D38C, 'u'],
      ['dialog.3.end.0', 0x48D148, 't'], ['dialog.3.end.0.speaker', 0x49D39C, 'u'],
      ['dialog.4.end.0', 0x48D13C, 't'], ['dialog.4.end.0.speaker', 0x49D3AC, 'u'],
      ['dialog.5.start.0', 0x48D0A8, 'm'], ['dialog.5.start.0.speaker', 0x49D3BC, 'u'],
      ['dialog.5.end.0', 0x48D058, 'm'], ['dialog.5.end.0.speaker', 0x49D3CC, 'u'],
      ['dialog.6.start.0', 0x48CFD8, 'm'], ['dialog.6.start.0.speaker', 0x49D3DC, 'u'],
      ['dialog.6.start.1', 0x48CF70, 'm'], ['dialog.6.start.1.speaker', 0x49D3E4, 'u'],
      ['dialog.6.end.0', 0x48CF30, 'm'], ['dialog.6.end.0.speaker', 0x49D3F4, 'u'],
      ['dialog.6.end.1', 0x48CF0C, 'm'], ['dialog.6.end.1.speaker', 0x49D3FC, 'u'],
      ['dialog.8.start.0', 0x48CE90, 'm'], ['dialog.8.start.0.speaker', 0x49D40C, 'u'],
      ['dialog.8.start.1', 0x48CDE8, 'm'], ['dialog.8.start.1.speaker', 0x49D414, 'u'],
      ['dialog.8.start.2', 0x48CDB0, 'm'], ['dialog.8.start.2.speaker', 0x49D41C, 'u'],
      ['dialog.9.end.0', 0x48CD98, 't'], ['dialog.9.end.0.speaker', 0x49D42C, 'u'],
      ['dialog.10.end.0', 0x48CD70, 't'], ['dialog.10.end.0.speaker', 0x49D43C, 'u'],
      ['dialog.10.end.1', 0x48CD20, 'm'], ['dialog.10.end.1.speaker', 0x49D444, 'u'],
      ['dialog.11.start.0', 0x48CC78, 'm'], ['dialog.11.start.0.speaker', 0x49D454, 'u'],
      ['dialog.11.start.1', 0x48CC58, 'm'], ['dialog.11.start.1.speaker', 0x49D45C, 'u'],
      ['dialog.11.end.0', 0x48CC00, 'm'], ['dialog.11.end.0.speaker', 0x49D46C, 'u'],
      ['dialog.12.start.0', 0x48CB58, 'm'], ['dialog.12.start.0.speaker', 0x49D47C, 'u'],
      ['dialog.12.end.0', 0x48CB38, 't'], ['dialog.12.end.0.speaker', 0x49D48C, 'u'],
      ['dialog.12.end.1', 0x48CB0C, 't'], ['dialog.12.end.1.speaker', 0x49D494, 'u'],
      ['dialog.14.start.0', 0x48CAC8, 'm'], ['dialog.14.start.0.speaker', 0x49D4A4, 'u'],
      ['dialog.14.start.1', 0x48CAA0, 't'], ['dialog.14.start.1.speaker', 0x49D4AC, 'u'],
      ['dialog.15.start.0', 0x48CA50, 'm'], ['dialog.15.start.0.speaker', 0x49D4BC, 'u'],
      ['dialog.15.start.1', 0x48CA44, 't'], ['dialog.15.start.1.speaker', 0x49D4C4, 'u'],
      ['dialog.15.end.0', 0x48C9D0, 'm'], ['dialog.15.end.0.speaker', 0x49D4D4, 'u'],
      ['dialog.16.end.0', 0x48C9A8, 'm'], ['dialog.16.end.0.speaker', 0x49D4E4, 'u'],
      ['dialog.17.start.0', 0x48C960, 'm'], ['dialog.17.start.0.speaker', 0x49D4F4, 'u'],
      ['dialog.17.start.1', 0x48C91C, 'm'], ['dialog.17.start.1.speaker', 0x49D4FC, 'u'],
      ['dialog.18.start.0', 0x48C890, 'm'], ['dialog.18.start.0.speaker', 0x49D50C, 'u'],
      ['dialog.18.start.1', 0x48C830, 'm'], ['dialog.18.start.1.speaker', 0x49D514, 'u'],
      ['dialog.18.start.2', 0x48C7F4, 'm'], ['dialog.18.start.2.speaker', 0x49D51C, 'u'],
      // END as2 address list
    ], override: [0x48D6EC, 0x48D6DC, 0x48D6C8, 0x48D6B8, 0x48D6AC, 0x48D6A0, 0x48D690, 0x48D680, 0x48D674, 0x48D668]
      .map((a, i) => [`ctl.row.${i}`, a]) } },
    // Gulf Thunder v2.71: the address list tools/exe_texts/gulf.json (corrected, issues gulf/402
    // and 410), as tools/extract_exe_texts.py reads it.
    '86195a9653489064844c172ce43307c703a50e53be7e00d45fe346c45d5ae077': { game: 'gulf', table: { listed: [
      // BEGIN gulf address list (generated from tools/exe_texts/gulf.json)
      ['title.start_game', 0x48CBC4, 't'], ['title.options', 0x48CA7C, 't'], ['title.controls', 0x48B6A0, 't'],
      ['title.heli', 0x48BE00, 't'], ['title.mission_complete', 0x48B8C8, 't'], ['title.top_scores', 0x48CBD0, 't'],
      ['title.enter_name', 0x48CBF0, 't'], ['title.exit', 0x48B93C, 't'], ['title.hint', 0x48CD70, 't'],
      ['title.game_over', 0x48BC90, 't'], ['label.exit', 0x48B91C, 't'], ['label.difficulty', 0x48CD58, 't'],
      ['label.game_mode', 0x48CD64, 't'], ['label.player', 0x48BE14, 't'], ['difficulty.0', 0x48CD4C, 't'],
      ['difficulty.1', 0x48CD44, 't'], ['difficulty.2', 0x48CD3C, 't'], ['difficulty.3', 0x48CD34, 't'],
      ['difficulty.4', 0x48CD28, 't'], ['mode.0', 0x48CD18, 't'], ['mode.1', 0x48CD0C, 't'],
      ['button.start_game', 0x48CBC4, 't'], ['button.top_scores', 0x48CBD0, 't'], ['button.options', 0x48CA7C, 't'],
      ['button.information', 0x48CBDC, 't'], ['button.credits', 0x48CBE8, 't'], ['button.quit', 0x48CA8C, 't'],
      ['button.quit_wide', 0x48B8EC, 't'], ['button.yes', 0x48B94C, 't'], ['button.no', 0x48B954, 't'],
      ['button.back', 0x48B70C, 't'], ['button.back_wide', 0x48B70C, 't'], ['button.next', 0x48B910, 't'],
      ['button.next_wide', 0x48B910, 't'], ['button.start', 0x48BE34, 't'], ['button.continue', 0x48BC80, 't'],
      ['button.continue.heli', 0x48BE28, 't'], ['button.accept', 0x48BE1C, 't'], ['button.restart', 0x48B8DC, 't'],
      ['button.restart.gameover', 0x48BD74, 't'], ['button.resume', 0x48CA74, 't'],
      ['button.choose_heli', 0x48B8F8, 't'], ['button.configure_controls', 0x48CCE8, 't'],
      ['button.apply', 0x48CD00, 't'], ['button.ok', 0x48CC00, 't'], ['button.hint_ok', 0x48CD80, 't'],
      ['stat.enemies', 0x48B85C, 't'], ['stat.stars', 0x48B878, 't'], ['stat.rank', 0x48B894, 't'],
      ['msg.new_heli', 0x48B8A8, 't'], ['rank.0', 0x489F64, 't'], ['rank.1', 0x489F5C, 't'],
      ['rank.2', 0x489F4C, 't'], ['rank.3', 0x489F44, 't'], ['rank.4', 0x489F34, 't'], ['rank.5', 0x489F28, 't'],
      ['rank.6', 0x489F20, 't'], ['heli.0', 0x48BDC0, 't'], ['heli.1', 0x48BDB4, 't'], ['heli.2', 0x48BDA4, 't'],
      ['heli.speed', 0x48BDF0, 't'], ['heli.armor', 0x48BDF8, 't'], ['heli.na', 0x48BDE0, 't'],
      ['scores.number', 0x48CD8C, 't'], ['scores.name', 0x48CD90, 't'], ['scores.score', 0x48CD98, 't'],
      ['scores.rank', 0x48CDA0, 't'], ['opt.resolution', 0x48CC60, 't'], ['opt.refresh', 0x48CC6C, 't'],
      ['opt.depth', 0x48CC7C, 't'], ['opt.fullscreen', 0x48CC8C, 't'], ['opt.brightness', 0x48CC98, 't'],
      ['opt.sfx', 0x48CCA4, 't'], ['opt.music', 0x48CCB4, 't'], ['opt.sound3d', 0x48CCC4, 't'],
      ['opt.camera', 0x48CCD0, 't'], ['opt.mouse', 0x48CCD8, 't'], ['opt.refresh.default', 0x48CC50, 't'],
      ['opt.off', 0x48CC4C, 't'], ['opt.on', 0x48CC48, 't'], ['opt.depth.0', 0x48CC34, 't'],
      ['opt.depth.16', 0x48CC14, 't'], ['opt.depth.32', 0x48CC0C, 't'], ['camera.0', 0x48CC3C, 't'],
      ['camera.1', 0x48CC34, 't'], ['camera.2', 0x48CC28, 't'], ['camera.3', 0x48CC1C, 't'],
      ['ctl.set', 0x48B6FC, 't'], ['ctl.player.1', 0x48B4A4, 't'], ['ctl.player.2', 0x48B498, 't'],
      ['ctl.row.0', 0x48B664, 't'], ['ctl.row.1', 0x48B654, 't'], ['ctl.row.2', 0x48B640, 't'],
      ['ctl.row.3', 0x48B630, 't'], ['ctl.row.4', 0x48B624, 't'], ['ctl.row.5', 0x48B618, 't'],
      ['ctl.row.6', 0x48B608, 't'], ['ctl.row.7', 0x48B5F8, 't'], ['ctl.row.8', 0x48B5EC, 't'],
      ['ctl.row.9', 0x48B5E0, 't'], ['info.page', 0x48CA6C, 't'], ['info.hint.prev', 0x48CA40, 't'],
      ['info.hint.next', 0x48CA58, 't'], ['info.pages.1', 0x48BE80, 't'], ['info.pages.2', 0x48BE78, 't'],
      ['info.pages.3', 0x48BE70, 't'], ['info.pages.4', 0x48BE68, 't'], ['info.pages.5', 0x48BE60, 't'],
      ['info.pages.6', 0x48BE58, 't'], ['info.pages.7', 0x48BE50, 't'], ['info.1.title', 0x48C0D0, 't'],
      ['info.1.0', 0x48BE88, 't'], ['info.1.1', 0x48BED0, 't'], ['info.1.2', 0x48BF10, 't'],
      ['info.1.3', 0x48BF50, 't'], ['info.1.5', 0x48BF60, 't'], ['info.1.6', 0x48BFA0, 't'],
      ['info.1.8', 0x48BFE8, 't'], ['info.1.9', 0x48C030, 't'], ['info.1.10', 0x48C078, 't'],
      ['info.1.11', 0x48C0BC, 't'], ['info.2.title', 0x48C2C0, 't'], ['info.2.0', 0x48C0DC, 't'],
      ['info.2.1', 0x48C0EC, 't'], ['info.2.2', 0x48C128, 't'], ['info.2.3', 0x48C164, 't'],
      ['info.2.5', 0x48C180, 't'], ['info.2.6', 0x48C190, 't'], ['info.2.7', 0x48C1CC, 't'],
      ['info.2.9', 0x48C1E0, 't'], ['info.2.10', 0x48C1F0, 't'], ['info.2.11', 0x48C224, 't'],
      ['info.2.13', 0x48C260, 't'], ['info.2.14', 0x48C270, 't'], ['info.2.15', 0x48C2A8, 't'],
      ['info.3.title', 0x48C48C, 't'], ['info.3.0', 0x48C2E0, 't'], ['info.3.1', 0x48C2EC, 't'],
      ['info.3.2', 0x48C32C, 't'], ['info.3.3', 0x48C368, 't'], ['info.3.4', 0x48C3A0, 't'],
      ['info.3.6', 0x48C3C0, 't'], ['info.3.7', 0x48C3D0, 't'], ['info.3.8', 0x48C40C, 't'],
      ['info.3.10', 0x48C43C, 't'], ['info.3.11', 0x48C448, 't'], ['info.3.12', 0x48C478, 't'],
      ['info.5.title', 0x48C6E4, 't'], ['info.5.0', 0x48C4AC, 't'], ['info.5.1', 0x48C4C0, 't'],
      ['info.5.2', 0x48C4FC, 't'], ['info.5.4', 0x48C53C, 't'], ['info.5.5', 0x48C54C, 't'],
      ['info.5.7', 0x48C584, 't'], ['info.5.8', 0x48C5A4, 't'], ['info.5.9', 0x48C5E4, 't'],
      ['info.5.10', 0x48C620, 't'], ['info.5.12', 0x48C628, 't'], ['info.5.13', 0x48C644, 't'],
      ['info.5.14', 0x48C680, 't'], ['info.5.15', 0x48C6B8, 't'], ['info.6.title', 0x48C77C, 't'],
      ['info.6.0', 0x48C6FC, 't'], ['info.6.1', 0x48C710, 't'], ['info.6.2', 0x48C748, 't'],
      ['info.7.title', 0x48C940, 't'], ['info.7.0', 0x48C794, 't'], ['info.7.1', 0x48C7A4, 't'],
      ['info.7.2', 0x48C7E0, 't'], ['info.7.4', 0x48C80C, 't'], ['info.7.5', 0x48C81C, 't'],
      ['info.7.7', 0x48C848, 't'], ['info.7.8', 0x48C858, 't'], ['info.7.10', 0x48C894, 't'],
      ['info.7.11', 0x48C8A8, 't'], ['info.7.12', 0x48C8E0, 't'], ['info.7.13', 0x48C918, 't'],
      ['info.8.title', 0x48CA2C, 't'], ['info.8.0', 0x48C954, 't'], ['info.8.1', 0x48C968, 't'],
      ['info.8.2', 0x48C99C, 't'], ['info.8.4', 0x48C9CC, 't'], ['info.8.5', 0x48C9DC, 't'],
      ['info.8.6', 0x48CA08, 't'], ['credits.0', 0x48B718, 't'], ['credits.1', 0x48B728, 't'],
      ['credits.3', 0x48B73C, 't'], ['credits.4', 0x48B748, 't'], ['credits.5', 0x48B758, 't'],
      ['credits.7', 0x48B768, 't'], ['credits.8', 0x48B784, 't'], ['credits.9', 0x48B794, 't'],
      ['credits.11', 0x48B7A8, 't'], ['credits.12', 0x48B7BC, 't'], ['credits.14', 0x48B7CC, 't'],
      ['credits.15', 0x48B7DC, 't'], ['credits.16', 0x48B7F0, 't'], ['credits.18', 0x48B804, 't'],
      ['credits.19', 0x48B80C, 't'], ['credits.21', 0x48B828, 't'], ['credits.22', 0x48B838, 't'],
      ['congrats.0', 0x48BA68, 't'], ['congrats.2', 0x48BA7C, 't'], ['congrats.3', 0x48BAB8, 't'],
      ['congrats.4', 0x48BAF4, 't'], ['congrats.6', 0x48BB28, 't'], ['congrats.7', 0x48BB6C, 't'],
      ['congrats.8', 0x48BBA8, 't'], ['congrats.10', 0x48BBC0, 't'], ['loading.label', 0x4892EC, 't'],
      ['cheat.god_on', 0x488FF8, 't'], ['cheat.god_off', 0x48900C, 't'], ['cheat.lives', 0x489034, 't'],
      ['cheat.weapons', 0x48905C, 't'], ['cheat.missiles', 0x489084, 't'], ['cheat.powerups', 0x4890B0, 't'],
      ['dialog.10.start.0', 0x48B3E0, 'm'], ['dialog.10.start.0.speaker', 0x49B324, 'u'],
      ['dialog.10.start.1', 0x48B340, 'm'], ['dialog.10.start.1.speaker', 0x49B32C, 'u'],
      ['dialog.10.start.2', 0x48B2B8, 'm'], ['dialog.10.start.2.speaker', 0x49B334, 'u'],
      ['dialog.10.end.0', 0x48B250, 'm'], ['dialog.10.end.0.speaker', 0x49B344, 'u'],
      ['dialog.10.end.1', 0x48B228, 'm'], ['dialog.10.end.1.speaker', 0x49B34C, 'u'],
      ['dialog.24.start.0', 0x48B160, 'm'], ['dialog.24.start.0.speaker', 0x49B35C, 'u'],
      ['dialog.24.start.1', 0x48B0E8, 'm'], ['dialog.24.start.1.speaker', 0x49B364, 'u'],
      // END gulf address list
    ] } },
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
    const cstr = (addr, limit = 256, multiline = false) => {
      const off = offset(addr);
      let end = -1;
      for (let i = off; i <= Math.min(off + limit, data.length - 1); i++) if (data[i] === 0) { end = i; break; }
      if (end < 0) fail('no string terminator');
      let s = '';
      for (let i = off; i < end; i++) {
        if ((data[i] < 0x20 && !(multiline && data[i] === 0x0A)) || data[i] > 0x7E) fail('non-text bytes');
        s += String.fromCharCode(data[i]);
      }
      return s;
    };
    const u32 = (addr) => dv.getUint32(offset(addr), true);
    const quote = (s) => '"' + s.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n') + '"';
    const write = (entries) => {
      let out = `# ${header} front-end texts, read from the user's executable by\n` +
                '# tools/extract_exe_texts.py. Do not commit. Format: key = "value".\n';
      for (const [k, v] of entries) out += `${k} = ${quote(v)}\n`;
      return out;
    };
    if (table.listed) {
      // An address list (tools/exe_texts/<game>.json): every entry key, address, kind.
      const fixed = new Map((table.override || []).map(([k, a]) => [k, cstr(a, 512)]));
      const leave = new Set(table.leaveOut || []); // Gulf Thunder (issue gulf/402)
      return write(table.listed.filter(([key]) => !leave.has(key)).map(([key, addr, kind]) => [key, fixed.has(key) ? fixed.get(key)
        : kind === 'u' ? String(u32(addr)) : cstr(addr, 512, kind === 'm')]));
    }
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
    return write(entries);
  }

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

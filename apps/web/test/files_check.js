// Checks apps/web/site/files.js with node against the owner's data (docs/web.md):
// the fallback SHA-256 against node's crypto, the sizes and hashes of known_files.json for
// every game whose files are on this machine, the texts read from each game's executable
// against tools/extract_exe_texts.py's output (a game without a text table must say so), and
// that a file dropped in is assigned to its game by its SHA-256.
//
//   AS3D_DATA_ROOT=/path/to/main/checkout node apps/web/test/files_check.js
//
// Needs the game data; prints SKIP without it.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const vm = require('vm');

const site = path.join(__dirname, '..', 'site');
const root = process.env.AS3D_DATA_ROOT || path.join(__dirname, '..', '..', '..');
const ctx = { window: {}, crypto: undefined, TextDecoder, Blob: globalThis.Blob, console };
vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(site, 'files.js'), 'utf8'), ctx);
const F = ctx.window.AS3DFiles;
let failed = 0;
const check = (ok, what) => {
  console.log((ok ? 'ok   ' : 'FAIL ') + what);
  if (!ok) failed++;
};

for (const n of [0, 1, 55, 56, 63, 64, 65, 1000, 100000]) {
  const b = crypto.randomBytes(n);
  check(F.sha256Js(new Uint8Array(b)) === crypto.createHash('sha256').update(b).digest('hex'), `sha256Js of ${n} bytes`);
}

const games = JSON.parse(fs.readFileSync(path.join(site, 'known_files.json'), 'utf8')).games;
const installOf = (key) => (key === 'as3d' ? path.join(root, 'third_party_local', 'original')
  : path.join(root, 'third_party_local', 'games', key));
const extractedOf = (key) => (key === 'as3d' ? path.join(root, 'assets_extracted')
  : path.join(root, 'assets_extracted_games', key));

// A File-like object for check(): name, size and arrayBuffer().
const fileLike = (p) => {
  const b = fs.readFileSync(p);
  return { name: path.basename(p), size: b.length, arrayBuffer: async () => b.buffer.slice(b.byteOffset, b.byteOffset + b.length) };
};

(async () => {
  // check() and fetch() read known_files.json through fetch.
  ctx.fetch = async () => ({ json: async () => JSON.parse(fs.readFileSync(path.join(site, 'known_files.json'), 'utf8')) });
  let any = false;
  for (const [key, g] of Object.entries(games)) {
    const orig = installOf(key);
    if (!fs.existsSync(path.join(orig, 'data', 'pak0.apk'))) { console.log(`skip ${key}: not in the data`); continue; }
    any = true;
    for (const [name, k] of Object.entries(g.files)) {
      if (!k.size) continue;
      const p = path.join(orig, k.where);
      if (!fs.existsSync(p)) { console.log(`skip ${key}/${name}: not in the data`); continue; }
      const b = fs.readFileSync(p);
      check(b.length === k.size, `${key}/${name} size`);
      check(F.sha256Js(new Uint8Array(b)) === k.sha256, `${key}/${name} sha256 (fallback implementation)`);
    }
    // Dropping the game's files: each is assigned to this game.
    const drop = [];
    for (const [name, k] of Object.entries(g.files)) {
      const p = k.size ? path.join(orig, k.where) : null;
      if (p && fs.existsSync(p)) drop.push(fileLike(p));
    }
    const res = await F.check(drop);
    const mine = res.games[key] || {};
    check(res.notes.every((n) => n.ok),
      `${key}: every file of the game is accepted (${res.notes.filter((n) => !n.ok).map((n) => n.text).join('; ')})`);
    check(F.missing(mine, key).length === 0, `${key}: its required files are all assigned to it`);
    for (const other of Object.keys(games)) {
      if (other === key) continue;
      const wrong = Object.keys(res.games[other] || {}).filter((n) => g.files[n] && g.files[n].required);
      check(wrong.length === 0, `${key}: no pak of it is assigned to ${other}`);
    }
    // The texts of the executable.
    const exe = path.join(orig, g.exe);
    const texts = path.join(extractedOf(key), g.texts);
    if (fs.existsSync(exe)) {
      const bytes = new Uint8Array(fs.readFileSync(exe));
      const r = F.textsFromExe(bytes, crypto.createHash('sha256').update(bytes).digest('hex'), games);
      if (key === 'as3d') {
        check(r.game === key && r.text !== null, `${key}: executable recognised with a text table`);
        if (fs.existsSync(texts)) {
          check(r.text === fs.readFileSync(texts, 'utf8'), `${key}: texts from ${g.exe} equal tools/extract_exe_texts.py output`);
        }
      } else {
        check(r.game === key && r.text === null && r.notMapped === `texts of ${g.title} are not mapped yet`,
          `${key}: "${r.notMapped}"`);
      }
    }
  }
  if (!any) console.log('SKIP: no game data under ' + root);
  // A file of no game, and a file with a known name but other contents, are refused.
  const junk = { name: 'pak1.apk', size: 1000, arrayBuffer: async () => new ArrayBuffer(1000) };
  const r = await F.check([junk]);
  check(r.notes.length === 1 && !r.notes[0].ok && Object.keys(r.games).length === 0, 'a wrong pak1.apk is refused');
  console.log(failed ? `files_check: ${failed} FAILED` : 'files_check: OK');
  process.exit(failed ? 1 : 0);
})();

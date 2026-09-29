// Checks apps/web/site/files.js with node against the owner's data (docs/web.md):
// the fallback SHA-256 against node's crypto and the sizes and hashes of known_files.json,
// and the texts read from AirStrike3D.exe against tools/extract_exe_texts.py's output.
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

const known = JSON.parse(fs.readFileSync(path.join(site, 'known_files.json'), 'utf8')).files;
const orig = path.join(root, 'third_party_local', 'original');
if (!fs.existsSync(path.join(orig, 'data', 'pak0.apk'))) {
  console.log('SKIP: no game data under ' + orig);
} else {
  for (const [name, k] of Object.entries(known)) {
    if (!k.size) continue;
    const p = path.join(orig, k.where);
    if (!fs.existsSync(p)) { console.log(`skip ${name}: not in the data`); continue; }
    const b = fs.readFileSync(p);
    check(b.length === k.size, `${name} size`);
    check(F.sha256Js(new Uint8Array(b)) === k.sha256, `${name} sha256 (fallback implementation)`);
  }
  const exe = path.join(orig, 'AirStrike3D.exe');
  const texts = path.join(root, 'assets_extracted', 'texts_v170.txt');
  if (fs.existsSync(exe) && fs.existsSync(texts)) {
    const got = F.extractExeTexts(new Uint8Array(fs.readFileSync(exe)));
    check(got === fs.readFileSync(texts, 'utf8'), 'texts from AirStrike3D.exe equal tools/extract_exe_texts.py output');
  }
}
console.log(failed ? `files_check: ${failed} FAILED` : 'files_check: OK');
process.exit(failed ? 1 : 0);

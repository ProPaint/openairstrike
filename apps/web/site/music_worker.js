// The music worker of "Files for the 3DS" (sd3ds.js, docs/web.md "Files for the 3DS"): runs
// as3d_music.wasm (apps/web/music_main.cpp, the render and encoder of audio_tool render-music)
// off the page's thread. The paks arrive as Blobs and are read in place (WORKERFS), the Ogg
// files go back as bytes. Messages from the page:
//   { type: 'init', stamp }                  -> { type: 'ready' }
//   { type: 'jobs', game, paks: [{ name, blob }] }
//                                            -> { type: 'jobs', jobs: [{ module, file, order }] }
//   { type: 'render', index }                -> { type: 'progress', fraction } ..., then
//                                               { type: 'file', bytes: Uint8Array, info }
// Any failure: { type: 'error', message }. The page terminates the worker to cancel.
'use strict';

let mod = null;
let mountCount = 0;
let mounted = null;

function post(m, transfer) { self.postMessage(m, transfer || []); }

self.onmessage = async (e) => {
  const m = e.data;
  try {
    if (m.type === 'init') {
      importScripts('as3d_music.js?v=' + encodeURIComponent(m.stamp || ''));
      mod = await self.AS3DMusic({
        locateFile: (p) => p + '?v=' + encodeURIComponent(m.stamp || ''),
        print: (t) => post({ type: 'log', text: String(t) }),
        printErr: (t) => post({ type: 'log', text: String(t) }),
      });
      mod.onMusicProgress = (f) => post({ type: 'progress', fraction: f });
      post({ type: 'ready' });
    } else if (m.type === 'jobs') {
      const FS = mod.FS;
      if (mounted) {
        FS.unmount(mounted);
        FS.rmdir(mounted);
      }
      mounted = '/paks' + (++mountCount);
      FS.mkdir(mounted);
      FS.mount(FS.filesystems.WORKERFS, { blobs: m.paks.map((p) => ({ name: p.name, data: p.blob })) }, mounted);
      const list = m.paks.map((p) => mounted + '/' + p.name).join('\n');
      const jobs = JSON.parse(mod.ccall('as3d_music_jobs', 'string', ['string'], [list]));
      post({ type: 'jobs', game: m.game, jobs });
    } else if (m.type === 'render') {
      const out = '/out.ogg';
      const info = JSON.parse(mod.ccall('as3d_music_render', 'string', ['number', 'string'], [m.index, out]));
      if (!info.ok) throw new Error(info.error);
      const bytes = mod.FS.readFile(out);
      mod.FS.unlink(out);
      post({ type: 'file', index: m.index, bytes, info }, [bytes.buffer]);
    }
  } catch (err) {
    post({ type: 'error', message: (err && err.message) || String(err) });
  }
};

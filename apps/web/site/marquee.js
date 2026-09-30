// The game selector's marquees on the web page (docs/web.md, docs/spec/issues/164).
//
// The engine draws each card's marquee: the first game's flaming 3D banner, AirStrike 2's logo
// with its "2" emblem, Gulf Thunder's logo. The page cannot run the engine before a game is
// chosen (its 25 to 48 MB of data are fetched only then), so:
//  * bundled build: tools/web_build.sh renders the marquees with the engine at build time
//    (marquee/<key>.webp, one looping animation per game, marquee/marquees.json);
//  * bring-your-own build: there is nothing of any game in the site. Once the player's files
//    are stored the page reads the logo pictures out of them, in the browser, and draws the two
//    logos with the same arithmetic as the engine (engine/src/ui/as2_widgets.cpp
//    titleLogoAt). The first game's banner is a 3D mesh only the engine can draw: that card
//    keeps its title in large text.
// Until there is a picture the card shows its title in text.
window.AS3DMarquee = (function () {
  'use strict';

  // ---------------------------------------------------------------------------------------
  // Bundled: the build-time renders.
  // ---------------------------------------------------------------------------------------
  let rendered = null;
  function renders() {
    if (!rendered) {
      rendered = fetch('marquee/marquees.json', { cache: 'no-cache' })
        .then((r) => (r.ok ? r.json() : {}))
        .catch(() => ({})); // no renders: text cards
    }
    return rendered;
  }
  async function fromRender(key) {
    const m = (await renders())[key];
    if (!m) return null;
    const img = new Image();
    img.alt = '';
    img.width = m.width;
    img.height = m.height;
    img.decoding = 'async';
    img.src = 'marquee/' + m.file + '?v=' + ((window.AS3D_BUILD || {}).stamp || '');
    try { await img.decode(); } catch (e) { return null; }
    return img;
  }

  // ---------------------------------------------------------------------------------------
  // Bring your own: pictures out of the stored paks (docs/spec/pak.md), no game file leaves
  // the browser.
  // ---------------------------------------------------------------------------------------
  async function pakIndex(blob) {
    const head = new Uint8Array(await blob.slice(0, 0x410).arrayBuffer());
    const magic = [0, 0, 0x80, 0x3f, 0x99, 0x99, 0, 0];
    if (head.length < 0x410 || magic.some((b, i) => head[i] !== b)) return null;
    const dv = new DataView(head.buffer);
    const tableOff = dv.getUint32(8, true), count = dv.getUint32(12, true);
    const key = head.subarray(0x10, 0x410);
    const table = new Uint8Array(await blob.slice(tableOff, tableOff + count * 76).arrayBuffer());
    for (let i = 0; i < table.length; i++) table[i] ^= key[i % 1024];
    const tv = new DataView(table.buffer);
    const entries = new Map();
    for (let e = 0; e < count && (e + 1) * 76 <= table.length; e++) {
      let n = '';
      for (let i = 0; i < 64 && table[e * 76 + i]; i++) n += String.fromCharCode(table[e * 76 + i]);
      entries.set(n.toLowerCase().replace(/\//g, '\\'), {
        off: tv.getUint32(e * 76 + 64, true), size: tv.getUint32(e * 76 + 68, true), enc: tv.getUint32(e * 76 + 72, true), key,
      });
    }
    return { blob, entries };
  }
  async function pakRead(pak, name) {
    const e = pak.entries.get(name);
    if (!e) return null;
    const b = new Uint8Array(await pak.blob.slice(e.off, e.off + e.size).arrayBuffer());
    if (e.enc) for (let j = 0; j < b.length; j++) b[j] ^= e.key[j % 1024];
    return b;
  }
  // Truevision TGA: uncompressed true colour (24, 32 bit) and 8 bit colour-mapped, which is all
  // the logos use. Rows top first.
  function decodeTga(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const idLen = b[0], mapType = b[1], type = b[2];
    const mapLen = dv.getUint16(5, true), mapBits = b[7];
    const w = dv.getUint16(12, true), h = dv.getUint16(14, true), bpp = b[16], desc = b[17];
    let p = 18 + idLen;
    let palette = null;
    if (mapType === 1) {
      const step = mapBits >> 3;
      palette = new Uint8Array(mapLen * 4);
      for (let i = 0; i < mapLen; i++) {
        palette[i * 4] = b[p + i * step + 2];
        palette[i * 4 + 1] = b[p + i * step + 1];
        palette[i * 4 + 2] = b[p + i * step];
        palette[i * 4 + 3] = step === 4 ? b[p + i * step + 3] : 255;
      }
      p += mapLen * step;
    }
    if (!((type === 2 && (bpp === 24 || bpp === 32)) || (type === 1 && bpp === 8 && palette))) return null;
    const out = new Uint8ClampedArray(w * h * 4);
    const bottomUp = !(desc & 0x20);
    for (let y = 0; y < h; y++) {
      const dy = bottomUp ? h - 1 - y : y;
      for (let x = 0; x < w; x++) {
        const o = (dy * w + x) * 4;
        if (palette) {
          const c = b[p + y * w + x] * 4;
          out[o] = palette[c]; out[o + 1] = palette[c + 1]; out[o + 2] = palette[c + 2]; out[o + 3] = palette[c + 3];
        } else {
          const s = p + (y * w + x) * (bpp >> 3);
          out[o] = b[s + 2]; out[o + 1] = b[s + 1]; out[o + 2] = b[s];
          out[o + 3] = bpp === 32 ? b[s + 3] : 255;
        }
      }
    }
    return { w, h, rgba: out };
  }
  const toCanvas = (t) => {
    const c = document.createElement('canvas');
    c.width = t.w;
    c.height = t.h;
    c.getContext('2d').putImageData(new ImageData(t.rgba, t.w, t.h), 0, 0);
    return c;
  };

  const PICTURES = {
    as2: { logo: 'gfx\\logo\\logo.tga', clouds: 'gfx\\logo\\clouds.tga', glow: 'gfx\\logo\\glow.tga', two: 'gfx\\logo\\two3.tga' },
    gulf: { logo: 'gfx\\logo\\logo_gulf.tga', clouds: 'gfx\\logo\\clouds.tga' },
  };
  async function loadPictures(key, files) {
    const want = PICTURES[key];
    if (!want) return null;
    const paks = [];
    for (const n of ['pak0.apk', 'pak1.apk', 'pak2.apk']) {
      if (files[n]) {
        const idx = await pakIndex(files[n]);
        if (idx) paks.push(idx);
      }
    }
    const pics = {};
    for (const [what, name] of Object.entries(want)) {
      let bytes = null;
      for (const pak of paks) bytes = (await pakRead(pak, name)) || bytes; // a later pak overrides
      const t = bytes && decodeTga(bytes);
      if (!t) {
        if (what === 'logo') return null;
        continue;
      }
      pics[what] = t;
    }
    return pics;
  }

  // The marquee box of the engine's card (352 x 162 virtual pixels), drawn at 2x.
  const BOX_W = 352, BOX_H = 162, RES = 2;
  function animate(canvas, draw) {
    const t0 = performance.now();
    let last = 0;
    const tick = (now) => {
      if (!canvas.isConnected) return; // the card is gone
      if (now - last >= 30) { // about 30 frames a second is plenty for these
        last = now;
        draw((now - t0) / 1000);
      }
      requestAnimationFrame(tick);
    };
    draw(0);
    requestAnimationFrame(tick);
  }

  // Both logos: the letters take the clouds through them (texture 2, combined by ADD) at 0.1
  // texture widths per title-clock unit, over two widths, the title clock running at half the
  // menu time.
  function logoWithClouds(logo, clouds, cloudT) {
    const out = new Uint8ClampedArray(logo.rgba);
    if (clouds) {
      for (let y = 0; y < logo.h; y++) {
        const cy = Math.min(clouds.h - 1, Math.floor(((y + 0.5) / logo.h) * clouds.h));
        for (let x = 0; x < logo.w; x++) {
          let s = 0.1 * cloudT + 2 * ((x + 0.5) / logo.w);
          s -= Math.floor(s);
          const c = (cy * clouds.w + Math.floor(s * clouds.w)) * 4, o = (y * logo.w + x) * 4;
          out[o] = Math.min(255, out[o] + clouds.rgba[c]);
          out[o + 1] = Math.min(255, out[o + 1] + clouds.rgba[c + 1]);
          out[o + 2] = Math.min(255, out[o + 2] + clouds.rgba[c + 2]);
        }
      }
    }
    return { w: logo.w, h: logo.h, rgba: out };
  }

  async function fromFiles(key, files) {
    if (!files) return null;
    let pics = null;
    try { pics = await loadPictures(key, files); } catch (e) { return null; }
    if (!pics) return null;
    const canvas = document.createElement('canvas');
    canvas.width = BOX_W * RES;
    canvas.height = BOX_H * RES;
    const ctx = canvas.getContext('2d');
    const glow = pics.glow && toCanvas(pics.glow), two = pics.two && toCanvas(pics.two);
    const scratch = document.createElement('canvas');
    scratch.width = pics.logo.w;
    scratch.height = pics.logo.h;
    const sctx = scratch.getContext('2d');
    ctx.imageSmoothingQuality = 'high';
    animate(canvas, (clock) => {
      const T = 0.5 * clock;
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.globalCompositeOperation = 'source-over';
      ctx.fillStyle = '#000';
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      sctx.putImageData(new ImageData(logoWithClouds(pics.logo, pics.clouds, T).rgba, pics.logo.w, pics.logo.h), 0, 0);
      if (key === 'gulf') {
        const k = Math.min(BOX_W / 512, BOX_H / 256) * RES;
        ctx.setTransform(k, 0, 0, k, (canvas.width - 512 * k) / 2, (canvas.height - 256 * k) / 2);
        ctx.drawImage(scratch, 0, 0);
        return;
      }
      // AirStrike 2: the title screen's logo, 614 x 190 virtual pixels of it, centred on the
      // letters' middle line (launcher_marquee.cpp).
      const k = Math.min(BOX_W / 614, BOX_H / 190) * RES;
      ctx.setTransform(k, 0, 0, k, (canvas.width - 614 * k) / 2 - 124 * k, canvas.height / 2 - 64 * k);
      if (glow) {
        ctx.globalCompositeOperation = 'lighter';
        ctx.drawImage(glow, 124, 0);
        ctx.globalCompositeOperation = 'source-over';
      }
      if (two) {
        const g = 15 + 15 * Math.sin(T + 0.2), size = 128 + 2 * g;
        ctx.save();
        ctx.translate(580 - g + size / 2, -g + size / 2);
        ctx.rotate(((20 + 15 * Math.sin(2 * T)) * Math.PI) / 180);
        ctx.drawImage(two, -size / 2, -size / 2, size, size);
        ctx.restore();
      }
      ctx.drawImage(scratch, 124, 0, 512, 128);
    });
    return canvas;
  }

  return { fromRender, fromFiles };
})();

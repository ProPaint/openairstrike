// A game controller in headless Chromium (docs/web.md "Controllers"): a fake "standard"
// gamepad (navigator.getGamepads overridden, gamepadconnected dispatched; Emscripten's SDL
// polls getGamepads) plays AirStrike 3D with no key and no click: A on the start screen is
// Play, A through the intro, D-pad + A / B through the main menu to Start Game, the stick and
// A start mission 1, the stick and the D-pad steer while A fires (screenshots), Start opens
// and closes the in-game menu, a key leaves the menus' pad mode, unplug and replug.
//
//   AS3D_WEB_SITE=$PWD/out/web/site tools/web_serve.sh 8766 &
//   node apps/web/test/gamepad.js [--url http://127.0.0.1:8766/] [--shots DIR]
//
// PLAYWRIGHT: the playwright module (path or name, default "playwright"); CHROMIUM: a Chromium
// executable (default: Playwright's own). One browser. Exit code 1 on failure.
'use strict';
const path = require('path');
const fs = require('fs');
const arg = (name, def) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : def; };
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright');
const OUT = arg('--shots', 'out/web/shots');
const PAGE_URL = arg('--url', 'http://127.0.0.1:8766/').replace(/\/?$/, '/') + '?game=as3d';
fs.mkdirSync(OUT, { recursive: true });

const initPad = () => {
  const mk = () => ({ id: 'Fake Xbox Controller (STANDARD GAMEPAD Vendor: 045e Product: 02ea)', index: 0,
    connected: true, mapping: 'standard', timestamp: 1,
    buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })),
    axes: [0, 0, 0, 0] });
  window.__pad = mk();
  window.__padOn = false;
  navigator.getGamepads = () => {
    const p = window.__pad;
    p.timestamp = performance.now();
    return window.__padOn ? [p, null, null, null] : [null, null, null, null];
  };
  const fire = (type) => {
    const e = new Event(type);
    Object.defineProperty(e, 'gamepad', { value: window.__pad });
    window.dispatchEvent(e);
  };
  window.__connect = () => { window.__padOn = true; window.__pad.connected = true; fire('gamepadconnected'); };
  window.__disconnect = () => { window.__padOn = false; window.__pad.connected = false; fire('gamepaddisconnected'); };
  window.__btn = (i, on) => { const b = window.__pad.buttons[i]; b.pressed = on; b.touched = on; b.value = on ? 1 : 0; };
  window.__axis = (i, v) => { window.__pad.axes[i] = v; };
};

const B = { A: 0, B: 1, X: 2, Y: 3, LB: 4, RB: 5, LT: 6, RT: 7, BACK: 8, START: 9, UP: 12, DOWN: 13, LEFT: 14, RIGHT: 15 };

(async () => {
  const browser = await chromium.launch({
    executablePath: process.env.CHROMIUM || undefined,
    headless: true,
    args: ['--use-gl=angle', '--use-angle=gl-egl', '--ignore-gpu-blocklist', '--enable-gpu'],
  });
  const results = [];
  const ok = (name, cond, extra) => { results.push({ name, ok: !!cond, extra }); console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra ? ' ' + extra : '')); };
  try {
    const ctx = await browser.newContext({ viewport: { width: 1280, height: 720 } });
    await ctx.addInitScript(initPad);
    const page = await ctx.newPage();
    const lines = [];
    page.on('console', (m) => lines.push(m.text()));
    page.on('pageerror', (e) => lines.push('pageerror ' + e));
    const mark = () => lines.length;
    const waitLine = async (rx, ms = 60000, after = 0) => {
      const end = Date.now() + ms;
      while (Date.now() < end) {
        for (let i = after; i < lines.length; i++) { const m = lines[i].match(rx); if (m) return m; }
        await page.waitForTimeout(100);
      }
      throw new Error('timeout waiting for ' + rx);
    };
    const press = async (i, ms = 120) => {
      await page.evaluate((i) => window.__btn(i, true), i);
      await page.waitForTimeout(ms);
      await page.evaluate((i) => window.__btn(i, false), i);
      await page.waitForTimeout(150);
    };
    const screen = () => page.evaluate(() => window.as3dState.screen);
    const shot = (n) => page.screenshot({ path: path.join(OUT, n + '.png') });

    await page.goto(PAGE_URL);
    await page.waitForFunction(() => window.as3dState && (window.as3dState.ready || window.as3dState.failed), null, { timeout: 120000 });
    // The pad shows up (the browser reveals it on a press), then A on the start screen is Play.
    await page.evaluate(() => { window.__btn(0, true); window.__connect(); });
    await page.waitForTimeout(300);
    ok('the revealing press does not start', !(await page.evaluate(() => window.as3dState.started)));
    await page.evaluate(() => window.__btn(0, false));
    await page.waitForTimeout(200);
    await press(B.A);
    await waitLine(/AS3D_WEB pad_play/, 5000);
    ok('start screen: A plays', true);
    const added = await waitLine(/AS3D_PAD added id=(\d+) name="([^"]*)"/, 60000);
    ok('engine: controller opened at startup', true, added[2]);

    // Intro pages to the main menu with A.
    await waitLine(/AS3D_SCREEN name=(intro|main)\b/, 90000);
    for (let k = 0; k < 40 && (await screen()) !== 'main'; k++) await press(B.A, 100), await page.waitForTimeout(400);
    ok('intro skipped with A, main menu', (await screen()) === 'main');
    await waitLine(/AS3D_LEVEL_LOADED/, 60000);
    await page.waitForTimeout(800);
    // D-pad down focuses the first item (Start Game); A opens it.
    let mk = mark();
    await press(B.DOWN);
    await waitLine(/AS3D_PAD navigation=1/, 3000);
    ok('menus in pad mode after a pad press', true);
    await shot('pad_main_focus');
    // A opens the focused item; anything but Start Game: B back, Up, again.
    const visited = [];
    for (let k = 0; k < 6; k++) {
      mk = mark();
      await press(B.A);
      const m = await waitLine(/AS3D_SCREEN name=(\w+)/, 10000, mk);
      visited.push(m[1]);
      if (m[1] === 'start') break;
      await page.waitForTimeout(400);
      mk = mark();
      await press(B.B);
      await waitLine(/AS3D_SCREEN name=main\b/, 10000, mk);
      await page.waitForTimeout(400);
      await press(B.UP);
    }
    ok('main menu: D-pad + A opens items, B comes back, reaches Start Game', visited[visited.length - 1] === 'start', visited.join(','));
    await page.waitForTimeout(500);
    const stick = async (ax, v, ms = 150) => {
      await page.evaluate(([a, v]) => window.__axis(a, v), [ax, v]);
      await page.waitForTimeout(ms);
      await page.evaluate((a) => window.__axis(a, 0), ax);
      await page.waitForTimeout(150);
    };
    await stick(1, 0.9);   // down: the list
    await stick(1, -0.9);  // up: wraps to Start
    await shot('pad_start_focus');
    mk = mark();
    await press(B.A);
    const sc = await waitLine(/AS3D_SCREEN name=(playing|hint)\b/, 90000, mk);
    ok('Start Game: stick + A starts the mission', true, sc[1]);
    await waitLine(/AS3D_LEVEL_LOADED mission=1/, 90000, mk);
    await page.waitForTimeout(1500);
    for (let k = 0; k < 5 && (await screen()) === 'hint'; k++) await press(B.A), await page.waitForTimeout(500);
    ok('hint closed with A', (await screen()) === 'playing', await screen());

    // Flight: hold the stick left 1.5 s, then the D-pad right 3 s, screenshots after each.
    await page.waitForTimeout(500);
    await shot('pad_fly_0');
    mk = mark();
    await page.evaluate(() => { window.__axis(0, -1); window.__btn(0, true); });
    await page.waitForTimeout(1500);
    await shot('pad_fly_left');
    await page.evaluate(() => { window.__axis(0, 0); window.__btn(14 + 1, true); });
    await page.waitForTimeout(3000);
    await shot('pad_fly_right');
    await page.evaluate(() => { window.__btn(15, false); window.__btn(0, false); });
    const heldLines = lines.slice(mk).filter((t) => /AS3D_PAD held=/.test(t));
    ok('pad bits seen in flight', heldLines.some((t) => /held=0x00000021/.test(t)) && heldLines.some((t) => /held=0x00000011/.test(t)),
       heldLines.join(' | '));
    // Start: the in-game menu; Start again resumes.
    await page.waitForTimeout(300);
    mk = mark();
    await press(B.START);
    await waitLine(/AS3D_SCREEN name=ingame\b/, 5000, mk);
    ok('Start opens the in-game menu', true);
    await shot('pad_ingame');
    mk = mark();
    await press(B.START);
    await waitLine(/AS3D_SCREEN name=playing\b/, 5000, mk);
    ok('Start again resumes', true);
    // A keyboard key leaves pad mode.
    mk = mark();
    await page.locator('#canvas').focus();
    await page.keyboard.press('ArrowLeft');
    await waitLine(/AS3D_PAD navigation=0/, 3000, mk);
    ok('a key leaves pad mode', true);
    // Hot-unplug.
    mk = mark();
    await page.evaluate(() => window.__disconnect());
    await waitLine(/AS3D_PAD removed/, 5000, mk);
    ok('unplug: controller closed', true);
    mk = mark();
    await page.evaluate(() => window.__connect());
    await waitLine(/AS3D_PAD added/, 5000, mk);
    ok('replug: controller opened again', true);
    const errs = lines.filter((t) => /pageerror|PAGE_ERROR|FATAL/.test(t));
    ok('no page errors', errs.length === 0, errs.join(' | '));
  } catch (e) {
    ok('exception', false, String(e && e.stack || e));
  } finally {
    await browser.close();
  }
  fs.writeFileSync(path.join(OUT, 'gamepad.json'), JSON.stringify(results, null, 1));
  process.exit(results.every((r) => r.ok) ? 0 : 1);
})();

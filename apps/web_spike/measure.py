#!/usr/bin/env python3
"""Measures the web spike in one headless Chromium through Playwright (docs/web-spike.md).

    measure.py --url http://127.0.0.1:8765/ --query "app=game&level=1&bot=1" --seconds 60 \
               --out OUTDIR/name [--gl swiftshader|gpu] [--keys] [--touch] [--click]

Writes OUTDIR/name.json (renderer, timings, frame statistics, memory, console) and
OUTDIR/name_*.png. Frame statistics come from wrapping requestAnimationFrame: the interval
between callbacks that drew and the time spent inside each callback.
Needs Playwright (pip install playwright; python -m playwright install chromium).
"""
import argparse
import json
import statistics
import subprocess
import time

from playwright.sync_api import sync_playwright

RAF_HOOK = """
(() => {
  window.__raf = [];
  const orig = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = (cb) => orig((ts) => {
    const a = performance.now();
    cb(ts);
    window.__raf.push([a, performance.now()]);
  });
})();
"""

GL_PROBE = """
(() => {
  const c = document.createElement('canvas');
  const gl = c.getContext('webgl2');
  if (!gl) return { webgl2: false };
  const ext = gl.getExtension('WEBGL_debug_renderer_info');
  return {
    webgl2: true,
    version: gl.getParameter(gl.VERSION),
    glsl: gl.getParameter(gl.SHADING_LANGUAGE_VERSION),
    vendor: ext ? gl.getParameter(ext.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
    renderer: ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
    maxTexture: gl.getParameter(gl.MAX_TEXTURE_SIZE),
    maxSamples: gl.getParameter(gl.MAX_SAMPLES),
  };
})()
"""


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    return s[min(len(s) - 1, int(p / 100.0 * len(s)))]


def chrome_rss_mb():
    """Resident memory of the headless browser's processes (all, and the largest one)."""
    out = subprocess.run(["ps", "-eo", "rss,args"], capture_output=True, text=True).stdout
    rss = [int(l.split(None, 1)[0]) for l in out.splitlines()[1:]
           if "headless_shell" in l or "chrome-linux" in l or "ms-playwright" in l or "pw-browsers" in l]
    return {"total_mb": round(sum(rss) / 1024, 1), "largest_mb": round(max(rss) / 1024, 1) if rss else 0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", default="http://127.0.0.1:8765/")
    ap.add_argument("--query", default="app=game&level=1&bot=1")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--out", required=True)
    ap.add_argument("--gl", default="swiftshader", choices=["swiftshader", "gpu"])
    ap.add_argument("--keys", action="store_true", help="hold keys: fly and fire")
    ap.add_argument("--touch", action="store_true", help="drag a finger over the canvas")
    ap.add_argument("--click", action="store_true", help="click the canvas after start (audio gesture)")
    ap.add_argument("--shots", type=int, default=3)
    a = ap.parse_args()

    if a.gl == "swiftshader":
        flags = ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"]
    else:
        # ANGLE on the host's OpenGL ES through EGL: the real GPU without a display (Vulkan
        # gave no WebGL 2 headless on the development machine).
        flags = ["--use-gl=angle", "--use-angle=gl-egl", "--ignore-gpu-blocklist", "--enable-gpu"]
    flags += ["--autoplay-policy=user-gesture-required"]
    result = {"query": a.query, "gl_mode": a.gl, "flags": flags, "console": []}
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True, args=flags)
        ctx = browser.new_context(viewport={"width": 900, "height": 760}, has_touch=a.touch)
        page = ctx.new_page()
        t_start = time.time()
        page.on("console", lambda m: result["console"].append(
            {"t": round(time.time() - t_start, 3), "type": m.type, "text": m.text}))
        page.on("pageerror", lambda e: result["console"].append(
            {"t": round(time.time() - t_start, 3), "type": "pageerror", "text": str(e)}))
        responses = []
        page.on("response", lambda r: responses.append(
            {"url": r.url.rsplit("/", 1)[-1], "status": r.status,
             "encoding": r.headers.get("content-encoding", "")}))
        page.add_init_script(RAF_HOOK)
        page.goto(a.url + "?" + a.query)
        result["gl"] = page.evaluate(GL_PROBE)
        first = "AS3D_WEB_FIRST_FRAME" if "app=level" in a.query else "AS3D_GAME_START"
        deadline = time.time() + 180
        while time.time() < deadline:
            if any(first in c["text"] or "FATAL" in c["text"] or "ABORT" in c["text"] for c in result["console"]):
                break
            page.wait_for_timeout(200)
        # Page-relative times of the first frame drawn after startup.
        page.wait_for_timeout(500)
        raf = page.evaluate("window.__raf")
        result["first_raf_end_ms"] = raf[0][1] if raf else None
        result["runtime_ready_ms"] = page.evaluate("window.as3dRuntimeMs || null")
        nav = page.evaluate("JSON.stringify(performance.getEntriesByType('navigation')[0])")
        result["navigation"] = json.loads(nav) if nav else None
        res = page.evaluate("performance.getEntriesByType('resource').map(r => ({name: r.name.split('/').pop(),"
                            " transfer: r.transferSize, decoded: r.decodedBodySize, dur: r.duration, end: r.responseEnd}))")
        result["resources"] = res
        canvas = page.locator("#canvas")
        if a.click:
            canvas.click()
        page.evaluate("window.__raf = []")
        t_measure = time.time()
        shots = 0
        mem = []
        next_shot = 0.0
        cdp = ctx.new_cdp_session(page)
        box = canvas.bounding_box()
        while time.time() - t_measure < a.seconds:
            el = time.time() - t_measure
            if shots < a.shots and el >= next_shot:
                canvas.screenshot(path=f"{a.out}_{shots}.png")
                shots += 1
                next_shot = el + a.seconds / max(1, a.shots)
            if a.keys:
                canvas.focus()
                page.keyboard.down("Control")
                page.keyboard.down("ArrowLeft" if int(el) % 4 < 2 else "ArrowRight")
                page.wait_for_timeout(400)
                page.keyboard.up("ArrowLeft")
                page.keyboard.up("ArrowRight")
                if int(el) % 5 == 0:
                    page.keyboard.press("Shift")
            elif a.touch:
                x0, y0 = box["x"] + box["width"] * 0.5, box["y"] + box["height"] * 0.7
                pts = [{"x": x0, "y": y0, "id": 1}]
                cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": pts})
                for k in range(10):
                    pts = [{"x": x0 + (k - 5) * 8, "y": y0 - k * 3, "id": 1}]
                    cdp.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": pts})
                    page.wait_for_timeout(40)
                cdp.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
                page.wait_for_timeout(300)
            else:
                page.wait_for_timeout(1000)
            if not mem or el - mem[-1]["t"] >= 5:
                heap = page.evaluate("(typeof Module !== 'undefined' && Module.HEAPU8) ? Module.HEAPU8.length : null")
                jsheap = page.evaluate("performance.memory ? performance.memory.usedJSHeapSize : null")
                mem.append({"t": round(el, 1), "wasm_heap_mb": heap and round(heap / 2**20, 1),
                            "js_heap_mb": jsheap and round(jsheap / 2**20, 1), **chrome_rss_mb()})
        raf = page.evaluate("window.__raf")
        result["audio_state"] = page.evaluate(
            "(typeof Module !== 'undefined' && Module.SDL2 && Module.SDL2.audioContext) ? Module.SDL2.audioContext.state : 'none'")
        canvas.screenshot(path=f"{a.out}_end.png")
        browser.close()

    ivals = [raf[i][0] - raf[i - 1][0] for i in range(1, len(raf))]
    work = [e - s for s, e in raf]
    result["frames"] = {
        "raf_callbacks": len(raf),
        "seconds": a.seconds,
        "callbacks_per_s": round(len(raf) / a.seconds, 1),
        "interval_ms": {"mean": round(statistics.mean(ivals), 2) if ivals else None,
                        "p50": pct(ivals, 50), "p95": pct(ivals, 95), "p99": pct(ivals, 99),
                        "max": max(ivals) if ivals else None},
        "callback_ms": {"mean": round(statistics.mean(work), 2) if work else None,
                        "p50": pct(work, 50), "p95": pct(work, 95), "max": max(work) if work else None},
    }
    result["memory"] = mem
    result["responses"] = responses
    with open(a.out + ".json", "w") as f:
        json.dump(result, f, indent=1)
    perf = [c["text"] for c in result["console"] if "AS3D_PERF" in c["text"] or "LEVEL_PERF" in c["text"]]
    errs = [c["text"] for c in result["console"] if c["type"] in ("error", "pageerror") or "FATAL" in c["text"]
            or "ERROR" in c["text"] or "ABORT" in c["text"]]
    print(json.dumps({"gl": result["gl"], "first_raf_end_ms": result["first_raf_end_ms"],
                      "runtime_ready_ms": result["runtime_ready_ms"], "frames": result["frames"],
                      "audio": result["audio_state"], "memory_last": mem[-1] if mem else None}, indent=1))
    print("perf lines:", *perf[-4:], sep="\n  ")
    print("errors:", *errs[:15], sep="\n  ")


if __name__ == "__main__":
    main()

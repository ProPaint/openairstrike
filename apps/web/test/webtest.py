"""Helpers for the web version's browser tests (docs/web.md): one Playwright browser, the page's
console lines (the engine's AS3D_* markers), and taps on the game's menu items by mouse or by
synthesized touch. Used by walk.py and measure.py.

Needs Playwright (pip install playwright; python -m playwright install chromium firefox).
"""
import json
import re
import subprocess
import time

from playwright.sync_api import sync_playwright

GPU_FLAGS = ["--use-gl=angle", "--use-angle=gl-egl", "--ignore-gpu-blocklist", "--enable-gpu"]
SWIFTSHADER_FLAGS = ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"]


def browser_rss_mb():
    """Resident memory of the test browser's processes (all, and the largest one)."""
    out = subprocess.run(["ps", "-eo", "rss,args"], capture_output=True, text=True).stdout
    rss = [int(l.split(None, 1)[0]) for l in out.splitlines()[1:] if "ms-playwright" in l]
    return {"total_mb": round(sum(rss) / 1024, 1), "largest_mb": round(max(rss) / 1024, 1) if rss else 0}


class Browser:
    """One browser for a whole run (the machine allows one at a time)."""

    def __init__(self, engine="chromium", gl="gpu", headless=True):
        self.pw = sync_playwright().start()
        self.engine = engine
        if engine == "chromium":
            flags = (GPU_FLAGS if gl == "gpu" else SWIFTSHADER_FLAGS) + ["--autoplay-policy=user-gesture-required"]
            self.browser = self.pw.chromium.launch(headless=headless, args=flags,
                                                   ignore_default_args=["--autoplay-policy=no-user-gesture-required"])
        else:
            self.browser = self.pw.firefox.launch(headless=headless, firefox_user_prefs={
                "webgl.force-enabled": True, "media.autoplay.default": 0})

    def page(self, base, query="", **context):
        ctx = self.browser.new_context(**context)
        return Page(ctx, base, query)

    def device(self, name):
        return dict(self.pw.devices[name])

    def close(self):
        self.browser.close()
        self.pw.stop()


class Page:
    def __init__(self, ctx, base, query):
        self.ctx = ctx
        self.page = ctx.new_page()
        self.lines = []
        self.t0 = time.time()
        self.page.on("console", lambda m: self.lines.append((round(time.time() - self.t0, 3), m.type, m.text)))
        self.page.on("pageerror", lambda e: self.lines.append((round(time.time() - self.t0, 3), "pageerror", str(e))))
        self.base = base
        if query is not None:
            self.goto(query)

    def goto(self, query):
        self.page.goto(self.base + ("?" + query if query else ""))

    def reload(self):
        self.lines.clear()
        self.page.reload()

    # -- console ---------------------------------------------------------------------------
    def texts(self):
        return [t for _, _, t in self.lines]

    def wait_line(self, pattern, timeout=60, after=0):
        """Waits for a console line matching `pattern` among lines[after:]; returns the match."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        while True:
            for _, _, t in self.lines[after:]:
                m = rx.search(t)
                if m:
                    return m
                if "FATAL" in t or t.startswith("ABORT"):
                    raise AssertionError("engine failed: " + t)
            if time.time() > end:
                raise AssertionError(f"timeout waiting for /{pattern}/; last lines:\n  " +
                                     "\n  ".join(self.texts()[-15:]))
            self.page.wait_for_timeout(100)

    def mark(self):
        return len(self.lines)

    def state(self):
        return self.page.evaluate("window.as3dState")

    def errors(self):
        return [t for _, ty, t in self.lines if ty in ("error", "pageerror") or "PAGE_ERROR" in t or "FATAL" in t]

    # -- start -----------------------------------------------------------------------------
    def wait_ready(self, timeout=120):
        self.page.wait_for_function("window.as3dState && (window.as3dState.ready || window.as3dState.failed)",
                                    timeout=timeout * 1000)
        st = self.state()
        assert not st["failed"], st["failed"]

    def wait_screen(self, name, timeout=60, after=0):
        return self.wait_line(r"AS3D_SCREEN name=" + name + r"\b", timeout, after)

    def menu_items(self, name):
        """{id: (x, y, w, h, disabled)} of the last AS3D_MENU of that screen (virtual 800x600)."""
        items = None
        for t in self.texts():
            m = re.search(r"AS3D_MENU name=(\S+) items=(.*)", t)
            if m and m.group(1) == name:
                items = {}
                for it in m.group(2).split():
                    mm = re.match(r"(-?\d+)@(-?[\d.]+),(-?[\d.]+),([\d.]+),([\d.]+)(d?)", it)
                    if mm:
                        items[int(mm.group(1))] = tuple(float(mm.group(k)) for k in range(2, 6)) + (mm.group(6) == "d",)
        assert items is not None, f"no AS3D_MENU for {name}"
        return items

    def view(self):
        """(scale, x, y) of the last AS3D_VIEW: framebuffer = virtual * scale + (x, y)."""
        m = None
        for t in self.texts():
            mm = re.search(r"AS3D_VIEW scale=([\d.]+) x=([-\d.]+) y=([-\d.]+)", t)
            if mm:
                m = mm
        assert m, "no AS3D_VIEW"
        return float(m.group(1)), float(m.group(2)), float(m.group(3))

    def virt_to_css(self, vx, vy):
        s, ox, oy = self.view()
        c = self.state()["canvas"]
        box = self.page.locator("#canvas").bounding_box()
        return box["x"] + (vx * s + ox) / c["scale"], box["y"] + (vy * s + oy) / c["scale"]

    def fb_to_css(self, fx, fy):
        c = self.state()["canvas"]
        box = self.page.locator("#canvas").bounding_box()
        return box["x"] + fx / c["scale"], box["y"] + fy / c["scale"]

    # -- input -----------------------------------------------------------------------------
    def click_css(self, x, y):
        self.page.mouse.move(x, y)
        self.page.wait_for_timeout(60)
        self.page.mouse.down()
        self.page.wait_for_timeout(80)
        self.page.mouse.up()

    def cdp(self):
        if not hasattr(self, "_cdp"):
            self._cdp = self.ctx.new_cdp_session(self.page)
        return self._cdp

    def touch(self, kind, points):
        """Sends one touch event through the DevTools protocol (real touch events in the page).
        points: [(x, y, id)] in CSS pixels; for touchEnd the points still down."""
        self.cdp().send("Input.dispatchTouchEvent", {
            "type": kind, "touchPoints": [{"x": x, "y": y, "id": i, "radiusX": 4, "radiusY": 4, "force": 1}
                                          for x, y, i in points]})

    def tap_css(self, x, y, by="mouse"):
        if by == "mouse":
            self.click_css(x, y)
        else:
            self.touch("touchStart", [(x, y, 1)])
            self.page.wait_for_timeout(80)
            self.touch("touchEnd", [])
        self.page.wait_for_timeout(150)

    def tap_item(self, screen, item_id, by="mouse"):
        items = self.menu_items(screen)
        assert item_id in items, f"no item {item_id} on {screen}: {sorted(items)}"
        x, y, w, h, disabled = items[item_id]
        assert not disabled, f"item {item_id} on {screen} is disabled"
        cx, cy = self.virt_to_css(x + w / 2, y + h / 2)
        self.tap_css(cx, cy, by)

    def tap_virtual(self, vx, vy, by="mouse"):
        cx, cy = self.virt_to_css(vx, vy)
        self.tap_css(cx, cy, by)

    def key(self, key, hold_ms=60):
        self.page.keyboard.down(key)
        self.page.wait_for_timeout(hold_ms)
        self.page.keyboard.up(key)
        self.page.wait_for_timeout(60)

    def shot(self, path):
        self.page.screenshot(path=path)

    def close(self):
        self.ctx.close()


def dump(obj, path):
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)

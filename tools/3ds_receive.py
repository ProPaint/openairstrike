#!/usr/bin/env python3
"""tools/3ds_receive.py [--port 7777] [--out out/3ds]: receives the files the 3DS app pushes at the
end of a run with run.ini report_to=<this PC>:<port> (docs/3ds-port.md "Soak mode and the log push
(round 2, P7)"), then reports on them.

The stream (apps/3ds/source/soak_report.h):
    "AS3D1\\n", then per file "<name>\\n<size>\\n" and <size> bytes, then "END\\n".
A name with a slash, a backslash or "..", or a control character, ends the connection (nothing of
it is written past that point); a file over 64 MB too. Nothing is ever sent back.

Each run goes to <out>/<date>-<commit>/ (the commit is the log's build=, a suffix -2, -3 ... when
the directory exists; "-partial" when the stream stopped before END). Then:
  - soak.txt (the console's own verdict) is printed;
  - tools/3ds_perf_report.py runs on the directory, or for a soak on each stage (the stage's part of
    log.txt and its perf.csv rows, written to <dir>/stages/<NN>-<game>-<level>-<eyes>/; the stereo
    stages with --slider max);
  - the mark dumps (mark<NN>_top/right/depth/bottom.ppm) become PNGs and marks_sheet.png
    (one row per mark: top | right eye | depth | bottom);
  - tools/3ds_soak_compare.py compares every stage's end state with the desktop as3d_sim.
One connection at a time; it keeps listening for the next run until Ctrl-C (--once: one run).

  --self-test    round-trips a stream through a local socket and checks the parser (no network)
"""
import argparse
import datetime
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAGIC = b"AS3D1\n"
END = b"END\n"
MAX_FILE = 64 * 1024 * 1024
MAX_NAME = 64


class StreamError(Exception):
    pass


def valid_name(name):
    if not name or len(name) > MAX_NAME or name in (".", ".."):
        return False
    if "/" in name or "\\" in name or ".." in name:
        return False
    return all(0x20 <= ord(c) < 0x7f for c in name)


class Reader:
    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def _fill(self):
        chunk = self.sock.recv(65536)
        if not chunk:
            raise StreamError("the connection closed early")
        self.buf += chunk

    def line(self, limit=128):
        while b"\n" not in self.buf:
            if len(self.buf) > limit:
                raise StreamError("a header line is too long")
            self._fill()
        i = self.buf.index(b"\n")
        out, self.buf = self.buf[:i], self.buf[i + 1:]
        return out

    def copy_to(self, f, size):
        left = size
        while left > 0:
            if not self.buf:
                self._fill()
            take = self.buf[:left]
            self.buf = self.buf[len(take):]
            f.write(take)
            left -= len(take)


def receive(sock, dest):
    """Reads one stream into the directory `dest`. Returns (names written, complete?, error or None)."""
    r = Reader(sock)
    names = []
    try:
        if r.line(16) + b"\n" != MAGIC:
            raise StreamError("not an AS3D1 stream")
        while True:
            raw = r.line()
            if raw + b"\n" == END:
                return names, True, None
            try:
                name = raw.decode("ascii")
            except UnicodeDecodeError:
                raise StreamError("a file name that is not ASCII")
            if not valid_name(name):
                raise StreamError("refused file name %r" % name)
            size_raw = r.line(24)
            if not size_raw.isdigit():
                raise StreamError("bad size %r for %s" % (size_raw, name))
            size = int(size_raw)
            if size > MAX_FILE:
                raise StreamError("%s is %d bytes, over the 64 MB cap" % (name, size))
            with open(os.path.join(dest, name), "wb") as f:
                r.copy_to(f, size)
            names.append(name)
    except (StreamError, OSError) as e:
        return names, False, str(e)


def run_dir(out_root, tmp):
    commit = "nocommit"
    log = os.path.join(tmp, "log.txt")
    if os.path.exists(log):
        m = re.search(r"build=([\w.-]+)", open(log, errors="replace").read())
        if m:
            commit = m.group(1)
    base = os.path.join(out_root, "%s-%s" % (datetime.date.today().isoformat(), commit))
    path, k = base, 2
    while os.path.exists(path):
        path = "%s-%d" % (base, k)
        k += 1
    return path


def contact_sheet(d):
    """mark<NN>_*.ppm -> PNGs and marks_sheet.png. Returns the sheet's path or None."""
    ppms = sorted(glob.glob(os.path.join(d, "mark*.ppm")))
    if not ppms:
        return None
    try:
        from PIL import Image
    except ImportError:
        print("receive: no PIL, the mark PPMs stay as they are")
        return None
    marks = {}
    for p in ppms:
        m = re.match(r"(mark\d+)_(top|right|depth|bottom)\.ppm$", os.path.basename(p))
        img = Image.open(p).convert("RGB")
        img.save(p[:-4] + ".png")
        if m:
            marks.setdefault(m.group(1), {})[m.group(2)] = img
    if not marks:
        return None
    order = ("top", "right", "depth", "bottom")
    w, h = 400, 240
    sheet = Image.new("RGB", (w * len(order), h * len(marks)), (40, 40, 40))
    for row, key in enumerate(sorted(marks)):
        for col, part in enumerate(order):
            img = marks[key].get(part)
            if img:
                sheet.paste(img, (col * w + (w - img.width) // 2, row * h))
    out = os.path.join(d, "marks_sheet.png")
    sheet.save(out)
    return out


STAGE_RE = re.compile(r"AS3D_SOAK_STAGE n=(\d+) of=\d+ game=(\w+) level=(\d+) eyes=(\w+)")


def split_stages(d):
    """A soak's log.txt and perf.csv cut per stage into <d>/stages/<NN>-<game>-<level>-<eyes>/.
    Returns [(dir, stereo)], empty when the log is not a soak's."""
    log = os.path.join(d, "log.txt")
    lines = open(log, errors="replace").read().splitlines(True)
    cuts = []
    for i, l in enumerate(lines):
        m = STAGE_RE.search(l)
        if m:
            cuts.append((i, int(m.group(1)), m.group(2), m.group(3), m.group(4)))
        elif "AS3D_SOAK_VERDICT" in l and cuts and len(cuts[-1]) == 5:
            cuts.append((i, None, None, None, None))
            break
    rows = {}
    csv_path = os.path.join(d, "perf.csv")
    header = None
    if os.path.exists(csv_path):
        with open(csv_path) as f:
            header = f.readline()
            cols = header.strip().split(",")
            k = cols.index("stage") if "stage" in cols else -1
            for row in f:
                parts = row.strip().split(",")
                if k >= 0 and len(parts) > k:
                    rows.setdefault(parts[k], []).append(row)
    out = []
    for j, (start, n, game, level, eyes) in enumerate(cuts):
        if n is None:
            break
        end = cuts[j + 1][0] if j + 1 < len(cuts) else len(lines)
        sd = os.path.join(d, "stages", "%02d-%s-%s-%s" % (n, game, level, eyes))
        os.makedirs(sd, exist_ok=True)
        with open(os.path.join(sd, "log.txt"), "w") as f:
            f.writelines(lines[start:end])
        if header is not None:
            with open(os.path.join(sd, "perf.csv"), "w") as f:
                f.write(header)
                f.writelines(rows.get(str(n), []))
        out.append((sd, eyes == "stereo"))
    return out


def build_of(d):
    """(build, launched-from) of a run from its log's first mode line, or (None, None)."""
    log = os.path.join(d, "log.txt")
    if not os.path.exists(log):
        return None, None
    m = re.search(r"build=([\w.-]+) from=(\S+)", open(log, errors="replace").read())
    return (m.group(1), m.group(2)) if m else (None, None)


def report(d, sim, expect=None):
    print("== %s" % d)
    build, launched = build_of(d)
    if build:
        how = ("the HOME menu title (a CIA installed with FBI; argv is empty)" if launched == "none"
               else "the Homebrew Launcher or 3dslink (%s)" % launched)
        print("-- build %s, launched from %s" % (build, how))
        if expect and build.split("-")[0] != expect.split("-")[0]:
            print("!! WARNING: this run is build %s, not the expected %s: the console ran an older copy "
                  "(an old CIA in the HOME menu? the 3dsx not uploaded?); its numbers do not test the drop"
                  % (build, expect))
    soak = os.path.join(d, "soak.txt")
    if os.path.exists(soak):
        print("-- soak.txt (the console's verdict)")
        sys.stdout.write(open(soak, errors="replace").read())
    log = os.path.join(d, "log.txt")
    if os.path.exists(log):
        lines = open(log, errors="replace").read().splitlines()
        for l in lines:
            if "AS3D_SOAK_RESULT" in l or "AS3D_AUTO_MARK" in l or "AS3D_REPORT" in l:
                print(l)
    stages = split_stages(d) if os.path.exists(log) else []
    perf = os.path.join(ROOT, "tools/3ds_perf_report.py")
    if stages:
        for sd, stereo in stages:
            print("-- tools/3ds_perf_report.py %s%s" % (os.path.relpath(sd, d), " --slider max" if stereo else ""), flush=True)
            subprocess.run([sys.executable, perf, sd] + (["--slider", "max"] if stereo else []))
    else:
        print("-- tools/3ds_perf_report.py", flush=True)
        subprocess.run([sys.executable, perf, d])
    sheet = contact_sheet(d)
    if sheet:
        print("-- marks: %s" % sheet, flush=True)
    if os.path.exists(log):
        print("-- end states against the desktop (tools/3ds_soak_compare.py)", flush=True)
        cmd = [sys.executable, os.path.join(ROOT, "tools/3ds_soak_compare.py"), d]
        if sim:
            cmd += ["--sim", sim]
        subprocess.run(cmd)


def serve(port, bind, out_root, once, sim, do_report=True, expect=None):
    os.makedirs(out_root, exist_ok=True)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((bind, port))
    srv.listen(1)
    print("receive: listening on %s:%d, writing under %s (Ctrl-C to stop)" % (bind, port, out_root), flush=True)
    try:
        while True:
            conn, peer = srv.accept()
            conn.settimeout(120)
            tmp = tempfile.mkdtemp(prefix=".incoming-", dir=out_root)
            try:
                names, complete, err = receive(conn, tmp)
            finally:
                conn.close()
            dest = run_dir(out_root, tmp) + ("" if complete else "-partial")
            shutil.move(tmp, dest)
            size = sum(os.path.getsize(os.path.join(dest, n)) for n in names)
            print("receive: %s sent %d files (%d bytes) -> %s%s" % (peer[0], len(names), size, dest,
                                                                  "" if complete else "  INCOMPLETE: %s" % err),
                  flush=True)
            if do_report:
                report(dest, sim, expect)
            if once:
                return dest
    except KeyboardInterrupt:
        print("\nreceive: stopped")
    finally:
        srv.close()


def self_test():
    """A stream through a local socket: files, an empty one, then a refused name."""
    def send(payload, port):
        s = socket.create_connection(("127.0.0.1", port))
        s.sendall(payload)
        s.close()

    def frame(files):
        out = MAGIC
        for name, data in files:
            out += name.encode() + b"\n" + str(len(data)).encode() + b"\n" + data
        return out + END

    ok = True
    with tempfile.TemporaryDirectory() as root:
        for payload, want_complete, want_names in (
                (frame([("soak.txt", b"VERDICT PASS\n"), ("empty.ppm", b""), ("log.txt", b"a\0b build=abc123\n")]),
                 True, ["soak.txt", "empty.ppm", "log.txt"]),
                (frame([("log.txt", b"x"), ("../evil", b"y")]), False, ["log.txt"]),
                (MAGIC + b"log.txt\n10\nabc", False, [])):
            srv = socket.socket()
            srv.bind(("127.0.0.1", 0))
            srv.listen(1)
            port = srv.getsockname()[1]
            t = threading.Thread(target=send, args=(payload, port))
            t.start()
            conn, _ = srv.accept()
            d = tempfile.mkdtemp(dir=root)
            names, complete, err = receive(conn, d)
            conn.close()
            srv.close()
            t.join()
            good = complete == want_complete and names == want_names
            if good and complete:
                good = open(os.path.join(d, "log.txt"), "rb").read() == b"a\0b build=abc123\n" and \
                    os.path.getsize(os.path.join(d, "empty.ppm")) == 0 and \
                    run_dir(root, d).endswith("-abc123")
            if good and not complete:
                good = not os.path.exists(os.path.join(root, "evil"))
            print("%s  %s complete=%s names=%s err=%s" % ("PASS" if good else "FAIL", payload[:24], complete, names, err))
            ok = ok and good
    print("self-test: %s" % ("OK" if ok else "FAILED"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--out", default=os.path.join(ROOT, "out/3ds"))
    ap.add_argument("--once", action="store_true", help="stop after one run")
    ap.add_argument("--sim", default=None, help="as3d_sim for the end-state comparison (tools/3ds_soak_compare.py)")
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--expect", default=None, metavar="COMMIT",
                    help="the drop's commit: a run of another build is flagged (an old CIA in the HOME menu)")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    serve(a.port, a.bind, a.out, a.once, a.sim, expect=a.expect)
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Scratch check for spec issue 120: projected rectangle of the player helicopter at its
movement limits, with the on-screen rules of G_ComputeScreenBounds (0x405930).

Inputs are spec values (camera mode 1, player z 100, script y band, native x clamp with a
10-unit margin) and the MDL header box of the model. No executable data is embedded.
Usage: scratch_edge_fire.py minx miny minz maxx maxy maxz [bbox_scale]
"""
import math
import sys


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def camera_matrix(cam_x, cam_y, cam_z, pitch_from_down, fov_y, aspect, near=4.0, far=2000.0):
    p = math.radians(pitch_from_down)
    f = (0.0, math.sin(p), -math.cos(p))          # view direction
    u = (0.0, math.cos(p), math.sin(p))           # screen up
    r = (1.0, 0.0, 0.0)                           # screen right
    e = (cam_x, cam_y, cam_z)
    dot = lambda a, b: sum(x * y for x, y in zip(a, b))
    view = [[r[0], r[1], r[2], -dot(r, e)],
            [u[0], u[1], u[2], -dot(u, e)],
            [-f[0], -f[1], -f[2], dot(f, e)],
            [0, 0, 0, 1]]
    t = 1.0 / math.tan(math.radians(fov_y) / 2)
    proj = [[t / aspect, 0, 0, 0], [0, t, 0, 0],
            [0, 0, (far + near) / (near - far), 2 * far * near / (near - far)],
            [0, 0, -1, 0]]
    return mat_mul(proj, view)


def project(c, pt, w, h):
    v = [sum(c[i][k] * (list(pt) + [1.0])[k] for k in range(4)) for i in range(4)]
    return ((w * v[0] / v[3] + w) / 2, (h * v[1] / v[3] + h) / 2)


def clamp_x(c, x, y, z, margin=10.0):
    right = [c[3][k] - c[0][k] for k in range(4)]
    left = [c[3][k] + c[0][k] for k in range(4)]
    solve = lambda pl: (-pl[3] - pl[1] * y - pl[2] * z) / pl[0]
    lo, hi = solve(left) + margin, solve(right) - margin
    return min(max(x, lo), hi), lo, hi


def axis(a14, a15):
    sx, cx = math.sin(math.radians(a14)), math.cos(math.radians(a14))
    sy, cy = math.sin(math.radians(a15)), math.cos(math.radians(a15))
    return [(cy, 0.0, sy), (sx * sy, cx, -sx * cy), (-cx * sy, sx, cx * cy)]


def rect(c, box, s, org, ax, w, h):
    xs, ys = [], []
    for i in range(8):
        X = (box[3] if i & 1 else box[0]) * s
        Y = (box[4] if i & 2 else box[1]) * s
        Z = (box[5] if i & 4 else box[2]) * s
        p = [org[k] + X * ax[0][k] + Y * ax[1][k] + Z * ax[2][k] for k in range(3)]
        sx, sy = project(c, p, w, h)
        xs.append(sx)
        ys.append(sy)
    return min(xs), min(ys), max(xs), max(ys)


def main():
    box = [float(v) for v in sys.argv[1:7]]
    s = float(sys.argv[7]) if len(sys.argv) > 7 else 0.7
    g = 1000.0
    for (w, h) in ((800, 600), (1024, 768)):
        print(f"== {w}x{h}")
        for name, vx, vy, cam_x, want in (
                ("left, moving left", -150, 0, 578, "lo"),
                ("left, at rest", 0, 0, 578, "lo"),
                ("right, moving right", 150, 0, 702, "hi"),
                ("right, at rest", 0, 0, 702, "hi"),
                ("bottom, moving back", 0, -150, 640, None),
                ("bottom, at rest", 0, 0, 640, None),
                ("bottom-left corner, moving", -150, -150, 578, "lo")):
            y = g + 35.0
            c = camera_matrix(cam_x, g, 270.0, 45.0, 60.0, w / h)
            _, lo, hi = clamp_x(c, 640.0, y, 100.0)
            x = {"lo": lo, "hi": hi, None: 640.0}[want]
            ax = axis(30.0 * vy / 150.0, -25.0 * vx / 150.0)
            r = rect(c, box, s, (x, y, 100.0), ax, w, h)
            ox, oy = project(c, (x, y, 100.0), w, h)
            inside = r[0] >= 0 and r[1] >= 0 and r[2] < w and r[3] < h
            overlap = r[2] >= 0 and r[0] < w and r[3] >= 0 and r[1] < h
            print(f"{name:28s} x={x:7.2f} origin px=({ox:7.2f},{oy:7.2f}) "
                  f"rect=({r[0]:7.2f},{r[1]:7.2f})-({r[2]:7.2f},{r[3]:7.2f}) "
                  f"inside={inside} overlap={overlap}")
        # middle of the band for reference, and the clamp bounds at the top of the band
        c = camera_matrix(640, g, 270.0, 45.0, 60.0, w / h)
        for yy in (35.0, 320.0):
            _, lo, hi = clamp_x(c, 640.0, g + yy, 100.0)
            print(f"clamp at y=g+{yy:.0f}, camera x 640: x in [{lo:.2f}, {hi:.2f}]")


if __name__ == "__main__":
    main()

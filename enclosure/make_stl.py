#!/usr/bin/env python3
"""Build STL files for the knob puck without OpenSCAD (Python 3 stdlib only).

Reads the [params-begin]..[params-end] block of knob_puck.scad, so edit the
numbers there and re-run this script. Every part in this design is 2.5D
(vertical walls, flat floors), so each part is described here as a stack of
z-slabs with a 2D signed-distance section per slab. A surface-nets mesher with
sharp-edge vertex placement turns that into a closed, consistently oriented
triangle mesh; the script checks that every edge has exactly two faces.

Usage:
    python3 make_stl.py                 # all parts, all variants, into ./stl/
    python3 make_stl.py shell lid_a     # just these
    python3 make_stl.py --res 0.2       # finer grid (slower, bigger files)

Outputs (binary STL, millimetres, already in print orientation):
    shell.stl  washer.stl  lid_a.stl  lid_b.stl  dock_b.stl  lid_c.stl
    lid_d.stl  riser_d.stl
The geometry mirrors the modules in knob_puck.scad; if you change a module
there, change the matching function here.
"""
import argparse
import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- params
class P(dict):
    __getattr__ = dict.__getitem__


def load_params(variant):
    src = open(os.path.join(HERE, "knob_puck.scad"), encoding="utf-8").read()
    block = src.split("// [params-begin]")[1].split("// [params-end]")[0]
    ns = {}
    for line in block.splitlines():
        line = line.split("//")[0]
        for stmt in line.split(";"):
            if "=" not in stmt:
                continue
            k, v = stmt.split("=", 1)
            k = k.strip()
            # eval is deliberate: the input is this repo's own .scad parameter
            # block (numbers, lists, arithmetic), evaluated without builtins.
            ns[k] = variant if k == "variant" else eval(v, {"__builtins__": {}}, ns)
    return P(ns)


# ---------------------------------------------------------------- 2D fields
class Grid:
    def __init__(self, x0, x1, y0, y1, res):
        self.nx = int(math.ceil((x1 - x0) / res)) + 1
        self.ny = int(math.ceil((y1 - y0) / res)) + 1
        self.xs = [x0 + i * res for i in range(self.nx)]
        self.ys = [y0 + j * res for j in range(self.ny)]
        self.px = [x for _ in self.ys for x in self.xs]
        self.py = [y for y in self.ys for _ in self.xs]
        self.cache = {}

    def circle(self, cx, cy, r):
        key = ("c", cx, cy, r)
        if key not in self.cache:
            h = math.hypot
            self.cache[key] = [h(x - cx, y - cy) - r for x, y in zip(self.px, self.py)]
        return self.cache[key]

    def capsule(self, x0, y0, x1, y1, r):
        key = ("k", x0, y0, x1, y1, r)
        if key not in self.cache:
            dx, dy = x1 - x0, y1 - y0
            ll = dx * dx + dy * dy
            out = []
            for x, y in zip(self.px, self.py):
                t = max(0.0, min(1.0, ((x - x0) * dx + (y - y0) * dy) / ll))
                out.append(math.hypot(x - x0 - t * dx, y - y0 - t * dy) - r)
            self.cache[key] = out
        return self.cache[key]

    def box(self, x0, y0, x1, y1, r=0.0):
        key = ("b", x0, y0, x1, y1, r)
        if key not in self.cache:
            cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
            hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
            out = []
            for x, y in zip(self.px, self.py):
                qx = abs(x - cx) - hx
                qy = abs(y - cy) - hy
                out.append(math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r)
            self.cache[key] = out
        return self.cache[key]


def U(*fs):
    fs = [f for f in fs if f is not None]
    if len(fs) == 1:
        return fs[0]
    return [min(t) for t in zip(*fs)]


def D(a, *bs):
    bs = [b for b in bs if b is not None]
    if not bs:
        return a
    b = U(*bs)
    return [max(p, -q) for p, q in zip(a, b)]


def ring(g, ro, ri):
    return D(g.circle(0, 0, ro), g.circle(0, 0, ri))


# ---------------------------------------------------------------- parts
# Each returns (bounds, section, extent): bounds = sorted slab z values,
# section(z) -> field over the grid for a z strictly inside a slab.

def part_shell(p, g):
    R = p.puck_d / 2
    disk = g.circle(0, 0, R)
    cav = U(g.box(-p.cav_x, p.cav_y0, p.cav_x, p.cav_y1, p.cav_r),
            g.box(p.cav_x - 1, p.usb_yc - p.plug_w / 2, p.cav_x - 1 + p.puck_d, p.usb_yc + p.plug_w / 2))
    lidp = screw_ring(g, p.lid_scr_r, p.lid_pilot)

    def sec(z):
        if z < p.z_ceil:
            cuts = [cav, lidp if z < p.lid_pilot_len else None]
        elif z < p.z_lt:
            cuts = [g.circle(0, 0, p.ledge_hd / 2) if z > p.z_ceil + p.membrane else None]
        elif z < p.z_deck:
            cuts = [g.circle(0, 0, p.bore_d / 2)]
        else:
            cuts = [g.circle(0, 0, p.pocket_d / 2)]
        return D(disk, *cuts)

    return [0, p.lid_pilot_len, p.z_ceil, p.z_ceil + p.membrane, p.z_lt, p.z_deck, p.shell_h], sec, R


def part_washer(p, g):
    gd = p.pin_h + 0.4
    base = D(g.circle(0, 0, p.washer_d / 2), g.circle(0, 0, p.fpc_hole / 2),
             *[g.capsule(r0 * math.cos(math.radians(a)), r0 * math.sin(math.radians(a)),
                         r1 * math.cos(math.radians(a)), r1 * math.sin(math.radians(a)), p.m25_hole / 2)
               for a in (90, 210, 330)
               for r0, r1 in [(min(p.scr_pcd, p.pin_pcd) / 2, max(p.scr_pcd, p.pin_pcd) / 2)]])
    groove = ring(g, (max(p.scr_pcd, p.pin_pcd) + p.pin_d + 1.0) / 2, (min(p.scr_pcd, p.pin_pcd) - p.pin_d - 1.0) / 2)

    def sec(z):
        return D(base, groove) if z > p.washer_t - gd else base

    return [0, p.washer_t - gd, p.washer_t], sec, p.washer_d / 2


def screw_ring(g, r, d, angles=(90, 180, 270)):
    return U(*[g.circle(r * math.cos(math.radians(a)), r * math.sin(math.radians(a)), d / 2) for a in angles])


def part_lid(p, g):
    R = p.puck_d / 2
    t = p.lid_t
    disk = g.circle(0, 0, R)
    holes = screw_ring(g, p.lid_scr_r, p.lid_hole)
    cbs = screw_ring(g, p.lid_scr_r, p.lid_cb_d)
    pts = [(h[0] - p.ad_w / 2, h[1] - p.ad_d / 2 + p.ad_y) for h in p.ad_holes]
    posts = U(*[g.circle(x, y, p.post_d / 2) for x, y in pts])
    pilots = U(*[g.circle(x, y, p.post_pilot / 2) for x, y in pts])
    low, z_low, through = None, 0, None
    if p.variant == 0:
        low, z_low = screw_ring(g, p.foot_r, p.foot_d, (45, 135, 225, 315)), p.foot_h
    elif p.variant == 1:
        low, z_low = screw_ring(g, p.mag_r, p.mag_d + p.mag_fit, (45, 135, 225, 315)), p.mag_h + 0.1
        through = U(g.box(p.pogo_x - p.pogo_l / 2 - p.tol, p.pogo_y - p.pogo_w / 2 - p.tol,
                          p.pogo_x + p.pogo_l / 2 + p.tol, p.pogo_y + p.pogo_w / 2 + p.tol),
                    g.box(R - p.key_d, -p.key_w / 2, R + 1, p.key_w / 2))
    elif p.variant == 2:
        low = g.box(-(p.qi_l / 2 + p.tol), -(p.qi_w / 2 + p.tol), p.qi_l / 2 + p.tol, p.qi_w / 2 + p.tol)
        z_low = p.qi_h
        through = g.circle(-(p.qi_l / 2 - 3), 3, p.qi_wire / 2)
    else:
        through = g.circle(-12, 0, 3)

    def sec(z):
        if z > t:
            return D(posts, pilots)
        cuts = [holes, through]
        if z < p.lid_cb_h:
            cuts.append(cbs)
        if low is not None and z < z_low:
            cuts.append(low)
        if z > t - 1:
            cuts.append(pilots)
        return D(disk, *cuts)

    return sorted({0, z_low, p.lid_cb_h, t - 1, t, t + p.under}), sec, R


def part_dock(p, g):
    R = p.dock_d / 2
    zf = p.dock_h - p.dock_recess
    disk = g.circle(0, 0, R)
    seat = D(g.circle(0, 0, p.puck_d / 2 + p.tol),
             g.box(p.puck_d / 2 - p.key_d + p.tol, -p.key_w / 2 + p.tol, p.puck_d / 2 + 2, p.key_w / 2 - p.tol))
    mags = screw_ring(g, p.mag_r, p.mag_d + p.mag_fit, (45, 135, 225, 315))
    pogo = g.box(p.pogo_x - p.pogo_l / 2 - p.tol, p.pogo_y - p.pogo_w / 2 - p.tol,
                 p.pogo_x + p.pogo_l / 2 + p.tol, p.pogo_y + p.pogo_w / 2 + p.tol)
    low = U(g.box(p.pogo_x - 6, p.pogo_y - 5, R - 3, p.pogo_y + 5, 1),
            g.box(R - 3 - p.brk_l, -p.brk_w / 2, R - 3, p.brk_w / 2, 1),
            g.box(R - 4, -p.plug_w / 2, R + 6, p.plug_w / 2),
            g.box(-R + 8, -14, -10, 14, 2))
    zm = zf - p.mag_h - 0.1

    def sec(z):
        cuts = []
        if z < zf:
            cuts.append(pogo)
        if z < 6:
            cuts.append(low)
        if zm < z < zf:
            cuts.append(mags)
        if z > zf:
            cuts.append(seat)
        return D(disk, *cuts)

    return [0, 6, zm, zf, p.dock_h], sec, R


def part_riser(p, g):
    R = p.riser_d / 2
    disk = g.circle(0, 0, R)
    bay = D(g.circle(0, 0, R - p.riser_wall), screw_ring(g, p.lid_scr_r, 7))
    holes = screw_ring(g, p.lid_scr_r, p.lid_hole)
    cbs = screw_ring(g, p.lid_scr_r, p.lid_cb_d)
    feet = screw_ring(g, 30, p.foot_d, (45, 135, 225, 315))
    port = g.box(12.5, -R - 1, 24.5, -24)
    switch = g.box(R - p.riser_wall - 2, -4.75, R + 1, 4.75)
    zp = p.riser_floor + 1

    def sec(z):
        cuts = [holes]
        if z < 1.3:
            cuts.append(cbs)
        if z < p.foot_h:
            cuts.append(feet)
        if z > p.riser_floor:
            cuts.append(bay)
        if z > zp:
            cuts.append(port)
        if z > 4:
            cuts.append(switch)
        return D(disk, *cuts)

    return sorted({0, p.foot_h, 1.3, p.riser_floor, zp, 4, p.riser_h}), sec, R


PARTS = {
    "shell": (0, part_shell),
    "washer": (0, part_washer),
    "lid_a": (0, part_lid),
    "lid_b": (1, part_lid),
    "dock_b": (1, part_dock),
    "lid_c": (2, part_lid),
    "lid_d": (3, part_lid),
    "riser_d": (3, part_riser),
}


# ---------------------------------------------------------------- mesher
def mesh_part(builder, p, res):
    # extent first (cheap dummy grid), then the real grid
    _, _, R = builder(p, Grid(0, 1, 0, 1, 1))
    pad = 2 * res
    g = Grid(-R - pad, R + pad, -R - pad, R + pad, res)
    bounds, sec, _ = builder(p, g)
    bounds = sorted(set(round(b, 6) for b in bounds))
    dz = min(0.02, min(b1 - b0 for b0, b1 in zip(bounds, bounds[1:])) / 4)
    levels, lev_slab, lev_h = [], [], []
    for k, h in enumerate(bounds):
        levels += [h - dz, h + dz]
        lev_slab += [k - 1, k]          # slab index valid for this level (-1 / len-1 = outside)
    nslab = len(bounds) - 1
    fields = {}
    outside = [1.0] * len(g.px)

    def field(s):
        if s < 0 or s >= nslab:
            return outside
        if s not in fields:
            fields[s] = sec((bounds[s] + bounds[s + 1]) / 2)
        return fields[s]

    F = [field(s) for s in lev_slab]
    nx, ny, nz = g.nx, g.ny, len(levels)
    xs, ys = g.xs, g.ys
    # boundary height between level k and k+1 (only meaningful when they straddle one)
    zcross = [bounds[k // 2] if k % 2 == 0 else None for k in range(nz - 1)]

    verts = {}
    vlist = []

    def vid(i, j, k):
        key = (i, j, k)
        v = verts.get(key)
        if v is not None:
            return v
        sx = sy = 0.0
        n = 0
        zc = None
        for kk in (k, k + 1):
            f = F[kk]
            for (a, b) in (((i, j), (i + 1, j)), ((i, j + 1), (i + 1, j + 1)),
                           ((i, j), (i, j + 1)), ((i + 1, j), (i + 1, j + 1))):
                fa = f[a[1] * nx + a[0]]
                fb = f[b[1] * nx + b[0]]
                if (fa < 0) != (fb < 0):
                    t = fa / (fa - fb)
                    sx += xs[a[0]] + t * (xs[b[0]] - xs[a[0]])
                    sy += ys[a[1]] + t * (ys[b[1]] - ys[a[1]])
                    n += 1
        f0, f1 = F[k], F[k + 1]
        for (ii, jj) in ((i, j), (i + 1, j), (i, j + 1), (i + 1, j + 1)):
            q = jj * nx + ii
            if (f0[q] < 0) != (f1[q] < 0):
                zc = zcross[k]
        if n:
            x, y = sx / n, sy / n
        else:
            x, y = (xs[i] + xs[i + 1]) / 2, (ys[j] + ys[j + 1]) / 2
        z = zc if zc is not None else (levels[k] + levels[k + 1]) / 2
        v = len(vlist)
        vlist.append((x, y, z))
        verts[key] = v
        return v

    quads = []
    for k in range(1, nz - 1):
        f = F[k]
        for j in range(1, ny - 1):
            row = j * nx
            for i in range(0, nx - 1):
                a, b = f[row + i] < 0, f[row + i + 1] < 0
                if a != b:   # x-edge
                    q = [vid(i, j - 1, k - 1), vid(i, j, k - 1), vid(i, j, k), vid(i, j - 1, k)]
                    quads.append(q if a else q[::-1])
    for k in range(1, nz - 1):
        f = F[k]
        for j in range(0, ny - 1):
            row = j * nx
            for i in range(1, nx - 1):
                a, b = f[row + i] < 0, f[row + nx + i] < 0
                if a != b:   # y-edge
                    q = [vid(i - 1, j, k - 1), vid(i - 1, j, k), vid(i, j, k), vid(i, j, k - 1)]
                    quads.append(q if a else q[::-1])
    for k in range(0, nz - 1):
        f0, f1 = F[k], F[k + 1]
        if f0 is f1:
            continue
        for j in range(1, ny - 1):
            row = j * nx
            for i in range(1, nx - 1):
                a, b = f0[row + i] < 0, f1[row + i] < 0
                if a != b:   # z-edge
                    q = [vid(i - 1, j - 1, k), vid(i, j - 1, k), vid(i, j, k), vid(i - 1, j, k)]
                    quads.append(q if a else q[::-1])
    tris = []
    for a, b, c, d in quads:
        tris.append((a, b, c))
        tris.append((a, c, d))
    return vlist, tris


def check(vlist, tris):
    edges = {}
    for t in tris:
        for u, v in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            edges[(u, v)] = edges.get((u, v), 0) + 1
    bad = sum(1 for (u, v), n in edges.items() if n != 1 or edges.get((v, u), 0) != 1)
    vol = 0.0
    for a, b, c in tris:
        (x1, y1, z1), (x2, y2, z2), (x3, y3, z3) = vlist[a], vlist[b], vlist[c]
        vol += (x1 * (y2 * z3 - y3 * z2) - x2 * (y1 * z3 - y3 * z1) + x3 * (y1 * z2 - y2 * z1)) / 6
    return bad, vol


def write_stl(path, vlist, tris, name):
    with open(path, "wb") as fh:
        fh.write(name.encode()[:80].ljust(80, b" "))
        fh.write(struct.pack("<I", len(tris)))
        for a, b, c in tris:
            p1, p2, p3 = vlist[a], vlist[b], vlist[c]
            ux, uy, uz = p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]
            vx, vy, vz = p3[0] - p1[0], p3[1] - p1[1], p3[2] - p1[2]
            nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
            ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
            fh.write(struct.pack("<12fH", nx / ln, ny / ln, nz / ln, *p1, *p2, *p3, 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("parts", nargs="*", default=list(PARTS))
    ap.add_argument("--res", type=float, default=0.3, help="grid step in mm (default 0.3)")
    ap.add_argument("--out", default=os.path.join(HERE, "stl"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    ok = True
    for name in a.parts:
        if name not in PARTS:
            sys.exit("unknown part %s; choose from %s" % (name, ", ".join(PARTS)))
        variant, builder = PARTS[name]
        p = load_params(variant)
        t0 = time.time()
        res = min(a.res, 0.15) if name == "washer" else a.res
        vlist, tris = mesh_part(builder, p, res)
        bad, vol = check(vlist, tris)
        path = os.path.join(a.out, name + ".stl")
        write_stl(path, vlist, tris, "knob_puck " + name)
        ok &= bad == 0 and vol > 0
        print("%-8s %7d tris  %8.1f cm3  bad edges %d  %.1fs  -> %s"
              % (name, len(tris), vol / 1000, bad, time.time() - t0, os.path.relpath(path)))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

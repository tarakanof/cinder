import math, os
import edit as E

ORIG = E.ORIG
GAUGE_ROT = ['135', '157.5', '180', '202.5', '225', '247.5', '270', '292.5', '315', '337.5', '0', '22.5']
OLD_STROKES = ['#3A4048', '#484E57', '#555C65', '#636B74', '#717982', '#7E8791', '#8C959F', '#99A3AE', '#A7B1BC',
               '#B5C0CB', '#C2CED9', '#D0DCE8']
BANDS = [(5, '#9AA4B0', 'Calm'), (11, '#4FD6CC', 'Light breeze'), (19, '#8ACF7A', 'Gentle breeze'),
         (28, '#C9D25A', 'Moderate breeze'), (38, '#E8D44A', 'Fresh breeze'), (49, '#F08A3A', 'Strong breeze'),
         (61, '#FF5050', 'Near gale'), (10 ** 9, '#D03060', 'Gale')]


def band(v):
    for hi, c, name in BANDS:
        if v <= hi:
            if name == 'Calm' and v >= 1:
                name = 'Light air'
            return c, name


def wc(v):
    return band(v)[0]


def shade(hexc, f=0.55):
    r, g, b = E.hex2rgb(hexc)
    return E.rgb2hex((r * f, g * f, b * f))


def marker_xy(v):
    a = math.radians(225 + 270 * min(v, 50) / 50)
    return round(233 + 222 * math.sin(a), 2), round(233 - 222 * math.cos(a), 2)


def recolour_gauge(s):
    for i, (rot, old) in enumerate(zip(GAUGE_ROT, OLD_STROKES)):
        centre = 50 * (i + 0.5) / 12
        s = E.sub_once(s, f'transform="rotate({rot} 233 233)" style="fill: none; stroke: {old};',
                       f'transform="rotate({rot} 233 233)" style="fill: none; stroke: {wc(centre)};')
    s = E.sub_once(s, '<circle cx="76.02" cy="389.98" r="6" style="fill: #3A4048">',
                   f'<circle cx="76.02" cy="389.98" r="6" style="fill: {wc(50 * 0.5 / 12)}">')
    s = E.sub_once(s, '<circle cx="389.98" cy="389.98" r="6" style="fill: #D0DCE8">',
                   f'<circle cx="389.98" cy="389.98" r="6" style="fill: {wc(50 * 11.5 / 12)}">')
    s = E.sub_once(s, 'color: #8A8A8A">0</div>', f'color: {wc(0)}">0</div>')
    s = E.sub_once(s, 'color: #D0DCE8">50+</div>', f'color: {wc(50)}">50+</div>')
    return s


def subline(v, direction):
    c, name = band(v)
    return (f'<div style="font-size: 30px; line-height: 36px; color: #BDBDBD; white-space: nowrap">'
            f'<span style="color: {c}">{name}</span> · {direction}</div>')


def colour_kf(name, prop, vals, fn):
    n = len(vals) - 1
    parts = []
    for k, v in enumerate(vals):
        t = 1 - (1 - k / n) ** (1 / 3)
        sel = E.fmt(E.ARRIVE_END * t) + '%' + (',100%' if k == n else '')
        parts.append(f'{sel}{{{prop}:{fn(v)};animation-timing-function:step-end}}')
    return '@keyframes ' + name + '{' + ''.join(parts) + '}'


L, NSTR = 65.0, 5
MOUNT = (215.0, 134.0)


def sock_geometry(v, k=1.0, mount=None):
    mount = mount or MOUNT
    n = v / 5.5
    droop = 75 * (1 - min(v, 28) / 28)
    s_len = L * k / NSTR
    cs = [max(0.0, min(1.0, n - m)) for m in range(NSTR)]
    bends, acc = [], 0.0
    for m in range(NSTR):
        acc += (1 - cs[m]) * 14
        bends.append(min(acc, 88 - droop))
    pts, phis = [(0.0, 0.0)], []
    for m in range(NSTR):
        phi = math.radians(bends[m] if m else bends[0] * 0.5)
        phis.append(phi)
        x, y = pts[-1]
        pts.append((x + s_len * math.cos(phi), y + s_len * math.sin(phi)))
    widths = []
    for j in range(NSTR + 1):
        full = (12 - 7 * (j * s_len) / (L * k)) * k
        b = 1.0 if j == 0 else max(0.0, min(1.0, n - j + 1))
        widths.append(full * (0.45 + 0.55 * b))
    polys = []
    for m in range(NSTR):
        quad = []
        for j in (m, m + 1):
            if j == 0:
                phi = phis[0]
            elif j == NSTR:
                phi = phis[-1]
            else:
                phi = (phis[j - 1] + phis[j]) / 2
            nx, ny = math.sin(phi), -math.cos(phi)
            px, py = pts[j]
            quad.append(((px + widths[j] * nx), (py + widths[j] * ny), (px - widths[j] * nx), (py - widths[j] * ny)))
        (ax, ay, bx, by), (cx, cy, dx, dy) = quad
        P = [(ax, ay), (cx, cy), (dx, dy), (bx, by)]
        polys.append(' '.join(f'{E.fmt(round(mount[0] + x, 2))},{E.fmt(round(mount[1] + y, 2))}' for x, y in P))
    f = min(v, 45) / 45
    flap = dict(period=round(3.0 - 2.6 * f, 2), amp=round(7 - 5.5 * f, 2), stretch=round(0.01 + 0.03 * f, 3))
    return dict(n=round(n, 2), droop=round(droop, 1), bends=[round(b, 1) for b in bends], polys=polys, flap=flap,
                inflated=sum(1 for c in cs if c >= 1), partial=[round(c, 2) for c in cs])


OLD_SOCK_KF = '@keyframes wxsock{0%{transform:rotate(9deg) scaleX(0.9)}25%{transform:rotate(2deg) scaleX(1)}50%{transform:rotate(6deg) scaleX(0.95)}75%{transform:rotate(0deg) scaleX(1.03)}100%{transform:rotate(9deg) scaleX(0.9)}}\n'
OLD_SOCK = '''<g style="transform-origin: 202px 134px; transform: rotate(5deg); animation: wxsock 1.3s ease-in-out infinite">
<polygon points="204,122 228,124.33 228,143.67 204,146" style="fill: #FF8A3D"></polygon>
<polygon points="228,124.33 252,126.67 252,141.33 228,143.67" style="fill: #F4F4F2"></polygon>
<polygon points="252,126.67 276,129 276,139 252,141.33" style="fill: #FF8A3D"></polygon>
</g>'''
OLD_STREAKS = '''<line x1="236" y1="160" x2="262" y2="160" style="stroke: #9AA4B0; stroke-width: 3; stroke-linecap: round; animation: wxstreak 1.6s linear infinite"></line>
<line x1="246" y1="182" x2="266" y2="182" style="stroke: #9AA4B0; stroke-width: 3; stroke-linecap: round; animation: wxstreak 1.6s linear -0.8s infinite"></line>
'''


def sock_face(name, v, direction, title=None, streaks=(), streak_period=1.6, labels=None, write=True):
    s = open(os.path.join(ORIG, 'AW2.dc.html')).read()
    mx, my = marker_xy(v)
    s = s.replace('cx="96.93" cy="57.59"', f'cx="{mx}" cy="{my}"')
    d, th = E.delta_for(mx, my)
    vals = E.count_values(v)
    n = len(vals) - 1
    final = wc(v)
    g = sock_geometry(v)
    fl = g['flap']
    a = fl['amp']
    kfs = [E.arrive_kf(d), E.count_kf(n),
           colour_kf('wxsockfill', 'fill', vals, wc), colour_kf('wxsockdark', 'fill', vals, lambda x: shade(wc(x))),
           '@keyframes wxsockin{0%{transform:rotate(75deg) scale(0.92,0.5);animation-timing-function:' + E.EASE + '}'
           f'22%,100%{{transform:rotate({E.fmt(g["droop"])}deg) scale(1,1)}}}}',
           f'@keyframes wxsock{{0%,100%{{transform:rotate({E.fmt(-a)}deg) scaleX({E.fmt(1 - fl["stretch"])})}}'
           f'50%{{transform:rotate({E.fmt(a)}deg) scaleX({E.fmt(1 + fl["stretch"])})}}}}']
    s = E.sub_once(s, OLD_SOCK_KF, '')
    s = E.add_keyframes(s, kfs)
    s = E.wrap_marker(s, mx, my)
    if title:
        s = E.sub_once(s, '<title>Wind, windsock</title>', f'<title>{title}</title>')
    s = recolour_gauge(s)
    polys = []
    for i, pts in enumerate(g['polys']):
        c, anim = (final, 'wxsockfill') if i % 2 == 0 else (shade(final), 'wxsockdark')
        polys.append(f'<polygon points="{pts}" style="fill: {c}; animation: {anim} 6s infinite"></polygon>')
    sock = (f'<g style="transform-origin: 215px 134px; transform: rotate({E.fmt(g["droop"])}deg); animation: wxsockin 6s linear infinite">\n'
            f'<g style="transform-origin: 215px 134px; animation: wxsock {fl["period"]}s ease-in-out infinite">\n'
            + '\n'.join(polys) + '\n</g>\n</g>')
    s = E.sub_once(s, OLD_SOCK, sock)
    st = ''.join(f'<line x1="{x1}" y1="{y}" x2="{x2}" y2="{y}" style="stroke: #9AA4B0; stroke-width: 3; stroke-linecap: round; '
                 f'animation: wxstreak {streak_period}s linear {E.fmt(-i * streak_period / max(1, len(streaks)))}s infinite"></line>\n'
                 for i, (x1, x2, y) in enumerate(streaks))
    s = E.sub_once(s, OLD_STREAKS, st)
    pole = '<line x1="200" y1="116" x2="200" y2="204" style="stroke: #9AA4B0; stroke-width: 5; stroke-linecap: round"></line>'
    s = E.sub_once(s, pole, pole + '\n<line x1="200" y1="134" x2="213" y2="134" style="stroke: #9AA4B0; stroke-width: 4; stroke-linecap: round"></line>')
    style = f'font-size: 96px; line-height: 84px; letter-spacing: -2px; color: {final}'
    s = E.sub_once(s, '<div style="font-size: 96px; line-height: 84px; letter-spacing: -2px">18</div>',
                   E.counter_wrapper(style, v, vals, wc) + '\n</div>')
    s = E.sub_once(s, '<div style="font-size: 30px; line-height: 36px; color: #BDBDBD">SW, gusts 34</div>', subline(v, direction))
    bname = band(v)[1].lower()
    s = E.sub_once(s, 'aria-label="Wind speed gauge from calm to 50 kilometres per hour and more, with a ring marking 18"',
                   f'aria-label="Wind speed gauge from calm to 50 kilometres per hour and more, coloured by Beaufort band, '
                   f'the marker sweeping up to the current {v}, a {bname}, as the face appears"')
    s = E.sub_once(s, 'aria-label="A striped windsock on a pole, flapping in the wind"', f'aria-label="{labels}"')
    s = E.end_labels(s)
    if not write:
        return s
    open(os.path.join(E.PROJ, name + '.dc.html'), 'w').write(s)
    print(name, dict(value=v, marker=(mx, my), clock=round(th, 2), delta=d, frames=n, vals=vals, colour=final,
                     dark=shade(final), sock={k: g[k] for k in ('n', 'droop', 'bends', 'inflated', 'partial', 'flap')}))
    return g


import re
HK = 1.9
HBASE = 205
HMOUNT_UP = 104
HARM = 22
POLE_TOP_ABOVE = 26
FRAME = 1 / 15
HSTART = (0.8, 0.5)
LEAVES = [(-10, 1.2, 0), (64, 1.6, -0.7)]
HERO = {
    'AW3': (10, 5, 0.012, [(40, 14, 22, 3.0, 0)], 0),
    'AW2': (5, 4, 0.02, [(-34, 30, 34, 1.6, 0), (64, 46, 30, 1.6, -0.53), (86, 22, 26, 1.6, -1.07)], 0),
    'AW4': (2, 3, 0.03, [(-26, 18, 36, 0.8, 0), (34, 40, 32, 0.8, -0.2), (54, 14, 30, 0.8, -0.4), (72, 52, 28, 0.8, -0.6)], 2),
}


def _xf(px, py, mount, droop, ang=0.0, sx=1.0, sy=1.0):
    x, y = px * sx, py * sy
    a = math.radians(ang + droop)
    return mount[0] + x * math.cos(a) - y * math.sin(a), mount[1] + x * math.sin(a) + y * math.cos(a)


def hero_sock_face(name, v, direction, title, streaks, streak_period, labels):
    base = sock_face(name, v, direction, title=title, streaks=streaks, streak_period=streak_period, labels=labels, write=False)
    pose_frames, ang, st, rows, leaves = HERO[name]
    mount_y = HBASE - HMOUNT_UP
    g0 = sock_geometry(v, HK, (0.0, 0.0))
    droop = g0['droop']
    local = [[tuple(map(float, p.split(','))) for p in poly.split(' ')] for poly in g0['polys']]
    poses = {'A': (-ang, 1 - st, 1.02), 'B': (0.0, 1.0, 1.0), 'C': (ang, 1 + st, 0.96)}
    xs = [_xf(px, py, (0, 0), droop, a_, sx, sy)[0] for poly in local for px, py in poly for a_, sx, sy in poses.values()]
    lo, hi = min(min(xs), -HARM - 22), max(xs)
    hoop_x = round(233 - (lo + hi) / 2, 1)
    pole_x = hoop_x - HARM
    def all_pts(mount):
        pts = [_xf(px, py, mount, droop, a_, sx, sy) for poly in local for px, py in poly for a_, sx, sy in poses.values()]
        return pts + [_xf(px * HSTART[0], py * HSTART[1], mount, 75) for poly in local for px, py in poly]
    mount_y = min(mount_y, math.floor(HBASE - 3 - max(y for x, y in all_pts((0.0, 0.0)))))
    mount = (hoop_x, mount_y)
    pts_all = all_pts(mount)
    assert max(y for x, y in pts_all) <= HBASE - 2, (name, 'sock too low', max(y for x, y in pts_all))
    mx, my = marker_xy(v)
    final = wc(v)
    def poly_svg():
        out = []
        for i, poly in enumerate(local):
            pts = ' '.join(f'{E.fmt(round(mount[0] + px, 2))},{E.fmt(round(mount[1] + py, 2))}' for px, py in poly)
            c, anim = (final, 'wxsockfill') if i % 2 == 0 else (shade(final), 'wxsockdark')
            out.append(f'<polygon points="{pts}" style="fill: {c}; animation: {anim} 6s infinite"></polygon>')
        return '\n'.join(out)
    cycle = round(4 * pose_frames * FRAME, 3)
    vis = {'A': '0%{opacity:1;animation-timing-function:step-end}25%,100%{opacity:0}',
           'B': '0%{opacity:0;animation-timing-function:step-end}25%{opacity:1;animation-timing-function:step-end}50%{opacity:0;animation-timing-function:step-end}75%,100%{opacity:1}',
           'C': '0%{opacity:0;animation-timing-function:step-end}50%{opacity:1;animation-timing-function:step-end}75%,100%{opacity:0}'}
    pose_g = ''.join(f'<g style="transform-origin: {mount[0]}px {mount[1]}px; transform: {"none" if p == "B" else f"rotate({a_}deg) scale({sx}, {sy})"}; '
                     f'opacity: {1 if p == "B" else 0}; animation: wxpose{p} {cycle}s linear infinite">\n{poly_svg()}\n</g>'
                     for p, (a_, sx, sy) in poses.items())
    sock = (f'<g style="transform-origin: {mount[0]}px {mount[1]}px; transform: rotate({E.fmt(droop)}deg); animation: wxsockin 6s linear infinite">\n'
            + pose_g + '\n</g>')
    pole = (f'<rect x="{pole_x - 3.5}" y="{mount_y - POLE_TOP_ABOVE}" width="7" height="{HBASE - 6 - (mount_y - POLE_TOP_ABOVE)}" rx="3.5" style="fill: #9AA4B0"></rect>'
            f'<circle cx="{pole_x}" cy="{mount_y - POLE_TOP_ABOVE}" r="5.5" style="fill: #9AA4B0"></circle>'
            f'<rect x="{pole_x}" y="{mount_y - 3}" width="{HARM}" height="6" rx="3" style="fill: #9AA4B0"></rect>'
            f'<rect x="{pole_x - 24}" y="{HBASE - 7}" width="48" height="7" rx="3.5" style="fill: #9AA4B0"></rect>')
    streak_svg, kfs = [], []
    for i, (dy, x0, ln, per, ph) in enumerate(rows):
        y = mount_y + dy
        x1 = hoop_x + x0
        streak_svg.append(f'<line x1="{x1}" y1="{y}" x2="{x1 + ln}" y2="{y}" style="stroke: #9AA4B0; stroke-width: 4; stroke-linecap: round; opacity: 0; '
                          f'animation: wxstreak {per}s steps({max(1, round(per / 2 * 15))}) {ph}s infinite"></line>')
    leaf_svg = ''
    if leaves:
        lp = 'M 0 0 C 4 -5, 12 -5, 16 0 C 12 5, 4 5, 0 0 Z'
        for i, (y0, per, ph) in enumerate([(mount_y + dy, per, ph) for dy, per, ph in LEAVES][:leaves]):
            kfs.append(f'@keyframes wxleaf{i}{{0%{{transform:translate(0px,0px) rotate(0deg);opacity:0}}12%{{opacity:1}}50%{{transform:translate(70px,-10px) rotate(160deg)}}'
                       f'88%{{opacity:1}}100%{{transform:translate(140px,6px) rotate(330deg);opacity:0}}}}')
            lx = hoop_x - 6
            leaf_svg += (f'<g style="transform-box: fill-box; transform-origin: center; opacity: 0; animation: wxleaf{i} {per}s steps({round(per / 2 * 15)}) {ph}s infinite">'
                         f'<path d="{lp}" transform="translate({lx} {y0})" style="fill: #8ACF7A"></path></g>')
    kfs += [f'@keyframes wxpose{p}{{{vis[p]}}}' for p in 'ABC']
    kfs += ['@keyframes wxstreak{0%{transform:translateX(-24px);opacity:0}50%{transform:translateX(16px);opacity:0.75}100%{transform:translateX(56px);opacity:0}}',
            f'@keyframes wxsockin{{0%{{transform:rotate(75deg) scale({HSTART[0]},{HSTART[1]});animation-timing-function:' + E.EASE + '}'
            f'22%,100%{{transform:rotate({E.fmt(droop)}deg) scale(1,1)}}}}']
    bx0 = min([x for x, y in pts_all] + [hoop_x + r[1] - 24 for r in rows]) - 4
    bx1 = max([x for x, y in pts_all] + [hoop_x + r[1] + r[2] + 56 for r in rows] + ([hoop_x + 150] if leaves else [])) + 4
    by0 = min([y for x, y in pts_all] + [mount_y + r[0] for r in rows] + [mount_y + dy - 18 for dy, _, _ in LEAVES[:leaves]]) - 6
    by1 = max([y for x, y in pts_all] + [mount_y + r[0] for r in rows] + [mount_y + dy + 14 for dy, _, _ in LEAVES[:leaves]]) + 6
    corners = [(bx0, by0), (bx1, by0), (bx0, by1), (bx1, by1)]
    assert max(math.hypot(x - 233, y - 233) for x, y in corners) < 205, (name, 'box reaches the ring', corners)
    assert min(math.hypot(mx - min(max(mx, bx0), bx1), my - min(max(my, by0), by1)) for _ in [0]) > 27, (name, 'box near marker')
    icon = (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="{labels}; device-optimized: 1 redraw area '
            f'(the sock, its wind streaks{" and leaves" if leaves else ""} in one box), three pre-drawn sock poses switched every '
            f'{round(pose_frames * FRAME * 1000)} ms" style="position: absolute; left: 0; top: 0">\n'
            + '\n'.join(streak_svg) + '\n' + pole + '\n' + sock + '\n' + leaf_svg + '\n</svg>')
    i0 = base.index('<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="' + labels)
    i1 = base.index('</svg>', i0) + len('</svg>')
    s = base[:i0] + icon + base[i1:]
    s = E.sub_once(s, '<div style="position: absolute; left: 0; top: 76px; width: 466px; text-align: center; font-size: 24px; line-height: 30px; color: #BDBDBD">Wind</div>\n', '')
    for kname in ('wxsock', 'wxstreak', 'wxsockin'):
        s = re.sub(r'@keyframes ' + kname + r'\{.*\}\n', '', s)
    s = E.add_keyframes(s, kfs)
    open(os.path.join(E.PROJ, name + '.dc.html'), 'w').write(s)
    print(name, dict(hoop=mount, pole_x=pole_x, droop=droop, box=(round(bx0), round(by0), round(bx1), round(by1)),
                     lowest=round(max(y for x, y in pts_all), 1), pose_ms=round(pose_frames * FRAME * 1000)))

if __name__ == '__main__':
    hero_sock_face('AW2', 18, 'SW', None, [(240, 262, 186), (252, 270, 200)], 1.6,
                   'A striped windsock on a pole, three of its five stripes filled and angled a little below horizontal in a gentle breeze')
    hero_sock_face('AW3', 4, 'NE', 'Wind, calm day', [], 1.6,
                   'A striped windsock hanging limp from its pole on a calm day, swaying slowly')
    hero_sock_face('AW4', 45, 'W', 'Wind, strong wind', [(232, 262, 112), (296, 322, 128), (244, 274, 160), (282, 304, 174)], 0.8,
                   'A striped windsock standing straight out from its pole, fluttering fast in a strong breeze')

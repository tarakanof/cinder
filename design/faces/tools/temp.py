import datetime, math, os
import edit as E

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(E.ORIG, 'AT1.dc.html')
EASE_P = (0.2, 0.8, 0.2, 1.0)
GREY = '#8A8A8A'
TICKS = [160, 147, 134]
MERC_BOTTOM = 184


def merc_top(t):
    return 144 - (t - 12) * 22 / 19


def minus(v):
    return str(v).replace('-', '−')


def bezier_time_for_progress(p):
    x1, y1, x2, y2 = EASE_P
    bx = lambda s: 3 * (1 - s) ** 2 * s * x1 + 3 * (1 - s) * s ** 2 * x2 + s ** 3
    by = lambda s: 3 * (1 - s) ** 2 * s * y1 + 3 * (1 - s) * s ** 2 * y2 + s ** 3
    lo, hi = 0.0, 1.0
    for _ in range(60):
        mid = (lo + hi) / 2
        if by(mid) < p:
            lo = mid
        else:
            hi = mid
    return bx((lo + hi) / 2)


def frame_pcts(n):
    return [E.ARRIVE_END * (1 - (1 - k / n) ** (1 / 3)) for k in range(n + 1)]


def prop_kf(name, prop, vals, colour_fn):
    n = len(vals) - 1
    parts = []
    for k, (v, pct) in enumerate(zip(vals, frame_pcts(n))):
        sel = E.fmt(pct) + '%' + (',100%' if k == n else '')
        parts.append(f'{sel}{{{prop}:{colour_fn(v)};animation-timing-function:step-end}}')
    return '@keyframes ' + name + '{' + ''.join(parts) + '}'


def tick_kf(name, vals, pass_pct):
    n = len(vals) - 1
    pcts = frame_pcts(n)
    cur = max(k for k in range(n + 1) if pcts[k] <= pass_pct)
    parts = [f'0%{{stroke:{GREY};animation-timing-function:step-end}}']
    parts.append(f'{E.fmt(pass_pct)}%{{stroke:{E.temp_colour(vals[cur])};animation-timing-function:step-end}}')
    for k in range(cur + 1, n + 1):
        sel = E.fmt(pcts[k]) + '%' + (',100%' if k == n else '')
        parts.append(f'{sel}{{stroke:{E.temp_colour(vals[k])};animation-timing-function:step-end}}')
    return '@keyframes ' + name + '{' + ''.join(parts) + '}', cur


def marker_xy(t, lo, hi):
    a = math.radians(225 + 270 * (t - lo) / (hi - lo))
    return round(233 + 222 * math.sin(a), 2), round(233 - 222 * math.cos(a), 2)


PIECES = 150


def fine_gauge(lo, hi):
    ov = math.degrees(0.6 / 222)
    out = []
    for k in range(PIECES):
        ca = 225 + 270 * k / PIECES
        cb = 225 + 270 * (k + 1) / PIECES + (ov if k < PIECES - 1 else 0)
        pa = [round(233 + 222 * math.sin(math.radians(c)), 2) for c in (ca,)] + [round(233 - 222 * math.cos(math.radians(ca)), 2)]
        pb = [round(233 + 222 * math.sin(math.radians(cb)), 2), round(233 - 222 * math.cos(math.radians(cb)), 2)]
        v = lo + (hi - lo) * (k + 0.5) / PIECES
        out.append(f'<path d="M {pa[0]} {pa[1]} A 222 222 0 0 1 {pb[0]} {pb[1]}" style="fill: none; stroke: {E.temp_colour(v)}; stroke-width: 12"></path>')
    return out


GAUGE_STROKES = ['#4AC5B9', '#4BC8AA', '#4CCAA2', '#4CCB9A', '#5CCC92', '#6BCD8A', '#7BCE82', '#8ACF7A',
                 '#9AD072', '#AAD06A', '#BAD162', '#C9D25A']
GAUGE_ROT = ['135', '157.5', '180', '202.5', '225', '247.5', '270', '292.5', '315', '337.5', '0', '22.5']

FROST = '''<g style="transform-origin: 201px 176px; animation: wxsparkle 2.6s ease-in-out infinite">
<line x1="201" y1="170" x2="201" y2="182" style="stroke: #D0DCE8; stroke-width: 2.5; stroke-linecap: round"></line>
<line x1="201" y1="170" x2="201" y2="182" transform="rotate(60 201 176)" style="stroke: #D0DCE8; stroke-width: 2.5; stroke-linecap: round"></line>
<line x1="201" y1="170" x2="201" y2="182" transform="rotate(120 201 176)" style="stroke: #D0DCE8; stroke-width: 2.5; stroke-linecap: round"></line>
</g>
<g style="transform-origin: 266px 186px; animation: wxsparkle 2.6s ease-in-out -1.3s infinite">
<line x1="266" y1="181" x2="266" y2="191" style="stroke: #D0DCE8; stroke-width: 2; stroke-linecap: round"></line>
<line x1="266" y1="181" x2="266" y2="191" transform="rotate(60 266 186)" style="stroke: #D0DCE8; stroke-width: 2; stroke-linecap: round"></line>
<line x1="266" y1="181" x2="266" y2="191" transform="rotate(120 266 186)" style="stroke: #D0DCE8; stroke-width: 2; stroke-linecap: round"></line>
</g>
<circle cx="211" cy="140" r="2" style="fill: #D0DCE8; transform-origin: 211px 140px; animation: wxsparkle 2.6s ease-in-out -0.6s infinite"></circle>'''
FROST_KF = '@keyframes wxsparkle{0%,100%{opacity:0.25;transform:scale(0.8)}50%{opacity:1;transform:scale(1.1)}}'

SHIMMER = '''<path d="M 206 176 q -4 -6 0 -12 q 4 -6 0 -12" style="fill: none; stroke: #E0A030; stroke-width: 3; stroke-linecap: round; opacity: 0; animation: wxshimmer 2.4s ease-out infinite"></path>
<path d="M 270 184 q -4 -6 0 -12 q 4 -6 0 -12" style="fill: none; stroke: #E0A030; stroke-width: 3; stroke-linecap: round; opacity: 0; animation: wxshimmer 2.4s ease-out -1.2s infinite"></path>'''
SHIMMER_KF = '@keyframes wxshimmer{0%{transform:translateY(6px);opacity:0}35%{opacity:0.85}100%{transform:translateY(-16px);opacity:0}}'


def build(name, t, lo, hi, title=None, recolour=False, extra=None, extra_kf=None, labels=None, sky=None, sky_kf=(), sub=None, sky_label=None, gauge='fine', clouds=None, cover=None, icon_shift=-14, big_sun=None, moon=None, is_day=True):
    s = open(SRC).read()
    tc = E.temp_colour
    if gauge == 'fine':
        old_block = '\n'.join(
            f'<circle cx="233" cy="233" r="222" transform="rotate({rot} 233 233)" style="fill: none; stroke: {c}; stroke-width: 12; '
            f'stroke-dasharray: {"87.18" if rot == "22.5" else "88.2"} 1394.87"></circle>' for rot, c in zip(GAUGE_ROT, GAUGE_STROKES))
        s = E.sub_once(s, old_block, '\n'.join(fine_gauge(lo, hi)))
    elif recolour:
        for i, (rot, old) in enumerate(zip(GAUGE_ROT, GAUGE_STROKES)):
            v = lo + (hi - lo) * i / 11
            E_old = f'transform="rotate({rot} 233 233)" style="fill: none; stroke: {old};'
            s = E.sub_once(s, E_old, f'transform="rotate({rot} 233 233)" style="fill: none; stroke: {tc(v)};')
    if recolour or gauge == 'fine':
        s = E.sub_once(s, '<circle cx="76.02" cy="389.98" r="6" style="fill: #4AC5B9">', f'<circle cx="76.02" cy="389.98" r="6" style="fill: {tc(lo)}">')
        s = E.sub_once(s, '<circle cx="389.98" cy="389.98" r="6" style="fill: #C9D25A">', f'<circle cx="389.98" cy="389.98" r="6" style="fill: {tc(hi)}">')
        s = E.sub_once(s, 'color: #4AC5B9">8°</div>', f'color: {tc(lo)}">{minus(lo)}°</div>')
        s = E.sub_once(s, 'color: #C9D25A">14°</div>', f'color: {tc(hi)}">{minus(hi)}°</div>')
    if title:
        s = E.sub_once(s, '<title>Temperature, rising mercury</title>', f'<title>{title}</title>')
    if labels:
        s = E.sub_once(s, "Gauge from today's low of 8 degrees to the high of 14, the marker sweeping up to the current 12 degrees as the face appears", labels[0])
        s = E.sub_once(s, 'aria-label="Thermometer whose mercury rises to the current reading"', f'aria-label="{labels[1]}"')
    mx, my = marker_xy(t, lo, hi)
    s = s.replace('cx="389.98" cy="76.02"', f'cx="{mx}" cy="{my}"')
    d, th = E.delta_for(mx, my)

    vals = E.count_values(t)
    n = len(vals) - 1
    final = tc(t)
    top = merc_top(t)
    h = MERC_BOTTOM - top

    kfs = [E.arrive_kf(d), E.count_kf(n), prop_kf('wxdeg', 'color', vals, tc), prop_kf('wxtfill', 'fill', vals, tc),
           '@keyframes wxmerc{0%{transform:scaleY(0);animation-timing-function:' + E.EASE + '}22%,100%{transform:scaleY(1)}}',
           '@keyframes wxbulb{0%{transform:scale(1)}10%{transform:scale(1.15)}22%,100%{transform:scale(1)}}']
    lit = []
    for i, ty in enumerate(TICKS):
        if top <= ty:
            p = (MERC_BOTTOM - ty) / h
            pass_pct = E.ARRIVE_END * bezier_time_for_progress(p)
            kf, cur = tick_kf(f'wxtick{i + 1}', vals, pass_pct)
            kfs.append(kf)
            lit.append((ty, i + 1, round(pass_pct, 2), vals[cur]))
    kfs += extra_kf or []
    kfs += list(sky_kf)
    if clouds:
        kfs += CLOUD_KF
    if big_sun:
        kfs += BIG_SUN_KF
    if moon:
        kfs += MOON_KF
    if sub:
        kfs.append(SUB_KF)
    s = E.sub_once(s, '@keyframes wxmerc{0%{transform:scaleY(0.12)}40%{transform:scaleY(1.06)}50%{transform:scaleY(1)}90%{transform:scaleY(1)}100%{transform:scaleY(0.12)}}\n', '')
    s = E.sub_once(s, '@keyframes wxbulb{0%,100%{transform:scale(1)}45%{transform:scale(1.15)}55%{transform:scale(1)}}\n', '')
    s = E.sub_once(s, '@keyframes wxneedle{0%{transform:rotate(-180deg)}40%{transform:rotate(4deg)}50%{transform:rotate(0deg)}90%{transform:rotate(0deg)}100%{transform:rotate(-180deg)}}\n', '')
    s = E.add_keyframes(s, kfs)

    s = E.sub_once(s, 'animation: wxneedle 4s ease-out infinite', 'animation: wxarrive 6s linear infinite')
    s = E.sub_once(s, '<rect x="229" y="144" width="8" height="40" rx="4" style="fill: #8ACF7A; transform-origin: 233px 184px; animation: wxmerc 4s ease-out infinite"></rect>',
                   f'<rect x="229" y="{E.fmt(top)}" width="8" height="{E.fmt(h)}" rx="4" style="fill: {final}; transform-origin: 233px 184px; animation: wxmerc 6s linear infinite, wxtfill 6s infinite"></rect>')
    s = E.sub_once(s, 'style="fill: #8ACF7A; transform-origin: 233px 183px; animation: wxbulb 4s ease-out infinite"',
                   f'style="fill: {final}; transform-origin: 233px 183px; animation: wxbulb 6s ease-in-out infinite, wxtfill 6s infinite"')
    for ty, idx, _, _ in lit:
        s = E.sub_once(s, f'<line x1="253" y1="{ty}" x2="261" y2="{ty}" style="stroke: #8A8A8A; stroke-width: 3; stroke-linecap: round"></line>',
                       f'<line x1="253" y1="{ty}" x2="261" y2="{ty}" style="stroke: {final}; stroke-width: 3; stroke-linecap: round; animation: wxtick{idx} 6s infinite"></line>')
    if extra:
        s = E.sub_once(s, '<line x1="253" y1="134" x2="261" y2="134"', extra + '\n<line x1="253" y1="134" x2="261" y2="134"')
    if sky:
        thermo = '<path d="M 222 127 A 11 11 0 0 1 244 127'
        s = E.sub_once(s, thermo, sky + '\n' + thermo)

    old = ('<div style="position: relative; font-size: 96px; line-height: 84px; letter-spacing: -2px; color: #8ACF7A">12'
           '<span style="position: absolute; left: 100%; top: 0">°</span></div>')
    lines = '\n'.join(f'<div style="height: 84px; font-size: 96px; line-height: 84px; letter-spacing: -2px; text-align: right; '
                      f'white-space: nowrap; color: {tc(v)}">{minus(v)}</div>' for v in vals)
    new = (f'<div style="position: relative; font-size: 96px; line-height: 84px; letter-spacing: -2px; color: {final}">'
           f'<span style="color: transparent">{minus(t)}</span>'
           '<div aria-hidden="true" style="position: absolute; right: 0; top: 0; width: max-content; height: 84px; overflow: hidden; '
           'font-size: 96px; line-height: 84px; letter-spacing: -2px">\n<div style="animation: wxcount 6s infinite">\n'
           f'{lines}\n</div>\n</div>\n'
           '<span style="position: absolute; left: 100%; top: 0; animation: wxdeg 6s infinite">°</span></div>')
    s = E.sub_once(s, old, new)
    if sub:
        col = '<div style="position: absolute; left: 0; top: 212px; width: 466px; display: flex; flex-direction: column; align-items: center">'
        s = E.sub_once(s, col, col.replace('align-items: center">', 'align-items: center; gap: 4px">'))
        tail = 'animation: wxdeg 6s infinite">°</span></div>\n</div>'
        s = E.sub_once(s, tail, tail[:-6] + '\n<div style="font-size: 30px; line-height: 36px; color: #BDBDBD; white-space: nowrap; '
                       'animation: wxsubin 6s linear infinite">' + sub + '</div>\n</div>')
    s = E.sub_once(s, '<div style="position: absolute; left: 0; top: 76px; width: 466px; text-align: center; font-size: 24px; line-height: 30px; color: #BDBDBD">Temperature</div>\n', '')
    icon_svg = 'role="img" aria-label="Thermometer'
    i = s.index(icon_svg)
    j = s.index('style="position: absolute; left: 0; top: 0">', i)
    s = s[:j] + f'style="position: absolute; left: 0; top: {icon_shift}px">' + s[j + len('style="position: absolute; left: 0; top: 0">'):]
    if clouds:
        k = s.rindex('<svg', 0, i)
        s = s[:k] + cloud_layer(clouds, cover) + '\n' + s[k:]
    if big_sun:
        i = s.index(icon_svg)
        k = s.rindex('<svg', 0, i)
        s = s[:k] + big_sun_layer(**big_sun) + '\n' + s[k:]
    if moon:
        i = s.index(icon_svg)
        k = s.rindex('<svg', 0, i)
        if clouds:
            k = s.rindex('<svg', 0, k)
        s = s[:k] + moon_layer(**moon) + '\n' + s[k:]
    if sky_label:
        lbl = 'aria-label="' + (labels[1] if labels else 'Thermometer whose mercury rises to the current reading') + '"'
        s = E.sub_once(s, lbl, lbl[:-1] + ', ' + sky_label + '"')
    s = E.end_labels(s)
    if name in ('AT4', 'AT5'):
        import opt2
        s = opt2.at4_optimize(s, title) if name == 'AT4' else opt2.at5_optimize(s, title)
    with open(os.path.join(E.PROJ, name + '.dc.html'), 'w') as f:
        f.write(s)
    print(name, dict(value=t, range=(lo, hi), marker=(mx, my), clock=round(th, 2), delta=d, frames=n, vals=vals,
                     final=final, merc_top=round(top, 2), lit=lit, lo_c=tc(lo), hi_c=tc(hi)))


RAYS = lambda cx, cy, c: '\n'.join(
    f'<line x1="{cx}" y1="{cy - 13.5}" x2="{cx}" y2="{cy - 17.5}" transform="rotate({a} {cx} {cy})" '
    f'style="stroke: {c}; stroke-width: 3; stroke-linecap: round"></line>' for a in range(0, 360, 45))
CLOUD_D = 'M -20 0 H 18 A 8 8 0 0 0 16 -15 A 12 12 0 0 0 -7 -16 A 11 11 0 0 0 -20 0 Z'


def cloud(x, y, colour, drift, dur, delay=0):
    return (f'<g style="animation: {drift} {dur}s ease-in-out {E.fmt(delay)}s infinite"><g transform="translate({x} {y})">'
            f'<path d="{CLOUD_D}" style="fill: #000000; stroke: {colour}; stroke-width: 4.5; stroke-linejoin: round"></path></g></g>')


def sun_glyph(cx, cy, spin=False, halo=None):
    rays = RAYS(cx, cy, '#C08828')
    if spin:
        rays = f'<g style="transform-origin: {cx}px {cy}px; animation: wxskyspin 16s linear infinite">\n{rays}\n</g>'
    h = ''
    if halo:
        h = (f'<circle cx="{cx}" cy="{cy}" r="10" style="fill: none; stroke: {halo}; stroke-width: 3; '
             f'transform-origin: {cx}px {cy}px; animation: wxuvpulse 2.4s ease-out infinite"></circle>\n')
    return (h + rays + f'\n<circle cx="{cx}" cy="{cy}" r="8" style="fill: #000000; stroke: #E0A030; stroke-width: 4.5"></circle>')


DRIFT_KF = '@keyframes wxdrift{0%,100%{transform:translateX(0px)}50%{transform:translateX(-5px)}}'
DRIFT2_KF = '@keyframes wxdrift2{0%,100%{transform:translateX(0px)}50%{transform:translateX(4px)}}'
SPIN_KF = '@keyframes wxskyspin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}'
UV_KF = '@keyframes wxuvpulse{0%{transform:scale(0.9);opacity:0.85}100%{transform:scale(2.1);opacity:0}}'
SUB_KF = '@keyframes wxsubin{0%,22%{opacity:0}30%,100%{opacity:1}}'


def cloud_path(circles, base):
    def upper_x(c1, c2):
        (x1, y1, r1), (x2, y2, r2) = c1, c2
        dx, dy = x2 - x1, y2 - y1
        d = math.hypot(dx, dy)
        a = (r1 * r1 - r2 * r2 + d * d) / (2 * d)
        h = math.sqrt(max(r1 * r1 - a * a, 0))
        mx, my = x1 + a * dx / d, y1 + a * dy / d
        p1 = (mx + h * dy / d, my - h * dx / d)
        p2 = (mx - h * dy / d, my + h * dx / d)
        return min(p1, p2, key=lambda p: p[1])

    cs = [(x, base + dy, r) for x, dy, r in circles]
    x0 = cs[0][0] - math.sqrt(cs[0][2] ** 2 - (cs[0][1] - base) ** 2)
    xn = cs[-1][0] + math.sqrt(cs[-1][2] ** 2 - (cs[-1][1] - base) ** 2)
    pts = [(x0, base)] + [upper_x(a, b) for a, b in zip(cs, cs[1:])] + [(xn, base)]
    d = f'M {E.fmt(round(x0, 2))} {base}'
    for (cx, cy, r), (sx, sy), (ex, ey) in zip(cs, pts, pts[1:]):
        a0, a1 = math.atan2(sy - cy, sx - cx), math.atan2(ey - cy, ex - cx)
        sweep = (a1 - a0) % (2 * math.pi)
        d += f' A {r} {r} 0 {1 if sweep > math.pi else 0} 1 {E.fmt(round(ex, 2))} {E.fmt(round(ey, 2))}'
    return d + ' Z'


CLOUD_KF = ['@keyframes wxcloudL{0%{transform:translateX(-160px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translateX(0px)}}',
            '@keyframes wxcloudR{0%{transform:translateX(160px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translateX(0px)}}',
            '@keyframes wxbreathe{0%,100%{transform:translateX(-4px)}50%{transform:translateX(4px)}}']


CLOUD_TONES = {
    'white': ('#E6ECF2', '#E6ECF2', '#C4CCD4'),
    'light': ('#8E96A0', '#737B86', '#A8B0BA'),
    'dark': ('#555D68', '#3A4048', '#6A7480'),
    'night': ('#3E4652', '#2E343D', '#56606C'),
}
CLOUD_CLIP = 195


def medium_cloud(inner, side, base, w):
    tpl = [(17, -14, 22), (55, -26, 26), (89, -12, 16)]
    k = w / 100
    circ = [(x * k, dy * k, r * k) for x, dy, r in tpl]
    xl = min(x - math.sqrt(r * r - dy * dy) for x, dy, r in circ)
    xr = max(x + math.sqrt(r * r - dy * dy) for x, dy, r in circ)
    if side == 'L':
        off = inner - xr
    else:
        off = inner - xl
        circ = sorted(((xr - x + xl, dy, r) for x, dy, r in circ))
    return [(round(x + off, 1), round(dy, 1), round(r, 1)) for x, dy, r in circ]


def cloud_layer(spec, cover):
    parts = []
    for side, layer, tone, base, circles, dur, delay in spec['clouds']:
        front, back, outline = CLOUD_TONES[tone]
        fill = front if layer == 'front' else back
        parts.append(f'<g style="animation: wxcloud{side} 6s linear infinite">'
                     f'<g style="animation: wxbreathe {dur}s ease-in-out {E.fmt(delay)}s infinite">'
                     f'<path d="{cloud_path(circles, base)}" style="fill: {fill}; stroke: {outline}; stroke-width: 4.5; stroke-linejoin: round"></path></g></g>')
    tone_word = {'white': 'white', 'light': 'light grey', 'dark': 'dark grey', 'night': 'dim blue-grey night'}[spec['clouds'][0][2]]
    return (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="{tone_word.capitalize()} clouds drifting in from the sides of the face, '
            f'{cover} percent cloud cover" style="position: absolute; left: 0; top: 0">\n'
            f'<defs><radialGradient id="wxcloudfade" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="{CLOUD_CLIP}">'
            f'<stop offset="{spec["fade_from"] / CLOUD_CLIP:.2f}" stop-color="#FFFFFF"></stop><stop offset="1" stop-color="#000000"></stop></radialGradient>'
            f'<mask id="wxcloudmask" maskUnits="userSpaceOnUse" x="0" y="0" width="466" height="466">'
            f'<circle cx="233" cy="233" r="{CLOUD_CLIP}" style="fill: url(#wxcloudfade)"></circle></mask></defs>\n'
            '<g mask="url(#wxcloudmask)">\n' + '\n'.join(parts) + '\n</g>\n</svg>')


def clouds_for(cover, is_day=True):
    spec = _clouds_for(cover)
    if spec and not is_day:
        spec = dict(spec, clouds=[c[:2] + ('night',) + c[3:] for c in spec['clouds']])
    return spec


def _clouds_for(cover):
    if cover < 10:
        return None
    if cover < 40:
        return dict(fade_from=175, clouds=[('R', 'front', 'white', 150, medium_cloud(290, 'R', 150, 115), 8, 0)])
    if cover < 70:
        return dict(fade_from=175, clouds=[('L', 'front', 'white', 182, medium_cloud(190, 'L', 182, 135), 8, 0),
                                           ('R', 'front', 'white', 160, medium_cloud(284, 'R', 160, 135), 9, -4)])
    if cover < 90:
        return dict(fade_from=165, clouds=[
            ('L', 'back', 'light', 142, medium_cloud(176, 'L', 142, 150), 10, -3),
            ('R', 'back', 'light', 128, medium_cloud(292, 'R', 128, 150), 9, -6),
            ('L', 'front', 'light', 190, medium_cloud(194, 'L', 190, 150), 8, 0),
            ('R', 'front', 'light', 176, medium_cloud(278, 'R', 176, 150), 8.5, -2)])
    return dict(fade_from=160, clouds=[
        ('L', 'back', 'dark', 140, [(20, -16, 28), (66, -36, 32), (118, -30, 30), (160, -12, 20)], 10, -3),
        ('R', 'back', 'dark', 128, [(308, -14, 22), (352, -30, 30), (400, -36, 32), (446, -16, 28)], 9, -6),
        ('L', 'front', 'dark', 196, [(24, -18, 30), (74, -34, 34), (128, -28, 30), (178, -14, 24)], 8, 0),
        ('R', 'front', 'dark', 184, [(284, -14, 24), (330, -28, 30), (380, -34, 34), (430, -18, 30)], 8.5, -2)])


UV_ADVICE = [(3, 5, 'Sunglasses on', '#F0E641'), (6, 7, 'Use sunscreen', '#F08A3A'),
             (8, 10, 'Sunscreen &amp; shade', '#FF5050'), (11, 99, 'Avoid the sun', '#B060E0')]


def sub_line(sky_word, uv=None, daytime=True):
    if uv is not None and daytime:
        for lo, hi, phrase, colour in UV_ADVICE:
            if lo <= uv <= hi:
                return f'<span style="color: {colour}">{phrase}</span>'
    return sky_word


RAY_COUNT = 22
RAY_KEEP_OUT = [(190, 88, 286, 202, 18), (76, 208, 390, 339, 24), (84, 356, 166, 410, 12), (299, 356, 382, 410, 12),
                (180, 422, 286, 452, 10)]


def big_sun_layer(cx, cy, uv=None, core=52):
    keep = ''.join(f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" rx="{r}" style="fill: #000000"></rect>'
                   for x0, y0, x1, y1, r in RAY_KEEP_OUT)
    rays = []
    out_dir = math.degrees(math.atan2(cy - 233, cx - 233)) % 360
    for k in range(RAY_COUNT):
        a = k * 360 / RAY_COUNT + 4
        off = abs((a - out_dir + 180) % 360 - 180)
        length = core + 40 if off < 50 else 330
        x2 = round(cx + length * math.cos(math.radians(a)), 1)
        y2 = round(cy + length * math.sin(math.radians(a)), 1)
        x1 = round(cx + (core + 14) * math.cos(math.radians(a)), 1)
        y1 = round(cy + (core + 14) * math.sin(math.radians(a)), 1)
        rays.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" style="stroke: url(#wxraygrad); stroke-width: {3.5 if k % 2 == 0 else 3}; stroke-linecap: round"></line>')
    halo = ''
    label = 'A large golden sun peeking in from the top left edge, long rays flowing around the reading'
    if uv is not None and uv >= 3:
        c = uv_colour(uv)
        halo = (f'<circle cx="{cx}" cy="{cy}" r="{core + 4}" style="fill: none; stroke: {c}; stroke-width: 3; '
                f'transform-origin: {cx}px {cy}px; animation: wxuvbig 2.4s ease-out infinite"></circle>\n')
        label += f', pulsing in the UV colour for a UV index of {uv}'
    defs = (
        '<radialGradient id="wxrimfade" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="195">'
        '<stop offset="0.82" stop-color="#FFFFFF"></stop><stop offset="1" stop-color="#000000"></stop></radialGradient>'
        '<mask id="wxsunmask" maskUnits="userSpaceOnUse" x="0" y="0" width="466" height="466">'
        '<circle cx="233" cy="233" r="195" style="fill: url(#wxrimfade)"></circle></mask>'
        '<filter id="wxfeather" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="6"></feGaussianBlur></filter>'
        '<mask id="wxraymask" maskUnits="userSpaceOnUse" x="0" y="0" width="466" height="466">'
        '<circle cx="233" cy="233" r="195" style="fill: url(#wxrimfade)"></circle>'
        f'<g style="filter: url(#wxfeather)">{keep}</g></mask>'
        f'<radialGradient id="wxsunfill" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="{core}">'
        '<stop offset="0" stop-color="#FFE07A"></stop><stop offset="0.6" stop-color="#FFB547"></stop><stop offset="1" stop-color="#F08A3A"></stop></radialGradient>'
        f'<radialGradient id="wxsunglow" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="80">'
        f'<stop offset="{core / 80:.2f}" stop-color="#FFB547" stop-opacity="0.22"></stop><stop offset="1" stop-color="#FFB547" stop-opacity="0"></stop></radialGradient>'
        f'<radialGradient id="wxraygrad" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="300">'
        '<stop offset="0.2" stop-color="#FFB547" stop-opacity="0.55"></stop><stop offset="1" stop-color="#FFB547" stop-opacity="0"></stop></radialGradient>')
    return (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="{label}" style="position: absolute; left: 0; top: 0">\n'
            f'<defs>{defs}</defs>\n'
            '<g style="animation: wxsunin 6s linear infinite">\n'
            '<g mask="url(#wxraymask)">\n'
            f'<g style="transform-origin: {cx}px {cy}px; animation: wxraysway 9s ease-in-out infinite">\n' + '\n'.join(rays) + '\n</g>\n</g>\n'
            '<g mask="url(#wxsunmask)" style="opacity: 0.88">\n'
            f'<circle cx="{cx}" cy="{cy}" r="80" style="fill: url(#wxsunglow)"></circle>\n'
            + halo +
            f'<circle cx="{cx}" cy="{cy}" r="{core}" style="fill: url(#wxsunfill); stroke: #FFB547; stroke-width: 1.5"></circle>\n'
            '</g>\n</g>\n</svg>')


BIG_SUN_KF = ['@keyframes wxsunin{0%{transform:translate(-110px,-110px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translate(0px,0px)}}',
              '@keyframes wxraysway{0%,100%{transform:rotate(-3deg)}50%{transform:rotate(3deg)}}',
              '@keyframes wxuvbig{0%{transform:scale(0.95);opacity:0.85}100%{transform:scale(1.55);opacity:0}}']


def moon_phase(when):
    d = (when - datetime.datetime(2000, 1, 1, 12)).total_seconds() / 86400
    r = math.radians
    g = r((357.529 + 0.98560028 * d) % 360)
    q = (280.459 + 0.98564736 * d) % 360
    ls = (q + 1.915 * math.sin(g) + 0.020 * math.sin(2 * g)) % 360
    L = (218.316 + 13.176396 * d) % 360
    M = r((134.963 + 13.064993 * d) % 360)
    F = r((93.272 + 13.229350 * d) % 360)
    D = r((297.850 + 12.190749 * d) % 360)
    lm = (L + 6.289 * math.sin(M) + 1.274 * math.sin(2 * D - M) + 0.658 * math.sin(2 * D) + 0.214 * math.sin(2 * M)
          - 0.186 * math.sin(g) - 0.114 * math.sin(2 * F))
    lat = 5.128 * math.sin(F)
    elong = math.acos(math.cos(r(lat)) * math.cos(r(lm - ls)))
    return (1 - math.cos(elong)) / 2, ((lm - ls) % 360) < 180


STARS = [(214, 70, 1.4), (262, 62, 1.2), (305, 95, 1.8), (352, 126, 1.3), (392, 186, 1.5), (320, 168, 1.1),
         (404, 254, 1.6), (396, 300, 1.2), (62, 262, 1.4), (74, 312, 1.1)]
MOON_KF = ['@keyframes wxmoonL{0%{transform:translate(-110px,-110px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translate(0px,0px)}}',
           '@keyframes wxmoonR{0%{transform:translate(110px,-110px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translate(0px,0px)}}',
           '@keyframes wxstarsin{0%{opacity:0}22%,100%{opacity:1}}',
           '@keyframes wxtwinkle{0%,100%{opacity:0.35}50%{opacity:1}}']


def moon_layer(cx, cy, r, illum, waxing, stars=False, glow=0.2, label='A moon'):
    sgn = 1 if waxing else -1
    rx = abs(1 - 2 * illum) * r
    top, bot = f'{cx} {round(cy - r, 2)}', f'{cx} {round(cy + r, 2)}'
    limb_sweep = 1 if sgn > 0 else 0
    term_sweep = (0 if sgn > 0 else 1) if illum < 0.5 else (1 if sgn > 0 else 0)
    lit = (f'M {top} A {r} {r} 0 0 {limb_sweep} {bot} A {round(rx, 2)} {r} 0 0 {term_sweep} {top} Z')
    stars_svg = ''
    if stars:
        tw = []
        for i, (x, y, sr) in enumerate(STARS):
            tw.append(f'<circle cx="{x}" cy="{y}" r="{sr}" style="fill: #DDE6F0; opacity: 0.35; '
                      f'animation: wxtwinkle {round(2.6 + 0.37 * (i % 5), 2)}s ease-in-out {-round(0.53 * i, 2)}s infinite"></circle>')
        stars_svg = '<g style="animation: wxstarsin 6s linear infinite">\n' + '\n'.join(tw) + '\n</g>\n'
        label += ', tiny stars twinkling around it'
    defs = ('<radialGradient id="wxmoonrim" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="195">'
            '<stop offset="0.82" stop-color="#FFFFFF"></stop><stop offset="1" stop-color="#000000"></stop></radialGradient>'
            '<mask id="wxmoonmask" maskUnits="userSpaceOnUse" x="0" y="0" width="466" height="466">'
            '<circle cx="233" cy="233" r="195" style="fill: url(#wxmoonrim)"></circle></mask>'
            f'<radialGradient id="wxmoonfill" gradientUnits="userSpaceOnUse" cx="{cx - sgn * r * 0.35}" cy="{cy - r * 0.2}" r="{r * 1.2}">'
            '<stop offset="0" stop-color="#E8ECF2"></stop><stop offset="1" stop-color="#C9D2DC"></stop></radialGradient>'
            f'<radialGradient id="wxmoonglow" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="{round(r * 1.6)}">'
            f'<stop offset="0.6" stop-color="#AFC4E0" stop-opacity="{glow}"></stop><stop offset="1" stop-color="#AFC4E0" stop-opacity="0"></stop></radialGradient>')
    return (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="{label}" style="position: absolute; left: 0; top: 0">\n'
            f'<defs>{defs}</defs>\n<g mask="url(#wxmoonmask)">\n' + stars_svg +
            f'<g style="animation: wxmoon{"L" if cx < 233 else "R"} 6s linear infinite">\n'
            f'<circle cx="{cx}" cy="{cy}" r="{round(r * 1.6)}" style="fill: url(#wxmoonglow)"></circle>\n'
            f'<circle cx="{cx}" cy="{cy}" r="{r}" style="fill: #000000"></circle>\n'
            f'<path d="{lit}" style="fill: url(#wxmoonfill)"></path>\n'
            '</g>\n</g>\n</svg>')


def uv_colour(u):
    return '#F0E641' if u <= 5 else '#F08A3A' if u <= 7 else '#FF5050' if u <= 10 else '#B060E0'


SUN_D = 165
SUN_C = round(233 - SUN_D * math.cos(math.radians(45)), 1)

if __name__ == '__main__':
    build('AT1', 12, 8, 14, sub=sub_line('Partly cloudy', uv=2), clouds=clouds_for(55), cover=55)
    build('AT3', -6, -9, -2, title='Temperature, cold day', recolour=True, extra=FROST, extra_kf=[FROST_KF],
          sub=sub_line('Overcast'), clouds=clouds_for(95), cover=95, labels=(
        "Gauge from today's low of minus 9 degrees to the high of minus 2, the marker sweeping up to the current minus 6 degrees as the face appears",
        'Thermometer whose mercury rises to the current reading, with ice crystals sparkling beside it'))
    build('AT4', 31, 24, 34, title='Temperature, hot day', recolour=True, extra=SHIMMER, extra_kf=[SHIMMER_KF],
          sub=sub_line('Sunny', uv=8), clouds=clouds_for(5), cover=5, big_sun=dict(cx=SUN_C, cy=SUN_C, uv=8), labels=(
        "Gauge from today's low of 24 degrees to the high of 34, the marker sweeping up to the current 31 degrees as the face appears",
        'Thermometer whose mercury rises to the current reading, with heat shimmer rising beside it'))

    ILLUM, WAXING = moon_phase(datetime.datetime(2026, 10, 8, 20, 0))
    print('moon', round(ILLUM * 100, 1), '% lit,', 'waxing' if WAXING else 'waning')
    MOON_D = 125
    MC = round(233 - MOON_D * math.cos(math.radians(45)), 1)
    phase_word = ('waxing' if WAXING else 'waning') + (' crescent' if ILLUM < 0.45 else ' gibbous' if ILLUM > 0.55 else ' half')
    build('AT5', 9, 6, 15, title='Temperature, clear night', recolour=True, is_day=False,
          sub=f'<span style="color: {E.temp_colour(6)}">low 6°</span> by 06:00',
          moon=dict(cx=MC, cy=MC, r=50, illum=ILLUM, waxing=WAXING, stars=True,
                    label=f'A thin {phase_word} moon in the top left of a clear night sky'), labels=(
        "Gauge from tonight's low of 6 degrees to the day's high of 15, the marker sweeping up to the current 9 degrees as the face appears",
        'Thermometer whose mercury rises to the current reading'))
    build('AT6', 11, 9, 14, title='Temperature, cloudy night', recolour=True, is_day=False,
          sub='Cloudy', clouds=clouds_for(80, is_day=False), cover=80,
          moon=dict(cx=300, cy=72, r=34, illum=ILLUM, waxing=WAXING, glow=0.14,
                    label=f'A small {phase_word} moon peeking out from behind the clouds'), labels=(
        "Gauge from tonight's low of 9 degrees to the day's high of 14, the marker sweeping up to the current 11 degrees as the face appears",
        'Thermometer whose mercury rises to the current reading'))

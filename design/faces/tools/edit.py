import math, re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
ORIG = os.path.join(ROOT, 'design', 'faces', 'tools', 'orig')
PROJ = os.path.join(ROOT, 'design', 'faces', 'canvas')

EASE = 'cubic-bezier(0.2,0.8,0.2,1)'
ARRIVE_END = 22.0


def fmt(x):
    s = f'{x:.3f}'.rstrip('0').rstrip('.')
    return s if s not in ('-0', '') else '0'


def delta_for(x, y):
    theta = math.degrees(math.atan2(x - 233, -(y - 233))) % 360
    return round((theta - 225) % 360, 2), theta


def arrive_kf(delta):
    return ('@keyframes wxarrive{0%{transform:rotate(-' + fmt(delta) + 'deg);animation-timing-function:' + EASE +
            '}22%,100%{transform:rotate(0deg)}}')


def count_values(v):
    if abs(v) <= 30:
        step = 1 if v >= 0 else -1
        return list(range(0, v + step, step))
    n = 23
    vals = [round(i * v / n) for i in range(n + 1)]
    assert len(set(vals)) == len(vals), vals
    return vals


def count_kf(n_frames):
    parts = []
    last = None
    for k in range(n_frames + 1):
        t = 1 - (1 - k / n_frames) ** (1 / 3)
        pct = fmt(ARRIVE_END * t)
        assert pct != last, (k, pct)
        last = pct
        y = fmt(-84 * k) if k else '0'
        sel = pct + '%' if k < n_frames else pct + '%,100%'
        parts.append(f'{sel}{{transform:translateY({y}px);animation-timing-function:step-end}}')
    return '@keyframes wxcount{' + ''.join(parts) + '}'


def hex2rgb(h):
    h = h.lstrip('#')
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def rgb2hex(c):
    return '#' + ''.join(f'{max(0, min(255, round(v))):02X}' for v in c)


_gauge = ['#4AC5B9', '#4BC8AA', '#4CCAA2', '#4CCB9A', '#5CCC92', '#6BCD8A', '#7BCE82', '#8ACF7A',
          '#9AD072', '#AAD06A', '#BAD162', '#C9D25A']
TEMP_POINTS = {-20: '#B070E0', -12: '#7060E8', -6: '#4C84F0', -2: '#5CB0F0', 0: '#66C2EC', 4: '#52C6D2',
               8: '#4AC5B9', 12: '#8ACF7A', 14: '#C9D25A', 15: '#E8D44A', 25: '#F08A3A', 35: '#FF5050', 40: '#E0306A'}
for i, c in enumerate(_gauge):
    v = 8 + 6 * i / 11
    if not any(abs(v - a) < 0.3 for a in TEMP_POINTS):
        TEMP_POINTS[v] = c


def temp_colour(v):
    pts = sorted(TEMP_POINTS.items())
    if v <= pts[0][0]:
        return pts[0][1]
    if v >= pts[-1][0]:
        return pts[-1][1]
    for (a, ca), (b, cb) in zip(pts, pts[1:]):
        if a <= v <= b:
            if v == a:
                return ca
            if v == b:
                return cb
            f = (v - a) / (b - a)
            A, B = hex2rgb(ca), hex2rgb(cb)
            return rgb2hex([A[j] + (B[j] - A[j]) * f for j in range(3)])


def aqi_colour(v):
    if v < 20:
        return '#4FD6CC'
    if v < 40:
        return '#50CCAA'
    if v < 60:
        return '#F0E641'
    if v < 80:
        return '#FF5050'
    return '#D03060'


def strip_lines(vals, colour_fn, align='center', extra=''):
    out = []
    for v in vals:
        out.append(f'<div style="height: 84px; font-size: 96px; line-height: 84px; letter-spacing: -2px; '
                   f'text-align: {align}; white-space: nowrap; color: {colour_fn(v)}{extra}">{v}</div>')
    return '\n'.join(out)


def counter_wrapper(style, value, vals, colour_fn):
    return (f'<div style="position: relative; {style}">'
            f'<span style="color: transparent">{value}</span>'
            f'<div aria-hidden="true" style="position: absolute; right: 0; top: 0; width: max-content; height: 84px; '
            f'overflow: hidden; font-size: 96px; line-height: 84px; letter-spacing: -2px">\n'
            f'<div style="animation: wxcount 6s infinite">\n'
            f'{strip_lines(vals, colour_fn, align="right")}\n'
            f'</div>\n</div>')


END_LABEL_SIZE = 36


def end_labels(s, size=END_LABEL_SIZE):
    lh = size + 6
    base = 366 + (36 - 1.219 * 30) / 2 + 0.968 * 30
    top = round(base - ((lh - 1.219 * size) / 2 + 0.968 * size))
    n = 0
    for side in ('left', 'right'):
        old = f'position: absolute; {side}: 96px; top: 366px; font-size: 30px; line-height: 36px; color: '
        n += s.count(old)
        s = s.replace(old, f'position: absolute; {side}: 96px; top: {top}px; font-size: {size}px; line-height: {lh}px; color: ')
    assert n == 2, ('end labels not found', n)
    return s


def sub_once(s, old, new):
    n = s.count(old)
    assert n == 1, (old[:80], n)
    return s.replace(old, new)


def add_keyframes(s, kfs):
    return sub_once(s, '\n</style>\n</helmet>', '\n' + '\n'.join(kfs) + '\n</style>\n</helmet>')


def wrap_marker(s, x, y, extra_before=''):
    c17 = f'<circle cx="{x}" cy="{y}" r="17" style="fill: #000000"></circle>'
    c10 = f'<circle cx="{x}" cy="{y}" r="10" style="fill: #000000; stroke: #F4F4F2; stroke-width: 5"></circle>'
    old = (extra_before + '\n' if extra_before else '') + c17 + '\n' + c10
    new = ('<g style="transform-origin: 233px 233px; animation: wxarrive 6s linear infinite">\n' + old + '\n</g>')
    return sub_once(s, old, new)


report = []


def row_face(tag, x, y, value, colour, colour_fn, halo=None):
    def f(s):
        d, th = delta_for(x, y)
        vals = count_values(value)
        report.append((tag, value, d, len(vals) - 1))
        s = add_keyframes(s, [arrive_kf(d), count_kf(len(vals) - 1)])
        s = wrap_marker(s, x, y, halo or '')
        style = f'font-size: 96px; line-height: 84px; letter-spacing: -2px; color: {colour}'
        old = f'<div style="{style}">{value}</div>'
        s = sub_once(s, old, counter_wrapper(style, value, vals, colour_fn) + '\n</div>')
        return s
    return f


AS1_HALO = ('<circle cx="27.9" cy="148.04" r="17" style="fill: none; stroke: #E0A030; stroke-width: 3; '
            'transform-origin: 27.9px 148.04px; animation: wxhalo 2.4s ease-out infinite"></circle>')



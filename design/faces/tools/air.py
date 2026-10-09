import math, os, re
import edit as E

STOPS = [(10, '#4FD6CC'), (30, '#50CCAA'), (50, '#F0E641'), (70, '#FF5050'), (90, '#D03060')]
PIECES = 150


def aqi_cont(v):
    if v <= STOPS[0][0]:
        return STOPS[0][1]
    if v >= STOPS[-1][0]:
        return STOPS[-1][1]
    for (a, ca), (b, cb) in zip(STOPS, STOPS[1:]):
        if a <= v <= b:
            A, B = E.hex2rgb(ca), E.hex2rgb(cb)
            f = (v - a) / (b - a)
            return E.rgb2hex([A[i] + (B[i] - A[i]) * f for i in range(3)])


def fine_gauge():
    ov = math.degrees(0.6 / 222)
    out = []
    for k in range(PIECES):
        ca = 225 + 270 * k / PIECES
        cb = 225 + 270 * (k + 1) / PIECES + (ov if k < PIECES - 1 else 0)
        p = lambda c: (round(233 + 222 * math.sin(math.radians(c)), 2), round(233 - 222 * math.cos(math.radians(c)), 2))
        (x1, y1), (x2, y2) = p(ca), p(cb)
        out.append(f'<path d="M {x1} {y1} A 222 222 0 0 1 {x2} {y2}" style="fill: none; stroke: {aqi_cont(100 * (k + 0.5) / PIECES)}; stroke-width: 12"></path>')
    return out


import plants

FACES = {'AA2': ('AA2', 151.28, 26.59, 42, '#F0E641', 'flower', None),
         'AA4': ('AA3', 399.52, 86.19, 68, '#FF5050', 'flower_poor', 'Air, flower, poor day')}


def flower_end_icon():
    s = open(os.path.join(E.ORIG, 'AA2.dc.html')).read()
    a = s.index('<ellipse cx="116" cy="376"')
    b = s.index('<circle cx="116" cy="383" r="3" style="fill: #F0E641"></circle>') + len('<circle cx="116" cy="383" r="3" style="fill: #F0E641"></circle>')
    return s[a:b]


if __name__ == '__main__':
    for name, (src, x, y, v, band, plant, title) in FACES.items():
        s = open(os.path.join(E.ORIG, src + '.dc.html')).read()
        bands = re.findall(r'<circle cx="233" cy="233" r="222" transform="rotate\([0-9.]+ 233 233\)"[^\n]*</circle>\n', s)
        assert len(bands) == 5
        s = E.sub_once(s, ''.join(bands), '\n'.join(fine_gauge()) + '\n')
        s = E.row_face(name, x, y, v, band, aqi_cont)(s)
        final = aqi_cont(v)
        s = E.sub_once(s, f'<div style="position: relative; font-size: 96px; line-height: 84px; letter-spacing: -2px; color: {band}">',
                       f'<div style="position: relative; font-size: 96px; line-height: 84px; letter-spacing: -2px; color: {final}">')
        s = s.replace('Air quality gauge in five bands', 'Air quality gauge shading smoothly through five bands')
        s = plants.apply(s, plant, title=title, flower_end_icon=flower_end_icon() if name == 'AA4' else None)
        open(os.path.join(E.PROJ, name + '.dc.html'), 'w').write(s)
        print(name, v, 'digit', final, 'band word', band, 'plant', plant)

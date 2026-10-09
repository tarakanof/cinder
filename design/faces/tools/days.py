import os
import edit as E
import temp as T

DST = os.path.join(E.PROJ, 'AN1.dc.html')
DAYS = [
    ('Fri', 'partly', 8, 15, None),
    ('Sat', 'rain', 9, 13, 70),
    ('Sun', 'clear', 6, 17, None),
]
ROW_Y = [143, 228, 313]
FS, LH = 34, 40
DAY_W, ICON, TEMP_W, BAR_W, GAP = 60, 40, 54, 104, 10
ARRIVE = 22.0


def tc(v):
    return T.E.temp_colour(v)


def kf_count(name, n, lh):
    parts = []
    for k in range(n + 1):
        t = 1 - (1 - k / n) ** (1 / 3) if n else 1
        sel = E.fmt(ARRIVE * t) + '%' + (',100%' if k == n else '')
        parts.append(f'{sel}{{transform:translateY({E.fmt(-lh * k) if k else 0}px);animation-timing-function:step-end}}')
    return '@keyframes ' + name + '{' + ''.join(parts) + '}'


def counter(name, value, align, dim=1.0):
    vals = E.count_values(value)
    lines = ''.join(f'<div style="height: {LH}px; text-align: {align}; white-space: nowrap; color: {tc(v)}">{v}°</div>' for v in vals)
    side = 'right: 0' if align == 'right' else 'left: 0'
    html = (f'<div style="position: relative; width: {TEMP_W}px; text-align: {align}; font-size: {FS}px; line-height: {LH}px; '
            f'color: {tc(value)}; opacity: {dim}"><span style="color: transparent">{value}°</span>'
            f'<div aria-hidden="true" style="position: absolute; {side}; top: 0; width: max-content; height: {LH}px; overflow: hidden">'
            f'<div style="animation: {name} 6s infinite">{lines}</div></div></div>')
    return html, kf_count(name, len(vals) - 1, LH)


def bar(lo, hi, amin, amax, idx):
    x0 = (lo - amin) / (amax - amin) * BAR_W
    x1 = (hi - amin) / (amax - amin) * BAR_W
    stops = ', '.join(f'{tc(v)} {E.fmt(round((v - lo) / (hi - lo) * 100, 1))}%' for v in range(lo, hi + 1))
    return (f'<div style="position: relative; width: {BAR_W}px; height: 10px; border-radius: 5px; background: #262626">'
            f'<div style="position: absolute; left: {E.fmt(round(x0, 1))}px; top: 0; width: {E.fmt(round(x1 - x0, 1))}px; height: 10px; '
            f'border-radius: 5px; background: linear-gradient(90deg, {stops}); transform-origin: center; '
            f'animation: wxbar{idx} 6s linear infinite"></div></div>')


def icon(sky, i):
    sun_grad = (f'<radialGradient id="wxdsun{i}" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#FFE07A"></stop>'
                f'<stop offset="0.6" stop-color="#FFB547"></stop><stop offset="1" stop-color="#F08A3A"></stop></radialGradient>')
    if sky == 'clear':
        rays = ''.join(f'<line x1="20" y1="5.5" x2="20" y2="2" transform="rotate({a} 20 20)" style="stroke: #FFB547; stroke-width: 2.5; stroke-linecap: round"></line>'
                       for a in range(0, 360, 45))
        body = (f'<defs>{sun_grad}</defs><g style="transform-origin: 20px 20px; animation: wxdsway 5s ease-in-out infinite">{rays}</g>'
                f'<circle cx="20" cy="20" r="11" style="fill: url(#wxdsun{i})"></circle>')
        label = 'Clear, sunny'
    elif sky == 'partly':
        cloud = T.cloud_path([(13, -5, 7), (21, -10, 8.5), (30, -5, 7)], 33)
        body = (f'<defs>{sun_grad}</defs><circle cx="14" cy="14" r="9" style="fill: url(#wxdsun{i})"></circle>'
                f'<g style="animation: wxddrift 7s ease-in-out infinite"><path d="{cloud}" style="fill: #E6ECF2; stroke: #C4CCD4; stroke-width: 2; stroke-linejoin: round"></path></g>')
        label = 'Partly cloudy'
    else:
        cloud = T.cloud_path([(10, -5, 7), (19, -10, 9), (29, -5, 7)], 24)
        drops = ''.join(f'<line x1="{x}" y1="28" x2="{x - 2}" y2="34" style="stroke: #5C9CE0; stroke-width: 2.5; stroke-linecap: round; opacity: 0; '
                        f'animation: wxdrain 0.9s linear {d}s infinite"></line>' for x, d in ((13, 0), (20, -0.3), (27, -0.6)))
        body = (f'{drops}<g style="animation: wxddrift 7s ease-in-out infinite"><path d="{cloud}" style="fill: #8E96A0; stroke: #A8B0BA; '
                f'stroke-width: 2; stroke-linejoin: round"></path></g>')
        label = 'Rain'
    return f'<svg viewBox="0 0 40 40" width="{ICON}" height="{ICON}" role="img" aria-label="{label}" style="overflow: visible">{body}</svg>'


def build():
    amin, amax = min(d[2] for d in DAYS), max(d[3] for d in DAYS)
    kfs = ['@keyframes wxdsway{0%,100%{transform:rotate(-10deg)}50%{transform:rotate(10deg)}}',
           '@keyframes wxddrift{0%,100%{transform:translateX(-3px)}50%{transform:translateX(3px)}}',
           '@keyframes wxdrain{0%{transform:translate(1px,-4px);opacity:0}25%{opacity:1}100%{transform:translate(-1px,5px);opacity:0}}']
    rows = []
    for i, (name, sky, lo, hi, rain) in enumerate(DAYS):
        s0 = round(i * 80 / 6000 * 100, 2)
        kfs.append(f'@keyframes wxrow{i}{{0%,{E.fmt(s0)}%{{opacity:0;transform:translateY(12px);animation-timing-function:{E.EASE}}}'
                   f'{E.fmt(s0 + 7)}%,100%{{opacity:1;transform:translateY(0px)}}}}')
        kfs.append(f'@keyframes wxbar{i}{{0%,{E.fmt(s0 + 3)}%{{transform:scaleX(0);animation-timing-function:{E.EASE}}}'
                   f'{E.fmt(s0 + 19)}%,100%{{transform:scaleX(1)}}}}')
        low_html, kf1 = counter(f'wxlo{i}', lo, 'right', dim=0.8)
        high_html, kf2 = counter(f'wxhi{i}', hi, 'left')
        kfs += [kf1, kf2]
        rain_html = ''
        if rain:
            rain_html = (f'<div style="position: absolute; left: 50%; top: {ICON + 2}px; transform: translateX(-50%); font-size: 24px; '
                         f'line-height: 28px; color: #5C9CE0; white-space: nowrap">{rain}%</div>')
        row = (f'<div style="position: absolute; left: 0; top: {ROW_Y[i] - LH // 2}px; width: 466px; display: flex; justify-content: center; '
               f'animation: wxrow{i} 6s linear infinite">\n'
               f'<div style="display: flex; align-items: center; gap: {GAP}px; height: {LH}px">'
               f'<div style="width: {DAY_W}px; font-size: 30px; line-height: 36px; color: #BDBDBD">{name}</div>'
               f'<div style="position: relative; width: {ICON}px; height: {ICON}px">{icon(sky, i)}{rain_html}</div>'
               f'{low_html}{bar(lo, hi, amin, amax, i)}{high_html}</div>\n</div>')
        rows.append(row)
    desc = '; '.join(f'{n}: {"partly cloudy" if s == "partly" else "rain " + str(r) + " percent" if s == "rain" else "sunny"}, '
                     f'{lo} to {hi} degrees' for n, s, lo, hi, r in DAYS)
    dots = ''.join(f'<circle cx="{x}" cy="{y}" r="4" style="fill: {"#FFFFFF" if k == 5 else "#4A4A4A"}"></circle>\n'
                   for k, (x, y) in enumerate([(193.28, 435.13), (209.07, 437.6), (225.01, 438.84), (240.99, 438.84), (256.93, 437.6), (272.72, 435.13)]))
    html = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Next days</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
<link href="https://fonts.googleapis.com/css2?family=Montserrat:wght@500&display=swap" rel="stylesheet">
<style>
body{{margin:0}}
{chr(10).join(kfs)}
</style>
</helmet>
<div style="width: 466px; height: 466px; position: relative; overflow: hidden; border-radius: 50%; background: #000000; color: #F4F4F2; font-family: Montserrat, 'Avenir Next', 'Segoe UI', sans-serif; font-weight: 500">
<svg viewBox="0 0 466 466" width="466" height="466" aria-hidden="true" style="position: absolute; left: 0; top: 0">
<circle cx="233" cy="233" r="222" style="fill: none; stroke: #262626; stroke-width: 6"></circle>
</svg>
<div role="img" aria-label="Next days. {desc}. Range bars share one temperature axis from {amin} to {amax} degrees" style="position: absolute; left: 0; top: 0; width: 466px; height: 466px">
{chr(10).join(rows)}
</div>
<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="Weather face 6 of 6" style="position: absolute; left: 0; top: 0; pointer-events: none">
{dots}</svg>
</div>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{{"$preview":{{"width":466,"height":466}}}}'>
class Component extends DCLogic {{
renderVals() {{
return {{}};
}}
}}
</script>
</body>
</html>
'''
    open(DST, 'w').write(html)
    print('AN1', [(d[0], d[1], d[2], d[3], d[4], tc(d[2]), tc(d[3])) for d in DAYS], 'axis', amin, amax)


if __name__ == '__main__':
    build()

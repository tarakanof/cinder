import math, os
import edit as E
import temp as T

SRC = os.path.join(E.ORIG, 'AD1.dc.html')
ARRIVE_AT = 16
RAIN_BLUE = '#5C9CE0'
SEG_COLOUR = {'light': '#4F81B7', 'moderate': '#5C9CE0', 'heavy': '#3D7BF5'}
VISUALS = {
    'light': dict(n=6, w=3, length=5, slant=0, dur=1.4, y=180, fall=(0, -3, 0, 9), spread=(172, 288),
                  cloud='#C4CCD4', fill='#E6ECF2', back=None, scale=0.75, word='light rain', pulse=False),
    'moderate': dict(n=9, w=4, length=10, slant=3, dur=0.9, y=176, fall=(2, -4, -2, 12), spread=(145, 315), layout='sides',
                     cloud='#838C97', back=None, scale=1.0, word='rain', pulse=False),
    'heavy': dict(n=14, w=4.5, length=16, slant=6, dur=0.6, y=174, fall=(4, -6, -4, 14), spread=(128, 330), layout='sides',
                  cloud='#6A7480', back='#4A525C', scale=1.15, word='heavy rain', pulse=True),
}
SIDE_CLOUDS = {
    'moderate': [('L', '#8E96A0', '#A8B0BA', 180, [(63.5, -18.9, 29.7), (114.8, -35.1, 35.1), (160.7, -16.2, 21.6)], 8, 0, False),
                 ('R', '#8E96A0', '#A8B0BA', 172, [(305.3, -16.2, 21.6), (351.2, -35.1, 35.1), (402.5, -18.9, 29.7)], 9, -4, False)],
    'heavy': [('L', '#3A4048', '#6A7480', 132, [(20, -16, 28), (66, -36, 32), (118, -30, 30), (166, -14, 22)], 10, -3, False),
              ('R', '#3A4048', '#6A7480', 122, [(300, -14, 22), (348, -30, 30), (400, -36, 32), (446, -16, 28)], 9, -6, False),
              ('L', '#555D68', '#6A7480', 172, [(30, -18, 30), (80, -34, 34), (134, -28, 30), (186, -14, 24)], 8, 0, True),
              ('R', '#555D68', '#6A7480', 162, [(264, -14, 24), (314, -28, 30), (368, -34, 34), (420, -18, 30)], 8.5, -2, True)],
}
SIDE_RAIN = {
    'moderate': [('L', 70, 165, 182, 4), ('R', 302, 400, 174, 5)],
    'heavy': [('L', 62, 196, 172, 7), ('R', 252, 400, 164, 7)],
}
SIDE_FADE_FROM = {'moderate': 180, 'heavy': 160}
SIDE_CLIP = 195
NUMBER_INK_TOP = 220
CELL_STARTS = [-88.5 + 30 * k for k in range(12)]


def clock(t):
    return (t / 60 % 12) * 30


def hhmm(t):
    return f'{t // 60:02d}:{t % 60:02d}'


def polar(c, r):
    a = math.radians(c)
    return round(233 + r * math.sin(a), 2), round(233 - r * math.cos(a), 2)


def countdown(minutes):
    if minutes >= 60:
        halves = round(minutes / 30)
        lab = lambda h: (str(h // 2) if h // 2 or h % 2 == 0 else '') + ('½' if h % 2 else '')
        return lab(halves), 'h', [lab(h) for h in range(halves + 1)]
    m5 = int(round(minutes / 5) * 5)
    return str(m5), 'min', [str(v) for v in range(0, m5 + 1, 5)]


def arc_path(c0, c1, colour, extra=''):
    (x1, y1), (x2, y2) = polar(c0, 222), polar(c1, 222)
    return (f'<path d="M {x1} {y1} A 222 222 0 0 1 {x2} {y2}" style="fill: none; stroke: {colour}; stroke-width: 12{extra}"></path>')


def streak_line(x, y, vis, i, extra_delay=0.0):
    dur = round(vis['dur'] * [1, 1.1, 0.9, 1.05, 0.95][i % 5], 2)
    delay = -round((i * 0.37 + extra_delay) % dur, 2)
    op = [0.85, 1, 0.75, 1, 0.65][i % 5]
    lowest = y + vis['length'] + vis['fall'][3] + vis['w'] / 2
    assert lowest <= NUMBER_INK_TOP - 12, ('rain too low', x, y, lowest)
    return (f'<line x1="{x}" y1="{y}" x2="{x - vis["slant"]}" y2="{y + vis["length"]}" style="stroke: {RAIN_BLUE}; stroke-width: {vis["w"]}; '
            f'stroke-linecap: round; stroke-opacity: {op}; opacity: 0; animation: wxfall {dur}s linear {E.fmt(delay)}s infinite"></line>')


def side_icon(kind, vis):
    groups = []
    rain = {side: [] for side in 'LR'}
    k = 0
    for side, x0, x1, y, cnt in SIDE_RAIN[kind]:
        for i in range(cnt):
            x = round(x0 + (x1 - x0) * (i + 0.5) / cnt)
            rain[side].append(streak_line(x, y + (i % 2) * 2, vis, k, 0.11 * (side == 'R')))
            k += 1
    clouds = SIDE_CLOUDS[kind]
    last_back = {side: max(i for i, c in enumerate(clouds) if c[0] == side) for side in 'LR'}
    for i, (side, fill, stroke, base, circles, br, delay, pulse) in enumerate(clouds):
        body = ''
        if i == last_back[side]:
            body += f'<g style="animation: wxraingate 6s linear infinite">\n' + '\n'.join(rain[side]) + '\n</g>\n'
        anim = '; animation: wxgloom 3.2s ease-in-out infinite' if pulse else ''
        body += (f'<path d="{T.cloud_path(circles, base)}" style="fill: {fill}; stroke: {stroke}; stroke-width: 4.5; '
                 f'stroke-linejoin: round{anim}"></path>')
        groups.append(f'<g style="animation: wxcloud{side} 6s linear infinite"><g style="animation: wxbreathe {br}s ease-in-out {E.fmt(delay)}s infinite">\n'
                      f'{body}\n</g></g>')
    desc = ('grey, layered rain clouds' if kind == 'heavy' else 'light grey rain clouds')
    return (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="Dark {desc} sliding in from both sides of the face, '
            f'{vis["word"]} falling from under them once the drop reaches the rain on the dial, stopping short of the countdown" '
            f'style="position: absolute; left: 0; top: 0">\n'
            f'<defs><radialGradient id="wxsidefade" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="{SIDE_CLIP}">'
            f'<stop offset="{SIDE_FADE_FROM[kind] / SIDE_CLIP:.2f}" stop-color="#FFFFFF"></stop><stop offset="1" stop-color="#000000"></stop></radialGradient>'
            f'<mask id="wxsidemask" maskUnits="userSpaceOnUse" x="0" y="0" width="466" height="466">'
            f'<circle cx="233" cy="233" r="{SIDE_CLIP}" style="fill: url(#wxsidefade)"></circle></mask></defs>\n'
            '<g mask="url(#wxsidemask)">\n' + '\n'.join(groups) + '\n</g>\n</svg>')


def sub_once(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


def build(name, now, rain, title=None):
    s = open(SRC).read()
    start, end = rain[0][0], rain[-1][1]
    kind = rain[0][2]
    vis = VISUALS[kind]
    now_c, start_c = clock(now), clock(start)

    lit = []
    for rs, re_, inten in rain:
        a, b = clock(rs), clock(rs) + (re_ - rs) / 2
        for k in range(12):
            c0, c1 = 30 * k + 1.5, 30 * k + 28.5
            lo, hi = max(a, c0), min(b, c1)
            if hi - lo > 0.2:
                lit.append(arc_path(lo, hi, SEG_COLOUR[inten], '; opacity: 1; animation: wxglow 6s linear infinite'))
    for rot in ('61.5', '91.5'):
        s = sub_once(s, f'transform="rotate({rot} 233 233)" style="fill: none; stroke: #5C9CE0; stroke-width: 12; stroke-dasharray: 104.62 1394.87; opacity: 0.6"',
                     f'transform="rotate({rot} 233 233)" style="fill: none; stroke: #262626; stroke-width: 12; stroke-dasharray: 104.62 1394.87"')
    mx, my = polar(now_c, 222)
    assert (round(mx, 1), round(my, 1)) == (447.4, 175.5), (mx, my)
    marker_outer = f'<circle cx="447.44" cy="175.54" r="17" style="fill: #000000"></circle>'
    s = sub_once(s, marker_outer, '\n'.join(lit) + '\n' + marker_outer)

    cell_start = 30 * math.floor(start_c / 30) + 1.5
    target = max(start_c, cell_start) + 1.5
    travel = target - now_c
    drop = (f'<g style="transform-origin: 233px 233px; animation: wxslide 6s linear infinite">\n'
            f'<g style="opacity: 0; animation: wxdropfade 6s linear infinite">\n'
            f'<path d="M -15 0 C -9 -1.5, -6.5 -7, 0 -7 A 7 7 0 0 1 0 7 C -6.5 7, -9 1.5, -15 0 Z" transform="translate(447.44 175.54) rotate({E.fmt(now_c)})" '
            f'style="fill: {RAIN_BLUE}; stroke: #000000; stroke-width: 2; stroke-linejoin: round"></path>\n</g>\n</g>')
    label = hhmm(start)[:2] if start % 60 == 0 else hhmm(start)
    lc = start_c
    if 45 < lc % 360 < 135:
        anchor, r = 'end', 198
    elif 225 < lc % 360 < 315:
        anchor, r = 'start', 198
    else:
        anchor, r = 'middle', 188
    lx, ly = polar(lc, r)
    arc_label = (f'<text x="{lx}" y="{ly}" text-anchor="{anchor}" dominant-baseline="central" '
                 f'style="font-size: 22px; fill: {RAIN_BLUE}; opacity: 1; animation: wxlabelin 6s linear infinite">{label}</text>')
    marker_inner = '<circle cx="447.44" cy="175.54" r="10" style="fill: #000000; stroke: #F4F4F2; stroke-width: 5"></circle>'
    s = sub_once(s, marker_inner, marker_inner + '\n' + drop + '\n' + arc_label)
    dial_label = (f'Clock dial of the next 12 hours, dry now at {hhmm(now)} with a ring marking the time; a drop slides from now to the rain, '
                  + '; '.join(f'{inten} rain from {hhmm(a)} to {hhmm(b)}' for a, b, inten in rain))
    s = sub_once(s, "aria-label=\"Clock dial of the next 12 hours, dry now at half past 2 with a ring marking the time, light rain at the 5 and 6 o'clock positions\"",
                 f'aria-label="{dial_label}"')

    final, unit, labels = countdown(start - now)
    n = len(labels) - 1
    kfs = [
        '@keyframes wxslide{0%{transform:rotate(0deg);animation-timing-function:cubic-bezier(0.45,0,0.25,1)}'
        f'{ARRIVE_AT}%,100%{{transform:rotate({E.fmt(round(travel, 2))}deg)}}}}',
        f'@keyframes wxdropfade{{0%{{opacity:0}}3%{{opacity:1}}{ARRIVE_AT}%{{opacity:1}}20%,100%{{opacity:0}}}}',
        f'@keyframes wxglow{{0%,{ARRIVE_AT}%{{opacity:0.6}}22%,100%{{opacity:1}}}}',
        f'@keyframes wxlabelin{{0%,{ARRIVE_AT}%{{opacity:0}}22%,100%{{opacity:1}}}}',
        '@keyframes wxcome{0%{transform:translateX(90px);opacity:0;animation-timing-function:' + E.EASE + '}10%{opacity:1}22%,100%{transform:translateX(0px);opacity:1}}',
        '@keyframes wxbreathe{0%,100%{transform:translateX(-4px)}50%{transform:translateX(4px)}}',
        f'@keyframes wxraingate{{0%,{ARRIVE_AT}%{{opacity:0}}19%,100%{{opacity:1}}}}',
        '@keyframes wxfall{{0%{{transform:translate({0}px,{1}px);opacity:0}}25%{{opacity:1}}75%{{opacity:1}}100%{{transform:translate({2}px,{3}px);opacity:0}}}}'.format(*vis['fall']),
        '@keyframes wxgloom{0%,100%{opacity:1}50%{opacity:0.7}}',
        E.count_kf(n),
    ]
    if vis.get('layout') == 'sides':
        kfs += ['@keyframes wxcloudL{0%{transform:translateX(-170px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translateX(0px)}}',
                '@keyframes wxcloudR{0%{transform:translateX(170px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translateX(0px)}}']
    s = sub_once(s, '@keyframes wxcome{0%{transform:translateX(84px);opacity:0}15%{opacity:1}65%{transform:translateX(0px);opacity:1}88%{transform:translateX(0px);opacity:1}100%{transform:translateX(0px);opacity:0}}\n', '')
    s = sub_once(s, '@keyframes wxdrops{0%,64%{opacity:0;transform:translateY(-6px)}72%{opacity:1}88%{opacity:1;transform:translateY(8px)}100%{opacity:0;transform:translateY(10px)}}\n', '')
    s = E.add_keyframes(s, kfs)
    s = s.replace('<style>\nbody{margin:0}\n\n', '<style>\nbody{margin:0}\n')

    s = sub_once(s, '<div style="position: absolute; left: 0; top: 76px; width: 466px; text-align: center; font-size: 24px; line-height: 30px; color: #BDBDBD">Rain</div>\n', '')

    BASE = 170
    k, cx0 = vis['scale'], 230
    circles = [(round(cx0 + (x - cx0) * k, 1), round(dy * k, 1), round(r * k, 1))
               for x, dy, r in [(148, -18, 26), (194, -40, 36), (250, -48, 38), (312, -24, 30)]]
    front = T.cloud_path(circles, BASE)
    back = ''
    if vis['back']:
        back = (f'<path d="{T.cloud_path([(x + 18, dy, r) for x, dy, r in circles], BASE - 12)}" '
                f'style="fill: #000000; stroke: {vis["back"]}; stroke-width: 4.5; stroke-linejoin: round"></path>\n')
    streaks = []
    nst = vis['n']
    x0, x1 = vis['spread']
    lowest = vis['y'] + vis['length'] + vis['fall'][3] + vis['w'] / 2
    assert lowest <= NUMBER_INK_TOP - 12, (kind, lowest)
    for i in range(nst):
        x = round(x0 + (x1 - x0) * (i + 0.5) / nst)
        y = vis['y'] + (i % 3) * 2 - 2 if kind != 'heavy' else vis['y']
        dur = round(vis['dur'] * [1, 1.1, 0.9, 1.05, 0.95][i % 5], 2)
        delay = -round((i * 0.37) % dur, 2)
        op = [0.85, 1, 0.75, 1, 0.65][i % 5]
        streaks.append(f'<line x1="{x}" y1="{y}" x2="{x - vis["slant"]}" y2="{y + vis["length"]}" style="stroke: {RAIN_BLUE}; stroke-width: {vis["w"]}; '
                       f'stroke-linecap: round; stroke-opacity: {op}; opacity: 0; animation: wxfall {dur}s linear {E.fmt(delay)}s infinite"></line>')
    pulse = '; animation: wxgloom 3.2s ease-in-out infinite' if vis['pulse'] else ''
    i0 = s.index('<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="A rain cloud drifting in from the right')
    i1 = s.index('</svg>', i0) + len('</svg>')
    icon = (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="A {"dark, layered " if vis["back"] else "white " if vis.get("fill") else "large "}rain cloud drifting in from the right, '
            f'starting {vis["word"]} once the drop reaches the rain on the dial, the rain stopping short of the countdown" style="position: absolute; left: 0; top: 0">\n'
            '<g style="animation: wxcome 6s linear infinite">\n<g style="animation: wxbreathe 8s ease-in-out infinite">\n'
            + back +
            '<g style="animation: wxraingate 6s linear infinite">\n' + '\n'.join(streaks) + '\n</g>\n'
            f'<path d="{front}" style="fill: {vis.get("fill", "#000000")}; stroke: {vis["cloud"]}; stroke-width: 4.5; stroke-linejoin: round{pulse}"></path>\n'
            '</g>\n</g>\n</svg>')
    if vis.get('layout') == 'sides':
        icon = side_icon(kind, vis)
    s = s[:i0] + icon + s[i1:]

    style = f'font-size: 96px; line-height: 84px; letter-spacing: -2px; color: {RAIN_BLUE}'
    row = ('<div style="display: flex; align-items: baseline; gap: 6px">\n'
           + E.counter_wrapper(style, final, labels, lambda v: RAIN_BLUE) + '\n</div>\n'
           f'<div style="font-size: 30px; line-height: 36px; color: #9A9A9A">{unit}</div>\n</div>')
    s = sub_once(s, '<div style="font-size: 96px; line-height: 84px; letter-spacing: -2px; color: #5C9CE0">17:00</div>', row)
    s = sub_once(s, '<div style="font-size: 30px; line-height: 36px; color: #BDBDBD">light rain, 2 h</div>',
                 f'<div style="font-size: 30px; line-height: 36px; color: #BDBDBD; white-space: nowrap">'
                 f'<span style="color: {RAIN_BLUE}">{vis["word"]}</span> at {hhmm(start)}</div>')
    if title:
        s = sub_once(s, '<title>Rain forecast, cloud coming</title>', f'<title>{title}</title>')
    import opt2
    if name in opt2.AD_OPT:
        kind_, title_, budget_ = opt2.AD_OPT[name]
        s = opt2.ad_optimize(s, name, kind_, title_, budget_)
    open(os.path.join(E.PROJ, name + '.dc.html'), 'w').write(s)
    print(name, dict(countdown=final + ' ' + unit, labels=labels, frames=n, travel=round(travel, 2), arcs=len(lit),
                     label=(label, lx, ly, anchor), visuals=kind))


if __name__ == '__main__':
    NOW = 14 * 60 + 30
    build('AD4', NOW, [(17 * 60, 19 * 60, 'light')], title='Rain forecast, light later')
    build('AD5', NOW, [(16 * 60, 17 * 60 + 30, 'moderate')], title='Rain forecast, moderate')
    build('AD3', NOW, [(15 * 60 + 15, 16 * 60 + 30, 'heavy')], title='Rain forecast, heavy soon')

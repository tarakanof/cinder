import math, os, re
import edit as E

T0, T1 = 6 * 60 + 55, 20 * 60 + 5
SUNRISE, SUNSET = 7 * 60 + 30, 19 * 60 + 30
GOLD_AM, GOLD_PM = 8 * 60 + 30, 18 * 60 + 30
NOW = 10 * 60 + 30
C = 2 * math.pi * 222

BLUE_OUT, BLUE_IN = '#4A4FC8', '#3E6FD8'
GOLD_HZN, GOLD_HIGH = '#FF9A3D', '#FFB547'
DAY_EDGE, DAY_NOON = '#7E5A20', '#B5832A'


def clock(t):
    return 225 + 270 * (t - T0) / (T1 - T0)


def lerp(c1, c2, f):
    a, b = E.hex2rgb(c1), E.hex2rgb(c2)
    return E.rgb2hex([a[i] + (b[i] - a[i]) * f for i in range(3)])


MAUVE, DUSK_PINK = '#9A6A9E', '#E08A5A'
BLEND = 17


def day_colour(t):
    mid = (GOLD_AM + GOLD_PM) / 2
    return lerp(DAY_EDGE, DAY_NOON, 1 - abs(t - mid) / (mid - GOLD_AM))


STOPS = [(T0, BLUE_OUT), (SUNRISE - BLEND, BLUE_IN), (SUNRISE - 5, MAUVE), (SUNRISE + 6, DUSK_PINK),
         (SUNRISE + BLEND, GOLD_HZN), (GOLD_AM - BLEND, GOLD_HIGH), (GOLD_AM + BLEND, day_colour(GOLD_AM + BLEND)),
         ((GOLD_AM + GOLD_PM) / 2, DAY_NOON)]


def colour_at(t):
    t = min(t, (T0 + T1) - t)
    for (ta, ca), (tb, cb) in zip(STOPS, STOPS[1:]):
        if ta <= t <= tb:
            if ta == GOLD_AM + BLEND:
                return day_colour(t)
            return lerp(ca, cb, (t - ta) / (tb - ta))
    return STOPS[-1][1]


PIECES = 150
OVERLAP_DEG = math.degrees(0.6 / 222)


def segments():
    out = []
    for k in range(PIECES):
        a, b = T0 + (T1 - T0) * k / PIECES, T0 + (T1 - T0) * (k + 1) / PIECES
        ca, cb = clock(a), clock(b) + (OVERLAP_DEG if k < PIECES - 1 else 0)
        (x1, y1), (x2, y2) = polar(ca, 222), polar(cb, 222)
        out.append(f'<path d="M {x1} {y1} A 222 222 0 0 1 {x2} {y2}" style="fill: none; stroke: {colour_at((a + b) / 2)}; stroke-width: 12"></path>')
    return out


def polar(clk, r):
    a = math.radians(clk)
    return round(233 + r * math.sin(a), 2), round(233 - r * math.cos(a), 2)


def ticks():
    out = []
    for t in (SUNRISE, SUNSET):
        (x1, y1), (x2, y2) = polar(clock(t), 199), polar(clock(t), 210)
        out.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" style="stroke: {GOLD_HZN}; stroke-width: 2; stroke-linecap: round"></line>')
    return out


OLD_GAUGE_COLOURS = ['#6A4418', '#7F551C', '#956521', '#AA7625', '#C08729', '#D5982E', '#D5982E', '#C08729', '#AA7625',
                     '#956521', '#7F551C', '#6A4418']
OLD_ROT = ['135', '157.5', '180', '202.5', '225', '247.5', '270', '292.5', '315', '337.5', '0', '22.5']


HZ_Y, HZ_X0, HZ_X1 = 196, 125, 341
SUN_CX, SUN_CY, SUN_R = 233, 178, 42
RISE = 64


def hero_sunrise(s):
    s = E.sub_once(s, '<div style="position: absolute; left: 0; top: 76px; width: 466px; text-align: center; font-size: 24px; line-height: 30px; color: #BDBDBD">Sun</div>\n', '')
    for kf in ('wxrise', 'wxrays'):
        s = re.sub(r'@keyframes ' + kf + r'\{.*\}\n', '', s)
    rays = []
    n = 13
    for k in range(n):
        ang = -168 + k * (156 / (n - 1))
        c, si = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        r1 = SUN_R + 12
        r2 = 170 if k % 2 == 0 else 130
        rays.append(f'<line x1="{round(SUN_CX + r1 * c, 1)}" y1="{round(SUN_CY + r1 * si, 1)}" x2="{round(SUN_CX + r2 * c, 1)}" '
                    f'y2="{round(SUN_CY + r2 * si, 1)}" style="stroke: url(#wxriseray); stroke-width: {3.5 if k % 2 == 0 else 3}; stroke-linecap: round"></line>')
    defs = ('<defs>'
            '<radialGradient id="wxriserim" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="195">'
            '<stop offset="0.82" stop-color="#FFFFFF"></stop><stop offset="1" stop-color="#000000"></stop></radialGradient>'
            '<mask id="wxrisemask" maskUnits="userSpaceOnUse" x="0" y="0" width="466" height="466">'
            f'<rect x="0" y="0" width="466" height="{HZ_Y - 2}" style="fill: url(#wxriserim)"></rect></mask>'
            f'<radialGradient id="wxrisefill" gradientUnits="userSpaceOnUse" cx="{SUN_CX}" cy="{SUN_CY}" r="{SUN_R}">'
            '<stop offset="0" stop-color="#FFE07A"></stop><stop offset="0.6" stop-color="#FFB547"></stop><stop offset="1" stop-color="#F08A3A"></stop></radialGradient>'
            f'<radialGradient id="wxriseglow" gradientUnits="userSpaceOnUse" cx="{SUN_CX}" cy="{SUN_CY}" r="74">'
            f'<stop offset="{SUN_R / 74:.2f}" stop-color="#FFB547" stop-opacity="0.24"></stop><stop offset="1" stop-color="#FFB547" stop-opacity="0"></stop></radialGradient>'
            f'<radialGradient id="wxriseray" gradientUnits="userSpaceOnUse" cx="{SUN_CX}" cy="{SUN_CY}" r="170">'
            '<stop offset="0.3" stop-color="#FFB547" stop-opacity="0.55"></stop><stop offset="1" stop-color="#FFB547" stop-opacity="0"></stop></radialGradient>'
            '</defs>')
    dash_y = HZ_Y + 8
    icon = (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="A large golden sun rising above a wide horizon as the face appears, '
            f'long thin rays fanning upward" style="position: absolute; left: 0; top: 0">\n{defs}\n'
            '<g mask="url(#wxrisemask)">\n'
            f'<g style="animation: wxsunrise 6s linear infinite">\n'
            f'<g style="opacity: 1; animation: wxrayson 6s linear infinite"><g style="transform-origin: {SUN_CX}px {SUN_CY}px; animation: wxraysway 9s ease-in-out infinite">\n'
            + '\n'.join(rays) + '\n</g></g>\n'
            f'<circle cx="{SUN_CX}" cy="{SUN_CY}" r="74" style="fill: url(#wxriseglow)"></circle>\n'
            f'<circle cx="{SUN_CX}" cy="{SUN_CY}" r="{SUN_R}" style="fill: url(#wxrisefill); stroke: #FFB547; stroke-width: 1.5; opacity: 0.9"></circle>\n'
            '</g>\n</g>\n'
            f'<line x1="{HZ_X0}" y1="{HZ_Y}" x2="{HZ_X1}" y2="{HZ_Y}" style="stroke: #9AA4B0; stroke-width: 4.5; stroke-linecap: round"></line>\n'
            f'<line x1="168" y1="{dash_y}" x2="208" y2="{dash_y}" style="stroke: #5A5A5A; stroke-width: 3; stroke-linecap: round"></line>\n'
            f'<line x1="258" y1="{dash_y}" x2="298" y2="{dash_y}" style="stroke: #5A5A5A; stroke-width: 3; stroke-linecap: round"></line>\n'
            '</svg>')
    i0 = s.index('<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="Sun rising above a horizon line')
    i1 = s.index('</svg>', i0) + len('</svg>')
    s = s[:i0] + icon + s[i1:]
    kfs = ['@keyframes wxsunrise{0%{transform:translateY(' + str(RISE) + 'px);animation-timing-function:' + E.EASE + '}22%,100%{transform:translateY(0px)}}',
           '@keyframes wxrayson{0%,12%{opacity:0}24%,100%{opacity:1}}',
           '@keyframes wxraysway{0%,100%{transform:rotate(-3deg)}50%{transform:rotate(3deg)}}']
    return E.add_keyframes(s, kfs)


def sun_face(name, halo):
    s = open(os.path.join(E.ORIG, name + '.dc.html')).read()
    old_lines = []
    for rot, c in zip(OLD_ROT, OLD_GAUGE_COLOURS):
        dash = '87.18' if rot == '22.5' else '88.2'
        old_lines.append(f'<circle cx="233" cy="233" r="222" transform="rotate({rot} 233 233)" style="fill: none; stroke: {c}; stroke-width: 12; stroke-dasharray: {dash} 1394.87"></circle>')
    s = E.sub_once(s, '\n'.join(old_lines), '\n'.join(segments() + ticks()))
    s = E.sub_once(s, '<circle cx="76.02" cy="389.98" r="6" style="fill: #6A4418"></circle>',
                   f'<circle cx="76.02" cy="389.98" r="6" style="fill: {BLUE_OUT}"></circle>')
    s = E.sub_once(s, '<circle cx="389.98" cy="389.98" r="6" style="fill: #6A4418"></circle>',
                   f'<circle cx="389.98" cy="389.98" r="6" style="fill: {BLUE_OUT}"></circle>')
    s = E.sub_once(s, 'color: #BDBDBD">07:30</div>', f'color: {GOLD_HZN}">07:30</div>')
    s = E.sub_once(s, 'color: #BDBDBD">19:30</div>', f'color: {GOLD_HZN}">19:30</div>')
    s = E.sub_once(s, '<div style="font-size: 30px; line-height: 36px; color: #BDBDBD">of 12 h daylight</div>',
                   f'<div style="font-size: 30px; line-height: 36px; color: {GOLD_HIGH}; white-space: nowrap">golden hour 18:30</div>')
    old_label = ('Gauge of the day from sunrise at 07:30 to sunset at 19:30, with a glowing ring marking now' if name == 'AS1'
                 else 'Gauge of the day from sunrise at 07:30 to sunset at 19:30, with a ring marking now')
    s = E.sub_once(s, old_label, 'Gauge of the day from the morning blue hour at 06:55 to the end of the evening blue hour at 20:05, '
                                 'with golden hours after sunrise at 07:30 and before sunset at 19:30, the marker sweeping up to now, 10:30, '
                                 '9 hours of daylight left')
    mx, my = polar(clock(NOW), 222)
    s = s.replace('cx="27.9" cy="148.04"', f'cx="{mx}" cy="{my}"')
    s = s.replace('transform-origin: 27.9px 148.04px', f'transform-origin: {mx}px {my}px')
    halo_str = halo.replace('27.9', str(mx)).replace('148.04', str(my)) if halo else None
    s = E.row_face(name, mx, my, 9, '#E0A030', lambda v: '#E0A030', halo=halo_str)(s)
    if name == 'AS2':
        s = hero_sunrise(s)
    s = E.end_labels(s)
    open(os.path.join(E.PROJ, name + '.dc.html'), 'w').write(s)
    print(name, dict(marker=(mx, my), clock=round(clock(NOW), 2), delta=E.delta_for(mx, my)[0],
                     sunrise_clock=round(clock(SUNRISE), 2), sunset_clock=round(clock(SUNSET), 2),
                     gold_am=round(clock(GOLD_AM), 2), gold_pm=round(clock(GOLD_PM), 2)))


if __name__ == '__main__':
    E.report.clear()
    sun_face('AS2', None)

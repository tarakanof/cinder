import os, math
import edit as E
import temp as T
import wind as W

CYCLE = 6.0
FPS = 15
F = 1 / FPS
SPRITE_START = 2 / FPS
SPRITE_IN, HOLD_END, SPRITE_OUT = 8 / FPS, 58 / FPS, 64 / FPS
TEXT_IN = (0, 1, 2)
TEXT_OUT = (65, 66, 67)
TEXT_TOP = 310
TEXT_BOX = (83, 310, 383, 346)
BOX = (156, 62, 308, 152)
RISE = 7
SPRITE_K = 0.9
SPRITE_E, SPRITE_F = round(232 - 233 * SPRITE_K, 2), round(62 - 84 * SPRITE_K, 2)
EYE_BOXES = {'B2-Glance': (138, 158, 327, 265), 'B2-Rain': (138, 158, 327, 265), 'B2-Storm': (138, 166, 327, 265),
             'B2-Snow': (125, 163, 340, 284), 'B2-Wind': (138, 172, 384, 270), 'B2-Heat': (138, 173, 327, 265),
             'B2-Night': (125, 176, 327, 295), 'B2-AfterRain': (132, 158, 333, 265), 'B2-RainWorking': (90, 176, 368, 300)}
ICON_BOX = (197, 331, 269, 379)
WHITE = '#F4F4F2'
BLUE = '#5C9CE0'
MOODS = {'idle': '#888888', 'working': '#2EE85E', 'waiting': '#FFC14D', 'done': '#4FA9FF', 'error': '#FF3A3A'}
FONT = "font-family: Montserrat, 'Avenir Next', 'Segoe UI', sans-serif; font-weight: 500"


def track(name, pts, mode='ease-in-out', cycle=CYCLE):
    pts = [p if len(p) == 3 else (p[0], p[1], None) for p in sorted(pts, key=lambda p: p[0])]
    if pts[0][0] > 0:
        pts.insert(0, (0.0, pts[0][1], None))
    if pts[-1][0] < cycle:
        pts.append((cycle, pts[-1][1], None))
    out = []
    for i, (t, decl, tf) in enumerate(pts):
        pct = E.fmt(round(t / cycle * 100, 3))
        if i == len(pts) - 1:
            out.append(f'{pct}%{{{decl}}}')
            continue
        t1, d1, _ = pts[i + 1]
        if tf is None:
            if mode == 'step15':
                tf = f'steps({max(1, round((t1 - t) * FPS))},end)' if d1 != decl else 'step-end'
            else:
                tf = mode if d1 != decl else 'step-end'
        out.append(f'{pct}%{{{decl};animation-timing-function:{tf}}}')
    return f'@keyframes {name}{{' + ''.join(out) + '}'


def loop15(name, period, pts):
    return track(name, pts, 'step15', period)


def anim(name, dur, delay=0.0, extra=''):
    return f'animation: {name} {E.fmt(dur)}s linear {E.fmt(delay)}s infinite{extra}'


def sprite_kf(name):
    return track(name, [(0, f'opacity:0;transform:translateY({RISE}px)'), (SPRITE_START, f'opacity:0;transform:translateY({RISE}px)'),
                        (SPRITE_IN, 'opacity:1;transform:translateY(0px)'),
                        (HOLD_END, 'opacity:1;transform:translateY(0px)'), (SPRITE_OUT, f'opacity:0;transform:translateY({RISE}px)')], 'step15')


def label_arc(name):
    return f'<path id="b2-arc-{name}" d="M 57 233 A 176 176 0 0 0 409 233" style="fill: none; stroke: none"></path>'


def label_text(name, text, colour):
    return (f'<text style="{FONT}; font-size: 30px; letter-spacing: 1px; fill: {colour}; text-anchor: middle">'
            f'<textPath href="#b2-arc-{name}" startOffset="50%">{text}</textPath></text>')


def text_line(text, colour):
    return (f'<div style="position: absolute; left: {TEXT_BOX[0]}px; top: {TEXT_TOP}px; width: {TEXT_BOX[2] - TEXT_BOX[0]}px; text-align: center; '
            f'font-size: 30px; line-height: 36px; white-space: nowrap; color: {colour}; opacity: 0; animation: b2text 6s linear infinite">{text}</div>')


TEXT_KF = [track('b2text', [(0, 'opacity:0.34'), (TEXT_IN[1] * F, 'opacity:0.67'), (TEXT_IN[2] * F, 'opacity:1'),
                            (TEXT_OUT[0] * F, 'opacity:0.67'), (TEXT_OUT[1] * F, 'opacity:0.34'), (TEXT_OUT[2] * F, 'opacity:0')], 'step-end'),
           track('b2icon', [(0, 'opacity:0'), (TEXT_OUT[2] * F, 'opacity:1')], 'step-end')]


def tool_icon():
    x0, y0, x1, y1 = ICON_BOX
    cx = (x0 + x1) / 2
    return (f'<g style="animation: b2icon 6s linear infinite">'
            f'<rect x="{cx - 22}" y="{y0 + 8}" width="44" height="32" rx="7" style="fill: none; stroke: {WHITE}; stroke-width: 3"></rect>'
            f'<path d="M {cx - 12} {y0 + 18} L {cx - 5} {y0 + 24} L {cx - 12} {y0 + 30}" style="fill: none; stroke: {WHITE}; stroke-width: 3; stroke-linecap: round; stroke-linejoin: round"></path>'
            f'<line x1="{cx - 1}" y1="{y0 + 31}" x2="{cx + 11}" y2="{y0 + 31}" style="stroke: {WHITE}; stroke-width: 3; stroke-linecap: round"></line></g>')


def sun_disc(cx, cy, r, gid):
    return (f'<defs><radialGradient id="{gid}" gradientUnits="userSpaceOnUse" cx="{cx - r * 0.3:.1f}" cy="{cy - r * 0.3:.1f}" r="{r * 1.3:.1f}">'
            '<stop offset="0" stop-color="#FFE07A"></stop><stop offset="0.6" stop-color="#FFB547"></stop><stop offset="1" stop-color="#F08A3A"></stop></radialGradient></defs>'
            f'<circle cx="{cx}" cy="{cy}" r="{r}" style="fill: url(#{gid}); stroke: #FFB547; stroke-width: 1.5"></circle>')


def filled_cloud(circles, base, tone, extra=''):
    front, _, outline = T.CLOUD_TONES[tone]
    return f'<path d="{T.cloud_path(circles, base)}" style="fill: {front}; stroke: {outline}; stroke-width: 4.5; stroke-linejoin: round{extra}"></path>'


def sprite_glance():
    kf = [loop15('b2drift', 3.2, [(0, 'transform:translateX(-4px)'), (1.6, 'transform:translateX(4px)'), (3.2, 'transform:translateX(-4px)')])]
    svg = (f'<circle cx="262" cy="116" r="30" style="fill: url(#b2glow)"></circle>'
           '<defs><radialGradient id="b2glow" gradientUnits="userSpaceOnUse" cx="262" cy="116" r="30"><stop offset="0.7" stop-color="#FFB547" stop-opacity="0.28"></stop>'
           '<stop offset="1" stop-color="#FFB547" stop-opacity="0"></stop></radialGradient></defs>'
           + sun_disc(262, 116, 21, 'b2sun')
           + f'<g style="{anim("b2drift", 3.2)}">' + filled_cloud([(198, -13, 19), (233, -27, 25), (266, -12, 17)], 166, 'white') + '</g>')
    return svg, kf, 'a small sun peeking over a white cloud'


def umbrella_poses(prefix, t_half=SPRITE_START + 2 * F, t_open=SPRITE_START + 4 * F):
    O, D, H = '#FF8A3D', '#D9692A', '#C9D2DC'
    xs, rim = [181, 207, 233, 259, 285], 132
    scal = ''.join(f' A 13 7 0 0 0 {xs[i - 1]} {rim}' for i in range(4, 0, -1))
    open_d = f'M 181 {rim} A 52 36 0 0 1 285 {rim}{scal} Z'
    mid = f'M 233 96 Q 213 104 207 {rim} A 13 7 0 0 1 233 {rim} A 13 7 0 0 1 259 {rim} Q 253 104 233 96 Z'
    handle = lambda y0: f'<path d="M 233 {y0} V 166 A 7 7 0 0 1 219 166" style="fill: none; stroke: {H}; stroke-width: 4.5; stroke-linecap: round"></path>'
    tip = f'<line x1="233" y1="97" x2="233" y2="89" style="stroke: {O}; stroke-width: 4; stroke-linecap: round"></line>'
    closed = (handle(148) + f'<path d="M 233 92 Q 244 120 237 150 L 229 150 Q 222 120 233 92 Z" style="fill: {O}"></path>'
              f'<path d="M 233 96 Q 236 122 233 148" style="fill: none; stroke: {D}; stroke-width: 2"></path>')
    half = (handle(140) + tip + f'<path d="M 205 142 Q 211 104 233 96 Q 255 104 261 142 Q 247 136 233 142 Q 219 136 205 142 Z" style="fill: {O}"></path>'
            f'<path d="M 233 96 Q 226 118 225 139 Q 229 138 233 142 Q 237 138 241 139 Q 240 118 233 96 Z" style="fill: {D}"></path>')
    full = handle(rim) + tip + f'<path d="{open_d}" style="fill: {O}"></path><path d="{mid}" style="fill: {D}"></path>'
    kfs = [track(f'{prefix}c', [(0, 'opacity:1'), (t_half, 'opacity:0')], 'step-end'),
           track(f'{prefix}h', [(0, 'opacity:0'), (t_half, 'opacity:1'), (t_open, 'opacity:0')], 'step-end'),
           track(f'{prefix}o', [(0, 'opacity:0'), (t_open, 'opacity:1')], 'step-end')]
    svg = ''.join(f'<g style="opacity: {1 if k == "o" else 0}; {anim(prefix + k, 6)}">{body}</g>' for k, body in (('c', closed), ('h', half), ('o', full)))
    return svg, kfs


def sprite_rain(prefix='b2u'):
    svg, kfs = umbrella_poses(prefix)
    kfs.append(loop15('b2drop', 0.8, [(0, 'transform:translate(3px,0px);opacity:0'), (0.133, 'transform:translate(2.5px,12px);opacity:1'),
                                      (0.667, 'transform:translate(-2px,64px);opacity:1'), (0.8, 'transform:translate(-3px,78px);opacity:0')]))
    drops = ''.join(f'<line x1="{x}" y1="96" x2="{x - 3}" y2="106" style="stroke: {BLUE}; stroke-width: 4; stroke-linecap: round; opacity: 0; '
                    f'{anim("b2drop", 0.8, -k * 3 * F)}"></line>' for k, x in enumerate((166, 296, 180, 306)))
    return drops + svg, kfs, 'a small orange umbrella popping open, a few raindrops falling past it'


FLASHES = (0.8, 1.2, 1.6)


def sprite_storm():
    bolt = ('<polygon points="236,126 258,126 246,148 260,148 228,178 236,156 222,156" '
            'style="fill: #F0D040; stroke: #F0D040; stroke-width: 2; stroke-linejoin: round"></polygon>')
    on = [(0, 'opacity:0')]
    for f0 in FLASHES:
        on += [(f0, 'opacity:1'), (f0 + 2 * F, 'opacity:0')]
    kfs = [track('b2flash', on, 'step-end'),
           track('b2dark', [(0, 'opacity:1')] + [p for f0 in FLASHES for p in ((f0, 'opacity:0'), (f0 + 2 * F, 'opacity:1'))], 'step-end')]
    circles, base = [(198, -12, 18), (233, -22, 24), (268, -12, 18)], 134
    svg = (f'<g style="opacity: 0; {anim("b2flash", 6)}">{bolt}</g>'
           f'<g style="opacity: 0; {anim("b2flash", 6)}">{filled_cloud(circles, base, "light")}</g>'
           f'<g style="{anim("b2dark", 6)}">{filled_cloud(circles, base, "dark")}</g>')
    return svg, kfs, 'a dark storm cloud with a yellow lightning bolt flashing three times beneath it, the cloud lighting up with each flash'


def flake(x, y, r=6):
    return ''.join(f'<line x1="{x - r}" y1="{y}" x2="{x + r}" y2="{y}" transform="rotate({a} {x} {y})" style="stroke: #D0DCE8; stroke-width: 2.5; stroke-linecap: round"></line>'
                   for a in (0, 60, 120))


def sprite_snow():
    kfs = [loop15('b2flake', 2.4, [(0, 'transform:translate(0px,-6px);opacity:0'), (0.4, 'transform:translate(3px,2px);opacity:1'),
                                   (1.2, 'transform:translate(-3px,20px);opacity:1'), (2.0, 'transform:translate(3px,33px);opacity:1'),
                                   (2.4, 'transform:translate(0px,40px);opacity:0')])]
    flakes = ''.join(f'<g style="opacity: 0; {anim("b2flake", 2.4, -k * 7 * F)}">{flake(x, 132)}</g>'
                     for k, x in enumerate((192, 236, 214, 280, 258)))
    cloud = filled_cloud([(206, -10, 15), (233, -20, 20), (260, -10, 14)], 128, 'white')
    return flakes + cloud, kfs, 'a small white cloud with five snowflakes drifting down beneath it'


def sprite_wind():
    rows = [(103, 54, 0.9, 0, True), (124, 40, 0.9, -6 * F, False), (148, 58, 0.9, -3 * F, True), (170, 36, 0.9, -9 * F, False)]
    kfs = [loop15('b2gust', 0.9, [(0, 'transform:translateX(0px);opacity:0'), (0.2, 'transform:translateX(22px);opacity:0.9'),
                                  (0.7, 'transform:translateX(78px);opacity:0.9'), (0.9, 'transform:translateX(92px);opacity:0')]),
           loop15('b2leaf', 1.6, [(0, 'transform:translate(0px,0px);opacity:0'), (0.2, 'transform:translate(18px,-6px);opacity:1'),
                                  (0.8, 'transform:translate(70px,6px);opacity:1'), (1.4, 'transform:translate(122px,-4px);opacity:1'),
                                  (1.6, 'transform:translate(136px,2px);opacity:0')])]
    out = []
    for y, ln, per, dl, curl in rows:
        x0 = 156
        d = f'M {x0} {y} H {x0 + ln}' + (f' A 7 7 0 1 0 {x0 + ln - 7} {y - 7}' if curl else '')
        out.append(f'<path d="{d}" style="fill: none; stroke: #D0DCE8; stroke-width: 4; stroke-linecap: round; opacity: 0; {anim("b2gust", per, dl)}"></path>')
    leaf = (f'<g style="opacity: 0; {anim("b2leaf", 1.6, -8 * F)}"><path d="M 0 0 C 4 -5, 12 -5, 16 0 C 12 5, 4 5, 0 0 Z" '
            f'transform="translate(158 136) rotate(-20)" style="fill: #8ACF7A"></path></g>')
    return '\n'.join(out) + leaf, kfs, 'wind streaks and a small leaf blowing across above the eyes'


def sprite_heat():
    cx, cy, r = 233, 130, 24
    rays = ''.join(f'<line x1="{cx + 31 * math.cos(math.radians(a)):.1f}" y1="{cy + 31 * math.sin(math.radians(a)):.1f}" '
                   f'x2="{cx + 40 * math.cos(math.radians(a)):.1f}" y2="{cy + 40 * math.sin(math.radians(a)):.1f}" '
                   f'style="stroke: #FFB547; stroke-width: 5; stroke-linecap: round"></line>' for a in range(0, 360, 45))
    kfs = [loop15('b2pulse', 1.6, [(0, 'opacity:0.35'), (0.8, 'opacity:1'), (1.6, 'opacity:0.35')])]
    glow = (f'<defs><radialGradient id="b2heatglow" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="46"><stop offset="0.5" stop-color="#FF8A3D" stop-opacity="0.45"></stop>'
            f'<stop offset="1" stop-color="#FF8A3D" stop-opacity="0"></stop></radialGradient></defs>'
            f'<circle cx="{cx}" cy="{cy}" r="46" style="fill: url(#b2heatglow); opacity: 0.35; {anim("b2pulse", 1.6)}"></circle>')
    return glow + rays + sun_disc(cx, cy, r, 'b2heatsun'), kfs, 'a small hot sun with short rays and a softly pulsing glow'


MOON = (212, 128, 30)


def sprite_night():
    cx, cy, r = MOON
    star = lambda x, y, s: (f'<path d="M {x} {y - s} Q {x} {y} {x + s} {y} Q {x} {y} {x} {y + s} Q {x} {y} {x - s} {y} Q {x} {y} {x} {y - s} Z" '
                            'style="fill: #E8ECF2"></path>')
    kfs = [loop15('b2twinkle', 1.6, [(0, 'opacity:1'), (0.8, 'opacity:0.3'), (1.6, 'opacity:1')])]
    defs = (f'<defs><radialGradient id="b2moonfill" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy - 6}" r="{r * 1.2:.0f}">'
            '<stop offset="0" stop-color="#E8ECF2"></stop><stop offset="1" stop-color="#C9D2DC"></stop></radialGradient>'
            f'<radialGradient id="b2moonglow" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="42">'
            '<stop offset="0.6" stop-color="#AFC4E0" stop-opacity="0.22"></stop><stop offset="1" stop-color="#AFC4E0" stop-opacity="0"></stop></radialGradient></defs>')
    svg = (defs + f'<circle cx="{cx}" cy="{cy}" r="42" style="fill: url(#b2moonglow)"></circle>'
           f'<circle cx="{cx}" cy="{cy}" r="{r}" style="fill: #000000"></circle>'
           '<path d="{{moonD}}" transform="{{moonT}}" style="fill: url(#b2moonfill)"></path>'
           f'<g style="{anim("b2twinkle", 1.6)}">{star(272, 104, 8)}</g>'
           f'<g style="{anim("b2twinkle", 1.6, -12 * F)}">{star(294, 150, 6)}</g>')
    return svg, kfs, 'a small moon in its current phase with two twinkling stars'


def sprite_afterrain():
    cx, cy = 233, 172
    bands = ['#E86A5A', '#F0A040', '#F0D040', '#5CCB8A', '#5C9CE0']
    arcs = ''.join(f'<path d="M {cx - rr} {cy} A {rr} {rr} 0 0 1 {cx + rr} {cy}" style="fill: none; stroke: {c}; stroke-width: 6.6"></path>'
                   for c, rr in zip(bands, (63, 57, 51, 45, 39)))
    kfs = [track('b2bow', [(0, 'opacity:0'), (SPRITE_IN, 'opacity:0'), (SPRITE_IN + 1.0, 'opacity:1')], 'step15')]
    feet = (filled_cloud([(172, -8, 10), (186, -13, 13), (200, -7, 9)], 174, 'white')
            + filled_cloud([(266, -7, 9), (280, -13, 13), (294, -8, 10)], 174, 'white'))
    return f'<g style="opacity: 0; {anim("b2bow", 6)}">{arcs}</g>' + feet, kfs, 'a small rainbow fading in between two little white clouds'


RR = 233 * 0.84
ES = 1.15
HOP_SCALE = 0.45
VIEWER = (0.0, 0.1)
GLIDE = 'cubic-bezier(0.65,0,0.35,1)'
SACC = 'cubic-bezier(0.34,1.4,0.64,1)'
LID_IN, LID_OUT = 'cubic-bezier(0.11,0,0.5,0)', 'cubic-bezier(0.5,1,0.89,1)'
DASH_W, DASH_H = 0.14 * ES * RR, 0.38 * ES * RR
ROUND_W, ROUND_H = 0.3 * ES * RR, 0.44 * ES * RR
HAPPY_W, HAPPY_H, HAPPY_SW = 0.3 * ES * RR, 0.16 * ES * RR, 0.11 * ES * RR
CLIP_TOP = 51
SEP_DASH, SEP_ROUND = 0.44 * (1 + (ES - 1) * 0.5), 0.5 * (1 + (ES - 1) * 0.5)


def gaze_model(gx, gy):
    reach, lim = 0.6, 0.52
    cx, cy = gx * reach, gy * reach
    d = math.hypot(cx, cy)
    if d > lim:
        cx, cy = cx * lim / d, cy * lim / d
    fx = math.sqrt(1 - 0.45 * cx * cx)
    lean = min(1.0, max(0.0, (abs(cx) - 0.06) / (0.67 * 0.6 - 0.06)))
    return cx, cy, fx, lean


def eye_pose(gx, gy):
    cx, cy, fx, lean = gaze_model(gx, gy)
    sg = -1 if cx < 0 else 1
    ox, oy = gx * 0.045, gy * 0.03
    out = []
    for side in (-1, 1):
        X = 233 + RR * (ox + cx + side * SEP_DASH * fx / 2)
        Y = 233 - RR * (oy * HOP_SCALE + cy)
        out.append(dict(X=round(X, 2), Y=round(Y, 2), rise=round(-RR * 0.04 * side * lean * sg, 2), dash_rot=round(-27 * lean * sg, 2),
                        round_dx=round(side * RR * (SEP_ROUND - SEP_DASH) * fx / 2, 2), round_rot=round(-10 * lean * sg, 2)))
    return out


def hop_curve(u):
    lerp = lambda a, b, k: a + (b - a) * min(1, max(0, k))
    eio = lambda u: 4 * u ** 3 if u < 0.5 else 1 - (-2 * u + 2) ** 3 / 2
    if u < 0.12:
        k = eio(u / 0.12)
        return 1 + 0.1 * k, 1 - 0.14 * k, 0
    if u < 0.30:
        v = (u - 0.12) / 0.18
        if v < 0.3:
            sx, sy = lerp(1.1, 0.93, v / 0.3), lerp(0.86, 1.1, v / 0.3)
        else:
            sx, sy = lerp(0.93, 1, (v - 0.3) / 0.7), lerp(1.1, 1, (v - 0.3) / 0.7)
        return sx, sy, 0.22 * (1 - (1 - v) ** 2)
    if u < 0.46:
        v = (u - 0.30) / 0.16
        return lerp(1, 0.95, v), lerp(1, 1.07, v), 0.22 * (1 - v * v)
    v = min(1, (u - 0.46) / 0.16)
    k = math.sin(math.pi * v)
    return 1 + 0.1 * k, 1 - 0.12 * k, 0


def blink(tb, speed=1.0, base=0.0):
    c, h, o = 0.075 * speed, 0.035 * speed, 0.15 * speed
    return [(tb, base, LID_IN), (tb + c, 1.0, 'linear'), (tb + c + h, 1.0, LID_OUT), (tb + c + h + o, base, 'linear')]


BLINK_PLAIN = 5.3


def script(kind):
    up = (0.0, 0.25)
    back = (HOLD_END, 0.42, *VIEWER, GLIDE)
    plain = blink(BLINK_PLAIN)
    if kind == 'glance':
        return dict(moves=[(F, 0.42, *up, GLIDE), back], lids=blink(2.4) + plain)
    if kind == 'rain':
        return dict(moves=[(F, 0.42, *up, GLIDE), back], lids=blink(1.6) + plain)
    if kind == 'storm':
        lids, prev = [], 0.0
        for f0 in FLASHES:
            lids += [(f0, prev, LID_IN), (f0 + 0.075, 0.45, 'linear'), (f0 + 0.2, 0.45, GLIDE), (f0 + 0.35, 0.12, 'linear')]
            prev = 0.12
        lids += [(2.4, 0.12, GLIDE), (2.8, 0.0, 'linear')] + plain
        return dict(moves=[(F, 0.42, 0.0, 0.18, GLIDE), back], lids=lids)
    if kind == 'snow':
        return dict(moves=[(0.4, 0.42, 0.0, 0.15, GLIDE), (1.4, 0.42, -0.15, 0.0, GLIDE), (2.4, 0.42, 0.15, 0.05, GLIDE),
                           (3.2, 0.42, 0.0, 0.12, GLIDE), back],
                    lids=blink(2 * F) + blink(HOLD_END) + plain, swaps=[(2 * F + 0.0925, 'costume'), (HOLD_END + 0.0925, 'base')], costume='round')
    if kind == 'wind':
        s = lambda t, gx, gy, a: (t, 0.025 + 0.045 * a, gx, gy, SACC)
        return dict(moves=[s(F, 0.38, 0.1, 0.38), s(1.2, 0.46, 0.12, 0.08), s(1.7, 0.33, 0.08, 0.13), s(2.5, 0.46, 0.1, 0.13),
                           s(3.2, 0.36, 0.12, 0.1), s(HOLD_END, *VIEWER, 0.36)],
                    lids=[(F, 0.0, GLIDE), (F + 0.25, 0.45, 'linear'), (HOLD_END, 0.45, GLIDE), (HOLD_END + 0.3, 0.0, 'linear')] + plain)
    if kind == 'heat':
        return dict(moves=[(0.4, 0.42, 0.0, 0.2, GLIDE), back], lids=blink(2 * F) + blink(HOLD_END) + plain,
                    swaps=[(2 * F + 0.0925, 'costume'), (HOLD_END + 0.0925, 'base')], costume='happy')
    if kind == 'night':
        return dict(moves=[(F, 0.42, -0.1, -0.15, GLIDE), back],
                    lids=[(F, 0.0, GLIDE), (0.7, 0.4, 'linear')] + blink(2.2, 2.2, 0.4) + [(HOLD_END, 0.4, GLIDE), (HOLD_END + 0.4, 0.0, 'linear')] + plain)
    if kind == 'afterrain':
        return dict(moves=[(0.4, 0.42, 0.0, 0.12, GLIDE), back], lids=blink(2 * F) + blink(HOLD_END) + plain,
                    swaps=[(2 * F + 0.0925, 'costume'), (HOLD_END + 0.0925, 'base')], costume='happy', hop=1.2)
    if kind == 'working':
        s = lambda t, gx, gy, a: (t, 0.025 + 0.045 * a, gx, gy, SACC)
        return dict(start=(-0.55, -0.15), moves=[s(1.0, -0.2, -0.17, 0.35), s(2.0, 0.15, -0.13, 0.35), s(3.0, 0.5, -0.15, 0.35),
                                                 s(4.0, *VIEWER, 0.56), s(5.0, -0.55, -0.15, 0.6)], lids=blink(4.6))
    raise KeyError(kind)


def eye_tracks(sc):
    kfs = []
    g0 = sc.get('start', VIEWER)
    for e, side in enumerate(('L', 'R')):
        wpts, rpts, dpts, qpts, opts = [], [], [], [], []
        cur = g0
        def add(t, g, tf):
            p = eye_pose(*g)[e]
            wpts.append((t, f'transform:translate({E.fmt(p["X"])}px,{E.fmt(p["Y"])}px)', tf))
            rpts.append((t, f'transform:translateY({E.fmt(p["rise"])}px)', tf))
            dpts.append((t, f'transform:rotate({E.fmt(p["dash_rot"])}deg)', tf))
            qpts.append((t, f'transform:translateX({E.fmt(p["round_dx"])}px)', tf))
            opts.append((t, f'transform:rotate({E.fmt(p["round_rot"])}deg)', tf))
        add(0, cur, 'linear')
        for t, dur, gx, gy, tf in sc['moves']:
            add(t, cur, tf)
            cur = (gx, gy)
            add(t + dur, cur, 'linear')
        assert cur == g0, 'the moment must end where it started'
        kfs += [track(f'b2w{side}', wpts), track(f'b2r{side}', rpts), track(f'b2d{side}', dpts), track(f'b2q{side}', qpts), track(f'b2o{side}', opts)]
    lids = sorted(sc['lids'])
    pts = lambda f: [(t, f(l), tf) for t, l, tf in lids]
    lid_line = lambda H, W, k: (lambda l: f'transform:translateY({E.fmt(round(l * (H - 0.3 * W * (1 + k * l)), 2))}px)')
    thick = lambda H, W, k: (lambda l: f'stroke-width:{E.fmt(round(W * (1 + k * l), 2))}px;transform:scaleY({E.fmt(round((H - W * (1 + k * l)) / (H - W), 4))})')
    kfs += [track('b2lidD', pts(lid_line(DASH_H, DASH_W, 0.35))), track('b2thD', pts(thick(DASH_H, DASH_W, 0.35))),
            track('b2lidO', pts(lid_line(ROUND_H, ROUND_W, 0.15))), track('b2thO', pts(thick(ROUND_H, ROUND_W, 0.15))),
            track('b2lidH', pts(lambda l: f'transform:scaleY({E.fmt(round(1 - 0.8125 * l, 4))})'))]
    if sc.get('swaps'):
        (t_on, _), (t_off, _) = sc['swaps']
        kfs += [track('b2base', [(0, 'opacity:1'), (t_on, 'opacity:0'), (t_off, 'opacity:1')], 'step-end'),
                track('b2cos', [(0, 'opacity:0'), (t_on, 'opacity:1'), (t_off, 'opacity:0')], 'step-end')]
    if sc.get('hop') is not None:
        th, hp, hy = sc['hop'], [], []
        for i in range(0, 21):
            u = i / 20
            sx, sy, dy = hop_curve(u * 0.62)
            sx, sy = 1 + (sx - 1) * 0.75, 1 + (sy - 1) * 0.75
            lift = round(RR * dy * HOP_SCALE, 2)
            hp.append((th + u, f'transform:translateY({E.fmt(-lift)}px) scale({E.fmt(round(sx, 4))},{E.fmt(round(sy, 4))})', 'linear'))
            hy.append((th + u, f'transform:translateY({E.fmt(-lift)}px)', 'linear'))
        kfs += [track('b2hop', [(0, hp[0][1], 'linear')] + hp)]
    return kfs


def eye_svg(sc):
    swaps, cos, out = bool(sc.get('swaps')), sc.get('costume'), []
    happy_pts = []
    for i in range(7):
        u = i / 6
        v = 1 - u
        x = v * v * (-HAPPY_W / 2) + 2 * v * u * 0 + u * u * (HAPPY_W / 2)
        y = v * v * (-HAPPY_H / 2) + 2 * v * u * (1.5 * HAPPY_H) + u * u * (-HAPPY_H / 2)
        happy_pts.append(f'{"M" if i == 0 else "L"} {x:.2f} {-y:.2f}')
    happy_d = ' '.join(happy_pts)
    stroke = lambda w: f'fill: none; stroke: {WHITE}; stroke-width: {w:.2f}; stroke-linecap: round; stroke-linejoin: round'
    happy = f'<path d="{happy_d}" style="{stroke(HAPPY_SW)}; vector-effect: non-scaling-stroke"></path>'
    nss = 'vector-effect: non-scaling-stroke; transform-origin: 0px 0px'
    dash = f'<line x1="0" y1="{-(DASH_H - DASH_W) / 2:.2f}" x2="0" y2="{(DASH_H - DASH_W) / 2:.2f}" style="{stroke(DASH_W)}; {nss}; {anim("b2thD", 6)}"></line>'
    rnd = f'<line x1="0" y1="{-(ROUND_H - ROUND_W) / 2:.2f}" x2="0" y2="{(ROUND_H - ROUND_W) / 2:.2f}" style="{stroke(ROUND_W)}; {nss}; {anim("b2thO", 6)}"></line>'
    base_anim = f'; {anim("b2base", 6)}' if swaps else ''
    cos_anim = f'opacity: 0; {anim("b2cos", 6)}'
    for side in ('L', 'R'):
        p0 = eye_pose(*sc.get('start', VIEWER))[0 if side == 'L' else 1]
        dash_g = (f'<g style="{anim("b2r" + side, 6)}"><g style="transform-origin: 0px 0px; {anim("b2d" + side, 6)}">{dash}</g>'
                  f'<rect x="-44" y="{-DASH_H / 2 - 160:.2f}" width="88" height="160" style="fill: #000000; {anim("b2lidD", 6)}"></rect></g>')
        parts = [f'<g style="display: {{{{dashD}}}}{base_anim}">{dash_g}</g>']
        if cos == 'round':
            parts.append(f'<g style="{cos_anim}"><g style="{anim("b2q" + side, 6)}"><g style="transform-origin: 0px 0px; {anim("b2o" + side, 6)}">{rnd}</g>'
                         f'<rect x="-48" y="{-ROUND_H / 2 - 160:.2f}" width="96" height="160" style="fill: #000000; {anim("b2lidO", 6)}"></rect></g></g>')
        clip = f'<g clip-path="url(#b2clip)">' + ''.join(parts) + '</g>'
        hap = f'<g style="display: {{{{happyD}}}}{base_anim}">{happy}</g>'
        if cos == 'happy':
            hap += f'<g style="{cos_anim}">{happy}</g>'
        hap = f'<g style="transform-origin: 0px 0px; {anim("b2lidH", 6)}">{hap}</g>'
        out.append(f'<g style="transform: translate({E.fmt(p0["X"])}px,{E.fmt(p0["Y"])}px); {anim("b2w" + side, 6)}">{clip}{hap}</g>')
    defs = f'<defs><clipPath id="b2clip" clipPathUnits="userSpaceOnUse"><rect x="-62" y="{-CLIP_TOP}" width="124" height="{CLIP_TOP + 62}"></rect></clipPath></defs>'
    return defs + '\n' + '\n'.join(out)


GLINT_PERIOD = 3.0


def glint():
    pt = lambda deg: (233 + RR * math.cos(math.radians(deg)), 233 + RR * math.sin(math.radians(deg)))
    (hx, hy), (tx, ty) = pt(-90), pt(-130)
    return (f'<defs><linearGradient id="b2glintg" gradientUnits="userSpaceOnUse" x1="{tx:.2f}" y1="{ty:.2f}" x2="{hx:.2f}" y2="{hy:.2f}">'
            '<stop offset="0" stop-color="#E0FCE7" stop-opacity="0"></stop><stop offset="1" stop-color="#E0FCE7" stop-opacity="1"></stop></linearGradient></defs>'
            f'<g style="transform-origin: 233px 233px; {anim("b2glint", GLINT_PERIOD)}">'
            f'<path d="M {tx:.2f} {ty:.2f} A {RR:.2f} {RR:.2f} 0 0 1 {hx:.2f} {hy:.2f}" style="fill: none; stroke: url(#b2glintg); stroke-width: 12"></path>'
            f'<circle cx="{hx:.2f}" cy="{hy:.2f}" r="6.5" style="fill: #E0FCE7"></circle></g>',
            loop15('b2glint', GLINT_PERIOD, [(0, 'transform:rotate(0deg)'), (GLINT_PERIOD, 'transform:rotate(360deg)')]))


BADGE = (233 + RR * math.cos(math.pi / 4) * 0.98, 233 - RR * math.sin(math.pi / 4) * 0.98, round(0.3 * RR), round(0.22 * RR))


def badge(hop):
    bx, by, gr, dr = BADGE
    a = ''
    return (f'<g style="display: {{{{badgeD}}}}{a}"><circle cx="{bx:.1f}" cy="{by:.1f}" r="{gr}" style="fill: #000000"></circle>'
            f'<circle cx="{bx:.1f}" cy="{by:.1f}" r="{dr}" style="fill: {{{{mood}}}}"></circle></g>')


FACES = {
    'B2-Glance': ('Bot moment, periodic glance', sprite_glance, 'glance', '12° Cloudy', E.temp_colour(12), ['idle', 'done'], 'idle'),
    'B2-Rain': ('Bot moment, raining', sprite_rain, 'rain', 'Rain until 18:00', BLUE, ['idle', 'done'], 'done'),
    'B2-Storm': ('Bot moment, storm', sprite_storm, 'storm', 'Storm', '#F0D040', ['idle', 'done'], 'idle'),
    'B2-Snow': ('Bot moment, snow', sprite_snow, 'snow', 'Snow &#8722;2°', E.temp_colour(-2), ['idle', 'done'], 'idle'),
    'B2-Wind': ('Bot moment, wind', sprite_wind, 'wind', 'Wind 45 km/h', W.wc(45), ['idle', 'done'], 'done'),
    'B2-Heat': ('Bot moment, heat', sprite_heat, 'heat', 'Hot 31°', E.temp_colour(31), ['idle', 'done'], 'idle'),
    'B2-Night': ('Bot moment, clear night', sprite_night, 'night', 'Clear 9°', E.temp_colour(9), ['idle', 'done'], 'idle'),
    'B2-AfterRain': ('Bot moment, rain stops', sprite_afterrain, 'afterrain', 'Rain stops', BLUE, ['idle', 'done'], 'done'),
    'B2-RainWorking': ('Bot moment, rain while working', lambda: sprite_rain(), None, 'Rain until 18:00', BLUE, ['working'], 'working'),
}
EYE_WORDS = {'glance': 'its eyes glance up at it', 'rain': 'its eyes look up and blink once', 'storm': 'its eyes squint at each flash',
             'snow': 'its eyes blink into the round waiting shape and follow the flakes', 'wind': 'its eyes are pushed to the side, tilting, and squint',
             'heat': 'its eyes blink into happy arcs', 'night': 'its eyes go sleepy, lids at 0.4, with one slow blink',
             'afterrain': 'its eyes blink into happy arcs and the bot hops once'}

NIGHT_PROPS = (',"phase":{"editor":"enum","options":["new","waxing crescent","first quarter","waxing gibbous","full","waning gibbous","last quarter","waning crescent"],"default":"waning crescent"}'
               ',"hemisphere":{"editor":"enum","options":["north","south"],"default":"north"}')
NIGHT_JS = f'''const phases = {{ 'new': 0, 'waxing crescent': 0.16, 'first quarter': 0.25, 'waxing gibbous': 0.36, 'full': 0.5, 'waning gibbous': 0.64, 'last quarter': 0.75, 'waning crescent': 0.84 }};
const cx = {MOON[0]}, cy = {MOON[1]}, R = {MOON[2]};
let p = phases[this.props.phase];
if (p === undefined) p = 0.84;
const waning = p > 0.5;
const q = waning ? 1 - p : p;
const k = Math.cos(2 * Math.PI * q);
const rx = (Math.abs(k) * R).toFixed(2);
const sweep = k > 0 ? 0 : 1;
const moonD = 'M ' + cx + ' ' + (cy - R) + ' A ' + R + ' ' + R + ' 0 0 1 ' + cx + ' ' + (cy + R) + ' A ' + rx + ' ' + R + ' 0 0 ' + sweep + ' ' + cx + ' ' + (cy - R) + ' Z';
const south = this.props.hemisphere === 'south';
const flip = waning !== south;
const moonT = flip ? 'translate(' + (2 * cx) + ' 0) scale(-1 1)' : 'translate(0 0)';
'''


def page(name):
    title, sprite_fn, kind, text, colour, moods, default = FACES[name]
    sprite, kfs, sprite_words = sprite_fn()
    quiet = kind is None
    sc = script('working' if quiet else kind)
    kfs = [sprite_kf('b2sprite')] + kfs + TEXT_KF[:1 if not quiet else 2] + eye_tracks(sc)
    hop = sc.get('hop') is not None
    if quiet:
        g_svg, g_kf = glint()
        kfs.append(g_kf)
        host = label_arc(name) + '\n' + label_text(name, 'CLAUDE', '{{mood}}') + '\n' + tool_icon()
        areas = 3
        aria = (f'Working bot, dash eyes reading left to right, a glint orbiting its green ring, host CLAUDE on the bottom arc. Quiet weather moment: '
                f'{sprite_words} appears above the eyes and a line under the eyes briefly reads {text.replace("&#8722;", "minus ")} while the eyes keep working '
                f'and the host label stays; the tool icon steps aside for the line and returns')
    else:
        g_svg, host = '', ''
        areas = 2
        aria = (f'Weather moment on the resting bot (dash eyes when idle, happy arcs and the badge when done): {sprite_words} pops in above the eyes, '
                f'{EYE_WORDS[kind]}, a line under the eyes briefly reads {text.replace("&#8722;", "minus ")}, then everything returns')
    aria += f'; device-optimized, at most {areas} redraw areas per frame' + ('; documented exception: the one-off firmware hop (about 1 s) moves and squashes the whole outline, as hops do in normal bot life' if hop else '')
    props = ('{"mood":{"editor":"enum","options":' + '[' + ','.join(f'"{m}"' for m in moods) + ']' + f',"default":"{default}"}}'
             + (NIGHT_PROPS if kind == 'night' else '') + ',"$preview":{"width":466,"height":466}}')
    colours = '{ ' + ', '.join(f"{m}: '{MOODS[m]}'" for m in moods) + ' }'
    extra_js = NIGHT_JS if kind == 'night' else ''
    ret = (f"{{ mood: colors[m] || '{MOODS[default]}', ringOp: m === 'idle' ? '0.55' : '1', dashD: m === 'done' ? 'none' : 'inline', "
           f"happyD: m === 'done' ? 'inline' : 'none', badgeD: m === 'done' ? 'inline' : 'none'"
           + (', moonD: moonD, moonT: moonT' if kind == 'night' else '') + ' }')
    bot = (f'<g style="transform-origin: 233px {233 + RR:.2f}px' + (f'; {anim("b2hop", 6)}' if hop else '') + '">\n'
           f'<circle cx="233" cy="233" r="{RR:.2f}" style="fill: none; stroke: {{{{mood}}}}; stroke-opacity: {{{{ringOp}}}}; stroke-width: 9"></circle>\n'
           + (g_svg + '\n' if g_svg else '') + eye_svg(sc) + '\n</g>')
    svg = (f'<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label="{aria}" style="position: absolute; left: 0; top: 0">\n'
           + f'<g style="opacity: 0; {anim("b2sprite", 6)}"><g transform="matrix({SPRITE_K} 0 0 {SPRITE_K} {E.fmt(SPRITE_E)} {E.fmt(SPRITE_F)})">\n{sprite}\n</g></g>\n'
           + bot + '\n' + badge(hop) + '\n' + (host + '\n' if host else '') + '</svg>')
    html = f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{title}</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
<link href="https://fonts.googleapis.com/css2?family=Montserrat:wght@500&display=swap" rel="stylesheet">
<style>
body{{margin:0}}
{chr(10).join(kfs)}
@media (prefers-reduced-motion: reduce){{*{{animation:none !important}}}}
</style>
</helmet>
<div style="width: 466px; height: 466px; position: relative; overflow: hidden; border-radius: 50%; background: #000000; color: {WHITE}; {FONT}">
{svg}
{text_line(text, colour)}
</div>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{props}'>
class Component extends DCLogic {{
renderVals() {{
const colors = {colours};
const m = this.props.mood;
{extra_js}return {ret};
}}
}}
</script>
</body>
</html>
'''
    open(os.path.join(E.PROJ, name + '.dc.html'), 'w').write(html)
    print(name, dict(areas=areas, label=text, colour=colour, mood=default))


def preview_vals(name, mood=None):
    _, _, kind, _, _, _, default = FACES[name]
    m = mood or default
    vals = {'mood': MOODS[m], 'ringOp': '0.55' if m == 'idle' else '1', 'dashD': 'none' if m == 'done' else 'inline',
            'happyD': 'inline' if m == 'done' else 'none', 'badgeD': 'inline' if m == 'done' else 'none'}
    if kind == 'night':
        cx, cy, R = MOON
        p = 0.84
        q = 1 - p
        k = math.cos(2 * math.pi * q)
        rx = f'{abs(k) * R:.2f}'
        sweep = 0 if k > 0 else 1
        vals['moonD'] = f'M {cx} {cy - R} A {R} {R} 0 0 1 {cx} {cy + R} A {rx} {R} 0 0 {sweep} {cx} {cy - R} Z'
        vals['moonT'] = f'translate({2 * cx} 0) scale(-1 1)'
    return vals


if __name__ == '__main__':
    for n in FACES:
        page(n)


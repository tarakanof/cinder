import re
import edit as E
import opt as O

FPS = 15


def steps_for(interval_s):
    return max(1, round(interval_s * FPS))


def sub_once(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


def retitle(s, title, label_add, label_hint):
    s = re.sub(r'<title>[^<]*</title>', f'<title>{title}</title>', s, count=1)
    s = re.sub(r'(aria-label="' + label_hint + r'[^"]*)"', r'\1; ' + label_add + '"', s, count=1)
    return s


def step_breathe(s):
    return re.sub(r'animation: wxbreathe ([0-9.]+)s ease-in-out',
                  lambda m: f'animation: wxbreathe {m.group(1)}s steps({steps_for(float(m.group(1)) / 2)})', s)


NOW = 14 * 60 + 30
LIGHT_A = [(176, 6, 5, 0.8, 0), (214, 22, 5, 1, 0), (248, 10, 5, 0.7, 0), (282, 30, 5, 0.9, 0), (196, 34, 5, 0.6, 0)]
LIGHT_B = [(186, 14, 5, 0.9, 0), (232, 2, 5, 0.75, 0), (266, 20, 5, 1, 0), (204, 28, 5, 0.65, 0), (292, 8, 5, 0.8, 0)]
MOD_LA = [(72, 4, 10, 0.85, 3), (118, 17, 10, 1, 3), (150, 29, 10, 0.7, 3), (94, 35, 10, 0.9, 4), (132, 9, 10, 0.75, 3), (60, 22, 10, 0.8, 3)]
MOD_LB = [(84, 8, 10, 0.8, 3), (136, 21, 10, 0.95, 3), (66, 27, 10, 0.65, 3), (160, 36, 10, 0.85, 4), (104, 14, 10, 0.9, 3), (146, 2, 10, 0.7, 3)]
MOD_RA = [(312, 7, 10, 0.9, 3), (356, 20, 10, 0.75, 3), (392, 32, 10, 1, 4), (334, 36, 10, 0.6, 3), (376, 2, 10, 0.85, 3), (306, 24, 10, 0.75, 3)]
MOD_RB = [(326, 3, 10, 0.85, 3), (372, 15, 10, 1, 3), (398, 27, 10, 0.7, 3), (344, 31, 10, 0.9, 4), (388, 10, 10, 0.8, 3), (316, 37, 10, 0.7, 3)]
HEAVY_LA = [(70, 4, 16, 0.9, 6), (102, 22, 16, 1, 6), (138, 10, 16, 0.75, 6), (170, 34, 16, 0.95, 6), (196, 48, 14, 0.7, 5)]
HEAVY_LB = [(84, 14, 16, 0.85, 6), (120, 40, 16, 1, 6), (154, 2, 16, 0.8, 6), (186, 24, 16, 0.9, 6), (66, 46, 14, 0.7, 5)]
HEAVY_RA = [(262, 8, 16, 0.9, 6), (296, 30, 16, 1, 6), (330, 4, 16, 0.75, 6), (362, 26, 16, 0.95, 6), (394, 44, 14, 0.7, 5)]
HEAVY_RB = [(278, 20, 16, 0.85, 6), (312, 46, 16, 1, 6), (346, 14, 16, 0.8, 6), (380, 36, 16, 0.9, 6), (256, 50, 14, 0.7, 5)]

RAIN_SPECS = {
    'light': {'C': ((160, 172, 302, 208), 40, LIGHT_A, LIGHT_B, 30, 0, 3)},
    'moderate': {'L': ((54, 182, 172, 208), 40, MOD_LA, MOD_LB, 12, 5, 4), 'R': ((298, 174, 406, 208), 40, MOD_RA, MOD_RB, 14, 11, 4)},
    'heavy': {'L': ((58, 172, 200, 208), 60, HEAVY_LA, HEAVY_LB, 12, 3, 4.5), 'R': ((248, 164, 400, 208), 60, HEAVY_RA, HEAVY_RB, 14, 9, 4.5)},
}


def ad_optimize(s, name, kind, title, budget):
    kfs = []
    sheets = {}
    for side, (box, H, A, B, tf, ph, w) in RAIN_SPECS[kind].items():
        svg, kf, _ = O.rain_sheet(f'{name}{side}', box, H, A, B, tf, ph, w=w, fade=0.08)
        sheets[side] = svg
        kfs.append(kf)
    gates = list(re.finditer(r'<g style="animation: wxraingate 6s linear infinite">\n(.*?)\n</g>\n', s, re.S))
    assert gates, 'no rain groups'
    if kind == 'light':
        g = gates[0]
        s = s[:g.start()] + s[g.end():]
        anchor = '<g style="animation: wxcome 6s linear infinite">'
        s = sub_once(s, anchor, f'<g style="animation: wxraingate 6s linear infinite">{sheets["C"]}</g>\n' + anchor)
    else:
        for g in reversed(gates):
            xs = [float(x) for x in re.findall(r'<line x1="([0-9.]+)"', g.group(1))]
            side = 'L' if sum(xs) / len(xs) < 233 else 'R'
            s = s[:g.start()] + f'<g style="animation: wxraingate 6s linear infinite">{sheets[side]}</g>\n' + s[g.end():]
    s = step_breathe(s)
    s = s.replace('animation: wxgloom 3.2s ease-in-out infinite', f'animation: wxgloom 3.2s steps({steps_for(1.6)}) infinite')
    s = re.sub(r'@keyframes wxfall\{.*\}\n', '', s)
    s = sub_once(s, '\n</style>\n</helmet>', '\n' + '\n'.join(kfs) + '\n</style>\n</helmet>')
    s = retitle(s, title, f'device-optimized: {budget}', 'A ' if kind == 'light' else 'Dark ')
    return s


SHIMMER_KF = '@keyframes wxshimmer{0%{transform:translateY(6px);opacity:0}35%{opacity:0.85}100%{transform:translateY(-16px);opacity:0}}'


def at4_optimize(s, title='Temperature, hot day'):
    s = sub_once(s, ' animation: wxraysway 9s ease-in-out infinite', '')
    s = re.sub(r"@keyframes wxraysway\{.*\}\n", '', s)
    s = s.replace('@keyframes wxuvbig{0%{transform:scale(0.95);opacity:0.85}100%{transform:scale(1.55);opacity:0}}',
                  '@keyframes wxuvbig{0%{transform:scale(0.95);opacity:0.9}100%{transform:scale(1.4);opacity:0}}')
    s = sub_once(s, 'animation: wxuvbig 2.4s ease-out infinite', f'animation: wxuvbig 2.4s steps({steps_for(2.4)}) infinite')
    s = sub_once(s, 'style="fill: url(#wxsunglow)"></circle>', f'style="fill: url(#wxsunglow); animation: wxglowpulse 3.2s steps({steps_for(1.6)}) infinite"></circle>')
    s = s.replace(SHIMMER_KF, '@keyframes wxshimmer{0%{transform:translateY(6px);opacity:0}50%{transform:translateY(-5px);opacity:0.85}100%{transform:translateY(-16px);opacity:0}}')
    s = s.replace('animation: wxshimmer 2.4s ease-out', f'animation: wxshimmer 2.4s steps({steps_for(1.2)})')
    s = sub_once(s, '\n</style>\n</helmet>', '\n@keyframes wxglowpulse{0%,100%{opacity:0.7}50%{opacity:1}}\n</style>\n</helmet>')
    s = retitle(s, title,
                'device-optimized: 2 redraw areas at 15 fps (the sun box with its glow and UV pulse, the heat shimmer box); rays static', 'A large golden sun')
    return s


TWINKLE = {(214, 70), (262, 62), (305, 95), (352, 126)}


def at5_optimize(s, title='Temperature, clear night'):
    def star(m):
        x, y = int(m.group(1)), int(m.group(2))
        dur = float(m.group(3))
        if (x, y) in TWINKLE:
            return m.group(0).replace(f'{m.group(3)}s ease-in-out', f'{m.group(3)}s steps({steps_for(dur / 2)})')
        return re.sub(r'; animation: wxtwinkle [^"]*', '', m.group(0))
    s = re.sub(r'<circle cx="([0-9]+)" cy="([0-9]+)" r="[0-9.]+" style="fill: #DDE6F0; opacity: 0.35; animation: wxtwinkle ([0-9.]+)s ease-in-out [^"]*"></circle>', star, s)
    s = retitle(s, title,
                'device-optimized: 2 redraw areas at 15 fps (two pairs of twinkling stars); the other stars and the moon are static after arrival', 'A thin')
    return s


AD_OPT = {
    'AD4': ('light', 'Rain forecast · light later', '2 redraw areas at 15 fps (the drifting cloud box, the drizzle sheet below it)'),
    'AD5': ('moderate', 'Rain forecast · moderate', '2 redraw areas at 15 fps (each side: its cloud with its rain sheet in one box)'),
    'AD3': ('heavy', 'Rain forecast · heavy soon', '2 redraw areas at 15 fps (each side: both cloud layers, the darker pulse and the rain sheet in one box)'),
}

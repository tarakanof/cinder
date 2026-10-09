import math, os, re
import edit as E
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(E.ORIG, 'AR2.pre-umbrella.dc.html')
DST = os.path.join(E.PROJ, 'AR2.dc.html')

L, R, RIM, APEX = 105, 361, 198, 95
CX, RX, RY = (L + R) / 2, (R - L) / 2, RIM - APEX
SCALLOPS = 5
DRY = (L, RIM - 8, R, 340)
INK_TOP, INK_BOTTOM, GAP, CAP = 220, 327, 7, 2.25
SHAFT_X, HOOK_R = 233, 11
STUB_END = INK_TOP - GAP - CAP
LOW_START = INK_BOTTOM + GAP + CAP
HOOK_Y = 366
LABEL_ZONES = [(84, 357, 163, 411), (266, 357, 382, 411)]
DRY_HANDLE = (SHAFT_X - 22, LOW_START - 8, SHAFT_X + 2 * HOOK_R + 7, HOOK_Y + HOOK_R + 15)
ORANGE, BLUE = '#FF8A3D', '#5C9CE0'


def surface(x):
    return RIM - RY * math.sqrt(max(0.0, 1 - ((x - CX) / RX) ** 2))


def sub_once(s, a, b):
    assert s.count(a) == 1, (a[:80], s.count(a))
    return s.replace(a, b)


s = open(SRC).read()

removed, kept = [], 0
def keep_streak(m):
    global kept
    x1, y1, x2, y2 = map(float, m.group(1, 2, 3, 4))
    bx0, bx1 = min(x1, x2) - 4, max(x1, x2) + 6
    by0, by1 = min(y1, y2) - 16, max(y1, y2) + 12
    for z in (DRY, DRY_HANDLE, *LABEL_ZONES):
        if not (bx1 < z[0] or bx0 > z[2] or by1 < z[1] or by0 > z[3]):
            removed.append((x1, y1))
            return ''
    if L < bx0 and bx1 < R and by1 < RIM and by0 + 8 > max(surface(bx0), surface(bx1)):
        removed.append((x1, y1, 'hidden under canopy'))
        return ''
    kept += 1
    return m.group(0)
s = re.sub(r'<line x1="([\d.]+)" y1="([\d.]+)" x2="([\d.]+)" y2="([\d.]+)" style="stroke: #5C9CE0; stroke-width: 4; stroke-linecap: round; '
           r'(?:stroke-opacity: [\d.]+; )?animation: wxfall [^"]*"></line>\n', keep_streak, s)

canopy_rain = []
for i, (x, dur, delay, op) in enumerate([(138, 0.7, -0.1, 0.85), (166, 0.8, -0.45, 1), (196, 0.65, -0.3, 0.75), (224, 0.75, -0.6, 1),
                                         (252, 0.7, -0.2, 0.85), (282, 0.85, -0.5, 1), (310, 0.65, -0.05, 0.75), (336, 0.8, -0.35, 0.85)]):
    top = round(surface(x) - 30)
    canopy_rain.append(f'<line x1="{x}" y1="{top}" x2="{x - 3}" y2="{top + 10}" style="stroke: {BLUE}; stroke-width: 4; stroke-linecap: round; '
                       f'stroke-opacity: {op}; animation: wxfall {dur}s linear {delay}s infinite"></line>')

w = (R - L) / SCALLOPS
rim = ''.join(f' A {w / 2:.1f} 9 0 0 0 {R - w * (k + 1):.1f} {RIM}' for k in range(SCALLOPS))
canopy_d = f'M {L} {RIM} A {RX:.0f} {RY} 0 0 1 {R} {RIM}{rim} Z'
splashes = []
for i, x in enumerate([150, 190, 233, 276, 316]):
    y = round(surface(x) - 3, 1)
    for dx in (-5, 5):
        splashes.append(f'<line x1="{x}" y1="{y}" x2="{x + dx}" y2="{y - 6}" style="stroke: {BLUE}; stroke-width: 3; stroke-linecap: round; '
                        f'opacity: 0; animation: wxsplash 0.7s linear {-(i * 0.27) % 0.7:.2f}s infinite"></line>')
handle = (f'<line x1="{SHAFT_X}" y1="185" x2="{SHAFT_X}" y2="{STUB_END}" style="stroke: {ORANGE}; stroke-width: 4.5; stroke-linecap: round"></line>\n'
          f'<path d="M {SHAFT_X} {LOW_START} V {HOOK_Y} A {HOOK_R} {HOOK_R} 0 0 0 {SHAFT_X + 2 * HOOK_R} {HOOK_Y}" '
          f'style="fill: none; stroke: {ORANGE}; stroke-width: 4.5; stroke-linecap: round"></path>\n')
umbrella = (f'<g style="transform-origin: {SHAFT_X}px {HOOK_Y}px; animation: wxsway 4s ease-in-out infinite">\n' + handle +
            f'<path d="{canopy_d}" style="fill: #000000; stroke: {ORANGE}; stroke-width: 4.5; stroke-linejoin: round"></path>\n'
            f'<line x1="{CX:.0f}" y1="{APEX - 2}" x2="{CX:.0f}" y2="{APEX - 14}" style="stroke: {ORANGE}; stroke-width: 4.5; stroke-linecap: round"></line>\n'
            + '\n'.join(splashes) + '\n'
            f'<circle cx="{L + 2}" cy="{RIM + 4}" r="3.5" style="fill: {BLUE}; opacity: 0; animation: wxdrip 1.4s ease-in infinite"></circle>\n'
            f'<circle cx="{R - 2}" cy="{RIM + 4}" r="3.5" style="fill: {BLUE}; opacity: 0; animation: wxdrip 1.4s ease-in -0.7s infinite"></circle>\n'
            f'<circle cx="{L + 2}" cy="{RIM + 4}" r="3" style="fill: {BLUE}; opacity: 0; animation: wxdrip 1.4s ease-in -0.35s infinite"></circle>\n'
            f'<circle cx="{R - 2}" cy="{RIM + 4}" r="3" style="fill: {BLUE}; opacity: 0; animation: wxdrip 1.4s ease-in -1.05s infinite"></circle>\n'
            '</g>')
start = s.index('<g style="transform-origin: 240px 197px; animation: wxsway 3s ease-in-out infinite">')
end = s.index('</g>\n</svg>', start) + len('</g>')
s = s[:start] + '\n'.join(canopy_rain) + '\n' + umbrella + s[end:]

s = sub_once(s, '@keyframes wxsway{0%,100%{transform:rotate(-3deg)}50%{transform:rotate(3deg)}}',
             '@keyframes wxsway{0%,100%{transform:rotate(-1.5deg)}50%{transform:rotate(1.5deg)}}')
s = sub_once(s, 'aria-label="Rain falling all over the face and onto a swaying umbrella, splashing on top and dripping off its edges"',
             'aria-label="Rain falling all over the face onto a wide umbrella that shelters the reading, its handle passing behind the text to a hook below, splashing on its canopy and dripping off both edges"')
s = E.end_labels(s)
import opt
s = opt.ar2_optimize(s, 'Rain · umbrella')
open(DST, 'w').write(s)
print('kept random streaks', kept, 'removed', len(removed), removed, 'canopy rain', len(canopy_rain))

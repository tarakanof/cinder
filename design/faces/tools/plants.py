import re

GROUND_Y = 206


def leaf_d(L, W):
    return (f'M 0 0 C {L * 0.25:.1f} {-W / 2:.1f}, {L * 0.75:.1f} {-W / 2:.1f}, {L} 0 '
            f'C {L * 0.75:.1f} {W / 2:.1f}, {L * 0.25:.1f} {W / 2:.1f}, 0 0 Z')


def leaf(x, y, a, L, W, fill, outline, rib=True, extra=''):
    r = (f'<line x1="4" y1="0" x2="{L - 6}" y2="0" style="stroke: {outline}; stroke-width: 2; stroke-linecap: round"></line>'
         if rib else '')
    return (f'<g transform="translate({x} {y}) rotate({a})"{extra}><path d="{leaf_d(L, W)}" style="fill: {fill}; stroke: {outline}; '
            f'stroke-width: 3; stroke-linejoin: round"></path>{r}</g>')


def dust(points, dur, colour):
    return '\n'.join(f'<circle cx="{x}" cy="{y}" r="{r}" style="fill: {colour}; animation: wxdust {dur}s linear {d}s infinite"></circle>'
                     for x, y, r, d in points)


HAZARD = ('<g style="transform-origin: 318px 128px; animation: wxwarn 1.6s ease-in-out infinite">\n'
          '<polygon points="318,108 340,146 296,146" style="fill: #000000; stroke: #FF5050; stroke-width: 4; stroke-linejoin: round"></polygon>\n'
          '<line x1="318" y1="121" x2="318" y2="133" style="stroke: #FF5050; stroke-width: 4; stroke-linecap: round"></line>\n'
          '<circle cx="318" cy="139" r="2.5" style="fill: #FF5050"></circle>\n</g>')
GROUND = f'<line x1="178" y1="{GROUND_Y}" x2="292" y2="{GROUND_Y}" style="stroke: #2E3236; stroke-width: 2; stroke-linecap: round"></line>'

KF = {
    'sway': '@keyframes wxplantsway{0%,100%{transform:rotate(-3deg)}50%{transform:rotate(3deg)}}',
    'nod': '@keyframes wxnod{0%,100%{transform:rotate(-2deg)}50%{transform:rotate(4deg)}}',
    'sag': '@keyframes wxsag{0%,100%{transform:rotate(16deg)}50%{transform:rotate(22deg)}}',
    'fall': '@keyframes wxfallpiece{0%{transform:translate(14px,-58px) rotate(-50deg);opacity:0}8%{opacity:1}'
            '62%{transform:translate(0px,0px) rotate(0deg);opacity:1}90%{transform:translate(0px,0px) rotate(0deg);opacity:1}100%{opacity:0}}',
}


def sprig_healthy():
    G, GO, STEM = '#7BC86C', '#5FAE55', '#4E8F45'
    parts = [f'<path d="M 233 204 C 228 172, 238 134, 233 100" style="fill: none; stroke: {STEM}; stroke-width: 5; stroke-linecap: round"></path>',
             leaf(232, 184, -155, 50, 24, G, GO), leaf(234, 162, -25, 52, 26, G, GO),
             leaf(232, 138, -150, 44, 22, G, GO), leaf(234, 118, -35, 40, 20, G, GO),
             leaf(233, 102, -90, 28, 15, G, GO, rib=False)]
    body = ('<g style="transform-origin: 233px 204px; animation: wxplantsway 3.6s ease-in-out infinite">\n' + '\n'.join(parts) + '\n</g>')
    motes = dust([(166, 120, 3, 0), (300, 96, 2.5, -1.2), (156, 172, 2.5, -2.3), (304, 168, 3, -0.6)], 3.4, '#9AA4B0')
    return body + '\n' + motes, ['sway'], 'A green sprig with filled fresh leaves swaying gently, specks of dust drifting past'


def flower_healthy():
    G, GO, STEM = '#7BC86C', '#5FAE55', '#4E8F45'
    cx, cy = 233, 112
    petals = '\n'.join(f'<ellipse cx="{cx}" cy="{cy - 19}" rx="12.5" ry="20" transform="rotate({k * 60} {cx} {cy})" '
                       f'style="fill: url(#wxpetalgrad); stroke: #F4B6C6; stroke-width: 2.5"></ellipse>' for k in range(6))
    head = (f'<g style="transform-origin: {cx}px {cy + 14}px; animation: wxnod 4.4s ease-in-out infinite">\n{petals}\n'
            f'<circle cx="{cx}" cy="{cy}" r="11" style="fill: #F5C84A; stroke: #E3AE36; stroke-width: 2"></circle>\n</g>')
    defs = (f'<defs><radialGradient id="wxpetalgrad" gradientUnits="userSpaceOnUse" cx="{cx}" cy="{cy}" r="40">'
            '<stop offset="0.3" stop-color="#F59AB0"></stop><stop offset="1" stop-color="#E87A96"></stop></radialGradient></defs>')
    parts = [f'<path d="M 233 204 C 230 176, 237 150, 233 126" style="fill: none; stroke: {STEM}; stroke-width: 5; stroke-linecap: round"></path>',
             leaf(233, 188, -22, 40, 20, G, GO), leaf(233, 170, -158, 34, 18, G, GO), head]
    body = (defs + '\n<g style="transform-origin: 233px 204px; animation: wxplantsway 4s ease-in-out infinite">\n' + '\n'.join(parts) + '\n</g>')
    return body, ['sway', 'nod'], 'A pink flower with a yellow centre on a green stem, swaying gently'


def falling(piece_svg, x, y, delay):
    return (f'<g style="transform-origin: {x}px {y}px; opacity: 0; animation: wxfallpiece 7s ease-in-out {delay}s infinite">{piece_svg}</g>')


def sprig_poor():
    Y, YO, STEM = '#D9B44A', '#B8923A', '#8A7A44'
    parts = [f'<path d="M 224 204 C 220 172, 228 134, 224 102" style="fill: none; stroke: {STEM}; stroke-width: 5; stroke-linecap: round"></path>',
             leaf(223, 180, 160, 44, 20, Y, YO), leaf(225, 156, 30, 46, 22, Y, YO),
             leaf(223, 132, 150, 38, 18, Y, YO), leaf(225, 112, 50, 34, 16, Y, YO), leaf(224, 104, 110, 24, 12, Y, YO, rib=False)]
    body = ('<g style="transform-origin: 224px 204px; transform: rotate(18deg); animation: wxsag 4s ease-in-out infinite">\n'
            + '\n'.join(parts) + '\n</g>')
    fallen = '\n'.join(leaf(x, y, a, 20, 9, '#C9A040', YO, rib=False) for x, y, a in [(184, 203, -8), (262, 204, 172), (280, 202, 12)])
    drop = falling(leaf(242, 202, -6, 20, 9, '#D9B44A', YO, rib=False), 252, 202, -1.5)
    motes = dust([(166, 128, 3, 0), (182, 166, 3.5, -0.9), (196, 108, 2.5, -1.7), (302, 176, 3, -0.4), (276, 90, 2.5, -2.2), (156, 188, 2.5, -1.3)],
                 2.6, '#8A8A8A')
    return ('\n'.join([GROUND, fallen, body, drop, motes, HAZARD]), ['sag', 'fall'],
            'A wilting sprig with yellow leaves drooping in thick dust, fallen leaves on the ground and one drifting down, a hazard sign pulsing beside it')


def flower_poor():
    G, GO, STEM = '#9CA35A', '#7F8646', '#7C8A50'
    P, PO = '#C98A9A', '#D9A9B4'
    cx, cy = 226, 122
    petals = '\n'.join(f'<ellipse cx="{cx}" cy="{cy - 18}" rx="11.5" ry="18" transform="rotate({k * 60} {cx} {cy})" '
                       f'style="fill: {P}; stroke: {PO}; stroke-width: 2.5"></ellipse>' for k in (0, 2, 3, 5))
    head = (f'<g transform="rotate(32 {cx} {cy})">\n{petals}\n'
            f'<circle cx="{cx}" cy="{cy}" r="10" style="fill: #C9A84A; stroke: #A98A3A; stroke-width: 2"></circle>\n</g>')
    parts = [f'<path d="M 226 204 C 222 176, 230 150, 226 132" style="fill: none; stroke: {STEM}; stroke-width: 5; stroke-linecap: round"></path>',
             leaf(226, 188, 20, 36, 17, G, GO), leaf(226, 172, 165, 30, 15, G, GO), head]
    body = ('<g style="transform-origin: 226px 204px; transform: rotate(18deg); animation: wxsag 4s ease-in-out infinite">\n'
            + '\n'.join(parts) + '\n</g>')
    petal = lambda x, y, a: (f'<ellipse cx="{x}" cy="{y}" rx="4.5" ry="8" transform="rotate({a} {x} {y})" '
                             f'style="fill: {P}; stroke: {PO}; stroke-width: 1.5"></ellipse>')
    fallen = '\n'.join(petal(x, y, a) for x, y, a in [(188, 202, 72), (266, 203, 104), (284, 201, 60)])
    drop = falling(petal(250, 202, 86), 250, 202, -2.0)
    motes = dust([(166, 128, 3, 0), (182, 166, 3.5, -0.9), (196, 108, 2.5, -1.7), (302, 176, 3, -0.4), (276, 90, 2.5, -2.2), (156, 188, 2.5, -1.3)],
                 2.6, '#8A8A8A')
    return ('\n'.join([GROUND, fallen, body, drop, motes, HAZARD]), ['sag', 'fall'],
            'A drooping flower with faded petals, two of them gone, fallen petals on the ground and one drifting down, in thick dust with a hazard sign pulsing beside it')


PLANTS = {'sprig': sprig_healthy, 'flower': flower_healthy, 'sprig_poor': sprig_poor, 'flower_poor': flower_poor}
OLD_PLANT_KF = ['wxsway', 'wxdroop', 'wxpetal', 'wxsag']


def bigger_end_icons(s):
    s = s.replace('<g transform="translate(104 394) rotate(-40)">', '<g transform="translate(104 394) rotate(-40) scale(1.2)">', 1)
    a = s.index('<polygon points="352,368 370,399 334,399"')
    tail = '<circle cx="352" cy="394" r="2" style="fill: #D03060"></circle>'
    b = s.index(tail, a) + len(tail)
    s = s[:a] + '<g transform="translate(349 383) scale(1.2) translate(-352 -386)">\n' + s[a:b] + '\n</g>' + s[b:]
    if '<ellipse cx="116" cy="376"' in s:
        a = s.index('<ellipse cx="116" cy="376"')
        tail = '<circle cx="116" cy="383" r="3" style="fill: #F0E641"></circle>'
        b = s.index(tail, a) + len(tail)
        s = s[:a] + '<g transform="translate(116 383) scale(1.2) translate(-116 -383)">\n' + s[a:b] + '\n</g>' + s[b:]
    return s


def apply(s, plant, title=None, flower_end_icon=None):
    s = s.replace('<div style="position: absolute; left: 0; top: 76px; width: 466px; text-align: center; font-size: 24px; line-height: 30px; color: #BDBDBD">Air</div>\n', '', 1)
    m = re.search(r'(<svg viewBox="0 0 466 466" width="466" height="466" role="img" aria-label=")(A [^"]*)(" style="position: absolute; left: 0; top: 0">\n)(.*?)(\n</svg>)', s, re.S)
    assert m and ('sprig' in m.group(2) or 'flower' in m.group(2)), 'icon svg not found'
    body, kf_names, label = PLANTS[plant]()
    s = s[:m.start()] + m.group(1) + label + m.group(3) + body + m.group(5) + s[m.end():]
    for name in OLD_PLANT_KF:
        s = re.sub(r'@keyframes ' + name + r'\{.*\}\n', '', s)
    s = s.replace('\n</style>\n</helmet>', '\n' + '\n'.join(KF[k] for k in kf_names) + '\n</style>\n</helmet>', 1)
    if title:
        s = re.sub(r'<title>[^<]*</title>', f'<title>{title}</title>', s, 1)
    if flower_end_icon:
        a = s.index('<g transform="translate(104 394) rotate(-40)">')
        b = s.index('</g>', a) + len('</g>')
        s = s[:a] + flower_end_icon + s[b:]
        s = s.replace('from a leaf at the clean end', 'from a fresh flower at the clean end')
    return bigger_end_icons(s)

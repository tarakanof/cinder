import math, os, re
import edit as E
import music as M

P = E.PROJ
FPS = 15


def sub_once(s, a, b):
    assert s.count(a) == 1, (a[:80], s.count(a))
    return s.replace(a, b)


FRAME = 1 / FPS
CANOPY = dict(L=105, R=361, RIM=198, APEX=95)


def canopy_y(x):
    cx, rx, ry = (CANOPY['L'] + CANOPY['R']) / 2, (CANOPY['R'] - CANOPY['L']) / 2, CANOPY['RIM'] - CANOPY['APEX']
    return CANOPY['RIM'] - ry * (max(0.0, 1 - ((x - cx) / rx) ** 2)) ** 0.5


def rain_sheet(uid, box, H, pat_a, pat_b, tile_frames, delay_frames, extra='', w=4, fade=0.16):
    x0, y0, x1, y1 = box
    frames = 2 * tile_frames
    period = round(frames * FRAME, 3)

    def tile(pat):
        return ''.join(f'<line x1="{x}" y1="{y}" x2="{x - sl}" y2="{y + L}" style="stroke: #5C9CE0; stroke-width: {w}; stroke-linecap: round; stroke-opacity: {op}"></line>'
                       for x, y, L, op, sl in pat)
    n_super = int((y1 - y0) / (2 * H)) + 2
    strip = ''.join(f'<g transform="translate(0 {y0 - 2 * H + k * 2 * H})">{tile(pat_a)}<g transform="translate(0 {H})">{tile(pat_b)}</g></g>'
                    for k in range(n_super))
    defs = (f'<clipPath id="sheetclip{uid}"><rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}"></rect></clipPath>'
            f'<linearGradient id="sheetfade{uid}" gradientUnits="userSpaceOnUse" x1="0" y1="{y0}" x2="0" y2="{y1}">'
            f'<stop offset="0" stop-color="#000000"></stop><stop offset="{fade}" stop-color="#FFFFFF"></stop>'
            f'<stop offset="{round(1 - fade, 2)}" stop-color="#FFFFFF"></stop><stop offset="1" stop-color="#000000"></stop></linearGradient>'
            f'<mask id="sheetmask{uid}" maskUnits="userSpaceOnUse" x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}">'
            f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" style="fill: url(#sheetfade{uid})"></rect></mask>')
    delay = -round(delay_frames * FRAME, 3)
    kf = f'@keyframes wxsheet{uid}{{0%{{transform:translateY(0px)}}100%{{transform:translateY({2 * H}px)}}}}'
    svg = (f'<defs>{defs}</defs><g clip-path="url(#sheetclip{uid})" mask="url(#sheetmask{uid})">'
           f'<g style="animation: wxsheet{uid} {period}s steps({frames}, end) {delay}s infinite">{strip}</g>{extra}</g>')
    return svg, kf, dict(box=box, H=H, frames=frames, period=period, delay=delay, delay_frames=delay_frames)


def splashes_for(sheet, pat_a, pat_b, which):
    x0, y0, x1, y1 = sheet['box']
    H, frames, period = sheet['H'], sheet['frames'], sheet['period']
    step = 2 * H / frames
    out, kfs = [], []
    pct = lambda k: round(100 * k / frames, 3)
    for n, (tile, idx) in enumerate(which):
        x, yl, L, op, sl = (pat_a if tile == 'A' else pat_b)[idx]
        off = yl + (H if tile == 'B' else 0)
        ys = canopy_y(x)
        T_hit = (ys - 2 - L - (y0 - 2 * H) - off) % (2 * H)
        f = math.ceil(T_hit / step) % frames
        name = f'wxsplash{n}'
        kfs.append(f'@keyframes {name}{{0%,{pct(4)}%{{opacity:0;transform:translateY(3px);animation-timing-function:step-end}}'
                   f'{pct(4)}%{{opacity:0.7;transform:translateY(1px);animation-timing-function:step-end}}'
                   f'{pct(5)}%{{opacity:1;transform:translateY(0px);animation-timing-function:step-end}}'
                   f'{pct(6)}%{{opacity:0.6;transform:translateY(-2px);animation-timing-function:step-end}}'
                   f'{pct(7)}%,100%{{opacity:0;transform:translateY(-3px)}}}}')
        d = -round(((4 - f + sheet['delay_frames']) % frames) * FRAME, 3)
        y = round(ys - 3, 1)
        for dx in (-5, 5):
            out.append(f'<line x1="{x}" y1="{y}" x2="{x + dx}" y2="{y - 6}" style="stroke: #5C9CE0; stroke-width: 3; stroke-linecap: round; opacity: 0; '
                       f'animation: {name} {period}s linear {d}s infinite"></line>')
    return '\n'.join(out), kfs


def drip(uid, x, y, period_frames, start_frame):
    pct = lambda k: round(100 * k / period_frames, 3)
    seq = [(0.45, 0, 0.6), (0.75, 0, 0.9), (1.0, 1, 1), (1.0, 6, 1), (1.0, 14, 0.9), (1.0, 24, 0.65), (1.0, 34, 0.35)]
    body = '0%{opacity:0;transform:translateY(0px) scale(0.4);animation-timing-function:step-end}'
    for i, (sc, dy, op) in enumerate(seq):
        body += f'{pct(start_frame + i)}%{{opacity:{op};transform:translateY({dy}px) scale({sc});animation-timing-function:step-end}}'
    body += f'{pct(start_frame + len(seq))}%,100%{{opacity:0;transform:translateY(36px) scale(1)}}'
    kf = f'@keyframes wxdrip{uid}{{{body}}}'
    period = round(period_frames * FRAME, 3)
    svg = (f'<g style="transform-origin: {x}px {y}px; opacity: 0; animation: wxdrip{uid} {period}s linear infinite">'
           f'<path d="M {x} {y - 4} Q {x + 4} {y + 2} {x} {y + 5} Q {x - 4} {y + 2} {x} {y - 4} Z" style="fill: #5C9CE0"></path></g>')
    return svg, kf


LEFT_A = [(70, 12, 16, 0.9, 3), (92, 70, 12, 0.6, 3), (64, 118, 18, 0.8, 5)]
LEFT_B = [(86, 30, 14, 0.75, 3), (66, 88, 16, 1.0, 3), (96, 128, 12, 0.55, 4)]
RIGHT_A = [(376, 20, 16, 0.85, 3), (396, 84, 12, 0.6, 4), (370, 126, 14, 0.95, 3)]
RIGHT_B = [(390, 8, 14, 0.7, 3), (372, 60, 18, 0.9, 5), (398, 110, 12, 0.55, 3)]
TOP_A = [(172, 10, 16, 0.9, 3), (246, 64, 14, 0.7, 3), (298, 112, 12, 0.8, 4), (205, 128, 16, 0.6, 3)]
TOP_B = [(226, 24, 14, 1.0, 3), (186, 80, 12, 0.65, 5), (280, 40, 16, 0.8, 3), (160, 136, 14, 0.7, 3)]


def ar2_optimize(s, title='Rain · umbrella'):
    s = re.sub(r'<line [^>]*animation: wxfall[^>]*></line>\n', '', s)
    s = re.sub(r'<circle [^>]*animation: wxdrip[^>]*></circle>\n', '', s)
    s = re.sub(r'<line x1="(150|190|233|276|316)" [^>]*animation: wxsplash[^>]*></line>\n', '', s)
    s = sub_once(s, '<g style="transform-origin: 233px 366px; animation: wxsway 4s ease-in-out infinite">', '<g>')
    for name in ('wxfall', 'wxdrip', 'wxsway', 'wxsplash'):
        s = re.sub(r'@keyframes ' + name + r'\{.*\}\n', '', s)
    dl, kdl = drip('L', 107, 203, 24, 5)
    dr, kdr = drip('R', 359, 203, 32, 13)
    left, kf1, _ = rain_sheet('L', (58, 150, 118, 332), 150, LEFT_A, LEFT_B, 20, 6, dl)
    right, kf2, _ = rain_sheet('R', (348, 150, 402, 332), 150, RIGHT_A, RIGHT_B, 24, 15, dr)
    top, kf3, topinfo = rain_sheet('T', (150, 46, 316, 132), 150, TOP_A, TOP_B, 17, 0)
    spl, kfs = splashes_for(topinfo, TOP_A, TOP_B, [('A', 0), ('A', 1), ('B', 2)])
    kfs += [kf1, kf2, kf3, kdl, kdr]
    top = top + '\n' + spl
    s = sub_once(s, '<g>\n<line x1="233" y1="185"', f'{left}\n{right}\n{top}\n<g>\n<line x1="233" y1="185"')
    s = sub_once(s, '\n</style>\n</helmet>', '\n' + '\n'.join(kfs) + '\n</style>\n</helmet>')
    s = re.sub(r'<title>[^<]*</title>', f'<title>{title}</title>', s, count=1)
    s = re.sub(r'(aria-label="Rain falling all over the face onto a wide umbrella[^"]*)"',
               r'\1; device-optimized: 3 redraw areas at 15 fps (left and right rain sheets, the band above the canopy)"', s, count=1)
    return s


def m8_body():
    a, b = M.TRACK, M.TRACK2
    C, R, W, CIRC = M.C, M.RING_R, M.RING_W, M.CIRC
    cx, cy = M.HERO2
    fo, fn = a['pos'] / a['dur'], b['pos'] / b['dur']
    Lo, Ln, tho, thn = CIRC * fo, CIRC * fn, 360 * fo, 360 * fn
    hx, hy = M.polar(thn, R)
    ms = lambda v: round(v / 60, 3)
    t0 = 250
    f1, f2, f3, blk, n1, n2, n3 = t0, t0 + 50, t0 + 100, t0 + 133, t0 + 166, t0 + 216, t0 + 266
    kfs = [
        f'@keyframes wxoldcover{{0%{{opacity:1;animation-timing-function:step-end}}{ms(f1)}%{{opacity:0.67;animation-timing-function:step-end}}'
        f'{ms(f2)}%{{opacity:0.33;animation-timing-function:step-end}}{ms(f3)}%,100%{{opacity:0}}}}',
        f'@keyframes wxnewcover{{0%,{ms(blk)}%{{opacity:0;animation-timing-function:step-end}}{ms(n1)}%{{opacity:0.33;animation-timing-function:step-end}}'
        f'{ms(n2)}%{{opacity:0.67;animation-timing-function:step-end}}{ms(n3)}%,100%{{opacity:1}}}}',
        f'@keyframes wxswapout{{0%{{opacity:1;animation-timing-function:step-end}}{ms(f3)}%,100%{{opacity:0}}}}',
        f'@keyframes wxswapin{{0%{{opacity:0;animation-timing-function:step-end}}{ms(f3)}%,100%{{opacity:1}}}}',
        f'@keyframes wxskipq{{0%{{opacity:0;animation-timing-function:step-end}}{ms(f3)}%{{opacity:1;animation-timing-function:step-end}}{ms(n1)}%,100%{{opacity:0}}}}',
        f'@keyframes wxunwind{{0%,{ms(t0)}%{{stroke-dasharray:{Lo:.1f} {CIRC:.1f};stroke:{a["accent"]};animation-timing-function:cubic-bezier(0.6,0,0.4,1)}}'
        f'{ms(blk)}%{{stroke-dasharray:0 {CIRC:.1f};stroke:{a["accent"]};animation-timing-function:step-end}}{ms(blk) + 0.01}%{{stroke-dasharray:0 {CIRC:.1f};stroke:{b["accent"]};animation-timing-function:ease-out}}'
        f'20%,100%{{stroke-dasharray:{Ln:.1f} {CIRC:.1f};stroke:{b["accent"]}}}}}',
        f'@keyframes wxheadun{{0%,{ms(t0)}%{{transform:rotate({tho - thn:.1f}deg);animation-timing-function:cubic-bezier(0.6,0,0.4,1)}}'
        f'{ms(blk)}%{{transform:rotate({-thn:.1f}deg);animation-timing-function:ease-out}}20%,100%{{transform:rotate(0deg)}}}}',
    ]
    old_art = M.art_disc(f'<g style="animation: wxoldcover 6s linear infinite">{M.full_low_tide("M8o")}</g>'
                         f'<g style="opacity: 0; animation: wxnewcover 6s linear infinite">{M.full_paper_moons("M8n")}</g>', 'M8')
    skip = (f'<g style="opacity: 0; animation: wxskipq 6s linear infinite">'
            f'<circle cx="{cx}" cy="{cy}" r="46" style="fill: #000000; opacity: 0.55"></circle>'
            f'<path d="M {cx - 26} {cy - 16} L {cx - 4} {cy} L {cx - 26} {cy + 16} Z M {cx - 4} {cy - 16} L {cx + 18} {cy} L {cx - 4} {cy + 16} Z" '
            'style="fill: #F4F4F2; stroke: #F4F4F2; stroke-width: 3; stroke-linejoin: round"></path>'
            f'<rect x="{cx + 19}" y="{cy - 17}" width="6" height="34" rx="2" style="fill: #F4F4F2"></rect></g>')
    head = (f'<g style="transform-origin: {C}px {C}px; animation: wxheadun 6s linear infinite">'
            f'<g style="animation: wxswapout 6s linear infinite">{M.head_dot(hx, hy, a["accent"], "o")}</g>'
            f'<g style="opacity: 0; animation: wxswapin 6s linear infinite">{M.head_dot(hx, hy, b["accent"], "n")}</g></g>')
    ring_svg = (f'<circle cx="{C}" cy="{C}" r="{R}" style="fill: none; stroke: #262626; stroke-width: {W}"></circle>'
                f'<circle cx="{C}" cy="{C}" r="{R}" transform="rotate(-90 {C} {C})" style="fill: none; stroke: {b["accent"]}; stroke-width: {W}; '
                f'stroke-linecap: round; stroke-dasharray: {Ln:.1f} {CIRC:.1f}; animation: wxunwind 6s linear infinite"></circle>' + head)
    body = (M.svg(old_art + skip, f'Next track, device-optimized: {a["album"]} fades to black in three steps, one black frame with the skip symbol, '
                                  f'then {b["album"]} by {b["artist"]} appears in three steps; covers only change opacity') + '\n'
            + M.svg(ring_svg, f'The edge ring unwinds and re-tints to {M.mmss(b["pos"])} of {M.mmss(b["dur"])}') + '\n'
            + M.art_texts(a, f'{M.mmss(a["pos"])} / {M.mmss(a["dur"])}', 'M8o', ' animation: wxswapout 6s linear infinite;') + '\n'
            + M.art_texts(b, f'{M.mmss(b["pos"])} / {M.mmss(b["dur"])}', 'M8n', ' opacity: 0; animation: wxswapin 6s linear infinite;', blue=True))
    return body, kfs



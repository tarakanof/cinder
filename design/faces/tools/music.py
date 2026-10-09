import math, os
import edit as E

PROJ = E.PROJ
C = 233
DISK_R, RING_R, VOL_R = 188, 222, 211
RING_W = 12
ART_R = 208
CIRC = 2 * math.pi * RING_R
EASE = E.EASE

TRACK = dict(title='Glass Harbor', artist='Northern Static', album='Low Tide Radio', source='Plex',
             pos=134, dur=228, volume=62, accent='#FF8A5C')
TRACK_B = dict(title='Paper Moons', artist='Ivy Calder', album='Paper Moons', source='Apple Music',
               pos=71, dur=205, volume=62, accent='#A18CFF')


def mmss(s):
    return f'{s // 60}:{s % 60:02d}'


def polar(clock_deg, r, cx=C, cy=C):
    a = math.radians(clock_deg)
    return round(cx + r * math.sin(a), 2), round(cy - r * math.cos(a), 2)


def art_low_tide(cx, cy, r, uid):
    waves = []
    for k, (col, dy) in enumerate([('#1F8A8A', 0.30), ('#2FB3A8', 0.46), ('#13606B', 0.62), ('#0B4250', 0.80)]):
        y0 = cy + r * dy
        amp, wl = r * 0.06, r * 0.5
        d = f'M {cx - r - 4} {y0:.1f}'
        x = cx - r - 4
        while x < cx + r + 4:
            d += f' q {wl / 4:.1f} {-amp if k % 2 else amp:.1f} {wl / 2:.1f} 0 t {wl / 2:.1f} 0'
            x += wl
        d += f' V {cy + r + 4} H {cx - r - 4} Z'
        waves.append(f'<path d="{d}" style="fill: {col}"></path>')
    return (f'<defs><linearGradient id="sky{uid}" x1="0" y1="0" x2="0" y2="1">'
            '<stop offset="0" stop-color="#2B1B5A"></stop><stop offset="0.38" stop-color="#8E3B7A"></stop>'
            '<stop offset="0.62" stop-color="#FF8A5C"></stop><stop offset="0.8" stop-color="#FFC27A"></stop></linearGradient>'
            f'<clipPath id="clip{uid}"><circle cx="{cx}" cy="{cy}" r="{r}"></circle></clipPath></defs>'
            f'<g clip-path="url(#clip{uid})">'
            f'<rect x="{cx - r}" y="{cy - r}" width="{2 * r}" height="{2 * r}" style="fill: url(#sky{uid})"></rect>'
            f'<circle cx="{cx + r * 0.12:.1f}" cy="{cy + r * 0.18:.1f}" r="{r * 0.36:.1f}" style="fill: #FFE3A0"></circle>'
            f'<circle cx="{cx - r * 0.45:.1f}" cy="{cy - r * 0.42:.1f}" r="{r * 0.07:.1f}" style="fill: #FFD9E6; opacity: 0.8"></circle>'
            + ''.join(waves) + '</g>')


def avatar(cx, cy, r, uid):
    return (f'<defs><radialGradient id="av{uid}" cx="0.35" cy="0.3" r="0.9"><stop offset="0" stop-color="#F2B48A"></stop>'
            f'<stop offset="1" stop-color="#6B3F7A"></stop></radialGradient><clipPath id="avc{uid}"><circle cx="{cx}" cy="{cy}" r="{r}"></circle></clipPath></defs>'
            f'<circle cx="{cx}" cy="{cy}" r="{r + 3}" style="fill: #000000"></circle>'
            f'<g clip-path="url(#avc{uid})"><rect x="{cx - r}" y="{cy - r}" width="{2 * r}" height="{2 * r}" style="fill: url(#av{uid})"></rect>'
            f'<circle cx="{cx}" cy="{cy - r * 0.18:.1f}" r="{r * 0.32:.1f}" style="fill: #2A1830; opacity: 0.75"></circle>'
            f'<ellipse cx="{cx}" cy="{cy + r * 0.78:.1f}" rx="{r * 0.62:.1f}" ry="{r * 0.5:.1f}" style="fill: #2A1830; opacity: 0.75"></ellipse></g>')


def backdrop(uid, dim=0.5):
    blobs = [('#FF8A5C', 150, 150, 110), ('#8E3B7A', 320, 140, 120), ('#1F8A8A', 200, 330, 130), ('#FFC27A', 330, 300, 80)]
    return (f'<defs><filter id="blur{uid}" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="34"></feGaussianBlur></filter>'
            f'<clipPath id="disk{uid}"><circle cx="{C}" cy="{C}" r="{DISK_R}"></circle></clipPath>'
            f'<linearGradient id="scrim{uid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0.55" stop-color="#000000" stop-opacity="0"></stop>'
            f'<stop offset="0.8" stop-color="#000000" stop-opacity="0.55"></stop></linearGradient></defs>'
            f'<g clip-path="url(#disk{uid})"><g style="filter: url(#blur{uid})">'
            + ''.join(f'<circle cx="{x}" cy="{y}" r="{r}" style="fill: {c}"></circle>' for c, x, y, r in blobs) +
            f'</g><rect x="0" y="0" width="466" height="466" style="fill: #000000; opacity: {dim}"></rect>'
            f'<rect x="0" y="0" width="466" height="466" style="fill: url(#scrim{uid})"></rect></g>')


def volume_arc(vol, colour='#8A8A8A'):
    return ''
    """Volume on the right side of the bezel, r 207-215: grows upward from 150 deg to 30 deg."""
    a0, a1 = 150, 150 - 120 * vol / 100
    p0, p1, p2 = polar(150, VOL_R), polar(a1, VOL_R), polar(30, VOL_R)
    return (f'<path d="M {p0[0]} {p0[1]} A {VOL_R} {VOL_R} 0 0 0 {p2[0]} {p2[1]}" style="fill: none; stroke: #1A1A1A; stroke-width: 8; stroke-linecap: round"></path>'
            f'<path d="M {p0[0]} {p0[1]} A {VOL_R} {VOL_R} 0 0 0 {p1[0]} {p1[1]}" style="fill: none; stroke: {colour}; stroke-width: 8; stroke-linecap: round"></path>')


def ring(frac, colour, sweep=True, head_extra='', track='#262626', glow=False, arc_opacity=1.0, head_opacity=1.0):
    L = CIRC * frac
    theta = 360 * frac
    hx, hy = polar(theta, RING_R)
    anim = ' animation: wxringsweep 6s linear infinite;' if sweep else ''
    g = (f'<filter id="ringglow" x="-20%" y="-20%" width="140%" height="140%"><feGaussianBlur stdDeviation="6"></feGaussianBlur></filter>'
         f'<circle cx="{C}" cy="{C}" r="{RING_R}" transform="rotate(-90 {C} {C})" style="fill: none; stroke: {colour}; stroke-width: 22; '
         f'stroke-linecap: round; stroke-dasharray: {L:.1f} {CIRC:.1f}; opacity: 0.45; filter: url(#ringglow);{anim}"></circle>') if glow else ''
    return (f'<g style="opacity: {arc_opacity}"><circle cx="{C}" cy="{C}" r="{RING_R}" style="fill: none; stroke: {track}; stroke-width: {RING_W}"></circle>' + g +
            f'<circle cx="{C}" cy="{C}" r="{RING_R}" transform="rotate(-90 {C} {C})" style="fill: none; stroke: {colour}; stroke-width: {RING_W}; '
            f'stroke-linecap: round; stroke-dasharray: {L:.1f} {CIRC:.1f};{anim}"></circle></g>'
            f'<g style="opacity: {head_opacity}; transform-origin: {C}px {C}px;{" animation: wxheadsweep 6s linear infinite;" if sweep else ""}">'
            + head_dot(hx, hy, colour) + f'{head_extra}</g>'), (hx, hy), L, theta


def head_dot(hx, hy, colour, uid='h'):
    return (f'<filter id="headglow{uid}" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="4"></feGaussianBlur></filter>'
            f'<circle cx="{hx}" cy="{hy}" r="13" style="fill: {colour}; opacity: 0.55; filter: url(#headglow{uid})"></circle>'
            f'<circle cx="{hx}" cy="{hy}" r="8" style="fill: #F4F4F2; stroke: {colour}; stroke-width: 2"></circle>')


def sweep_kf(L, theta):
    return [f'@keyframes wxringsweep{{0%{{stroke-dasharray:0 {CIRC:.1f};animation-timing-function:{EASE}}}22%,100%{{stroke-dasharray:{L:.1f} {CIRC:.1f}}}}}',
            f'@keyframes wxheadsweep{{0%{{transform:rotate({-theta:.1f}deg);animation-timing-function:{EASE}}}22%,100%{{transform:rotate(0deg)}}}}']


def counter(name, labels, size, lh, colour, align='center'):
    n = len(labels) - 1
    parts = []
    for k in range(n + 1):
        t = 1 - (1 - k / n) ** (1 / 3)
        sel = E.fmt(22 * t) + '%' + (',100%' if k == n else '')
        parts.append(f'{sel}{{transform:translateY({E.fmt(-lh * k) if k else 0}px);animation-timing-function:step-end}}')
    kf = '@keyframes ' + name + '{' + ''.join(parts) + '}'
    lines = ''.join(f'<div style="height: {lh}px; text-align: {align}; white-space: nowrap">{l}</div>' for l in labels)
    side = {'center': 'left: 50%; transform: translateX(-50%)', 'right': 'right: 0', 'left': 'left: 0'}[align]
    html = (f'<span style="position: relative; display: inline-block; font-size: {size}px; line-height: {lh}px; color: {colour}">'
            f'<span style="color: transparent">{labels[-1]}</span>'
            f'<span aria-hidden="true" style="position: absolute; {side}; top: 0; width: max-content; height: {lh}px; overflow: hidden">'
            f'<span style="display: block; animation: {name} 6s infinite">{lines}</span></span></span>')
    return html, kf


def time_labels(a, b, n=23):
    return [mmss(round(a + (b - a) * i / n)) for i in range(n + 1)]


def page(title, kfs, body):
    return f'''<!doctype html>
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
</style>
</helmet>
<div style="width: 466px; height: 466px; position: relative; overflow: hidden; border-radius: 50%; background: #000000; color: #F4F4F2; font-family: Montserrat, 'Avenir Next', 'Segoe UI', sans-serif; font-weight: 500">
{body}
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


def svg(content, label=None, style=''):
    role = f'role="img" aria-label="{label}"' if label else 'aria-hidden="true"'
    return f'<svg viewBox="0 0 466 466" width="466" height="466" {role} style="position: absolute; left: 0; top: 0;{style}">\n{content}\n</svg>'


def text_block(top, html_lines):
    return (f'<div style="position: absolute; left: 0; top: {top}px; width: 466px; display: flex; flex-direction: column; align-items: center">'
            + ''.join(html_lines) + '</div>')


ALBUM_C, ALBUM_R = (233, 165), 104
FRAC = TRACK['pos'] / TRACK['dur']


def now_playing(name, mode):
    t = TRACK
    acc = t['accent']
    cx, cy = ALBUM_C
    kfs = []
    paused, dimmed = mode == 'paused', mode == 'dimmed'
    hx, hy = polar(360 * FRAC, RING_R)
    av = avatar(hx, hy, 32, name)
    ring_svg, head, L, theta = ring(FRAC, '#6A6A6A' if paused else acc, sweep=(mode == 'playing'), head_extra=av,
                                    arc_opacity=0.35 if dimmed else 1.0, head_opacity=0.75 if dimmed else 1.0)
    if mode == 'playing':
        kfs += sweep_kf(L, theta)
    album = art_low_tide(cx, cy, ALBUM_R, name)
    if mode == 'playing':
        kfs += ['@keyframes wxalbumin{0%{transform:scale(0.85);opacity:0;animation-timing-function:' + EASE + '}22%,100%{transform:scale(1);opacity:1}}',
                '@keyframes wxdrift{0%,100%{transform:translate(-1.5px,0px)}50%{transform:translate(1.5px,1px)}}']
        album_g = (f'<g style="transform-origin: {cx}px {cy}px; animation: wxalbumin 6s linear infinite">'
                   f'<g style="animation: wxdrift 24s ease-in-out infinite">{album}</g></g>')
    elif dimmed:
        kfs += ['@keyframes wxburn{0%,100%{transform:translate(6px,-5px)}50%{transform:translate(-2px,3px)}}']
        album_g = f'<g style="animation: wxburn 40s ease-in-out infinite">{album}</g>'
    else:
        album_g = album
    face_filter = ' filter: saturate(0.2) brightness(0.62);' if paused else ''
    layers = []
    layers.append(svg(backdrop(name) + f'<g style="{face_filter}">{album_g}</g>', f'Album art for {t["album"]}: an abstract sunset over a teal sea',
                      style=' opacity: 0.35;' if dimmed else ''))
    if paused:
        layers.append(svg(f'<circle cx="{cx}" cy="{cy}" r="54" style="fill: #000000; opacity: 0.55"></circle>'
                          f'<rect x="{cx - 19}" y="{cy - 23}" width="13" height="46" rx="4" style="fill: #F4F4F2"></rect>'
                          f'<rect x="{cx + 6}" y="{cy - 23}" width="13" height="46" rx="4" style="fill: #F4F4F2"></rect>', 'Paused'))
    vol = volume_arc(t['volume'], '#4A4A4A' if paused else '#8A8A8A')
    if dimmed:
        layers.append(svg(f'<g style="opacity: 0.35">{vol}</g>' + ring_svg,
                          f'Progress {mmss(t["pos"])} of {mmss(t["dur"])}, volume {t["volume"]} percent'))
    else:
        layers.append(svg(vol + ring_svg, f'Progress {mmss(t["pos"])} of {mmss(t["dur"])}, the artist photo riding the progress point; volume {t["volume"]} percent',
                          style=' filter: saturate(0.2);' if paused else ''))
    if mode == 'playing':
        time_html, kf = counter('wxtime', time_labels(0, t['pos']), 24, 28, '#BDBDBD', 'right')
        kfs.append(kf)
        time_line = f'<div style="font-size: 24px; line-height: 28px; color: #BDBDBD; white-space: nowrap">{time_html} / {mmss(t["dur"])}</div>'
    elif paused:
        time_line = '<div style="font-size: 24px; line-height: 28px; color: #BDBDBD">Paused</div>'
    else:
        time_line = f'<div style="font-size: 24px; line-height: 28px; color: #BDBDBD; opacity: 0.75">{mmss(t["pos"])} / {mmss(t["dur"])}</div>'
    tdim = ' opacity: 0.5;' if dimmed else ''
    texts = text_block(282, [f'<div style="font-size: 30px; line-height: 36px; color: #F4F4F2; white-space: nowrap;{tdim}">{t["title"]}</div>',
                             f'<div style="font-size: 24px; line-height: 28px; color: #9A9A9A; white-space: nowrap; margin-top: 2px;{tdim}">{t["artist"]}</div>',
                             f'<div style="margin-top: 6px">{time_line}</div>'])
    if paused or dimmed:
        kfs.append('@keyframes wxfadein{0%{opacity:0;animation-timing-function:' + EASE + '}22%,100%{opacity:1}}')
        body = f'<div style="position: absolute; inset: 0; animation: wxfadein 6s linear infinite">\n' + '\n'.join(layers) + '\n' + texts + '\n</div>'
    else:
        body = '\n'.join(layers) + '\n' + texts
    return body, kfs


def nothing_playing():
    grooves = ''.join(f'<circle cx="233" cy="186" r="{r}" style="fill: none; stroke: #1C1C1C; stroke-width: 1.5"></circle>' for r in range(52, 119, 9))
    rec = (f'<circle cx="233" cy="186" r="122" style="fill: none; stroke: #2A2A2A; stroke-width: 2"></circle>{grooves}'
           '<circle cx="233" cy="186" r="40" style="fill: none; stroke: #2A2A2A; stroke-width: 2"></circle>'
           '<circle cx="233" cy="186" r="4" style="fill: #2A2A2A"></circle>'
           '<g style="transform-origin: 233px 186px; animation: wxspin 24s linear infinite">'
           '<path d="M 233 86 A 100 100 0 0 1 314 128" style="fill: none; stroke: #3A3A3A; stroke-width: 3; stroke-linecap: round"></path>'
           '<path d="M 233 286 A 100 100 0 0 1 152 244" style="fill: none; stroke: #2E2E2E; stroke-width: 3; stroke-linecap: round"></path></g>')
    kfs = ['@keyframes wxspin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}',
           '@keyframes wxfadein{0%{opacity:0;animation-timing-function:' + EASE + '}22%,100%{opacity:1}}']
    body = (f'<div style="position: absolute; inset: 0; animation: wxfadein 6s linear infinite">\n'
            + svg(rec, 'A faint outline of a vinyl record, slowly turning') + '\n'
            + text_block(326, ['<div style="font-size: 30px; line-height: 36px; color: #BDBDBD">Nothing playing</div>',
                               '<div style="font-size: 24px; line-height: 28px; color: #6A6A6A; margin-top: 6px">push to resume</div>'])
            + '\n</div>')
    return body, kfs


def curved(text, r, size, colour, uid, span_deg=120):
    p0, p1 = polar(180 + span_deg / 2, r), polar(180 - span_deg / 2, r)
    return (f'<path id="arc{uid}" d="M {p0[0]} {p0[1]} A {r} {r} 0 0 0 {p1[0]} {p1[1]}" style="fill: none"></path>'
            f'<text style="font-size: {size}px; fill: {colour}"><textPath href="#arc{uid}" startOffset="50%" text-anchor="middle">{text}</textPath></text>')


def vinyl(t=TRACK, label_svg=None, with_backdrop=True):
    acc = t['accent']
    RC, RR, LR = (233, 200), 135, 52
    grooves = ''.join(f'<circle cx="{RC[0]}" cy="{RC[1]}" r="{r}" style="fill: none; stroke: #181818; stroke-width: 1"></circle>' for r in range(60, 132, 5))
    sheen = ''.join(f'<path d="M {RC[0]} {RC[1]} L {polar(a0, RR, *RC)[0]} {polar(a0, RR, *RC)[1]} A {RR} {RR} 0 0 1 {polar(a1, RR, *RC)[0]} {polar(a1, RR, *RC)[1]} Z" '
                    f'style="fill: url(#sheen)"></path>' for a0, a1 in ((300, 335), (120, 155)))
    label = label_svg if label_svg else art_low_tide(RC[0], RC[1], LR, 'M5')
    rec = ('<defs><radialGradient id="sheen" gradientUnits="userSpaceOnUse" cx="233" cy="200" r="135">'
           '<stop offset="0.4" stop-color="#FFFFFF" stop-opacity="0"></stop><stop offset="0.8" stop-color="#FFFFFF" stop-opacity="0.07"></stop>'
           '<stop offset="1" stop-color="#FFFFFF" stop-opacity="0.02"></stop></radialGradient></defs>'
           f'<g style="transform-origin: {RC[0]}px {RC[1]}px; animation: wxrecin 6s linear infinite">'
           f'<circle cx="{RC[0]}" cy="{RC[1]}" r="{RR}" style="fill: #0B0B0B; stroke: #222222; stroke-width: 2"></circle>{grooves}{sheen}'
           f'<g style="transform-origin: {RC[0]}px {RC[1]}px; animation: wxspin 12s linear infinite">{label}'
           f'<circle cx="{RC[0] + 30}" cy="{RC[1] - 30}" r="4" style="fill: #FFFFFF; opacity: 0.5"></circle></g>'
           f'<circle cx="{RC[0]}" cy="{RC[1]}" r="4" style="fill: #000000; stroke: #333333; stroke-width: 1.5"></circle></g>')
    arm = ('<circle cx="398" cy="250" r="10" style="fill: #2A2A2A; stroke: #4A4A4A; stroke-width: 2"></circle>'
           '<path d="M 398 250 L 352 170 L 334 148" style="fill: none; stroke: #6A6A6A; stroke-width: 4; stroke-linecap: round; stroke-linejoin: round"></path>'
           '<rect x="326" y="136" width="14" height="20" rx="3" transform="rotate(-40 333 146)" style="fill: #8A8A8A"></rect>')
    ring_svg, head, L, theta = ring(t['pos'] / t['dur'], acc)
    kfs = sweep_kf(L, theta) + ['@keyframes wxspin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}',
                                '@keyframes wxrecin{0%{transform:scale(0.9);opacity:0;animation-timing-function:' + EASE + '}22%,100%{transform:scale(1);opacity:1}}']
    rem_html, kf = counter('wxrem', ['−' + l for l in time_labels(t['dur'], t['dur'] - t['pos'])], 24, 28, '#BDBDBD')
    kfs.append(kf)
    texts = (svg(curved(t['title'], 152, 30, '#F4F4F2', 'T') + curved(t['artist'], 178, 24, '#9A9A9A', 'A'),
                 f'{t["title"]} by {t["artist"]}') + '\n'
             + f'<div style="position: absolute; left: 0; top: 96px; width: 466px; text-align: center">{rem_html}</div>')
    body = ((svg(backdrop('M5', dim=0.72), None) + '\n' if with_backdrop else '')
            + svg(rec + arm, 'A black vinyl record spinning slowly, ' + ('a plain label with a music note at its centre' if label_svg else 'the album art as its centre label')) + '\n'
            + svg(volume_arc(t['volume']) + ring_svg, f'Progress {mmss(t["pos"])} of {mmss(t["dur"])}') + '\n' + texts)
    return body, kfs


def type_first():
    t = TRACK
    acc = t['accent']
    ring_svg, head, L, theta = ring(FRAC, acc, glow=True)
    kfs = sweep_kf(L, theta) + [
        '@keyframes wxtitlein{0%{opacity:0;transform:translateY(12px);animation-timing-function:' + EASE + '}22%,100%{opacity:1;transform:translateY(0px)}}',
        '@keyframes wxeq{0%,100%{transform:scaleY(0.35)}50%{transform:scaleY(1)}}']
    glow = (f'<defs><radialGradient id="tfglow" gradientUnits="userSpaceOnUse" cx="233" cy="200" r="200">'
            f'<stop offset="0" stop-color="{acc}" stop-opacity="0.16"></stop><stop offset="1" stop-color="{acc}" stop-opacity="0"></stop></radialGradient></defs>'
            f'<circle cx="233" cy="233" r="{DISK_R}" style="fill: url(#tfglow)"></circle>')
    rem_html, kf = counter('wxrem', time_labels(t['dur'], t['dur'] - t['pos']), 96, 84, '#F4F4F2', 'right')
    kfs.append(kf)
    eq = ''.join(f'<rect x="{221 + i * 9}" y="356" width="6" height="22" rx="2" style="fill: {acc}; transform-origin: {224 + i * 9}px 378px; '
                 f'animation: wxeq {0.9 + i * 0.17:.2f}s ease-in-out {-i * 0.31:.2f}s infinite"></rect>' for i in range(3))
    words = t['title'].split(' ')
    title_html = ''.join(f'<div style="font-size: 48px; line-height: 52px; color: #F4F4F2; white-space: nowrap">{w}</div>' for w in words)
    texts = (f'<div style="position: absolute; left: 0; top: 92px; width: 466px; display: flex; flex-direction: column; align-items: center; '
             f'animation: wxtitlein 6s linear infinite">{title_html}'
             f'<div style="font-size: 24px; line-height: 28px; color: #9A9A9A; margin-top: 6px">{t["artist"]}</div></div>'
             f'<div style="position: absolute; left: 0; top: 248px; width: 466px; display: flex; justify-content: center; align-items: baseline; gap: 8px">'
             f'<div style="font-size: 96px; line-height: 84px; letter-spacing: -2px">{rem_html}</div>'
             f'<div style="font-size: 24px; line-height: 28px; color: #9A9A9A">left</div></div>')
    body = (svg(glow + volume_arc(t['volume']) + ring_svg + eq, f'Progress {mmss(t["pos"])} of {mmss(t["dur"])} in the album colour, a small equaliser bouncing while it plays')
            + '\n' + texts)
    return body, kfs


BOT_SCRIPT = """<script type="text/x-dc" data-dc-script data-props='{"mood":{"editor":"enum","options":["idle","working","waiting","done","error"],"default":"working"},"$preview":{"width":466,"height":466}}'>
class Component extends DCLogic {
renderVals() {
const colors = { idle: '#888888', working: '#2EE85E', waiting: '#FFC14D', done: '#4FA9FF', error: '#FF3A3A' };
return { mood: colors[this.props.mood] || '#2EE85E' };
}
}
</script>"""
TRACK2 = dict(title='Paper Moons', artist='Ivy Calder', album='Paper Moons', source='Apple Music', pos=3, dur=205, accent='#7FA8FF')


def art_paper_moons(cx, cy, r, uid):
    def crescent(x, y, rr, off, col, op=1):
        return (f'<mask id="cm{uid}{x}"><rect x="{cx - r}" y="{cy - r}" width="{2 * r}" height="{2 * r}" style="fill: #FFFFFF"></rect>'
                f'<circle cx="{x + off:.1f}" cy="{y - off * 0.4:.1f}" r="{rr:.1f}" style="fill: #000000"></circle></mask>'
                f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{rr:.1f}" mask="url(#cm{uid}{x})" style="fill: {col}; opacity: {op}"></circle>')
    stars = ''.join(f'<circle cx="{cx + dx * r:.1f}" cy="{cy + dy * r:.1f}" r="{s:.1f}" style="fill: #DDE6FF; opacity: 0.8"></circle>'
                    for dx, dy, s in ((-0.5, -0.5, 2), (0.45, -0.6, 1.6), (0.6, 0.1, 1.8), (-0.62, 0.2, 1.4), (0.1, -0.75, 1.3)))
    return (f'<defs><linearGradient id="nsky{uid}" x1="0" y1="0" x2="0.3" y2="1">'
            '<stop offset="0" stop-color="#0B1640"></stop><stop offset="0.6" stop-color="#23407A"></stop><stop offset="1" stop-color="#3D6BB8"></stop></linearGradient>'
            f'<clipPath id="nclip{uid}"><circle cx="{cx}" cy="{cy}" r="{r}"></circle></clipPath></defs>'
            f'<g clip-path="url(#nclip{uid})"><rect x="{cx - r}" y="{cy - r}" width="{2 * r}" height="{2 * r}" style="fill: url(#nsky{uid})"></rect>'
            + stars + crescent(cx - r * 0.12, cy - r * 0.05, r * 0.46, r * 0.22, '#CFE0FF')
            + crescent(cx + r * 0.38, cy + r * 0.42, r * 0.22, r * 0.1, '#7FA8FF', 0.9)
            + crescent(cx - r * 0.5, cy + r * 0.5, r * 0.14, r * 0.07, '#9FBCFF', 0.8) + '</g>')


def avatar_blue(cx, cy, r, uid):
    return avatar(cx, cy, r, uid).replace('#F2B48A', '#A9C4FF').replace('#6B3F7A', '#24346E')


def backdrop_blue(uid, dim=0.5):
    return (backdrop(uid, dim).replace('#FF8A5C', '#3D6BB8').replace('#8E3B7A', '#1B2A66').replace('#1F8A8A', '#5B8FE0').replace('#FFC27A', '#9FBCFF'))


def m1_text(t, time_text, extra_style='', colour_time='#BDBDBD'):
    return text_block(282, [f'<div style="font-size: 30px; line-height: 36px; color: #F4F4F2; white-space: nowrap">{t["title"]}</div>',
                            f'<div style="font-size: 24px; line-height: 28px; color: #9A9A9A; white-space: nowrap; margin-top: 2px">{t["artist"]}</div>',
                            f'<div style="margin-top: 6px; font-size: 24px; line-height: 28px; color: {colour_time}; white-space: nowrap">{time_text}</div>']
                      ).replace('<div style="position: absolute; left: 0; top: 282px;', f'<div style="position: absolute; left: 0; top: 282px;{extra_style}', 1)


def steps_counter(name, labels, times, size, lh, colour, align='center'):
    parts = [f'{E.fmt(tm)}%{{transform:translateY({-lh * k}px);animation-timing-function:step-end}}' for k, tm in enumerate(times)]
    parts[-1] = parts[-1].replace('%{', '%,100%{', 1)
    kf = '@keyframes ' + name + '{0%{transform:translateY(0px);animation-timing-function:step-end}' + ''.join(parts[1:]) + '}'
    lines = ''.join(f'<div style="height: {lh}px; text-align: {align}; white-space: nowrap">{l}</div>' for l in labels)
    side = {'center': 'left: 50%; transform: translateX(-50%)', 'right': 'right: 0', 'left': 'left: 0'}[align]
    html = (f'<span style="position: relative; display: inline-block; font-size: {size}px; line-height: {lh}px; color: {colour}">'
            f'<span style="color: transparent">{labels[-1]}</span><span aria-hidden="true" style="position: absolute; {side}; top: 0; width: max-content; '
            f'height: {lh}px; overflow: hidden"><span style="display: block; animation: {name} 6s infinite">{lines}</span></span></span>')
    return html, kf


def volume_face():
    t = TRACK
    acc = t['accent']
    cx, cy = ALBUM_C
    ring_svg, head, L, theta = ring(FRAC, acc, sweep=False, head_extra=avatar(*polar(360 * FRAC, RING_R), 32, 'M7'))
    span = 2 * math.pi * VOL_R * 120 / 360
    vals, times = [56, 58, 60, 62], [0, 5, 10, 15]
    p150, p30 = polar(150, VOL_R), polar(30, VOL_R)
    arc_d = f'M {p150[0]} {p150[1]} A {VOL_R} {VOL_R} 0 0 0 {p30[0]} {p30[1]}'
    steps = ''.join(f'{E.fmt(tm)}%{{stroke-dasharray:{span * v / 100:.1f} {span:.1f};animation-timing-function:step-end}}' for v, tm in zip(vals, times))
    kfs = ['@keyframes wxvolarc{' + steps.replace(f'{E.fmt(times[-1])}%{{', f'{E.fmt(times[-1])}%,100%{{', 1) + '}']
    ticks = ''
    for k, (v, tm) in enumerate(zip(vals[1:], times[1:])):
        x, y = polar(150 - 120 * v / 100, VOL_R)
        kfs.append(f'@keyframes wxtick{k}{{0%,{tm}%{{opacity:0;transform:scale(0.6)}}{tm + 0.5}%{{opacity:1;transform:scale(1)}}'
                   f'{tm + 5}%,100%{{opacity:0;transform:scale(2.2)}}}}')
        ticks += (f'<circle cx="{x}" cy="{y}" r="6" style="fill: none; stroke: #FFFFFF; stroke-width: 2.5; opacity: 0; transform-origin: {x}px {y}px; '
                  f'animation: wxtick{k} 6s linear infinite"></circle>')
    vol = (f'<defs><linearGradient id="volgrad" gradientUnits="userSpaceOnUse" x1="0" y1="{p150[1]}" x2="0" y2="{p30[1]}">'
           f'<stop offset="0" stop-color="{acc}"></stop><stop offset="1" stop-color="#FFFFFF"></stop></linearGradient></defs>'
           f'<path d="{arc_d}" style="fill: none; stroke: #1A1A1A; stroke-width: 8; stroke-linecap: round"></path>'
           f'<path d="{arc_d}" style="fill: none; stroke: url(#volgrad); stroke-width: 8; stroke-linecap: round; stroke-dasharray: {span * 0.62:.1f} {span:.1f}; '
           f'animation: wxvolarc 6s linear infinite"></path>' + ticks)
    num, kf = steps_counter('wxvolnum', [str(v) for v in vals], times, 96, 84, '#F4F4F2')
    kfs.append(kf)
    speaker = (f'<g transform="translate({cx - 20} 96)"><rect x="0" y="8" width="9" height="14" rx="2" style="fill: #F4F4F2"></rect>'
               '<path d="M 9 8 L 20 0 V 30 L 9 22 Z" style="fill: #F4F4F2"></path>'
               '<path d="M 26 9 Q 31 15 26 21" style="fill: none; stroke: #F4F4F2; stroke-width: 3; stroke-linecap: round"></path>'
               '<path d="M 31 4 Q 39 15 31 26" style="fill: none; stroke: #F4F4F2; stroke-width: 3; stroke-linecap: round"></path></g>')
    body = (svg(backdrop('M7') + f'<g style="opacity: 0.4">{art_low_tide(cx, cy, ALBUM_R, "M7")}</g>' + speaker, 'Album dimmed behind the volume level') + '\n'
            + svg(vol + ring_svg, f'Volume rising to {t["volume"]}, the arc in the bezel growing a step per detent') + '\n'
            + f'<div style="position: absolute; left: 0; top: 128px; width: 466px; text-align: center; letter-spacing: -2px">{num}</div>\n'
            + m1_text(t, f'{mmss(t["pos"])} / {mmss(t["dur"])}'))
    return body, kfs


def next_track():
    a, b = TRACK, TRACK2
    cx, cy = ALBUM_C
    fo, fn = a['pos'] / a['dur'], b['pos'] / b['dur']
    Lo, Ln, tho, thn = CIRC * fo, CIRC * fn, 360 * fo, 360 * fn
    hx, hy = polar(thn, RING_R)
    kfs = [
        '@keyframes wxoldout{0%{transform:translateX(0px);opacity:1;animation-timing-function:cubic-bezier(0.5,0,0.8,0.4)}10%,100%{transform:translateX(-130px);opacity:0}}',
        '@keyframes wxnewin{0%,6%{transform:translateX(130px);opacity:0;animation-timing-function:' + EASE + '}20%,100%{transform:translateX(0px);opacity:1}}',
        '@keyframes wxfadeout{0%{opacity:1}8%,100%{opacity:0}}',
        '@keyframes wxfadein2{0%,10%{opacity:0}20%,100%{opacity:1}}',
        '@keyframes wxbgout{0%,4%{opacity:1}16%,100%{opacity:0}}',
        '@keyframes wxbgin{0%,4%{opacity:0}16%,100%{opacity:1}}',
        f'@keyframes wxunwind{{0%{{stroke-dasharray:{Lo:.1f} {CIRC:.1f};stroke:{a["accent"]};animation-timing-function:cubic-bezier(0.6,0,0.4,1)}}'
        f'8%{{stroke-dasharray:0 {CIRC:.1f};stroke:{a["accent"]}}}12%{{stroke:{b["accent"]}}}20%,100%{{stroke-dasharray:{Ln:.1f} {CIRC:.1f};stroke:{b["accent"]}}}}}',
        f'@keyframes wxheadun{{0%{{transform:rotate({tho - thn:.1f}deg);animation-timing-function:cubic-bezier(0.6,0,0.4,1)}}8%{{transform:rotate({-thn:.1f}deg)}}'
        f'20%,100%{{transform:rotate(0deg)}}}}',
        f'@keyframes wxheadcol{{0%,8%{{fill:{a["accent"]}}}12%,100%{{fill:{b["accent"]}}}}}',
        '@keyframes wxskip{0%,3%{opacity:0;transform:scale(0.8)}8%{opacity:1;transform:scale(1.06)}12%{opacity:1;transform:scale(1)}17%,100%{opacity:0;transform:scale(1.1)}}',
    ]
    head = (f'<g style="transform-origin: {C}px {C}px; animation: wxheadun 6s linear infinite">'
            f'<circle cx="{hx}" cy="{hy}" r="6" style="animation: wxheadcol 6s linear infinite"></circle>'
            f'<g style="animation: wxfadeout 6s linear infinite">{avatar(hx, hy, 32, "M8o")}</g>'
            f'<g style="opacity: 0; animation: wxfadein2 6s linear infinite">{avatar_blue(hx, hy, 32, "M8n")}</g></g>')
    ring_svg = (f'<circle cx="{C}" cy="{C}" r="{RING_R}" style="fill: none; stroke: #262626; stroke-width: 6"></circle>'
                f'<circle cx="{C}" cy="{C}" r="{RING_R}" transform="rotate(-90 {C} {C})" style="fill: none; stroke: {b["accent"]}; stroke-width: 6; '
                f'stroke-linecap: round; stroke-dasharray: {Ln:.1f} {CIRC:.1f}; animation: wxunwind 6s linear infinite"></circle>' + head)
    skip = (f'<g style="transform-origin: {cx}px {cy}px; opacity: 0; animation: wxskip 6s linear infinite">'
            f'<circle cx="{cx}" cy="{cy}" r="46" style="fill: #000000; opacity: 0.55"></circle>'
            f'<path d="M {cx - 26} {cy - 16} L {cx - 4} {cy} L {cx - 26} {cy + 16} Z M {cx - 4} {cy - 16} L {cx + 18} {cy} L {cx - 4} {cy + 16} Z" style="fill: #F4F4F2; stroke: #F4F4F2; stroke-width: 3; stroke-linejoin: round"></path>'
            f'<rect x="{cx + 19}" y="{cy - 17}" width="6" height="34" rx="2" style="fill: #F4F4F2"></rect></g>')
    body = (svg(f'<g style="animation: wxbgout 6s linear infinite">{backdrop("M8o")}</g>'
                f'<g style="opacity: 0; animation: wxbgin 6s linear infinite">{backdrop_blue("M8n")}</g>'
                f'<g style="animation: wxoldout 6s linear infinite">{art_low_tide(cx, cy, ALBUM_R, "M8o")}</g>'
                f'<g style="opacity: 0; animation: wxnewin 6s linear infinite">{art_paper_moons(cx, cy, ALBUM_R, "M8n")}</g>' + skip,
                f'Skipping to the next track: {a["album"]} slides out, {b["album"]} by {b["artist"]} slides in') + '\n'
            + svg(volume_arc(b['volume'] if 'volume' in b else 62) + ring_svg, f'Progress reset to {mmss(b["pos"])} of {mmss(b["dur"])}') + '\n'
            + m1_text(a, f'{mmss(a["pos"])} / {mmss(a["dur"])}', ' animation: wxfadeout 6s linear infinite;') + '\n'
            + m1_text(b, f'{mmss(b["pos"])} / {mmss(b["dur"])}', ' opacity: 0; animation: wxfadein2 6s linear infinite;'))
    return body, kfs


def resume_tap():
    t = TRACK
    acc = t['accent']
    cx, cy = ALBUM_C
    hx, hy = polar(360 * FRAC, RING_R)
    ring_svg, head, L, theta = ring(FRAC, acc, sweep=False, head_extra=avatar(hx, hy, 32, 'M9'))
    ring_svg = ring_svg.replace(f'stroke: {acc}; stroke-width: 6;', f'stroke: {acc}; stroke-width: 6; animation: wxretint 6s linear infinite;', 1)
    ring_svg = ring_svg.replace(f'<circle cx="{hx}" cy="{hy}" r="6" style="fill: {acc}">', f'<circle cx="{hx}" cy="{hy}" r="6" style="fill: {acc}; animation: wxretintf 6s linear infinite">', 1)
    kfs = ['@keyframes wxcolour{0%,4%{filter:saturate(0.2) brightness(0.62)}18%,100%{filter:saturate(1) brightness(1)}}',
           f'@keyframes wxretint{{0%,6%{{stroke:#6A6A6A}}18%,100%{{stroke:{acc}}}}}',
           f'@keyframes wxretintf{{0%,6%{{fill:#6A6A6A}}18%,100%{{fill:{acc}}}}}',
           '@keyframes wxplay{0%{opacity:0;transform:scale(0.4);animation-timing-function:' + EASE + '}6%{opacity:1;transform:scale(1.12)}12%{opacity:1;transform:scale(1)}22%,100%{opacity:0;transform:scale(1.05)}}',
           '@keyframes wxfadeout{0%,4%{opacity:1}10%,100%{opacity:0}}',
           '@keyframes wxfadein2{0%,10%{opacity:0}18%,100%{opacity:1}}']
    play = (f'<g style="transform-origin: {cx}px {cy}px; opacity: 0; animation: wxplay 6s linear infinite">'
            f'<circle cx="{cx}" cy="{cy}" r="54" style="fill: #000000; opacity: 0.55"></circle>'
            f'<path d="M {cx - 14} {cy - 24} L {cx + 24} {cy} L {cx - 14} {cy + 24} Z" style="fill: #F4F4F2; stroke: #F4F4F2; stroke-width: 4; stroke-linejoin: round"></path></g>')
    body = (svg(f'<g style="animation: wxcolour 6s linear infinite">{backdrop("M9")}{art_low_tide(cx, cy, ALBUM_R, "M9")}</g>' + play,
                'Resuming: the colour returns to the album as a play symbol grows and fades') + '\n'
            + svg(f'<g style="animation: wxcolour 6s linear infinite">{volume_arc(t["volume"])}{ring_svg}</g>', f'Progress {mmss(t["pos"])} of {mmss(t["dur"])}') + '\n'
            + text_block(282, [f'<div style="font-size: 30px; line-height: 36px; color: #F4F4F2">{t["title"]}</div>',
                               f'<div style="font-size: 24px; line-height: 28px; color: #9A9A9A; margin-top: 2px">{t["artist"]}</div>',
                               '<div style="margin-top: 6px; position: relative; height: 28px; width: 200px">'
                               '<div style="position: absolute; inset: 0; text-align: center; font-size: 24px; line-height: 28px; color: #BDBDBD; animation: wxfadeout 6s linear infinite">Paused</div>'
                               f'<div style="position: absolute; inset: 0; text-align: center; font-size: 24px; line-height: 28px; color: #BDBDBD; opacity: 0; animation: wxfadein2 6s linear infinite">{mmss(t["pos"])} / {mmss(t["dur"])}</div></div>']))
    return body, kfs


def error_badge():
    t = TRACK
    cx, cy = ALBUM_C
    hx, hy = polar(360 * FRAC, RING_R)
    ring_svg, head, L, theta = ring(FRAC, '#5A5A5A', sweep=False, head_extra=f'<g style="filter: saturate(0) brightness(0.7)">{avatar(hx, hy, 32, "M10")}</g>')
    kfs = ['@keyframes wxbadge{0%,3%{opacity:0;transform:scale(0.9);animation-timing-function:' + EASE + '}16%,100%{opacity:1;transform:scale(1)}}']
    cloud = ('M -20 8 H 18 A 8 8 0 0 0 16 -7 A 12 12 0 0 0 -7 -8 A 11 11 0 0 0 -20 8 Z')
    badge = (f'<g style="transform-origin: {cx}px {cy}px; opacity: 0; animation: wxbadge 6s linear infinite">'
             f'<rect x="{cx - 66}" y="{cy - 66}" width="132" height="132" rx="26" style="fill: #0A0A0A; opacity: 0.86; stroke: #3A3A3A; stroke-width: 2"></rect>'
             f'<g transform="translate({cx} {cy - 30})"><path d="{cloud}" style="fill: #9AA4B0"></path>'
             '<line x1="-18" y1="-18" x2="18" y2="18" style="stroke: #0A0A0A; stroke-width: 9; stroke-linecap: round"></line>'
             '<line x1="-18" y1="-18" x2="18" y2="18" style="stroke: #FF5050; stroke-width: 4; stroke-linecap: round"></line></g>'
             f'<text x="{cx}" y="{cy + 14}" text-anchor="middle" style="font-size: 24px; fill: #F4F4F2">Ember</text>'
             f'<text x="{cx}" y="{cy + 42}" text-anchor="middle" style="font-size: 24px; fill: #F4F4F2">offline</text></g>')
    body = (svg(backdrop('M10') + f'<g style="opacity: 0.7">{art_low_tide(cx, cy, ALBUM_R, "M10")}</g>' + badge,
                'Ember offline: a badge over the album with a crossed-out cloud; the track details shown are stale') + '\n'
            + svg(volume_arc(t['volume'], '#4A4A4A') + ring_svg, 'Progress ring greyed while the data is stale') + '\n'
            + m1_text(t, f'{mmss(t["pos"])} / {mmss(t["dur"])}', colour_time='#6A6A6A'))
    return body, kfs


BLE = '#7FA8FF'


def link_glyph(x, y, colour):
    return (f'<g transform="translate({x} {y})"><circle cx="0" cy="0" r="5" style="fill: {colour}"></circle>'
            f'<path d="M 9 -8 Q 15 0 9 8" style="fill: none; stroke: {colour}; stroke-width: 3; stroke-linecap: round"></path>'
            f'<path d="M 15 -14 Q 25 0 15 14" style="fill: none; stroke: {colour}; stroke-width: 3; stroke-linecap: round"></path></g>')


def ble_face(pairing=False):
    gx, gy = 233, 190
    glyph = (f'<path d="M {gx - 44} {gy - 30} L {gx - 2} {gy} L {gx - 44} {gy + 30} Z" style="fill: #E8ECF2; stroke: #E8ECF2; stroke-width: 6; stroke-linejoin: round"></path>'
             f'<rect x="{gx + 10}" y="{gy - 31}" width="13" height="62" rx="4" style="fill: #E8ECF2"></rect>'
             f'<rect x="{gx + 31}" y="{gy - 31}" width="13" height="62" rx="4" style="fill: #E8ECF2"></rect>')
    kfs = ['@keyframes wxfadein{0%{opacity:0;animation-timing-function:' + EASE + '}22%,100%{opacity:1}}']
    if pairing:
        kfs.append('@keyframes wxpair{0%,100%{opacity:0.25;transform:scale(0.96)}50%{opacity:0.9;transform:scale(1.04)}}')
        ringg = f'<circle cx="{gx}" cy="{gy}" r="84" style="fill: none; stroke: {BLE}; stroke-width: 5; transform-origin: {gx}px {gy}px; animation: wxpair 2.4s ease-in-out infinite"></circle>'
        hero = f'<g style="opacity: 0.35">{glyph}</g>' + ringg
        lines = ['<div style="font-size: 30px; line-height: 36px; color: #F4F4F2">Pairing…</div>',
                 '<div style="font-size: 24px; line-height: 28px; color: #9A9A9A; margin-top: 4px">Ember Knob</div>']
        bottom = text_block(312, lines)
        extra = ''
        label = 'Not connected: a dimmed play and pause symbol inside a slowly pulsing ring, pairing as Ember Knob'
    else:
        ringg = f'<circle cx="{gx}" cy="{gy}" r="84" style="fill: none; stroke: {BLE}; stroke-width: 5"></circle>'
        hero = glyph + ringg
        seg = 2 * math.pi * RING_R * 7 / 360
        p0, p1 = polar(90, RING_R), polar(90 - 30, RING_R)
        d = f'M {p0[0]} {p0[1]} A {RING_R} {RING_R} 0 0 0 {p1[0]} {p1[1]}'
        kfs += [f'@keyframes wxsteps{{0%{{stroke-dasharray:0.1 999;animation-timing-function:step-end}}4%{{stroke-dasharray:{seg:.1f} 999;animation-timing-function:step-end}}'
                f'9%{{stroke-dasharray:{2 * seg:.1f} 999;animation-timing-function:step-end}}14%,100%{{stroke-dasharray:{3 * seg:.1f} 999}}}}',
                '@keyframes wxstepfade{0%,30%{opacity:1}40%,100%{opacity:0}}']
        steplabel = ('<div style="position: absolute; left: 330px; top: 158px; width: 70px; text-align: center; font-size: 30px; line-height: 36px; '
                     'color: #F4F4F2; animation: wxstepfade 6s linear infinite">+3</div>')
        extra = (f'<g style="animation: wxstepfade 6s linear infinite"><path d="{d}" style="fill: none; stroke: #F4F4F2; stroke-width: {RING_W}; stroke-linecap: round; '
                 f'animation: wxsteps 6s linear infinite"></path></g>')
        bottom = (f'<div style="position: absolute; left: 0; top: 318px; width: 466px; display: flex; justify-content: center; align-items: center; gap: 10px">'
                  f'<svg viewBox="-6 -16 34 32" width="34" height="32" aria-hidden="true">{link_glyph(0, 0, BLE)}</svg>'
                  '<div style="font-size: 24px; line-height: 28px; color: #BDBDBD">MacBook</div></div>' + steplabel)
        label = 'Connected as a media remote: a play and pause symbol in a blue ring; turning shows +3 volume steps in the bezel'
    body = (f'<div style="position: absolute; inset: 0; animation: wxfadein 6s linear infinite">\n'
            + svg(hero + extra, label) + '\n' + bottom + '\n</div>')
    return body, kfs


def bot_base(label_text):
    arc = ('<path id="mbot-label-arc" d="M 57 233 A 176 176 0 0 0 409 233" style="fill: none; stroke: none"></path>'
           '<text style="font-family: Montserrat, \'Avenir Next\', \'Segoe UI\', sans-serif; font-weight: 500; font-size: 30px; letter-spacing: 1px; '
           f'fill: #F4F4F2; text-anchor: middle"><textPath href="#mbot-label-arc" startOffset="50%">{label_text}</textPath></text>')
    return '<circle cx="233" cy="233" r="196" style="fill: none; stroke: {{mood}}; stroke-width: 4.5"></circle>' + arc


def note(x, y, colour):
    return (f'<g transform="translate({x} {y})"><ellipse cx="0" cy="0" rx="6.5" ry="5" transform="rotate(-20)" style="fill: {colour}"></ellipse>'
            f'<line x1="5.5" y1="-2" x2="5.5" y2="-22" style="stroke: {colour}; stroke-width: 3; stroke-linecap: round"></line>'
            f'<path d="M 5.5 -22 Q 14 -18 12 -9" style="fill: none; stroke: {colour}; stroke-width: 3; stroke-linecap: round"></path></g>')


def bot_listening():
    soft = '#F2A07E'
    kfs = ['@keyframes wxbeat{0%,100%{transform:translateY(0px) scaleY(1)}25%{transform:translateY(4px) scaleY(0.82)}55%{transform:translateY(-5px) scaleY(1.06)}}',
           '@keyframes wxheadbob{0%,100%{transform:translateY(0px)}25%{transform:translateY(2px)}55%{transform:translateY(-2px)}}',
           '@keyframes wxphones{0%{transform:translateY(-60px);opacity:0;animation-timing-function:' + EASE + '}22%,100%{transform:translateY(0px);opacity:1}}',
           '@keyframes wxnote{0%{transform:translate(0px,0px) rotate(0deg);opacity:0}15%{opacity:1}100%{transform:translate(var(--dx),-70px) rotate(12deg);opacity:0}}']
    band = f'<path d="M 112 236 A 121 121 0 0 1 354 236" style="fill: none; stroke: {soft}; stroke-width: 12; stroke-linecap: round"></path>'
    cups = (f'<rect x="96" y="214" width="36" height="72" rx="16" style="fill: {soft}"></rect><rect x="104" y="226" width="8" height="48" rx="4" style="fill: #000000; opacity: 0.25"></rect>'
            f'<rect x="334" y="214" width="36" height="72" rx="16" style="fill: {soft}"></rect><rect x="354" y="226" width="8" height="48" rx="4" style="fill: #000000; opacity: 0.25"></rect>')
    notes = ''.join(f'<g style="--dx: {dx}px; opacity: 0; animation: wxnote 2.4s ease-out {d}s infinite">{note(x, y, soft)}</g>'
                    for x, y, dx, d in ((104, 200, -16, 0), (360, 196, 18, -0.8), (118, 190, -8, -1.6), (348, 186, 10, -2.0)))
    phones = f'<g style="animation: wxphones 6s linear infinite"><g style="animation: wxheadbob 0.6s ease-in-out infinite">{band}{cups}</g></g>'
    body = (svg(bot_base('GLASS HARBOR') + phones + notes, 'The bot wearing headphones, eyes bobbing to the beat, music notes floating up; listening to Glass Harbor') + '\n'
            + '<div style="position: absolute; left: 152px; top: 236px; width: 68px; height: 36px; border-radius: 18px; background: #F4F4F2; transform-origin: 50% 100%; animation: wxbeat 0.6s ease-in-out infinite"></div>\n'
            + '<div style="position: absolute; left: 246px; top: 236px; width: 68px; height: 36px; border-radius: 18px; background: #F4F4F2; transform-origin: 50% 100%; animation: wxbeat 0.6s ease-in-out infinite"></div>')
    return body, kfs


def bot_new_track():
    kfs = ['@keyframes wxpop{0%,2%{opacity:0;transform:scale(0.3)}10%{opacity:1;transform:scale(1.08);animation-timing-function:ease-out}14%,100%{opacity:1;transform:scale(1)}}',
           '@keyframes wxglance{0%,8%{transform:translateY(0px)}13%,48%{transform:translateY(-14px)}55%,100%{transform:translateY(0px)}}',
           '@keyframes wxlabel{0%,12%{opacity:0}22%,100%{opacity:1}}']
    album = (f'<g style="transform-origin: 233px 132px; opacity: 0; animation: wxpop 6s linear infinite">'
             f'<circle cx="233" cy="132" r="48" style="fill: #000000"></circle>{art_paper_moons(233, 132, 45, "M14")}</g>')
    body = (svg('<circle cx="233" cy="233" r="196" style="fill: none; stroke: {{mood}}; stroke-width: 4.5"></circle>' + album
                + '<g style="opacity: 0; animation: wxlabel 6s linear infinite">' + bot_base('PAPER MOONS').split('</circle>', 1)[1] + '</g>',
                'A new track: a small album circle pops in above the bot, its eyes glance up at it, Paper Moons curving along the bottom') + '\n'
            + '<div style="position: absolute; left: 152px; top: 244px; width: 68px; height: 36px; border-radius: 18px; background: #F4F4F2; animation: wxglance 6s ease-in-out infinite"></div>\n'
            + '<div style="position: absolute; left: 246px; top: 244px; width: 68px; height: 36px; border-radius: 18px; background: #F4F4F2; animation: wxglance 6s ease-in-out infinite"></div>')
    return body, kfs


HERO = (233, 180)


def full_low_tide(uid):
    waves = []
    for k, (col, y0) in enumerate([('#1F8A8A', 262), ('#2FB3A8', 306), ('#13606B', 352), ('#0B4250', 402)]):
        amp, wl = 12, 116
        d = f'M -20 {y0}'
        x = -20
        while x < 486:
            d += f' q {wl / 4:.0f} {-amp if k % 2 else amp} {wl / 2:.0f} 0 t {wl / 2:.0f} 0'
            x += wl
        waves.append(f'<path d="{d} V 486 H -20 Z" style="fill: {col}"></path>')
    stars = ''.join(f'<circle cx="{x}" cy="{y}" r="{r}" style="fill: #FFE6F0; opacity: 0.7"></circle>'
                    for x, y, r in ((150, 70, 1.8), (300, 56, 1.4), (360, 110, 1.6), (96, 150, 1.3)))
    return (f'<defs><linearGradient id="fsky{uid}" x1="0" y1="0" x2="0" y2="466" gradientUnits="userSpaceOnUse">'
            '<stop offset="0" stop-color="#2B1B5A"></stop><stop offset="0.3" stop-color="#8E3B7A"></stop>'
            '<stop offset="0.48" stop-color="#FF8A5C"></stop><stop offset="0.57" stop-color="#FFC27A"></stop></linearGradient></defs>'
            f'<rect x="-20" y="-20" width="506" height="506" style="fill: url(#fsky{uid})"></rect>' + stars +
            '<circle cx="122" cy="122" r="9" style="fill: #FFD9E6; opacity: 0.85"></circle>'
            '<circle cx="262" cy="232" r="80" style="fill: #FFE3A0"></circle>' + ''.join(waves))


def full_paper_moons(uid):
    def crescent(x, y, rr, off, col, op=1):
        return (f'<mask id="fcm{uid}{x}" maskUnits="userSpaceOnUse" x="-20" y="-20" width="506" height="506">'
                f'<rect x="-20" y="-20" width="506" height="506" style="fill: #FFFFFF"></rect>'
                f'<circle cx="{x + off}" cy="{y - off * 0.4:.0f}" r="{rr}" style="fill: #000000"></circle></mask>'
                f'<circle cx="{x}" cy="{y}" r="{rr}" mask="url(#fcm{uid}{x})" style="fill: {col}; opacity: {op}"></circle>')
    stars = ''.join(f'<circle cx="{x}" cy="{y}" r="{r}" style="fill: #DDE6FF; opacity: 0.8"></circle>'
                    for x, y, r in ((120, 96, 2), (330, 80, 1.6), (380, 180, 1.8), (90, 230, 1.4), (300, 150, 1.3), (180, 60, 1.5), (400, 260, 1.4)))
    return (f'<defs><linearGradient id="fnsky{uid}" x1="0" y1="0" x2="0.3" y2="1"><stop offset="0" stop-color="#0B1640"></stop>'
            '<stop offset="0.6" stop-color="#23407A"></stop><stop offset="1" stop-color="#3D6BB8"></stop></linearGradient></defs>'
            f'<rect x="-20" y="-20" width="506" height="506" style="fill: url(#fnsky{uid})"></rect>' + stars
            + crescent(206, 170, 92, 44, '#CFE0FF') + crescent(352, 238, 40, 18, '#7FA8FF', 0.9) + crescent(108, 250, 26, 12, '#9FBCFF', 0.8))


def legibility(uid):
    return (f'<defs><linearGradient id="fscrim{uid}" gradientUnits="userSpaceOnUse" x1="0" y1="230" x2="0" y2="400">'
            '<stop offset="0" stop-color="#000000" stop-opacity="0"></stop><stop offset="1" stop-color="#000000" stop-opacity="0.75"></stop></linearGradient>'
            f'<radialGradient id="fvig{uid}" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="233">'
            '<stop offset="0.72" stop-color="#000000" stop-opacity="0"></stop><stop offset="1" stop-color="#000000" stop-opacity="0.6"></stop></radialGradient></defs>'
            f'<rect x="0" y="0" width="466" height="466" style="fill: url(#fscrim{uid})"></rect>'
            f'<rect x="0" y="0" width="466" height="466" style="fill: url(#fvig{uid})"></rect>')


FB_TRACK = 'rgba(0, 0, 0, 0.45)'
KEN_BURNS = '@keyframes wxkenburns{0%{transform:scale(1)}100%{transform:scale(1.03)}}'
ART_IN = '@keyframes wxartin{0%{opacity:0;transform:scale(1.04);animation-timing-function:' + EASE + '}22%,100%{opacity:1;transform:scale(1)}}'


def fb_art(art, uid, outer_style='', inner_anim='animation: wxkenburns 30s ease-in-out infinite alternate'):
    return (f'<g style="transform-origin: 233px 233px;{outer_style}"><g style="transform-origin: 233px 233px; {inner_anim}">{art}</g></g>'
            + legibility(uid))


def fb_now_playing(name, mode):
    t = TRACK
    acc = t['accent']
    paused, dimmed = mode == 'paused', mode == 'dimmed'
    hx, hy = polar(360 * FRAC, RING_R)
    ring_svg, head, L, theta = ring(FRAC, '#6A6A6A' if paused else acc, sweep=(mode == 'playing'), head_extra=avatar(hx, hy, 32, name),
                                    track=FB_TRACK, arc_opacity=0.35 if dimmed else 1.0, head_opacity=0.75 if dimmed else 1.0)
    kfs = []
    art = full_low_tide(name)
    if mode == 'playing':
        kfs += sweep_kf(L, theta) + [ART_IN, KEN_BURNS]
        art_svg = fb_art(art, name, ' animation: wxartin 6s linear infinite;')
    elif paused:
        kfs += [KEN_BURNS]
        art_svg = f'<g style="filter: saturate(0.2) brightness(0.55)">{fb_art(art, name, "", "")}</g>'
    else:
        kfs += ['@keyframes wxburn{0%,100%{transform:translate(6px,-5px) scale(1.04)}50%{transform:translate(-2px,3px) scale(1.04)}}']
        art_svg = f'<g style="opacity: 0.35">{fb_art(art, name, "", "animation: wxburn 40s ease-in-out infinite")}</g>'
    layers = [svg(art_svg, f'Album art for {t["album"]}, filling the face: a dusk sky with a pale sun over teal waves')]
    if paused:
        layers.append(svg(f'<circle cx="{HERO[0]}" cy="{HERO[1]}" r="56" style="fill: #000000; opacity: 0.55"></circle>'
                          f'<rect x="{HERO[0] - 19}" y="{HERO[1] - 23}" width="13" height="46" rx="4" style="fill: #F4F4F2"></rect>'
                          f'<rect x="{HERO[0] + 6}" y="{HERO[1] - 23}" width="13" height="46" rx="4" style="fill: #F4F4F2"></rect>', 'Paused'))
    vol = volume_arc(t['volume'], '#4A4A4A' if paused else '#8A8A8A')
    layers.append(svg((f'<g style="opacity: 0.35">{vol}</g>' if dimmed else vol) + ring_svg,
                      f'Progress {mmss(t["pos"])} of {mmss(t["dur"])}, the artist photo riding the progress point; volume {t["volume"]} percent',
                      style=' filter: saturate(0.2);' if paused else ''))
    if mode == 'playing':
        time_html, kf = counter('wxtime', time_labels(0, t['pos']), 24, 28, '#BDBDBD', 'right')
        kfs.append(kf)
        time_line = f'{time_html} / {mmss(t["dur"])}'
    elif paused:
        time_line = 'Paused'
    else:
        time_line = f'{mmss(t["pos"])} / {mmss(t["dur"])}'
    tdim = ' opacity: 0.5;' if dimmed else ''
    texts = text_block(282, [f'<div style="font-size: 30px; line-height: 36px; color: #F4F4F2; white-space: nowrap;{tdim}">{t["title"]}</div>',
                             f'<div style="font-size: 24px; line-height: 28px; color: #BDBDBD; white-space: nowrap; margin-top: 2px;{tdim}">{t["artist"]}</div>',
                             f'<div style="margin-top: 6px; font-size: 24px; line-height: 28px; color: #D6D6D6; white-space: nowrap;{" opacity: 0.75;" if dimmed else ""}">{time_line}</div>'])
    if paused or dimmed:
        kfs.append('@keyframes wxfadein{0%{opacity:0;animation-timing-function:' + EASE + '}22%,100%{opacity:1}}')
        return f'<div style="position: absolute; inset: 0; animation: wxfadein 6s linear infinite">\n' + '\n'.join(layers) + '\n' + texts + '\n</div>', kfs
    return '\n'.join(layers) + '\n' + texts, kfs


def fb_text(t, time_text, extra_style='', colour_time='#D6D6D6'):
    return m1_text(t, time_text, extra_style, colour_time).replace('color: #9A9A9A', 'color: #BDBDBD')


def fb_volume():
    t = TRACK
    acc = t['accent']
    cx, cy = HERO
    body, kfs = volume_face()
    i0 = body.index('<svg'); i1 = body.index('</svg>') + len('</svg>')
    speaker = body[body.index('<g transform="translate(213 96)">'):]
    speaker = speaker[:speaker.index('</g>') + 4].replace('translate(213 96)', f'translate({cx - 20} {cy - 82})')
    body = body[:i0] + svg(f'<g style="opacity: 0.4">{fb_art(full_low_tide("M7"), "M7", "", "")}</g>' + speaker,
                           'Album art dimmed behind the volume level') + body[i1:]
    body = body.replace('stroke: #262626; stroke-width: 6"', f'stroke: {FB_TRACK}; stroke-width: 6"', 1)
    body = body.replace('<div style="position: absolute; left: 0; top: 128px;', f'<div style="position: absolute; left: 0; top: {cy - 50}px;', 1)
    body = body.replace('color: #9A9A9A', 'color: #BDBDBD')
    return body, kfs


def fb_next_track():
    a, b = TRACK, TRACK2
    body, kfs = next_track()
    i0 = body.index('<svg'); i1 = body.index('</svg>') + len('</svg>')
    skip = body[body.index('<g style="transform-origin: 233px 165px; opacity: 0; animation: wxskip'):i1 - len('</svg>')]
    skip = skip.replace('233px 165px', f'{HERO[0]}px {HERO[1]}px')
    for old, new in (('cy="165"', f'cy="{HERO[1]}"'), (' 149 ', f' {HERO[1] - 16} '), (' 165 ', f' {HERO[1]} '), (' 181 ', f' {HERO[1] + 16} '),
                     ('y="148"', f'y="{HERO[1] - 17}"')):
        skip = skip.replace(old, new)
    push = 'cubic-bezier(0.65,0,0.35,1)'
    kfs += ['@keyframes wxfbout{0%,4%{transform:translateX(0px);animation-timing-function:' + push + '}20%,100%{transform:translateX(-466px)}}',
            '@keyframes wxfbin{0%,4%{transform:translateX(466px);animation-timing-function:' + push + '}20%,100%{transform:translateX(0px)}}']
    layer_clip = '<clipPath id="fbclip{0}"><rect x="0" y="0" width="466" height="466"></rect></clipPath>'
    shadow = ('<defs><linearGradient id="fbshadow" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#000000" stop-opacity="0"></stop>'
              '<stop offset="1" stop-color="#000000" stop-opacity="0.45"></stop></linearGradient></defs>'
              '<rect x="-12" y="0" width="12" height="466" style="fill: url(#fbshadow)"></rect>')
    art = (f'<defs>{layer_clip.format("o")}{layer_clip.format("n")}</defs>'
           f'<g style="animation: wxfbout 6s linear infinite"><g clip-path="url(#fbclipo)">{full_low_tide("M8o")}</g></g>'
           f'<g style="transform: translateX(466px); animation: wxfbin 6s linear infinite">{shadow}<g clip-path="url(#fbclipn)">{full_paper_moons("M8n")}</g></g>')
    body = body[:i0] + svg(art + legibility('M8') + skip, f'Skipping to the next track: {a["album"]} slides out, {b["album"]} by {b["artist"]} slides in') + body[i1:]
    body = body.replace('stroke: #262626; stroke-width: 6"', f'stroke: {FB_TRACK}; stroke-width: 6"', 1).replace('color: #9A9A9A', 'color: #BDBDBD')
    return body, kfs


def fb_resume():
    t = TRACK
    body, kfs = resume_tap()
    i0 = body.index('<svg'); i1 = body.index('</svg>') + len('</svg>')
    play = body[body.index('<g style="transform-origin: 233px 165px; opacity: 0; animation: wxplay'):i1 - len('</svg>')]
    play = (play.replace('233px 165px', f'{HERO[0]}px {HERO[1]}px').replace('cy="165"', f'cy="{HERO[1]}"')
            .replace(' 141 ', f' {HERO[1] - 24} ').replace(' 165 ', f' {HERO[1]} ').replace(' 189 ', f' {HERO[1] + 24} '))
    body = body[:i0] + svg(f'<g style="animation: wxcolour 6s linear infinite">{fb_art(full_low_tide("M9"), "M9", "", "")}</g>' + play,
                           'Resuming: the colour returns to the album art as a play symbol grows and fades') + body[i1:]
    body = body.replace('stroke: #262626; stroke-width: 6"', f'stroke: {FB_TRACK}; stroke-width: 6"', 1).replace('color: #9A9A9A', 'color: #BDBDBD')
    return body, kfs


def fb_error():
    body, kfs = error_badge()
    i0 = body.index('<svg'); i1 = body.index('</svg>') + len('</svg>')
    badge = body[body.index('<g style="transform-origin: 233px 165px; opacity: 0; animation: wxbadge'):i1 - len('</svg>')]
    dy = HERO[1] - 165
    badge = badge.replace('233px 165px', f'{HERO[0]}px {HERO[1]}px').replace(
        '<g style="transform-origin', f'<g transform="translate(0 {dy})"><g style="transform-origin', 1).replace(
        f'{HERO[0]}px {HERO[1]}px', '233px 165px') + '</g>'
    body = body[:i0] + svg(f'<g style="opacity: 0.7">{fb_art(full_low_tide("M10"), "M10", "", "")}</g>' + badge,
                           'Ember offline: a badge over the album art with a crossed-out cloud; the track details shown are stale') + body[i1:]
    body = body.replace('stroke: #262626; stroke-width: 6"', f'stroke: {FB_TRACK}; stroke-width: 6"', 1).replace('color: #9A9A9A', 'color: #BDBDBD')
    return body, kfs


NOCOVER = dict(title='Untitled demo', artist='Home recording', album='', source='Plex', pos=62, dur=160, volume=62,
               accent='#FFB547')


def no_cover():
    RC, LR = (233, 200), 52
    label = (f'<circle cx="{RC[0]}" cy="{RC[1]}" r="{LR}" style="fill: #D8D2C4"></circle>'
             f'<circle cx="{RC[0]}" cy="{RC[1]}" r="{LR - 6}" style="fill: none; stroke: #FFB547; stroke-width: 2; opacity: 0.8"></circle>'
             f'<g transform="translate({RC[0] - 4} {RC[1] - 22})"><ellipse cx="0" cy="34" rx="9" ry="7" transform="rotate(-20 0 34)" style="fill: #4A4540"></ellipse>'
             '<rect x="6" y="2" width="4" height="32" rx="2" style="fill: #4A4540"></rect>'
             '<path d="M 8 2 Q 22 6 20 20 Q 18 12 8 12 Z" style="fill: #4A4540"></path></g>')
    body, kfs = vinyl(NOCOVER, label_svg=label, with_backdrop=False)
    return body.replace('<circle cx="263" cy="170" r="4" style="fill: #FFFFFF; opacity: 0.5"></circle>', ''), kfs


HERO2 = (233, 168)


def art_disc(art, uid, inner_style='', outer_style='', vignette=True):
    vig = (f'<radialGradient id="avig{uid}" gradientUnits="userSpaceOnUse" cx="233" cy="233" r="{ART_R}">'
           '<stop offset="0.78" stop-color="#000000" stop-opacity="0"></stop><stop offset="1" stop-color="#000000" stop-opacity="0.35"></stop></radialGradient>')
    scrim = (f'<linearGradient id="ascrim{uid}" gradientUnits="userSpaceOnUse" x1="0" y1="230" x2="0" y2="400">'
             '<stop offset="0" stop-color="#000000" stop-opacity="0"></stop><stop offset="1" stop-color="#000000" stop-opacity="0.75"></stop></linearGradient>')
    return (f'<defs><clipPath id="adisc{uid}"><circle cx="233" cy="233" r="{ART_R}"></circle></clipPath>{vig}{scrim}</defs>'
            f'<g clip-path="url(#adisc{uid})"><g style="transform-origin: 233px 233px;{outer_style}"><g style="transform-origin: 233px 233px;{inner_style}">{art}</g></g>'
            f'<rect x="0" y="0" width="466" height="466" style="fill: url(#ascrim{uid})"></rect>'
            + (f'<rect x="0" y="0" width="466" height="466" style="fill: url(#avig{uid})"></rect>' if vignette else '') + '</g>')


def photo(uid, blue=False, grey=False):
    av = avatar_blue(22, 22, 22, uid) if blue else avatar(22, 22, 22, uid)
    av = av.replace('<circle cx="22" cy="22" r="25" style="fill: #000000"></circle>', '')
    style = ' filter: saturate(0) brightness(0.75);' if grey else ''
    return f'<svg viewBox="0 0 44 44" width="44" height="44" role="img" aria-label="Artist photo" style="display: block;{style}">{av}</svg>'


def art_texts(t, time_html, uid, extra_style='', time_colour='#D6D6D6', dim=None, time_dim=None, blue=False, grey=False):
    d = f' opacity: {dim};' if dim else ''
    td = f' opacity: {time_dim};' if time_dim else ''
    return (f'<div style="position: absolute; left: 0; top: 232px; width: 466px; display: flex; flex-direction: column; align-items: center;{extra_style}">'
            f'<div style="margin-bottom: 6px;{d}">{photo(uid, blue, grey)}</div>'
            f'<div style="font-size: 30px; line-height: 36px; color: #F4F4F2; white-space: nowrap;{d}">{t["title"]}</div>'
            f'<div style="font-size: 24px; line-height: 28px; color: #BDBDBD; white-space: nowrap; margin-top: 2px;{d}">{t["artist"]}</div>'
            f'<div style="margin-top: 6px; font-size: 24px; line-height: 28px; color: {time_colour}; white-space: nowrap;{td}">{time_html}</div></div>')


def edge_now_playing(name, mode):
    t = TRACK
    acc = t['accent']
    paused, dimmed = mode == 'paused', mode == 'dimmed'
    ring_svg, head, L, theta = ring(FRAC, '#6A6A6A' if paused else acc, sweep=(mode == 'playing'),
                                    arc_opacity=0.35 if dimmed else 1.0, head_opacity=0.85 if dimmed else 1.0)
    kfs = []
    art = full_low_tide(name)
    if mode == 'playing':
        kfs += sweep_kf(L, theta) + [ART_IN, KEN_BURNS]
        art_svg = art_disc(art, name, ' animation: wxkenburns 30s ease-in-out infinite alternate;', ' animation: wxartin 6s linear infinite;')
    elif paused:
        art_svg = f'<g style="filter: saturate(0.2) brightness(0.55)">{art_disc(art, name)}</g>'
    else:
        kfs += ['@keyframes wxburn{0%,100%{transform:translate(6px,-5px) scale(1.04)}50%{transform:translate(-2px,3px) scale(1.04)}}']
        art_svg = f'<g style="opacity: 0.35">{art_disc(art, name, " animation: wxburn 40s ease-in-out infinite;")}</g>'
    layers = [svg(art_svg, f'Album art for {t["album"]} filling a disc inside the edge ring: a dusk sky with a pale sun over teal waves')]
    if paused:
        layers.append(svg(f'<circle cx="{HERO2[0]}" cy="{HERO2[1]}" r="52" style="fill: #000000; opacity: 0.55"></circle>'
                          f'<rect x="{HERO2[0] - 19}" y="{HERO2[1] - 23}" width="13" height="46" rx="4" style="fill: #F4F4F2"></rect>'
                          f'<rect x="{HERO2[0] + 6}" y="{HERO2[1] - 23}" width="13" height="46" rx="4" style="fill: #F4F4F2"></rect>', 'Paused'))
    layers.append(svg(ring_svg, f'Progress {mmss(t["pos"])} of {mmss(t["dur"])} on the edge ring'))
    if mode == 'playing':
        time_html, kf = counter('wxtime', time_labels(0, t['pos']), 24, 28, '#D6D6D6', 'right')
        kfs.append(kf)
        texts = art_texts(t, f'{time_html} / {mmss(t["dur"])}', name, ' animation: wxartin 6s linear infinite;')
    elif paused:
        texts = art_texts(t, 'Paused', name, grey=True)
    else:
        texts = art_texts(t, f'{mmss(t["pos"])} / {mmss(t["dur"])}', name, dim=0.5, time_dim=0.75)
    if paused or dimmed:
        kfs.append('@keyframes wxfadein{0%{opacity:0;animation-timing-function:' + EASE + '}22%,100%{opacity:1}}')
        return f'<div style="position: absolute; inset: 0; animation: wxfadein 6s linear infinite">\n' + '\n'.join(layers) + '\n' + texts + '\n</div>', kfs
    return '\n'.join(layers) + '\n' + texts, kfs


def edge_volume():
    t = TRACK
    acc = t['accent']
    cx, cy = HERO2
    vals, times = [56, 58, 60, 62], [6, 10, 14, 18]
    back = 18 + round(1500 / 60)
    ring_svg, head, L, theta = ring(FRAC, acc, sweep=False)
    def lerp(c1, c2, f):
        a, b = E.hex2rgb(c1), E.hex2rgb(c2)
        return E.rgb2hex([a[i] + (b[i] - a[i]) * f for i in range(3)])
    ov = math.degrees(0.6 / RING_R)
    groups = {}
    for k in range(vals[-1]):
        a0, a1 = 3.6 * k, 3.6 * (k + 1) + (ov if k < vals[-1] - 1 else 0)
        (x1, y1), (x2, y2) = polar(a0, RING_R), polar(a1, RING_R)
        stage = 0 if k < vals[0] else 1 + (k - vals[0]) // 2
        groups.setdefault(stage, []).append(f'<path d="M {x1} {y1} A {RING_R} {RING_R} 0 0 1 {x2} {y2}" style="fill: none; stroke: {lerp("#FFFFFF", acc, (k + 0.5) / 100)}; stroke-width: {RING_W}"></path>')
    kfs = [f'@keyframes wxprogout{{0%,2%{{opacity:1}}6%,{back}%{{opacity:0}}{back + 5}%,100%{{opacity:1}}}}',
           f'@keyframes wxvolin{{0%,4%{{opacity:0}}8%,{back}%{{opacity:1}}{back + 5}%,100%{{opacity:0}}}}',
           f'@keyframes wxartdim{{0%,4%{{opacity:1}}8%,{back}%{{opacity:0.4}}{back + 5}%,100%{{opacity:1}}}}']
    stages = ''
    for s, paths in sorted(groups.items()):
        if s == 0:
            stages += ''.join(paths)
        else:
            tm = times[s]
            kfs.append(f'@keyframes wxvstage{s}{{0%,{tm}%{{opacity:0;animation-timing-function:step-end}}{tm + 0.01}%,100%{{opacity:1}}}}')
            stages += f'<g style="opacity: 0; animation: wxvstage{s} 6s linear infinite">' + ''.join(paths) + '</g>'
    start_cap = f'<circle cx="{polar(0, RING_R)[0]}" cy="{polar(0, RING_R)[1]}" r="{RING_W / 2}" style="fill: #FFFFFF"></circle>'
    heads = ''
    for k, (v, tm) in enumerate(zip(vals, times)):
        x, y = polar(3.6 * v, RING_R)
        nxt = times[k + 1] if k + 1 < len(times) else 100
        kfs.append(f'@keyframes wxvhead{k}{{0%,{tm}%{{opacity:0;animation-timing-function:step-end}}{tm + 0.01}%{{opacity:1;animation-timing-function:step-end}}{nxt}%,100%{{opacity:{1 if k == len(vals) - 1 else 0}}}}}')
        heads += (f'<g style="opacity: 0; animation: wxvhead{k} 6s linear infinite">{head_dot(x, y, acc, f"v{k}")}'
                  + (f'<circle cx="{x}" cy="{y}" r="9" style="fill: none; stroke: #FFFFFF; stroke-width: 2.5; transform-origin: {x}px {y}px; '
                     f'animation: wxtick{k} 6s linear infinite"></circle>' if k else '') + '</g>')
        if k:
            kfs.append(f'@keyframes wxtick{k}{{0%,{tm}%{{opacity:0;transform:scale(0.7)}}{tm + 0.5}%{{opacity:1;transform:scale(1)}}{tm + 5}%,100%{{opacity:0;transform:scale(2.2)}}}}')
    vol_svg = (f'<circle cx="{C}" cy="{C}" r="{RING_R}" style="fill: none; stroke: #262626; stroke-width: {RING_W}"></circle>' + start_cap + stages + heads)
    num, kf = steps_counter('wxvolnum', [str(v) for v in vals], times, 96, 84, '#F4F4F2')
    kfs.append(kf)
    speaker = (f'<g transform="translate({cx - 20} {cy - 82})"><rect x="0" y="8" width="9" height="14" rx="2" style="fill: #F4F4F2"></rect>'
               '<path d="M 9 8 L 20 0 V 30 L 9 22 Z" style="fill: #F4F4F2"></path>'
               '<path d="M 26 9 Q 31 15 26 21" style="fill: none; stroke: #F4F4F2; stroke-width: 3; stroke-linecap: round"></path>'
               '<path d="M 31 4 Q 39 15 31 26" style="fill: none; stroke: #F4F4F2; stroke-width: 3; stroke-linecap: round"></path></g>')
    body = (svg(f'<g style="animation: wxartdim 6s linear infinite">{art_disc(full_low_tide("M7"), "M7")}</g>', 'Album art, dimmed while the volume is shown') + '\n'
            + svg(f'<g style="animation: wxprogout 6s linear infinite">{ring_svg}</g>'
                  f'<g style="opacity: 0; animation: wxvolin 6s linear infinite">{vol_svg}{speaker}</g>',
                  f'Volume mode on the edge ring: rising to {t["volume"]} a step per detent, then back to the progress ring') + '\n'
            + f'<div style="position: absolute; left: 0; top: {cy - 46}px; width: 466px; text-align: center; letter-spacing: -2px; opacity: 0; animation: wxvolin 6s linear infinite">{num}</div>\n'
            + art_texts(t, f'{mmss(t["pos"])} / {mmss(t["dur"])}', 'M7'))
    return body, kfs


def edge_next_track():
    a, b = TRACK, TRACK2
    cx, cy = HERO2
    fo, fn = a['pos'] / a['dur'], b['pos'] / b['dur']
    Lo, Ln, tho, thn = CIRC * fo, CIRC * fn, 360 * fo, 360 * fn
    hx, hy = polar(thn, RING_R)
    push = 'cubic-bezier(0.65,0,0.35,1)'
    kfs = ['@keyframes wxfbout{0%,4%{transform:translateX(0px);animation-timing-function:' + push + '}20%,100%{transform:translateX(-466px)}}',
           '@keyframes wxfbin{0%,4%{transform:translateX(466px);animation-timing-function:' + push + '}20%,100%{transform:translateX(0px)}}',
           '@keyframes wxfadeout{0%{opacity:1}8%,100%{opacity:0}}',
           '@keyframes wxfadein2{0%,10%{opacity:0}20%,100%{opacity:1}}',
           f'@keyframes wxunwind{{0%{{stroke-dasharray:{Lo:.1f} {CIRC:.1f};stroke:{a["accent"]};animation-timing-function:cubic-bezier(0.6,0,0.4,1)}}'
           f'8%{{stroke-dasharray:0 {CIRC:.1f};stroke:{a["accent"]}}}12%{{stroke:{b["accent"]}}}20%,100%{{stroke-dasharray:{Ln:.1f} {CIRC:.1f};stroke:{b["accent"]}}}}}',
           f'@keyframes wxheadun{{0%{{transform:rotate({tho - thn:.1f}deg);animation-timing-function:cubic-bezier(0.6,0,0.4,1)}}8%{{transform:rotate({-thn:.1f}deg)}}20%,100%{{transform:rotate(0deg)}}}}',
           '@keyframes wxskip{0%,3%{opacity:0;transform:scale(0.8)}8%{opacity:1;transform:scale(1.06)}12%{opacity:1;transform:scale(1)}17%,100%{opacity:0;transform:scale(1.1)}}']
    head = (f'<g style="transform-origin: {C}px {C}px; animation: wxheadun 6s linear infinite">'
            f'<g style="animation: wxfadeout 6s linear infinite">{head_dot(hx, hy, a["accent"], "o")}</g>'
            f'<g style="opacity: 0; animation: wxfadein2 6s linear infinite">{head_dot(hx, hy, b["accent"], "n")}</g></g>')
    ring_svg = (f'<circle cx="{C}" cy="{C}" r="{RING_R}" style="fill: none; stroke: #262626; stroke-width: {RING_W}"></circle>'
                f'<circle cx="{C}" cy="{C}" r="{RING_R}" transform="rotate(-90 {C} {C})" style="fill: none; stroke: {b["accent"]}; stroke-width: {RING_W}; '
                f'stroke-linecap: round; stroke-dasharray: {Ln:.1f} {CIRC:.1f}; animation: wxunwind 6s linear infinite"></circle>' + head)
    clip = '<clipPath id="fbclip{0}"><rect x="0" y="0" width="466" height="466"></rect></clipPath>'
    shadow = ('<defs><linearGradient id="fbshadow" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#000000" stop-opacity="0"></stop>'
              '<stop offset="1" stop-color="#000000" stop-opacity="0.45"></stop></linearGradient></defs>'
              '<rect x="-12" y="0" width="12" height="466" style="fill: url(#fbshadow)"></rect>')
    layers = (f'<defs>{clip.format("o")}{clip.format("n")}</defs>'
              f'<g style="animation: wxfbout 6s linear infinite"><g clip-path="url(#fbclipo)">{full_low_tide("M8o")}</g></g>'
              f'<g style="transform: translateX(466px); animation: wxfbin 6s linear infinite">{shadow}<g clip-path="url(#fbclipn)">{full_paper_moons("M8n")}</g></g>')
    skip = (f'<g style="transform-origin: {cx}px {cy}px; opacity: 0; animation: wxskip 6s linear infinite">'
            f'<circle cx="{cx}" cy="{cy}" r="46" style="fill: #000000; opacity: 0.55"></circle>'
            f'<path d="M {cx - 26} {cy - 16} L {cx - 4} {cy} L {cx - 26} {cy + 16} Z M {cx - 4} {cy - 16} L {cx + 18} {cy} L {cx - 4} {cy + 16} Z" '
            'style="fill: #F4F4F2; stroke: #F4F4F2; stroke-width: 3; stroke-linejoin: round"></path>'
            f'<rect x="{cx + 19}" y="{cy - 17}" width="6" height="34" rx="2" style="fill: #F4F4F2"></rect></g>')
    body = (svg(art_disc(layers, 'M8') + skip, f'Skipping to the next track: {a["album"]} pushes out, {b["album"]} by {b["artist"]} pushes in') + '\n'
            + svg(ring_svg, f'The edge ring unwinds and re-tints to {mmss(b["pos"])} of {mmss(b["dur"])}') + '\n'
            + art_texts(a, f'{mmss(a["pos"])} / {mmss(a["dur"])}', 'M8o', ' animation: wxfadeout 6s linear infinite;') + '\n'
            + art_texts(b, f'{mmss(b["pos"])} / {mmss(b["dur"])}', 'M8n', ' opacity: 0; animation: wxfadein2 6s linear infinite;', blue=True))
    return body, kfs


def edge_resume():
    t = TRACK
    acc = t['accent']
    cx, cy = HERO2
    ring_svg, head, L, theta = ring(FRAC, acc, sweep=False)
    ring_svg = ring_svg.replace(f'stroke: {acc}; stroke-width: {RING_W};', f'stroke: {acc}; stroke-width: {RING_W}; animation: wxretint 6s linear infinite;', 1)
    kfs = ['@keyframes wxcolour{0%,4%{filter:saturate(0.2) brightness(0.55)}18%,100%{filter:saturate(1) brightness(1)}}',
           '@keyframes wxheadcol{0%,4%{filter:saturate(0)}18%,100%{filter:saturate(1)}}',
           f'@keyframes wxretint{{0%,6%{{stroke:#6A6A6A}}18%,100%{{stroke:{acc}}}}}',
           '@keyframes wxplay{0%{opacity:0;transform:scale(0.4);animation-timing-function:' + EASE + '}6%{opacity:1;transform:scale(1.12)}12%{opacity:1;transform:scale(1)}22%,100%{opacity:0;transform:scale(1.05)}}',
           '@keyframes wxfadeout{0%,4%{opacity:1}10%,100%{opacity:0}}',
           '@keyframes wxfadein2{0%,10%{opacity:0}18%,100%{opacity:1}}']
    play = (f'<g style="transform-origin: {cx}px {cy}px; opacity: 0; animation: wxplay 6s linear infinite">'
            f'<circle cx="{cx}" cy="{cy}" r="52" style="fill: #000000; opacity: 0.55"></circle>'
            f'<path d="M {cx - 14} {cy - 24} L {cx + 24} {cy} L {cx - 14} {cy + 24} Z" style="fill: #F4F4F2; stroke: #F4F4F2; stroke-width: 4; stroke-linejoin: round"></path></g>')
    time_html = ('<span style="position: relative; display: inline-block; width: 200px; height: 28px">'
                 '<span style="position: absolute; inset: 0; text-align: center; animation: wxfadeout 6s linear infinite">Paused</span>'
                 f'<span style="position: absolute; inset: 0; text-align: center; opacity: 0; animation: wxfadein2 6s linear infinite">{mmss(t["pos"])} / {mmss(t["dur"])}</span></span>')
    body = (svg(f'<g style="animation: wxcolour 6s linear infinite">{art_disc(full_low_tide("M9"), "M9")}</g>' + play,
                'Resuming: the colour returns to the album art as a play symbol grows and fades') + '\n'
            + svg(ring_svg.replace('<g style="opacity: 1.0; transform-origin', '<g style="opacity: 1.0; animation: wxheadcol 6s linear infinite; transform-origin', 1),
                  f'Progress {mmss(t["pos"])} of {mmss(t["dur"])}, the ring re-tinting from grey') + '\n'
            + art_texts(t, time_html, 'M9'))
    return body, kfs


def edge_error():
    t = TRACK
    cx, cy = HERO2[0], HERO2[1] - 4
    ring_svg, head, L, theta = ring(FRAC, '#5A5A5A', sweep=False)
    kfs = ['@keyframes wxbadge{0%,3%{opacity:0;transform:scale(0.9);animation-timing-function:' + EASE + '}16%,100%{opacity:1;transform:scale(1)}}']
    cloud = 'M -20 8 H 18 A 8 8 0 0 0 16 -7 A 12 12 0 0 0 -7 -8 A 11 11 0 0 0 -20 8 Z'
    badge = (f'<g style="transform-origin: {cx}px {cy}px; opacity: 0; animation: wxbadge 6s linear infinite">'
             f'<rect x="{cx - 66}" y="{cy - 66}" width="132" height="132" rx="26" style="fill: #0A0A0A; opacity: 0.86; stroke: #3A3A3A; stroke-width: 2"></rect>'
             f'<g transform="translate({cx} {cy - 30})"><path d="{cloud}" style="fill: #9AA4B0"></path>'
             '<line x1="-18" y1="-18" x2="18" y2="18" style="stroke: #0A0A0A; stroke-width: 9; stroke-linecap: round"></line>'
             '<line x1="-18" y1="-18" x2="18" y2="18" style="stroke: #FF5050; stroke-width: 4; stroke-linecap: round"></line></g>'
             f'<text x="{cx}" y="{cy + 14}" text-anchor="middle" style="font-size: 24px; fill: #F4F4F2">Ember</text>'
             f'<text x="{cx}" y="{cy + 42}" text-anchor="middle" style="font-size: 24px; fill: #F4F4F2">offline</text></g>')
    body = (svg(f'<g style="opacity: 0.7">{art_disc(full_low_tide("M10"), "M10")}</g>' + badge,
                'Ember offline: a badge over the album art with a crossed-out cloud; the track details shown are stale') + '\n'
            + svg(ring_svg, 'Edge ring greyed while the data is stale') + '\n'
            + art_texts(t, f'{mmss(t["pos"])} / {mmss(t["dur"])}', 'M10', time_colour='#6A6A6A', grey=True))
    return body, kfs

FACES = {
    'M1': ('Music, playing', lambda: edge_now_playing('M1', 'playing')),
    'M2': ('Music, paused', lambda: edge_now_playing('M2', 'paused')),
    'M3': ('Music, nothing playing', nothing_playing),
    'M4': ('Music, dimmed', lambda: edge_now_playing('M4', 'dimmed')),
    'M6': ('Music, type first', type_first),
    'M7': ('Music, volume', edge_volume),
    'M8': ('Music, next track', lambda: __import__('opt').m8_body()),
    'M9': ('Music, resume tap', edge_resume),
    'M10': ('Music, Ember offline', edge_error),
    'M11': ('Music, BLE remote', lambda: ble_face(False)),
    'M12': ('Music, BLE pairing', lambda: ble_face(True)),
    'M13': ('Bot, listening', bot_listening),
    'M14': ('Bot, new track', bot_new_track),
    'M15': ('Music, no cover', no_cover),
}
BOT_FACES = {'M13', 'M14'}

if __name__ == '__main__':
    for name, (title, fn) in FACES.items():
        body, kfs = fn()
        html = page(title, kfs, body)
        if name == 'M15':
            html = __import__('opt3').vinyl_optimize(html, 'M15')
        if name in BOT_FACES:
            html = html[:html.index('<script type="text/x-dc"')] + BOT_SCRIPT + '\n</body>\n</html>\n'
        open(os.path.join(PROJ, name + '.dc.html'), 'w').write(html)
        print(name, title)

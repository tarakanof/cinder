import re


def sub_once(s, a, b):
    assert s.count(a) == 1, (a[:90], s.count(a))
    return s.replace(a, b)


def add_kf(s, kfs):
    return sub_once(s, '\n</style>\n</helmet>', '\n' + '\n'.join(kfs) + '\n</style>\n</helmet>')


def vinyl_optimize(s, name='M15'):
    start = s.index('<g style="transform-origin: 233px 200px; animation: wxspin 12s linear infinite">')
    end = s.index('<circle cx="233" cy="200" r="4" style="fill: #000000; stroke: #333333; stroke-width: 1.5"></circle>', start)
    inner = s[start:end]
    inner = inner[inner.index('>') + 1:inner.rindex('</g>')]
    frames = []
    kfs8 = []
    for k in range(8):
        lab = inner if name == 'M15' else inner.replace('M5', f'M5f{k}')
        frames.append(f'<g transform="rotate({45 * k} 233 200)" style="opacity: {1 if k == 0 else 0}; animation: wxlabel{k} 2s linear infinite">{lab}</g>')
        a, b = round(12.5 * k, 2), round(12.5 * (k + 1), 2)
        kfs8.append(f'@keyframes wxlabel{k}{{0%{{opacity:{1 if k == 0 else 0};animation-timing-function:step-end}}'
                    + (f'{a}%{{opacity:1;animation-timing-function:step-end}}' if k else '') + f'{b}%,100%{{opacity:0}}}}')
    s = s[:start] + ''.join(frames) + s[end:]
    s = add_kf(s, kfs8)
    s = re.sub(r'(aria-label="A black vinyl record spinning slowly[^"]*)"',
               r'\1; device-optimized: 1 redraw area (the 104 px label, 8 pre-rendered frames at 4 fps) plus the ring head and time once a second"', s, count=1)
    return s



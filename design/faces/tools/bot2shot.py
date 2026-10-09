import os, sys, math, io, pathlib
import numpy as np
from PIL import Image, ImageDraw
from playwright.sync_api import sync_playwright
import shoot, bot2
OUT = sys.argv[2] if len(sys.argv) > 2 else 'bot2-check.png'
SHOT_T = tuple(int(x) for x in os.environ.get('B2T', '300,2000,5000').split(','))
NAMES = sys.argv[1].split(',') if len(sys.argv) > 1 and sys.argv[1] else list(bot2.FACES)
LABEL_BOX = (56, 380, 410, 430)
TEXT_AREA = (bot2.TEXT_BOX[0], bot2.TEXT_BOX[1], bot2.TEXT_BOX[2], bot2.ICON_BOX[3])

BX, BY, BG, _ = bot2.BADGE
yy, xx = np.mgrid[0:466, 0:466]
RAD = np.hypot(xx - 233, yy - 233)
ANNULUS = (RAD >= 184) & (RAD <= 210)
HIDE = '<style>[style*="b2sprite"],[style*="b2text"],[style*="b2icon"]{display:none !important}</style>'


def build(name, mood=None, eyes_only=False):
    tag = f'{name}-{mood or "def"}{"-eyes" if eyes_only else ""}'
    out = shoot.build(os.path.join(bot2.E.PROJ, name + '.dc.html'), os.path.join(shoot.PAGES, f'b2-{tag}.html'), bot2.preview_vals(name, mood))
    assert '{{' not in open(out).read(), name
    if eyes_only:
        h = open(out).read().replace('</head>', HIDE + '</head>'); open(out, 'w').write(h)
    return out


def regions_changed(a, b, REG):
    d = np.abs(a.astype(int) - b.astype(int)).sum(axis=2) > 24
    hit, stray = set(), 0
    if (d & (ANNULUS | (np.hypot(xx - BX, yy - BY) <= BG + 2) | ((yy < 40) | (yy > 430)))).any():
        hit.add('outline')
    rest = d & ~ANNULUS & ~(np.hypot(xx - BX, yy - BY) <= BG + 2)
    for k, (x0, y0, x1, y1) in REG.items():
        m = np.zeros_like(rest); m[y0:y1 + 1, x0:x1 + 1] = True
        if (rest & m).any():
            hit.add(k)
        rest &= ~m
    return hit, int(rest.sum())


shots, report = {}, {}
with sync_playwright() as p:
    b = p.chromium.launch(args=['--disable-threaded-animation']); pg = b.new_page(viewport={'width': 466, 'height': 466})
    def at(t):
        pg.evaluate(f'() => document.getAnimations().forEach(a => {{ a.pause(); a.currentTime = {t}; }})')
        pg.evaluate('() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))')
        pg.wait_for_timeout(40)
        return np.array(Image.open(io.BytesIO(pg.screenshot())).convert('RGB'))
    def load(path):
        pg.goto(pathlib.Path(path).as_uri()); pg.evaluate('document.fonts.ready'); pg.wait_for_timeout(150)
    for n in NAMES:
        REG = {'sprite': bot2.BOX, 'eyes': bot2.EYE_BOXES[n], 'text': TEXT_AREA, 'label': LABEL_BOX}
        hop = bot2.script(bot2.FACES[n][2] or 'working').get('hop')
        load(build(n))
        for t in SHOT_T:
            shots[(n, t)] = Image.fromarray(at(t))
        base = at(5800)
        frames = [at(k * 1000 / 15) for k in range(0, 90)]
        worst, strays, per = 0, 0, []
        for k in range(0, len(frames)):
            hit, stray = regions_changed(frames[k - 1], frames[k], REG)
            if hop is not None and hop <= k / 15 <= hop + 1.0 + 1 / 15:
                hit = {'HOP(full outline)', 'sprite'} & ({'sprite'} if 'sprite' in hit else set()) | {'HOP(full outline)'}
                stray = 0
            per.append((k, sorted(hit))); strays += stray
            worst = max(worst, len(hit - {'outline'}) + (1 if 'outline' in hit else 0))
        dm = np.abs(at(4550).astype(int) - base.astype(int)).sum(axis=2) > 24
        if bot2.FACES[n][2] is None:
            keep = np.zeros_like(dm)
            for x0, y0, x1, y1 in (bot2.BOX, TEXT_AREA, LABEL_BOX):
                keep[y0:y1 + 1, x0:x1 + 1] = True
            dm &= keep & ~ANNULUS
        ext = {}
        for mood in bot2.FACES[n][5]:
            load(build(n, mood, eyes_only=True))
            ys0, ys1, xs0, xs1 = 999, 0, 999, 0
            for k in range(0, 90):
                fr = at(k * 1000 / 15)
                m = (fr.min(axis=2) > 180) & (RAD < 184) & ~(np.hypot(xx - BX, yy - BY) <= BG + 2)
                if m.any():
                    ys, xs = np.nonzero(m)
                    ys0, ys1, xs0, xs1 = min(ys0, ys.min()), max(ys1, ys.max()), min(xs0, xs.min()), max(xs1, xs.max())
            ext[mood] = (int(xs0), int(ys0), int(xs1), int(ys1))
        report[n] = dict(max_areas=worst, stray_px=strays, outline_frames=len([k for k, h in per if 'outline' in h]), hop_frames=len([k for k, h in per if 'HOP(full outline)' in h]),
                         text_frames=[k for k, h in per if 'text' in h], sprite_and_text=[k for k, h in per if 'text' in h and 'sprite' in h],
                         label_frames=[k for k, h in per if 'label' in h], eye_extent=ext,
                         gap_to_sprite=min(e[1] for e in ext.values()) - bot2.BOX[3], gap_to_text=bot2.TEXT_BOX[1] - max(e[3] for e in ext.values()),
                         returned_diff_px=int(dm.sum()))
        print(n, report[n], flush=True)
    b.close()
COLS = 3 if len(SHOT_T) == 1 else len(SHOT_T)
ROWS = math.ceil(len(NAMES) / 3) if len(SHOT_T) == 1 else len(NAMES)
img = Image.new('RGB', (466 * COLS, 466 * ROWS), '#222'); d = ImageDraw.Draw(img)
for i, n in enumerate(NAMES):
    for j, t in enumerate(SHOT_T):
        im = shots[(n, t)].copy()
        if t == 2000:
            dd = ImageDraw.Draw(im); dd.rectangle(bot2.BOX, outline='#FF00FF'); dd.rectangle(bot2.EYE_BOXES[n], outline='#00FFFF'); dd.rectangle(bot2.TEXT_BOX, outline='#FFFF00')
        cx, cy = ((i % 3) * 466, (i // 3) * 466) if len(SHOT_T) == 1 else (j * 466, i * 466)
        img.paste(im, (cx, cy)); d.text((cx + 6, cy + 4), f'{n} {t}ms', fill='white')
os.makedirs(shoot.BUILD, exist_ok=True)
img.save(os.path.join(shoot.BUILD, OUT)); print('saved', OUT, img.size)

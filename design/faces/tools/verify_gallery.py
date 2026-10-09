import base64, io, json, os, pathlib, sys
import numpy as np
from PIL import Image
from playwright.sync_api import sync_playwright
import shoot, bot2

FACES = shoot.FACES
GALLERY = os.path.join(FACES, 'gallery')
COMPARE = ['AT4', 'AR2', 'AD3', 'AW4', 'AS2', 'AA4', 'AN1', 'M8', 'M15', 'B2-Snow', 'B2-AfterRain', 'B2-RainWorking']
LIVE = ['BotNow', 'BotNext']


def font_css():
    parts = []
    for f, r in (('montserrat-500.woff2', ''), ('montserrat-latin-ext-500.woff2', ';unicode-range:U+0100-02AF,U+2000-206F,U+2190-22FF')):
        b = base64.b64encode(open(os.path.join(shoot.FONTS, f), 'rb').read()).decode()
        parts.append('@font-face{font-family:Montserrat;font-style:normal;font-weight:500;src:url(data:font/woff2;base64,%s) format("woff2")%s}' % (b, r))
    return ''.join(parts)


def shot(pg):
    return np.array(Image.open(io.BytesIO(pg.screenshot(clip={'x': 0, 'y': 0, 'width': 466, 'height': 466}))).convert('RGB'))


YY, XX = np.mgrid[0:466, 0:466]
INSIDE = np.hypot(XX - 233, YY - 233) <= 231


def diff(a, b):
    return float(((np.abs(a.astype(int) - b.astype(int)).sum(axis=2) > 48) & INSIDE).mean())


def main():
    names = [f[:-len('.dc.html')] for f in json.load(open(os.path.join(shoot.PROJ, 'canvas.json')))['order']]
    css = font_css()
    report, bad = {}, []
    with sync_playwright() as p:
        b = p.chromium.launch(args=['--disable-threaded-animation'])
        ctx = b.new_context(viewport={'width': 466, 'height': 466})
        ctx.route('https://fonts.googleapis.com/**', lambda r: r.fulfill(status=200, content_type='text/css', body=css))
        ctx.add_init_script('window.__botSeed = 777;')
        pg = ctx.new_page()
        errors = []
        pg.on('pageerror', lambda e: errors.append(str(e)))
        pg.on('console', lambda m: errors.append(m.text) if m.type == 'error' else None)
        for n in names:
            errors.clear()
            pg.goto(pathlib.Path(os.path.join(GALLERY, n + '.html')).as_uri() + '?embed=1')
            pg.evaluate('document.fonts.ready')
            pg.wait_for_timeout(200)
            r = {}
            if n in LIVE:
                a = shot(pg); pg.wait_for_timeout(2000); c = shot(pg)
                r['moved_2s'] = round(diff(a, c), 4)
                ok = r['moved_2s'] > 0
            else:
                anims = pg.evaluate('document.getAnimations().length')
                shoot.seek(pg, 500); a = shot(pg)
                shoot.seek(pg, 3000); c = shot(pg)
                r['animations'] = anims
                r['changes_500_3000'] = round(diff(a, c), 4)
                ok = anims > 0
                if n in COMPARE:
                    vals = bot2.preview_vals(n) if n.startswith('B2-') else shoot.defaults(open(os.path.join(shoot.PROJ, n + '.dc.html')).read())
                    page = shoot.build(os.path.join(shoot.PROJ, n + '.dc.html'), os.path.join(shoot.PAGES, n + '.html'), vals)
                    hp = ctx.new_page()
                    hp.goto(pathlib.Path(page).as_uri()); hp.evaluate('document.fonts.ready'); hp.wait_for_timeout(200)
                    shoot.seek(hp, 3000)
                    r['vs_canvas_3000'] = round(diff(c, shot(hp)), 4)
                    hp.close()
                    ok = ok and r['vs_canvas_3000'] < 0.002
            r['nonblack'] = round(float((c.max(axis=2) > 40).mean()), 3)
            r['errors'] = errors[:3]
            ok = ok and r['nonblack'] > 0.01 and not errors
            report[n] = r
            if not ok:
                bad.append(n)
            print(n, 'ok' if ok else 'CHECK', r, flush=True)
        b.close()
    print('faces:', len(names), 'need a look:', bad or 'none')


if __name__ == '__main__':
    main()

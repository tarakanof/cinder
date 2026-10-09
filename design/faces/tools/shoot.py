import os, re, sys, json, tempfile, pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
FACES = os.path.normpath(os.path.join(HERE, '..'))
PROJ = os.path.join(FACES, 'canvas')
BUILD = os.path.join(FACES, 'build')
PAGES = tempfile.mkdtemp(prefix='cinder-faces-')
FONTS = os.path.join(HERE, 'fonts')
TIMES = [0, 400, 900, 3000]
FONT_CSS = ''.join(
    '@font-face{font-family:Montserrat;font-weight:500;src:url(%s) format("woff2")%s}' % (pathlib.Path(os.path.join(FONTS, f)).as_uri(), r)
    for f, r in (('montserrat-500.woff2', ''), ('montserrat-latin-ext-500.woff2', ';unicode-range:U+0100-02AF,U+2000-206F,U+2190-22FF')))


def defaults(s):
    m = re.search(r"data-props='(.*?)'", s, re.S)
    props = json.loads(m.group(1)) if m else {}
    return {k: v.get('default') for k, v in props.items() if not k.startswith('$')}


def build(src, out, vals=None):
    s = open(src).read()
    style = re.search(r'<helmet>.*?<style>(.*?)</style>\s*</helmet>', s, re.S).group(1)
    face = re.search(r'</helmet>\s*(.*?)\s*</x-dc>', s, re.S).group(1)
    for k, v in (vals or {'mood': '#2EE85E'}).items():
        face = face.replace('{{' + k + '}}', str(v))
    html = ('<!doctype html><html><head><meta charset="utf-8">'
            f'<style>{FONT_CSS}</style>'
            f'<style>{style}</style></head><body style="margin:0;background:#333">'
            f'<div id="face" style="width:466px;height:466px">{face}</div></body></html>')
    open(out, 'w').write(html)
    return out


def seek(pg, t):
    pg.evaluate(f'() => document.getAnimations().forEach(a => {{ a.pause(); a.currentTime = {t}; }})')
    pg.evaluate('() => new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)))')
    pg.wait_for_timeout(60)


def run(names, times):
    from playwright.sync_api import sync_playwright
    out_dir = os.path.join(BUILD, 'frames')
    os.makedirs(out_dir, exist_ok=True)
    with sync_playwright() as p:
        b = p.chromium.launch(args=['--disable-threaded-animation'])
        pg = b.new_page(viewport={'width': 466, 'height': 466})
        for n in names:
            page = build(os.path.join(PROJ, n + '.dc.html'), os.path.join(PAGES, n + '.html'))
            pg.goto(pathlib.Path(page).as_uri())
            pg.evaluate('document.fonts.ready')
            pg.wait_for_timeout(150)
            for t in times:
                seek(pg, t)
                pg.screenshot(path=os.path.join(out_dir, f'{n}-{t}.png'))
        b.close()
    print('frames in', out_dir)


if __name__ == '__main__':
    names = sys.argv[1].split(',')
    times = [int(x) for x in sys.argv[2].split(',')] if len(sys.argv) > 2 else TIMES
    run(names, times)

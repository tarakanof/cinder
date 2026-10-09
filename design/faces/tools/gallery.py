import html, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
FACES = os.path.normpath(os.path.join(HERE, '..'))
CANVAS = os.path.join(FACES, 'canvas')
GALLERY = os.path.join(FACES, 'gallery')

ROWS = [('Bot today vs proposed', 'Live simulations of the firmware bot and the proposed bot. Pick a mood on the face page.',
         lambda n: n in ('BotNow', 'BotNext')),
        ('Bot weather moments v2', 'Brief weather interruptions on the agent-status bot, looped in a 6 s cycle.',
         lambda n: n.startswith('B2-')),
        ('Weather faces', 'Temperature, rain, rain forecast, wind, sun, air and next days. Arrival plays in the first 1.3 s of each 6 s loop.',
         lambda n: n.startswith('A')),
        ('Music faces', 'Now playing, alternatives, feedback, BLE remote and bot music moments.',
         lambda n: n.startswith('M'))]

SHIM = r'''
let __schedule = () => {};
class DCLogic {
  constructor(props) { this.props = props; this.state = {}; }
  setState(u) {
    const next = typeof u === 'function' ? u(this.state, this.props) : u;
    this.state = Object.assign({}, this.state, next);
    __schedule();
  }
}
'''

BOOT = r'''
(function () {
  const root = document.getElementById('face');
  const q = new URLSearchParams(location.search);
  if (q.get('embed') === '1') document.body.classList.add('embed');
  const coerce = (d, v) => d.editor === 'boolean' ? (v === true || v === 'true' || v === '1') : d.editor === 'range' ? Number(v) : v;
  const props = {};
  for (const [k, d] of Object.entries(SCHEMA)) {
    if (k[0] === '$') continue;
    props[k] = q.has(k) ? coerce(d, q.get(k)) : d.default;
  }
  const comp = typeof Component === 'function' ? new Component(Object.assign({}, props)) : null;
  let last = null, pending = false;
  const render = () => {
    pending = false;
    const vals = Object.assign({}, props, comp && comp.renderVals ? (comp.renderVals() || {}) : {});
    const out = TEMPLATE.replace(/\{\{(\w+)\}\}/g, (m, k) => (vals[k] == null ? '' : String(vals[k])));
    if (out !== last) { root.innerHTML = out; last = out; }
  };
  __schedule = () => { if (!pending) { pending = true; Promise.resolve().then(render); } };
  render();
  if (comp && comp.componentDidMount) comp.componentDidMount();
  window.addEventListener('pagehide', () => { if (comp && comp.componentWillUnmount) comp.componentWillUnmount(); });
  const box = document.getElementById('controls');
  const set = (k, v) => {
    props[k] = v;
    if (comp) comp.props = Object.assign({}, props);
    const u = new URL(location.href); u.searchParams.set(k, String(v)); history.replaceState(null, '', u);
    render();
  };
  for (const [k, d] of Object.entries(SCHEMA)) {
    if (k[0] === '$') continue;
    const label = document.createElement('label');
    label.textContent = k + ' ';
    let input;
    if (d.editor === 'enum') {
      input = document.createElement('select');
      for (const o of d.options) { const op = document.createElement('option'); op.value = o; op.textContent = o; input.appendChild(op); }
      input.value = props[k];
      input.onchange = () => set(k, input.value);
    } else if (d.editor === 'boolean') {
      input = document.createElement('input'); input.type = 'checkbox'; input.checked = !!props[k];
      input.onchange = () => set(k, input.checked);
    } else if (d.editor === 'range') {
      input = document.createElement('input'); input.type = 'range';
      input.min = d.min; input.max = d.max; input.step = d.step || 1; input.value = props[k];
      const out = document.createElement('output'); out.textContent = props[k] + (d.unit || '');
      input.oninput = () => { out.textContent = input.value + (d.unit || ''); set(k, Number(input.value)); };
      label.appendChild(input); label.appendChild(out); box.appendChild(label); continue;
    } else continue;
    label.appendChild(input); box.appendChild(label);
  }
  if (!box.children.length) box.textContent = 'No props on this face.';
})();
'''

PAGE_CSS = ('body{margin:0;padding:24px;background:#151515;color:#d8d8d8;font:14px/1.4 system-ui,sans-serif}'
            'body.embed{padding:0;background:#000}body.embed .chrome{display:none}'
            '#face{width:466px;height:466px}'
            '.chrome{margin-top:16px;max-width:466px}.chrome h1{font-size:16px;margin:0 0 4px}.chrome p{margin:0 0 12px;color:#999}'
            '#controls{display:flex;flex-wrap:wrap;gap:8px 16px}#controls label{display:flex;align-items:center;gap:6px}'
            'a{color:#8ab4f8}')


def parse(name):
    s = open(os.path.join(CANVAS, name + '.dc.html'), encoding='utf-8').read()
    helmet = re.search(r'<helmet>(.*?)</helmet>', s, re.S).group(1)
    face = re.search(r'</helmet>\s*(.*?)\s*</x-dc>', s, re.S).group(1)
    m = re.search(r"<script type=\"text/x-dc\" data-dc-script data-props='(.*?)'>(.*?)</script>", s, re.S)
    return dict(title=re.search(r'<title>(.*?)</title>', s, re.S).group(1),
                links=re.findall(r'<link [^>]*>', helmet), styles=re.findall(r'<style>(.*?)</style>', helmet, re.S),
                face=face, schema=json.loads(m.group(1)) if m else {}, code=m.group(2).strip() if m else '')


def face_page(name, board_title):
    f = parse(name)
    js = lambda v: json.dumps(v, ensure_ascii=False).replace('</', '<\\/')
    code = f['code'] if 'class Component' in f['code'] else ''
    return ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>{f["title"]}</title>\n' + '\n'.join(f['links']) + '\n'
            + ''.join(f'<style>{st}</style>\n' for st in f['styles'])
            + f'<style>{PAGE_CSS}</style>\n</head>\n<body>\n<div id="face"></div>\n'
            f'<div class="chrome"><h1>{html.escape(board_title)}</h1><p>{name}.dc.html · <a href="index.html">all faces</a></p>'
            '<div id="controls"></div></div>\n'
            f'<script>\nconst SCHEMA = {js(f["schema"])};\nconst TEMPLATE = {js(f["face"])};\n{SHIM}\n{code}\n{BOOT}\n</script>\n</body>\n</html>\n')


def index_page(rows):
    cards = []
    for title, blurb, names in rows:
        items = ''.join(
            f'<figure><div class="frame"><iframe src="{n}.html?embed=1" title="{html.escape(t)}" width="466" height="466" loading="lazy" scrolling="no"></iframe></div>'
            f'<figcaption><a href="{n}.html">{html.escape(t)}</a><span>{n}</span></figcaption></figure>' for n, t in names)
        cards.append(f'<section><h2>{html.escape(title)}</h2><p>{html.escape(blurb)}</p><div class="grid">{items}</div></section>')
    css = ('body{margin:0;padding:24px 32px;background:#101010;color:#d8d8d8;font:14px/1.4 system-ui,sans-serif}'
           'h1{font-size:22px;margin:0 0 6px}h2{font-size:18px;margin:32px 0 4px}p{margin:0 0 12px;color:#999}'
           '.grid{display:flex;flex-wrap:wrap;gap:20px}figure{margin:0;width:280px}'
           '.frame{width:280px;height:280px;overflow:hidden;border-radius:50%;background:#000}'
           '.frame iframe{border:0;width:466px;height:466px;transform:scale(0.6009);transform-origin:0 0;pointer-events:none}'
           'figcaption{margin-top:6px;display:flex;flex-direction:column}figcaption span{color:#777;font-size:12px}a{color:#8ab4f8;text-decoration:none}')
    return ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            f'<title>Cinder faces</title>\n<style>{css}</style>\n</head>\n<body>\n<h1>Cinder faces</h1>\n'
            '<p>Design gallery rendered from design/faces/canvas. Click a title to open a face full size with its props.</p>\n'
            + '\n'.join(cards) + '\n</body>\n</html>\n')


def main():
    canvas = json.load(open(os.path.join(CANVAS, 'canvas.json'), encoding='utf-8'))
    names = [f[:-len('.dc.html')] for f in canvas['order']]
    titles = {k[:-len('.dc.html')]: v.get('title', k) for k, v in canvas['boards'].items()}
    os.makedirs(GALLERY, exist_ok=True)
    for n in names:
        open(os.path.join(GALLERY, n + '.html'), 'w', encoding='utf-8').write(face_page(n, titles.get(n, n)))
    rows, used = [], set()
    for title, blurb, pick in ROWS:
        sel = [(n, titles.get(n, n)) for n in names if n not in used and pick(n)]
        used |= {n for n, _ in sel}
        rows.append((title, blurb, sel))
    assert used == set(names), set(names) - used
    open(os.path.join(GALLERY, 'index.html'), 'w', encoding='utf-8').write(index_page(rows))
    print('gallery:', len(names), 'faces')


if __name__ == '__main__':
    main()

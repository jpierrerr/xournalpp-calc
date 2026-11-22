import gzip, re, sys, json
src, dst, meta = sys.argv[1:4]
doc = gzip.open(src, 'rt', encoding='utf-8').read()
pages = re.findall(r'<page [^>]*>.*?</page>', doc, re.S)
out = []; info = []
for pi, pg in enumerate(pages):
    head = re.match(r'<page [^>]*>', pg).group(0)
    items = re.findall(r'<(?:text|stroke) [^>]*>.*?</(?:text|stroke)>', pg, re.S)
    def key(it):
        m = re.search(r' y="([\d.]+)"', it); x = re.search(r' x="([\d.]+)"', it)
        return (float(m.group(1)) if m else 1e9, float(x.group(1)) if x else 0)
    texts = sorted([i for i in items if i.startswith('<text')], key=key)
    strokes = [i for i in items if i.startswith('<stroke')]
    for k, it in enumerate(texts + strokes):
        out.append(head + '\n<background type="solid" color="#ffffffff" style="plain"/>\n<layer>\n' + it + '\n</layer>\n</page>')
        kind = 'text' if it.startswith('<text') else 'stroke'
        calc = 'calc="true"' in it
        label = re.sub(r'<[^>]+>', '', it)[:40] if kind == 'text' else 'stroke'
        info.append({'page': pi + 1, 'kind': kind, 'calc': calc, 'label': label})
hdr = doc[:doc.index('<page ')]
gzip.open(dst, 'wt', encoding='utf-8').write(hdr + '\n'.join(out) + '\n</xournal>\n')
json.dump(info, open(meta, 'w'))
print(len(info))

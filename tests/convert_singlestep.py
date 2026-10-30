import gzip, json, sys, os
meta = json.load(open('meta2.json'))['opcodes']
os.makedirs('txt', exist_ok=True)
LIMIT = 1500
order = ['ax','bx','cx','dx','cs','ss','ds','es','sp','bp','si','di','ip','flags']
def mask_for(name):
    if '.' in name:
        op, r = name.split('.')
        e = meta[op]['reg'][r]
    else:
        e = meta.get(name, {})
        if 'reg' in e:
            e = {}
    return e.get('flags-mask', 0xFFFF)
for fn in sorted(os.listdir('gz')):
    name = fn[:-8]
    path = 'gz/' + fn
    if os.path.getsize(path) < 1000:
        continue
    data = json.load(gzip.open(path))
    out = [f"M {mask_for(name)}"]
    for t in data[:LIMIT]:
        ini = t['initial']['regs']
        fin = dict(ini); fin.update(t['final']['regs'])
        out.append("T " + t['name'].replace(' ', '_'))
        out.append("I " + ' '.join(str(ini[k]) for k in order))
        ir = t['initial']['ram']
        out.append("IR %d " % len(ir) + ' '.join(f"{a} {v}" for a, v in ir))
        out.append("F " + ' '.join(str(fin[k]) for k in order))
        fr = t['final']['ram']
        out.append("FR %d " % len(fr) + ' '.join(f"{a} {v}" for a, v in fr))
    open(f'txt/{name}.txt', 'w').write('\n'.join(out) + '\n')
print(len(os.listdir('txt')))

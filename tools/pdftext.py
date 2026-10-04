import re, zlib, sys, collections

def pages(path):
    raw=open(path,'rb').read()
    out=[]
    for m in re.finditer(rb'stream\r?\n(.*?)\r?\nendstream', raw, re.S):
        try: d=zlib.decompress(m.group(1))
        except Exception: continue
        if b'TJ' in d or b'Tj' in d: out.append(d.decode('latin-1'))
    return out

def runs(content):
    """(x, y, text) for each text-showing operator."""
    res=[]; x=y=0.0
    for tok in re.finditer(r'([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+Tm'
                           r'|\[(.*?)\]\s*TJ'
                           r'|\((.*?)\)\s*Tj', content, re.S):
        if tok.group(1) is not None:
            x=float(tok.group(5)); y=float(tok.group(6))
        elif tok.group(7) is not None:
            g=[int(h,16) for h in re.findall(r'<([0-9A-Fa-f]{4})>', tok.group(7))]
            if g: res.append((x,y,''.join(chr(c+29) for c in g)))
        elif tok.group(8) is not None:
            res.append((x,y,tok.group(8)))
    return res

def lines(path):
    """[(page, y, [(x,text)...])] grouped into visual lines."""
    allout=[]
    for pno, c in enumerate(pages(path), 1):
        by=collections.defaultdict(list)
        for x,y,t in runs(c):
            by[round(y,1)].append((x,t))
        for y in sorted(by):
            allout.append((pno, y, sorted(by[y])))
    return allout

if __name__=='__main__':
    for pth in sys.argv[1:]:
        print('########', pth)
        for pno,y,cells in lines(pth):
            txt=' | '.join(t for _,t in cells)
            print(f'p{pno}\t{y:8.1f}\t{txt}')

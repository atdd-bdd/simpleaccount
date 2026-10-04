"""Compare SimpleAccount's figures with a Quicken Itemized Categories PDF.

    python tools/compare.py testdata/<report>.pdf <book.QIF> <from> <to>
    python tools/compare.py --all        # every report in testdata, auto-matched

Reads the PDF's own heading for its date range, so a mislabelled filename
cannot silently compare the wrong period.
"""
import re, sys, os, collections
HERE=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pdftext import lines

AMT=re.compile(r'^(.*?)(-?[\d,]+\.\d\d)$')
RANGE=re.compile(r'(\d{1,2})/(\d{1,2})/(\d{4})\s*through\s*(\d{1,2})/(\d{1,2})/(\d{4})')

def report_sections(pdf):
    """The INCOME / EXPENSES / TRANSFERS totals and the stated date range."""
    sec=None; tot={}; rng=None; kind=None
    for pno,y,cells in lines(pdf):
        j=''.join(t for _,t in cells).rstrip()
        if rng is None:
            m=RANGE.search(j)
            if m:
                rng=((int(m.group(3)),int(m.group(1)),int(m.group(2))),
                     (int(m.group(6)),int(m.group(4)),int(m.group(5))))
        if kind is None and 'Itemized Categories' in j: kind='ItemizedCategories'
        m=AMT.match(j)
        if not m: continue
        lab=m.group(1); amt=float(m.group(2).replace(',',''))
        ind=len(lab)-len(lab.lstrip(' ')); name=lab.strip().upper()
        if ind==0 and name in ('INCOME','EXPENSES','TRANSFERS','OVERALL TOTAL'):
            tot[name]=amt
    return kind, rng, tot

def load_qif(path):
    raw=open(path,'rb').read()
    try: txt=raw.decode('utf-8')
    except UnicodeDecodeError: txt=raw.decode('cp1252')
    sec=None; cur={}; recs=[]
    for line in txt.split('\n'):
        line=line.rstrip('\r\n')
        if line.startswith('!'): sec=line.rstrip(); continue
        if not line: continue
        if line=='^':
            if cur: recs.append((sec,cur))
            cur={}; continue
        cur.setdefault(line[0], []).append(line[1:])
    return recs

def num(x):
    try: return round(float(x.replace(',','').strip()),2)
    except Exception: return 0.0

def qdate(r):
    d=(r.get('D',[''])[0] or '')
    m=re.match(r"\s*(\d{1,2})\s*/\s*(\d{1,2})\s*'\s*(\d{1,4})\s*$", d)
    return (2000+int(m.group(3)), int(m.group(1)), int(m.group(2))) if m else None

BASE={'!Type:Bank','!Type:CCard','!Type:Cash','!Type:Oth A','!Type:Oth L','!Type:Tax',
      '!Type:Invoice','!Type:Bill'}

def ours(recs, lo, hi):
    cats={}
    for s,r in recs:
        if s=='!Type:Cat' and 'N' in r:
            cats[r['N'][0].strip()]='I' if 'I' in r else ('E' if 'E' in r else '?')
    inc=exp=0.0; per=collections.Counter()
    for s,r in recs:
        d=qdate(r)
        if not d or not (lo<=d<=hi): continue
        if s not in BASE and s!='!Type:Invst': continue
        L=[v.strip() for v in r.get('L',[])]; l0=L[0] if L else ''
        # round one leaves the securities side of an investment account out
        if s=='!Type:Invst' and (not l0 or l0.startswith('[')): continue
        S=[v.strip() for v in r.get('S',[])]; D=[num(v) for v in r.get('$',[])]
        pairs=[]
        if S and D:
            for c,a in zip(S,D):
                if not c.startswith('['): pairs.append((c.split('/')[0],a))
        elif l0.startswith('['): pass                      # a transfer
        elif l0 and l0!='--Split--': pairs.append((l0.split('/')[0], num(r.get('T',['0'])[0])))
        else: pairs.append(('', num(r.get('T',['0'])[0])))
        for c,a in pairs:
            k=cats.get(c,'?') if c else '?'
            # an uncategorised amount is placed by ITS OWN sign, not the total's
            if k=='I': inc+=a
            elif k=='E': exp+=a
            elif a>0: inc+=a
            else: exp+=a
            per[c or '<uncategorised>']+=a
    return round(inc,2), round(exp,2), per

def compare(pdf, qif):
    kind, rng, tot = report_sections(pdf)
    base=os.path.basename(pdf)
    if not rng:
        print('  SKIP %-46s no date range in the heading' % base[:46]); return
    if kind != 'ItemizedCategories':
        print('  SKIP %-46s not an Itemized Categories report' % base[:46]); return
    if 'INCOME' not in tot and 'EXPENSES' not in tot:
        print('  SKIP %-46s no INCOME/EXPENSES sections' % base[:46]); return
    lo,hi=rng
    inc,exp,_=ours(load_qif(qif), lo, hi)
    qi=tot.get('INCOME',0.0); qe=tot.get('EXPENSES',0.0)
    di=round(inc-qi,2); de=round(exp-qe,2)
    ok='OK  ' if (abs(di)<0.005 and abs(de)<0.005) else 'DIFF'
    print('  %s %-46s %d-%02d-%02d..%d-%02d-%02d' % (ok, os.path.basename(pdf)[:46], *lo, *hi))
    print('        income   ours %14.2f  quicken %14.2f  diff %12.2f' % (inc,qi,di))
    print('        expenses ours %14.2f  quicken %14.2f  diff %12.2f' % (exp,qe,de))

if __name__=='__main__':
    if len(sys.argv)>=3 and sys.argv[1]!='--all':
        compare(sys.argv[1], sys.argv[2])
    else:
        td=os.path.join(HERE,'..','testdata')
        pdfs=sorted(f for f in os.listdir(td) if f.lower().endswith('.pdf'))
        for f in pdfs:
            full=os.path.join(td,f)
            if os.path.getsize(full)==0:
                print('  SKIP %-46s empty file' % f[:46]); continue
            qif='kenpugh.QIF' if 'kenpugh' in f.lower() and 'killeen' not in f.lower() else 'pughkilleen.QIF'
            compare(full, os.path.join(td,qif))

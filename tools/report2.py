import re, sys
sys.path.insert(0, r'C:\Users\user\AppData\Local\Temp\claude\C--Users-user-source-repos-simpleaccount\5643df22-a651-46e9-a87c-b0de568e2394\scratchpad')
from pdftext import lines

TRAIL=re.compile(r'^(.*?)(-?[\d,]+\.\d\d)$')

def parse(path):
    meta=[]; out=[]
    for pno,y,cells in lines(path):
        if not cells: continue
        joined=''.join(t for _,t in cells)
        if pno==1 and y<110:
            meta.append(joined); continue
        m=TRAIL.match(joined.rstrip())
        if m:
            label, amt = m.group(1), m.group(2)
            indent=len(label)-len(label.lstrip(' '))
            out.append((indent, label.strip(), amt))
    return meta, out

for p in sys.argv[1:]:
    meta,rows=parse(p)
    print('########', p)
    print('   ', meta[0] if meta else '', '|', meta[1] if len(meta)>1 else '')
    print('    figure lines:', len(rows))
    tot=[r for r in rows if r[1].upper().startswith('TOTAL') or r[1].upper()=='OVERALL TOTAL'
         or r[1].upper() in ('INCOME','EXPENSES')]
    print('    --- section and TOTAL lines ---')
    for ind,lab,amt in tot[:40]:
        print(f'      i{ind:<3}{lab:<52}{amt:>16}')

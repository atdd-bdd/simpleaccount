"""Does each balance difference equal exactly the transactions that postdate
the account's own last entry in the QIF? If so the import is complete and the
gap is only that the QIF was exported before the QFX downloads."""
import sys, re, datetime, subprocess, os
sys.path.insert(0, 'tools')
from xlsx import rows

SUMMARY = re.compile(
    r'^(?:BALANCE\s+\d{1,2}/\d{1,2}/\d{4}'
    r'|TOTAL\s+(?:INFLOWS|OUTFLOWS)'
    r'|NET\s+TOTAL'
    r'|\d{1,2}/\d{1,2}/\d{4}\s*-\s*\d{1,2}/\d{1,2}/\d{4})\b', re.I)


def iso(s):
    return (datetime.date(1899, 12, 30) +
            datetime.timedelta(days=int(float(s)))).isoformat()


def cents(t):
    t = (t or '').strip().replace(',', '').replace('$', '')
    if not t:
        return None
    neg = t.startswith('(') and t.endswith(')')
    if neg:
        t = t[1:-1]
    try:
        v = round(float(t) * 100)
    except ValueError:
        return None
    return -v if neg else v


PAIRS = [
    ('testdata/joint - crown - wachovia 3105.xlsx',
     'Assets:Joint - Crown - Wachovia 3105', -3382884),
    ('testdata/ken Checking - Wavhovia 3113.xlsx',
     'Assets:Ken Checking - Wachovia 3112', -34735),
    ('testdata/wells fargo ken visa 1333 (3932).xlsx',
     'Liabilities:Wells Fargo Ken Visa 1333 (3932)', 157382),
    ('testdata/chase sapphire 4391 (7253).xlsx',
     'Liabilities:Chase Sapphire 4391 (7253)', 0),
]

print('%-40s %10s %7s %14s %14s %9s'
      % ('account', 'cutoff', 'after', 'tail sum', 'bal diff', 'agree'))
for f, acct, baldiff in PAIRS:
    hdr = None
    groups = []
    for _, c in rows(f):
        if hdr is None:
            if c and 'Date' in c and 'Amount' in c:
                hdr = {v: i for i, v in enumerate(c) if v}
            continue

        def g(k):
            i = hdr.get(k)
            return c[i] if i is not None and i < len(c) else ''

        d = g('Date').strip()
        amt = cents(g('Amount'))
        joined = ' '.join(x for x in c if x).strip()
        if groups and SUMMARY.match(joined):
            break
        if d.replace('.', '').isdigit():
            groups.append({'date': iso(d), 'amt': amt or 0})
        elif amt is not None and groups:
            groups[-1]['amt'] += amt

    out = subprocess.run([os.path.abspath('build/Release/sa_register.exe'),
                          os.path.abspath('testdata/pughkilleen.QIF'), acct],
                         capture_output=True, text=True).stdout
    ours = [p[0] for p in (l.split('\t') for l in out.splitlines()[1:]) if len(p) >= 6]
    cut = max(ours)
    tail = [x for x in groups if x['date'] > cut]
    s = sum(x['amt'] for x in tail)
    # Both figures are already in the account's own reading -- the xlsx Amount
    # column and the balances report agree on which way a card runs -- so they
    # compare directly.
    print('%-40s %10s %7d %14.2f %14.2f %9s'
          % (acct.split(':')[-1][:40], cut, len(tail), s / 100.0,
             baldiff / 100.0, 'yes' if s == baldiff else 'NO'))

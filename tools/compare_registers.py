import sys, re, datetime, collections, subprocess, os
sys.path.insert(0, 'tools')
from xlsx import rows

# The summary block a Quicken register export ends with: a date-range line,
# then BALANCE, TOTAL INFLOWS, TOTAL OUTFLOWS, NET TOTAL. Everything from the
# first of these on is not a transaction. A transaction called "Balance
# Adjustment" is not one of them, which is why the patterns are anchored.
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
     'Assets:Joint - Crown - Wachovia 3105'),
    ('testdata/ken Checking - Wavhovia 3113.xlsx',
     'Assets:Ken Checking - Wachovia 3112'),
    ('testdata/wells fargo ken visa 1333 (3932).xlsx',
     'Liabilities:Wells Fargo Ken Visa 1333 (3932)'),
    ('testdata/chase sapphire 4391 (7253).xlsx',
     'Liabilities:Chase Sapphire 4391 (7253)'),
    ('testdata/pk checking wavhcovia 2905.xlsx',
     'Assets:PK Checking Wachovia 2905'),
    ('testdata/cap one savings 0852.xlsx',
     'Assets:Cap One Savings 0852'),
]

print('%-40s %5s %5s %14s %14s %10s  %s'
      % ('register', 'Q', 'mine', 'quicken', 'mine', 'diff', 'unmatched'))
worst = 0
for f, acct in PAIRS:
    hdr = None
    groups = []
    stated = None
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
        # The date-range line is also the report's own subtitle near the top,
        # so the block only ends the register once transactions have started.
        if groups and SUMMARY.match(joined):
            stated = cents(joined.split()[-1])
            break
        if d.replace('.', '').isdigit():
            groups.append({'date': iso(d), 'amt': amt or 0,
                           'desc': g('Description')})
        elif amt is not None and groups:
            groups[-1]['amt'] += amt      # a split continuation line

    out = subprocess.run([os.path.abspath('build/Release/sa_register.exe'),
                          os.path.abspath('testdata/pughkilleen.QIF'), acct],
                         capture_output=True, text=True).stdout
    ours = [{'date': p[0], 'amt': int(p[4]), 'desc': p[2]}
            for p in (l.split('\t') for l in out.splitlines()[1:]) if len(p) >= 6]

    # The QIF and the xlsx were exported on different days, and each account's
    # feed was downloaded on its own day. Compare only as far as this account
    # actually goes in the QIF.
    cut = max(o['date'] for o in ours)
    q = [x for x in groups if x['date'] <= cut]
    sq = sum(x['amt'] for x in q)
    sm = sum(o['amt'] for o in ours)

    cq = collections.Counter((x['date'], x['amt']) for x in q)
    co = collections.Counter((o['date'], o['amt']) for o in ours)
    oq = sorted((cq - co).elements())
    rem = sorted((co - cq).elements())
    res = []
    for d, a in oq:
        # Quicken dates each side of a transfer in its own account; one shared
        # date here means the same movement can sit a few days apart.
        hit = next((x for x in rem if x[1] == a and
                    abs((datetime.date.fromisoformat(d) -
                         datetime.date.fromisoformat(x[0])).days) <= 7), None)
        if hit:
            rem.remove(hit)
        else:
            res.append((d, a))

    worst = max(worst, abs(sq - sm))
    print('%-40s %5d %5d %14.2f %14.2f %10.2f  Q %d / mine %d'
          % (os.path.basename(f)[:40], len(q), len(ours), sq / 100.0,
             sm / 100.0, (sq - sm) / 100.0, len(res), len(rem)))
    for d, a in (res + rem)[:6]:
        src = q if (d, a) in oq else ours
        m = next((r for r in src if (r['date'], r['amt']) == (d, a)), {})
        print('        %s %12.2f  %s' % (d, a / 100.0, (m.get('desc') or '')[:44]))

print()
print('largest register difference: %.2f' % (worst / 100.0))

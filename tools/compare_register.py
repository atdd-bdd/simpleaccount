"""Compare a register of ours with the register report Quicken exports as xlsx.

    python tools/compare_register.py <quicken.xlsx> <file.qif> [account path]

The account is taken from the report itself when not given. Dates in the report
are Excel serials; amounts are compared in cents so nothing turns on formatting.
"""
import datetime, os, re, subprocess, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xlsx import rows as xlsx_rows

HERE = os.path.dirname(os.path.abspath(__file__))
SA_REGISTER = os.path.join(HERE, '..', 'build', 'Release', 'sa_register.exe')

def excel_date(serial):
    return (datetime.date(1899, 12, 30) + datetime.timedelta(days=int(float(serial)))).isoformat()

def cents(text):
    t = (text or '').strip().replace(',', '').replace('$', '')
    if not t: return 0
    neg = t.startswith('(') and t.endswith(')')
    if neg: t = t[1:-1]
    value = round(float(t) * 100)
    return -value if neg else value

def read_quicken(path):
    """(account, [rows]) where a row is date, num, description, category, amount."""
    out = []
    account = None
    header = None
    for _, cells in xlsx_rows(path):
        if not cells: continue
        if header is None:
            if 'Date' in cells and 'Amount' in cells:
                header = {name: i for i, name in enumerate(cells) if name}
            continue
        def field(name):
            i = header.get(name)
            return cells[i] if i is not None and i < len(cells) else ''
        date_cell = field('Date')
        if not re.fullmatch(r'\d+(\.\d+)?', (date_cell or '').strip()):
            continue            # a BALANCE line or a total
        if account is None and field('Account'): account = field('Account')
        out.append({'date': excel_date(date_cell), 'num': field('Num'),
                    'desc': field('Description'), 'category': field('Category'),
                    'amount': cents(field('Amount'))})
    return account, out

def read_ours(qif, account):
    text = subprocess.run([SA_REGISTER, qif, account], capture_output=True, text=True)
    if text.returncode != 0:
        print(text.stderr.strip()); sys.exit(1)
    out = []
    for line in text.stdout.splitlines()[1:]:
        parts = line.split('\t')
        if len(parts) < 6: continue
        out.append({'date': parts[0], 'num': parts[1], 'payee': parts[2],
                    'category': parts[3], 'amount': int(parts[4]),
                    'balance': int(parts[5])})
    return out

def main():
    xls, qif = sys.argv[1], sys.argv[2]
    account_in_report, theirs = read_quicken(xls)
    account = sys.argv[3] if len(sys.argv) > 3 else 'Assets:' + (account_in_report or '')
    ours = read_ours(qif, account)

    print('report   %s' % os.path.basename(xls))
    print('account  %s  (ours: %s)' % (account_in_report, account))
    print('rows     quicken %d   ours %d   difference %+d' % (len(theirs), len(ours),
                                                              len(ours) - len(theirs)))
    tq = sum(r['amount'] for r in theirs)
    to = sum(r['amount'] for r in ours)
    print('sum      quicken %.2f   ours %.2f   difference %.2f'
          % (tq / 100.0, to / 100.0, (to - tq) / 100.0))

    # Where the two first disagree, by date and amount, which is what matters.
    key = lambda r: (r['date'], r['amount'])
    cq = collections.Counter(key(r) for r in theirs)
    co = collections.Counter(key(r) for r in ours)
    only_theirs = sorted((cq - co).elements())
    only_ours = sorted((co - cq).elements())
    print('only in quicken: %d    only in ours: %d' % (len(only_theirs), len(only_ours)))
    for label, rows_ in (('quicken', only_theirs), ('ours', only_ours)):
        for d, a in rows_[:12]:
            match = next((r for r in (theirs if label == 'quicken' else ours)
                          if key(r) == (d, a)), {})
            print('   only in %-7s %s %12.2f  %s' % (
                label, d, a / 100.0,
                (match.get('desc') or match.get('payee') or '')[:44]))
        if len(rows_) > 12: print('   ... and %d more' % (len(rows_) - 12))

if __name__ == '__main__':
    main()

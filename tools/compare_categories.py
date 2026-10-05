import re, subprocess, sys, os, collections

year = sys.argv[1]
pdf = 'testdata/pughkilleeen-%s-spending.pdf' % year

txt = subprocess.run([sys.executable, 'tools/pdftext.py', pdf],
                     capture_output=True, text=True).stdout

AMT = re.compile(r'-?[\d,]+\.\d\d')

# Lines that are a category subtotal: leading spaces, a name, then an amount,
# and nothing else on the line.
rows = []          # (indent, name, cents)
section = None
for line in txt.splitlines():
    if not line.startswith('p'):
        continue
    body = line.split('\t', 2)[-1]
    if body.startswith('INCOME'):
        section = 'INCOME'
        rows.append((-1, 'INCOME', body[len('INCOME'):]))
        continue
    if body.startswith('EXPENSES'):
        section = 'EXPENSES'
        rows.append((-1, 'EXPENSES', body[len('EXPENSES'):]))
        continue
    if body.startswith('TRANSFERS'):
        section = 'TRANSFERS'
        rows.append((-1, 'TRANSFERS', body[len('TRANSFERS'):]))
        continue
    if body.startswith('OVERALL'):
        section = None
        continue
    if section is None or not body.startswith('    '):
        continue
    indent = len(body) - len(body.lstrip(' '))
    rest = body.strip()
    # The amount is the last money-looking token, possibly after a " | ".
    rest = rest.replace(' | ', '')
    m = list(AMT.finditer(rest))
    if not m:
        continue
    last = m[-1]
    if last.end() != len(rest):
        continue
    name = rest[:last.start()].strip()
    if not name:
        continue
    rows.append((indent, name, last.group()))

def cents(t):
    return round(float(t.replace(',', '')) * 100)

# Build the full path from the indent nesting.
quicken = {}
stack = []
cur_section = None
for indent, name, amt in rows:
    if indent == -1:
        cur_section = name
        stack = []
        quicken[name] = cents(amt)
        continue
    while stack and stack[-1][0] >= indent:
        stack.pop()
    parent = stack[-1][1] if stack else cur_section
    path = parent + ':' + name
    stack.append((indent, path))
    quicken[path] = cents(amt)

out = subprocess.run([os.path.abspath('build/Release/sa_report.exe'),
                      os.path.abspath('testdata/pughkilleen.QIF'),
                      '%s-01-01' % year, '%s-12-31' % year, '--rows',
                      '--quicken'],
                     capture_output=True, text=True).stdout
mine = {}
for line in out.splitlines():
    m = re.match(r'^((?:Income|Expenses)\S?.*?)\s{2,}(-?[\d,]+\.\d\d)\s*(yes)?\s*$', line)
    if m:
        mine[m.group(1).strip()] = cents(m.group(2))

# Quicken writes expenses negative and names the root EXPENSES; mine are
# positive under Expenses. Normalise onto Quicken's naming and sign.
def norm(path):
    p = path.replace('Income', 'INCOME', 1) if path.startswith('Income') else path
    if path.startswith('Expenses'):
        p = 'EXPENSES' + path[len('Expenses'):]
    return p

# sa_report is asked for Quicken signs, so both sides already agree on which
# way an expense runs and nothing here has to negate a column.
norm_mine = {}
for k, v in mine.items():
    norm_mine[norm(k)] = v

print('%-46s %14s %14s %12s' % ('category', 'quicken', 'mine', 'diff'))
keys = sorted(set(quicken) | set(norm_mine))
shown = 0
for k in keys:
    q = quicken.get(k)
    m = norm_mine.get(k)
    if q == m:
        continue
    # A node Quicken writes as "Other X" is my X posted to directly; skip the
    # structural difference and report only real figure differences.
    print('%-46s %14s %14s %12s' % (
        k[:46],
        'n/a' if q is None else '%.2f' % (q / 100.0),
        'n/a' if m is None else '%.2f' % (m / 100.0),
        '' if q is None or m is None else '%.2f' % ((q - m) / 100.0)))
    shown += 1
if not shown:
    print('  every category agrees')
print()
for root in ('INCOME', 'EXPENSES', 'TRANSFERS'):
    if root in quicken:
        kids = [v for k, v in quicken.items()
                if k.startswith(root + ':') and k.count(':') == 1]
        print('%-10s quicken stated %14.2f   sum of its own children %14.2f   mine %14.2f'
              % (root, quicken[root] / 100.0, sum(kids) / 100.0,
                 norm_mine.get(root, 0) / 100.0))

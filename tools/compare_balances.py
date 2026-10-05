import re, subprocess, sys, os

PDF = 'testdata/pughkilleeen account balances 2025 after update.pdf'

out = subprocess.run([os.path.abspath('build/Release/sa_balances.exe'),
                      os.path.abspath('testdata/pughkilleen.QIF')],
                     capture_output=True, text=True).stdout
mine = {}
for line in out.splitlines():
    parts = line.split('\t')
    if len(parts) < 3:
        continue
    path, kind, amt = parts[0], parts[1], parts[2]
    try:
        mine[path] = (kind, int(amt))
    except ValueError:
        pass

# Match a report line against the account whose leaf name it starts with,
# longest first: "Cap One CD 4439" and "Cap One CD 4439 40,163.43" differ only
# by where the name stops, and a short name would otherwise swallow the digits
# of the amount.
leaves = sorted(((p.rsplit(':', 1)[-1], p) for p in mine),
                key=lambda kv: -len(kv[0]))

txt = subprocess.run([sys.executable, 'tools/pdftext.py', PDF],
                     capture_output=True, text=True).stdout
AMT = re.compile(r'-?[\d,]+\.\d\d$')

seen = {}
unresolved = []
for line in txt.splitlines():
    if not line.startswith('p'):
        continue
    body = line.split('\t', 2)[-1].replace(' | ', '').strip()
    if not body or body.upper().startswith(('TOTAL', 'OVERALL', 'ACCOUNT',
                                            'BALANCE', 'ASSETS', 'LIABILITIES',
                                            'NET WORTH')):
        continue
    for leaf, path in leaves:
        if not body.startswith(leaf):
            continue
        rest = body[len(leaf):].strip()
        m = AMT.match(rest)
        if not m:
            continue
        seen[path] = round(float(m.group().replace(',', '')) * 100)
        break
    else:
        # A long name is printed truncated, as "Joint - Crown - Wachovia...20,997.25".
        # The prefix before the ellipsis still identifies the account as long as
        # exactly one account's leaf name starts with it.
        m = re.match(r'^(.*?)\.\.\.(-?[\d,]+\.\d\d)$', body)
        if m:
            prefix, amt = m.group(1).strip(), m.group(2)
            hits = [p for leaf, p in leaves if leaf.startswith(prefix)]
            if len(hits) == 1:
                seen[hits[0]] = round(float(amt.replace(',', '')) * 100)
                continue
        if AMT.search(body):
            unresolved.append(body)

INVEST = {'Investment'}
ok = bad = 0
rows = []
for path, stated in sorted(seen.items()):
    kind, got = mine[path]
    # The report shows each account the way its own statement reads; so does
    # sa_balances.
    if stated == got:
        ok += 1
    else:
        bad += 1
        rows.append((path, kind, stated, got))

print('resolved %d report lines, %d unresolved' % (len(seen), len(unresolved)))
for b in unresolved[:6]:
    print('   unresolved: %s' % b[:70])
print()
print('exact: %d    differing: %d' % (ok, bad))
noninv = [r for r in rows if r[1] not in INVEST]
inv = [r for r in rows if r[1] in INVEST]
print()
print('non-investment accounts that differ (%d):' % len(noninv))
for path, kind, stated, got in noninv:
    print('   %-52s %-12s report %12.2f  mine %12.2f  diff %11.2f'
          % (path[:52], kind, stated / 100.0, got / 100.0, (stated - got) / 100.0))
if not noninv:
    print('   none')
print()
print('investment accounts that differ: %d (securities are not imported)' % len(inv))

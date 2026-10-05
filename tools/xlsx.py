"""Read an .xlsx worksheet into rows of strings, with no third-party library.

Quicken writes its tags with an x: prefix and a byte order mark, so a reader has
to allow for both.
"""
import re, zipfile

CELL = re.compile(r'<(?:\w+:)?c([^>/]*)(?:/>|>(.*?)</(?:\w+:)?c>)', re.S)
ROW = re.compile(r'<(?:\w+:)?row([^>]*)>(.*?)</(?:\w+:)?row>', re.S)
VAL = re.compile(r'<(?:\w+:)?v>(.*?)</(?:\w+:)?v>', re.S)
TXT = re.compile(r'<(?:\w+:)?t[^>]*>(.*?)</(?:\w+:)?t>', re.S)
SI = re.compile(r'<(?:\w+:)?si>(.*?)</(?:\w+:)?si>', re.S)

def _unescape(s):
    return (s.replace('&lt;', '<').replace('&gt;', '>').replace('&quot;', '"')
             .replace('&apos;', "'").replace('&amp;', '&'))

def _col(ref):
    letters = ''.join(ch for ch in ref if ch.isalpha())
    n = 0
    for ch in letters:
        n = n * 26 + (ord(ch) - ord('A') + 1)
    return n - 1

def rows(path, sheet='xl/worksheets/sheet1.xml'):
    """Yield (row_number, [cell text, ...]) with cells placed by their column."""
    z = zipfile.ZipFile(path)
    shared = []
    if 'xl/sharedStrings.xml' in z.namelist():
        xml = z.read('xl/sharedStrings.xml').decode('utf-8-sig', 'replace')
        for si in SI.findall(xml):
            shared.append(_unescape(''.join(TXT.findall(si))))
    data = z.read(sheet).decode('utf-8-sig', 'replace')
    out = []
    for attrs, body in ROW.findall(data):
        number = re.search(r'r="(\d+)"', attrs)
        cells = {}
        for cattrs, cbody in CELL.findall(body):
            ref = re.search(r'r="([A-Z]+\d+)"', cattrs)
            kind = re.search(r't="(\w+)"', cattrs)
            value = ''
            v = VAL.search(cbody or '')
            if v:
                value = _unescape(v.group(1))
                if kind and kind.group(1) == 's' and value != '':
                    value = shared[int(value)]
            elif kind and kind.group(1) == 'inlineStr':
                value = _unescape(''.join(TXT.findall(cbody or '')))
            if ref:
                cells[_col(ref.group(1))] = value
        width = (max(cells) + 1) if cells else 0
        out.append((int(number.group(1)) if number else 0,
                    [cells.get(i, '') for i in range(width)]))
    return out

if __name__ == '__main__':
    import sys
    for number, cells in rows(sys.argv[1])[:int(sys.argv[2]) if len(sys.argv) > 2 else 20]:
        print(number, cells)

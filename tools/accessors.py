"""Put the right accessor on every cell field in the glue: see finding 11.

A cell of a declared type is a struct in AlignThree 0.9.2 and after, holding
its text in `.value` for a `...String` and `.text` for a `...Typed`, so the
glue has to say which of the two it means. Which one is never a guess: the
generated struct declares it. A field of type `XTyped` reads through `.text`,
one of type `XString` through `.value`, and a field the generator left as
`std::string`, `bool` or `double` is the value itself and takes no accessor.

So this reads `generated/common/*.h`, works out the type of every cell
variable in a glue function from its declaration, and normalises each
`var.field` it can account for -- adding the accessor, correcting it, or
taking it off again. An expression whose root is not a known cell variable is
never touched, which is what keeps production objects out of it: `got.cleared`
is an enum, `blank_.payee` a std::string and `got.shares` a double, and a
first pass driven by the compiler's log put a `.value` on all three. A log
names the line but not which of its two operands is the cell, and in
`EXPECT_EQ(want.cleared, types::to_string(got.cleared))` the two sides belong
to different worlds.

Run:  python tools/accessors.py [<build log>] [<glue file> ...] [--apply]

With a log, only the lines it mentions are rewritten, which keeps the diff to
what is actually broken. Naming glue files runs over the whole of those files
instead, which is quicker when one file holds many sites: MSVC reports a
single template instantiation failure per translation unit and suppresses the
rest of a cascading line, so each log-driven round uncovers only the next site
in each file. Migrating this project took seven rounds that way, and naming
the last two files ended it.
"""
import glob
import io
import re
import sys

FIELD = re.compile(
    r'^    ([A-Za-z_][A-Za-z0-9_:<>, ]*?) +([a-z][A-Za-z0-9_]*) *(?:=[^;]*)?;$', re.M)
STRUCT = re.compile(r'^struct ([A-Za-z0-9_]+) \{', re.M)

DECL = re.compile(r'(?:const )?([A-Za-z0-9_]+(?:String|Typed))[&* ] *([a-z][A-Za-z0-9_]*)\b')
VEC = re.compile(r'std::vector<([A-Za-z0-9_]+(?:String|Typed))> *[&]? *([a-z][A-Za-z0-9_]*)\b')
FOR = re.compile(r'for \(const auto& ([a-z][A-Za-z0-9_]*) : ([a-z][A-Za-z0-9_]*)\)')
AUTO = re.compile(r'const auto&? ([a-z][A-Za-z0-9_]*) = ([a-z][A-Za-z0-9_]*)[\[.]')
FUNC = re.compile(r'^    [A-Za-z_]')

USE = re.compile(r'(?<![A-Za-z0-9_.])([a-z][A-Za-z0-9_]*)'
                 r'(\[[^]]*\]|\.front\(\)|\.back\(\))?'
                 r'\.([a-z][A-Za-z0-9_]*)(\.value|\.text)?')


def structs():
    """Every generated struct, as field name to the accessor it needs."""
    out = {}
    for path in glob.glob('generated/common/*.h'):
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for m in STRUCT.finditer(text):
            body = text[m.end():]
            body = body[:body.find('\n};')]
            fields = {}
            for d in FIELD.finditer(body):
                kind = d.group(1).strip()
                if kind.endswith('Typed'):
                    fields[d.group(2)] = '.text'
                elif kind.endswith('String'):
                    fields[d.group(2)] = '.value'
                else:
                    fields[d.group(2)] = ''
            out[m.group(1)] = fields
    return out


def sites(log_path):
    """Every glue line the compiler mentioned, error or instantiation note."""
    log = io.open(log_path, encoding='utf-8', errors='replace').read()
    found = set()
    for m in re.finditer(r'([A-Za-z0-9_]+_glue[.]h)[(](\d+)', log):
        found.add((m.group(1), int(m.group(2))))
    return found


def rewrite(path, known, only):
    name = path.replace('generated/', '').replace('generated' + chr(92), '')
    lines = io.open(path, encoding='utf-8', newline='').read().split('\n')
    # The compiler names the line a statement starts on, so a site carries
    # the rest of its statement with it.
    flagged = None
    if only is not None:
        flagged = set(n for nm, n in only if nm == name)
        for n in sorted(flagged):
            m = n
            while m < len(lines) and not lines[m - 1].rstrip().endswith((';', '{', '}')):
                m += 1
                flagged.add(m)
    out = []
    cells = {}      # variable -> struct it is
    element = {}    # vector variable -> struct of its elements
    changes = []
    for n, line in enumerate(lines, 1):
        if FUNC.match(line):
            cells, element = {}, {}
        for m in VEC.finditer(line):
            if m.group(1) in known:
                element[m.group(2)] = m.group(1)
                cells[m.group(2)] = m.group(1)
        for m in DECL.finditer(line):
            if m.group(1) in known:
                cells[m.group(2)] = m.group(1)
        for m in FOR.finditer(line):
            if m.group(2) in element:
                cells[m.group(1)] = element[m.group(2)]
        for m in AUTO.finditer(line):
            if m.group(2) in element:
                cells[m.group(1)] = element[m.group(2)]

        def repl(m):
            root, index, field, had = m.group(1), m.group(2) or '', m.group(3), m.group(4) or ''
            if root not in cells:
                return m.group(0)
            fields = known[cells[root]]
            if field not in fields:
                return m.group(0)
            rest = line[m.end():m.end() + 1]
            if not had and rest == '(':
                return m.group(0)        # a method of the struct, not a field
            return root + index + '.' + field + fields[field]

        new = line if flagged is not None and n not in flagged else USE.sub(repl, line)
        if new != line:
            changes.append((n, line, new))
        out.append(new)
    return '\n'.join(out), changes


def main():
    apply = '--apply' in sys.argv
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    logs = [a for a in args if not a.endswith('_glue.h')]
    # Naming glue files runs over the whole of them rather than the lines a log
    # happens to mention: cheaper than another round when a file has many
    # sites and the compiler reports one of them at a time.
    whole = [a for a in args if a.endswith('_glue.h')]
    only = sites(logs[0]) if logs else None
    known = structs()
    total = 0
    for path in sorted(glob.glob('generated/*_glue.h')):
        name = path.replace('generated/', '').replace('generated' + chr(92), '')
        if whole and name not in whole:
            continue
        text, changes = rewrite(path, known, None if name in whole else only)
        total += len(changes)
        for n, old, new in changes:
            print('%s:%d' % (path.replace('generated/', ''), n))
            print('  - ' + old.strip()[:110])
            print('  + ' + new.strip()[:110])
        if apply and changes:
            io.open(path, 'w', encoding='utf-8', newline='').write(text)
    print('lines rewritten: %d%s' % (total, '' if apply else '   (dry run)'))


if __name__ == '__main__':
    main()

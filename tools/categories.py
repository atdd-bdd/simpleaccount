"""The categories in a Quicken "Itemized Categories - All Dates" export.

Produces the list to be reviewed before the chart of accounts is settled: every
category Quicken reports, where it sits in the tree, its all-dates total, and
the last year it was used.

The last year is what makes the list decidable. A category used for three years
ending in 2009 is a different thing from one used last month, and only one of
them belongs in a picker. The totals come from the PDF because that is the
oracle the reports are checked against; the last-used year comes from the QIF,
because the PDF carries no dates per category.

    python tools/categories.py testdata/pughkillleen-allyears-category.pdf \
                               testdata/pughkilleen.QIF
"""
import collections
import re
import sys

import pdftext

# A trailing amount, with or without the " | " Quicken sometimes puts first.
AMOUNT = re.compile(r"\s*\|?\s*(-?[\d,]+\.\d\d)$")
SECTIONS = ("INCOME", "EXPENSES", "TRANSFERS")


def categories_in(path):
    """[(section, depth, name, total)] in the order the report lists them."""
    out = []
    section = ""
    for _page, _y, runs in pdftext.lines(path):
        text = "".join(t for _x, t in runs).rstrip()
        bare = text.strip()
        if not bare:
            continue
        if bare.startswith(("Page", "Account |", "Itemized Categories")) \
                or "through" in bare or re.match(r"^\d+/\d+/\d+", bare):
            continue

        # The heading carries the section total with no space before it, so
        # "INCOME3,742,439.40" is one token and has to be matched by prefix.
        for heading in SECTIONS:
            if bare.startswith(heading):
                section = heading
                bare = ""
                break
        if not bare:
            continue
        if bare.startswith(("TOTAL", "OVERALL")):
            continue

        found = AMOUNT.search(text)
        if found is None:
            continue
        name = text[: found.start()]
        depth = (len(name) - len(name.lstrip())) // 4
        name = name.strip()
        if not name or name.startswith("Total "):
            continue
        # An "Other X" row holds the transactions posted to X itself, where X
        # also contains other categories. Nobody picks the row -- they pick X --
        # but the money on it is real, and it is there so the rows under a
        # category add up to the category. See the Other-row rule in
        # Reports.spectable.
        own_postings = name.startswith("Other ") and depth > 0
        out.append((section, depth, name, found.group(1), own_postings))
    return out


def last_year_used(qif_path):
    """{category name: the last year it appears on a transaction}."""
    last = {}
    year = None
    for raw in open(qif_path, encoding="latin-1", errors="replace"):
        line = raw.rstrip("\r\n")
        if not line:
            continue
        code, rest = line[0], line[1:]
        if code == "D":
            parts = re.split(r"[/'\-]", rest.strip())
            if len(parts) >= 3 and parts[2].strip()[:4].isdigit():
                y = int(parts[2].strip()[:4])
                year = y + 2000 if y < 100 else y
        elif code in ("L", "S") and year is not None:
            # A transfer is written in brackets and is an account, not a
            # category; the category is whatever is left.
            name = rest.strip()
            if name.startswith("["):
                continue
            name = name.split("/")[0].strip()      # a class follows a slash
            if not name:
                continue
            for key in {name, name.split(":")[-1].strip()}:
                if key and (key not in last or year > last[key]):
                    last[key] = year
    return last


def main():
    if len(sys.argv) < 2:
        sys.stdout.write(__doc__)
        return 2
    rows = categories_in(sys.argv[1])
    last = last_year_used(sys.argv[2]) if len(sys.argv) > 2 else {}

    width = 46
    print("%-9s %-*s %16s  %s" % ("WHERE", width, "CATEGORY", "ALL DATES", "LAST USED"))
    print("-" * (9 + width + 28))
    section = None
    stale = 0
    real = 0
    for where, depth, name, total, own_postings in rows:
        if where != section:
            print()
            section = where
        seen = last.get(name, last.get(name.split(":")[-1].strip(), ""))
        if own_postings:
            print("%-9s %-*s %16s  %s"
                  % (where, width, "  " * depth + name, total,
                     "(posted to the parent itself)"))
            continue
        real += 1
        if isinstance(seen, int) and seen < 2023:
            stale += 1
        print("%-9s %-*s %16s  %s"
              % (where, width, "  " * depth + name, total, seen if seen else "?"))

    print()
    print("%d rows: %d categories to pick from, %d Other rows holding "
          "transactions posted to a parent" % (len(rows), real, len(rows) - real))
    print("%d of the categories were last used before 2023" % stale)
    return 0


if __name__ == "__main__":
    sys.exit(main())

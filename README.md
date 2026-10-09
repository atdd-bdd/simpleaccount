# SimpleAccount

A replacement for Quicken. C++17 / Qt 6, double entry, built specification first.

The specification comes first: twelve `.spectable`
files in `spec/`, each a set of worked examples of one area of behaviour. They
are the agreed description of what the program does, and the tests are generated
from them.

## Scope of the first round

**Spending reports.** What was spent and earned, by category and by payee, over
a period — and the migration and imports that have to be right for those figures
to be right. Net worth, account balances and anything about investment
performance are specified but not compared against Quicken yet.

That does not make the investment records ignorable. A dividend posts to an
income category and a brokerage fee to an expense one, so the 899 investment
records in the business file reach the spending report through their income and
expense sides. What waits is reporting *on* the holdings, not importing what
they paid. The rule sending revaluation to `Equity:Market Value Changes` is
first-round work precisely because its job is to keep market movement *out* of
the spending report.

**Two books.** The business and personal QIF files become two separate files,
with no query that can reach across them. `Books.spectable` sets out what is per
book (accounts, categories, payees, rules, OFX account mappings, fiscal year)
and what is shared (bank CSV profiles and header aliases, which are facts about
the bank rather than about either book). Money moving between them — an owner
draw — is two transactions, one in each book, each balancing on its own, both
against Equity so that neither book counts it as income.

## Features being specified

| Feature | File |
| --- | --- |
| The book, and what two books share | `spec/Books.spectable` |
| Core types, money, enumerations | `spec/CoreTypes.spectable` |
| Chart of accounts, balances, net worth | `spec/Accounts.spectable` |
| Transactions, splits, transfers | `spec/Transactions.spectable` |
| Transaction register | `spec/TransactionRegister.spectable` |
| QFX/OFX import | `spec/ImportOfx.spectable` |
| CSV import, bank header matching | `spec/ImportCsv.spectable` |
| QIF migration of existing history | `spec/ImportQif.spectable` |
| Acceptance against Quicken's own reports | `spec/AcceptanceAgainstQuicken.spectable` |
| Payees and auto-categorisation | `spec/Payees.spectable` |
| Reporting by category and payee, multi-year | `spec/Reports.spectable` |
| Investment balances | `spec/Investments.spectable` |

## Verifying the specification

AlignThree's headless tools are the check. Both are run from the sibling
`SpecStudio` checkout.

```powershell
$env:PATH += ";C:\Qt\6.10.0\msvc2022_64\bin"
$analyze = "..\SpecStudio\build\tools\analyze_cli\Release\analyze_cli.exe"
& $analyze .\spec
```

Currently: **0 diagnostics over 12 files**, 365 generated tests, of which
**66 pass and 299 are still failing stubs**. All-red is the correct starting
state — the converter ends every stub in a failure — so the count of passing
tests is the measure of how much is implemented. See `CLAUDE.md` for the build
commands.

Implemented so far, in `src/`:

| Header | What it carries |
| --- | --- |
| `money.h` | Whole cents, parsing, the register and report conventions |
| `text_types.h` | The named text and number types, and path name/parent derivation |
| `account_type.h` | The three enumerations, the class and display-sign rules |
| `date.h` | A date to the day, civil-calendar arithmetic, the two-digit year window |
| `date_parse.h` | QIF and CSV date reading, and what a file's own dates prove |
| `posting.h` | Posting, Transaction, the balance rule, register entry, Uncategorized |
| `qif_lexer.h` | Sections, cleared markers, amounts, split signs, account mapping, split ratio |
| `csv_headers.h` | Heading normalisation, the alias table, amount styles |
| `payee_rules.h` | Pattern matching and which rule wins |
| `register_view.h` | Column headings, running-balance sign, register order |
| `holding.h` | Holding value, averaged price, staleness |
| `report_ranges.h` | Named date ranges and fiscal years |
| `chart.h` | The account tree: adding, renaming, moving, deleting, hiding, retyping |
| `ledger.h` | Postings, raw and display balances, roll-ups, balance as at a date, net worth, opening balances |

Chosen in that order because the reconciliation against Quicken had already
confirmed them. Every passing test is a business rule with worked examples
behind it.

Pass the analyzer an **absolute** path. Given a relative one it indexes every
file twice and reports every symbol as a duplicate of itself.
To generate the test scaffolding, every other file is passed as `--context` so
that the types declared in `CoreTypes.spectable` resolve:

```powershell
$conv = "..\SpecStudio\dist\AlignThree-0.9.1-windows-x64\SpecTableConverter.exe"
$ctx = Get-ChildItem .\spec\*.spectable | ForEach-Object { "--context"; $_.FullName }
Get-ChildItem .\spec\*.spectable | ForEach-Object {
  & $conv -l Cpp @ctx $_.FullName .\generated
}
```

## Checking against Quicken

`tools/compare.py` reads a Quicken Itemized Categories PDF, takes the date range
from the report's own heading rather than its filename, computes the same
figures from the QIF, and prints the difference.

```powershell
python tools\compare.py --all
```

As it stands:

| Report | Result |
| --- | --- |
| 2026 year to date, before the QFX import | **matches exactly** |
| 2025 | **matches exactly** |
| 2026 after the import | differs by the import, as it should |
| All dates | income short 580,159.90, expenses over 142,695.31 |

The two exact matches validate the date reading, the split signs, the exclusion
of transfers, the handling of `--Split--`, and the per-transaction treatment of
uncategorised amounts — all at once, before any C++ exists. The all-dates gap is
somewhere in 2005 to 2024, and no Quicken report for those years exists yet.

`tools/pdftext.py` does the extraction: Quicken writes its PDFs with a
glyph-indexed subset font, where the character is the glyph id plus 29.

## The decisions the specification takes

These are the ones worth disagreeing with before any code is written. Each is
stated in the file that depends on it, with the reasoning next to it.

**Categories are accounts.** There is one tree. `Expenses:Auto:Fuel` and
`Assets:Checking` are both accounts, differing only in type. This is what lets
every transaction balance with no special case for a category, and it makes a
split line that transfers to a real account need no extra machinery. The UI can
still say "category" wherever the account is an Income or Expense one.
(`Accounts.spectable`)

**Debits are positive, for every account type.** A posting carries a signed
amount and they sum to zero. What each account type shows on screen is then a
display sign: a credit card reads as the amount owed, income as the amount
earned. (`Accounts.spectable`, the display sign rule)

**An account type belongs to a group, and the group is not stored.** The types
are Checking, Savings, Cash, CreditCard, LineOfCredit, Brokerage, IRA, HSA,
Retirement401k, Mortgage, AutoLoan, PersonalLoan, OtherDebt, RealEstate,
Vehicle, OtherAsset, AccountsReceivable, AccountsPayable, TransferIn,
TransferOut, and the three categories. The seven groups -- Banking, Credit,
Investments, Loan & Debt, Property & Asset, Business, Transfer -- follow from
the type, so an account filed once cannot drift out of its heading. The account
list is divided by group and says nothing about assets and liabilities.
(`CoreTypes.spectable`, the group rule in `UserInterface.spectable`)

**Which side of a balance sheet a type is on is asked only by the balance
sheet.** Every real type has a side, and the side is also what gives it a
display sign. Business proves the side has to follow the type and not the
group: a receivable is owed to the business and a payable by it. The Transfer
group has sides and is deliberately left off the sheet, because its accounts
hold money on its way between two accounts that are already on it.
(`CoreTypes.spectable`, the balance-side rule)

**The old type names are read for ever and never written.** A book written
before the types were split has `Bank`, `Asset`, `Liability` and `Investment`
in it. Those read as Checking, OtherAsset, OtherDebt and Brokerage. A book that
cannot be opened by the next build is a book lost.

**An account has no Name and no opening balance field.** The name is the last
segment of the path. An opening balance is a transaction against
`Equity:Opening Balances`, so the book balances from its first day — which is
what lets a twenty-year import start from a known position.

**Cleared status is on the posting, not the transaction.** A transfer leaves one
account and arrives in another on two different statements, so it clears twice.

**A credit card payment is a transfer.** Recording it as an expense would count
the spending twice, once when each charge was categorised and again when the
card was paid. (`Transactions.spectable`)

**The uncategorised side is an account, chosen by sign.**
`Expenses:Uncategorized` or `Income:Uncategorized`. An import that knows one
side of a transaction creates it rather than refusing it, and the work left to
do is visible in one place.

**OFX dedupes on FITID, scoped to the account. CSV cannot, and says so.** A
re-imported OFX file changes nothing. A re-imported CSV file has only a
date/amount/payee fingerprint to go on, so its matches are *offered* rather than
skipped. The difference is stated rather than papered over.

**Date order in a CSV or QIF file is stated in the profile, never guessed.**
Guessing wrong moves history by up to eleven months, silently. A file whose
dates are ambiguous is reported before anything is imported.

**Bank CSV headings are matched to one fixed set of fields.** `Date`, `Payee`,
`Amount` or `Debit`+`Credit`, plus optional `Memo`, `CheckNo`, `Category` and
`Balance`. Headings are normalised — case, spaces and punctuation removed — and
looked up in an alias table, so `Posted Date`, `Trans Date` and `Transaction
Date` all reach `Date`. A bank not seen before usually imports with nothing to
set up; what the user corrects is saved as a named profile and recognised
thereafter by the set of headings in the file, independent of their order.

Two headings matching one field, or two columns with the same heading, are
reported rather than resolved by taking the first. A matched `Balance` column is
used to check each row against the running balance and then discarded — it is
the nearest thing a CSV file has to an OFX ledger balance.

**The QIF migration collapses both halves of every transfer.** Quicken writes a
transfer once in each account, so a naive import doubles twenty years of them.
Two halves are one transfer only if each names the other's account in brackets,
the amounts are equal and opposite, the dates are the same, and neither is
already paired — nothing weaker is safe, because two identical transfers between
the same pair of accounts on one day is a real thing. Halves a day apart are
left as two and reported as a likely pair.

QIF also gets: the category list read first so income and expense are known
rather than inferred from signs (and every fallback reported); the opening
balance that Quicken writes as a self-transfer recognised and posted to Equity;
classes kept as a transaction tag so a once-only migration loses nothing;
memorized transactions converted into payee rules, splits included. The whole
run is all-or-nothing, and produces a report — the actual deliverable of a
migration.

**Investments: no lots, no cost basis, no realised gains.** A security is an
account of type Investment carrying a money balance, with a share count and a
last price beside it. Value is always shares × price.

A price change has to balance against something, and that something is
`Equity:Market Value Changes`. Equity is nominal, so a revaluation moves net
worth without ever appearing in a report of income and expenses — while
dividends and interest post to `Income:` and do appear, because they are
taxable and worth totalling. Asking for a gain report says what is not tracked
rather than offering a figure that would not agree with the 1099.

The carried price after two purchases is an average. It is not a cost basis and
is not used as one. (`Investments.spectable`)

## Still open

### What the real QIF files settled

`testdata/` holds two real exports: `pughkilleen.QIF` (business, 43,029 records,
June 2005 to January 2026) and `kenpugh.QIF` (personal, 739 records, 2017 to
2025). Reading them settled fourteen of the seventeen points that were
guesswork, and the `DialectRisk` rule at the end of `ImportQif.spectable` now
records each one with its evidence. The corrections that mattered:

- **`L--Split--`** is a literal category value on 1,026 business records. Read as
  a category name it would have created an account called `--Split--`. The rule
  is now "ignore `L` whenever there are split lines", which also covers the 19
  records that carry a real category alongside splits.
- **Blank-padded years.** `6/17' 5` is June 2005. Day *and* year are
  blank-padded, not zero-padded. The year tokens run `' 5` to `'26` with no gap,
  so an apostrophe year is 2000 plus the number.
- **The stock split ratio is confirmed.** The one `StkSplit` record is iShares US
  Transportation on 7 March 2024 — a real four-for-one — and its `Q` is 40. So
  `Q` is the ratio times ten, as assumed, and share counts are not out by 10×.
- **Split signs are per line, not per record.** All 1,045 split records sum
  exactly to `T`, but 268 carry lines of both signs. A posting is the `$` value
  with its sign turned round.
- **Date order is proved by the data**, not merely asserted: 24,324 dates have a
  second part above 12 and none has a first part above 12.
- **The two files use different encodings.** One is valid UTF-8; the other
  contains a lone `0x96`, which is Windows-1252. Decided by trying, not asking.
- **No security has a symbol.** All 44 are names, 43 with spaces. `Symbol` is now
  optional and `SecurityName` is the identifier — a change to `CoreTypes` and
  `Investments`.
- **No price list and no statement balances.** So a holding is priced from its
  last transaction (and every migrated holding is correctly reported stale), and
  the migration cannot check itself against a stated balance. It says so rather
  than reporting that everything agreed.
- **Quicken Home & Business sections are real transactions.** `!Type:Invoice`,
  `!Type:Bill` and `!Type:Tax` carry dated amounts with splits, and the account
  list has `Invoice`, `Bill` and `Tax` types. Receivables map to an asset,
  payables to a liability. Their `X` extension and `A` address fields are
  dropped and counted.
- **`U` never disagrees with `T`** in any of the 42,322 transaction records. The
  guard stays and is expected never to fire.

Still assumed, because the files give no evidence either way: the transfer
pairing rule (8,228 bracketed fields to pair — the one unsettled rule that
matters at scale), multi-line field continuation, and the decimal-comma case.

### What the four PDFs settled

All in `testdata/`, and three of the four have content. They are **Itemized
Categories, cash basis**, which answers "which report". Their headings also
answer how a part-year report is bounded: Year to Date reads *1/1/2026 through
10/3/2026* — it stops at today, and so does the All Dates report.

Two of my assumptions were wrong:

- **An Itemized Categories report does not exclude transfers.** The all-dates
  report carries a `TRANSFERS` section of -63,888.84, and its `OVERALL TOTAL` is
  income plus expenses plus that figure, exactly. The 2025 and 2026 reports have
  no such section because every transfer in those periods has both halves inside
  the range, so they cancel. A transfer is left out only when it nets to zero —
  arithmetic, not an exclusion rule. Our report needs the same section.
- **Quicken uses natural signs**: income positive, expenses negative, overall
  total their sum. Ours reports both positive and nets them. Expense lines are
  negated on one side of the comparison, and getting that wrong would flip
  several hundred lines at once.

The section totals of all three populated reports are now in the spec as the
first acceptance tests — four figures for the whole period, three each for 2025
and 2026.

### Still open

1. **The full category tables.** Section totals for all five Quicken reports are
   now in the spec as acceptance tests. The per-category lines are not —
   435 figure lines for all-dates, 120 for 2025, 70 each for the two 2026
   category reports and 1,235 for the 2026 transaction report. Transcribing
   them is mechanical and worth doing once the section totals agree.

2. **`testdata/printer.pdf`** is not a Quicken report — it has no report
   heading and no figure lines, and it is dated January 2026. It was in `spec/`
   with the others and has been moved along with them; delete it if it is
   nothing to do with this.

3. **Whether the seven transfer pairs match the two you approved.** Four
   between the two checking accounts and three credit-card autopays, listed in
   `ImportOfx.spectable`.

## Layout

```
spec/        the specification; the source of truth -- .spectable files only
src/         production code
generated/   generated tests, and the glue that joins them to src/
testdata/    real QIF exports, QFX files and Quicken PDFs; git-ignored
tools/       compare.py and the Quicken PDF reader
dialog.md    every instruction and correction from the author, in order

```

`generated/test_*.cpp` is overwritten on every generation and is not tracked.
`generated/*_glue.h` **is** tracked: the converter writes each glue file once as
failing stubs, then leaves it alone and appends a stub for any step added later,
so the glue is hand-written production code. Never delete it wholesale.

`testdata/` is excluded by `.gitignore`, along with `*.QIF` and `*.pdf`, so
that twenty years of transactions cannot reach a remote by accident. Remove
those lines if you would rather track them.

The production code is not laid out yet, deliberately: it follows the
specification rather than the other way round.

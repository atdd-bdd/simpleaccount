# Architecture

How SimpleAccount is put together, and what the file a book lives in actually
holds. `README.md` says what the program is for and which decisions the
specification takes; `CLAUDE.md` says how work is done here. This file is about
the shape of the code and the shape of the data.

## The rule that decides the shape

No feature is implemented before it is specified. A `.spectable` in `spec/`
comes first, it is reviewed, and only then is there code. That is why the
layers below run the way they do: the specification is the source of truth, the
tests are generated from it, and the production code exists to make those tests
pass.

```
spec/*.spectable          the specification -- worked examples, in tables
      |
      |  AlignThree (SpecTableConverter), --tag-filter "NOT WIP"
      v
generated/test_*.cpp      one test per Scenario, BusinessRule or DataType
      |                   rewritten on every generation; not tracked
      |  calls
      v
generated/*_glue.h        hand-written; joins a step to the code it exercises
      |                   written once as stubs, then never overwritten
      |  calls
      v
src/*.h                   production code: header-only, no Qt, no SQLite in
      |                   anything but the store
      |
      +---> app/          the Qt window
      +---> cli/          sa_book, sa_register, sa_report, sa_balances
```

`tools/rebuild.sh` runs that pipeline in order -- analyse, generate, build, run
-- and is the only correct way to build. Building without regenerating leaves
the previous tables compiled in, which looks exactly like a bug in production
code.

## The layers

Everything in `src/` is a header. There is no library and no link order to get
wrong; a test includes what it needs.

**The vocabulary.** `money.h` (whole cents, never a double), `date.h` and
`date_parse.h`, `text_types.h` (AccountPath, PayeeName, CheckNumber and the
rest -- small types that refuse what they must not hold), `account_type.h` (the
types, their groups, and which side of the balance sheet each is on).

**The chart.** `chart.h` holds `Account` and `Category` in two collections and
nothing else: it knows no transactions, so anything about deleting or retyping
that depends on them has to be told. It hands out the ids both collections
share, resolves between a path and an id (`Ref`), fills in a missing parent as
a placeholder, and renames and moves subtrees.

**The book.** `ledger.h` is the running total of what has been entered;
`transaction_id.h` names a transaction when it is created; `transaction_edit.h`
changes one. A ledger is derived from the transactions, never stored, so a
balance cannot disagree with the register that produced it.

**What a register and a report show.** `register_lines.h` turns transactions
into the lines of one account's register, `register_entry.h` is the blank line
at the bottom, `register_view.h` the column headings that differ by account
type. `report.h`, `report_ranges.h` and `balance_sheet.h` are the reports.

**Importing.** Three readers and three importers, kept apart: `qif_lexer.h` →
`qif_reader.h` → `qif_import.h`, `csv_reader.h` → `csv_headers.h` →
`csv_import.h`, `ofx_reader.h` → `ofx_import.h`. `import_review.h` is what a
download becomes before anybody accepts it, and `payee_rules.h` and
`category_search.h` are what make a downloaded line readable.

**Storage.** `store_sqlite.h` is the only file that knows SQL. It writes a
whole book and reads it back; the schema is below.

**The window, without Qt.** `ui_model.h` is the presentation model: the account
list, the tabs, the bottom panel, the register selection and its menu, the two
listings, the editors and the column widths. It has no Qt in it at all, which
is what lets `UserInterface.spectable` be executed rather than described.
`app/MainWindow.{h,cpp}` is the Qt shell over it and holds no rules of its own;
when it has had to decide something itself, that has been a defect twice.

**The command line.** `cli/sa_book.cpp` makes books, accounts and entries and
imports files; `sa_register`, `sa_report` and `sa_balances` print. They are how
the real exports get checked without a window in the way.

## What the program will not do

- **Debits are positive for every account type**, and a transaction's two sides
  sum to zero.
- **A transaction names accounts by id, never by path**, so renaming or moving
  a category rewrites nothing.
- **An id is never handed out twice.** The counter is recorded in the book and
  only goes forward, so a deleted account's number is never given to the next
  one created.
- **Amounts are whole cents in an INTEGER column.** A binary floating-point
  column cannot hold 0.10, and a book that cannot add up is the one thing this
  program may not be.
- **A book is a file, not a session.** Anything that changes it writes it;
  closing the window is not a way to lose a morning's entry.

## The database

One SQLite file per book, `<name>.sadb`. Books live in a single machine-wide
folder -- `%ProgramData%\SimpleAccount` -- and the list of books is made by
looking at the file names rather than maintained. `SIMPLEACCOUNT_BOOKS`
overrides the folder, which is how a test starts from an empty one.

The schema version is read before anything else. A file from a later version is
refused and left untouched; an older one is brought up to date in place by
`upgrade()`. The current version is **10**.

### schema

| Column | Type | Notes |
|---|---|---|
| version | INTEGER NOT NULL | One row. Read first, before any other table |

### book

One row. The book's own data, in named columns rather than name-and-value
pairs: there are few of them, each means something different, and a row of
named columns cannot hold a setting nobody recognises.

| Column | Type | Notes |
|---|---|---|
| name | TEXT NOT NULL | |
| fiscal_year_start | INTEGER NOT NULL DEFAULT 1 | Month, 1-12 |
| check_window_days | INTEGER NOT NULL DEFAULT 1 | How long a check number is trusted to settle a match |
| last_id_used | INTEGER NOT NULL DEFAULT 0 | The id counter, shared by accounts and categories. Recorded, not worked out from the chart |

### accounts

| Column | Type | Notes |
|---|---|---|
| id | INTEGER NOT NULL DEFAULT 0 UNIQUE | The identity a transaction names. Unique: two accounts sharing one would make every reference ambiguous |
| path | TEXT NOT NULL PRIMARY KEY | What a person reads and types |
| type | TEXT NOT NULL | The AccountType's own name |
| placeholder | INTEGER NOT NULL DEFAULT 0 | A grouping node; takes no entries |
| hidden | INTEGER NOT NULL DEFAULT 0 | Kept out of the tree; history kept |
| alias | TEXT NOT NULL DEFAULT '' | A short way to type it |
| payment_payee | TEXT NOT NULL DEFAULT '' | What a payment to it is called on the paying account's statement |
| bank | TEXT NOT NULL DEFAULT '' | From the account's own dialog |
| account_number | TEXT NOT NULL DEFAULT '' | Kept exactly as written |

### categories

Income, Expense and Equity nodes, kept apart from accounts because almost
nothing about them is shared. Same shape, minus the bank and the number, and
their ids come from the same counter so the two can never collide.

| Column | Type | Notes |
|---|---|---|
| id | INTEGER NOT NULL DEFAULT 0 UNIQUE | |
| path | TEXT NOT NULL PRIMARY KEY | |
| type | TEXT NOT NULL | |
| placeholder | INTEGER NOT NULL DEFAULT 0 | |
| hidden | INTEGER NOT NULL DEFAULT 0 | |
| alias | TEXT NOT NULL DEFAULT '' | |

### transactions

One account's own side of an entry, with the other side named by id. There are
no postings any more.

| Column | Type | Notes |
|---|---|---|
| id | TEXT NOT NULL PRIMARY KEY | Given at creation and kept for life |
| ref | TEXT NOT NULL | What the register groups by. Never a key: names that are positions cannot be keys |
| date | TEXT NOT NULL | ISO, so it sorts and compares as text |
| payee | TEXT NOT NULL DEFAULT '' | |
| raw_name | TEXT NOT NULL DEFAULT '' | What the bank sent, where a rule renamed it |
| check_no | TEXT NOT NULL DEFAULT '' | |
| memo | TEXT NOT NULL DEFAULT '' | |
| tag | TEXT NOT NULL DEFAULT '' | |
| source_ref | INTEGER NOT NULL DEFAULT 0 | The account this side belongs to, by id |
| destination_ref | INTEGER NOT NULL DEFAULT 0 | The other side, by id |
| amount_cents | INTEGER NOT NULL DEFAULT 0 | |
| cleared | TEXT NOT NULL DEFAULT 'Uncleared' | Per entry |

### transaction_splits

| Column | Type | Notes |
|---|---|---|
| transaction_id | TEXT NOT NULL REFERENCES transactions(id) ON DELETE CASCADE | |
| line | INTEGER NOT NULL | |
| destination_ref | INTEGER NOT NULL | |
| amount_cents | INTEGER NOT NULL | |
| memo | TEXT NOT NULL DEFAULT '' | |
| | PRIMARY KEY (transaction_id, line) | |

### transaction_import_ids

What the file called a row, so re-importing it is recognised. At most one per
source: a transaction is one account's own download.

| Column | Type | Notes |
|---|---|---|
| transaction_id | TEXT NOT NULL REFERENCES transactions(id) ON DELETE CASCADE | |
| source | TEXT NOT NULL | QFX, CSV, QIF |
| id | TEXT NOT NULL | The FITID, or whatever the file gave |
| | PRIMARY KEY (transaction_id, source) | |

### register_column_width

One row per column somebody dragged. A measurement per column rather than one
named setting, which is why it is a table and not more columns on `book`.

| Column | Type | Notes |
|---|---|---|
| name | TEXT NOT NULL PRIMARY KEY | The column's name in the specification, not its heading |
| width | INTEGER NOT NULL | Pixels, never below 20 |

### payee_rules

| Column | Type | Notes |
|---|---|---|
| pattern | TEXT NOT NULL | |
| match_type | TEXT NOT NULL DEFAULT 'Contains' | |
| payee | TEXT NOT NULL | |
| category | TEXT NOT NULL DEFAULT '' | |
| enabled | INTEGER NOT NULL DEFAULT 1 | |
| | PRIMARY KEY (pattern, match_type) | |

### payee_split_rules

| Column | Type | Notes |
|---|---|---|
| pattern | TEXT NOT NULL | |
| match_type | TEXT NOT NULL DEFAULT 'Contains' | |
| payee | TEXT NOT NULL | |
| shape | TEXT NOT NULL DEFAULT 'Amounts' | |
| | PRIMARY KEY (pattern, match_type) | |

### ofx_account_mappings

What a bank calls an account, and which account here that is.

| Column | Type | Notes |
|---|---|---|
| bank_id | TEXT NOT NULL DEFAULT '' | Routing number, where the file gives one |
| ofx_account_id | TEXT NOT NULL | |
| account_path | TEXT NOT NULL | |
| | PRIMARY KEY (bank_id, ofx_account_id) | |

### transfer_match_rules

| Column | Type | Notes |
|---|---|---|
| from_account | TEXT NOT NULL | |
| from_pattern | TEXT NOT NULL | |
| to_account | TEXT NOT NULL | |
| to_pattern | TEXT NOT NULL | |
| enabled | INTEGER NOT NULL DEFAULT 1 | |

### csv_profiles and csv_header_matches

A named way of reading one bank's CSV, and the headings it uses for each field.

| Column | Type | Notes |
|---|---|---|
| name | TEXT NOT NULL PRIMARY KEY | |
| date_order | TEXT NOT NULL DEFAULT 'MDY' | |
| has_header | INTEGER NOT NULL DEFAULT 1 | |
| decimal_comma | INTEGER NOT NULL DEFAULT 0 | |

| Column | Type | Notes |
|---|---|---|
| profile | TEXT NOT NULL REFERENCES csv_profiles(name) ON DELETE CASCADE | |
| heading | TEXT NOT NULL | What the file's header row says |
| field | TEXT NOT NULL | Which field it is |
| | PRIMARY KEY (profile, heading) | |

### Indexes

| Index | On |
|---|---|
| import_ids_by_id | transaction_import_ids(source, id) |
| transactions_by_date | transactions(date) |

### How an attribute becomes a column

Mechanically, by its type, so that no column is a judgement call: Money →
INTEGER (whole cents), Date → TEXT (ISO), Integer → INTEGER, Boolean → INTEGER
(0 or 1), Text → TEXT, AccountPath → TEXT, AccountId → INTEGER, an enumeration
→ TEXT holding the value's own name as the specification writes it.

### The version ladder

Each step adds to what is there; nothing already on disk is read differently by
an older step, because a new column always defaults to what its absence meant.

| To | What it added |
|---|---|
| 2 | alias, payment_payee, bank, account_number on accounts |
| 3 | the categories table |
| 4 | a transaction's own source, destination and amount, and transaction_splits |
| 5 | cleared on a transaction |
| 6 | a posting's account by id instead of path text |
| 7 | postings go away: a transaction is its own one side |
| 8 | id on accounts and categories |
| 9 | last_id_used on book; id made UNIQUE on both |
| 10 | register_column_width |

The ladder is kept for now but is no longer load-bearing: every database was
deleted in October 2026 and books are made from scratch at the current schema.
Removing `upgrade()` is item 1 in `TASKS.md`.

## Saving

A save is still a whole-book rewrite inside one transaction: delete the rows,
write them again, commit. An interrupted save leaves the file as it was rather
than half of each, which is the property worth having, but the cost grows with
the book -- 80,000 entries are rewritten to record one. Making the store the
record, so a save touches the rows that changed, is the next structural piece
of work.

Two things are deliberately not part of that rewrite: `register_column_width`
is written by upsert and never cleared, so an ordinary save cannot wipe a width
nobody mentioned; and `book.last_id_used` is updated rather than reinserted.

## Testing

`tools/rebuild.sh` analyses the specification (target: 0 diagnostics),
regenerates, builds and runs. The suite is **449 tests from 13 suites**, and it
is green by design: a block the program does not implement yet is tagged `$WIP`
and generates no test, so **a failing test is a regression, not unfinished
work**. `TAGS="" tools/rebuild.sh` generates the whole specification, tags and
all, which is how to see what is still owed.

What the suite does not cover is Qt. `ui_model.h` is executed by
`UserInterface.spectable`, but `app/MainWindow.cpp` is not executed by
anything, and both defects found by running the program in October 2026 were
there.

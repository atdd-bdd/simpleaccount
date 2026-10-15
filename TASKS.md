# Tasks

Drawn from `TODO.md` and checked against the code on 2026-10-13. `TODO.md`
stays as it is -- it holds the questions and the author's notes; this file is
the list of work those notes imply, in the order it was agreed.

Each item says whether a spec table already exists for it. Per CLAUDE.md,
anything marked **spec first** starts with a `.spectable` and stops for review
before any production code.

Selected order: **4, then 5, then 10, 11 and 13.**

Done so far: **4** (the specification speaks in transactions), **5** (ids are
recorded and never reused), **10** (the two listings and the menu on the tree),
**11** (the import is in the panel below the register) and **13** (column
widths). All green.

Two traps found while writing those tables, worth knowing before adding to
them:

- `Examples: BookInfrastructure` in `Books.spectable` is not documentation. Its
  glue (`generated/books_glue.h`) creates a real book and checks every column
  named there against the real schema, so naming a column that does not exist
  yet turns a live test red. A column being added belongs in a `$WIP` rule
  until the schema has it.
- An `Examples:` table under a `BusinessRule` or a `DataType` generates a test
  that calls a glue method of its own. A new one therefore needs either a
  `$WIP` tag or a hand-written glue method -- without both, the build breaks on
  a method nobody wrote.

## Three facts established while making this list

- `struct Posting` is gone from the code -- `src/posting.h` now holds
  `Transaction`, `Side`, `TransactionSplit`, `RegisterEntry` -- but the
  specification still speaks in postings: 267 `Posting`/`PostingRow`
  references across 12 of the 14 spectables. The suite is green because the
  glue translates the old tables onto the new shape.
- Find is already specified: nine scenarios in `UserInterface.spectable`
  (1081-1173) plus the bottom-panel scenarios (934, 951). `TODO.md` section 7
  is out of date.
- `accounts` and `categories` have `PRIMARY KEY (path)` and
  `id INTEGER NOT NULL DEFAULT 0`, so an id is not unique at the schema level,
  and `next_id_` (`src/chart.h:455`) lives only in memory, recovered from the
  maximum on load.

## Housekeeping, now that the databases have been deleted

- [ ] **1. Delete the migration machinery.** `upgrade()`
      (`src/store_sqlite.h:353-480`, seven steps) has nothing left to migrate.
      Replace with a refusal mirroring the version-99 scenario already in
      `Books.spectable:591`. *Spec first (one scenario). Small.*
- [ ] **2. Rename `src/posting.h`.** Nothing named Posting is in it; ten files
      include it under the old name. *Code only, pure rename. Small.*
- [ ] **3. CLAUDE.md section 6 cleanup.** Drop the local `unquoted()` from
      `generated/payees_glue.h`, put `~` in those two cells, correct the
      `spec_cell::unquote()` paragraph in CLAUDE.md. *Small.*

## The specification's vocabulary

- [ ] **4. Convert the `PostingRow` tables to the transaction shape.**
      SELECTED -- in progress. 12 files: `ImportQif` 38, `ImportOfx` 36,
      `Transactions` 36, `UserInterface` 28, `Accounts` 25, `Reports` 25,
      `Investments` 22, `Payees` 21, `TransactionRegister` 16,
      `AcceptanceAgainstQuicken` 8, `Books` 7, `ImportCsv` 5.
      Not a textual substitution: the glue's `group_into()`
      (`generated/transactions_glue.h:473`) shows a two-posting transfer
      between two real accounts becomes *two* transactions, one per account,
      while a posting pair against a category or Unassigned becomes one.
      *Spec only. Large. Do before items 10-19.*

### Raised by item 4, and settled

A transfer is two transactions, however it got into the book. `reg::commit`
(`src/register_entry.h`) commits the register's own entry and then, when the
destination is a real account, a second transaction for that account naming
this one back -- so a transfer typed into one register leaves the same shape
two separate downloads would, and each side clears on its own statement. The
transfer scenarios in `Transactions.spectable` now say so.

### Still open: an investment record's N field becomes a check number

A QIF `!Type:Invst` record's N field is an action word -- `NCash` -- and the
importer keeps it as the transaction's check number, so the register shows
"Cash" in the Num column. No scenario asserted it under the old shape, because
`PostingRow` had no CheckNo column at all;
`Scenario The Cash action moves cash in an investment account` is `CompareOnly`
for that reason. Whether the action belongs anywhere, and whether it belongs
there, is the author's call.

## Identity and storage

- [x] **5. Unique ids, recorded in the book.** Done. `id` is UNIQUE on
      accounts and categories, the last id used is a column of the book row,
      written inside the same transaction as the save and read back when the
      book is opened -- after the chart, so a row carrying a higher id still
      raises the counter rather than being overwritten by it. Schema 9, with an
      upgrade step that adds the column and the two unique indexes.
      Specified by: `BusinessRule An id is never given out a second time`
      (`Accounts.spectable`, with the counter not dropping back over a delete),
      `BusinessRule The id counter is recorded in the book, not worked out from
      it` (`Books.spectable`), and `Scenario Every node in the chart has an id,
      and the book records the last one` -- the one scenario in the whole
      specification that states literal ids, because a new book starts the
      counter from nothing. Groups take ids too, numbered before the child that
      filled them in, which is the order `chart::add` already creates them in.
      Two spec tables had to be restated to be implementable: each row of the
      counter table now creates exactly one node (`Assets`, then
      `Assets:Checking`, then the delete, then `Assets:Savings`), and the Books
      scenario names its groups instead of relying on parent-filling -- as
      written, one row would have made two ids. The counter rule's glue asserts
      the id each node actually got, not just the counter, so a counter that
      dropped back would hand out 2 twice and fail.
- [ ] **6. Starting balance and its date in account settings.**
      `OpeningBalance` is in `Accounts.spectable`; the settings dialog does not
      offer it. *Spec first. Medium.*
- [ ] **7. A CSV import sets Cleared** (`TODO.md` 1), with a book option "do
      not set cleared on CSV import". *Spec first (`ImportCsv.spectable`
      tables, the option in `Books.spectable`). Medium.*
- [ ] **8. `CsvProfile` gains `BalanceColumn`** (`TODO.md` 2), ignored when the
      column is absent or empty. `csv::Profile` already has the field.
      *Spec first. Small.*
- [ ] **9. The default destination is Uncategorized** (`TODO.md` 3 note).
      Touches the register's blank row and every table that currently asserts
      an empty category. *Spec first. Medium.*

## The window

- [x] **10. An account listing and a category listing from the menu**, click
      to edit, and a context menu on the left-hand tree. Done -- nine scenarios
      at the end of `UserInterface.spectable`, under "The listings, and the menu
      on the tree", and `ui::account_listing` / `ui::category_listing` beside
      the account list they are the opposite of. In the window: the two
      listings on the Accounts menu, a context menu on the tree, and the
      account-settings and category-settings editors, neither of which existed
      before -- the settings were specified and stored but had no way in.
      Delete is greyed by asking the transactions whether anything names the
      account, because `chart::postings_on` answers from a count that nothing
      in the workspace sets. What the scenarios settle: each
      listing is one line per node, every node, in path order, with no group
      headings and nothing hidden left out (the tree is the opposite on all
      four counts, deliberately); the account listing carries the bank and the
      number from the account's own dialog, the category listing carries the
      alias; both are windows of their own rather than tabs; a click opens the
      editor of the thing on the line, and Edit on the tree opens the same one.
      The tree's menu offers New account, Edit, Hide and Delete, with Delete
      greyed rather than absent when the account has entries, and a group
      heading offers only New account because a heading is not a thing in the
      book. *Medium-large.*
- [x] **11. The import moves to the bottom panel** rather than beside the
      register. Done; the suite is 432 tests and green, and the app builds.
      Note that "implementation only" understated it: there was no bottom panel
      anywhere. `ui_model.h` showed the review as pane 2, and `MainWindow` put
      `reviewPanel_` in a horizontal splitter beside the tabs. What was
      written: the panel in the model (its tabs, the active one, open exactly
      when it has a tab), `panes()` reduced to the one pane, three glue steps
      connected plus the tab-kind enumeration by hand, and the splitter turned
      vertical with a closable tab widget below -- closing that tab is
      cancelling the import, there being nothing else it could mean.
      `PaneContent::ImportReview` and `split_open()` went with the second pane:
      the specification's enumeration no longer lists the one, and the other
      could only return false. Six blocks came off `$WIP`; the four Find ones
      stay for item 12, which is what the panel's second tab is for.
- [ ] **12. Find.** Modeless dialog, results as a tab in the same bottom panel.
      *Already specified: nine scenarios. Implementation only. Large.*
- [x] **13. Column resize, widths shared across registers and stored with the
      book.** Done -- "The width of a column" at the end of
      `UserInterface.spectable`, widths on the workspace, a
      `register_column_width` table at schema 10, and a `sectionResized`
      connection in the window that records the width, puts it onto every other
      register and saves. The table is written by upsert and never cleared, so
      an ordinary save -- which is nearly every save, and knows nothing about
      widths -- cannot wipe them. The conflict at line 14 is resolved rather
      than ignored: what a column is *wide by default* is still not specified,
      so changing it cannot fail a test, but a width somebody set by dragging
      is behaviour, because it has to outlast the register it was set in and
      outlast the session. One set of widths for every register (they are all
      the same nine columns), kept with the book, keyed by the column's name in
      this specification rather than by its heading -- Payment and Deposit are
      headed differently per account type and the width is not. Only a column
      somebody resized is recorded. A width is clamped to a minimum of 20
      pixels, because a column dragged to nothing cannot be dragged back; that
      number is my choice and is the one thing here worth disagreeing with.
      *Medium.*
- [ ] **14. Sort a register by any column.** The only ordering scenario today
      is `TransactionRegister.spectable:182` (same-date entry order), which
      sorting has to keep as the tiebreak. *Spec first. Medium.*
- [ ] **15. Register date filters** -- "this year", "past two years" -- and the
      book-level "hide transactions before this date". Two features, one spec
      pass. *Spec first. Medium.*
- [ ] **16. A scroll bar on reports and every other window** (`TODO.md` 8).
      `ScrolledTo` scenarios already exist for the register. *Mostly code.
      Small-medium.*
- [ ] **17. A modeless "in progress" dialog** during an import and a
      recalculation. *Spec first. Small-medium.*
- [ ] **18. Tags.** The field is on the transaction and in storage already;
      what is missing is the tag table, the list from a menu, and the
      type-ahead that works the way category entry does. *Spec first. Medium.*
- [ ] **19. A memo carried from CSV and QFX onto every transaction.**
      `MemoColumn` and memo are already through the import specs; this one
      needs a gap check before it can be sized. *Unknown until looked at.*

## Import semantics

- [ ] **20. A context menu on a "Possible" row showing the candidate matches**
      it could be. *Spec first. Medium.*
- [ ] **21. Re-importing the same file: matched or duplicate.** The second
      import reported Matched where duplicates were expected. The multiset
      pairing question has to be settled before a spec table.
      *Design, then spec. Medium.*
- [ ] **22. Changing a destination to an account offers matching
      transactions** in the other register, with "none" creating an uncleared
      transaction there instead (`TODO.md` 4 note). *Spec first.
      Medium-large.*

## Last

- [ ] **23. The cost of redoing a report or a register after one change**
      (`TODO.md` 5). The author's instruction is to leave this until the end
      as an optimisation. *Investigate before changing anything.*

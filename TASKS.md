# Tasks

Drawn from `TODO.md` and checked against the code on 2026-10-13. `TODO.md`
stays as it is -- it holds the questions and the author's notes; this file is
the list of work those notes imply, in the order it was agreed.

Each item says whether a spec table already exists for it. Per CLAUDE.md,
anything marked **spec first** starts with a `.spectable` and stops for review
before any production code.

Selected order: **4, then 5.**

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

- [ ] **5. Unique ids, recorded in the book.** SELECTED -- next. Make `id` the
      primary key (or UNIQUE) on accounts and categories, store the last id
      used, increment from it rather than deriving a maximum, so a deleted
      account's id is never handed out again. *Spec first
      (`Books.spectable` storage rule, `Accounts.spectable`). Medium.*
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

- [ ] **10. An account listing and a category listing from the menu**, click to
      edit, and a context menu on the left-hand tree. No spec exists for any
      of it. *Spec first. Medium-large.*
- [ ] **11. The import moves to the bottom panel** rather than beside the
      register. *Already specified: `UserInterface.spectable:934, 951`.
      Implementation only. Medium.*
- [ ] **12. Find.** Modeless dialog, results as a tab in the same bottom panel.
      *Already specified: nine scenarios. Implementation only. Large.*
- [ ] **13. Column resize, widths shared across registers and stored with the
      book.** Note the conflict: `UserInterface.spectable:14` currently says
      column widths are deliberately *not* specified, so that line has to be
      revisited first. *Spec first. Medium.*
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

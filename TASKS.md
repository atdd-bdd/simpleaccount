# Tasks

Drawn from `TODO.md` and checked against the code on 2026-10-13. `TODO.md`
stays as it is -- it holds the questions and the author's notes; this file is
the list of work those notes imply, in the order it was agreed.

Each item says whether a spec table already exists for it. Per CLAUDE.md,
anything marked **spec first** starts with a `.spectable` and stops for review
before any production code.

All twenty-four items are done. They have moved, in full, to
`archive/completed-tasks.md`, which is also where the record of what each one
settled now lives. What is owed now is in `TODO.md` and in `questions.md`
section 31 -- the behaviours that are specified and not yet written -- rather
than here.

Two traps found while writing those tables, worth knowing before adding to
them:

- `Examples: BookInfrastructure` in `Books.spectable` is not documentation. Its
  glue (`generated/books_glue.h`) creates a real book and checks every column
  named there against the real schema, so naming a column that does not exist
  yet turns a live test red. A column being added belongs in a `$TBD` rule
  until the schema has it. Item 18's tag table is written that way.
- An `Examples:` table under a `BusinessRule` or a `DataType` generates a test
  that calls a glue method of its own. A new one therefore needs either a
  `$TBD` tag or a hand-written glue method -- without both, the build breaks on
  a method nobody wrote.

## Three facts established while making this list

- `struct Posting` is gone from the code -- `src/transaction.h` now holds
  `Transaction`, `Side`, `TransactionSplit`, `RegisterEntry` -- and the
  specification no longer speaks in postings either; see item 4.
- Find is already specified: nine scenarios in `UserInterface.spectable` plus
  the bottom-panel scenarios. `TODO.md` section 7 is out of date.
- `accounts` and `categories` have `PRIMARY KEY (path)` and
  `id INTEGER NOT NULL DEFAULT 0`, so an id is not unique at the schema level,
  and `next_id_` (`src/chart.h:455`) lives only in memory, recovered from the
  maximum on load.

## Still open: an investment record's N field becomes a check number

A QIF `!Type:Invst` record's N field is an action word -- `NCash` -- and the
importer keeps it as the transaction's check number, so the register shows
"Cash" in the Num column. No scenario asserted it under the old shape, because
`PostingRow` had no CheckNo column at all;
`Scenario The Cash action moves cash in an investment account` is `CompareOnly`
for that reason. Whether the action belongs anywhere, and whether it belongs
there, is the author's call.


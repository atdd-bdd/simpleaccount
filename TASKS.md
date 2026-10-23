# Tasks

Drawn from `TODO.md` and checked against the code on 2026-10-13. `TODO.md`
stays as it is -- it holds the questions and the author's notes; this file is
the list of work those notes imply, in the order it was agreed.

Each item says whether a spec table already exists for it. Per CLAUDE.md,
anything marked **spec first** starts with a `.spectable` and stops for review
before any production code.

Twenty-two of the first twenty-three items are done. They have moved, in full,
to `archive/completed-tasks.md`, which is also where the record of what each
one settled now lives. What follows is what is owed.

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


## The window

- [ ] **17. A modeless "in progress" dialog** during an import and a
      recalculation. Specified -- "Saying that something long is going on" in
      `UserInterface.spectable`: `BusinessRule Work that takes a noticeable
      time says so, and does not take the window away`, and four `$TBD`
      scenarios. What they settle: modeless, because a modal window over an
      import hides the register the import is about to change and because
      nothing is being asked; a count with a total is a time estimate and a
      count without one is not, so work that cannot say how much there is
      reports what it has done and no bar; the report appears only when the
      work takes longer than a second, so an import of six rows shows nothing;
      finishing takes the report away; and stopping a recalculation leaves the
      book exactly as it was, because a half-finished recalculation is not a
      state the book is allowed to be in. Part done -- the strip is in the
      status bar, hidden until progress::shows() says a second has gone by, and
      it is driven from the one loop in the window that is both countable and
      writes nothing: filling a register's cells. Two things are not met, both
      for want of a thread. The opaque imports -- qif::Importer::run,
      review::from_file -- are each one call with no point inside to report
      from, so they say nothing; and there is no Stop, because the work runs on
      the thread that would deliver the click and a button that does nothing is
      worse than none. advanceProgress() already returns whether to carry on
      and the callers already read it. *Code left: a tick callback in the
      readers, and the work off this thread.* questions.md 14.

## Last

- [ ] **24. `And` for the second and later steps of a Given, When or Then.**
      Asked for on 2026-10-23. In a scenario, only the first step of a run
      needs its keyword: `Given ... And ... And ...` reads the way the author
      would say it out loud, and the same holds for `When` and `Then`. The spec
      tables here repeat the keyword on every step, which is how they were
      first written and not a decision.

      To be done once the defects in hand are fixed, not before, and it is a
      rewrite of all fourteen files rather than a file at a time. The test for
      it is exact: **the generated `test_*.cpp` must come out byte-identical.**
      `And` is the same step as the keyword above it, so nothing about what is
      generated changes -- as with the 2,851 `#` continuations converted to
      `\`, where byte-identical output was what said the conversion was sound.

      So: analyze (0 diagnostics), generate into a scratch folder, `diff -r`
      against the generated folder, and only then commit. A diff means an `And`
      landed under the wrong keyword, which is the one way this can go wrong --
      an `And` after a `Then` that was meant to begin a new `When` would be a
      silently different test.

      Checked before the item was written, on a copy of `spec/` so nothing in
      the repo moved: `Given`/`And`/`And` in `Payees.spectable` and
      `Then`/`And` in `AcceptanceAgainstQuicken.spectable` both analyze at 0
      diagnostics under 0.9.2 and generate test files byte-identical to the
      ones on disk, 51 tests either way for Payees. So the keyword is accepted
      and the byte-identical test is a real test. Worth knowing: `And` is not
      in `spectable syntax v3.3a.md` anywhere, so the converter is the only
      authority for it -- one for `alignthree-findings.md`.

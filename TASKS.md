# Tasks

Drawn from `TODO.md` and checked against the code on 2026-10-13. `TODO.md`
stays as it is -- it holds the questions and the author's notes; this file is
the list of work those notes imply, in the order it was agreed.

Each item says whether a spec table already exists for it. Per CLAUDE.md,
anything marked **spec first** starts with a `.spectable` and stops for review
before any production code.

Selected order: **4, then 5, then 10, 11 and 13.**

Done so far: **2**, **3**, **4** (the specification speaks in transactions),
**5** (ids are recorded and never reused), **10** (the two listings and the
menu on the tree), **11** (the import is in the panel below the register),
**12** (Find), **13** (column widths) and **16** (the scroll bars). All green.

A pass on 2026-10-20 went down the rest of the list and wrote the spec half of
every item marked *spec first*: **1, 6, 7, 8, 9, 14, 15, 17, 18, 21, 22** now
have their tables, every new block tagged `$TBD`, which is where CLAUDE.md says
to stop. **19** and **20** turned out to be done already and say so below.
**23** was investigated and nothing was changed. What is owed after this pass
is the code for those eleven, each of which is a review before it is a build.

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

## Housekeeping, now that the databases have been deleted

- [x] **1. Delete the migration machinery.** `upgrade()`
      (`src/store_sqlite.h:353-480`, seven steps) has nothing left to migrate.
      Replace with a refusal mirroring the version-99 scenario already in
      `Books.spectable`. Specified: `BusinessRule A book of any version but
      this one is refused` (`Books.spectable:820`) with
      `Scenario A book from a later version of the program is refused` (808)
      and `Scenario A book from an earlier version of the program is refused`
      (848). Done -- `upgrade()` is gone and `open_named` refuses any version
      but `kSchemaVersion`, in both directions. There is no migration at all
      now, which is the decision of 2026-10-22: a book either starts over or is
      initialised from a QIF.
- [x] **2. Rename `src/posting.h`.** Done -- `src/transaction.h`, and the ten
      files that included it under the old name.
- [x] **3. CLAUDE.md section 6 cleanup.** Done -- the local `unquoted()` is out
      of `generated/payees_glue.h`, the two cells carry `~`, and the
      `spec_cell::unquote()` paragraph is corrected.

## The specification's vocabulary

- [x] **4. Convert the `PostingRow` tables to the transaction shape.** Done.
      Not a textual substitution: the glue's `group_into()` shows a two-posting
      transfer between two real accounts becomes *two* transactions, one per
      account, while a posting pair against a category or Unassigned becomes
      one.

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
- [x] **6. Starting balance and its date in account settings.** Specified --
      "The starting balance, in the dialog rather than only at creation" in
      `Accounts.spectable:641-790`, seven `$TBD` scenarios. What they settle:
      there is still no opening balance field on an account. The dialog shows
      the transaction against `Equity:Opening Balances`, read back off the
      book, and editing the field edits that transaction -- so the figure has
      one home whichever way it was entered, and an account that never had one
      shows an empty field rather than a zero. Done -- Starting balance and
      As at are on the account settings dialog, read back off the book with
      `edit::starting_balance_of` and written with `edit::set_starting_balance`,
      so the figure has one home whichever way it was entered.
- [x] **7. A CSV import sets Cleared** (`TODO.md` 1), with a book option "do
      not set cleared on CSV import". Specified -- `SetClearedOnCsvImport` on
      `Attributes BookSetting` (`ImportOfx.spectable`, default true) and the
      two scenarios at `ImportCsv.spectable:1184` and `:1221`. A download
      arrives cleared because the bank has already reported it; the option
      exists because a CSV is sometimes a hand-made file rather than a
      download. Done -- `set_cleared_on_csv_import` is on the book row and in
      the book settings dialog, and the import reads it off the book rather
      than remembering it.
- [x] **8. `CsvProfile` gains `BalanceColumn`** (`TODO.md` 2), ignored when the
      column is absent or empty. Specified -- `BalanceColumn` on `CsvProfile`
      (`ImportCsv.spectable:41`), `BalanceColumnCheck`,
      `Examples: BalanceColumnUse`, and the scenarios from :553 on. The column
      is a check and not an import: a running balance that disagrees is a
      question about the file, not a transaction. Done -- `csv::balance_checks`,
      and the profile's `balance_column` is stored with the book now that
      `csv_profiles` holds the whole of `csv::Profile` (schema 16).
- [x] **9. The default destination is Uncategorized** (`TODO.md` 3 note).
      Specified -- `BusinessRule A destination nobody named is Uncategorized`
      (`Transactions.spectable:122`) with the two scenarios at :164 and :180,
      and `Scenario The blank line shows no category and commits to
      Uncategorized` (`TransactionRegister.spectable:936`): the blank line
      shows no category, and committing it writes `Expenses:Uncategorized`.
      The sign chooses between the two -- money out asks what it was for,
      money in asks where it came from -- and Uncategorized stays distinct from
      Unassigned, which is chosen and never defaulted. Done --
      `ledger::uncategorized_for` chooses by the sign, and the register, the
      CSV reader and the OFX reader all go through it.

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
      could only return false. Six blocks came off `$TBD`; the four Find ones
      stay for item 12, which is what the panel's second tab is for.
- [x] **12. Find.** Modeless dialog, results as a tab in the same bottom panel.
      Done -- the Find section of `UserInterface.spectable`, `src/find.h`, and
      Edit > Find... in the window. Choosing a result goes to the transaction
      in the register the result is read under, which the spec gained two
      scenarios for.
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
- [x] **14. Sort a register by any column.** Specified -- "Sorting by a column
      other than the date" in `TransactionRegister.spectable`:
      `BusinessRule What each column sorts on`,
      `BusinessRule Which way round a sort starts`, and five `$TBD` scenarios.
      What they settle: Amount sorts on the signed number and not on the
      printed text, so -100.00 comes before -9.99; the same-date entry order
      (`:182`) is the tiebreak and a sort is stable, so tied lines stay as they
      were; sorting by the same column twice reverses it; the running balance
      is shown only in date order, because that is the only order it adds up
      in, and sorting back by date brings it back. `Attributes SortBy` gained a
      defaulted `Direction` in `CoreTypes.spectable`, so every table that names
      only a column still says what it used to say. Done --
      `detail::sorts_before` has a key per column, Memo and Tag among them, and
      the heading is a toggle in the window. The glue checks the order itself
      now rather than only the row count: the Memo and Tag rows of the sort-key
      table are chosen so that no other key would put them in that order.
- [x] **15. Register date filters** -- "this year", "past two years" -- and the
      book-level "hide transactions before this date". Specified -- "Date
      ranges with names, and a book that hides its early years" in
      `TransactionRegister.spectable`: `BusinessRule The date ranges the
      register offers by name` with its six names,
      `BusinessRule A book may hide everything before a date`, and five `$TBD`
      scenarios. What they settle: "This year" is the calendar year even in a
      book whose fiscal year starts in July, because the name says year and not
      fiscal year; "past two years" counts back from today rather than from
      January; a register with no range chosen shows all dates; a book that
      hides its early years brings their balance forward as an opening line
      rather than dropping it, so the register still adds up; and a range
      wholly inside the hidden years shows nothing and says why, rather than
      showing an empty register. `HideBefore` and `FiscalYearStart` are on
      `Attributes BookSetting` (`ImportOfx.spectable`). Done -- `reg::named_range`
      and `reg::in_range` behind a range box per register, and `hide_before` on
      the book row and in the book settings dialog.
- [x] **16. A scroll bar on reports and every other window** (`TODO.md` 8).
      Done. `ScrolledTo` and the paging scroll bar were already specified and
      built for the register, and every `QTableWidget` scrolls on its own --
      the gap was the windows built by hand around a form, which lay their
      content out on themselves and so have nowhere to put content taller than
      the screen: the buttons end up below the bottom edge and the window
      cannot be made small enough to bring them back. Two helpers in
      `app/MainWindow.cpp` (`scrollingBody`, `fitToScreen`): the first gives a
      dialog a scroll area filling it and returns the widget the content goes
      into, so it is called in place of the dialog as the layout's parent and
      nothing else about the dialog changes -- a widget made with the dialog as
      its parent is reparented when it is added to a layout, so every pointer
      already held stays good. The second caps a window to the screen it opened
      on and leaves a smaller one alone. Wrapped: the rule form, the
      account-settings and category-settings editors, the new-book question,
      the new-account form and the import question. Capped but not wrapped: the
      category picker, the two listings, the split look and the duplicate look,
      each of which holds a table or a list that scrolls already -- a scroll
      area around one of those would be a scroll inside a scroll. On a report
      tab the table already scrolled; what did not was the chooser row, which
      is a single row that does not wrap, so it now scrolls sideways at the
      height one row needs.
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
- [x] **18. Tags.** Specified -- "Tags" in `UserInterface.spectable`
      (`BusinessRule A tag is the third thing a transaction says about itself`,
      `BusinessRule Typing a tag works the way typing a category works`, five
      `$TBD` scenarios) and `BusinessRule A tag table of its own, once there is
      a list of tags` in `Books.spectable`. What they settle, from the author's
      own words in `TODO.md:33`: the field holds words and every word in it is
      a tag, so a transaction can carry several and a tag is therefore a single
      word -- a tag with a space in it is not available, and where one is
      wanted the words are joined. The transaction keeps the tag as a name
      rather than as an id, which is the one place this book departs from the
      identity rule for accounts: a tag is a label and not a thing with a
      balance, renaming one is a rename of every use of it, and nothing refers
      to a tag except the transactions carrying it. A tag nothing carries stays
      in the table, because the list is a vocabulary. Completion fires on the
      first character, matches the word after the last space rather than the
      whole field, ignores case, and a word matching no tag is accepted and
      added -- which is how a tag comes to exist. No report groups by tag in
      this release. The tag table is stated in a `$TBD` rule of its own rather
      than in `Examples: BookInfrastructure`, per the trap at the top of this
      file. Done -- `src/tags.h`, the `tags` table, the Tags listing under
      Accounts with Add and Rename, the Tag column in the register, and a word
      typed there that is in no list joins it.
- [x] **19. A memo carried from CSV and QFX onto every transaction.** Done
      already; this was the gap check the item asked for. In the spec,
      `MemoColumn` is on `CsvProfile` and memo is asserted through the import
      tables. In the code: `src/csv_import.h` has `memo_column` (324), the
      alias map (107, 215-219), `read.memo` (474) and
      `t.memo = one.row.memo` (643); `src/ofx_import.h` sets `t.memo` at 629
      and 678, and on both halves of a proposed transfer at 1010/1022 and
      1067/1079. Nothing is dropped on the way through. *Nothing left.*

## Import semantics

- [x] **20. A context menu on a "Possible" row showing the candidate matches**
      it could be. Done already. The rule and four live scenarios are in the
      import section, and the window wires them: `show_duplicate()` and
      `close_duplicate_look()` in `ui_model.h`, the menu on the review row, and
      the duplicate look at `app/MainWindow.cpp:2123`, which quotes each
      candidate as its own file wrote it. *Nothing left.*
- [x] **21. Re-importing the same file: matched or duplicate.** The design
      question is settled and most of it is built. The `Matched` disposition is
      gone; the four dispositions and the notes column are specified, and the
      multiset pairing is the count -- two identical rows in a file are two
      transactions, so a row is a duplicate only while the book has an unpaired
      transaction left to pair it with. Re-import scenarios are live in
      `ImportOfx.spectable:661` and `ImportCsv.spectable:1435`. This pass added
      the one case nobody had written: `$TBD Scenario Re-importing a file only
      half of which was accepted asks about the other half` in
      `ImportCsv.spectable` -- three of five rows accepted, so the second read
      says Duplicate three times and New twice. It is the shape the author's
      report was about, and it is the one that cannot be got right by
      remembering which files have been read: a file is not a unit of anything
      after the review, so the second import has to work it out from the book.
      Done -- the scenario is live and the pairing is a multiset count, so three
      of five accepted rows read back as three Duplicates and two News.
- [x] **22. Changing a destination to an account offers matching
      transactions** in the other register, with "none" creating an uncleared
      transaction there instead (`TODO.md` 4 note). Specified -- "Naming a real
      account in the destination column" in `UserInterface.spectable`:
      `BusinessRule Naming an account in the destination column looks for the
      other half` and six `$TBD` scenarios, written on the author's own 3348.07
      figure. What they settle: the evidence is the import's evidence -- equal
      and opposite to the cent, in the account just named, within the two-day
      window, not already paired, the payee not consulted -- and the candidates
      are offered closest in days first. Choosing one is a *merge* and not a
      link: the transaction being edited keeps its own date, payee and memo,
      the transaction that was in the other register is gone, and the survivor
      is cleared if either half was. Answering none creates nothing, because
      the one transaction already names both accounts and so is already an
      uncleared line in the other register -- what none rules out is the merge,
      not the transfer. A category, an Unassigned destination, and an account
      with nothing equal and opposite in it are all silent.
      `src/transfer_match.h` has the window, the `Half` shape, `halves_in` and
      `merge`. Done -- `MainWindow::offerTheOtherHalf` asks from the register,
      closest in days first, and choosing one is a merge. One thing had to be
      decided: `reg::commit` makes a transfer as two rows and this rule
      describes it as one, so the row this commit made for the other account
      stands in the place of the line that is gone and carries its cleared
      status. questions.md 15.

## Last

- [x] **23. The cost of redoing a report or a register after one change**
      (`TODO.md` 5). Investigated on 2026-10-20; nothing changed, per the
      item's own instruction. The finding worth having first: **nothing
      re-reads the book from disk after an edit.** Every cost is in memory, and
      there are three of them, each O(n) in the whole book for a change to one
      transaction:
      - `save()` (`app/MainWindow.cpp:1041`, the only `store_.write`) ->
        `store_sqlite.h:589+` builds a `state_of()` string for every
        transaction in the book to diff against what is stored, and rewrites
        `accounts`, `categories` and `payee_rules` wholesale
        (`DELETE FROM` all three, then re-insert). Transaction rows themselves
        are replaced row by row, which is the cheap part.
      - `book_is_now(...)` (`src/ui_model.h:486`) deep-copies `accounts_`,
        `book_`, `transactions_` and `splits_`, and clears `review_`,
        `accepted_`, `results_`, `selected_`, `listing_` and `editor_` -- so an
        edit also throws away the import review, the find results and the
        selection, which is a correctness cost as much as a speed one.
      - `refresh()` (`app/MainWindow.cpp:2345`) builds one fresh
        `reg::BookIndex` per refresh (`src/register_lines.h:81-147`: a
        `std::map<std::string, std::size_t>` keyed by id over every
        transaction, the parts, the starting balances, and a `stable_sort` of
        all indices), then repopulates every open tab's Qt widgets.

      Report tabs are the one thing already cached: each holds its last result
      against `workspace_->changes()` and only re-runs when that number moves.
      The cheapest first move, if this is ever taken up, is the string-keyed
      map in `BookIndex` -- ids are integers and the map is rebuilt from
      nothing on every refresh.

      Two of the three were paid for on 2026-10-22, both from the author's own
      report that switching registers and accepting an import took a noticeable
      time. `refresh()` now fills only the tab in front and marks the rest
      stale, so switching to a register that is already right costs nothing;
      and the book is opened `journal_mode = WAL` with `synchronous = NORMAL`,
      which is what made `save()` after an import wait on the disk. The
      string-keyed map in `BookIndex` is still there and is still the cheapest
      next move.

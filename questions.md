# Questions

Written while working the list unattended. Each one is a question the work
raised that only the author can settle. None of them blocked anything: each
says the decision taken and where the code now is, so changing the answer is a
change to one named place rather than an archaeology exercise.

## 1. Can an existing transaction's tag be edited?

Not from the register, no. The register's cells are read-only on every line but
the blank one -- a stray keypress must not change a transaction from 2009 --
and there is no transaction editor dialog in the window at all; the memo is in
exactly the same position. So today a tag is set when the line is entered, and
changed afterwards only by renaming the tag everywhere from **Accounts >
Tags...**.

**Decision taken.** Leave it. The gap is the absence of a transaction editor,
not something about tags, and inventing one for this column alone would put a
second way of editing a transaction in the program. If an editor is wanted it
is its own item, with its own spec table, and it carries the memo, the tag, the
check number and the cleared status together.

## 2. Does the tag belong on a split part as well as the transaction?

The field is one column on the transaction, so a split transaction carries one
tag for the whole of it. A rental expense split across two categories cannot
tag one half.

**Decision taken.** One tag per transaction, as `Books.spectable` settles it.
If a split part ever needs its own tag the column goes on the split row and
`tags::words_of` reads it the same way; nothing else changes.

## 3. The progress strip is specified but not shown anywhere

`src/progress.h` is written and green against the noticeable-time rule in
`UserInterface.spectable`: a `Report`, the words it reads as, and the
one-second threshold below which no strip appears. Nothing in the window
displays one yet.

**Why it stopped there.** The strip cannot be driven from the window as the
code stands. The jobs that take a noticeable time -- reading a QIF file,
accepting an import, totalling a report over twenty years -- are single calls
into header-only code in `src/` with no callback, and a Qt timer cannot fire
while one of them is running: the event loop is inside the call. Showing a
strip that is a lie (always visible, never updated) is worse than showing none,
and a thread is a change of a different size.

**Decision taken.** The honest shape is a progress callback parameter on the
three slow calls -- `qif::read`, `ui::accept_import` and `report::run` -- taking
`const std::function<void(const progress::Report&)>&` with a default that does
nothing, so every existing caller and all the glue stay as they are. That is an
afternoon's work in four files and wants its own pass. Until then the status bar
says what happened after the fact, which is what it says today.

## 4. Is the register's date range meant to be remembered with the book?

Each register tab now has its own range and its own sort column, held on the
window rather than in `ui::Workspace`. Reopening the book opens every register
on All dates in date order.

**Decision taken.** Not remembered, for now. A report's controls are not
modeled in the workspace either -- the window owns them, the way a dialog owns
its fields -- and the sort and the range are the same kind of thing. The
workspace is specified in `UserInterface.spectable` and adding two columns to a
tab row is a spec change plus a schema change for a convenience nobody has
asked for yet. Hide-before, which *is* a book setting, is stored with the book
and wins over any range chosen inside the hidden years.

## 5. Which tag does a converted payee's transaction get?

The payee converter rewrites what the import calls a payee; the categoriser
puts a category on a payee it knows. Neither says anything about tags.

**Decision taken.** Nothing. An import brings in no tags, and a rule that
quietly tagged transactions would be a rule the author cannot see the effect
of. Tags are typed, in the column, on the line.

## 6. The fourth row of the percentage rounding rule did not add up

`BusinessRule How a percentage split rounds` in `Payees.spectable` had this row:

```
| 10.00   | 33       | 33       | 3.34    | 3.30    | Percentages need not total 100 |
```

33% of 10.00 is 3.30 exactly, twice, so no rounding happens at all and there
is nothing to give to the larger line. 3.34 cannot be reached from the stated
rule -- "each line is rounded to the cent, and whatever is left over after
that is added to the largest line" -- by any reading I could find. It looks
like 10.00 divided three ways rather than 33 and 33 of it.

**Decision taken.** The rule is implemented as it is written, and the row is
amended to `3.30 | 3.30` with the note extended to say that the rest is a
shortfall. Two lines of 33 allocate two thirds of the payment and the
remaining 3.40 is reported as a shortfall for the user to place, which is the
same answer the Amounts shape gives when the lines no longer come to the
total. The alternative -- normalising the percentages to their own total, so
33 and 33 become 5.00 and 5.00 -- would mean a person who typed 70 and 20 by
mistake silently got 77.78 and 22.22, which is the program deciding where a
tenth of the money went.

## 7. A remembered split is offered, and nothing writes it

The three remembered-split scenarios say what is offered and what the
shortfall is. None of them says the split is stored, and `Then split offered
is` is about a window that is about to open rather than about the book.

**Decision taken.** `payees::offer_split` returns the lines and the
arithmetic, and the import stops there. The split window opened on the offer
is where it is accepted, and the save goes through
`edit::split_save_check` -- the same check a hand-typed split goes through, so
a remembered split that no longer adds up is refused in exactly the way a
mistyped one is. The `Payee` field on the split rule is carried on the offer
and is not applied to the transaction here: the ordinary payee rules already
name a row, and two things renaming one row is how they come to disagree.

## 8. The assignment rule's table said neither half is deleted; the code deletes one

`BusinessRule Assigning an account asks whether the other entry already exists`
in `Transactions.spectable` had a row reading

```
| Has the row, found by the user | Each points at the other's account; neither is deleted |
```

but its own `Details`, three paragraphs above it, say of the same case: "Then
there are two transactions describing one movement of money, and leaving both
would double it. **They become one.**" Those cannot both be true. Keeping two
transactions that point at each other is exactly the double-counting the
Details rule out -- the money would appear in the checking register and again
in the card register as two separate movements, and every balance and report
downstream adds them both.

`transfers::merge` in `src/transfer_match.h` implements the Details: the
transaction being edited keeps its own date, payee and memo, takes the higher
cleared status of the two, inherits the other half's import identifiers so that
re-reading the file still finds this payment, and the other half is erased.

**Decision.** The Details are what the program does, so the table row was the
error and has been amended to "The two become one transaction naming both
accounts". The rule stays `$TBD`: `merge` is written and used, but nothing yet
drives the *offering* half of the rule -- the list of the other account's
entries, nearest in date and amount first, that the user scrolls through. That
is a register panel, and the block comes untagged when the panel exists.

Worth a second look when you read this: the inherited `import_ids` mean a later
download of *either* file matches the surviving transaction, which is what
stops a re-import from re-creating the half that was merged away.

## 9. A book is quick because of two pragmas, and one of them gives something up

You said importing a QFX file and accepting the transactions took a while even
with nothing checked, and that switching between two registers took time too.
Both are now measured rather than guessed at.

The register half was already fixed: `MainWindow::refresh` fills only the tab in
front and marks the others stale, and `tabChanged` returns without doing
anything at all when the tab clicked is not stale. Switching is a repaint.

The import half was not a matching cost or an indexing cost. It was the cost of
writing the book. Measured in `scratchpad/books` on this machine, twenty SQLite
commits in the default mode:

| journal_mode | synchronous | 20 commits |
|--------------|-------------|-----------|
| DELETE       | FULL        | 16.0 s    |
| DELETE       | NORMAL      | 12.1 s    |
| DELETE       | OFF         | 1.96 s    |
| TRUNCATE     | NORMAL      | 11.7 s    |
| WAL          | NORMAL      | 0.002 s   |

So roughly a second per save, and accepting an import saves repeatedly. The
suite showed the same thing from the other end: one Books scenario took 100
seconds on its own and the whole run had been going for twelve hours.

**Decision: `PRAGMA journal_mode = WAL` and `PRAGMA synchronous = NORMAL`, set
on every book as it is opened or created** -- `store::detail::make_it_quick` in
`src/store_sqlite.h`, called from both `Book::open_at` and `Book::create`.

Two things this gives up, and neither seemed worth 16 seconds:

- `synchronous = NORMAL` is durable against the program crashing but not
  against the machine losing power in the middle of a commit, in which case the
  last transaction can be lost. It cannot corrupt the book -- the log is
  replayed or discarded whole. `OFF` was measured too and is not the answer: it
  is still 1.96 seconds, so it gives up the guarantee and buys nothing.
- A book in WAL mode has two sidecar files, `.sadb-wal` and `.sadb-shm`, while
  it is open. SQLite folds them back in and deletes them on a clean close, so a
  closed book is still the one file that `Books.spectable` says a book is. The
  case that is not covered is copying a book while the program has it open --
  the copy would be missing whatever is still in the log. Nothing in the
  specification promises that, but it is the sort of thing somebody does with a
  backup script, so it is written down here.

If either of those turns out to matter more than the speed does, the change is
two lines in one function.

The other thing that came out of this: the suite was writing its books into
`C:\ProgramData\SimpleAccount`, the folder holding the real `pughkilleen.sadb`.
Nothing it did ever named the real book, but `tools/rebuild.sh` now exports
`SIMPLEACCOUNT_BOOKS` to `scratchpad/books` so a test run is not in that folder
at all.

## 10. The cross-book transfer scenario asks for a rejection the register does not do

`$TBD Scenario A transfer may not name an account in another book`
(`Books.spectable:220`) commits a register line in Personal with the category
`Expenses:Consulting Fees`, which exists in Business, and expects:

```
Then rejected because : Rejection
| Reason                                                  |
| Expenses:Consulting Fees is not an account in this book |
```

`reg::commit` (`src/register_entry.h:90`) does not reject it. A category typed
into the register that is not in the chart is **created**, and the reasoning is
written into that function: "a person typing a new category has said what they
want and refusing it would mean two operations for one intention." That rule is
live and green across `TransactionRegister.spectable` -- it is not an
unimplemented corner, it is the register's settled behaviour.

So the two blocks disagree, and the disagreement is not about scope. Both
outcomes keep the books separate: the Books scenario wants the typed path
refused, and what actually happens is that a *Personal* `Expenses:Consulting
Fees` is created, with Business's account never consulted and never touched.
The separation the scenario is making a point about holds either way. What
differs is only whether a mistyped path is a refusal or a new category.

**Decision: the scenario stays `$TBD` and no glue was written for it.** Making
it pass means one of two things, and both are yours to choose:

1. **Keep the register rule and amend the scenario** to assert what separation
   actually means here -- a category created in Personal, Business unchanged.
   This is the smaller change and the one I would pick: the register rule has
   more specification behind it and a clearer reason.
2. **Keep the scenario and narrow the register rule** so that a path whose
   *parent* exists is created but a path naming an unknown top-level group is
   refused. That is a real rule and it would catch typos, but it is new
   behaviour, it needs its own scenarios, and it would change tables that are
   green today.

I did not pick for you because this is a rule about what the program does, not
a defect in how it does it. Writing glue that asserted the rejection would have
meant either a failing test for behaviour nobody has decided on, or changing
`reg::commit` out from under the register's own specification.

## 11. Condition 2 of the transfer rule was stated and not enforced

`BusinessRule When two new transactions are proposed as one transfer` lists
five conditions, and the second is *They are in two different accounts, and
both accounts are real*. `ofx::decide_multi` only ever checked the first half
of that: two halves in different accounts, equal and opposite, within the
window. Nothing asked whether either account was real.

**Decided:** the check is added, because the rule says so. A mapping that
points a download at a category -- which the import dialog allows, since the
account column is a path -- would otherwise pair that half with a bank row and
offer to turn an expense into a transfer. A path the chart has never heard of
is treated as pairable, because that is an account the import is about to
create rather than a category.

The cost is one `ref_for_path` per new row per pairing attempt. Nothing in the
three test files changes, which is the point: the condition was true of every
real file already and the code was relying on it rather than checking it.

## 12. `as previous` in a file with a Background, again

`Scenario A payee report for one category only` read

```
Given the chart of accounts is as previous
Given transactions are as previous
```

and came out with no rows at all. CLAUDE.md describes exactly this: because
`Reports.spectable` has a `Background`, `as previous` resolves to the
*Background's* chart and transactions, not to the two tables immediately above
it. The Background has no `Expenses:Home:Garden` and no Home Depot in it, and
it has no split lines at all -- which the scenario needs, since the garden is
one line of a three-line split.

**Decided:** the three tables are spelled out in the scenario, with a comment
saying why. This is the fifth time the construct has cost time here. The five
files that have a `Background` were left alone in the earlier pass on the
grounds that inlining would change meaning; that was right in general and
wrong for this scenario, where the meaning the author wanted *is* the tables
above. Worth a pass over the other `as previous` uses in those five files
looking for the same thing: a scenario whose own tables sit directly above it.

## 13. `csv_header_matches` stays, and `csv_profiles` is rewritten

The `csv_profiles` table predates the present shape of `csv::Profile` and did
not fit it: a `decimal_comma` column the struct has never had, and nothing for
`skip_rows`, the delimiter, the nine column names, the amount style or the
outward sign. Nothing read it or wrote it, so nothing noticed -- until "A CSV
profile set up for one book is not in the other", which needs profiles
actually stored per book.

**Decided:** `csv_profiles` is rewritten to the struct and carried through
`write` and `read`; the schema version goes 15 -> 16, and with no migration
path an older book is refused, which is what the version rule already says.

The first plan was to drop `csv_header_matches` with it, on the grounds that
the headings a profile expects are now the column names in `csv_profiles` and
a second table held the same facts twice. It stays: the specification names it
in the `EntityStorage` table in `Books.spectable` and asserts it exists in the
per-book scope rule, and both are live tests. A table the spec says the book
holds is not mine to delete. If it should go, the two spec rows go first.

## 14. The strip that says something long is going on, without a thread

`progress.h` is green against the noticeable-time rule in
`UserInterface.spectable`, and what was left of item 17 was the painting of it.
The strip is now in the status bar -- a label and a bar, hidden until
`progress::shows()` says a second has gone by -- and it is driven from the one
loop in the window that is both countable and writes nothing: filling a
register's cells, which is the thirty-thousand-line wait a person actually sits
through.

Two parts of the rule are not met, and both come down to there being no thread
in this program:

- **The opaque imports say nothing.** `qif::Importer::run` and
  `review::from_file` are each one call, so there is no point inside them to
  report from. Reporting through them means a tick callback in the reader
  itself -- `run(text, std::function<bool(long long)>)` -- which is production
  code with a spec of its own. **Decision:** not added here. The import of a
  twenty-year QIF is the slowest thing in the program and is the one that most
  wants a strip, so this is the first thing to do when item 17 is picked up
  again.
- **There is no Stop button.** The work runs on the thread that would deliver
  the click, so a Stop drawn on the strip could not be pressed. A button that
  does nothing is worse than no button, so it is left off.
  `advanceProgress()` already returns whether to carry on and the callers
  already read it, so the day the work moves off this thread the button is the
  only thing to add. The events pumped while the strip is up are repaints only
  (`QEventLoop::ExcludeUserInputEvents`): a click answered against a
  half-filled table would be answered against a register that does not exist
  yet.

## 15. The other half, asked from the register, over a two-row transfer

Item 22 is wired: naming a real account in the destination column now looks for
the other half with `transfers::halves_in` and merges with `transfers::merge`,
both of which are green.

One thing had to be decided on the way. `UserInterface.spectable` says of
answering none that "the one transaction already names both accounts and so is
already an uncleared line in the other register", which describes a transfer as
one row. `reg::commit` makes two -- `register_entry.h:147`, "each clears on its
own account's statement, which is why it is not one row with two accounts on
it" -- and that is itself specified and green.

**Decision:** the two-row shape stands, and the question is asked over it. The
line that was in the other register is gone, which is what the rule says; the
row this commit made for that same account stands in its place; and the cleared
status comes across onto *that* row rather than onto the row in the register
being typed into, because the statement that cleared the line being replaced is
the other account's statement. The merge itself -- the import ids, and cleared
if either half was -- is `transfers::merge` unchanged.

If the one-row shape was what was meant all along, the thing to change is
`reg::commit`, not this; it is the same decision in both places and it is
written down in only one of them.

## 16. A drag of a column divider wrote the whole book, once per pixel

`sectionResized` fires for every pixel of a drag, and the lambda wired to it
called `applyColumnWidths()` and then `save()`. A save hashes every small table
and compares 37,460 transactions against what is stored, so a divider moved
forty pixels was forty of those. That is item 2 of the change list -- "altering
size of columns takes a long time" -- and it is not the saving being slow, it
is the saving happening forty times.

**Decision.** The width is recorded as the drag goes, so the column keeps
following the divider, and the book is written once, when the drag stops. Qt
gives no "drag finished" signal on a header, so the seam is explicit:
`Workspace::resize_column` records, and `Workspace::finish_resize()` reports
whether there is anything to write. The UI calls the second one when the drag
has stopped, which in a widget toolkit that will not say so means a short timer
restarted on each resize.

Two scenarios in `UserInterface.spectable` state it: three widths reported and
one write, and a drag still going with no write at all. `BookWrites` counts
writes in a table, which is a measurement in a specification and wants a word:
how *often* the book is written is a fact about the program, not about the
machine, and it is the fact that was wrong here.

**Not decided here.** The write is still a whole-book write. A widths-only
write would be better and is a bigger change to `store_sqlite`; one write per
drag is already the difference between a second and nothing.

## 17. The category picker read the whole book once per candidate

Item 4 -- "creating the category selection dialog takes a long time; doing a
simple recategorization takes a long time". `chart::offered_for_typed` filters
candidates and then, for each survivor, calls `last_used`, which is a linear
scan of every posting in the book looking for the newest date on that account.
On a chart of a few hundred categories and a book of 37,460 transactions that
is millions of comparisons, and `filter()` is wired to `textChanged`, so it
happened again on every keystroke. The Debug binary makes it worse, which is
what the author is running.

**Decision.** The dates are read together, in one pass over the postings, into
a map from account path to its newest date, and `offered_for_typed` reads its
answer out of that map. The pass happens when the picker opens; a keystroke
does none, because nothing can post to an account while somebody is typing.

What is offered does not change by a line, which is why the existing scenarios
are untouched -- they are the regression test for this. The new scenario in
`Accounts.spectable` states the one-pass read on its own terms, including the
case the map has to keep: an account in the chart that is in no posting at all,
which is `never` rather than a date.

## 18. Three balances at the top of the register, not two

Item 7 asks for "the current balance as computed and the last balance
downloaded". Taken literally that is two figures, and comparing them would show
a difference nearly all the time: a cheque written this morning is in the
register and not at the bank, which is not a disagreement with anybody.

**Decision.** Three figures. **Balance** is every transaction, which is what
the money is. **Cleared** is only what is marked cleared, which is what the
bank should agree with. **Downloaded** is what the last download said, with the
date it was for. **Difference** is Cleared less Downloaded, so zero is the
ordinary case and anything else is worth looking at. All four in the display
sign of the account, so a card reads the way its statement reads.

The downloaded figure is kept on the account -- `DownloadedBalance` and
`DownloadedBalanceAt` on `Entity Account` -- written by the import and never
typed. A download older than the one already recorded does not replace it:
files arrive out of order and only the latest is worth the space. That is a
schema change, so the version goes to 17 and, as decided before, there is no
migration: an older book is refused.

## 19. The on-line balance is a dialog field for every format, not only CSV

Item 7 also asks for an "On-line balance" entry on the import, for CSV files
that state none. Rather than a second, CSV-only dialog, the field is on the
confirmation dialog that every import already shows: filled in from LEDGERBAL
for a QFX and correctable, empty for a CSV and a QIF and optional.

**Decision.** Empty means no answer, which is not zero: no check is made and
the balance already on the account is left alone. A date goes with the figure,
defaulting to the latest date in the file, because a balance with no date
cannot be compared with anything. What is typed is used for exactly what the
file's own figure is used for -- the check at the end of the import, and the
balance written onto the account.

This closes the deferred question in the memory note about CSV online balances:
the dialog it was waiting for is this one.

## 20. Item 10 was already specified and already written; it was never called

"The first time a QFX file is read, and the account is confirmed on the dialog,
copy the bank id and the account number into the account and save it."

`ofx::teach_account` does this, `ImportOfx.spectable` specifies it in three
scenarios, and all three pass. Nothing in `app/` calls it:
`confirmImportAccount` shows the bank id and the account number as labels and
throws them away. The only caller is the generated glue.

**Decision.** Called from the confirmation, before a row of the file is read,
and the chart written immediately -- so the answer survives the import being
cancelled a moment later, which is the likeliest outcome for the first import
into a new account. Two scenarios in `UserInterface.spectable` state it at the
UI level: a confirmation that teaches writes once, and one that disagrees with
what the account already says writes nothing at all. The rule about what is
filled in and what is left alone is ImportOfx's and is not restated.

## 21. Editing a category offered one field out of five

Item 5. `editCategorySettings` put up a dialog with Alias on it, so a category
entered under the wrong parent, or named badly, could not be corrected -- it
had to be added again elsewhere and the old one hidden, and the transactions
stayed on the old one.

**Decision.** The category dialog asks what the adding dialog asks: Name,
Under, Type, Alias, Hidden. Renaming and moving are the chart's own operations
and are already specified -- a rename rewrites the paths beneath it, a move
carries the subtree, and a transaction names its category rather than the text
of a path, so neither touches any transaction. The dialog is where they are
reached for a category, which was the only kind of node with no way to reach
them.

Type is asked because a category entered as an expense when it was income reads
as the wrong sign on every report that covers it.

## 22. A category is added from the listing, with no parent meaning its root

Item 6. The only way to add a category was the account dialog, which offers the
three category kinds at the bottom of a list of seven account groups and then
puts the new category somewhere the listing has to be reopened to see.

**Decision.** A button at the top of the category listing, and the new row
appears in the listing without it being reopened. The dialog is the one from
decision 21, minus Hidden. A category added with no parent goes under the root
of its own kind -- `Income`, `Expenses`, `Equity` -- rather than becoming a
root of its own, because a category outside those trees is invisible to every
report there is. The root is created if the book has not got one, which is the
same rule as a missing parent anywhere else in the chart.

## 23. The category on a payee rule was a string nobody checked

Item 3. `RuleForm`'s category was a `QLineEdit`. A rule carrying
`Expenses:Grocerys` matched every visit to the shop and then wrote a category
that does not exist, so the rule looked right in the list of rules and the
transactions went on arriving in Uncategorized, with nothing to say so.

**Decision.** The field is a button onto the same picker Recategorize opens --
`CategoryPick`, one picker in the program, reached from a second place -- and
what comes back is a path that is in the book. Left empty is still how a rule
says it tidies the name and leaves the category alone, so the picker can be
cancelled and the field stays as it was.

## 24. Applying the payee rules to a selection

Item 8. Applying the rules to the whole book is right after a long import and
wrong while working through one statement: a rule written a minute ago is meant
for the twenty lines in front of somebody, not for twenty years, where it will
also rewrite transactions that were already right and that nobody asked about.

**Decision.** `Apply payee rules...` on the register's context menu, over the
current selection. It is the same pass over the same rules in the same order,
reporting the same three counts through the existing `RuleApplication` -- only
the set of transactions it is given is different. Nothing about how a rule is
applied changes, and the whole-book pass in `Payees.spectable` is untouched.

## 25. Three smaller decisions taken while writing these eight

**The category-settings scenarios are stated over the chart, not over the
categories table.** `Given the categories are` fills `chart::Category`, which
is the old listing's own table; `category_settings` and
`apply_category_settings` read `chart::Account`, because a category is an
account in the same tree. The four scenarios of "what is kept about a category"
were written over the first and would have asserted against an empty table.
They are stated over `ChartOfAccounts` instead, and a rename or a move is
checked with `Then the chart of accounts is : ChartOfAccounts CompareOnly` over
Path alone -- a presence check, which is what a move is: the path is there and
the old one is not.

**The drag settles after 400 ms.** Decision 16 left it as "a short timer". 400
ms is long enough that a divider dragged in two movements is one write and
short enough that nobody notices the wait before the column lays out. It is one
number in `MainWindow` and not in the specification, which states only that a
drag writes once.

**A file is asked whether it stated a balance, not what its balance is.**
`ofx::Statement` carries `ledger_balance` and `ledger_balance_at` as a `Money`
and a `Date`, both of which have a value before anything has been read, so
there is no way to tell a stated zero from a file that said nothing. A balance
of zero is a real balance, so `numbers_in_file` looks for `<LEDGERBAL` in the
text and reports nothing when it is absent. The alternative -- a `bool stated`
on `ofx::Statement` -- is the better answer and is a change to the reader and
its own scenarios; this one is in the place that asks the question.

## 26. Two rules looking for the same thing

Reported from the running program: applying payee rules raised `UNIQUE
constraint failed: payee_rules.pattern, payee_rules.match_type`, and so did
dragging a column divider afterwards.

**What it was.** `payee_rules` is keyed on `(pattern, match_type)`, and
`payees::add` checked only that a rule had something to look for and a payee
name. A second rule with the same pattern and match type went into the
in-memory list, and every write of the book from that moment on failed -- the
save the add itself made, then the save behind anything else, which is why it
read as a fault in applying the rules and then in moving a column. Nothing
reached the file, so the book on disk was never wrong and a restart cleared it.

**The decision: refused where it is typed.** `payees::add` and `payees::change`
now refuse a rule whose pattern and match type are already taken, with "a rule
already looks for X". The pair is what the list changes and deletes a rule by
and what the book stores it under, so it names a rule; a second one with the
same pair could not act anyway, because the first rule that matches wins.
`change` leaves the rule being changed out of the comparison -- it is the one
it would otherwise collide with.

**The comparison ignores case and surrounding blanks.** Matching ignores case,
so `SHELL` and `shell` look for the same thing and the second would never be
reached. This is stricter than the key the book uses, which is case-sensitive:
two such rules are storable and are still refused, because they are the same
rule. An older book holding both is left alone -- nothing is rewritten, only
further adds are refused.

**Not made defensive in storage.** `store::write` could drop duplicates
instead of failing, and deliberately does not: a write that quietly discards a
row would hide the next bug of this shape rather than report it. The gate is
where the rule is written.

## 27. A drag writes the widths and nothing else

Asked while the duplicate rule above was being fixed: is the book written when
a column width changes? It was -- `MainWindow::save()`, which is
`store::Book::write` over the chart, every transaction, every split and every
rule, to record one integer.

Decision 16 fixed how often that happened: a drag reports a width per pixel and
now writes once, after it settles. It did not change what that write is. On a
book of thirty thousand transactions the one write is still most of a second,
and -- as the duplicate payee rule showed -- a width change carrying the rest
of the book with it fails over anything else in the book that cannot be
stored. Widening a column has nothing to do with the payee rules and should
not be able to be broken by them.

**The decision.** `store::Book::write_column_widths` writes
`register_column_width` alone, by the upsert the whole-book write already used,
following `write_conversions` and `write_tags` -- each a table edited in one
place and written by its own call rather than ridden along on a save of the
book. `MainWindow::saveColumnWidths` calls it, and the settle timer calls that
instead of `save()`.

**Not in a scenario.** The specification says when the width is written and
which widths are recorded, both of which are observable. That the write is
narrow is said in the `Details` of that section and is otherwise a fact about
`store` rather than about what the program does -- the same judgement as the
400 ms in decision 25. `BookWrites` counts writes in the workspace, which is
not the layer where this distinction exists, so a scenario asserting it would
be asserting against the glue.

## 28. Splitting a transaction that is already in the register

Asked while running it: a split that came from a file is shown in a floating
list, but there is no way to make one. Recategorize puts the whole transaction
in one category, which is the wrong answer for a payment covering three
things, and the only way to split anything was to type it in as a new entry --
which a downloaded row cannot be.

**The decision.** `Split...` on the register's context menu, opening the split
window the entry side already has, on the transaction that is selected.
`edit::split_transaction` writes the lines onto a transaction already in the
book: the destination becomes the split ref, the lines are checked against the
amount the transaction already has, and a category that is not in the chart yet
is created by sign, exactly as `reg::commit_split` does for a blank line. The
refusal is word for word the one the entry side gives -- *The split is 30.00
short of 184.50* -- because it is the same arithmetic and a second wording for
it would be a second thing to learn.

Nothing about the transaction's own side moves: not its amount, its date, its
payee or its cleared status. A split says where the money went, and the
register's side of it is settled before the window opens.

**One at a time, and not a split already.** The window checks its lines against
one amount, and two transactions have no one amount between them, so the item
is enabled only on a single selected line. A transaction that is already a
split is shown and not reopened -- the lines of an imported split are what the
file said, which the author asked for -- and the item is greyed rather than
left off, so that the answer to "can this be split" is in the same place either
way.

**Not asked for and not done.** Editing the lines of a split that already
exists. That is a separate question from being able to make one, and the
request was explicit that an imported split need not be changeable.

## 29. Reconciling an account, and where a balance adjustment goes

**The request.** A Cleared column on the register; a Reconcile option on an
account's context menu that checks the local balance against the on-line one;
uncleared items listed there with the option to clear them; and, where the
balances do not agree, a *Balance Adjustment* offered as a transaction to make
them agree -- set to not cleared, because a user will probably never clear it.

**The Cleared column was already specified.** `Attributes RegisterLine` in
`TransactionRegister.spectable` has carried a Cleared column from the start,
and *What each column sorts on* already says uncleared sorts before cleared.
What was missing was the column in the window, so the register went from nine
columns to ten and the prose that counted them was corrected. No new
specification was needed for that half of the request.

**The figures are the register's own two.** Reconciling asks exactly the
question the top of a register already raises -- the book's cleared balance
against the last downloaded one -- so `recon::figures_for` wraps
`reg::balances_for` rather than adding a second way of working out the same
three numbers. Agreeing means there is a stated balance *and* the difference is
nothing: an account no download has reached does not agree, because the
question has no second figure rather than the answer yes. The window opens on
such an account all the same and says so, which is the answer to what was
asked.

**The adjustment's other side is Equity, not an expense.** `Equity:Balance
Adjustments`, created on first use, the way `Equity:Opening Balances` is.
Nobody knows what the money was; a spending report that put it under Groceries
would be inventing an answer, and one that put it under Uncategorized would be
asking somebody to categorise something that cannot be categorised. Equity
keeps it off the spending reports and on the balance sheet, where an
unexplained difference belongs.

**Written uncleared, and what that means for the two figures.** The request was
explicit, so the adjustment is written uncleared -- and that has a consequence
worth stating: the **Balance** then agrees with the bank and the **Cleared**
figure still shows the difference, so the register goes on reporting it until
somebody clears the adjustment. That is the point of writing it rather than
quietly moving a figure: there is now a dated line, with a payee that says what
it is, standing for the difference nobody could find. Clearing it is the
deliberate act of accepting the figure.

**Sign.** The amount the window offers is in the display sign of the account,
which is the sign the person reading it sees: a book 50.00 above the bank is
offered -50.00. What is stored is the account's own sign, so a credit card --
shown with its sign turned over -- is adjusted in the direction the window
said.

**Greyed on a category.** A category has no statement and nothing to clear, so
`Reconcile...` is offered and refused rather than left off, for the reason
`Delete...` is: the menu answers the question either way.

**Where the scenarios live.** In `UserInterface.spectable` rather than a spec
file of their own, because reconciling is a window and its scenarios are about
what the window shows and what ticking in it does. The arithmetic underneath is
already specified as the register's two balances in
`TransactionRegister.spectable`.

## 30. Every balance in the 1/1/2026 report, against the book

The register for `Ken Checking - Wachovia 3112` on the real `pughkilleen` book
was reported as way off. It is, and the whole of the difference is accounted
for; so is every other non-investment difference against
`testdata/account balancesw 1 1 2026.pdf`. Investments were left alone, as
asked.

Of the 48 accounts the report names, 33 agree to the cent, 10 are Brokerage and
out of scope, and 5 differ. Each of the 5 is explained below to the cent. The
reading was done read-only against `C:\ProgramData\SimpleAccount\
pughkilleen17.sadb`; nothing was written to the book.

### What Ken Checking is off by, and why

At 1/1/2026 Ken Checking agrees with the report exactly (24,346.36), so the
history is sound and the damage is in 2026. At 9/30/2026 the book says
-23,138.89 and Quicken says 15,861.11 -- **39,000.00 apart**, and four
transactions account for all of it:

| Ref | Date | Amount | Payee | Went to |
|---|---|---|---|---|
| T37478 | 2026-07-31 | -3,000.00 | TO PUGH K CHECKING REF #OP0Z5NX4 | Expenses:Uncategorized |
| T37479 | 2026-08-26 | -30,000.00 | ONLINE TRANSFER TO PUGH K | Expenses:Uncategorized |
| T37480 | 2026-08-31 | -3,000.00 | TO PUGH K CHECKING REF #OP0ZL8TN | Expenses:Uncategorized |
| T37471 | 2026-09-30 | -3,000.00 | FROM PUGH K PRIME CHECKING REF # | Joint - Crown |

The first three each sit beside a transfer that was already in the book --
T6166, T6180, T6182, same date, same amount, same payee text -- and Quicken's
register report has exactly one row for each of those dates. The fourth is the
9/30 transfer read from both sides at once: T37471 from the Crown statement and
T37485 from the Checking statement are one 3,000.00 transfer entered twice
under two payee texts.

**Why the import did not see the first three.** `ofx::existing_in` is what the
duplicate test is given to work against, and it is one line:

```cpp
if (t.source_account != account_ref) continue;      // src/ofx_import.h:292
```

Only transactions whose **source** is the account being imported into are
considered. T6166, T6180 and T6182 were entered from the Crown side -- source
Crown, destination Ken Checking -- so none of them is in the list the test
looks at, and a statement row naming the same date, amount and payee is judged
`New`. Both importers go through that one function (`src/csv_import.h:562`,
`src/ofx_import.h:699`), so a CSV and an OFX fail the same way.

This is not the multiset-pairing rule doing its job. Two identical rows in a
file are two transactions, and that is settled. This is a transaction already
in the book that the test could not see, which is a different thing: the row
was never compared against it at all.

**The decision.** `existing_in` should offer the account's whole register, not
half of it: a transaction whose destination is the account is a line in that
account's register, the statement will report it, and it must be a candidate.
The amount a destination-side transaction contributes is the negative of what
is stored, which is the signed figure the register already shows, so
`e.amount.cents() == row.amount.cents()` keeps working unchanged. Split lines
whose destination is the account are the same case and belong in the same fix.
Specified before written, as everything here is -- so the next pass is
scenarios in `ImportOfx.spectable` for a transfer entered from the far side,
and only then the code.

### The other four

**Chase Sapphire 4391 (7253)** -- report -654.76, book -4,958.06, off
-4,303.30. One transaction: T6053, the January autopay of 4,303.30, dated
2026-01-02 in the book. -4,958.06 + 4,303.30 = -654.76 exactly. Quicken holds
the two halves as separate rows -- the card's own download says `AUTOMATIC
PAYMENT - THANK YOU` at the end of December, the bank's says `CHASE CREDIT CRD
AUTOPAY` on 1/2 -- so its card balance at 1/1 has the payment in it. The book
pairs the halves into one transaction and keeps the bank's date. The register
is otherwise identical.

**Wells Fargo Ken Visa 1333 (3932)** -- report -2,050.18, book -5,208.34, off
-3,158.16. The same thing: T6052, 2026-01-02, 3,158.16, and -5,208.34 +
3,158.16 = -2,050.18.

Both are a consequence of pairing, not a defect in it, and the book's single
dated transaction is the better record. What it costs is that an as-of balance
between the two dates disagrees with Quicken's. Nothing is changed for it; it
is written down here so the next reading of these two accounts is not
investigated twice.

**Transfer Account - Should be 0** -- report -10,001.00, book -55,428.30, off
-45,427.30. The report's figure is the book's own running balance at 2025-06-30.
Three rows come after it: T7493 (-5,000.00, 2025-10-14) and T7507 (+5,000.00,
2025-12-08), a pair that cancels, and T887 (2025-11-21, 45,427.30 into Cap One
Savings 0852, no payee), which is the whole difference. Cap One Savings agrees
with the report, so Quicken has the money arriving too and gives it a different
other half -- a category or an investment account, neither of which appears on
an Account Balances report. The importer put the far side of all three into
this catch-all account instead.

**Paypal - Payments and Charges** -- report 961.86, book 2,045.47, off
+1,083.61. 1,083.61 is credited to Paypal twice: T9247 (2020-03-19, `ONLINE
TRANSFER TO KEN PUGH,`) and T9265 (2021-01-08, `PAYPAL TRANSFER`), both from
Online KP Wachovia 3668. Online KP agrees with the report, so Quicken has both
rows and sends one of them somewhere other than Paypal.

**Amex Hilton Ken 1002** -- report -59.30, book -62.30, off -3.00. Three
December 2025 rows, netting +3.00:

- the card payment of 62.30 is in the book as T6045, 2025-12-23, from Joint -
  Crown to `Expenses:Uncategorized` -- so the card never received the credit.
  This is the same blind spot as Ken Checking's, read from the paying side: the
  payment's far half is the card, and nothing paired them.
- TMobile -55.00 and NYTimes -4.30 for December are absent from the book
  altogether, though every other month from 2025-08 to 2026-08 has both. The
  December statement for this card was never imported.

62.30 - 55.00 - 4.30 = 3.00.

### What this says about the import as a whole

1,038 transactions in this book land in an `Uncategorized` account. Most are
old and were Uncategorized in Quicken too, so they cost nothing; the report
agrees with them. The ones that matter are where Quicken has a transfer and the
book has a category, and they share one cause with the Ken Checking duplicates:
the importer is not finding the far half of a transfer that is already in the
book. Groups worth looking at when that is fixed -- `CHASE CREDIT CRD AUTOPAY`
(7 rows, -30,960.83), `AUTOMATIC PAYMENT - THANK YOU` from the Wells Fargo card
(2 rows, 5,535.04), `Preauthorized Withdrawal to WELLS` (-45,000.00),
`AMICA INSURANCE debitpmt` (6 rows, -5,847.52).

Nothing in the book was changed. The four 2026 duplicates in Ken Checking are
still there and are still 39,000.00.

## 31. Questions on the replies in `questions with replies.md`

Twenty-nine replies, read one at a time. Most settle their question outright and
raise nothing; what follows is only where a reply opens something new or where
two replies pull in different directions. Each has a decision under it, so
nothing is waiting on an answer.

### a. One correction first: the matching was not working (reply 15)

Reply 15 says "It appears that the matching is working." For an import into an
account whose transfer had already been entered from the *other* account, it was
not. `ofx::existing_in` only ever offered transactions whose source is the
account being imported into, so the far half was never a candidate and the
statement row came in as `New`. That is the 39,000.00 in Ken Checking
(section 30) and the 62.30 on the Amex. It is now specified -- "A transfer
entered from the other account is a duplicate in this one",
`ImportOfx.spectable` -- and fixed. The rest of reply 15 stands: matching the
two sides of a transfer as the second statement is imported does work, and what
was missing was the case where the transfer was entered by hand first.

### b. Cleared on the far side of a transfer (replies 15 and 29)

Both replies say the two sides of a manual transfer are uncleared, and that is
the sharpest thing in the set, because a transaction carries one cleared status
and shows in two registers. So an imported, cleared row that the user then
categorises with an account would show as cleared in the other account's
register too, although that bank has confirmed nothing. A far-side *split* line
is already forced to Uncleared (`src/register_lines.h`), a far-side transfer
line copies the status, and nothing written down says which is right.

**Decision.** Cleared belongs to the register the transaction lives in. A
far-side line always shows Uncleared, which makes the transfer rule agree with
the split rule it sits beside, and a manual transfer is then uncleared in both
registers without the far account having to hold a row of its own. To be written
as a scenario in `TransactionRegister.spectable` next to the membership rule.

### c. What the balance adjustment account is, and how it clears (reply 29)

"Balance Adjustment should be an account rather than an expense. Creating a
balance adjustment in a register also creates an entry in Balance Adjustment.
Both are uncleared (just like a manual transfer)." Two things follow that the
reply does not say. First, if the adjustment is uncleared in the account being
reconciled, that account's cleared balance still does not come to the statement
figure, so the reconciliation the adjustment was made for cannot close. Second,
"creates an entry" reads like a second row, where a transfer here is one
transaction seen in two registers.

**Decision.** One transaction, as everywhere else: it lives in the account being
reconciled and names `Balance Adjustment`, so it is a line in both registers
without a second row existing. It is created **Cleared** in the account being
reconciled -- that is the whole point of making it -- and by (b) it shows
Uncleared in `Balance Adjustment`, which is what the reply asks for in the place
the reply is about. The account is top-level `Balance Adjustment`, type
OtherAsset, created the first time one is made.

### d. The transaction editor dialog, and what is left in the register (reply 1)

A dialog that edits tag, memo, check number, cleared status and amount answers
the question. It leaves two: whether the cleared tick in the register column
stays clickable (it is specified and written), and whether the warning on
changing a cleared amount refuses or merely warns.

**Decision.** The tick stays -- reconciling is a column of ticks and a dialog
per line would make it unusable. Everything else belongs to the dialog. The
warning warns: Change anyway / Cancel, with Cancel the default. A split
transaction's amount is not editable there; that button opens the split dialog,
which is reply 28's dialog and already knows what the lines have to come to.

### e. The modeless progress dialog replaces the strip (replies 3 and 14)

**Decision.** A modeless dialog naming the job ("Importing <file>...",
"Opening <book>...") is shown before a long job starts and dismissed when it
ends. No Stop button in it yet: stopping needs the import off the UI thread,
which is TASKS item 17 and still owed. The `ProgressStrip` blocks in the spec
are retagged `$TBD` rather than deleted, because the dialog needs the same two
facts the strip did and they are already written down.

### f. Percentage splits leave the specification (reply 6)

"There does not need to be anything as a percentage split. Every split is based
on the amount which the user types."

**Decision.** The percentage rounding rule and its worked table come out of the
spec rather than being corrected -- a rule nobody will implement is worse than
no rule. A remembered split therefore remembers amounts. Offered against a
transaction with a different total, the amounts are offered unchanged and the
difference is left unassigned for the user to type over; the split dialog
already says what is unassigned.

### g. A payee on a split line (reply 7)

A split line carrying its own payee, for a deposit slip with several payers,
with the payee rules not applying to it.

**Decision.** The column goes on the split line. A category register's line for
that split shows the line's payee where it has one and the transaction's payee
otherwise, which is the only way the deposit is readable from the category side.
Payee reports read it the same way. No rule is applied to it, no payee record is
created from it, and `payees::convert` never sees it.

### h. May an import create a *category* (replies 10 and 11)

"An import should not create an account" is clear, and is about accounts. Reply
10 is about a category the user types into a register, and says an error dialog
rather than a new category. Neither says what the importer does with a category
it has no account for -- and today's imports create `Expenses:Uncategorized` and
have put 1,038 transactions in it.

**Decision.** An import creates no account, ever: a file naming an account that
is not in the chart is refused before a row is read. It does create categories,
because the money has to land somewhere and refusing a whole statement over a
category nobody has typed yet would stop the work. The import summary says which
categories it created. The register is reply 10's rule and is stricter: an
unknown category typed into a register line is refused with a dialog, and the
category list grows only from its own Add.

### i. Deleting a category that is in use (reply 23)

"Deleting a category should delete the payee rule, so that non-existent
categories are never assigned", with "(and possibly reassign all the
transactions to another category)" left open.

**Decision.** Deleting a category no transaction names deletes it, and any payee
rule naming it goes with it. Deleting one that transactions name asks for a
replacement category and moves them, because the alternative is transactions
pointing at nothing. The payee rules naming it are deleted either way rather
than repointed at the replacement: a rule is a decision about where money goes,
and inheriting one silently is how a chart comes to disagree with itself.

### j. Seeding a new book's CSV profiles (reply 13)

Per-book profiles with user-assigned titles, chosen or created when a file is
read, and the header matching done by the user. That leaves the shipped profile
list: a brand-new book would have none, so the first import of every file would
begin with a mapping dialog.

**Decision.** A new book is seeded with the shipped profiles under their present
names, so the first import of a known bank's file still just works, and the
chooser lists them like any other. `csv_header_matches` stays as the thing that
suggests which profile to pick.

### k. `Define` and `=Define` in place of `as previous` (reply 12)

Agreed, and already the practice here: the six files without a `Background` are
spelled out. The one thing to check before leaning on the table-form `Define` is
whether 0.9.2 resolves `=Name` across the files given as `--context`, which is
how this folder is generated. **Decision.** Spell a table out where it is used
once; use `Define` only where the same table is wanted by several scenarios of
the same file, until a cross-file `Define` has been seen to work.

### l. The replies that say "I will check this"

19 (the online-balance field for every format) and 21, 22 (the category editor's
five fields, and a category added with no parent meaning a root). Nothing is
waiting on them; they are written and specified. Listed here so they are not
read later as open questions.

### m. A transfer entered as two rows, in two scenarios written before the rule

Found on 2026-10-23 while clearing the eleven failures left by the two commits
that made a transfer a line in both of its registers. Two things in the
specification still describe the old shape, where a transfer was a row in each
account and each register showed only its own:

- `UserInterface.spectable`, *Going to the chosen register of a transfer opens
  that one*, entered the Visa payment twice -- T2 in the current account, T3 in
  the card -- and expected the card's register to hold two lines. Under *a
  transfer is a line in each of the two registers it names*
  (`TransactionRegister.spectable`) those are two movements of money, so the
  card's register holds three lines and nothing is wrong with the program.
  The scenario immediately above it, which offers both registers and opens
  neither, already uses the one-transaction form.

**Decision.** The scenario now has the same Given as the one above it and opens
T2 in `Liabilities:Visa`. A transfer is one transaction; which of its two
registers it is read in is the reader's choice, and that is the whole of what
the scenario is about. T3 was never a second movement, only the old way of
drawing the first one.

- `UserInterface.spectable`, *Answering yes makes the two halves one transfer*,
  was right and the program was wrong: accepting the import rewrote the half
  already in the book to name the import's account but recorded no match, and
  an unrecorded pair is indistinguishable from two accounts paying each other
  both ways -- so the money showed in the current account twice. Accepting now
  joins the two through `ledger::match_destinations`, which is the one place
  that points each destination at the other and records each id on the other.

**Decision.** Where the paired row was not ticked there is no second
transaction to join, and the half already here is left naming the account the
answer gave it, exactly as before. That case has no scenario; it is written
this way so that answering yes and then unticking cannot leave a match
pointing at nothing.

## 28. The thread the two unmet halves of item 17 were waiting for

Question 14 above left item 17 part done and said exactly what was owed: a tick
callback in the readers, and the work off the thread that paints. Both are now
written, so this records what was decided while writing them. No spec table
changed: the noticeable-time rule and its four scenarios were already untagged,
generated and green -- they state what the strip says, and what a thread is for
is not reachable from a test that has no window.

- **What a reader is handed.** `progress::Ticker` is
  `bool(const std::string& what, long long done, long long total)`, returning
  false for stop. The `what` is in it rather than fixed by the caller because a
  reader does two things in turn -- a QIF reads its records, then works out the
  transactions -- and each has its own count. Without it the bar would fill
  twice under one set of words, or the second half would report nothing. The
  unit stays the window's, settled before the work starts, because it is a word
  about the counting and not about the work.
- **A reader with no ticker runs as it did.** `progress::carry_on` takes an
  empty ticker for yes, so the command line, `ui::Workspace` and every test are
  untouched; the parameter is defaulted at the end of the three
  `review::from_file` signatures and is a setter, `reporting_to`, on
  `qif::Importer`, in the style of `as_a_statement`.
- **Stopping is said in the result, not thrown.** `qif::Imported` and
  `review::Prepared` gained `bool stopped`, and a stopped read returns where it
  was. Nothing is undone because nothing was written: reading and saving are
  separate, and all three callers return before the saving. The register says
  "Reading stopped; the book is as it was", which is the recalculation
  scenario's rule applied to an import.
- **Where the ticks are.** Every two hundredth record, because a record is
  cheap and the ticker crosses a thread: `read_records` and
  `build_transactions` in the QIF importer, and the row loops of `from_ofx` and
  `from_csv`. **Decision, and a limit worth knowing:** deciding which rows of a
  download are duplicates happens before the first row is made, and that is the
  slow half of a download. It reports nothing. So the strip shows the words and
  a bar that says only that something is going on until the rows start -- which
  is what the rule asks for in that case, a count with no total being no
  estimate, but it is a bar that sits still for most of the wait on a large
  file. Reporting from inside `ofx::decide` and `csv::decide` would mean a
  ticker in the matching itself, which is a third reader to change; not done
  here.
- **How the waiting works.** `MainWindow::runWithProgress` starts a
  `std::thread`, then waits in a `QEventLoop` with a 50 ms `QTimer` that reads
  the worker's `progress::Running` and paints the strip from it. The worker
  touches nothing the window paints: it has its own copies, and the ticker
  writes two numbers and a string under a lock and reads one flag back. A throw
  on the worker is carried out in an `exception_ptr` and raised on the thread
  that asked, rather than ending the program.
- **Nothing else is offered while it runs.** The menu bar and the whole central
  widget are disabled for the duration and Stop is the only live control, which
  is the rule's "nothing that would change the book is offered while the work
  runs" read literally. The register greys out while an import reads, which is
  heavier-looking than it needs to be but says the truth.
- **The button appears only where it would work.** `progressStopping_` is the
  worker's `Running` while there is one and null otherwise, and Stop is shown
  only when it is set. The one tick site that is still on this thread -- filling
  a register's cells -- therefore shows the strip with no button, as before, and
  `advanceProgress` still pumps repaints only for it. A button that does
  nothing is worse than no button, and that has not changed; there is now
  simply somewhere it does something.

## 29. `matching transfers.txt` read against what the program does

The author's note of 2026-10-23, checked line by line against the code and the
spec tables. Four of its seven paragraphs are already the program's behaviour;
three are not, and one of those three is a disagreement about the model rather
than a gap. Nothing was changed in response to it -- per CLAUDE.md the spec
comes first, and these are the author's calls.

**Agrees, already written.**

- *"Imports should be marked as cleared; manually input transactions should be
  marked as not cleared."* That is the arrives-cleared rule in
  `ImportOfx.spectable`, applied to CSV by the book's own setting and to a QIF
  read as a statement; `reg::Entry::cleared` starts `Uncleared` for a line
  typed into the register.
- *The pairing question on import.* `transfers::halves_for` pairs equal and
  opposite to the cent, in some other real account, within `kWindow = 2` days,
  not already paired, closest in days first -- so +/- 2 days, either direction,
  whichever account is being imported, exactly as the note asks. The question
  is the transfer question in `UserInterface.spectable`, answered Yes, Yes and
  remember, or No.
- *What the two registers show afterwards.* Account A's line reads Account B
  and Account B's reads Account A, as the note's tables have it.
- *Reconciling.* The dialog lists every uncleared transaction with a tick and
  "Clear ticked", and the Balance Adjustment is offered only for a difference
  that is still there once the ticking is done -- so a balance that matches
  when the uncleared rows are accounted for creates nothing, which is the
  note's last paragraph.

**Disagrees about the model, and the author should settle it.** The note says
that on Yes *the transactions* are changed -- two transactions, each naming the
other's account. This program's settled decision is that a transfer is **one**
transaction naming both accounts, shown as a line in each of their registers
(`TransactionRegister.spectable:86`, decision in `README.md`, and
`transfers::merge`'s own comment). So on Yes the two halves are merged: one is
kept with its date, payee and memo, it gains the other's cleared status if that
one was cleared and the other's import identifiers, and the other row is gone.
On an import pairing the same thing is done by recording a match -- `matched_to`
on both -- so the far side is shown as a duplicate rather than as a second row.
What a person sees in the two registers is what the note describes; what is
underneath is one transaction, not two. The difference matters for the note's
own next paragraph.

**Missing, and not specified anywhere yet.**

- *"Match this transaction" in the register's context menu.* The register
  offers "Go to the transaction..." and "Recategorize..." and nothing else.
  Pairing from the register happens only when a real account is typed into the
  destination column, and `transfers::halves_in` then looks in **that one
  account** -- not "potential matching transactions in all other accounts",
  which is `halves_for` and already written. So the menu item is a dialog plus
  a call that exists. Cancel meaning no change is free. **"Create a transaction
  in the other account" is new behaviour** and has no scenario: under the
  one-transaction model it is not a second transaction but naming the other
  account in the destination column, which is what the register already does
  by hand -- unless the author means something else by it.
- *Re-categorising one side of an existing match.* The note asks for the other
  side to go uncategorized with a warning dialog. Today nothing warns.
  `edit::assign_category` refuses a split and allows everything else, so
  "Recategorize..." on a transfer silently rewrites the destination from the
  other account to a category, which takes the line out of the other register
  and leaves any recorded match pointing at a transaction that is no longer a
  transfer. The bulk pass does not have this hole -- `payees::assign_categories`
  skips anything whose destination is not a category account, and skips
  Reconciled -- so the two paths disagree with each other, which is a defect
  whichever way the note is answered. **Decision:** written down here rather
  than fixed, because what should happen is the question: refuse, or warn and
  unpick the pairing. Under one transaction there is no "other one" to leave
  uncategorized, so the warning would say "this will take the line out of
  Account B's register".
- *"Transfer Accounts do not have imports."* The Transfer group exists and is
  deliberately off the balance sheet (`CoreTypes.spectable`,
  `src/account_type.h:328`), but nothing stops one being chosen as an import
  target. Unenforced, and worth a rule if it is meant to be one.

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

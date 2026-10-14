# To look at

Decisions that are the author's to make. Each is a question about what the
program should do, not a defect, and each is reachable from a test that passes
today — so nothing here is urgent and nothing here is lost.



## Here are some things to be sure are done (in order of importance)



- Every account / category has an id which is unique - simply record last id used in the book and increment it by one.   

- There is an account listing (from the menu) and a category listing (from the menu) which shows accounts and categories.   Clicking on one allows editing of the account/category. 
  
  The context menu for the account listing on the left side allows for editing the accounts 
  
  Accounts settings should have the starting balance, the date of that starting balance
  
  A "in progress " modeless dialog box should appear when things are being imported or recalculated. 

- The import window should be below the other window, not to the side of it.    
  
  If an import says "Possible", then a context menu on the item should show the possible current matches.  
  
  Possible bug:  I imported the same file twice.  It showed Matched on a couple of items the second time.  There shouldn't be any matches, they should be duplicates, as the transaction was already matched.  Isn't that correct?
  
  Every thing that has columns (e.g. registers and reports) should allow shrinking/expanding of the columns by moving the border of the column header.  If the total width is less than the width of the window, that is okay. These column widths should be the same for all registers and should be stored with the book.  
  
  Every transaction has a memo, which could be read from a CSV or a QFX file.   
  
  There should be a tag field on transactions.    Any words in this field are considered tags.   Tags should be kept in a table.  A list of all tags should be available from a menu.  When entering a tag in the field, after the first character, a list of matching tags, with a selection button (or using a click) should select that tag.   This is the same way entering the letters of a category / account work in finding the destination for a transaction.  
  
  There should be a find in the menu.  Two options - find a value (or range of values) looks for that value in all registers and in the split table.  Matching finds are shown in the bottom window.     The other option is a more detailed find - it shows all fields in a register and the user can enter values to be matched for that field.  There should be a field that says, "All registers or just current register"
  
  Registers should allow sorting by any column.   
  
  Registers should have a option that allows "Only show this years,  only show past two years" of transactions.  
  
  Books should have option - "Hide transactions before this date".  All registers will only show transactions after that date.   
  
  
  
     
  
  
  
  ## 1. A CSV import leaves postings uncleared; an OFX import clears them

`csv::transactions_for` leaves the posting in the statement's account
`Uncleared`. `ofx::transactions_for` marks the equivalent posting `Cleared`.

The specification is what settled it: the posting tables in the category
scenarios of `ImportCsv.spectable` leave the `Cleared` column out and are not
`CompareOnly`, so the default `Uncleared` is asserted. The reasoning written into
the code is that a CSV carries no statement and says nothing about what the bank
has settled, unlike an OFX file whose rows *are* a statement.

That may be exactly right, or the column may have been left out because those
scenarios are about categories. It matters for reconciliation: only cleared
postings count towards a stated balance, so a book filled from CSV files would
reconcile against nothing until someone cleared the rows by hand.

**If it should clear:** add `Cleared` to those tables in `ImportCsv.spectable`
and set it in `csv::transactions_for`.

My note:  CSV import should be treated as an OFX import - it sets the cleared flag.  There can be a configuration option that says "do not set cleared flag on CSV import". 



## 2. `Entity CsvProfile` cannot name a balance column

`CsvProfile` in `ImportCsv.spectable` names `DateColumn`, `PayeeColumn`,
`MemoColumn`, `CheckColumn`, `CategoryColumn`, `AmountColumn`, `DebitColumn` and
`CreditColumn` — but no `BalanceColumn`.

So a profile recognised by its signature can place the balance column, because
the alias table does it; a profile written out by hand in a table cannot. A bank
whose balance column has an unusual heading therefore cannot have its balance
checked, which is the strongest check a CSV file offers.

`csv::Profile` in `src/csv_import.h` already has a `balance_column` field, and
the signature and `columns_of` both use it. Only the specification's entity and
the storage rule in `Books.spectable` need the column added.



My note: add the column.   But if the column is not present or does not contain any data, then don't use it for checking.  

## 3. Two specification files were edited to make scenarios pass

Both are recorded because changing a specification to turn a test green is the
one move that can hide a defect rather than find one. Neither changed what the
program does.

### Payees.spectable: five tables gained a Cleared column

Five `PostingRow` tables said what an import produced but left out whether the
bank had settled it, so the default `Uncleared` was asserted while an OFX import
legitimately clears the side the statement is for. The scenario above them in the
same file already stated it, so the behaviour was never in doubt -- the tables
were simply incomplete.

Each now reads `Cleared` for the account the statement is for and `Uncleared` for
the category, with the reason in the file: the bank has reported one side and
nobody has reported the other. This adds assertions rather than removing them,
which is the safer direction.

It also bears on question 1 above: these tables settle what an OFX import does,
which strengthens the case that the CSV tables leaving the column out was an
oversight. The CSV behaviour has deliberately not been changed on that inference.

My note: there is no need for a cleared on a category.   Postings should be gone by now.  Is it not?  



### TransactionRegister.spectable: one table marked CompareOnly

In `TransactionRegister.spectable`, `Scenario Two transactions on one date keep
the order they were entered in` now reads `Then register lines are : RegisterLine
CompareOnly`.

Without `CompareOnly` the columns the table does not name are asserted to be
their defaults, so `Category` would have to be empty — and a register's category
column holds the other account of the transaction, which the first scenario in
the same file asserts. The scenario is about the order of the lines, so comparing
only what it names is the smaller change.

The alternative is to list `Category` in that table instead. This is the only
`.spectable` edit made to turn a test green, which is why it is written down.

My note: I think the default for a transaction destination should be "uncategorized" (I think that's the correct one).   But I think CompareOnly is fine here.

## 4. A matched or completing row does nothing when accepted in the window

`sa_book import --accept` calls `ofx::apply_matches`, which clears the
transaction a downloaded row matched rather than adding a second copy of it. The
window does not: `review::from_ofx` shows the row with disposition `Matched` and
leaves it unticked, and accepting the review adds the ticked rows only.

So nothing is duplicated and nothing is lost -- but a check entered by hand and
then seen on a statement stays `Uncleared` when the statement is imported through
the window, where the command line would have cleared it. That matters for
reconciliation, which is the acceptance test for this program.

The same is true of a payment's other half. `ofx::decide` calls such a row
`Completes`, and `ofx::apply_completions` fills in the account the payment went
to on the transaction that was waiting for it. The window does neither, so a card
payment imported from both sides through the window leaves the bank side sitting
in `Unassigned` -- which is exactly the case the two-unknowns rule was written
for. The register can still be used to assign it by hand.

Doing it properly means the review knowing which transaction each row claimed,
so that accepting can clear that one: `ui::ReviewRow` would carry the claimed
id, `UserInterface.spectable` would gain a scenario for a matched row clearing
rather than adding, and `MainWindow::acceptImport` would ask `ofx::apply_matches`.
It was left out rather than half-done, because a window that silently cleared
rows nobody ticked would be worse than one that clears nothing.



My note:  I think this is fine.  Now when a transaction destination is changed to an account, then potential matching transactions in the other register that currently do no have a destination should be shown.   The user should be allowed to select one to match (or select none), in which case a new transaction - uncleared - will appear in the destination register. 

## 5. Redoing a report or a register after one change may be re-reading more than it needs to

Raised by the author, not yet investigated: editing a single transaction
appears to cost noticeably more than editing one row should, which smells like
the report or register rebuilding from the full on-disk set rather than from
whatever is already in memory.

To look at before changing anything: where `MainWindow` currently gets its
transactions from on a recategorise/edit/delete (`store::Book::read()` again,
or the in-memory `transactions_` vector it already keeps); whether
`ui::Workspace`/the report path recomputes from that same vector or re-reads
the book; and whether the cost is in SQLite I/O, in rebuilding `ledger::Ledger`
balances from scratch, or in Qt table repopulation.

The author's proposed direction, to weigh once the above is known: keep the
authoritative in-memory copy (sortable/filterable) that the UI always reads
and writes, and treat SQLite purely as what that copy is written through to on
an add or edit, rather than a thing re-read on every change. `MainWindow`
already holds `transactions_` and `chart_` in memory, so this may be partly in
place already -- the open question is which paths bypass it.

My note:   Do this at the end as an optimization  

## 6. CLAUDE.md names a helper this project does not have

CLAUDE.md says a cell holding a leading or trailing blank is wrapped in double
quotes in the spec and unwrapped by calling `spec_cell::unquote()` in the glue.
There is no `spec_cell` namespace anywhere in this checkout -- it is presumably
in the AlignThree example tests.

`generated/payees_glue.h` now defines a local `unquoted()` for the two refusal
scenarios that need an empty pattern and an empty payee. If AlignThree does
provide one, the glue should use that instead and the local copy should go.

My note: already have said to use ~ as a space.  There is no need for quotes in spectable cells.  (There is an exception, but that does not apply here)

## 7. A future, optional split window at the bottom of the register, shared by imports and a not-yet-specified Find

Raised by the author for later, not yet specified or started.

An optional panel at the bottom of the register, shown for two different
purposes depending on what is open above it:

- **For an import**, it shows the imported transactions -- the same thing the
  import review already shows today.
- **For a Find** (a feature that does not exist yet), it shows the
  transactions the find matched.

**Find** would be a modeless dialog, so it can stay open while the register is
used, with:

- A text string, matched against the payee or the memo.
- A range of dates.
- A range of amounts.
- A source or destination account/category.
- A choice of "search current register" or "search all registers".

Nothing here has a spec table yet -- per CLAUDE.md, that comes first, and
specifically the Find feature itself has not been designed, only sketched.

My note: Find has been specified at the beginning.

## 8. Reports, and every other window, should have a scroll bar on the right

Raised by the author for later, not yet specified or started.

A report with more rows than fit on screen, and any other window in the same
position, should show a scroll bar on its right-hand side rather than clip or
require resizing the window.

My note:  this should be added.   

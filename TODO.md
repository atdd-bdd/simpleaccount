# To look at

Decisions that are the author's to make. Each is a question about what the
program should do, not a defect, and each is reachable from a test that passes
today — so nothing here is urgent and nothing here is lost.

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

## 5. CLAUDE.md names a helper this project does not have

CLAUDE.md says a cell holding a leading or trailing blank is wrapped in double
quotes in the spec and unwrapped by calling `spec_cell::unquote()` in the glue.
There is no `spec_cell` namespace anywhere in this checkout -- it is presumably
in the AlignThree example tests.

`generated/payees_glue.h` now defines a local `unquoted()` for the two refusal
scenarios that need an empty pattern and an empty payee. If AlignThree does
provide one, the glue should use that instead and the local copy should go.

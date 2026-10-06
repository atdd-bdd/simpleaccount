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

## 3. One table was marked CompareOnly to make a scenario pass

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

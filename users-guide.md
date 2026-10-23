# SimpleAccount — a user's guide

SimpleAccount keeps household and business books: accounts, categories,
transactions, imported statements, reconciliations and reports. It is a
replacement for Quicken rather than an imitation of it, and the differences are
deliberate. This guide says what the program does today and how to do each job
in it. Where something is specified but not yet built, it says so rather than
leaving you to find out.

Two ideas run through everything and are worth reading before the rest.

**It is double entry.** Every transaction has two sides and they sum to zero.
Debits are positive for every kind of account, so money leaving your checking
account is negative in checking and positive in the category it went to. You
are never asked to think in debits and credits -- the register asks for a
Payment or a Deposit -- but that is what is underneath, and it is why a book
cannot quietly go out of balance.

**Categories are accounts.** `Expenses:Groceries` and `Income:Consulting` live
in the same tree as `Assets:Checking`, and the Category column of a register
holds the other side of the transaction, whatever kind of thing that is. So
naming a real account there is a transfer, and naming a category is a spend or
a receipt. There is one mechanism, not two.

## Where a book lives

A book is a single file, `<name>.sadb`, in one machine-wide folder:
`C:\ProgramData\SimpleAccount`. The program lists the books by looking in that
folder, so copying a `.sadb` in is enough to make it appear, and copying one
out is a backup. `SIMPLEACCOUNT_BOOKS` overrides the folder if you want books
somewhere else.

A book records the version of the program's schema that wrote it, and a book
written by any other version is **refused** -- in both directions, with no
migration. That is a decision, not a gap: a book either starts over or is built
again from a Quicken export. So keep the export that a book was made from.

## Starting a book

**File > New book...** asks for a name and then asks one question: is this an
entirely new book, or should it be filled from a Quicken history file? Choose
the history and you are asked for a `.QIF`; everything in it -- accounts,
categories, payees, transactions, splits and transfers -- is read in, and that
is the normal way to start. Choose the new book and you get an empty one.

A new book with no history to bring in is not left empty. It starts with three
accounts somebody can type into on the first day -- Checking, Savings and
Credit Card -- and a category list taken from Quicken's own, because the
categories a stranger expects are the ones they have used for twenty years.
`Expenses:Uncategorized` is this program's own addition, since an import puts
rows in it from the first file onwards. Where there *is* a history file, the
chart is that file's own, which is the better answer when there is one.

**File > Open book...** lists the books in the folder, and **Open recent**
remembers the last few.

**Edit > Book settings...** holds the four things that belong to the book
rather than to an account:

| Setting | What it does |
|---|---|
| Fiscal year starts | The month a fiscal year begins. Report periods use it; the register's "This year" is the calendar year regardless, because the name says year. |
| A check number is trusted for | How many days an import will look past, on a check number alone, when matching a check you typed against the bank posting it. |
| A CSV import arrives cleared | On by default. A download is a statement, so its rows arrive cleared. Turn it off for a CSV you made by hand. |
| Hide everything before | Every register shows only transactions after this date, with the earlier balance brought forward as an opening line so the register still adds up. |

## The window

Three areas:

- **The left side** is the chart: every account and group, in a tree, with its
  balance beside it. Selecting an account opens its register as a tab. A right
  click offers New account, Edit, Reconcile, Hide and Delete -- Delete greyed
  rather than absent when the account has transactions in it, and a group
  heading offers only New account, a heading not being a thing in the book.
- **The middle** is the tabs: a register or a report in each.
- **The bottom panel** opens when there is something to put in it -- an import
  waiting to be reviewed, or the results of a find. Closing an import's tab
  cancels the import, there being nothing else that could mean. The divider can
  be dragged.

**View** offers a font and "Show hidden accounts". A hidden account is still in
the book and still counts; it is simply out of the way.

The status bar says what just happened, and shows a progress strip for work
that takes longer than a second.

## The register

Ten columns: Date, Num, Payee, Category, Memo, Tag, Payment, Deposit, Cleared,
Balance. Payment and Deposit are headed to suit the account -- a credit card is
charged and paid, not paid and deposited into.

**Entering a transaction.** The last line of a register is blank and is the
only line you can type in; everything above it is read-only, so a stray
keypress cannot change a transaction from 2009. Fill in what you know and press
Enter. The transaction is written to the file immediately -- a book is a file,
not a session -- and the next blank line keeps the date you just used, because
entering a morning of receipts means typing the same date over and over
otherwise.

- Leave the Category empty and it commits to `Expenses:Uncategorized` for money
  out, `Income:Uncategorized` for money in. Uncategorized is what nobody named;
  `Unassigned` is a thing you choose, and the two stay distinct.
- Typing in the Category column completes against the chart. Typing a real
  account there makes it a transfer: see below.
- Typing in the Tag column completes against the list of tags, matching the
  word after the last space. A word that matches nothing is accepted and joins
  the list -- typing is how a tag comes to exist.
- A line with no amount is a line nobody meant, and is ignored rather than
  refused.

**Right click** a row, or a selection of rows (shift and control extend it),
for Recategorize..., Split..., Add payee rule..., Apply payee rules... and
Delete.... Split takes one transaction at a time, so it is greyed rather than
left off for a selection of several or for a transaction that is already split
-- the answer to "can this be split" is then in the same place either way.
Deleting removes both sides of a transaction, never one of them, because one
side alone would leave the book out of balance.

**The Cleared column** is a tick you can click. It means the bank has reported
the transaction. Everything imported arrives cleared; only a transaction you
typed starts uncleared.

**Sorting.** Click a heading to sort by it, click again to reverse. Amount
sorts on the signed number rather than the printed text, so -100.00 comes
before -9.99, and ties keep the order they were entered in. The running Balance
is shown only in date order, because that is the only order it adds up in --
sort by something else and the column empties, sort back by date and it
returns.

**Show.** A box above the register offers All dates, This year, Last year, Past
two years, and Since a date. A range wholly inside the years the book hides
shows nothing, and says why.

**Column widths.** Drag a divider. The width goes to every other register --
they are all the same columns -- and is stored with the book, so it outlasts
the session. Only a column somebody dragged is recorded.

## Transfers between accounts

Naming a real account in the Category column makes the transaction a transfer,
and a transfer is a line in **both** registers. Each side clears on its own
statement, which is the point: the money leaves checking on one day and arrives
in savings on another, and each bank reports its own.

When you commit a transfer, the program looks in the account you just named for
something equal and opposite -- to the cent, within a two-day window, not
already paired -- and offers what it finds, closest in days first. Choosing one
is a **merge**, not a link: the transaction you were editing keeps its date,
payee and memo, the other one is gone, and the survivor is cleared if either
half was. Answering none creates nothing, because the transaction you entered
already names both accounts and so is already a line in the other register --
what none rules out is the merge, not the transfer.

A matched pair is recorded as a pair. The register never infers a match from
two accounts happening to pay each other both ways -- a book where checking
pays the mortgage and the mortgage account occasionally refunds checking would
hide hundreds of real lines if it did. It did, once; see
`findings/register-and-import.md`.

## Splits

**Split...** on a register row opens a table of Category, Amount and Memo. The
parts must add up to the transaction's amount; one that does not offers to be
edited rather than only cancelled. A transaction already in the register can be
split after the fact.

Each split line carries its own payee where it needs one, and no payee rule
ever rewrites it.

## Importing

**File > Import transactions...** reads CSV, QFX/OFX and QIF. The dialog is the
program's own rather than the one Windows provides, sorted newest first,
because an import is nearly always the file you just downloaded.

The file is chosen first and the account second, because the file is the better
witness of the two: a QFX knows which account it was downloaded for, and the
register in front of you only knows what you last looked at. The dialog
proposes an account, says *why* it proposed it -- the file says so, the file's
name fits, or it is the register you are in -- and lets you change it.

What arrives is a review in the bottom panel, one row per transaction in the
file, with a tick box and an **Import says** column:

| Disposition | Meaning |
|---|---|
| New | Nothing in the book answers to it. |
| Duplicate | The book already has it. A second read of the same file does nothing. |
| Possible | It might be one of several -- right click for **Show possible duplicates...** |
| Completed | It is the other half of a payment the book was waiting for. |

A Duplicate says in the same column how it was recognised and what did not
agree: the details differ, the check number is the same, the bank's id was
added, the payee did not match, or the transaction it matched was one you had
typed and not cleared. Right click for **Show duplicate transaction...** to see
what it was matched against. The note belongs to the import you are looking at;
keeping it on the transaction, so that it is still there next week, is asked for
and not yet built.

Matching, in short: the bank's id decides when there is one; otherwise date and
amount must agree exactly, the payee is compared *after* your payee rules have
been applied and with store numbers trimmed, and a check number reaches past
the day by as many days as the book setting allows. Where several rows in the
file look alike, the count decides -- two identical rows in a file are two
transactions, and they are duplicates only while the book has unpaired
transactions left to pair them with. An unequal count is Possible rather than a
guess.

A CSV whose profile names a balance column is checked against that running
balance; a disagreement is a question about the file, not a transaction.

Where a row looks like the other half of a transfer, you are asked -- one
question at a time, both sides quoted as their own files wrote them, with the
gap in days: **One payment** or **Two payments**. No answer writes anything.

**Accept ticked rows** is what writes. Untick anything you do not want.

An import never creates an account -- the account is the one you confirmed. It
does create a category it has not heard of, as an Expense or an Income
according to which way the money went, so a file naming categories of its own
can be read without stopping to make them first.

## Payees

A bank sends `SHELL OIL 3344` and you want `Shell`. That is a payee rule.

**Accounts > Payee rules...** lists them: Looks for, How, Payee, Category, On.
A rule looks for the part of the name that repeats -- `SHELL OIL` rather than
`SHELL OIL 3344`, so the next visit matches too -- and matches by Contains,
StartsWith, Exact or Regex. It can set the category as well as the name; left
empty, it tidies the name and leaves the category alone. The category is chosen
from the chart rather than typed, because a path typed by hand is how a rule
ends up naming a category the book has not got, which afterwards reads as a
rule that does nothing. Two rules looking for the same thing are refused.

**Accounts > Payee tables...** is the same thing seen the other way round:
every raw name the book has seen, and what it became.

Three actions apply rules to a book that is already full:

- **Assign payees according to rules** -- renames what the rules recognise.
- **Convert the payees in the book** -- the one-off tidy of a book just
  migrated from Quicken.
- **Assign the categories in the book** -- fills in categories the rules know.

The name the file wrote is always kept, so a rule can be changed and run again.
Splits, transfers and reconciled transactions are left alone.

## Tags

A third thing a transaction can say about itself, besides its payee and its
category. The Tag column holds words, and every word in it is a tag, so a
transaction can carry several -- which is why a tag is a single word, and why a
tag wanted with a space in it is written joined.

**Accounts > Tags...** lists the tags with how many transactions carry each,
and offers Add and Rename. Renaming a tag renames every use of it: a tag is a
label, not a thing with a balance. A tag nothing carries stays in the list,
because the list is a vocabulary. No report groups by tag yet.

## Find

**Edit > Find...** (Ctrl+F) is modeless, so it stays open while you use the
register. It takes text matched against payee or memo, a date range, an amount
(Exact or Range), an account, and a choice of **This register** or **Every
register**.

One amount field, and it matches the figure in either column -- payment or
deposit, positive or negative -- and inside splits too. Where the match is in a
split, the transaction is what you are shown, and the result row offers **Show
split...**. Choosing a result goes to the transaction in the register the
result was read under.

Results are a tab in the bottom panel, so they survive while you work.

## Reconciling

**Reconcile** on an account, from the tree's context menu. The dialog lists
what is not yet cleared -- date, payee, amount -- and says: *tick what the
statement shows and press Clear ticked.* As you tick, the figures say what the
book has cleared against what you entered for the statement.

If the two still differ when you press **Done**, you are offered a **Balance
Adjustment**: one transaction for the difference, with the payee
`Balance Adjustment`, against `Equity:Balance Adjustments`. It is cleared in
the account you are reconciling -- otherwise the reconciliation it was made for
could not close -- and uncleared on the other side. It is one transaction, not
two rows, and you can decline it.

Reconciling is the acceptance test for this program: the checking accounts
balancing against real statements is what says the book is right.

## Reports

**Reports > Spending by category...** opens a report tab. It offers:

- **Period** -- a named period, or a from and to date.
- **Detail** -- Categories - highest only, Categories - all, or Transactions.
- **Quicken signs** -- expenses negative, as Quicken writes them, so a figure
  can be read straight against one of its reports.
- **Show empty categories** -- categories with nothing in the period.

With Detail set to Transactions, a right click on a line offers
**Recategorize...** and **Go to the transaction** -- in the register it lives
in, or, where a transfer is a line in two registers, a submenu naming both so
that you choose.

A report tab holds its last result and only recomputes when the book changes,
so switching back to one is free. Every table scrolls, and so does every
hand-built window: a dialog taller than the screen gets a scroll bar rather
than putting its buttons below the bottom edge.

## Accounts and categories

**Accounts > New account...** takes a name, a type, the group it goes under, an
opening balance and the date of it, and the bank and the number on the
statement. The opening balance is a transaction against
`Equity:Opening Balances` -- not a field on the account -- so the figure has
one home however it was entered.

**Account listing...** and **Category listing...** are one line per node, every
node, in path order, with no group headings and nothing hidden left out. The
tree is the opposite on all four counts, deliberately. Clicking a line opens
the editor for the thing on it; Edit on the tree opens the same one.

**Account settings** holds the bank, the number on the statement, and the
starting balance with its date -- read off the book and written back to that
same transaction. An account that never had one shows an empty field rather
than a zero.

**Category settings** holds the name, the group it sits under, the type, an
alias, and a tick to keep it out of the pickers.

Deleting is refused rather than silently done where the book would be left
inconsistent -- an account with transactions in it has its Delete greyed, and a
delete that is refused says why. Deleting a category that is in use currently
refuses too; offering a replacement instead, and clearing out the payee rules
that named it, is decided and not yet built (`questions.md` section 31).

## What is not here yet

An honest list, so nothing comes as a surprise:

- **Investments.** A QIF investment record's action word currently lands in the
  Num column. Investment accounts are deliberately left for later.
- **A progress dialog with a Stop button.** The status bar strip appears for
  work that takes more than a second, but a long import still says nothing
  while it runs, because the reading is one call on the thread that would
  deliver the click.
- **Reports beyond spending by category.**

`tasks.md` and `TODO.md` hold what is owed; `archive/` holds what is done, with
the reasoning. `README.md` and `decisions.md` say why the
program is shaped the way it is, and `spec/` -- fourteen `.spectable` files --
is the specification every one of these behaviours was written from, in worked
examples, before the code.

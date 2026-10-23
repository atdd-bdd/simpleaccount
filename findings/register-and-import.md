# Ken Checking, and the two defects behind it

What this is: the reading of the real `pughkilleen` book against
`testdata/account balancesw 1 1 2026.pdf` and `testdata/kens checking.csv`,
what was wrong, and what was changed for it. Every figure here was read
read-only from `C:\ProgramData\SimpleAccount\pughkilleen17.sadb`. Nothing was
written to the book.

The short answer to the question that started it: the register was not off
because of the year-end rollover. Two defects were found, one in the register
and one in the importer, and the register one is the big one -- it was hiding
846,048.03 of this account's own history. The rollover explanation does fit two
of the other four differences exactly, and part of a third.

## 1. The register defect: 903,048.03 of transfers hidden by one row

`reg::lines_for` decided whether to show a transfer that another account had
made *into* this one by asking a question about the whole book:

```cpp
// as it was
if (index.points_at(t.source_account, account)) continue;   // "they pay each other, so hide it"
```

If the account this transfer points at had **any** transaction pointing back
here, the line was dropped. The intent was the second row of a pair matched by
hand: two rows describing one movement of money, where showing both would count
it twice. The test it used cannot tell that pair from two accounts that simply
pay each other both ways, which is ordinary -- a joint account funds a personal
one all year and is repaid once.

In this book, `Joint - Crown` had made **329** transfers into Ken Checking,
903,048.03 in all. One row went the other way (T37485, itself one of the
import duplicates below), and that single row suppressed all 329. Ken Checking's
register read **870,394.39** where the report says **24,346.36**.

Measured, on the real book, three ways of deciding it:

| Rule | Ken Checking at 1/1/2026 |
|---|---|
| Any traffic the other way hides it (as shipped) | 870,394.39 |
| Per transaction, pair by equal-and-opposite amount | 27,346.36 |
| No suppression at all | **24,346.36** (the report's figure) |

The middle row is why no cleverer heuristic was used: pairing by amount still
wrongly suppressed T1323 of 2008-03-03. And the specification deliberately
places a hand-matched pair fifteen days apart, so no date window would work
either.

**What changed.** The match is now recorded rather than inferred. A transaction
carries `matched_to`, the id of the row it was matched to (`src/transaction.h`),
`ledger::match_destinations` writes it on both rows, and the register reads only
that -- so the one thing ever hidden is the row a human said was the same money.
`points_at()` and its index are gone.

Specified first, in `TransactionRegister.spectable`:

- `BusinessRule A transfer is a line in each of the two registers it names`,
  with the five kinds of transaction side by side.
- `Scenario Two accounts that pay each other both ways keep every line` -- the
  regression test for exactly this defect.
- two scenarios for the matched pair, one from each register's side.

And in `Transactions.spectable` and `Books.spectable`: `MatchedTo` on the
Transaction entity, and a scenario that a matched pair reads back still matched
after a save.

### This bumps the schema to 18

`matched_to` is a new column, so `kSchemaVersion` goes 17 to 18 and there is no
migration -- a book of another version is refused, by design. The real book has
to be rebuilt by re-importing, as with every other bump
(`pughkilleen18.sadb`). Re-importing is also what clears the duplicates in
section 2, so the two go together.

## 2. The import defect: a transfer entered from the far side imported again

`ofx::existing_in` builds the list of transactions the duplicate test is allowed
to look at, and it was one line:

```cpp
if (t.source_account != account_ref) continue;      // as it was
```

Only transactions *living in* the account being imported into were candidates. A
transfer entered from the other account -- source there, destination here -- is
a line of this register and will be on this statement, and it was never
compared against at all. So the row came in as `New` and the money arrived
twice. Both importers go through that one function, so CSV and OFX failed the
same way.

Four such duplicates in Ken Checking, 39,000.00 between them:

| Ref | Date | Amount | Payee | Landed in |
|---|---|---|---|---|
| T37478 | 2026-07-31 | -3,000.00 | TO PUGH K CHECKING REF #OP0Z5NX4 | Expenses:Uncategorized |
| T37479 | 2026-08-26 | -30,000.00 | ONLINE TRANSFER TO PUGH K | Expenses:Uncategorized |
| T37480 | 2026-08-31 | -3,000.00 | TO PUGH K CHECKING REF #OP0ZL8TN | Expenses:Uncategorized |
| T37471 | 2026-09-30 | -3,000.00 | FROM PUGH K PRIME CHECKING REF # | Joint - Crown |

This is not the multiset-pairing rule working as intended. Two identical rows in
one file are two transactions, and that is settled. Here a transaction already
in the book was invisible to the test.

**What changed.** `existing_in` now offers the far side too, with the amount
negated -- which is the figure the register shows, so the amount test is
unchanged. A far-side entry carries no FITID and no check number, so tests 1 and
2 cannot claim it and date-and-amount decides; and nothing is ever written back
to it, because the row belongs to the other account's statement.

Specified first, in `ImportOfx.spectable`: `Scenario A transfer entered from the
other account is a duplicate in this one`, plus the wording of tests 3 and 4 of
`DispositionRule` changed from "in this account" to "the lines of this account's
register", which is what it had to have meant.

**Left out on purpose.** A split line whose destination is the target account is
the same case and is not fixed yet. It needs the split's own line amount as the
candidate figure, and no real difference in this book turned on it.

## 3. The 1/1/2026 report, account by account

48 rows. 33 agree to the cent, 10 are Brokerage and out of scope as asked, 5
differ:

**Ken Checking - Wachovia 3112** -- sections 1 and 2. At 1/1/2026 the history
is sound; the 39,000.00 is all 2026.

**Chase Sapphire 4391** -- report -654.76, book -4,958.06. One transaction:
T6053, the January autopay of 4,303.30 dated 2026-01-02. -4,958.06 + 4,303.30 =
-654.76. **This is the year-end rollover.** Quicken keeps the two halves as
separate rows -- the card's download says `AUTOMATIC PAYMENT - THANK YOU` in
late December, the bank's says `CHASE CREDIT CRD AUTOPAY` on 1/2 -- so its card
balance at 1/1 already has the payment in it. The book pairs the halves into one
transaction and keeps the bank's date. Nothing is wrong; an as-of balance
between the two dates simply disagrees with Quicken's.

**Wells Fargo Ken Visa 1333** -- report -2,050.18, book -5,208.34. The same
thing: T6052, 2026-01-02, 3,158.16. -5,208.34 + 3,158.16 = -2,050.18. **Also
the rollover.**

**Amex Hilton Ken 1002** -- report -59.30, book -62.30, off 3.00. Two causes,
and only one of them is the rollover:

- the card payment of 62.30 is in the book as T6045 (2025-12-23, from
  `Joint - Crown` to `Expenses:Uncategorized`), so the card never got the
  credit -- that is section 2's defect read from the paying side;
- December's TMobile -55.00 and NYTimes -4.30 are absent from the book
  altogether, though every other month from 2025-08 to 2026-08 has both: the
  December statement for this card was never imported.

62.30 - 55.00 - 4.30 = 3.00.

**Transfer Account - Should be 0** -- report -10,001.00, book -55,428.30. The
report's figure is the book's own running balance at 2025-06-30. After it:
T7493 (-5,000.00, 2025-10-14) and T7507 (+5,000.00, 2025-12-08) cancel, and
T887 (2025-11-21, 45,427.30 into Cap One Savings 0852, no payee) is the whole
difference. Cap One Savings agrees with the report, so Quicken has the money
arriving and gives it a different other half -- a category or an investment
account, neither of which appears on an Account Balances report. The importer
put the far side of all three into this catch-all. **Not the rollover.**

**Paypal - Payments and Charges** -- report 961.86, book 2,045.47. 1,083.61 is
credited to Paypal twice: T9247 (2020-03-19, `ONLINE TRANSFER TO KEN PUGH,`)
and T9265 (2021-01-08, `PAYPAL TRANSFER`), both from Online KP Wachovia 3668.
Online KP agrees with the report, so Quicken has both rows and sends one of
them somewhere other than Paypal. **Not the rollover** -- these are five and six
years old.

## 4. What is still owed

- A re-import of the real book, for schema 18 and to clear the four duplicates.
- 1,038 transactions in this book sit in an `Uncategorized` account. Most were
  Uncategorized in Quicken too and cost nothing. The ones that matter are where
  Quicken has a transfer and the book has a category, and they share section 2's
  cause. Worth another look after the re-import: `CHASE CREDIT CRD AUTOPAY`
  (7 rows, -30,960.83), `AUTOMATIC PAYMENT - THANK YOU` from the Wells Fargo
  card (2 rows, 5,535.04), `Preauthorized Withdrawal to WELLS` (-45,000.00),
  `AMICA INSURANCE debitpmt` (6 rows, -5,847.52).
- The split-line half of section 2.
- Investments were left alone, as asked.

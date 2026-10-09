# outline.txt, item by item

Checked against the spec files and the code on 2026-10-08, at commit `928c5ac`.
Three states, and the difference between the last two is the whole point:

- **Built** — production code, with a test that runs and passes.
- **Specified** — written in a `.spectable` and tagged `$WIP`, so no test runs
  and no code exists. A promise, not a feature.
- **Nothing** — not in the specification at all.

Two items are **in conflict** with what is already specified; those are called
out rather than listed as missing, because something has to be decided before
either can be written.

---

## Accounts

| Item | State | Where / what is missing |
|---|---|---|
| Add an account | Built in the window; scenario `$WIP` | `Accounts > New account` works. "Adding an account under a parent" is tagged, so nothing tests it |
| Change the name | Specified, **live** | Renaming and its descendants are tested. No way to do it from the window |
| Add institution | Specified | `AccountSettings`, written today. No code |
| Add account number | Specified | Same dialog. No code |
| Change the starting balance | **Nothing** | Opening a book *with* a balance is live; changing one afterwards is nowhere |
| Change the date of that balance | **Nothing** | Same |
| Hide an account | Specified | `View > Show hidden accounts` reveals them; nothing hides one |
| Delete an account when empty | Specified, **live** | Both scenarios pass. No way to do it from the window |
| The seven type groups and their sub-types | **Nothing** | `AccountType` is still the old nine values. See below |
| Accounts listed under those groups | **Conflict** | Specified this morning as two sections, Assets and Liabilities, which your outline replaces |
| Each type is an asset or a liability, for the balance sheet | Partly | `AccountClass` (Real/Nominal) is live and the balance sheet derives its two sides from the group. No per-type asset-or-liability rule |

**The type list is the foundation and the one piece of real work in this
section.** Going to Checking / Savings / Cash / Credit Card / Line of Credit /
Brokerage / IRA / HSA / 401K / Mortgage / Auto / Personal / Other / Real Estate
/ Vehicle / Other / Accounts Receivable / Accounts Payable / Transfer In /
Transfer Out means `Bank` stops being a type. Every `| Type | Bank |` in
fourteen spec files has to change with it, and so does the enumeration in
`src/account_type.h`, the switches over it, and `account_type_from_string`,
which must still read `Bank` so that books already written can be opened.

## Categories

| Item | State | Where / what is missing |
|---|---|---|
| Add a category | **Nothing** as a thing a person does | An import invents them; there is no dialog |
| Hide a category | Specified | The same hidden flag as an account |
| Delete a category and reassign its transactions | **Nothing** | Deleting an *account* needs it to be empty, which is the opposite rule |
| Change the name | Specified, **live** | The account rename covers it |
| Abbreviation for a category | Specified | `Alias` on the account, and "An alias stands for one category however little is typed" |
| Find a category by its abbreviation | Specified | Same scenario, tagged |
| Quicken's convention: the name without Income or Expense, accounts in `[]` | **Nothing** | The deep one. Settled as display-only |

## Registers

| Item | State | Where / what is missing |
|---|---|---|
| The account outline on the left | Built | Live scenarios. To be regrouped per the types above |
| Context menu on an account, to its settings | **Nothing** | The dialog is specified; the way to reach it is not |
| Clicking an account opens its register in a tab | Specified | Written today |
| Register context menu — Change category | Specified | Written today as Recategorize, over a multi-line selection |
| Register context menu — Add payee rule | Specified | Written today, with the category pre-filled or asked for |
| Register context menu — Delete, with a warning | Partly | Deleting a transaction is specified; the menu item and the warning are not |
| Register context menu — Split, with a dialog | Partly | Splits are specified and tagged; the dialog is nowhere |

## Transaction and posting

| Item | State | Where / what is missing |
|---|---|---|
| Every transaction has an id, the millisecond it was created | Built | `src/transaction_id.h`; its scenario is tagged |
| The posting shows the transaction's category | Built | The register's category column, live |
| Several postings for a split | Specified | Display and arithmetic both tagged |
| A category that is another account joins an existing entry rather than making one, matching within two days | **Conflict** | `Transactions.spectable` says the program *asks* which of the two it is. Your outline says do it. See below |
| A person can add a transaction by hand | Built | The blank line at the foot of the register |
| ...with an indication that this was done | **Nothing** | Nothing records or shows how a transaction arrived |

## Downloads

| Item | State | Where / what is missing |
|---|---|---|
| Import QFX | Built | |
| Import CSV | Built | |
| Read the bank and account id from the QFX | Specified | The mapping asks which account the first time |
| Read them from the **name** of the CSV file | **Nothing** | |
| Show them in a dialog, editable, before the import starts | **Nothing** | |
| A progress dialog while importing | **Nothing** | |
| Duplicate by matching the QFX id | Specified, **live** | |
| ...and a dialog when the amounts disagree | **Nothing** | |
| Duplicate by payee, amount and date | Specified, **live** | The fingerprint test |
| Review before accepting | Built | In the second pane |
| Duplicates marked in the list | Built | The disposition column |
| Hiding the duplicates on request | **Nothing** | |
| Payee matches categorise automatically on import | Built | The rules are applied as the rows are prepared |

## Specific operations

| Item | State | Where / what is missing |
|---|---|---|
| No check printing | **Nothing recorded** | Worth a line in the decisions section of `README.md`, so it stays decided |
| Investments are the least important | — | The whole Investments file is tagged, which already matches |

---

## The two conflicts

**Where accounts are listed.** This morning's commit specifies the list as two
sections, Assets and Liabilities, with Banking and Credit Cards inside them —
that came from "Credit Cards should be under Liabilities". The outline says the
seven type groups, flat, and that asset-or-liability "is only important when
reporting the balance sheet". The second is now the decision, so the first is
to be undone: three scenarios and a `DataType` go back to the flat shape with
seven groups instead of five.

**Joining a transfer to an entry that already exists.** Already specified, and
specified the other way:

> Assigning a real account to an Unassigned side says where the money went. It
> does not say whether that account already has a row for it. Both are possible
> and they are different outcomes, so the program asks rather than choosing.

The outline says that when the matching transaction exists, no new transaction
is made and a posting is added to the existing one, matching within two days.
Those cannot both be true. Deciding it needs an answer to: what happens when a
row is found within two days that is *not* the same payment — two cards from
one bank, paid the same amount on the same day, is the case `ImportOfx` already
worries about. Asking is what protects against joining the wrong two; doing it
automatically is what saves the asking. A middle reading is that it joins
without asking only when exactly one candidate is within two days, and asks
when there is more than one, which keeps both properties.

# Questions, and the decision taken

The author asked that anything open be written down here with the decision
already made and implemented, rather than waiting for an answer. Each entry is
a question, the decision, and where the code is. Any of them can be reversed by
changing the spec table named.

## 1. A book of an older schema is refused rather than upgraded

**Question.** `upgrade()` had seven steps and nothing to migrate; adding the
tables this pass needs (payee conversions, payee categories, tags) means a
schema bump. Refuse the old book, or carry an upgrade path?

**Decision.** Refuse. The author's words: "there will be no migration at this
time. Figure that everything will either start over or be initialized from a
QIF file." So `kSchemaVersion` moves and a book of any other version is refused
with the message `Books.spectable` already specifies. When the program is
released this has to change, and the place it changes is `store::open`.

## 2. Where the payee conversion and categorisation tables live

**Question.** `Payee Rules.txt` describes two tables -- a name conversion table
(pattern -> name) and a payee -> category table -- separate from the existing
`payee_rules`. One table with three columns, or two tables?

**Decision.** Two tables, `payee_conversions` and `payee_categories`, because
they are asked two different questions at two different times: the conversion
runs over the raw name the bank sent, the categorisation over the name that
came out of it. `src/payee_rules.h` already models them separately and the
import path takes them separately. One editor window with two tabs, so the
author sees them together.

## 3. Tags are stored on the transaction as one text column

**Question.** A tag per row in a join table, or the words in one column?

**Decision.** One `tags` text column on the transaction, words separated by
spaces, plus a `tags` table that is the vocabulary (so a tag nothing carries is
still offered). This is what the Tags section of `UserInterface.spectable`
settles and `src/tags.h` implements. A join table would be the right shape for
reporting by tag, and no report groups by tag in this release.

## 4. Speed of an import, and of accepting one

**Question.** The author reports that importing a QFX file and accepting it
takes a while even when nothing is ticked.

**Decision.** Index what is already in the account instead of scanning it per
row, and let a judged row keep the identity of the transaction it matched so
accepting does no searching. Three places, all measured as O(rows x book)
before:

- `ofx::judge` scanned the whole account for every row, three times over, with
  a payee normalisation per comparison. It now asks an index keyed on the
  identifier, the check number and the amount in cents.
- `reg::BookIndex` keyed its map on the account id written out as a string,
  rebuilt from nothing on every refresh. The key is the integer it already was.
- `ui::accept_import` asked `ledger::transaction_count` once per accepted row,
  which is a scan of the book per row, and looked each pairing up by scanning.

Nothing about the answers changes, which is what the 589 tests are for.

## 5. The side tables get their own store accessors, not more parameters

**Question.** The payee converter table, the payee-to-category table and the
tag vocabulary all have to be written somewhere. `store::write()` and
`store::read()` already take five things each. Add two more, or give each table
its own pair of calls?

**Decision.** Its own pair: `write_conversions`/`conversions`,
`write_categorisations`/`categorisations`, `write_tags`/`tags`. Each is a list
edited in a window of its own, and nothing about the book changes when one does
-- a converter row is not part of a transaction and a tag with no uses is not
part of anything. Writing them with the book would mean the whole book is
rewritten because somebody fixed a spelling in a table of ten rows.

Both payee tables are separate tables rather than one, because the columns
differ: a converter row is a pattern and what it becomes, a categorisation row
is a payee and a category. One table of four columns would be half blank on
every row. They are edited in one window of two tabs, which is the thing the
author actually asked for.

The new column in `payee_conversions` is `pattern` rather than `match`, because
MATCH is a SQLite keyword and the DDL will not take it unquoted.

## 6. Schema 14 refuses an older book rather than upgrading it

**Question.** Four schema changes landed at once -- the two payee tables, the
tag vocabulary, the starting-balance work. What happens to a book written by an
earlier version?

**Decision.** `kSchemaVersion` is 14 and `upgrade()` is gone: a book whose
version is below the current one is refused, with the reason said plainly.
Sanctioned by the author -- the program is not released, and every book either
starts over or is initialized from a QIF file. This is a decision to revisit
before anybody else has data in one.

## 7. The register's Tag column offers no inline completion

**Question.** `tags::offer` exists and the tag rules describe what a column
offers against what has been typed. The register now has a Tag column between
Memo and the two amounts. Should it complete as you type?

**Decision.** No -- not yet, and not inconsistently. The Category column in the
register offers no inline completion either: there is no `QCompleter` anywhere
in the app, and a category is picked through the `CategoryPick` dialog. The tag
rules ask the Tag column to behave the way the Category column does, and the
way it does today is "type it in full". A word typed that is not in the
vocabulary joins it when the line is recorded, which is the one place a tag is
created, so nothing has to be opened first. When inline completion arrives it
arrives for both columns at once, from one place.

## 8. A filtered register says its opening balance in the strip, not in a row

**Question.** `reg::Filtered` hands back the balance the account stood at the
day before the range began, so the running balance has somewhere to start
from. Where does the window put it?

**Decision.** In the label beside the range box, as "Balance on <date> <amount>",
rather than as a first row of the table. A row would be a line in the register
that is not a transaction, and every line number the workspace holds -- the
selected lines, "go to the transaction" from a report -- counts from the first
transaction. Shifting them all by one to show a figure is the kind of change
that breaks the register menu in a way nobody notices for a week. The same
label carries the hide-before note, and why the balance column is empty when
the register is sorted by something other than the date.

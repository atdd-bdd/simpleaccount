# Where things stand

Written 26 October 2026, at the end of the session that closed the import
matching defect and added the review filter. The author is away for two weeks,
so this says what is finished, what is next, and what a cold start needs to
know. The long-lived lists are unchanged and still authoritative:
`TODO.md` for decisions that are the author's to make, `tasks.md` for work
items, `questions.md` for decisions already taken and why.

## The tree, as left

`main` at `666d2dc`, pushed, nothing uncommitted. The suite is **717 tests,
all passing** -- `tools/rebuild.sh`, which analyses (0 diagnostics over 14
files), regenerates, builds and runs in that order. A red test here is a
regression: unwritten behaviour is tagged `$TBD` and generates no test at all.

A Release build of the app is current: `build/Release/simpleaccount_app.exe`,
built 25 October 23:44. `LNK4098: defaultlib 'LIBCMT' conflicts` and
`'pwsh.exe' is not recognized` are both expected noise -- vcpkg's applocal
step -- and neither means a failed build.

## What the last two sessions closed

**A payment the bank posted the next day is not a second payment** (`d46f7ae`).
Eleven automatic card payments imported as New when they were already in Joint
Checking as transfers. Two mechanisms had declined them: the duplicate pass
keys on an exact-date fingerprint, and `transfers::halves_for` skips a half
whose destination is already a real account. The fix is a second counting pass
in `ofx::judge` over *far-side* lines -- lines of this register that belong to
another account's transaction -- within `ofx::kMatchWindow = 5` days. It
counts over a connected component rather than a date bucket, because a window
is not an equality: row A can reach a line row B also reaches while A and B
are six days apart. Same-account matching stays exact, so a payment and a
refund are never merged. Specified as `BusinessRule A far-side line is matched
across the bank's posting lag` in `spec/ImportOfx.spectable`, with four
scenarios. Reasoning in `questions.md` section 36.

**A review can be narrowed to the rows that are ticked** (`666d2dc`). A
checkbox, `Show only ticked rows`, over the import review table. It is a view
and not a list: `review()` still returns every row, `review_shown()` builds
the narrowed one on demand, the heading keeps counting the whole import, and
accepting a narrowed review writes exactly what accepting all of it would. A
row leaves as it is unticked and returns as it is ticked again, and the
checkbox resets on every new import. Seven scenarios in
`spec/UserInterface.spectable`; reasoning in `questions.md` section 37.

One latent defect was repaired on the way: `MainWindow::reviewMenu` took the
clicked row's line to be its index plus one, which stops being true the moment
rows are left out. It reads `Qt::UserRole` off the tick cell now, where the
line already was for the tick handler.

## The next thing, and it is the author's own note

`matching transfers.txt`, committed with this file, is the author's
specification in prose for matching transfers between two accounts: a dialog
asking *are these the same transaction*, a `Match this transaction` context
menu item on a register line, what happens when one side of a matched pair is
recategorised, Transfer Accounts for money moving to books SimpleAccount does
not keep, and a closing paragraph on reconciling a local balance against an
online one without inventing a Balance Adjustment. None of it is specified in
a `.spectable` yet, and the house rule is that the table comes first and is
reviewed before any code. That is the obvious next piece of work.

It overlaps two things already known to be wrong, and they should be settled
as part of it rather than separately:

- `src/transfer_match.h:97` skips any half whose destination is already a real
  account. That is the other half of the bug the far-side pass worked around:
  the five payments were never even offered for pairing after the import.
- That pass uses `transfers::kWindow = 2` while `Examples: DateWindow` claims
  five days for `One register's transaction against another's`, which is
  `ofx::kMatchWindow`. The row claims five days for something the code does in
  two. Whether those are one number or two is a decision, not a fix. The
  author's note above says +/- 2 days, which is evidence for two.

## The rest of the backlog, in no order

- The split-line half of the import fix: a downloaded row against an existing
  split transaction.
- A re-import of the real book, to see the far-side fix against all of it
  rather than against the four scenarios.
- A way to type what a statement says -- the dialog the CSV import wants,
  since only OFX states a balance. `questions.md` section 34.
- `questions.md` section 31's specified-but-unwritten items.
- Account path should come from the parent id rather than being stored; raised
  several times, queued behind the Posting removal.
- `book_is_now(...)` loses the import review, the find results and the
  selection. `TODO.md` section 5.
- `reg::BookIndex` with integer keys. Last, on purpose: it is an optimisation.
- Spec hygiene: `as previous` is spelled out in the six files with no
  `Background`; the five that have one still use it and were left alone,
  because inlining there would change meaning rather than restore it. The
  eleven one-attribute `Attributes` workarounds can go once a release past
  AlignThree 0.9.2 ships.

## The books on this machine

`C:\ProgramData\SimpleAccount`. The app accepts schema 19 and nothing else
(`src/store_sqlite.h:40`, refused either side at `:464`).

| File | Schema | Opens |
| `pughkilleen19.sadb` | 19 | yes -- the live book, 37,559 transactions, 561 accounts |
| `pughkilleen18.sadb` | 18 | no |
| `pughkilleen17.sadb` | 17 | no |
| `pughkilleen.sadb` | 16 | no |
| `AccountSettingsRoundTrip.sadb` | 18 | no |

The earlier files are per-schema copies kept deliberately. `quicken_signs` is
already in the 19 book, so the migration that was being offered for weeks is
done and the offer has been dropped.

## Traps that have each cost real time

All of these are in `CLAUDE.md` in full; this is the short list, because each
one has been paid for more than once.

- AlignThree 0.9.2 or later, never 0.9.1: the older converter silently drops
  whole `Examples:` tables.
- Never `rm -rf generated/`. The converter writes `*_glue.h` once as failing
  stubs and never rewrites what is there -- it does append new methods -- and
  the glue is hand-written. Only `test_*.cpp` is rewritten.
- Never `as previous`.
- Rebuild rather than build: building without regenerating compiles the
  previous tables, which looks exactly like a defect in production code.
- A backslash does not survive a shell heredoc into Python. It cost another
  twenty minutes this session: a patch script matching a stub line containing
  a C string escape failed because the escape arrived as a real newline. Write
  the file with the editing tool, or build the backslash as `chr(92)`, or
  match a span by its signature line and avoid the escape entirely.

## The transcript

The session that produced all of the above is saved outside the repository, in
`C:\Users\user\source\repos\transcripts`: the full record as
`2026-10-26-import-matching-and-review-filter.jsonl` (111 MB, tool calls
included) and a readable rendering of what was said on both sides as
`2026-10-26-import-matching-and-review-filter.md` (2.4 MB). They are outside
the repository on purpose -- the raw record is too large to carry in git.

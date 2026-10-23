# To look at

Decisions that are the author's to make. Each is a question about what the
program should do, not a defect, and each is reachable from a test that passes
today — so nothing here is urgent and nothing here is lost.

Everything else that was on this list is done. It has moved verbatim, with a
line on each saying what closed it, to `archive/completed-todo.txt`.

## An "in progress" dialog while something long is going on

> A "in progress " modeless dialog box should appear when things are being
> imported or recalculated.

Part done, and the open half is task 17 in `tasks.md`. The strip is in the
status bar and appears only once a second has gone by, driven from filling a
register's cells. The imports themselves still say nothing, because
`qif::Importer::run` and `review::from_file` are each one call with no point
inside to report from, and there is no Stop button because the work runs on the
thread that would deliver the click. What is left is a tick callback in the
readers and the work off this thread.

## 5. Redoing a report or a register after one change may be re-reading more than it needs to

Raised by the author, investigated on 2026-10-20, and two thirds paid for on
2026-10-22. The author's note on it: **do this at the end as an optimization.**

The finding worth keeping: nothing re-reads the book from disk after an edit.
Every cost is in memory, and there were three, each O(n) in the whole book for
a change to one transaction — `save()`'s `state_of()` diff, the deep copy in
`book_is_now(...)`, and a fresh `reg::BookIndex` per `refresh()`.

Two were paid: `refresh()` now fills only the tab in front and marks the rest
stale, and the book is opened `journal_mode = WAL` with
`synchronous = NORMAL`, which is what made a save after an import wait on the
disk.

**What is left:** the string-keyed map in `reg::BookIndex`
(`src/register_lines.h:81-147`) is a `std::map<std::string, std::size_t>` keyed
by id, rebuilt from nothing on every refresh, while ids are integers. That is
the cheapest next move. Also still true, and a correctness cost rather than a
speed one: an edit clears the import review, the find results and the
selection, because `book_is_now(...)` throws them away.

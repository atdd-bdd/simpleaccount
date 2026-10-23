# Response to "AlignThree findings from a real project"

Written 2026-10-23 against the current repository, which is ahead of the 0.9.2
you were using. Every claim below was checked by running the converter or
`analyze_cli` from this tree, or by reading the code that produces the
behaviour; where a repro is quoted, it was run.

Thank you for the detail. The two items that cost you the most — the headline
"analyses clean, does not compile" case and the dropped empty table — are both
already closed, and the one that is still open (#11) is open exactly where you
say it is, for exactly the reason you give.

## Summary

| # | Finding | Status |
|---|---|---|
| 1 | `EveryCell` on a bare DataType generates uncompilable glue | **Fixed** |
| 2 | The CLI does not warn about step-name collisions | **Not reproducible** — see below |
| 3 | `analyze_cli` reports a file as a duplicate of itself | **Fixed, in two places** |
| 4 | Tilde-for-space is not in the syntax document | **It is, in your own copy** |
| 5 | `as previous` silently produces an empty table | **Not a feature of the language** |
| 6 | A table with a heading and no rows is dropped | **Fixed, as you proposed** |
| 7 | Stub appending works well and deserves documentation | **Documented** |
| 8 | Examples heading mismatch gives two messages | **Open** |
| 9 | `$tag` filtering is excellent and undocumented | **Fixed**, both halves |
| 10 | Named comment continuation; a stray word ends a block | Request 1 **shipped**; request 2 **open** |
| 11 | 0.9.2 makes a cell a struct and comparisons stop compiling | **Open, and being fixed now** |
| 12 | `And` works and is in no document | **Open** — being documented, with two rulings |

## Fixed since 0.9.2

**#1 — `EveryCell` on a bare DataType.** Your repro, run here for C++:

```
DataType TransactionRef
Examples: ValidValues
| Value | IsValid | Notes |
| T1    | Yes     |       |

When transaction deleted : TransactionRef EveryCell
| T1 |
```

now writes `common/transactionref_string.h` and `common/transactionref_typed.h`,
and generates

```cpp
void when_transaction_deleted(const std::vector<TransactionRefString>& values)
```

called from the test as `TransactionRefString("T1")`. No `from_string_struct`,
no undeclared identifier, 0 diagnostics. Option 1 of the three you listed is
what shipped: a DataType used with `EveryCell` gets its own `String`/`Typed`
pair, so the documented language works. The eleven one-attribute `Attributes`
blocks you wrote as a workaround can be removed.

**#3 — a file reported as a duplicate of itself.** Both causes are addressed.
`analyze_cli` now skips a generated copy and says so:

```
-- 0 diagnostic(s) over 3 file(s), 1 generated copy/copies skipped (--include-generated to analyse them)
```

Forced on with `--include-generated`, the message names the other file rather
than itself (`Two.spectable:7 ... is also declared in generated/Two.spectable:7`),
so a symbol is never its own duplicate. Separately, the converter no longer
copies the `.spectable` into the output at all unless asked: the `.specconfig`
default became `copySpectable: false` on 2026-10-11 and the command line
followed on 2026-10-20 — it is `--copy-spectable` to ask for the copy now, and
`--no-copy-spectable` is still accepted and does nothing, so your scripts keep
running.

**#4 — the tilde.** `spectable syntax v3.3a.md` as shipped inside 0.9.2 contains
the rule, in the section on tables: a cell is trimmed, a tilde stands for a
space, and every generator does it, so it holds in all nine languages. It looks
as though the copy you searched was 0.9.1's.

**#6 — a heading with no rows.** Generated now from
`Then rule list rows are : RuleListRow` with a heading row and nothing else:

```cpp
std::vector<RuleListRowString> objectList1 = {
};
glue.then_rule_list_rows_are(objectList1);
```

The step is generated and the glue is handed an empty table — an empty table is
a statement, not an absence. The two spec tables you left written the correct
way are now real assertions without being edited.

**#7 — stub appending.** It is in the User Guide, under *Glue is yours*: each
glue method is created once as a stub and thereafter only appended to, never
rewritten or removed, together with the cost of that safety (glue for a deleted
step is listed as a warning until you remove it).

**#9 — `$tag` and `--tag-filter`.** Both halves are closed. The syntax file
carries the `TagLine` grammar, the rule that a blank line clears a pending tag,
the end-of-file case, and the filter's case-insensitivity and tolerance of a `$`
inside the expression; the User Guide carries the workflow and the fact that a
`$tag` above `Specification` covers the whole file; the Configuration Guide
documents `tagFilter` and `--tag-filter`. And your third bullet — a tag that
attaches to nothing — is now reported at the line you wrote it on:

```
$WIP marks nothing -- a blank line ends a tag, so what follows is generated as
though it were untagged. Put the tag immediately above the block it belongs to.
```

A tag written above a `ScenarioGroup` is dropped the same way, with its own
message, since a group generates no test for a tag to mark.

## Not reproducible

**#2 — the CLI is silent about step-name collisions.** It is not. Your two
steps, in one file, generated by `SpecTableConverter.exe` with no `--context`
and no other arguments:

```
WARNING:17:Step 'when_account_added' expects a AccountWithOpeningBalance table here,
but the same glue method elsewhere in this project expects a Account table -- rename
one of the steps, or switch this project to naming glue methods after the table they
take
```

The check lives in all nine generators, which is what the CLI runs; there is no
path where the IDE has it and the command line does not. What differs is the
input. The IDE passes every other specification in the project as `--context`,
so a collision whose two steps are in different files is visible to it. A CLI
run over one file at a time cannot see the other file's step, and that is almost
certainly what happened with your three. Pass the siblings as `--context` — which
you want anyway, since a DataType declared in one file has to be visible to
another — and the warning appears.

Your second half of #2 is done: `--step-name-attrset-on-collision`, used with
`--step-name-attrset`, appends the attribute set only to the glue methods that
would otherwise collide. Every other method keeps the name the step reads as.

## Not a feature

**#5 — `as previous`.** There is no such construct in the language. The phrase
appears in no generator, nowhere in the IDE, in no shipped document and in no
example specification; it was invented on your side. `Given postings are as
previous` is simply a step whose text ends in those words, with no table under
it, so it generates a no-argument call — the same as any other table-less step.

That also means there are not two readings of it. Nothing resolves to the
`Background`: a `Background`'s own steps, with their tables, are inlined into
every test in the file, so the state you saw was established by the Background
itself and not by the `as previous` step. In a file without one, nothing
establishes anything, which is the 72 you counted.

The repair is what you were told and what you did: a table-form `Define`,
referenced as `=SomePostings` from each scenario that needs it. That is the
construct the language has for sharing a table, and your mechanical replacement
of the six Background-less files is the right outcome. No tool change follows
from this one.

## Open

**#11 — the cell wrapper, and your C++ reasoning is right.** The `...String`
struct still has no `operator==`, no `operator!=` and no `empty()`, and the
comment you quote is still in the generator. Your analysis of why the promise in
that comment does not hold is correct, and the generator makes the same argument
itself eight lines further down, for streaming:

> Streaming needs its own operator: the one for `std::string` is a template, and
> template argument deduction does not apply a user-defined conversion, so
> `message << cell` would not find it.

That is exactly why `cell == expected` cannot reach `std::string`'s
`operator==` either — so the ambiguity the comment guards against cannot arise,
and omitting the operator removed the only comparison that would have worked.
Being fixed now, as you suggested: comparison against its own type, against
`std::string` and against `const char*`, in both operand orders, plus `empty()`.
The `const char*` overload is the one that answers the original worry — with it
`cell == "x"` is an exact match rather than a choice between two conversions.

Two notes on the rest of #11. The `.value` / `.text` split between a `...String`
and a `...Typed` is staying — renaming either would break glue a second time —
but it belongs in the release notes, and your `tools/accessors.py` approach of
resolving each variable's declared type from `generated/common/*.h` is the right
way to drive the migration; thank you for writing down why the compiler log
alone is not enough. We are not adding `--cells-as-strings`: with comparison
restored, most of the 214 errors do not happen.

**#10, request 2 — an unrecognised keyword inside a block still ends it
silently.** The parser warns about the stray word and closes the block, so a
`BusinessRule` can lose its `Examples:` and the message that says so is the
third warning rather than an error. The leading `\` marker you asked for removed
the common cause but not the consequence. Open, and your framing of it is the
one we will work from.

**#9's paragraph break.** A line holding only `\` is the stand-in you found, and
it is undocumented. Either a marker of its own or a documented rule, not left as
folklore.

**#8, first bullet.** A mismatched `Examples:` column heading still produces a
warning and an error for one cause. One message naming both spellings is the
fix.

**#12 — `And`.** You are right that it works and is undocumented:
`normalizeKeyword` maps `and` to the keyword of the step above it and the step
pattern accepts it, which is why your converted scenarios generate
byte-identical tests. It is being added to the syntax document, with two
rulings:

- **A leading `And` — one with no `Given`, `When` or `Then` above it — will
  warn.** Today it silently becomes a `Given`, which is a guess the file does
  not ask for.
- **`But` is not supported.** A comment in the parser lists it; the code never
  did. The comment is wrong and is being corrected, and the document will say
  `And` only.

## And the one thing that was not a finding

The most-vexing-parse line — `T(cell);` inside a `try` declaring a variable
instead of calling a constructor, so every "this value is invalid" row passed
vacuously — is going into the glue documentation. You are right that the
generated stub shape invites it, which makes it ours to warn about even though
the generator had nothing to do with the bug.

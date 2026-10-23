# AlignThree findings from a real project

Written while specifying and starting to implement SimpleAccount (a Quicken
replacement) with AlignThree 0.9.1 and then 0.9.2. Fourteen `.spectable`
files, ~16,400 lines, 496 generated C++ tests, GoogleTest, MSVC 2022.

Ready to drop in the AlignThree repository. Everything below was observed, not
guessed; each item says how to reproduce it.

## The headline

**A specification can analyse clean and still generate C++ that does not
compile.** `analyze_cli` reported 0 diagnostics over 12 files while the generated
glue referenced types that were never written. That gap is what the items below
are mostly about, and it matters because the analyzer is the fast check people
will run — the build is five minutes away.

Suggestion: an `analyze_cli --generated` mode, or a check in the analyzer for the
cases below, so that "0 diagnostics" means "will compile".

---

## 1. `EveryCell` on a bare DataType generates uncompilable glue

**Confirmed bug, and the costliest one.**

The syntax document (v3.3.a, §2.4.1) says of `EveryCell`:

> The type may be an Entity, an Attributes block, or a DataType.

The C++ generator emits the `…Typed` / `…String` structs for an `Attributes` or
`Entity` block only. So a step written against a DataType generates glue that
references a struct nobody wrote:

```
When transaction deleted : TransactionRef EveryCell
| T1 |
```

generates

```cpp
void when_transaction_deleted(const std::vector<TransactionRefString>& values) {
    const TransactionRefTyped t = TransactionRefTyped::from_string_struct(values[0]);
```

and `TransactionRefTyped` / `TransactionRefString` do not exist.

```
error C2065: 'TransactionRefTyped': undeclared identifier
error C2653: 'TransactionRefString': is not a class or namespace name
error C3861: 'from_string_struct': identifier not found
```

It hit 21 steps across 5 files, on 7 distinct DataTypes (`AccountPath`,
`TransactionRef`, `SearchText`, `SortColumn`, `ProfileName`, `PayeeName` and one
local one). `analyze_cli`: 0 diagnostics throughout.

**Repro:** declare `DataType Thing` with an `Examples: ValidValues` table, write a
step `When thing chosen : Thing EveryCell` with a one-cell table, generate for
C++, compile.

**Possible fixes, in preference order:**

1. Generate a one-field `…Typed`/`…String` pair for a DataType used with
   `EveryCell`, so the documented language works.
2. Failing that, have the analyzer reject `EveryCell` on a DataType for targets
   whose generator cannot support it, and say so in the message.
3. Failing that, correct §2.4.1 to say Entity or Attributes only.

**Workaround used:** a one-attribute `Attributes` block per DataType
(`AccountSelect` wrapping `AccountPath`, and so on) and an ordinary table. It
works but adds eleven blocks that exist only to satisfy the generator, and it
makes the specification slightly less direct to read.

---

## 2. The IDE build warns about step-name collisions; the CLI converter does not

**Confirmed inconsistency.** The IDE is right and the CLI is the one to fix.

Two steps whose text reads alike but take different tables collapse into one glue
method with one signature:

```
When account added : Account Vertical
When account added : AccountWithOpeningBalance Vertical
```

```
error C2664: cannot convert argument 1 from
  'std::vector<AccountWithOpeningBalanceString>' to
  'const std::vector<AccountString> &'
```

Building the project in the IDE produced exactly the right message, and it is a
good one:

> WARNING:369:Step 'When_account_added' expects a AccountWithOpeningBalance table
> here, but the same glue method elsewhere in this project expects a Account
> table — rename one of the steps, or switch this project to naming glue methods
> after the table they take

`SpecTableConverter.exe` generating the same files emitted nothing, and
`analyze_cli` reported nothing. Three collisions in this project, found only by
compiling, then explained by the IDE afterwards.

**Repro:** two steps with identical text and different attribute sets. Generate
with the CLI — silent. Build in the IDE — warned.

**Fix:** emit that warning from the converter and/or the analyzer. The message
needs no improvement, only to reach the person using the command line.

A note on the two fixes it offers: `--step-name-attrset` appends the attribute set
to *every* method name (`when_account_added_account`), not only the colliding
ones, which makes all the other names worse. Renaming the step was the better
choice here. Appending only where there is an actual collision would make the
flag much more usable.

---

## 3. `analyze_cli` reports a file as a duplicate of itself

**Confirmed bug.**

The IDE writes generated output into `<project>/generated/`, and the converter
copies the `.spectable` source there too by default. Pointing `analyze_cli` at the
project folder then finds both copies and reports every symbol in the project as
duplicated — including, read literally, each file duplicating itself:

```
Transactions.spectable:35: warning: Collection 'Postings' is also declared in
  spec/Transactions.spectable:35 -- only the first declaration is generated
-- 496 diagnostic(s) over 24 file(s)
```

24 files from 12. The same line number and the same file name on both sides of
"is also declared in" is the giveaway.

**Repro:** build a project in the IDE, then run `analyze_cli <project folder>`.

**Two things worth fixing separately:**

- A symbol should never be reported as a duplicate of itself. Comparing
  canonicalised absolute paths would catch it; the comment in
  `tools/analyze_cli/main.cpp` says the index already means to do this.
- Generated output living inside the folder that is analysed is the underlying
  problem. Either skip a `generated/` subfolder when walking, or default the
  IDE's output folder to a sibling of the project rather than a child.

**Workaround used:** `--no-copy-spectable`, plus keeping generated output out of
the spec folder.

---

## 4. The tilde-for-space convention is not in the syntax document

**Documentation gap, not a defect.**

A table cell is trimmed, so a value whose leading or trailing blank matters cannot
be written literally. Every generator handles this -- each one does
`s.replace('~', ' ')` on a cell -- so `~Checking` and `!Type:Bank~` work exactly
as wanted.

Nothing in `spectable syntax v3.3a.md` says so. Searching it for "tilde" or "~"
finds nothing, so the only way to learn the convention is to read the generators
or be told. Two real rules here were written with a quoting convention invented
to work around a problem that did not exist, and the quotes then arrived as part
of the value.

**Suggested fix:** one paragraph in the syntax document, in the section on tables.

---

## 5. `as previous` silently produces an empty table across scenarios

**Confirmed, and it fails quietly, which is the worst way to fail.**

A `Given` written as `Given postings are as previous` generates a no-argument call
`given_postings_are_as_previous()`. Where the table being referred to came from a
`Background`, that is fine: the background table is inlined into every test, so
the state is already there and the no-op is correct.

Where it refers to the **previous scenario's** table, there is nothing. Each
generated test stands alone, the earlier scenario's table is not inlined, and the
step quietly establishes nothing. The test then fails against a state that was
never set up, and the failure looks like a bug in production code -- two
scenarios here reported every balance as zero.

```
Scenario Balances of an account after some postings
Given postings are : PostingLine
| Account         | Amount  |
| Assets:Checking | 1000.00 |
...
Scenario A placeholder balance is the total of its descendants
Given postings are as previous      <- establishes nothing
```

`analyze_cli`: 0 diagnostics. The build: clean. The tests: wrong.

**Repro:** two scenarios, the second using `as previous` for a table the first
declared. Generate and run.

**Suggested fix:** report it. The analyzer knows whether the table being referred
to came from a `Background` or from a sibling scenario, and the second case is
always a mistake. A warning along the lines of *"'as previous' refers to a table
in another scenario, which is not carried over -- use a Define"* would have saved
the whole investigation.

**Workaround used:** a table-form `Define`, referenced with `=SomePostings` from
each scenario that needs it. That is the right way to write it anyway, and it is
what the feature exists for -- which is an argument for the analyzer pointing at
it.

### 5a. Measured scale, and a second failure mode

Revisited later in the same project, once there was enough of a specification to
count. There were **165 uses of `as previous` across twelve files**, and the
behaviour divides by whether the file has a `Background`:

| File | `as previous` | Background | Effect |
|---|---|---|---|
| UserInterface | 34 | yes | resolves to the Background |
| Reports | 26 | yes | resolves to the Background |
| ImportOfx | 21 | **no** | establishes nothing |
| Accounts | 21 | yes | resolves to the Background |
| Investments | 20 | yes | resolves to the Background |
| ImportQif | 17 | **no** | establishes nothing |
| TransactionRegister | 12 | **no** | establishes nothing |
| Books | 11 | yes | resolves to the Background |
| Payees | 8 | **no** | establishes nothing |
| ImportCsv | 5 | **no** | establishes nothing |
| Transactions | 4 | **no** | establishes nothing |
| AcceptanceAgainstQuicken | 2 | **no** | establishes nothing |

**72 of the 165 established nothing at all**, in the six files with no
`Background` to fall back on. Spelling those tables out fixed three test
failures in `ImportQif` alone and turned a long-standing puzzle into an obvious
one: seven invoice scenarios had been throwing `an account path is required`,
because `Given import target is as previous` left the target empty and the
importer was then handed `""` as an account path. That read as a defect in the
importer for some time.

The second failure mode is worse than an empty table, because it is silent in
the other direction. In a file that **does** have a `Background`, a scenario that
declares its own table, followed by a scenario saying `as previous`, gets the
**Background's** table rather than the one immediately above -- which is not what
the words say. A scenario can therefore *pass for the wrong reason*: it is
exercising data the author did not intend. That happened here when a new
`Background` was added mid-file and five scenarios downstream changed behaviour
without being touched, one of which had previously been passing against the
wrong tables.

So the two readings of `as previous` -- "the Background" and "the table just
above" -- are both plausible to a reader, and the generator implements the first
only by side effect of inlining Backgrounds. Either reading would be defensible
if it were stated and checked; what is not defensible is a given that compiles to
an empty function.

**Suggested fix, refined:** reject `as previous` outright, or require it to name
what it refers to (`as previous Background`, `as in <scenario>`). A no-arg glue
method that establishes nothing should not be reachable from a passing build. In
the meantime a lint that flags every `as previous` in a file with no `Background`
would catch the clear half of this for nothing.

**What was done here:** every `as previous` in the six Background-less files was
replaced by the table it means, mechanically, by copying the nearest preceding
table of the same kind. The five files with a `Background` were left alone,
because inlining there would change meaning rather than restore it.

---

## 6. A table with a heading and no rows is dropped without a word

**The converter should generate the step and pass it an empty table.** That is
the author's ruling, and it settles what the right behaviour is: an empty table
is a statement, not an absence.

What happens instead: `Then rule list rows are : RuleListRow` followed by a
heading row and nothing else -- the natural way to say "and now there are none"
-- produces no call at all. No empty vector, no step, nothing. The scenario
still passes, and it passes because the assertion is absent rather than because
it held.

That is the dangerous shape: the file says a thing is checked and nothing checks
it. A reader of `Payees.spectable` would have believed the rule list was
asserted empty after a delete, and it was not.

Found by reading the generated test rather than by it failing, which is luck. A
spec table that generates nothing should never be silent -- but with the fix
above it does not need to be caught at all, because it will generate a call.

The same shape appears in a `Given`, where it matters just as much:
`Given payee rules are : PayeeRules` with a heading and no rows reads as "there
are no rules", and should call the glue with an empty collection so that it
clears whatever was there. Today whether anything is cleared depends on whether
the glue happens to be called by some other step. `Payees.spectable` has had one
of these since well before this was noticed -- the scenario "With no rule the raw
name is kept" -- and it passes only because the rules happen to start empty.

Both spec tables are written the correct way in this project and left that way,
with a comment at the `Then`, so that fixing the converter turns them into real
assertions rather than needing the specification edited back.

## 7. Something that works well and is worth keeping

When a step is added to a specification whose glue is already written by hand, the
converter **appends a stub for the new step** in a fresh `public:` section rather
than refusing to touch the file or overwriting it. That is exactly the right
behaviour and it is not obvious that a generator would get it right. Worth a line
in the documentation, because it is the thing that makes hand-written glue
sustainable.

---

## 8. Small things

- **`Examples:` column headings must match attribute names exactly**, which is
  right, but a mismatch produces both a warning (*column 'Assumed here' … will be
  ignored*) and an error (*no column 'AssumedHere'*) for the same cause. One
  message naming both spellings would be clearer.
- **A `Scenario` with no Given/When/Then** is reported well — *"generates a test
  with nothing in it … the test passes regardless"*. That check earned its keep
  twice here. No change wanted; noting it because it is the best message in the
  tool.
- **`--fail-every-test` as the default** is the right call. A fresh scaffold being
  all-red is what made it obvious that 8 of 16 early passes were passing
  vacuously.

---

## 9. `$tag` filtering is excellent and is in no document

`--tag-filter "NOT WIP"` with `$WIP` above a block is the single most useful
thing added to this project's workflow. A specification that deliberately runs
ahead of the code had 217 failing tests out of 467, all of them by design, and
the suite could not tell anyone anything. With the filter, what gets compiled is
what should pass, and a red test means a regression again.

Three things about it:

- **It is in no document.** Not in `spectable syntax v3.3a.md`, not in the User
  Guide. It was found by running `strings` over the converter binary. The syntax
  reference needs the `$tag` line form, and the User Guide needs the workflow:
  tag what is not built, filter it out, take the tag off when the code lands.
- **Stub generation ignores the filter, which is right.** A `$WIP` block's steps
  still get their glue stubs, so implementing one is: write the code, connect the
  glue, remove the tag. If stubs were filtered too, every tag would have to be
  removed before the glue existed to fill in. Worth stating in the document so
  nobody "fixes" it.
- **A tag that attaches to nothing is silent.** Tags are cleared by a blank line,
  which is the right rule, but `$WIP` followed by a blank line and then a
  `Scenario` simply generates the test. Nothing warns. The failure is quiet in
  both directions: a block you believe is skipped is tested, or -- after an edit
  that inserts a line -- a block you believe is tested is skipped, and a skipped
  test looks exactly like a passing one. `analyze_cli` has no opinion about tags
  at all. **A `$tag` line not immediately followed by a taggable block should be
  a diagnostic.** It is the same shape as finding 6: the file says one thing and
  the generated tests do another, with nothing in between to say so.

Two smaller notes: the filter accepts a `$` on the tags inside the expression
(`"NOT $WIP"` works as well as `"NOT WIP"`) and matching is case-insensitive,
neither of which is guessable; and a `$WIP` above the `Specification` line tags
every block in the file, which is the right thing for a file that is entirely
unwritten and is also undocumented.

---

## 10. A named comment has no continuation, and a stray word takes a block's Examples with it

**Feature request, with a defect behind it.**

A named comment is written one keyword per line:

```
Description The most specific rule, which is taken to be the one with the
Description longest pattern. Length is a crude measure, and it is used because
Description it needs no explaining.
```

Three lines of prose, three repetitions of the keyword. In a specification with
real prose in it -- this one has about 2,000 such lines -- the keyword is most
of what a reader's eye meets, and rewrapping a paragraph means retyping the
keyword on every line that moved. The obvious thing to reach for is a trailing
backslash:

```
Description The most specific rule, which is taken to be the one with the \
longest pattern. Length is a crude measure, and it is used because it \
needs no explaining.
```

That is not supported, which is fair enough -- it is in neither the grammar
(§3, `NamedComment ::= ("Description" | "Details" | ...) CommentText Table?`)
nor the prose. **What is worth fixing is what happens instead.** Each continued
line is read as a new block, and the first word of it is reported as a keyword:

```
Payees.spectable:67: warning: Unrecognized keyword 'pattern.'
Payees.spectable:68: warning: Unrecognized keyword 'explaining:'
```

and then, from the same three lines:

```
Payees.spectable:65: warning: BusinessRule 'Which rule wins when several match'
                              has no Examples: section
```

The stray lines ended the `BusinessRule`, so the `Examples:` table below them
was no longer part of it. **A rule that was being checked stops being checked,
and the message that says so is the third of three warnings rather than an
error.** On a 16,000-line specification with a scroll of output, that is a test
quietly leaving the suite.

Two things would help, independently:

1. **A continuation marked at the start of the line, not the end of the one
   before it.** A leading `\` or `/` in the first column:

   ```
   Description The most specific rule, which is taken to be the one with the
   \ longest pattern. Length is a crude measure, and it is used because it
   \ needs no explaining.
   ```

   The failure above is the argument for putting the marker there. With a
   trailing `\` the parser cannot classify a line until it has looked at the
   line before it and remembered what it ended with, which is precisely how
   these continuation lines became stray keywords and took the `Examples:`
   table with them. A leading marker is decidable at the first character:
   a line that starts with it continues the named comment above, and a line
   that does not is still a line the analyzer can report. An unmarked stray
   sentence stays an error instead of becoming invisible.

   It also keeps the text **inside the model**. Commenting the continuation
   lines out with `#` works today -- 0 diagnostics over this whole folder, and
   the generated tests come out byte-identical -- but only the first line is
   then part of the `Description`, so anything that ever renders a named
   comment shows one line of a three-line paragraph. A continuation marker
   appends to the comment; a comment character discards it.
2. **An unrecognised keyword inside a block should not silently end it.** A line
   that is not a keyword, inside a `BusinessRule` or `Scenario`, is far more
   likely to be prose somebody wrapped than the start of something new. Either
   treat it as a continuation of the comment above it, or make it an error
   rather than a warning -- but a block losing its `Examples:` because of a
   wrapped sentence should not be survivable.

To reproduce: take any `BusinessRule` with a multi-line `Description` and an
`Examples:` table, join the comment lines with a trailing backslash, and run
`analyze_cli` on the folder. 4 diagnostics, all warnings, and one of them is the
rule going dark.

**Resolved in 0.9.2, and the leading marker is the one that shipped.** Syntax
v3.3a §4.1.1 defines a line whose first character is `\` as a continuation of
the named comment above it, which is request 1 as asked for. The interim `#`
form had grown to 2,851 lines across the fourteen files; they were converted
to `\` in one pass, and the result is 0 diagnostics with the generated tests
byte-identical -- as expected, since nothing is generated from a named comment
today. The text is now inside the model instead of being discarded, which was
the point.

Two notes from the conversion:

- **A paragraph break has no marker, and a bare `\` is what we used.** The
  continuation joins with a single space, so a paragraph of two cannot be
  written as one `Description`. A line holding `\` and nothing else analyzes
  clean and does not break the chain, so that is the stand-in here; it is
  worth either documenting or giving a marker of its own, because today it
  joins as a single space like any other continuation and a reader of the
  rendered comment will not see the break.
- **Request 2 is still open.** An unrecognised keyword inside a block still
  ends it silently -- the leading marker removes the reason a wrapped sentence
  arrives there, but not the consequence if one does.

## 11. 0.9.2 makes a cell a struct, and the glue that compared one stops compiling

**Defect, or a release note that is missing.**

Upgrading the converter from 0.9.1 to 0.9.2 -- the same spec, the same command
line -- turned a clean build into **214 compiler errors across twelve glue
files.** Nothing in the specification changed. What changed is the generated
cell type. In 0.9.1 a `DataType`'s cell arrived as a `std::string`. In 0.9.2 it
arrives as a wrapper:

```cpp
struct ClearedStatusString {
    std::string value;
    ClearedStatusString(const char* text) : value(text ? text : "") {}
    ClearedStatusString(std::string text) : value(std::move(text)) {}
    operator const std::string&() const { return value; }
    static ClearedStatusString from_text(const std::string& text) { ... }
    std::string to_string() const { return value; }
};
```

The header says, in its own comment, that this is source-compatible:

> It converts to and from a string implicitly, so glue written against the text
> keeps working and a cell can be compared, streamed or handed to the
> production class directly.

**Handed to, yes. Compared, no.** The implicit conversion works for a
parameter, which is why most glue still compiles, but `std::string`'s
`operator==` and `operator!=` are templates on `charT`, `traits` and `Alloc`,
and template argument deduction does not consider user-defined conversions.
So the one thing the comment promises is the one thing that fails:

```cpp
if (value.cleared != DNCString && !value.cleared.empty())   // both errors
```

`DNCString` is the converter's own constant, declared `const std::string` eight
lines above the struct in the same generated header.

The 214 errors are five shapes:

| count | written as | error |
|------:|------------|-------|
| ~100 | `cell != expected`, `cell == expected` | C2678, no operator found |
| 26 | `cell.empty()` | C2039, not a member |
| 15 | `EXPECT_EQ(cell, x)` | C2660 / C2737, downstream of the above |
| 10 | `std::stod(cell)` | C2665, no overload converts |
| 11 | `Money(cell)`, `account_type_from_string(cell)` on a `...Typed` | C2440 / C2664 |

The last row is a second type with a second accessor: a `...Typed` holds its
text in `.text` where a `...String` holds it in `.value`, so glue that touches
both is edited two ways.

**The comment's reasoning is worth a second look**, because the cost above
bought nothing:

> No operator== of its own, deliberately: one would make `cell == "x"`
> ambiguous against the string comparison the conversion already allows.

There is no such ambiguity to avoid. `cell == "x"` cannot reach
`std::string`'s templated `operator==` at all -- deduction fails before
conversions are considered -- so there is no competing candidate for a member
`operator==` to be ambiguous with. Omitting it does not prevent an ambiguity;
it removes the only comparison that would have worked.

Suggested, in order of how much they would have helped here:

1. **Give the wrapper `operator==` and `operator!=` against `std::string`,
   `const char*` and its own type, and an `empty()`.** Five lines each, and the
   214 errors become none. `dnc_equal(cell, other)` already works today,
   because a parameter takes the conversion -- so the wrapper is one step from
   behaving as its comment says it does.
2. **If the break is intended, say so in the release.** `0.9.1 -> 0.9.2` reads
   as a patch release. A line in the notes naming the two accessors, `.value`
   and `.text`, would have turned an afternoon of compiler errors into a
   scripted edit.
3. **A `--cells-as-strings` flag** for a project mid-migration. There is no
   option to ask for the old shape: the full `--help` has nothing between
   `--no-flatten-single-row` and `--prod-dir` that touches it.

To reproduce: take a project whose glue compares a cell to an expected value --
any `Then` that checks a table -- generate it with 0.9.1, confirm it builds,
then generate the same spec with 0.9.2 and build again.

### 11a. What the migration took, for whoever does it next

Driven by the compiler's log it is guesswork, because the log names a line and
not which of its two operands is the cell. In
`EXPECT_EQ(want.cleared, types::to_string(got.cleared))` the left side is a
`ClearedStatusString` and the right an enum of the production code, and a
greedy pass that appends `.value` to both compiles neither.

The generated struct says it exactly, so the migration is mechanical after
all: a field declared `XTyped` reads through `.text`, one declared `XString`
through `.value`, and a field the generator left as `std::string`, `bool` or
`double` is the value itself and takes no accessor. Note that one row struct
now mixes the three -- `QifDateParseTyped` has a plain `std::string text` and
a `bool valid` beside a `DateOrderTyped dateorder` -- so the rule has to be
read per field, not per struct.

`tools/accessors.py` in this project is that pass: it reads
`generated/common/*.h`, resolves each glue variable's type from its
declaration, and rewrites only what the structs account for. An expression
whose root is not a known cell variable is left alone, which is what keeps
`got.cleared`, `blank_.payee` and the two plain doubles of `AveragedHolding`
out of it.

Two things cost rounds rather than minutes. The compiler reports one template
instantiation failure per translation unit and suppresses the rest of a
cascading line, so each repair exposes the next site: 214 sites came out as
163, then 45, 45, 14, 8, 2, 2 over seven builds. And it attributes a
multi-line statement to the line it starts on, so a site carries the rest of
its statement with it -- `Money(t.knownamount)` two lines below the
`EXPECT_EQ(` that was named.

**Why it was upgraded here.** Not for this; for finding 10. The continuation
marker arrived in 0.9.2 and the spec's 2,851 continuation lines were converted
to it, which makes 0.9.2 the only converter that reads this spec correctly --
0.9.1 reports each `\` as an unrecognized keyword and generates 24 tests for
`Payees.spectable` where it has 29. So the two findings arrive together: the
fix in 10 cannot be taken without the break in 11.

---

---


## 12. `And` works and is in no document

The converter accepts `And` for the second and later steps of a run, so

```
Given the chart of accounts is : ChartOfAccounts
| ... |
And payee rules are : PayeeRules
| ... |
```

is the same as repeating `Given`, and the same holds after `When` and after
`Then`. Checked on 0.9.2 against a copy of this project's `spec/`: two Givens
turned into `And` in `Payees.spectable` and one Then in
`AcceptanceAgainstQuicken.spectable` analyze at 0 diagnostics and generate
`test_*.cpp` **byte-identical** to the versions generated from the keyword
spelled out -- 51 tests either way for Payees.

The word does not appear in `spectable syntax v3.3a.md`. Section 4 lists
`Given`, `When` and `Then` and says nothing about continuing a run, so a reader
of the document has no way to know `And` is allowed, and this project wrote
every step with its own keyword for two months for that reason. It is a
one-line addition to §4 and it changes how every scenario in a specification
reads.

---

## What was not AlignThree's fault

Recorded so the list above is not read as longer than it is.

- A most-vexing-parse in hand-written glue: `T(cell);` inside a `try` declares a
  variable named `cell` rather than calling the constructor, so nothing threw and
  every "this value is invalid" row passed vacuously. Braces fix it. The
  generator had nothing to do with this, but it is worth a line in the glue
  documentation, because the generated stub shape invites exactly this code.
- Spec rules that were simply wrong about the domain, found by comparing against
  real Quicken reports. The tool reported these faithfully by failing.

---

## Context

The specification this came from: 14 files, 16,400 lines, 449 tests, covering
double-entry transactions, QIF migration of a 43,000-record 20-year export,
OFX/QFX and CSV import, payees, reporting, and acceptance comparison against
Quicken's own PDF reports. Two of those acceptance comparisons now agree with
Quicken to the cent, computed from the spec's rules before any production code
existed — which is the thing AlignThree made possible, and worth saying alongside
the defect list.

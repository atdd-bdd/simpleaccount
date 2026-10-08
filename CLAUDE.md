# CLAUDE.md

Guidance for Claude Code when working in this repository.

## The rule that governs everything here

**No feature is implemented before it is specified.** A `.spectable` file in
`spec/`, showing worked examples of the behaviour, comes first and is reviewed
first. Only then is there code.

If asked to add behaviour, write or extend the spec table, verify it, and stop
for review. Do not write production code in the same pass unless explicitly
asked to.

## Project

SimpleAccount — a Quicken replacement. C++17, Qt 6.10, CMake, double entry
throughout. Intended mainly for the author, possibly for others later.

## Verifying a spec table

These are the author's own tools, in the sibling `SpecStudio` checkout
(AlignThree, formerly SpecStudio). **Always run the analyzer after editing a
spec table.** Target is 0 diagnostics.

```bash
export PATH="$PATH:/c/Qt/6.10.0/msvc2022_64/bin"
/c/Users/user/source/repos/SpecStudio/build/tools/analyze_cli/Release/analyze_cli.exe \
  /c/Users/user/source/repos/simpleaccount/spec
```

Generation is the second check — a file that analyzes clean can still fail to
generate. Pass every other spec file as `--context` so cross-file types resolve:

```bash
CONV=/c/Users/user/source/repos/SpecStudio/dist/AlignThree-0.9.1-windows-x64/SpecTableConverter.exe
SPEC=/c/Users/user/source/repos/simpleaccount/spec
CTX=""; for f in "$SPEC"/*.spectable; do CTX="$CTX --context $f"; done
for f in "$SPEC"/*.spectable; do "$CONV" -l Cpp --tag-filter "NOT WIP" $CTX "$f" ./generated; done
```

## $WIP: the suite that is built is the suite that should pass

A specification here runs ahead of the code on purpose, so most of it describes
behaviour that is not written yet. Those blocks are marked `$WIP` on the line
directly above them, and the converter is told `--tag-filter "NOT WIP"`, so no
test is generated for them. **A failing test is therefore a regression, not a
feature nobody has built.** Read it that way.

```
$WIP
Scenario A payee report for one category only
```

- The tag goes on its own line **immediately** above the `Scenario`,
  `BusinessRule`, `DataType` or `Calculation` it belongs to. A blank line
  between the two clears it and the test is generated after all.
- `$WIP` above the `Specification` line tags every block in the file.
- Matching is case-insensitive, and the filter is a boolean expression:
  `"NOT WIP"`, `"smoke AND NOT WIP"`.
- Glue stubs are still written for a `$WIP` block's steps -- stub generation
  ignores the filter -- so implementing one is: write the code, connect the
  glue, take the tag off, rebuild.
- The analyzer says nothing about tags either way, so a misplaced `$WIP` is
  silent. The number of tests in the run is what shows it: a block that should
  be live and is tagged simply never appears.
- `TAGS="" tools/rebuild.sh` generates the whole specification, tags and all,
  which is how to see what is still owed. Expect it to be mostly red.

Do not take a tag off to make something green, and do not add one to make a
failure go away. A `$WIP` block is a promise about what the program will do; a
test that fails without one is a bug.

The authoritative language reference is
`../SpecStudio/spectable syntax v3.3a.md`. Worked C++ examples, including the
glue-header style, are in `../SpecStudioExampleTests/CPlusPlus/ExampleTests/`.

## Spec table conventions used in this project

- `CoreTypes.spectable` declares every shared type. The analyzer treats the
  folder as one namespace, so do not redeclare a type in a second file.
- **No colons in step text.** A colon is the type separator, so
  `When rows imported into Assets:Checking` parses `Checking` as the type. Put
  the account in a `Given` with an `ImportTarget` table instead.
- Every attribute without a default must appear in a step's table. Either give
  optional attributes a non-blank default (`none` is used throughout) or put
  `CompareOnly` on the step when checking a subset is the point.
- A `Scenario` with no Given/When/Then generates an empty test. Prose-only
  observations belong in `Description` on a real scenario, or in a
  `BusinessRule` with an `Examples:` table.
- Avoid C++ keywords as Specification or attribute names — the generator escapes
  them, but the escaped name is what a reader then sees. `Register` became
  `TransactionRegister` for this reason.
- Tables group postings into transactions with a `Ref` column. `Ref` is a
  device of the specification, not a field the program shows.
- Examples table column headings must match the attribute names exactly —
  `AssumedHere`, not `Assumed here`. A mismatched heading is a silently
  ignored column plus a missing-column error.
- **Never write `as previous`.** Spell the table out, or use a table-form
  `Define`. It compiles to a no-argument call that establishes nothing, and the
  only reason it ever appears to work is that a `Background`'s givens are
  inlined into every test in the file. So in a file with no `Background` it sets
  up nothing at all, and in a file with one it silently resolves to the
  *Background's* table rather than the table immediately above — which means a
  scenario can pass while exercising data nobody intended.

  This has cost real time four separate times here. The worst case was seven
  invoice scenarios throwing `an account path is required`, which read as a
  defect in the importer for days: `Given import target is as previous` left the
  target empty and the importer was handed `""`. 72 of the 165 uses in this spec
  established nothing. They are now spelled out in the six files that have no
  `Background`; the five that have one still use it and are left alone, because
  inlining there would change meaning rather than restore it.

  If you add a `Background` to a file, check every scenario below it: they all
  inherit it, and any `as previous` downstream now refers to yours.

## Domain conventions

Debits are positive for every account type; a transaction's postings sum to
zero. Categories are Income/Expense accounts in the same tree as the real ones.
Cleared status lives on the posting. See the decisions section of `README.md`
before changing any of this — each one has other files depending on it.

## Build

Qt 6.10.0 at `C:\Qt\6.10.0\msvc2022_64`, CMake at
`C:\Qt\Tools\CMake_64\bin\cmake.exe`, MSVC 2022. There is no CMake project yet;
add one when the first production code is written.

## Implementing a step

Production code in `src/`, glue in `generated/*_glue.h`. The converter writes a
glue file once as failing stubs and never overwrites it, so **never `rm -rf
generated/`** — that deletes hand-written glue. Regenerate in place; only
`test_*.cpp` is rewritten.

Three traps already paid for:

- **Braces, not parentheses, when constructing to test validity.** `T(cell);`
  inside a `try` declares a variable named `cell` rather than calling the
  constructor, so nothing throws and every "this value is invalid" row passes
  vacuously. Write `const T parsed{cell};`.
- **A table cell cannot hold a leading or trailing blank**, so the convention is
  to wrap such a value in double quotes in the spec and call
  `spec_cell::unquote()` in the glue. It is safe on every cell.
- **Writing C++ escapes through a script is where the time goes.** A backslash
  has to survive the shell, then Python, then the regex replacement, and it
  usually does not. `'\\n'` in a heredoc has arrived as a real newline inside a
  char literal more than once, and `re.sub` interprets escapes in the
  *replacement* string too, so a repair written as a plain template breaks the
  file a second time in the same place. Pass a lambda as the replacement and
  build the backslash as `chr(92)`, or write the file with the editing tool and
  leave the shell out of it.

Findings worth sending upstream to AlignThree are collected in
`alignthree-findings.md`.

Use `tools/rebuild.sh` rather than calling cmake directly. It analyses,
regenerates, builds and runs in that order. Building without regenerating leaves
the previous tables compiled in, so the tests measure a specification that is no
longer on disk — which looks exactly like a bug in production code. That mistake
has already been made once here.

A table cell cannot hold a leading or trailing blank, so **a tilde in a cell is a
space** — every generator replaces `~` with one. Write `~Checking` and
`!Type:Bank~`. (This is not in the syntax document; it is in the generators.)

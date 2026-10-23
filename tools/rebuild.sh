#!/usr/bin/env bash
# Regenerate the tests from spec/, then build, then run.
#
# Always go through this rather than calling cmake directly. Editing a spec table
# and building without regenerating leaves the old tables compiled in, and the
# tests then pass or fail against a specification that is no longer on disk --
# which looks exactly like a bug in production code.
set -u
cd "$(dirname "$0")/.."
ROOT="$PWD"
export PATH="$PATH:/c/Qt/6.10.0/msvc2022_64/bin"

# The suite writes books, and left to itself it writes them beside the real
# one in %ProgramData%\SimpleAccount. Nothing it does names the real book, but
# a test run has no business in that folder at all, so it is sent somewhere of
# its own. store::books_folder reads this.
mkdir -p "$ROOT/scratchpad/books"
# As a Windows path: the program opens books with fopen, which has never
# heard of /c/Users.
export SIMPLEACCOUNT_BOOKS="$(cd "$ROOT/scratchpad/books" && pwd -W)"
# 0.9.2 or later, and not by preference: the spec writes its wrapped
# Descriptions as continuation lines, which 0.9.1 reads as an unrecognized
# keyword per line. That ends the block the comment belonged to, so its
# Examples: table is no longer part of it and the tests it would have made
# are never generated -- Payees alone comes out 24 tests instead of 29, with
# nothing but warnings to say so.
CONV="$ROOT/../SpecStudio/dist/AlignThree-0.9.2-windows-x64/SpecTableConverter.exe"
CMAKE="/c/Qt/Tools/CMake_64/bin/cmake.exe"
ANALYZE="$ROOT/../SpecStudio/build/tools/analyze_cli/Release/analyze_cli.exe"

echo "== analyze (absolute path: a relative one double-indexes) =="
"$ANALYZE" "$(cygpath -w "$ROOT/spec" 2>/dev/null || echo "$ROOT/spec")" || true

echo "== generate =="
# Only test_*.cpp is rewritten; *_glue.h is written once and then left alone, so
# generating never destroys hand-written glue.
#
# NOT TBD leaves out every block marked $TBD in the spec -- the behaviour that
# is specified and not yet built. So the suite that gets compiled is the one
# that should be green, and a failure means a regression rather than a feature
# nobody has written yet. Glue stubs are still written for a $TBD block's
# steps, so implementing one is a matter of taking the tag off.
#
# To see the whole specification, tags and all: TAGS="" tools/rebuild.sh
TAGS="${TAGS-NOT TBD}"
CTX=""
for f in "$ROOT"/spec/*.spectable; do CTX="$CTX --context $f"; done
for f in "$ROOT"/spec/*.spectable; do
  "$CONV" -l Cpp --no-copy-spectable --tag-filter "$TAGS" $CTX "$f" "$ROOT/generated" >/dev/null || echo "FAILED to generate $f"
done

echo "== build =="
# One compiler process. Several of them write the same vc143.pdb and the build
# then dies with C1041, which leaves the previous binary in place -- so the
# tests run and report a result that belongs to the specification of an hour
# ago. That has been read as a passing suite twice.
BUILD_LOG="$ROOT/build/last-build.log"
BIN="$ROOT/build/Debug/spec_tests.exe"
"$CMAKE" --build build --config Debug -- -m:1 -p:CL_MPCount=1 > "$BUILD_LOG" 2>&1
BUILT=$?
grep -E "error C|error MSB|warning C4|spec_tests.vcxproj ->" "$BUILD_LOG" | head -20

# The question is not what msbuild returned, it is whether the binary about to
# be run was built from what is on disk now. They are different questions, and
# the exit code answers the wrong one: vcpkg's applocal step runs pwsh.exe,
# which is not installed here, so it exits 9009 and falls back to
# powershell.exe -- the fallback works, the binary is linked, and msbuild still
# reports failure. Trusting the code meant a green build looked like a broken
# one and the tests did not run at all.
#
# So: a compile error in the log is a failure, and a binary older than anything
# it is compiled from is a failure. Nothing else is. src and generated are what
# the binary is made of; the spec reaches it through generation, which has
# already happened above.
STALE=""
if [ ! -f "$BIN" ]; then
  STALE="there is no binary at $BIN"
elif NEWER=$(find "$ROOT/src" "$ROOT/generated" -type f \
               \( -name '*.h' -o -name '*.cpp' \) -newer "$BIN" -print -quit) \
     && [ -n "$NEWER" ]; then
  STALE="the binary is older than ${NEWER#$ROOT/}"
fi
# Errors belonging to the app are reported and then set aside: while the app is
# running its exe cannot be relinked (LNK1168), which says nothing at all about
# the tests and must not stop them.
if grep -E "error C[0-9]|error MSB[0-9]|fatal error" "$BUILD_LOG" \
     | grep -q -v "simpleaccount_app.vcxproj"; then
  echo "== BUILD FAILED -- not running the tests =="
  echo "   the compiler reported errors; see $BUILD_LOG"
  exit 1
fi
if grep -E "error C[0-9]|error MSB[0-9]|fatal error" "$BUILD_LOG" \
     | grep -q "simpleaccount_app.vcxproj"; then
  echo "   (the app did not build -- is it running? the tests do not depend on"
  echo "    it, so they are run anyway; see $BUILD_LOG)"
fi
if [ -n "$STALE" ]; then
  echo "== BUILD FAILED -- not running the tests =="
  echo "   $STALE; see $BUILD_LOG"
  exit 1
fi
if [ $BUILT -ne 0 ]; then
  echo "   (msbuild exited $BUILT, but the binary is current and no compiler"
  echo "    error was logged -- see the note above this check in $0)"
fi

echo "== run =="
# The exit status is the binary's and not tail's. Piped through tail the
# status was always 0, so a suite with seven failures in it reported
# success -- which is the one thing this script exists to say.
"$BIN" "$@" 2>&1 | tail -6
exit "${PIPESTATUS[0]}"

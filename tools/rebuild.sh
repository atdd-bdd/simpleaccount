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
CONV="$ROOT/../SpecStudio/dist/AlignThree-0.9.1-windows-x64/SpecTableConverter.exe"
CMAKE="/c/Qt/Tools/CMake_64/bin/cmake.exe"
ANALYZE="$ROOT/../SpecStudio/build/tools/analyze_cli/Release/analyze_cli.exe"

echo "== analyze (absolute path: a relative one double-indexes) =="
"$ANALYZE" "$(cygpath -w "$ROOT/spec" 2>/dev/null || echo "$ROOT/spec")" || true

echo "== generate =="
# Only test_*.cpp is rewritten; *_glue.h is written once and then left alone, so
# generating never destroys hand-written glue.
CTX=""
for f in "$ROOT"/spec/*.spectable; do CTX="$CTX --context $f"; done
for f in "$ROOT"/spec/*.spectable; do
  "$CONV" -l Cpp --no-copy-spectable $CTX "$f" "$ROOT/generated" >/dev/null || echo "FAILED to generate $f"
done

echo "== build =="
# One compiler process. Several of them write the same vc143.pdb and the build
# then dies with C1041, which leaves the previous binary in place -- so the
# tests run and report a result that belongs to the specification of an hour
# ago. That has been read as a passing suite twice.
BUILD_LOG="$ROOT/build/last-build.log"
"$CMAKE" --build build --config Debug -- -m:1 -p:CL_MPCount=1 > "$BUILD_LOG" 2>&1
BUILT=$?
grep -E "error C|error MSB|warning C4|spec_tests.vcxproj ->" "$BUILD_LOG" | head -20
if [ $BUILT -ne 0 ]; then
  echo "== BUILD FAILED -- not running the tests =="
  echo "   the binary on disk is older than the spec; see $BUILD_LOG"
  exit 1
fi

echo "== run =="
"$ROOT/build/Debug/spec_tests.exe" "$@" 2>&1 | tail -4

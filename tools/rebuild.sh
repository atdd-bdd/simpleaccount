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
"$CMAKE" --build build --config Debug 2>&1 | grep -E "error C|warning C4|spec_tests.vcxproj ->" | head -20

echo "== run =="
"$ROOT/build/Debug/spec_tests.exe" "$@" 2>&1 | tail -4

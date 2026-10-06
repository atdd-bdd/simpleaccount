cmake_minimum_required(VERSION 3.20)
project(SimpleAccount CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GoogleTest is fetched rather than expected on the machine, so the suite builds
# from a clean checkout. Same approach as the AlignThree example tests.
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# /FS, which the compiler itself asks for by name in C1041. Without it a build
# of this suite dies partway through on "cannot open program database", leaving
# the previous binary on disk -- so the tests then run and report a result that
# belongs to an older specification. Forcing synchronous PDB writes costs a
# little build time and removes a failure that looks exactly like a passing
# suite.
if(MSVC)
  add_compile_options(/FS)
endif()

enable_testing()

# ---------------------------------------------------------------------------
# SQLite, vendored as the single-file amalgamation. A book is one SQLite
# database; see the storage rules in Books.spectable.
#
# Vendored rather than found on the machine, and used instead of Qt's SQL
# module, so that src/store_sqlite.h has no Qt in it and the spec suite can
# test the real schema headlessly. The alternative tested a fake and left the
# SQL itself exercised only by running the window.
# ---------------------------------------------------------------------------
add_library(sqlite3 STATIC "${CMAKE_SOURCE_DIR}/third_party/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${CMAKE_SOURCE_DIR}/third_party")
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_OMIT_LOAD_EXTENSION      # nothing here loads extensions
  SQLITE_DQS=0                    # a bare double-quoted string is an error, not a fallback
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_FOREIGN_KEYS=1)
if(MSVC)
  # Vendored third-party C: its warnings are not ours to fix.
  target_compile_options(sqlite3 PRIVATE /w)
endif()

# ---------------------------------------------------------------------------
# The tests are generated from spec/*.spectable, never written by hand.
# Every file is passed as --context to the others so that the types declared in
# CoreTypes.spectable resolve across the folder.
# ---------------------------------------------------------------------------
set(SPEC_DIR     "${CMAKE_SOURCE_DIR}/spec")
set(GENERATED_DIR "${CMAKE_SOURCE_DIR}/generated")

find_program(SPECTABLE_CONVERTER
  NAMES SpecTableConverter SpecTableConverter.exe
  HINTS "${CMAKE_SOURCE_DIR}/../SpecStudio/dist/AlignThree-0.9.1-windows-x64"
        "${CMAKE_SOURCE_DIR}/../SpecStudio/build/converter/Release"
        "${CMAKE_SOURCE_DIR}/../SpecStudio/build/src/Release"
  DOC "AlignThree spectable converter")

file(GLOB SPEC_FILES CONFIGURE_DEPENDS "${SPEC_DIR}/*.spectable")

if(SPECTABLE_CONVERTER)
  set(_ctx "")
  foreach(s ${SPEC_FILES})
    list(APPEND _ctx --context "${s}")
  endforeach()
  set(_gen_cmds COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}")
  foreach(s ${SPEC_FILES})
    list(APPEND _gen_cmds
      COMMAND "${SPECTABLE_CONVERTER}" -l Cpp --no-copy-spectable ${_ctx} "${s}" "${GENERATED_DIR}")
  endforeach()
  add_custom_target(generate_tests ${_gen_cmds}
    COMMENT "Generating tests from spec/*.spectable"
    VERBATIM)
else()
  message(WARNING
    "SpecTableConverter not found: the generate_tests target is unavailable. "
    "Build it in the sibling SpecStudio checkout, or generate by hand.")
endif()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${GENERATED_DIR}/test_*.cpp")
if(TEST_SOURCES)
  add_executable(spec_tests ${TEST_SOURCES})
  target_include_directories(spec_tests PRIVATE
    "${GENERATED_DIR}" "${CMAKE_SOURCE_DIR}/src")
  target_link_libraries(spec_tests PRIVATE GTest::gtest_main sqlite3)
  include(GoogleTest)
  gtest_discover_tests(spec_tests DISCOVERY_MODE PRE_TEST)
else()
  message(STATUS "No generated tests yet: build the generate_tests target first.")
endif()

# A command-line report, so that the import and the report can be checked
# against Quicken's own figures at full scale and not only against the examples.
add_executable(sa_report "${CMAKE_SOURCE_DIR}/cli/sa_report.cpp")
target_include_directories(sa_report PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(sa_report PRIVATE sqlite3)

add_executable(sa_register "${CMAKE_SOURCE_DIR}/cli/sa_register.cpp")
target_include_directories(sa_register PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(sa_register PRIVATE sqlite3)

# Driving a book without the window: make one, add accounts, enter
# transactions, read the registers and reports back. This is how a sequence of
# scenarios is run and compared, so it is a first-class way in rather than a
# convenience wrapper.
add_executable(sa_book "${CMAKE_SOURCE_DIR}/cli/sa_book.cpp")
target_include_directories(sa_book PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(sa_book PRIVATE sqlite3)

add_executable(sa_balances "${CMAKE_SOURCE_DIR}/cli/sa_balances.cpp")
target_include_directories(sa_balances PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(sa_balances PRIVATE sqlite3)

# The window. Qt is kept to this target: everything about what is shown lives in
# src/ui_model.h, which has no Qt in it and is tested headlessly.
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
  set(CMAKE_AUTOMOC ON)
  add_executable(simpleaccount_app WIN32
    "${CMAKE_SOURCE_DIR}/app/main.cpp"
    "${CMAKE_SOURCE_DIR}/app/MainWindow.cpp"
    "${CMAKE_SOURCE_DIR}/app/MainWindow.h")
  target_include_directories(simpleaccount_app PRIVATE
    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/app")
  target_link_libraries(simpleaccount_app PRIVATE Qt6::Widgets sqlite3)
else()
  message(STATUS "Qt6 not found: the window is not built. Pass -DQt6_DIR=<path>.")
endif()

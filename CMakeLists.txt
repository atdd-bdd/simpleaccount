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

enable_testing()

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
  target_link_libraries(spec_tests PRIVATE GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(spec_tests DISCOVERY_MODE PRE_TEST)
else()
  message(STATUS "No generated tests yet: build the generate_tests target first.")
endif()

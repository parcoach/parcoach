if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.18)
  project(PARCOACH-tests)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")
  include(CMakePolicy)
  include(Variables)
  include(ParcoachCommonOptions)
  include(Helpers)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL Clang
     OR NOT CMAKE_C_COMPILER_ID STREQUAL Clang)
   message(FATAL_ERROR "PARCOACH's tests require clang as a compiler. Please "
     "set the variables CMAKE_C_COMPILER and CMAKE_CXX_COMPILER appropriately.")
  endif()
  enable_testing()
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  find_program(PARCOACH_BIN
    parcoach REQUIRED
    HINTS ${PARCOACH_INSTALL_DIR}/bin)
  if(PARCOACH_ENABLE_MPI)
    find_library(PARCOACH_MPI_INSTR_LIB
      MPI_DynamicCheck REQUIRED
      HINTS ${PARCOACH_INSTALL_DIR}/lib)
  endif()
else()
  set(PARCOACH_BIN ${CMAKE_BINARY_DIR}/parcoach)
  if(PARCOACH_ENABLE_MPI)
    set(PARCOACH_MPI_INSTR_LIB MPI_DynamicCheck)
  endif()
endif()

set(PARCOACH_GREP_BIN ${CMAKE_CURRENT_BINARY_DIR}/parcoach_grep)
configure_file(parcoach_grep.in ${PARCOACH_GREP_BIN})

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(LIT_BIN lit REQUIRED)
find_program(FILECHECK_BIN FileCheck
  REQUIRED
  HINTS ${LLVM_TOOLS_BINARY_DIR})
set(LIT_ARGS_DEFAULT "-v")
set(PARCOACH_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

set(PARCOACH_FILE_CONTAINS ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.py)

# Top-level target for all generated test files.
# This makes it easy for jobs like coverage to have a single tests dependencies.
add_custom_target(tests-dependencies ALL)

# This provide a "high-level" test target that can be used to depend on running
# *all* tests even if nested in a folder.
add_custom_target(run-ctest
  ${CMAKE_CTEST_COMMAND} --output-on-failure --timeout 30
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
  DEPENDS ${PARCOACH_BIN} tests-dependencies
  )

add_custom_target(run-lit
  ${LIT_BIN} ${PARCOACH_LIT_ARGS} ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
  DEPENDS ${PARCOACH_BIN}
  )
add_custom_target(run-tests DEPENDS run-ctest run-lit)

parcoach_canonicalize_cmake_booleans(
  PARCOACH_ENABLE_COVERAGE
  )
set(DEFAULT_LIT_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/default.cfg")

# This lit config simply tells to look into nested folders for discovering
# other test suites.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg" @ONLY)
# This sets up default configuration and substitutions for our lit tests.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/default.cfg.in"
  ${DEFAULT_LIT_CONFIG} @ONLY)

if(PARCOACH_ENABLE_MPI)
  add_subdirectory(MPI)
endif()

if(PARCOACH_ENABLE_OPENMP)
  add_subdirectory(OMP)
endif()

add_subdirectory(features)

if(PARCOACH_ENABLE_COVERAGE)
  add_subdirectory(coverage)
endif()

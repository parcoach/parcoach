if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.18)
  project(PARCOACH-tests)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake")
  include(CMakePolicy)
  find_program(PARCOACH_BIN parcoach REQUIRED HINTS ${PARCOACH_DIR})
  find_program(CLANG_BIN clang REQUIRED HINTS ${CLANG_DIR})
  enable_testing()
else()
  set(PARCOACH_BIN ${CMAKE_BINARY_DIR}/parcoach)
  find_llvm_program(CLANG_BIN clang)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Top-level target for all generated test input files.
# This makes it easy for jobs like coverage to have a single tests dependencies.
add_custom_target(generate-test-inputs ALL)

# This provide a "high-level" test target that can be used to depend on running
# *all* tests even if nested in a folder.
add_custom_target(run-tests
  ${CMAKE_CTEST_COMMAND} --output-on-failure
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
  DEPENDS ${PARCOACH_BIN} generate-test-inputs
  )

# For now only MPI tests are supported through CMake
find_package(MPI)
if(MPI_FOUND)
  message(STATUS "Configuring MPI tests")
  configure_file(MPI/test_mpi.in test_mpi @ONLY)
  set(TEST_MPI_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_mpi)
  add_subdirectory(MPI)
  add_subdirectory(MBI)
else()
  message(STATUS "MPI was *NOT* found, disabling MPI tests")
endif()

if(PARCOACH_ENABLE_COVERAGE)
  add_subdirectory(coverage)
endif()

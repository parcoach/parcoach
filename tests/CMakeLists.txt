if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.24)
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

set(PARCOACH_COLL_INSTR_LIB_NAME ParcoachCollDynamic_MPI_C)
set(PARCOACH_RMA_C_INSTR_LIB_NAME ParcoachRMADynamic_MPI_C)
set(PARCOACH_RMA_Fortran_INSTR_LIB_NAME ParcoachRMADynamic_MPI_Fortran)

# Top-level target for all generated test files.
add_custom_target(tests-dependencies ALL)
add_custom_target(tests-ready DEPENDS tests-dependencies)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  if(PARCOACH_ENABLE_FORTRAN)
    enable_language(Fortran)
    message(STATUS "Using Fortran Compiler: ${CMAKE_Fortran_COMPILER}")
  endif()
  find_package(Parcoach ${PARCOACH_VERSION} EXACT REQUIRED
    COMPONENTS Instrumentation_C Instrumentation_Fortran)
  message(STATUS "Using PARCOACH_BIN: ${PARCOACH_BIN}")
  message(STATUS "Using PARCOACHCC_BIN: ${PARCOACHCC_BIN}")
  if(PARCOACH_ENABLE_MPI AND PARCOACH_ENABLE_INSTRUMENTATION)
    set(PARCOACH_LIB_DIR ${Parcoach_DIR}/../..)
    set(PARCOACH_COLL_INSTR_LIB Parcoach::${PARCOACH_COLL_INSTR_LIB_NAME})
    set(PARCOACH_RMA_C_INSTR_LIB Parcoach::${PARCOACH_RMA_C_INSTR_LIB_NAME})
    set(PARCOACH_RMA_Fortran_INSTR_LIB Parcoach::${PARCOACH_RMA_Fortran_INSTR_LIB_NAME})
  endif()
else()
  set(PARCOACH_BIN ${CMAKE_BINARY_DIR}/parcoach)
  set(PARCOACHCC_BIN ${CMAKE_BINARY_DIR}/parcoachcc)
  add_dependencies(tests-ready parcoach parcoachcc)
  if(PARCOACH_ENABLE_MPI AND PARCOACH_ENABLE_INSTRUMENTATION)
    set(PARCOACH_COLL_INSTR_LIB ${PARCOACH_COLL_INSTR_LIB_NAME})
    set(PARCOACH_RMA_C_INSTR_LIB ${PARCOACH_RMA_C_INSTR_LIB_NAME})
    set(PARCOACH_RMA_Fortran_INSTR_LIB ${PARCOACH_RMA_Fortran_INSTR_LIB_NAME})
    set(PARCOACH_LIB_DIR ${CMAKE_BINARY_DIR}/src/instrumentation)
    add_dependencies(tests-ready ${PARCOACH_COLL_INSTR_LIB} ${PARCOACH_RMA_C_INSTR_LIB})
    if(PARCOACH_ENABLE_FORTRAN)
      add_dependencies(tests-ready ${PARCOACH_RMA_Fortran_INSTR_LIB})
    endif()
  endif()
endif()

get_filename_component(PARCOACH_BIN_PATH ${PARCOACH_BIN} DIRECTORY)

set(PARCOACH_FILE_CONTAINS ${CMAKE_CURRENT_SOURCE_DIR}/file_contains.py)
set(PARCOACH_GREP_BIN ${CMAKE_CURRENT_BINARY_DIR}/parcoach_grep)
configure_file(parcoach_grep.in ${PARCOACH_GREP_BIN})

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_program(LIT_BIN lit REQUIRED)
find_llvm_program(FILECHECK_BIN FileCheck)
find_llvm_program(NOT_BIN not)
set(LIT_ARGS_DEFAULT "-v")
set(PARCOACH_LIT_ARGS "${LIT_ARGS_DEFAULT}" CACHE STRING "Default options for lit")

parcoach_canonicalize_cmake_booleans(
  PARCOACH_DISABLE_INSTRUMENTATION_TESTS
  PARCOACH_ENABLE_COVERAGE
  PARCOACH_ENABLE_INSTRUMENTATION
  PARCOACH_ENABLE_FORTRAN
  )
set(DEFAULT_LIT_CONFIG "${CMAKE_CURRENT_BINARY_DIR}/default.cfg")

if(PARCOACH_ENABLE_COVERAGE)
  set(PROFILE_FOLDER "${CMAKE_CURRENT_BINARY_DIR}/raw")
  set(PROFILE_FILE "${PROFILE_FOLDER}/%p.profraw")
  set(CTEST_COMMAND
    ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE="${PROFILE_FILE}" ${CMAKE_CTEST_COMMAND})
else()
  set(CTEST_COMMAND ${CMAKE_CTEST_COMMAND})
endif()

# This lit config simply tells to look into nested folders for discovering
# other test suites.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg" @ONLY)
# This sets up default configuration and substitutions for our lit tests.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/default.cfg.in"
  ${DEFAULT_LIT_CONFIG} @ONLY)

set(EXTRA_ARGS_CTEST "")
if(PARCOACH_DISABLE_INSTRUMENTATION_TESTS)
  set(EXTRA_ARGS_CTEST -E instr)
endif()

add_custom_target(run-ctest
  ${CTEST_COMMAND} --output-on-failure --timeout 30 ${EXTRA_ARGS_CTEST}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
  DEPENDS tests-ready
  )

add_custom_target(run-lit
  ${LIT_BIN} ${PARCOACH_LIT_ARGS} ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
  DEPENDS tests-ready
  )

# This provide a "high-level" test target that can be used to depend on running
# *all* tests even if nested in a folder.
add_custom_target(run-tests DEPENDS run-ctest run-lit)

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

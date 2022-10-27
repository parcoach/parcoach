cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0054 NEW)

project(PARCOACH)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(Variables)
include(ParcoachOptions)

message(STATUS "This is PARCOACH")
message(STATUS "You need LLVM ${REQUIRED_LLVM_VERSION} to build this project")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using C Compiler: ${CMAKE_C_COMPILER}")

include(FindLLVM)

# PARCOACH-specific helpers
include(LLVMHelpers)
if(${PARCOACH_ENABLE_COVERAGE})
  include(Coverage)
endif()
if(${PARCOACH_ENABLE_TIDY})
  include(Tidy)
endif()

# Activate all warnings.
add_compile_options(-Wall)

# List of sources to format; will be populated by subdirectories.
set(PARCOACH_FORMAT_SOURCES "" CACHE INTERNAL "" FORCE)

add_subdirectory(src)

if(${PARCOACH_ENABLE_TESTS})
  enable_testing()
  add_subdirectory(tests)
endif()

add_subdirectory(code-quality)

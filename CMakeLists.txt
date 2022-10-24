cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0054 NEW)

project(PARCOACH)
set(REQUIRED_LLVM_VERSION 15)
message("This is PARCOACH")
message("You need LLVM ${REQUIRED_LLVM_VERSION} to build this project")
find_package(LLVM ${REQUIRED_LLVM_VERSION} REQUIRED CONFIG)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_INSTALL_PREFIX}")
message(STATUS "Using LLVM binaries in: ${LLVM_TOOLS_BINARY_DIR}")

set(EXPECTED_COMPILER "Clang ${LLVM_VERSION}")
if(NOT "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" STREQUAL ${EXPECTED_COMPILER}
   OR NOT "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}" STREQUAL ${EXPECTED_COMPILER})
 message(FATAL_ERROR "PARCOACH needs Clang shipped with LLVM, and it looks like"
         " the detected compiler doesn't match that. Please set the variables "
         "CMAKE_C_COMPILER and CMAKE_CXX_COMPILER appropriately.")
endif()

message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using C Compiler: ${CMAKE_C_COMPILER}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}" "${CMAKE_SOURCE_DIR}/cmake")
include(AddLLVM)
# PARCOACH-specific helpers
include(Helpers)

find_llvm_program(CLANG_BIN clang)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Make the LLVM definitions globally available.
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

# Activate all warnings.
add_compile_options(-Wall)

# List of sources to format; will be populated by subdirectories.
set(PARCOACH_FORMAT_SOURCES "" CACHE INTERNAL "" FORCE)

# Add an option to enable clang-tidy integration.
option(PARCOACH_ENABLE_TIDY "Enable clang-tidy when building parcoach.")
if(${PARCOACH_ENABLE_TIDY})
  find_llvm_program(CLANG_TIDY clang-tidy)
  set(CMAKE_C_CLANG_TIDY ${CLANG_TIDY})
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY})
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

add_subdirectory(code-quality)

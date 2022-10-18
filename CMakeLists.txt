cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0054 NEW)

project(PARCOACH)
message("This is PARCOACH")
message("You need LLVM 12 to build this project")
find_package(LLVM 12 REQUIRED CONFIG)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_INSTALL_PREFIX}")
message(STATUS "Using LLVM binaries in: ${LLVM_TOOLS_BINARY_DIR}")
# FIXME: Not sure binary compiled with GCC would be always compatible with
# LLVM's opt. Maybe we should disable CMAKE_CXX_COMPILER and force using the
# clang provided with the LLVM release.
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using C Compiler: ${CMAKE_C_COMPILER}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}" "${CMAKE_SOURCE_DIR}/cmake")
include(AddLLVM)
# PARCOACH-specific helpers
include(Helpers)


# Make the LLVM definitions globally available.
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

# Activate all warnings.
add_compile_options(-Wall)

# List of sources to format; will be populated by subdirectories.
set(PARCOACH_FORMAT_SOURCES "" CACHE INTERNAL "" FORCE)

# Add an option to enable clang-tidy integration.
option(PARCOACH_ENABLE_TIDY "Enable clang-tidy when building parcoach.")
if (${PARCOACH_ENABLE_TIDY})
  find_llvm_program(CLANG_TIDY clang-tidy)
  set(CMAKE_C_CLANG_TIDY ${CLANG_TIDY})
  set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY})
endif()

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

add_subdirectory(code-quality)

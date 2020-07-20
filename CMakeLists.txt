cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0054 NEW)

project(PARCOACH)
message("This is PARCOACH")
message("You need LLVM 3.9 to build this project")


# c++11
set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


find_package(LLVM "9" REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_INSTALL_PREFIX}")
message(STATUS "Using LLVM binaries in: ${LLVM_TOOLS_BINARY_DIR}")
# FIXME: Not sure binary compiled with GCC would be always compatible with
# LLVM's opt. Maybe we should disable CMAKE_CXX_COMPILER and force using the
# clang provided with the LLVM release.
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using C Compiler: ${CMAKE_C_COMPILER}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# Make the LLVM definitions globally available.
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

# Activate all warnings
add_compile_options(-Wall)


add_subdirectory(src)

enable_testing()
add_subdirectory(tests)

#add_subdirectory(src/lib/)

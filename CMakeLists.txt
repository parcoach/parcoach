cmake_minimum_required(VERSION 3.1)
cmake_policy(SET CMP0054 NEW)

# c++11
set (CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(PARCOACH)

find_package(LLVM "3.9.1" EXACT REQUIRED  CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

add_subdirectory(src/aSSA/)

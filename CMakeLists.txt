cmake_minimum_required(VERSION 3.1)
#cmake_policy(SET CMP0054 NEW)

# c++11
#set (CMAKE_CXX_STANDARD 11)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(PARCOACH)

find_package(LLVM "3.9.1" EXACT REQUIRED  CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${LLVM_DIR})
include(${CMAKE_MODULE_PATH}/LLVM-Config.cmake)

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
include(${LLVM_DIR}/LLVM-Config.cmake)

include(HandleLLVMOptions)
include(AddLLVM)

link_directories(${LLVM_LIBRARY_DIRS})

find_package(MPI)
find_package(OpenMP)

add_subdirectory(src/aSSA/)
#add_subdirectory(src/lib/)


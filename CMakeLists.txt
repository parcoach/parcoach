cmake_minimum_required(VERSION 3.1)
#cmake_policy(SET CMP0054 NEW)

# c++11
#set (CMAKE_CXX_STANDARD 11)
#set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(PARCOACH)
enable_testing()

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

### run PARCOACH on MPI test files

file(MAKE_DIRECTORY tests/MPI/precompiled)
file(MAKE_DIRECTORY tests/MPI/output)

set(PARCOACH_FLAGS -check-mpi CACHE STRING "Option to compile with opt")
set(PATH_TO_MPI /usr/local/include CACHE STRING "Path to MPI")
set(PATH_TO_PARCOACH /Users/esaillar/Documents/PARCOACH/parcoach_gitlab/parcoach//build/src/aSSA/libaSSA.so CACHE STRING "Path to PARCOACH")


file(GLOB SRC_FILES ${PARCOACH_SOURCE_DIR}/tests/MPI/*.c)

set(RES blabla)
foreach(X IN ITEMS ${SRC_FILES})
  get_filename_component(RES ${X} NAME_WE)
  execute_process(COMMAND clang -g -c -emit-llvm -I${PATH_TO_MPI} -o ${PARCOACH_SOURCE_DIR}/tests/MPI/precompiled/${RES}.bc ${X})
  execute_process(COMMAND opt -load ${PATH_TO_PARCOACH} -postdomtree -parcoach ${PARCOACH_FLAGS} ${PARCOACH_SOURCE_DIR}/tests/MPI/precompiled/${RES}.bc -o /dev/null
                  ERROR_FILE ${PARCOACH_SOURCE_DIR}/tests/MPI/output/output_${RES}.txt)
  add_test(test_${RES} TEST ${PARCOACH_SOURCE_DIR}/tests/MPI/output/output_${RES}.txt)
endforeach()
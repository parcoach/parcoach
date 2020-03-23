cmake_minimum_required(VERSION 3.1)
cmake_policy(SET CMP0054 NEW)

project(PARCOACH)


if (UNIX)
	# c++11
	set (CMAKE_CXX_STANDARD 11)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif ()

find_package(LLVM "3.9.1" EXACT REQUIRED  CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if(APPLE)
	message ("You are running on MAC OS X")

	set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${LLVM_DIR})
	include(${CMAKE_MODULE_PATH}/LLVM-Config.cmake)
	add_definitions(${LLVM_DEFINITIONS})
	include_directories(${LLVM_INCLUDE_DIRS})
	include(${LLVM_DIR}/LLVM-Config.cmake)
	include(HandleLLVMOptions)
	include(AddLLVM)
	link_directories(${LLVM_LIBRARY_DIRS})
endif ()


find_package(MPI)
find_package(OpenMP)

enable_testing()

add_subdirectory(src/aSSA/)
#add_subdirectory(src/lib/)


### run PARCOACH on MPI test files

set(PARCOACH_FLAGS -check-mpi CACHE STRING "Option to compile with opt")
set(PARCOACH_PASS ${CMAKE_CURRENT_SOURCE_DIR}/build/src/aSSA/libaSSA.so CACHE STRING "PARCOACH pass")

file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/MPI/precompiled)
set(PRECOMPILED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/MPI/precompiled)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests/MPI/output)
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/MPI/output)

set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/)
set(TESTS_MPI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/MPI)
set(RES blabla)

file(GLOB MPI_SRC_FILES ${TESTS_MPI_DIR}/*.c)
add_executable(TEST ${TESTS_DIR}/checkpass.c)

foreach(X IN ITEMS ${MPI_SRC_FILES})
  get_filename_component(RES ${X} NAME_WE)
  execute_process(COMMAND clang -g -c -emit-llvm -I${MPI_C_HEADER_DIR} -o ${PRECOMPILED_DIR}/${RES}.bc ${X})
  execute_process(COMMAND opt -postdomtree -load ${PARCOACH_PASS} -parcoach ${PARCOACH_FLAGS} ${PRECOMPILED_DIR}/${RES}.bc -o /dev/null
                  ERROR_FILE ${OUTPUT_DIR}/output_${RES}.txt)
  add_test(test_${RES} TEST ${OUTPUT_DIR}/output_${RES}.txt)
endforeach()

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


### run PARCOACH on MPI test files

#set(PARCOACH_FLAGS -check-mpi CACHE STRING "Option to compile with opt")

#set(PARCOACH_PASS ${CMAKE_CURRENT_BINARY_DIR}/src/aSSA/aSSA${CMAKE_SHARED_LIBRARY_SUFFIX} STRING "PARCOACH pass")

#set(TESTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/)
#set(TESTS_MPI_DIR ${TESTS_DIR}/MPI)

#file(MAKE_DIRECTORY ${TESTS_MPI_DIR}//precompiled)
#set(PRECOMPILED_DIR ${TESTS_MPI_DIR}/precompiled)
#file(MAKE_DIRECTORY ${TESTS_MPI_DIR}/output)
#set(OUTPUT_DIR ${TESTS_MPI_DIR}/output)

#set(RES blabla)

#file(GLOB MPI_SRC_FILES ${TESTS_MPI_DIR}/*.c)
#add_executable(TEST ${TESTS_DIR}/checkpass.c)

# FIXME: execute_process is executed at *configuration* stage,
# so the .so/.dylib can't exist yet.
# It should probably be extracted to some configured (.in) file that runs
# the tests.
#foreach(X IN ITEMS ${MPI_SRC_FILES})
  #get_filename_component(RES ${X} NAME_WE)
  #execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/clang -g -c -emit-llvm -I${MPI_C_HEADER_DIR} -o ${PRECOMPILED_DIR}/${RES}.bc ${X})
  #execute_process(COMMAND ${LLVM_TOOLS_BINARY_DIR}/opt -postdomtree -load ${PARCOACH_PASS} -parcoach ${PARCOACH_FLAGS} ${PRECOMPILED_DIR}/${RES}.bc -o /dev/null
                  #ERROR_FILE ${OUTPUT_DIR}/output_${RES}.txt)
  #add_test(test_${RES} TEST ${OUTPUT_DIR}/output_${RES}.txt)
#endforeach()

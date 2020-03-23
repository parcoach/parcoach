cmake_minimum_required(VERSION 3.1)

find_package(LLVM "3.9.1" EXACT REQUIRED  CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
find_package(MPI)
message(STATUS "Found MPI")


set(PARCOACH_FLAGS -check-mpi CACHE STRING "opt command options")
set(PARCOACH_PASS ${CMAKE_CURRENT_SOURCE_DIR}/../../build/src/aSSA/libaSSA.so CACHE STRING "PARCOACH pass")


enable_testing()

file(MAKE_DIRECTORY precompiled)
file(MAKE_DIRECTORY output)
set(PRECOMPILED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/precompiled/)
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output/)

add_executable(TEST ../checkpass.c)

file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
set(RES blabla)

foreach(X IN ITEMS ${SRC_FILES})
  get_filename_component(RES ${X} NAME_WE)
  execute_process(COMMAND clang -g -c -emit-llvm -I${MPI_C_HEADER_DIR} -o ${PRECOMPILED_DIR}/${RES}.bc ${X})
  execute_process(COMMAND opt -postdomtree -load ${PARCOACH_PASS} -parcoach ${PARCOACH_FLAGS} ${PRECOMPILED_DIR}/${RES}.bc -o /dev/null
                  ERROR_FILE ${OUTPUT_DIR}/output_${RES}.txt)
  add_test(test_${RES} TEST ${OUTPUT_DIR}/output_${RES}.txt)
endforeach()

cmake_minimum_required(VERSION 3.1)

find_package(LLVM "3.9.1" EXACT REQUIRED  CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")

set(WORDS_SET_PASS ${CMAKE_CURRENT_SOURCE_DIR}/../../build/src/words/libwords.so)

enable_testing()

file(MAKE_DIRECTORY precompiled)
file(MAKE_DIRECTORY output)
set(PRECOMPILED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/precompiled/)
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output/)


file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
set(RES blabla)

#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../src/words/)

foreach(X IN ITEMS ${SRC_FILES})
    get_filename_component(RES ${X} NAME_WE)
		execute_process(COMMAND clang++ -std=c++11 -g -c -emit-llvm -o ${PRECOMPILED_DIR}/${RES}.bc ${X})
		execute_process(COMMAND opt -load ${WORDS_SET_PASS} -allwords ${PRECOMPILED_DIR}/${RES}.bc -o /dev/null ERROR_FILE ${OUTPUT_DIR}/output_${RES}.txt)
		add_test(test_${RES} TEST ${OUTPUT_DIR}/output_${RES}.txt)

    #execute_process(COMMAND clang++ -std=c++11 -o test_${RES} ${X})
    #add_test(test_${RES} TEST ${OUTPUT_DIR}/output_${RES}.txt)
    #add_test(test_${RES} TEST output_${RES}.txt)
    #target_link_libraries( test_${RES} words )
endforeach()

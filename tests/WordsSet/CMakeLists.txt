cmake_minimum_required(VERSION 3.1)

set(PRECOMPILED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/precompiled/)
set(OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/output/)

file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../src/words/)

foreach(X IN ITEMS ${SRC_FILES})
    get_filename_component(RES ${X} NAME_WE)
    execute_process(COMMAND clang -o test_${RES} ${X})
    add_test(test_${RES} TEST ${OUTPUT_DIR}/output_${RES}.txt)
    target_link_libraries( test_${RES} words )
endforeach()
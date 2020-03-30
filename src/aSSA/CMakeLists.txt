set(SOURCES *.cpp ../utils/*.cpp)
file (GLOB_RECURSE source_files ${SOURCES})

add_library(aSSA MODULE ${source_files})

target_include_directories(aSSA PRIVATE include ${LLVM_INCLUDE_DIRS})
target_compile_definitions(aSSA PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(aSSA PRIVATE -fno-rtti -Wall)

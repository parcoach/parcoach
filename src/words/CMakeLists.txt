file (GLOB_RECURSE source_files *.cpp ../aSSA/PTACallGraph.cpp ../aSSA/andersen/*.cpp ../aSSA/ExtInfo.cpp ../aSSA/Utils.cpp ../utils/*.cpp)

add_library(words MODULE ${source_files})

target_include_directories(words PRIVATE include ${LLVM_INCLUDE_DIRS})
target_compile_definitions(words PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(words PRIVATE -fno-rtti -Wall)

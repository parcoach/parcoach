file (GLOB_RECURSE source_files *.cpp ../aSSA/PTACallGraph.cpp ../aSSA/andersen/*.cpp ../aSSA/ExtInfo.cpp ../aSSA/Utils.cpp ../utils/*.cpp)

llvm_add_library(words SHARED LINK_LIBS ${LLVM_AVAILABLE_LIBS} OBJLIBS ${source_files})
target_include_directories(words PRIVATE include ${LLVM_INCLUDE_DIRS})
target_compile_options(words PRIVATE -fno-rtti -Wall)

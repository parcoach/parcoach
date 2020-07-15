# FIXME: Globbing is bad, we should list all the files.
file (GLOB_RECURSE source_files *.cpp)

# FIXME: if using an opt *not* built with all the LLVM libs included, we should
# set(LLVM_LINK_COMPONENTS
#  Core
#  Support
#  #... other components used
# )
# Which will tell llvm_add_library to add them to the linker command line.
# The original components in this files were:
# bitwriter core ipo irreader instcombine instrumentation target linker analysis scalaropts support
llvm_add_library(aSSA MODULE OBJLIBS ${source_files})

target_include_directories(aSSA PRIVATE include ${LLVM_INCLUDE_DIRS})
target_compile_definitions(aSSA PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(aSSA PRIVATE -Wall)


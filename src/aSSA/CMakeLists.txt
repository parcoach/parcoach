set(SOURCES *.cpp)
file (GLOB_RECURSE source_files ${SOURCES})
#file (GLOB SOURCES 
#	*.cpp)

#add_llvm_library(aSSA MODULE ${source_files})

#llvm_add_library(aSSA SHARED LINK_LIBS ${LLVM_AVAILABLE_LIBS} OBJLIBS ${source_files})

########
#add_llvm_library(aSSA MODULE LINK_LIBS ${LLVM_AVAILABLE_LIBS} OBJLIBS ${source_files})

#add_llvm_loadable_module(LLVMaSSA	${source_files})

llvm_add_library(aSSA MODULE OBJLIBS ${source_files})

#llvm_add_library(aSSA MODULE LINK_LIBS ${LLVM_AVAILABLE_LIBS} OBJLIBS ${source_files})

#add_library(aSSA MODULE ${source_files})

target_link_libraries(aSSA ${LLVM_AVAILABLE_LIBS})
#llvm_map_components_to_libnames(llvm_libs bitwriter core ipo irreader instcombine instrumentation target linker analysis scalaropts support )
target_include_directories(aSSA PRIVATE include ${LLVM_INCLUDE_DIRS})
target_compile_definitions(aSSA PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(aSSA PRIVATE -fno-rtti -Wall)


cmake_minimum_required(VERSION 3.1)

project(PARCOACH)

Find_Package(MPI REQUIRED)
#Find_Package(OpenMP REQUIRED)
#Find_Package(Threads)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "/home/phuchant/mytools/llvm-3.7/share/llvm/cmake/")

if(NOT TARGET LLVMSupport)
	include(${CMAKE_MODULE_PATH}/LLVMExports.cmake)
endif()

include(${CMAKE_MODULE_PATH}/LLVM-Config.cmake)

set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

include(HandleLLVMOptions)
include(AddLLVM)


add_definitions(${LLVM_DEFINITIONS} ${MPI_LINK_FLAGS})
include_directories(${LLVM_INCLUDE_DIRS} "/home/phuchant/mytools/llvm-3.7/include" ${MPI_INCLUDE_PATH})
link_directories(${LLVM_LIBRARY_DIRS})
set(LIBS ${LIBS} ${MPI_LIBRARIES})

add_subdirectory(src/Parcoach/)
add_subdirectory(src/Parcoach/interprocedural_analysis/)

#if(NOT DISABLE_UNIT_TESTS)
#	enable_testing()
#	add_subdirectory(tests)
#endif(NOT DISABLE_UNIT_TESTS)

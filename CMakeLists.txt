cmake_minimum_required(VERSION 3.1)

project(PARCOACH)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")


Find_Package(MPI REQUIRED)
#Find_Package(OpenMP REQUIRED)
#Find_Package(Threads)

## cmake dir - To change 
#set(PIERRE_CMAKE_DIR "/home/phuchant/mytools/llvm-3.9/build/lib/cmake/llvm")
set(EMMA_CMAKE_DIR "/Users/emmanuellesaillard/Documents/LLVM_390/build/lib/cmake/llvm")
#set(EMMA_CMAKE_DIR "/Users/emmanuellesaillard/Documents/LLVM_371/build/share/llvm/cmake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${EMMA_CMAKE_DIR})

if(NOT TARGET LLVMSupport)
	include(${CMAKE_MODULE_PATH}/LLVMExports.cmake)
endif()

include(${CMAKE_MODULE_PATH}/LLVM-Config.cmake)

set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)

include(HandleLLVMOptions)
include(AddLLVM)

# Directory LLVM source (includes) and binaries (libraries) are in
#set(EMMA_LLVM_SRC_DIR "/Users/emmanuellesaillard/Documents/LLVM_371/llvm")
#set(EMMA_LLVM_SRC_DIR "/Users/emmanuellesaillard/Documents/LLVM_390/llvm")
#set(PIERRE_LLVM_SRC_DIR "/home/phuchant/mytools/llvm-3.7")
#set(LLVM_SRC_DIR2 "/opt/local/libexec/llvm-3.9")

add_definitions(${LLVM_DEFINITIONS} ${MPI_LINK_FLAGS})
include_directories(${LLVM_INCLUDE_DIRS} ${MPI_INCLUDE_PATH})

#include_directories(${LLVM_INCLUDE_DIRS} ${EMMA_LLVM_SRC_DIR}/include ${LLVM_SRC_DIR2}/include ${MPI_INCLUDE_PATH})
link_directories(${LLVM_LIBRARY_DIRS})
set(LIBS ${LIBS} ${MPI_LIBRARIES})

add_subdirectory(src/aSSA/)	
add_subdirectory(src/Parcoach/)	

#if(NOT DISABLE_UNIT_TESTS)
#	enable_testing()
#	add_subdirectory(tests)
#endif(NOT DISABLE_UNIT_TESTS)

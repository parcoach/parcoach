find_package(LLVM ${REQUIRED_LLVM_VERSION} REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} in ${LLVM_INSTALL_PREFIX}")
message(STATUS "Using LLVM binaries in: ${LLVM_TOOLS_BINARY_DIR}")

set(EXPECTED_COMPILER "Clang ${LLVM_VERSION}")
if(NOT "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}" STREQUAL EXPECTED_COMPILER
   OR NOT "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}" STREQUAL EXPECTED_COMPILER)
  message(FATAL_ERROR "PARCOACH needs Clang shipped with LLVM, and it looks like"
         " the detected compiler doesn't match that. Please set the variables "
         "CMAKE_C_COMPILER and CMAKE_CXX_COMPILER appropriately.")
endif()

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# Make the LLVM definitions globally available.
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

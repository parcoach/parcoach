cmake_minimum_required(VERSION 3.18)

project(PARCOACH)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(CMakePolicy)
include(Variables)
include(ParcoachOptions)

set(PARCOACH_LANGUAGES C CXX)
if(PARCOACH_ENABLE_FORTRAN)
  # I'm not sure doing this is "clean" as per CMake usage, but it allows to not
  # bother installing a CMake version different from the system's if we're not
  # using fortran.
  # CMake 3.24 is the first version to be able to detect LLVMFlang.
  cmake_minimum_required(VERSION 3.24)
  LIST(APPEND PARCOACH_LANGUAGES Fortran)
endif()
enable_language(${PARCOACH_LANGUAGES})

include(Helpers)

message(STATUS "This is PARCOACH ${PACKAGE_VERSION}")
message(STATUS "You need LLVM ${REQUIRED_LLVM_VERSION} to build this project")
message(STATUS "Using CXX Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Using C Compiler: ${CMAKE_C_COMPILER}")
if(PARCOACH_ENABLE_FORTRAN)
  message(STATUS "Using Fortran Compiler: ${CMAKE_Fortran_COMPILER}")
endif()


include(FindLLVM)

# PARCOACH-specific helpers
include(LLVMHelpers)
if(PARCOACH_ENABLE_COVERAGE)
  include(Coverage)
endif()
if(PARCOACH_ENABLE_TIDY)
  include(Tidy)
endif()

# Activate all warnings.
add_compile_options(-Wall)

# List of sources to format; will be populated by subdirectories.
set(PARCOACH_FORMAT_SOURCES "" CACHE INTERNAL "" FORCE)

add_subdirectory(module)

add_subdirectory(src)

if(PARCOACH_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
  add_subdirectory(unittests)
endif()

add_subdirectory(code-quality)

set(CPACK_PACKAGE_NAME "parcoach")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
"PARCOACH (PARallel COntrol flow Anomaly CHecker) automatically checks parallel \
applications to verify the correct use of collectives. This tool uses an \
inter-procedural control and data-flow analysis to detect potential errors at compile-time."
)
set(CPACK_PACKAGE_DESCRIPTION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
set(CPACK_PACKAGE_VERSION ${PACKAGE_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PARCOACH_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PARCOACH_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PARCOACH_VERSION_PATCH})
set(CPACK_PACKAGE_VENDOR "Inria")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_BINARY_STGZ OFF)
set(CPACK_BINARY_TZ OFF)
# Include this unconditionally, let the user provide CPACK_BINARY_RPM=ON if
# they do want to generate the package.
include(RPM)
include(CPack)

configure_file(sonar-project.properties.in ${CMAKE_CURRENT_BINARY_DIR}/sonar-project.properties @ONLY)

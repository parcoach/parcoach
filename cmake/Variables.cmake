set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(REQUIRED_LLVM_VERSION 15)
if(NOT CMAKE_BUILD_TYPE)
  message(WARNING
    "You didn't specify a build type through CMAKE_BUILD_TYPE, "
    "using 'Debug' as a default.")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

set(PARCOACH_VERSION_MAJOR 2)
set(PARCOACH_VERSION_MINOR 3)
set(PARCOACH_VERSION_PATCH 1)
if(NOT DEFINED PARCOACH_VERSION_SUFFIX)
  set(PARCOACH_VERSION_SUFFIX dev)
endif()
# This is not included in the software version, it's just something for the
# package file name.
if(NOT DEFINED PARCOACH_PACKAGE_SUFFIX)
  set(PARCOACH_PACKAGE_SUFFIX "")
endif()

set(PARCOACH_VERSION "${PARCOACH_VERSION_MAJOR}.${PARCOACH_VERSION_MINOR}.${PARCOACH_VERSION_PATCH}${PARCOACH_VERSION_SUFFIX}")
set(PACKAGE_VERSION "${PARCOACH_VERSION}${PARCOACH_PACKAGE_SUFFIX}")

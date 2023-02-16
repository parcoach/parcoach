set(PARCOACH_BUILD_SHARED OFF CACHE BOOL "")
set(PARCOACH_ENABLE_INSTRUMENTATION ON CACHE BOOL "")
set(PARCOACH_ENABLE_FORTRAN ON CACHE BOOL "")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
set(CMAKE_INSTALL_LIBDIR lib CACHE STRING "")
# We're just building a release, we don't care about tests.
set(PARCOACH_ENABLE_TESTS OFF CACHE BOOL "")
set(PARCOACH_PACKAGE_SUFFIX "-static" CACHE STRING "")

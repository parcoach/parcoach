set(PARCOACH_BUILD_SHARED OFF CACHE BOOL "")
set(PARCOACH_LINK_DYLIB OFF CACHE BOOL "")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
# We're just building a release, we don't care about tests.
set(PARCOACH_ENABLE_TESTS OFF CACHE BOOL "")
set(PARCOACH_PACKAGE_SUFFIX "-static" CACHE STRING "")

set(PARCOACH_BUILD_SHARED ON CACHE BOOL "")
set(PARCOACH_ENABLE_INSTRUMENTATION ON CACHE BOOL "")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
# We're just building a release, we don't care about tests.
set(PARCOACH_ENABLE_TESTS OFF CACHE BOOL "")
set(PARCOACH_PACKAGE_SUFFIX "-shared" CACHE STRING "")

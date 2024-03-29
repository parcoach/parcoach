set(PARCOACH_ENABLE_COVERAGE ON CACHE BOOL "")
set(PARCOACH_ENABLE_INSTRUMENTATION ON CACHE BOOL "")
set(PARCOACH_ENABLE_FORTRAN ON CACHE BOOL "")
set(PARCOACH_BUILD_SHARED OFF CACHE BOOL "")

# For coverage we definitely want that.
set(PARCOACH_ENABLE_MBI_TESTS ON CACHE BOOL "")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "")
# We want -g and as few optimizations as possible to be able to have the most
# accurate line coverage.
set(CMAKE_BUILD_TYPE Debug CACHE STRING "")

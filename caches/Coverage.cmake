set(PARCOACH_ENABLE_COVERAGE ON CACHE BOOL "")
set(PARCOACH_SHARED_BUILD OFF CACHE BOOL "")
# We want -g and as few optimizations as possible to be able to have the most
# accurate line coverage.
set(CMAKE_BUILD_TYPE Debug CACHE STRING "")

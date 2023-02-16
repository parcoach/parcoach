include(ParcoachCommonOptions)
option(PARCOACH_ENABLE_COVERAGE "Enable code coverage.")
option(PARCOACH_ENABLE_TIDY "Enable clang-tidy when building parcoach.")
option(PARCOACH_BUILD_SHARED "Build parcoach as a shared library." ON)
option(PARCOACH_ENABLE_TESTS "Enable PARCOACH test targets." ON)

if(PARCOACH_ENABLE_COVERAGE AND NOT PARCOACH_ENABLE_TESTS)
  message(FATAL_ERROR "You must enable tests to enable coverage.")
endif()

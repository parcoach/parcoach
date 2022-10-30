option(PARCOACH_ENABLE_COVERAGE "Enable code coverage.")
option(PARCOACH_ENABLE_TIDY "Enable clang-tidy when building parcoach.")
option(PARCOACH_BUILD_SHARED "Build parcoach as a shared library." ON)
option(PARCOACH_ENABLE_TESTS "Enable PARCOACH test targets." ON)
option(PARCOACH_LINK_DYLIB "Enable linking PARCOACH to the LLVM dylib" ON)

if(NOT PARCOACH_LINK_DYLIB AND PARCOACH_BUILD_SHARED)
  message(FATAL_ERROR "PARCOACH is set to not link against the LLVM dylib, "
    "building shared libraries must be turned off by passing "
    "-DPARCOACH_BUILD_SHARED=OFF.")
endif()

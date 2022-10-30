if(PARCOACH_BUILD_SHARED)
  message(FATAL_ERROR
    "PARCOACH configured with shared libs, but coverage requires to statically "
    "link the lib into the binary. Turn off shared build by passing "
    "-DPARCOACH_BUILD_SHARED=OFF to cmake."
    )
endif()

string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWERED)
if(NOT "${BUILD_TYPE_LOWERED}" STREQUAL "debug")
  message(FATAL_ERROR
    "PARCOACH must be configured in Debug mode for accurate coverage. "
    "Please pass -DCMAKE_BUILD_TYPE=Debug to cmake."
    )
endif()

set(COVERAGE_OPTIONS
  "-fprofile-instr-generate"
  "-fcoverage-mapping"
  # Pass NDEBUG to remove all asserts: we don't want to include debug stuff
  # like asserts in the coverage.
  "-DNDEBUG"
  )

add_compile_options(${COVERAGE_OPTIONS})
add_link_options(${COVERAGE_OPTIONS})
find_llvm_program(LLVM_COV_BIN llvm-cov)
find_llvm_program(LLVM_PROFDATA_BIN llvm-profdata)

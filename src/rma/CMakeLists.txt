set(RMA_PASS_SOURCES
  LocalConcurrencyAnalysis.cpp
  RMAInstrumentation.cpp
  RMAStatisticsAnalysis.cpp
  )
add_sources_to_format(SOURCES ${RMA_PASS_SOURCES})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

call_llvm_helper(
  add_llvm_library
  ParcoachRMA
  ${RMA_PASS_SOURCES}
  ${PARCOACH_LIB_TYPE}
  ${LLVM_DYLIB_OPTION}
  ${PARCOACH_BUILDTREE}
  )

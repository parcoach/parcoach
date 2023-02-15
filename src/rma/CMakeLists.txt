set(RMA_PASS_SOURCES
  LocalConcurrencyAnalysis.cpp
  RMAInstrumentation.cpp
  RMAStatisticsAnalysis.cpp
  )
add_sources_to_format(SOURCES ${RMA_PASS_SOURCES})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# FIXME: this seems like a shareable setting with aSSA
if(PARCOACH_LINK_DYLIB)
  set(BUILDTREE "")
else()
  # If we don't link the dylib we're doing a full static build, in which case
  # we just want to install the tool.
  set(BUILDTREE "BUILDTREE_ONLY")
endif()

call_llvm_helper(
  add_llvm_library
  ParcoachRMA
  ${RMA_PASS_SOURCES}
  ${PARCOACH_LIB_TYPE}
  ${LLVM_DYLIB_OPTION}
  ${BUILDTREE}
  )

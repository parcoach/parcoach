if(PARCOACH_BUILD_SHARED)
  set(PARCOACH_LIB_TYPE SHARED)
else()
  set(PARCOACH_LIB_TYPE STATIC)
endif()
if(PARCOACH_LINK_DYLIB)
  set(LLVM_DYLIB_OPTION "")
else()
  set(LLVM_DYLIB_OPTION "DISABLE_LLVM_LINK_LLVM_DYLIB")
endif()

include_directories(include)
set(SHARED_HEADERS
  include/parcoach/andersen/Andersen.h
  include/parcoach/andersen/Constraint.h
  include/parcoach/andersen/CycleDetector.h
  include/parcoach/andersen/GraphTraits.h
  include/parcoach/andersen/NodeFactory.h
  include/parcoach/andersen/PtsSet.h
  include/parcoach/andersen/SparseBitVectorGraph.h
  include/parcoach/Analysis.h
  include/parcoach/Collectives.h
  include/parcoach/DepGraphDCF.h
  include/parcoach/ExtInfo.h
  include/parcoach/MemoryRegion.h
  include/parcoach/MemorySSA.h
  include/parcoach/ModRefAnalysis.h
  include/parcoach/Options.h
  include/parcoach/Passes.h
  include/parcoach/RMAPasses.h
  include/parcoach/StatisticsAnalysis.h
  )
add_sources_to_format(SOURCES ${SHARED_HEADERS})

if(PARCOACH_ENABLE_RMA)
  add_subdirectory(rma)
endif()

add_subdirectory(aSSA)

if(PARCOACH_ENABLE_INSTRUMENTATION)
  add_subdirectory(instrumentation)
endif()

add_subdirectory(tool)

if(PARCOACH_BUILD_SHARED)
  # The plugin can only be built when building shared libs.
  add_subdirectory(plugin)
endif()

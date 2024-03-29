if(PARCOACH_BUILD_SHARED)
  set(PARCOACH_LIB_TYPE SHARED)
  set(PARCOACH_BUILDTREE "")
  set(LLVM_DYLIB_OPTION "")
else()
  set(PARCOACH_LIB_TYPE STATIC)
  # This is an LLVM specific option: when adding a library with this argument
  # it is not included in the installed targets.
  # It's useful when linking statically as we just want to install the tool.
  set(PARCOACH_BUILDTREE "BUILDTREE_ONLY")
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
  include/parcoach/Collectives.h
  include/parcoach/CollectiveList.h
  include/parcoach/CollListFunctionAnalysis.h
  include/parcoach/CollListLoopAnalysis.h
  include/parcoach/CFGVisitors.h
  include/parcoach/DepGraphDCF.h
  include/parcoach/ExtInfo.h
  include/parcoach/MemoryRegion.h
  include/parcoach/MemorySSA.h
  include/parcoach/ModRefAnalysis.h
  include/parcoach/MPICommAnalysis.h
  include/parcoach/Options.h
  include/parcoach/Passes.h
  include/parcoach/RMAPasses.h
  include/parcoach/SerializableWarning.h
  include/parcoach/SonarSerializationPass.h
  include/parcoach/StatisticsAnalysis.h
  include/parcoach/Warning.h
  )
add_sources_to_format(SOURCES ${SHARED_HEADERS})

add_subdirectory(rma)
add_subdirectory(aSSA)

if(PARCOACH_ENABLE_INSTRUMENTATION)
  add_subdirectory(instrumentation)
endif()

add_subdirectory(tools)

if(PARCOACH_BUILD_SHARED)
  # The plugin can only be built when building shared libs.
  add_subdirectory(plugin)
endif()

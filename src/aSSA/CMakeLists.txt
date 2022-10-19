set(ASSA_SOURCES
  Collectives.cpp
  CollList.cpp
  DepGraph.cpp
  DepGraphDCF.cpp
  ExtInfo.cpp
  MemoryRegion.cpp
  MemorySSA.cpp
  ModRefAnalysis.cpp
  Options.cpp
  PTACallGraph.cpp
  Parcoach.cpp
  ParcoachAnalysisInter.cpp
  Register.cpp
  Utils.cpp
  andersen/Andersen.cpp
  andersen/ConstraintCollect.cpp
  andersen/ConstraintOptimize.cpp
  andersen/ConstraintSolving.cpp
  andersen/ExternalLibrary.cpp
  andersen/NodeFactory.cpp
  )
set(ASSA_HEADERS
  Collectives.h
  CollList.h
  DepGraph.h
  DepGraphDCF.h
  ExtInfo.h
  MemoryRegion.h
  MemorySSA.h
  ModRefAnalysis.h
  MSSAMuChi.h
  Options.h
  Parcoach.h
  ParcoachAnalysisInter.h
  PTACallGraph.h
  Utils.h
  andersen/Andersen.h
  andersen/Constraint.h
  andersen/CycleDetector.h
  andersen/GraphTraits.h
  andersen/NodeFactory.h
  andersen/PtsSet.h
  andersen/SparseBitVectorGraph.h
  )
add_sources_to_format(SOURCES ${ASSA_SOURCES})
add_sources_to_format(SOURCES ${ASSA_HEADERS})
add_llvm_pass_plugin(
  aSSAPlugin
  ${ASSA_SOURCES}
  SHARED
  )

if(APPLE)
  # The magic line.
  # We're building a shared lib, tell OSX to resolve the symbol when actually
  # loading the library.
  # If we don't set this and manually add the appropriate LLVM libs, loading
  # the plugin will silently fail (!!!) because of duplicate symbols.
  set_target_properties(aSSA PROPERTIES
    LINK_FLAGS "-undefined dynamic_lookup"
    )
endif()

set(PARCOACH_LIB ${CMAKE_CURRENT_BINARY_DIR}/aSSAPlugin${CMAKE_SHARED_LIBRARY_SUFFIX})
configure_file(parcoach.in ${CMAKE_BINARY_DIR}/parcoach @ONLY)

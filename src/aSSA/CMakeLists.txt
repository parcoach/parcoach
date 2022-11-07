set(ASSA_SOURCES
  Collectives.cpp
  CollList.cpp
  DepGraph.cpp
  DepGraphDCF.cpp
  Debug.cpp
  ExtInfo.cpp
  MemoryRegion.cpp
  MemorySSA.cpp
  ModRefAnalysis.cpp
  Options.cpp
  PTACallGraph.cpp
  Parcoach.cpp
  ParcoachAnalysisInter.cpp
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

configure_file(Config.h.in ${CMAKE_CURRENT_BINARY_DIR}/Config.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(PARCOACH_LINK_DYLIB)
  set(BUILDTREE "")
else()
  # If we don't link the dylib we're doing a full static build, in which case
  # we just want to install the tool.
  set(BUILDTREE "BUILDTREE_ONLY")
endif()

call_llvm_helper(
  add_llvm_library
  aSSA
  ${ASSA_SOURCES}
  ${PARCOACH_LIB_TYPE}
  ${LLVM_DYLIB_OPTION}
  ${BUILDTREE}
  )

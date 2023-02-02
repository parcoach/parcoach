set(ASSA_SOURCES
  Collectives.cpp
  CollList.cpp
  DepGraphDCF.cpp
  Debug.cpp
  ExtInfo.cpp
  Instrumentation.cpp
  MemoryRegion.cpp
  MemorySSA.cpp
  ModRefAnalysis.cpp
  OpenMPInstr.cpp
  Options.cpp
  PTACallGraph.cpp
  Parcoach.cpp
  ParcoachAnalysisInter.cpp
  StatisticsAnalysis.cpp
  ShowPAInterResults.cpp
  Utils.cpp
  andersen/Andersen.cpp
  andersen/ConstraintCollect.cpp
  andersen/ConstraintOptimize.cpp
  andersen/ConstraintSolving.cpp
  andersen/ExternalLibrary.cpp
  andersen/NodeFactory.cpp
  )
set(ASSA_HEADERS
  CollList.h
  Instrumentation.h
  MSSAMuChi.h
  OpenMPInstr.h
  ParcoachAnalysis.h
  ParcoachAnalysisInter.h
  PTACallGraph.h
  ShowPAInterResults.h
  Utils.h
  )
add_sources_to_format(SOURCES ${ASSA_SOURCES})
add_sources_to_format(SOURCES ${ASSA_HEADERS})

# NOTE: Turning on enable-hvn/enable-hu actually breaks PARCOACH at the moment.
option(ANDERSEN_ENABLE_OPTIMIZATIONS
  "Enable constraints optimization in Andersen."
  " Setting this to ON is not yet supported")

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

if(PARCOACH_ENABLE_RMA)
  target_link_libraries(aSSA PRIVATE RMA)
endif()

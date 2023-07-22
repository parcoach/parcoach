set(ASSA_SOURCES
  Collectives.cpp
  CollectiveList.cpp
  CollListFunctionAnalysis.cpp
  CollListLoopAnalysis.cpp
  CFGVisitors.cpp
  DepGraphDCF.cpp
  Debug.cpp
  ExtInfo.cpp
  Instrumentation.cpp
  MemoryRegion.cpp
  MemorySSA.cpp
  ModRefAnalysis.cpp
  MPICommAnalysis.cpp
  OpenMPInstr.cpp
  Options.cpp
  Parcoach.cpp
  PTACallGraph.cpp
  SerializableWarning.cpp
  ShowPAInterResults.cpp
  SonarSerializationPass.cpp
  StatisticsAnalysis.cpp
  Utils.cpp
  Warning.cpp
  andersen/Andersen.cpp
  andersen/ConstraintCollect.cpp
  andersen/ConstraintOptimize.cpp
  andersen/ConstraintSolving.cpp
  andersen/ExternalLibrary.cpp
  andersen/NodeFactory.cpp
  )
set(ASSA_HEADERS
  Instrumentation.h
  MSSAMuChi.h
  OpenMPInstr.h
  ParcoachAnalysis.h
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

call_llvm_helper(
  add_llvm_library
  ParcoachPasses
  ${ASSA_SOURCES}
  ${PARCOACH_LIB_TYPE}
  ${LLVM_DYLIB_OPTION}
  ${PARCOACH_BUILDTREE}
  )

target_link_libraries(ParcoachPasses PRIVATE ParcoachRMA)

# FIXME: Globbing is bad, we should list all the files.
#file (GLOB_RECURSE source_files *.cpp)

add_llvm_library(aSSA MODULE
 # ${source_files}
	Collectives.cpp
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
	Utils.cpp
	andersen/Andersen.cpp
	andersen/ConstraintCollect.cpp
	andersen/ConstraintOptimize.cpp
	andersen/ConstraintSolving.cpp
	andersen/ExternalLibrary.cpp
	andersen/NodeFactory.cpp

  PLUGIN_TOOL
  opt
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

set(PARCOACH_LIB ${CMAKE_CURRENT_BINARY_DIR}/aSSA${CMAKE_SHARED_LIBRARY_SUFFIX})
configure_file(parcoach.in ${CMAKE_BINARY_DIR}/parcoach @ONLY)

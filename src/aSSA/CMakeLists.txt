# If we don't need RTTI or EH, there's no reason to export anything
# from the hello plugin.
if( NOT LLVM_REQUIRES_RTTI )
  if( NOT LLVM_REQUIRES_EH )
	  set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Parcoach.exports)
  endif()
endif()

if(WIN32 OR CYGWIN)
  set(LLVM_LINK_COMPONENTS Core Support)
endif()

add_llvm_loadable_module( LLVMaSSA
        DepGraph.cpp
	ExtInfo.cpp
	MemoryRegion.cpp
	MemorySSA.cpp
	ModRefAnalysis.cpp
	Parcoach.cpp
	PTACallGraph.cpp
	Utils.cpp
	Collectives.cpp
	andersen/Andersen.cpp
	andersen/ConstraintCollect.cpp
	andersen/ConstraintOptimize.cpp
	andersen/ConstraintSolving.cpp
	andersen/ExternalLibrary.cpp
	andersen/NodeFactory.cpp
  )

add_definitions("-Wall -g")

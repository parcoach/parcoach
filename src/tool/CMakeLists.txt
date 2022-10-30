set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsCodeGens
  AllTargetsDescs
  AllTargetsInfos
  Analysis
  AsmParser
  Core
  IRReader
  Instrumentation
  Support
  TransformUtils
  Passes
  )
set(TOOL_SOURCES
  Parcoacht.cpp
  )
add_sources_to_format(SOURCES ${TOOL_SOURCES})

# This makes cmake generate the 'parcoach' binary at the root of the build
# folder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(LLVM_BUILD_TOOLS ON)
call_llvm_helper(
  add_llvm_tool
  parcoach
  ${TOOL_SOURCES}
  ${LLVM_DYLIB_OPTION}
  DEPENDS
  aSSA
  )

target_link_libraries(parcoach PRIVATE aSSA)

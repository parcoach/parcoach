set(WRAPPER_SOURCES
  CommandLineUtils.cpp
  TempFileRAII.cpp
  Wrapper.cpp
  )
set(WRAPPER_HEADERS
  CommandLineUtils.h
  TempFileRAII.h
  )
add_sources_to_format(SOURCES ${WRAPPER_SOURCES} ${WRAPPER_HEADERS})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(LLVM_BUILD_TOOLS ON)
call_llvm_helper(
  add_llvm_tool
  parcoachcc
  ${WRAPPER_SOURCES}
  DISABLE_LLVM_LINK_LLVM_DYLIB
  DEPENDS
  parcoach
  )

target_link_libraries(parcoachcc PRIVATE LLVMSupport)

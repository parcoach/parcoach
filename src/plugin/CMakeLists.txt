set(LLVM_LINK_COMPONENTS
  Passes
  )
set(PLUGIN_SOURCES
  Register.cpp
  )
add_sources_to_format(SOURCES ${PLUGIN_SOURCES})
call_llvm_helper(
  add_llvm_pass_plugin
  ParcoachPlugin
  ${PLUGIN_SOURCES}
  LINK_LIBS
  ParcoachPasses
  ${LLVM_DYLIB_OPTION}
  )

set(PARCOACH_LIB ${CMAKE_CURRENT_BINARY_DIR}/ParcoachPlugin${CMAKE_SHARED_LIBRARY_SUFFIX})
configure_file(parcoach.in ${CMAKE_BINARY_DIR}/parcoachp @ONLY)
install(PROGRAMS ${CMAKE_BINARY_DIR}/parcoachp TYPE BIN)

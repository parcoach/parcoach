if(PARCOACH_BUILD_SHARED)
  set(PARCOACH_LIB_TYPE SHARED)
else()
  set(PARCOACH_LIB_TYPE STATIC)
endif()
if(PARCOACH_LINK_DYLIB)
  set(LLVM_DYLIB_OPTION "")
else()
  set(LLVM_DYLIB_OPTION "DISABLE_LLVM_LINK_LLVM_DYLIB")
endif()

include_directories(include)
set(SHARED_HEADERS
  include/parcoach/Passes.h
  )
add_sources_to_format(SOURCES ${SHARED_HEADERS})

add_subdirectory(aSSA)
add_subdirectory(tool)
if(PARCOACH_BUILD_SHARED)
  # The plugin can only be built when building shared libs.
  add_subdirectory(plugin)
endif()

if(PARCOACH_BUILD_SHARED)
  set(PARCOACH_LIB_TYPE SHARED)
else()
  set(PARCOACH_LIB_TYPE STATIC)
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

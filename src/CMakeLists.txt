include_directories(include)
set(SHARED_HEADERS
  include/parcoach/Passes.h
  )
add_sources_to_format(SOURCES ${SHARED_HEADERS})
add_subdirectory(aSSA)
add_subdirectory(tool)
add_subdirectory(plugin)

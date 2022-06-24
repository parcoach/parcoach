macro(find_llvm_program var name)
  set(current_var_value ${${var}})
  if (NOT current_var_value)
    find_program("${var}" NAMES "${name}"
      HINTS ${LLVM_TOOLS_BINARY_DIR}
      DOC "${doc}"
      REQUIRED
      )
  endif()
  message(STATUS "Using ${name}: ${${var}}")
endmacro()

function(add_sources_to_format)
  cmake_parse_arguments(ARG
    ""
    ""
    "SOURCES"
    ${ARGN})
  foreach(source ${ARG_SOURCES})
    list(APPEND PARCOACH_FORMAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${source})
  endforeach()
  # It's actually painful to propagate a global variable through
  # PARENT_SCOPE (because there will be an arbitrary amount of depth),
  # so we just use an internal cache variable.
  set(PARCOACH_FORMAT_SOURCES ${PARCOACH_FORMAT_SOURCES} CACHE INTERNAL "" FORCE)
endfunction()

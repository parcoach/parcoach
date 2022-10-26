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

# This is a small wrapper to fix an annoying "feature" of llvm's helpers:
# add_llvm_executable/add_llvm_library automatically set the
# BUILD_WITH_INSTALL_RPATH property to ON on the target, which actually breaks
# the rpath for the built executables, and makes it impossible to run/load
# libraries without having to fix the LD_LIBRARY_PATH first.
function(call_llvm_helper function target)
  list(POP_FRONT ARGV)
  cmake_language(CALL ${function} ${ARGV})
  set_property(TARGET ${target} PROPERTY BUILD_WITH_INSTALL_RPATH OFF)
  if(APPLE)
    # The magic line.
    # We're building a shared lib, tell OSX to resolve the symbol when actually
    # loading the library.
    # If we don't set this and manually add the appropriate LLVM libs, loading
    # the plugin will silently fail (!!!) because of duplicate symbols.
    set_target_properties(${target} PROPERTIES
      LINK_FLAGS "-undefined dynamic_lookup"
      )
  endif()
endfunction()

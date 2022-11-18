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

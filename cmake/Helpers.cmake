# NOTE: this is taken from LLVM's cmake, which may not be available when
# using tests as a standalone project.
# This function canonicalize the CMake variables passed by names
# from CMake boolean to 0/1 suitable for passing into Python or C++,
# in place.
function(parcoach_canonicalize_cmake_booleans)
  foreach(var ${ARGN})
    if(${var})
      set(${var} 1 PARENT_SCOPE)
    else()
      set(${var} 0 PARENT_SCOPE)
    endif()
  endforeach()
endfunction(parcoach_canonicalize_cmake_booleans)

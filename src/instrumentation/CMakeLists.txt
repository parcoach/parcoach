find_package(MPI REQUIRED)

set(CMAKE_CXX_CLANG_TIDY "")
# Avoid including any C++ bindings.
add_compile_definitions(OMPI_SKIP_MPICXX MPICH_SKIP_MPICXX)

# Make sure all instrumentation lib land in the same binary dir.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(mpi)
add_subdirectory(rma)

# FIXME
#if (FALSE AND OpenMP_FOUND)
  #add_library(OMP_DynamicCheck SHARED omp/OMP_DynamicCheck.c)
  #target_link_libraries(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  #target_compile_options(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  #target_link_libraries(Instrumentation OMP_DynamicCheck)
#endif()

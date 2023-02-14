find_package(MPI REQUIRED)

# Avoid including any C++ bindings.
add_compile_definitions(OMPI_SKIP_MPICXX MPICH_SKIP_MPICXX)

add_library(Instrumentation SHARED)
install(TARGETS Instrumentation)

add_subdirectory(mpi)
if(PARCOACH_ENABLE_RMA)
  add_subdirectory(rma)
endif()

# FIXME
#if (FALSE AND OpenMP_FOUND)
  #add_library(OMP_DynamicCheck SHARED omp/OMP_DynamicCheck.c)
  #target_link_libraries(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  #target_compile_options(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  #target_link_libraries(Instrumentation OMP_DynamicCheck)
#endif()

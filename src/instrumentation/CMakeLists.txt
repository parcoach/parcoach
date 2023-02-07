find_package(MPI REQUIRED)


add_library(Instrumentation SHARED)
install(TARGETS Instrumentation)

add_subdirectory(mpi)

# FIXME
#if (FALSE AND OpenMP_FOUND)
  #add_library(OMP_DynamicCheck SHARED omp/OMP_DynamicCheck.c)
  #target_link_libraries(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  #target_compile_options(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  #target_link_libraries(Instrumentation OMP_DynamicCheck)
#endif()

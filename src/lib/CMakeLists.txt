if (MPI_FOUND)
  add_library(MPI_DynamicCheck SHARED MPI_DynamicCheck.c)
  target_include_directories(MPI_DynamicCheck PUBLIC ${MPI_INCLUDE_PATH})
  target_link_libraries(MPI_DynamicCheck PUBLIC ${MPI_LIBRARY})
endif()


if (OpenMP_FOUND)
  add_library(OMP_DynamicCheck SHARED OMP_DynamicCheck.c)
  target_link_libraries(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  target_compile_options(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
endif()


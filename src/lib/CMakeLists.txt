find_package(MPI REQUIRED)
add_sources_to_format(SOURCES MPI_DynamicCheck.c)
add_library(MPI_DynamicCheck SHARED MPI_DynamicCheck.c)
target_include_directories(MPI_DynamicCheck PUBLIC MPI::MPI_C)
target_link_libraries(MPI_DynamicCheck MPI::MPI_C)
install(TARGETS MPI_DynamicCheck)

# FIXME
if (FALSE AND OpenMP_FOUND)
  add_library(OMP_DynamicCheck SHARED OMP_DynamicCheck.c)
  target_link_libraries(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
  target_compile_options(OMP_DynamicCheck PRIVATE "${OpenMP_CXX_FLAGS}")
endif()


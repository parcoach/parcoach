if(NOT MPI_FOUND)
  message(FATAL_ERROR "find_package(MPI) should have been called before!")
endif()

add_sources_to_format(SOURCES MPI_DynamicCheck.c)
add_library(ParcoachCollDynamic_MPI_C SHARED MPI_DynamicCheck.c)
install(TARGETS ParcoachCollDynamic_MPI_C EXPORT ParcoachTargets_C COMPONENT Instrumentation_C)
set_property(TARGET ParcoachCollDynamic_MPI_C PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(ParcoachCollDynamic_MPI_C MPI::MPI_C)

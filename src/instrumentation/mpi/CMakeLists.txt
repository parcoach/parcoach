add_sources_to_format(SOURCES MPI_DynamicCheck.c)
add_library(MPI_DynamicCheck OBJECT MPI_DynamicCheck.c)
set_property(TARGET MPI_DynamicCheck PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(MPI_DynamicCheck PRIVATE MPI::MPI_C)

target_link_libraries(Instrumentation MPI_DynamicCheck)

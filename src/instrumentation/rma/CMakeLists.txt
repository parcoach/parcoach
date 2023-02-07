find_package(MPI REQUIRED)
find_package(Threads REQUIRED)

set(RMA_DYNAMIC_SOURCES
  interval.c
  interval_list.c
  interval_tree.c
  rma_analyzer.c
  rma_analyzer_mpi_c_overload.c
  #rma_analyzer_mpi_fort_overload.c
  rma_analyzer_load_store_overload.c
  )
set(RMA_DYNAMIC_HEADERS
  interval.h
  interval_list.h
  interval_tree.h
  rma_analyzer.h
  uthash.h
  util.h
  )
add_sources_to_format(SOURCES ${RMA_DYNAMIC_SOURCES} ${RMA_DYNAMIC_HEADERS})
add_library(RMADynamic OBJECT ${RMA_DYNAMIC_SOURCES})
target_compile_definitions(RMADynamic PUBLIC __DEBUG)
set_property(TARGET RMADynamic PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(RMADynamic PRIVATE MPI::MPI_C Threads::Threads)

target_link_libraries(Instrumentation RMADynamic)

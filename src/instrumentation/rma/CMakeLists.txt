if(NOT MPI_FOUND)
  message(FATAL_ERROR "find_package(MPI) should have been called before!")
endif()

find_package(Threads REQUIRED)

set(RMA_DYNAMIC_SOURCES
  interval.cpp
  interval_tree.cpp
  rma_analyzer.cpp
  rma_analyzer_mpi_c_overload.cpp
  rma_analyzer_load_store_overload.cpp
  )
set(RMA_DYNAMIC_LIBS
  MPI::MPI_C
  Threads::Threads
  )

if(PARCOACH_ENABLE_FORTRAN)
  list(APPEND RMA_DYNAMIC_SOURCES
    rma_analyzer_mpi_fort_overload.cpp
    )
  list(APPEND RMA_DYNAMIC_LIBS
    MPI::MPI_Fortran
    )
endif()

set(RMA_DYNAMIC_HEADERS
  interval.h
  interval_tree.h
  rma_analyzer.h
  util.h
  )
add_sources_to_format(SOURCES ${RMA_DYNAMIC_SOURCES} ${RMA_DYNAMIC_HEADERS})
add_library(RMADynamic OBJECT ${RMA_DYNAMIC_SOURCES})
set_property(TARGET RMADynamic PROPERTY POSITION_INDEPENDENT_CODE 1)
target_link_libraries(RMADynamic PRIVATE ${RMA_DYNAMIC_LIBS})

target_link_libraries(Instrumentation RMADynamic)

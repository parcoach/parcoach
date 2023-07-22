if(NOT MPI_FOUND)
  message(FATAL_ERROR "find_package(MPI) should have been called before!")
endif()

find_package(Threads REQUIRED)

set(RMA_HEADERS
  Interval.h
  rma_analyzer.h
  util.h
  )

set(RMA_COMMON_SOURCES
  Interval.cpp
  rma_analyzer.cpp
  rma_analyzer_load_store_overload.cpp
  )

set(RMA_C_SOURCES
  rma_analyzer_mpi_c_overload.cpp
  )

set(RMA_Fortran_SOURCES
  rma_analyzer_mpi_fort_overload.cpp
  )

add_sources_to_format(SOURCES
  ${RMA_COMMON_SOURCES}
  ${RMA_C_SOURCES}
  ${RMA_Fortran_SOURCES}
  ${RMA_HEADERS}
)

set(RMA_COMMON_LIBS Threads::Threads)
set(RMA_C_LIBS MPI::MPI_C)
set(RMA_Fortran_LIBS MPI::MPI_Fortran)

macro(create_rma_library lang)
  add_library(ParcoachRMADynamic_MPI_${lang} SHARED
    ${RMA_COMMON_SOURCES}
    ${RMA_${lang}_SOURCES}
  )
  install(
    TARGETS ParcoachRMADynamic_MPI_${lang}
    EXPORT ParcoachTargets_${lang}
    COMPONENT Instrumentation_${lang}
  )
  set_property(
    TARGET ParcoachRMADynamic_MPI_${lang}
    PROPERTY POSITION_INDEPENDENT_CODE 1
  )
  target_link_libraries(ParcoachRMADynamic_MPI_${lang}
    ${RMA_COMMON_LIBS}
    ${RMA_${lang}_LIBS}
  )
endmacro()

create_rma_library(C)

if(PARCOACH_ENABLE_FORTRAN)
  create_rma_library(Fortran)
endif()

find_dependency(Threads)
find_dependency(MPI COMPONENTS Fortran)
include("${CMAKE_CURRENT_LIST_DIR}/ParcoachTargets_Fortran.cmake")

function(parcoach_rma_fortran_instrument target)
  _get_all_args(parcoach_args "-check=rma" ${ARGV})
  _parcoach_instrument(${target}
    LANGS Fortran
    PARCOACHCC_ARGS ${parcoach_args}
    LIB Parcoach::ParcoachRMADynamic_MPI_Fortran
  )
endfunction()

find_dependency(Threads)
find_dependency(MPI COMPONENTS C)
include("${CMAKE_CURRENT_LIST_DIR}/ParcoachTargets_C.cmake")

function(parcoach_coll_c_instrument target)
  _get_all_args(parcoach_args "-instrum-inter" ${ARGV})
  _parcoach_instrument(${target}
    LANGS C CXX
    PARCOACHCC_ARGS ${parcoach_args}
    LIB Parcoach::ParcoachCollDynamic_MPI_C
  )
endfunction()

function(parcoach_rma_c_instrument target)
  _get_all_args(parcoach_args "-check=rma" ${ARGV})
  _parcoach_instrument(${target}
    LANGS C CXX
    PARCOACHCC_ARGS ${parcoach_args}
    LIB Parcoach::ParcoachRMADynamic_MPI_C
  )
endfunction()

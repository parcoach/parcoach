option(PARCOACH_ENABLE_INSTRUMENTATION "Build PARCOACH instrumentation module." ON)
option(PARCOACH_ENABLE_FORTRAN "Enable Fortran's instrumentation support through LLVM's Flang." OFF)
option(PARCOACH_ENABLE_MPI "Build PARCOACH with MPI support." ON)
option(PARCOACH_ENABLE_OPENMP "Build PARCOACH with OpenMP support." ON)
option(PARCOACH_ENABLE_CUDA "Build PARCOACH with Cuda support." OFF)
option(PARCOACH_ENABLE_UPC "Build PARCOACH with UPC support." OFF)
option(PARCOACH_ENABLE_RMA "Build PARCOACH with support for MPI RMA." ON)
# This option is only here because sometimes we want to *build* instrumentation
# support and tests, but we don't want to run them.
option(PARCOACH_DISABLE_INSTRUMENTATION_TESTS
  "Explicitly disable instrumentation tests." OFF)

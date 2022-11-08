# For now only MPI tests are supported through CMake
find_package(MPI REQUIRED)
message(STATUS "Configuring MPI tests")
configure_file(test_mpi.in test_mpi @ONLY)
set(TEST_MPI_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_mpi)
add_subdirectory(basic)

option(PARCOACH_ENABLE_MBI_TESTS "Enable MBI test targets." ON)
if(PARCOACH_ENABLE_MBI_TESTS)
  add_subdirectory(MBI)
endif()

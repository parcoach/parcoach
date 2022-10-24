find_package(Python3 COMPONENTS Interpreter REQUIRED)

# For now only MPI tests are supported through CMake
find_package(MPI)
if(MPI_FOUND)
  message(STATUS "Configuring MPI tests")
  configure_file(MPI/test_mpi.in test_mpi @ONLY)
  set(TEST_MPI_COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test_mpi)
  add_subdirectory(MPI)
  add_subdirectory(MBI)
else()
  message(STATUS "MPI was *NOT* found, disabling MPI tests")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# For now only MPI tests are supported through CMake
find_package(MPI)
if(MPI_FOUND)
  message(STATUS "Configuring MPI tests")
  add_subdirectory(MPI)
else()
  message(STATUS "MPI was *NOT* found, disabling MPI tests")
endif()

set(PARCOACH_BUILD_SHARED ON CACHE BOOL "")
set(CMAKE_BUILD_TYPE Release CACHE STRING "")
# We're just building a release, we don't care about tests.
set(PARCOACH_ENABLE_TESTS OFF CACHE BOOL "")
set(PARCOACH_VERSION_SUFFIX "" CACHE STRING "")
set(PARCOACH_ENABLE_MPI ON CACHE BOOL "")
set(PARCOACH_ENABLE_OPENMP ON CACHE BOOL "")
set(PARCOACH_ENABLE_CUDA OFF CACHE BOOL "")
set(PARCOACH_ENABLE_UPC OFF CACHE BOOL "")
# FIXME: right now plafrim doesn't have a generic MPI module, so we simply
# deactivate the MPI instrumentation at the moment.
set(PARCOACH_ENABLE_INSTRUMENTATION OFF CACHE BOOL "")

# This is overridable by the user, but it's actually useful to determine 
# target installation folders.
set(PLAFRIM_PARCOACH_VERSION 2.0.0 CACHE STRING "")
set(PLAFRIM_MODULE_ROOT "/cm/shared/dev/modules/generic" CACHE STRING "")
set(PLAFRIM_MODULE_NAME "tools/parcoach" CACHE STRING "")
message(STATUS "Using plafrim cache for parcoach version ${PLAFRIM_PARCOACH_VERSION}")

if(ENABLE_PROD)
  set(CMAKE_INSTALL_PREFIX ${PLAFRIM_MODULE_ROOT}/apps/${PLAFRIM_MODULE_NAME}/${PLAFRIM_PARCOACH_VERSION} CACHE STRING "")
  set(MODULE_FILE_PREFIX ${PLAFRIM_MODULE_ROOT}/modulefiles/${PLAFRIM_MODULE_NAME} CACHE STRING "")
else()
  set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install CACHE STRING "")
  set(MODULE_FILE_PREFIX ${CMAKE_INSTALL_PREFIX} CACHE STRING "")
endif()

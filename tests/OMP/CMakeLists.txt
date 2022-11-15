find_package(OpenMP REQUIRED)
find_program(FILECHECK_BIN FileCheck
  REQUIRED
  HINTS ${LLVM_TOOLS_BINARY_DIR})

parcoach_canonicalize_cmake_booleans(
  PARCOACH_ENABLE_COVERAGE
  )
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.cfg.py")

add_custom_target(run-tests-openmp
  ${LIT_BIN} -v ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
  DEPENDS ${PARCOACH_BIN}
  )
add_dependencies(run-tests run-tests-openmp)

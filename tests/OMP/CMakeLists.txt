find_package(OpenMP REQUIRED)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lit.cfg" @ONLY)

add_custom_target(run-tests-openmp
  ${LIT_BIN} ${PARCOACH_LIT_ARGS} ${CMAKE_CURRENT_BINARY_DIR}
  USES_TERMINAL
  DEPENDS tests-ready
  )

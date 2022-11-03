
add_test(
  NAME "version"
  COMMAND ${PARCOACH_GREP_BIN} ${PACKAGE_VERSION} -parcoach-version ${CMAKE_CURRENT_SOURCE_DIR}/inputs/void.ll)

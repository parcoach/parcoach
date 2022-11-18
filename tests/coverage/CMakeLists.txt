set(LCOV_OUTPUT ${CMAKE_BINARY_DIR}/parcoach.lcov)
set(HTML_OUTPUT ${CMAKE_BINARY_DIR}/html)
set(PROFDATA_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/parcoach.profdata)
set(JSON_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/parcoach.json)

# These are common args to llvm-cov; they set the instr profile to use,
# the binary to cover, and the sources files used for that binary.
set(LLVM_COV_ARGS
  -instr-profile parcoach.profdata
  ${CMAKE_BINARY_DIR}/parcoach
  ${CMAKE_SOURCE_DIR}/src
  )

add_custom_target(clean-coverage
  ${CMAKE_COMMAND} -E rm -rf ${HTML_OUTPUT}
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${LCOV_OUTPUT}
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_CURRENT_BINARY_DIR}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  USES_TERMINAL
  )

add_custom_target(run-coverage
  # Cleaning this automatically is actually a bit buggy,
  # you're better off running clean-coverage manually first.
  DEPENDS run-tests
  )

add_custom_command(
  OUTPUT ${PROFDATA_OUTPUT}
  COMMAND ${LLVM_PROFDATA_BIN} merge -sparse -o ${PROFDATA_OUTPUT} ${PROFILE_FOLDER}/*.profraw
  DEPENDS run-coverage
  )

add_custom_command(
  OUTPUT ${LCOV_OUTPUT}
  COMMAND ${LLVM_COV_BIN} export ${LLVM_COV_ARGS} -format lcov > ${LCOV_OUTPUT}
  MAIN_DEPENDENCY ${PROFDATA_OUTPUT}
  VERBATIM
  )

add_custom_command(
  OUTPUT ${JSON_OUTPUT}
  COMMAND ${LLVM_COV_BIN} export ${LLVM_COV_ARGS} -summary-only > ${JSON_OUTPUT}
  MAIN_DEPENDENCY ${PROFDATA_OUTPUT}
  VERBATIM
  )

add_custom_command(
  OUTPUT ${HTML_OUTPUT}
  COMMAND ${LLVM_COV_BIN} show ${LLVM_COV_ARGS} -format html -output-dir=${HTML_OUTPUT}
  MAIN_DEPENDENCY ${PROFDATA_OUTPUT}
  )

add_custom_target(show-test-coverage
  ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_coverage.py ${JSON_OUTPUT}
  VERBATIM
  DEPENDS ${JSON_OUTPUT}
  )

# For coverage we want to:
#  - build the coverage report for gitlab to have it directly in the MR.
#  - build the html report to push it to pages.
#  - show the line percentage covered to display it.
add_custom_target(coverage
  DEPENDS ${LCOV_OUTPUT} show-test-coverage ${HTML_OUTPUT}
  )
set_property(TARGET coverage APPEND PROPERTY ADDITIONAL_CLEAN_FILES ${HTML_OUTPUT})

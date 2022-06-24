find_llvm_program(CLANG_FORMAT clang-format)

configure_file(test_format test_format COPYONLY)

add_test(NAME formatting
  COMMAND test_format ${CLANG_FORMAT} --dry-run -i ${PARCOACH_FORMAT_SOURCES})

# Add a command to automate formatting
add_custom_target(run-clang-format
  COMMAND ${CLANG_FORMAT} -i ${PARCOACH_FORMAT_SOURCES}
  )

set(MODULE_FILE ${CMAKE_CURRENT_BINARY_DIR}/${PARCOACH_VERSION})
# This takes care of plafrim module related duties.
configure_file(module.in ${MODULE_FILE} @ONLY)

if(MODULE_FILE_PREFIX)
  install(FILES ${MODULE_FILE} DESTINATION ${MODULE_FILE_PREFIX})
endif()

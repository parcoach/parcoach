include(CMakePackageConfigHelpers)
set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/Parcoach)

set(PARCOACH_AVAILABLE_COMPONENTS
  Analysis
)

macro(export_instrumentation_component lang)
  list(APPEND PARCOACH_AVAILABLE_COMPONENTS Instrumentation_${lang})
  export(EXPORT ParcoachTargets_${lang}
    FILE "${CMAKE_BINARY_DIR}/Parcoach/ParcoachTargets_${lang}.cmake"
    NAMESPACE Parcoach::
  )
  install(EXPORT ParcoachTargets_${lang}
    FILE ParcoachTargets_${lang}.cmake
    NAMESPACE Parcoach::
    DESTINATION ${ConfigPackageLocation}
  )
endmacro()

if(PARCOACH_ENABLE_INSTRUMENTATION)
  export_instrumentation_component(C)
  if(PARCOACH_ENABLE_FORTRAN)
    export_instrumentation_component(Fortran)
  endif()
endif()

write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/Parcoach/ParcoachConfigVersion.cmake"
  VERSION ${PARCOACH_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  ${CMAKE_SOURCE_DIR}/cmake/Parcoach/Config.cmake.in
  "${CMAKE_BINARY_DIR}/Parcoach/ParcoachConfig.cmake"
  INSTALL_DESTINATION ${ConfigPackageLocation}
)

set(PACKAGE_INSTALL_FILES
  "${CMAKE_SOURCE_DIR}/cmake/Parcoach/ParcoachHelpers.cmake"
  "${CMAKE_BINARY_DIR}/Parcoach/ParcoachConfig.cmake"
  "${CMAKE_BINARY_DIR}/Parcoach/ParcoachConfigVersion.cmake"
)

foreach(comp IN LISTS PARCOACH_AVAILABLE_COMPONENTS)
  list(APPEND PACKAGE_INSTALL_FILES ${CMAKE_SOURCE_DIR}/cmake/Parcoach/Config-${comp}.cmake)
endforeach()

install(
  FILES ${PACKAGE_INSTALL_FILES}
  DESTINATION ${ConfigPackageLocation}
)

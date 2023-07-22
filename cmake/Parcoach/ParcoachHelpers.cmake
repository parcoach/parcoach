set(C_EXPECTED_COMPILER "Clang")
set(CXX_EXPECTED_COMPILER "Clang")
set(Fortran_EXPECTED_COMPILER "LLVMFlang")

function(check_compiler lang)
  if(NOT "${CMAKE_${lang}_COMPILER_ID}" STREQUAL ${lang}_EXPECTED_COMPILER)
    message(FATAL_ERROR
      "Parcoach needs ${${lang}_EXPECTED_COMPILER} to be the ${lang} compiler "
      "because it works on the LLVM IR. It looks like the detected compilers "
      "(${CMAKE_${lang}_COMPILER_ID}) don't match that. You need to change "
      "the CMAKE_${lang}_COMPILER if you want to use Parcoach."
    )
  endif()
endfunction()

function(check_launcher target lang)
  get_target_property(LAUNCHER ${target} ${lang}_COMPILER_LAUNCHER)
  if(NOT LAUNCHER STREQUAL "LAUNCHER-NOTFOUND")
    message(FATAL_ERROR
      "Parcoach: it seems there is already a compiler launcher set for target "
      "'${target}'. Please check you didn't instrument the same target twice, "
      "or please check the CMAKE_{C/CXX}_COMPILER_LAUNCHER variables and "
      "remove the existing compiler launcher."
    )
  endif()
endfunction()

function(_get_all_args var extra)
  cmake_parse_arguments(ARG
    ""
    ""
    "PARCOACH_ARGS"
    ${ARGN})
  foreach(flag ${extra})
    list(APPEND ARG_PARCOACH_ARGS ${flag})
  endforeach()
  set(${var} ${ARG_PARCOACH_ARGS} PARENT_SCOPE)
endfunction()

# This is an internal function that takes a target and the following arguments:
#   - LANGS (eg: C;CXX, or Fortran), the LANGS to instrument
#   - LIB, the lib against we should link to instrument
#   - PARCOACHCC_ARGS, the args to provide to parcoachcc
function(_parcoach_instrument target)
  set(WRAPPER_CALL_TEMPLATE "#!/usr/bin/env bash
set -euxo pipefail
# This is workaround because we cannot specify arguments in <LANG>_COMPILER_WRAPPER
@PARCOACHCC_BIN@ @PARCOACHCC_ARGS@ --args \"$@\"
")
  cmake_parse_arguments(ARG
    ""
    "LIB"
    "LANGS;PARCOACHCC_ARGS"
    ${ARGN})
  foreach(lang ${ARG_LANGS})
    check_compiler(${lang})
    check_launcher(${target} ${lang})
  endforeach()
  set(TARGET_LAUNCHER_BIN "${CMAKE_CURRENT_BINARY_DIR}/${target}.launcher")
  message(STATUS
    "Parcoach: instrumenting ${target} with launcher ${TARGET_LAUNCHER_BIN}.")
  string(REPLACE ";" " " PARCOACHCC_ARGS "${ARG_PARCOACHCC_ARGS}")
  string(CONFIGURE ${WRAPPER_CALL_TEMPLATE} TARGET_LAUNCHER @ONLY)
  file(GENERATE
    OUTPUT ${TARGET_LAUNCHER_BIN}
    CONTENT ${TARGET_LAUNCHER}
    FILE_PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE
    )
  foreach(lang ${ARG_LANGS})
    set_target_properties(${target}
      PROPERTIES ${lang}_COMPILER_LAUNCHER ${TARGET_LAUNCHER_BIN}
    )
  endforeach()
  target_link_libraries(${target} ${ARG_LIB})
endfunction()

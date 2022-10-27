include(ExternalProject)
message(STATUS "Adding MBI as an external project using tag 'paper'")
set(MBI_SOURCE_DIR ${CMAKE_BINARY_DIR}/third_party/MBI)
set(GENERATED_TESTS_DIR ${CMAKE_CURRENT_BINARY_DIR}/gencodes)

ExternalProject_Add(MBI
  GIT_REPOSITORY "https://gitlab.com/MpiBugsInitiative/MpiBugsInitiative.git"
  GIT_TAG "paper"
  # Because we don't want the clone to take ages ;)
  GIT_SHALLOW ON
  GIT_SUBMODULES ""
  SOURCE_DIR ${MBI_SOURCE_DIR}
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir ${MBI_SOURCE_DIR} ${Python3_EXECUTABLE} MBI.py -c generate
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${MBI_SOURCE_DIR}/gencodes ${GENERATED_TESTS_DIR}
  BUILD_COMMAND ${CMAKE_COMMAND} -E echo "Build and install disabled for MBI"
  INSTALL_COMMAND ""
  STEP_TARGETS configure
  # This lets us have some feedback about what's going on
  USES_TERMINAL_CONFIGURE 1
  EXCLUDE_FROM_ALL ON
)

# FIXME: we could probably emit all bcs somewhere in the binary dir,
# and cache it accross jobs based on the MBI-tag

# Gather all reference files.
# I know using glob for this purpose is relatively bad, but listing all ref
# files here seems quite overkill.
file(GLOB REF_FILES ${CMAKE_CURRENT_SOURCE_DIR}/ref/*)
set(BC_FLAGS -g -c -emit-llvm -I${MPI_C_HEADER_DIR})
foreach(REF ${REF_FILES})
  get_filename_component(REF_FILENAME ${REF} NAME_WE)
  string(REPLACE "_0" "" REF_BASENAME ${REF_FILENAME})
  set(REF_BC "${CMAKE_CURRENT_BINARY_DIR}/${REF_BASENAME}.bc")
  set(REF_SOURCE "${MBI_SOURCE_DIR}/gencodes/${REF_BASENAME}.c")
  add_custom_command(
    OUTPUT ${REF_BC}
    COMMAND ${CLANG_BIN} ${BC_FLAGS} -o ${REF_BC} ${REF_SOURCE}
    DEPENDS MBI-configure
    )
  add_custom_target(${REF_BASENAME} DEPENDS ${REF_BC})
  add_dependencies(generate-test-inputs ${REF_BASENAME})
  add_test(
    NAME "MBI_${REF_BASENAME}"
    COMMAND ${TEST_MPI_COMMAND} ${REF_BC} ${REF})
endforeach()

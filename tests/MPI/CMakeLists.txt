set(EXPECTED_SUCCESS_TESTS
  Aislinn_invalid_order.c
  CIVL_barrierReduce.c
  CIVL_barrierScatter.c
  CIVL_bcast_bad.c
  CIVL_BcastReduce.c
  CIVL_BcastReduce2.c
  CIVL_BcastReduce_bad.c
  error2.c
  index-dep.c
  loop_barrier.c
  mismatch.c
  mismatch_barrier.c
  mismatch_barrier2.c
  mismatch_barrier3.c
  mismatch_barrier4.c
  mismatch_barrier5.c
  mismatch_barrier6.c
  mismatch_barrier_com.c
  mismatch_barrier_nb.c
  MPIexample.c
  noerror_barrier.c
  noerror_barrier1.c
  not_verifiable.c
  phi-cond.c
  pointer-alias.c
  pointer-instance.c
  )

set(EXPECTED_FAILURE_TESTS
  field-sensitive.c
  )

set(CLANG_BIN ${LLVM_TOOLS_BINARY_DIR}/clang)
set(BC_FLAGS -g -c -emit-llvm -I${MPI_C_HEADER_DIR})
set(ALL_TESTS_TARGETS)

configure_file(test_mpi.in test_mpi @ONLY)

foreach(SRC IN ITEMS ${EXPECTED_SUCCESS_TESTS} ${EXPECTED_FAILURE_TESTS})
  get_filename_component(TEST_NAME ${SRC} NAME_WE)
  set(TEST_BC ${TEST_NAME}.bc)
  set(TEST_TARGET tests_MPI_${TEST_NAME})
  # Append the target to all tests targets
  set(ALL_TESTS_TARGETS ${ALL_TESTS_TARGETS} ${TEST_TARGET})
  message("Adding test ${TEST_NAME}")
  add_custom_command(
    OUTPUT ${TEST_BC}
    COMMAND ${CLANG_BIN} ${BC_FLAGS} -o ${TEST_BC} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
    DEPENDS aSSA ${SRC}
    )
  add_custom_target(${TEST_TARGET} DEPENDS ${TEST_BC})
  add_test(NAME ${TEST_TARGET} COMMAND test_mpi ${CMAKE_CURRENT_BINARY_DIR}/${TEST_BC} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}.expected)
endforeach()

# Explicit mark expected failures
foreach(SRC IN ITEMS ${EXPECTED_FAILURE_TESTS})
  get_filename_component(TEST_NAME ${SRC} NAME_WE)
  set_tests_properties(tests_MPI_${TEST_NAME} PROPERTIES WILL_FAIL TRUE)
endforeach()

# Add a phony depend so that precompiled .bc are always built when running building aSSA
add_custom_target(tests_MPI ALL DEPENDS ${ALL_TESTS_TARGETS})

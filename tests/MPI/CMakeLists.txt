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
  loop_exit.c
  loop_exit2.c
  loop_finalize.c
  loop_barrier.c
  loop_barrier2.c
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

set(BC_FLAGS -g -c -emit-llvm -I${MPI_C_HEADER_DIR})

foreach(SRC IN ITEMS ${EXPECTED_SUCCESS_TESTS} ${EXPECTED_FAILURE_TESTS})
  get_filename_component(TEST_NAME ${SRC} NAME_WE)
  set(TEST_BC ${TEST_NAME}.bc)
  set(TEST_TARGET tests_MPI_${TEST_NAME})
  add_custom_command(
    OUTPUT ${TEST_BC}
    COMMAND ${CLANG_BIN} ${BC_FLAGS} -o ${TEST_BC} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
    DEPENDS ${SRC}
    )
  add_custom_target(${TEST_TARGET} DEPENDS ${TEST_BC})
  add_dependencies(generate-test-inputs ${TEST_TARGET})
  add_test(NAME ${TEST_TARGET} COMMAND ${TEST_MPI_COMMAND} ${CMAKE_CURRENT_BINARY_DIR}/${TEST_BC} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}.expected)
endforeach()

# Explicit mark expected failures
foreach(SRC IN ITEMS ${EXPECTED_FAILURE_TESTS})
  get_filename_component(TEST_NAME ${SRC} NAME_WE)
  set_tests_properties(tests_MPI_${TEST_NAME} PROPERTIES WILL_FAIL TRUE)
endforeach()

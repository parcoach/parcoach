set(EXPECTED_SUCCESS_TESTS
  src/Aislinn_invalid_order.c
  src/CIVL_barrierReduce.c
  src/CIVL_barrierScatter.c
  src/CIVL_bcast_bad.c
  src/CIVL_BcastReduce.c
  src/CIVL_BcastReduce2.c
  src/CIVL_BcastReduce_bad.c
  src/error2.c
  src/index-dep.c
  src/indirect_barrier.c
  src/indirect_ok.c
  src/loop_exit.c
  src/loop_exit2.c
  src/loop_finalize.c
  src/loop_barrier.c
  src/loop_barrier2.c
  src/mismatch.c
  src/mismatch_barrier.c
  src/mismatch_barrier2.c
  src/mismatch_barrier3.c
  src/mismatch_barrier4.c
  src/mismatch_barrier5.c
  src/mismatch_barrier6.c
  src/mismatch_barrier_com.c
  src/mismatch_barrier_nb.c
  src/MPIexample.c
  src/noerror_barrier.c
  src/noerror_barrier1.c
  src/not_verifiable.c
  src/phi-cond.c
  src/pointer-alias.c
  src/pointer-instance.c
  )

set(EXPECTED_FAILURE_TESTS
  src/field-sensitive.c
  )

set(INSTRUMENTED_TESTS
  src/Aislinn_invalid_order.c
  src/error2.c
  )

set(BC_FLAGS -g -c -emit-llvm -I${MPI_C_HEADER_DIR})

foreach(SRC IN ITEMS ${EXPECTED_SUCCESS_TESTS} ${EXPECTED_FAILURE_TESTS})
  get_filename_component(TEST_NAME ${SRC} NAME_WE)
  set(TEST_BC ${TEST_NAME}.bc)
  set(TEST_TARGET tests_MPI_${TEST_NAME})
  add_custom_command(
    OUTPUT ${TEST_BC}
    # PARCOACH's cmake enforces the compiler to be clang.
    COMMAND ${CMAKE_C_COMPILER} ${BC_FLAGS} -o ${TEST_BC} ${CMAKE_CURRENT_SOURCE_DIR}/${SRC}
    DEPENDS ${SRC}
    )
  add_custom_target(${TEST_TARGET} DEPENDS ${TEST_BC})
  add_dependencies(tests-dependencies ${TEST_TARGET})
  add_test(NAME ${TEST_TARGET} COMMAND ${TEST_MPI_COMMAND} ${CMAKE_CURRENT_BINARY_DIR}/${TEST_BC} ${CMAKE_CURRENT_SOURCE_DIR}/expectations/${TEST_NAME}.expected)

  if(PARCOACH_ENABLE_INSTRUMENTATION AND SRC IN_LIST INSTRUMENTED_TESTS)
    set(EXE_TARGET tests_MPI_instr_${TEST_NAME})
    create_mpi_instrumented_executable(${EXE_TARGET} ${TEST_BC} C)
    add_test(NAME ${EXE_TARGET}
      COMMAND ${TEST_INSTR_MPI_COMMAND} ${CMAKE_CURRENT_BINARY_DIR}/${EXE_TARGET} ${CMAKE_CURRENT_SOURCE_DIR}/expectations-instr/${TEST_NAME}.expected)
    if(PARCOACH_ENABLE_COVERAGE)
      # Make the PARCOACH instrumentation part of the tests for coverage!
      # This is just to have accurate coverage data as the call to
      # create_mpi_instrumented_executable is not counted in there.
      add_test(NAME gen_instr_bc_${TEST_NAME}
        COMMAND ${PARCOACH_BIN} ${TEST_BC} --disable-output -instrum-inter -check-mpi
        )
    endif()
  endif()
endforeach()

# Explicit mark expected failures
foreach(SRC IN ITEMS ${EXPECTED_FAILURE_TESTS})
  get_filename_component(TEST_NAME ${SRC} NAME_WE)
  set_tests_properties(tests_MPI_${TEST_NAME} PROPERTIES WILL_FAIL TRUE)
endforeach()

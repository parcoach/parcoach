find_package(MPI REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/src/instrumentation/rma)

add_executable(
  unit_tests_rma
  intersections.cpp
)

add_dependencies(tests-dependencies unit_tests_rma)

add_sources_to_format(SOURCES intersections.cpp)

target_link_libraries(
  unit_tests_rma
  GTest::gtest_main
  GTest::gmock
  MPI::MPI_CXX
  RMADynamic
)

gtest_discover_tests(unit_tests_rma)

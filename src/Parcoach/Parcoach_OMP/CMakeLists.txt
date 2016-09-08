set(LLVM_LINK_COMPONENTS support)

add_clang_executable(omp-analysis
	 OMP_Analysis.cpp
)

target_link_libraries(omp-analysis
	 clangTooling
)

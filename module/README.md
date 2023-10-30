The CMake cache `caches/Release-plafrim.cmake` should contain most of the necessary variables; this is how I configured it for testing the install:

```bash
module load toolchains/llvm build/cmake/3.26.0 mpi/openmpi/4.1.5
cmake .. -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -C ../caches/Release-plafrim.cmake
ninja install
```

For actually doing the install:

```bash
module load toolchains/llvm build/cmake/3.26.0 mpi/openmpi/4.1.5
cmake .. -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DENABLE_PROD=ON -C ../caches/Release-plafrim.cmake
ninja install
```

And then fixup the permissions:

```bash
module load tools/module_cat
module_perm /cm/shared/dev/modules/generic/apps/tools/parcoach/VERSION
module_perm /cm/shared/dev/modules/generic/modulefiles/tools/parcoach/VERSION
```

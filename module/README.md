The CMake cache `caches/Release-plafrim.cmake` should contain most of the necessary variables; this is how I configured it for testing the install:

```bash
module load toolchains/llvm
cmake .. -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -C ../caches/Release-plafrim.cmake
ninja install
```

For actually doing the install:

```bash
module load toolchains/llvm
cmake .. -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DENABLE_PROD=ON -C ../caches/Release-plafrim.cmake
ninja install
```

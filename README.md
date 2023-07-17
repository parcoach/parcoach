# PARCOACH (PARallel COntrol flow Anomaly CHecker)

**PARCOACH** automatically checks parallel applications to verify the correct use of collectives. This tool uses an inter-procedural control- and data-flow analysis to detect potential errors at compile-time.

## Getting Started

These instructions will get you a copy of the project up and running on your local machine.

### Prerequisites

The most straightforward way to setup a working build environment for PARCOACH
is to use the docker images used by our Continuous Integration.

As a simple user, the easiest is to use the docker compose shipped with this
project; you can use the following command from the root of this project to start
a shell in a docker container, with your local PARCOACH folder mounted at `/home/parcoach/sources`:

```bash
docker compose run --rm shell
```

It will build a docker image with the environment to build PARCOACH, without having to install
everything on your machine, but while still being able to use your editor out
of the docker container (because the source tree will be shared between the
host and the docker container).

If you want to take a look at how the docker images are built and how the CI
works, please take a look at the dedicated [README](ci/README.md).

At the time of writing these lines, the two main dependencies are CMake >= 3.16
and LLVM 15.

### Build instructions

Assuming you have cloned PARCOACH and have followed the previous section,
building PARCOACH and running the tests should be as easy as using the
following commands:

```bash
cd sources
# This installs necessary python dependencies for tests
pipenv install
pipenv shell
mkdir build && cd build
cmake .. -G Ninja
ninja run-tests
```

If you are using an installation of LLVM which is not in default paths,
you can instruct CMake to use a specific version of LLVM by using the following
command:
```bash
cmake .. -G Ninja -DLLVM_DIR=/path/to/llvm
```

### Build instructions with Guix

PARCOACH has a Guix package through the [guix-hpc]() channel.
You can also setup a development environment for the current main branch by
using the manifest at the root of this repository:
```bash
guix shell --pure -m manifest.scm -- bash
# This fixes MPI usage through Guix.
export OMPI_MCA_plm_rsh_agent=`which false`
mkdir build-guix && cd build-guix
cmake .. -G Ninja
ninja run-tests
```

## Usage

Codes with errors can be found in the [Parcoach Microbenchmark Suite](https://github.com/parcoach/microbenchmarks) and in our [tests](./tests) folders.

### Static checking

PARCOACH is a set of LLVM analysis and transformation passes which can be ran
either using the standalone `parcoach` executable.
It's also possible to use the [opt](http://llvm.org/docs/CommandGuide/opt.html)
wrapper `parcoachp`, or to directly call `opt` while loading PARCOACH as a pass
plugin; both these alternative are not recommended, but should work.

The `parcoach` interface mimics `opt`'s one: it takes LLVM IR as input, either
as bytecode (the `.bc` files) or as humanly readable IR (the `.ll` files).
Unless using the instrumentation part, you don't need any output IR and using
`-disable-output` is recommended.

#### How to use Parcoach on a single file

This can be executed right away on one of our tests files.

The first step is to generated the LLVM IR for the file, for a C file it would
be done with `clang` for instance.
The command below is meant as an illustrative example, it actually requires MPI
to be installed on your system.
If you don't/can't have it, skip this step as we have LLVM IR examples ready to
use in our codebase.

```bash
clang -g -S -emit-llvm tests/MPI/basic/src/MPIexample.c -o MPIexample.ll
```

The next step is to actually run PARCOACH over the IR; for the sake of this
example we'll use one of our IR tests files:

```bash
parcoach -check-mpi -disable-output tests/MPI/lit/MPIexample.ll
```

It should give you an output with a warning looking like this:
```
PARCOACH: ../tests/MPI/basic/src/MPIexample.c: warning: MPI_Reduce line 10 possibly not called by all processes because of conditional(s) line(s)  24 (../tests/MPI/basic/src/MPIexample.c) (full-inter)
```


#### If you have multiple files

##### 1) First, compile each file from your program with clang.
```bash
clang -g -c -emit-llvm file1.c -o file1.bc
clang -g -c -emit-llvm file2.c -o file2.bc
clang -g -c -emit-llvm file3.c -o file3.bc
```

 Do not forget to supply the `-g` option, so PARCOACH can provide more precise debugging information.

##### 2) Then, link all object files
```bash
llvm-link file1.bc file2.bc file3.bc -o merge.bc
```

##### 3) Finally, run the PARCOACH pass on the generated LLVM bytecode. To detect collective errors in MPI:

```bash
./parcoach -check-mpi merge.bc
```

#### PARCOACH's wrapper and integration with build systems

The executable `parcoachcc` is shipped with PARCOACH and can be used as a wrapper
to have a single step to compile your code while running the analyses over it.

A call to the wrapper looks like this:
```bash
$ parcoachcc clang example.c -c -o example.o
remark: Parcoach: running '/usr/bin/clang example.c -c -o example.o'
remark: Parcoach: running '/usr/bin/clang example.c -g -S -emit-llvm -o parcoach-ir-782df9.ll'
remark: Parcoach: running '/usr/local/bin/parcoach parcoach-ir-782df9.ll'
...
```

As you can see, under the hood `parcoachcc` will perform three steps:
  - it will execute the original command line.
  - it will generate a temporary LLVM IR file.
  - it will run PARCOACH over that temporary IR.

This wrapper lets you easily integrate PARCOACH in popular build systems;
you can check our wiki articles about
[autotools integration](https://gitlab.inria.fr/parcoach/parcoach/-/wikis/Using-PARCOACH-in-an-autotools-project)
or [CMake integration](https://gitlab.inria.fr/parcoach/parcoach/-/wikis/Using-PARCOACH-in-a-CMake-project).

### Runtime checking

Coming soon

## Developer notes

### Contributing to PARCOACH

PARCOACH has adopted pretty much the same coding style as LLVM.
Two tools are used for this:
  - `clang-tidy`, which checks wether the naming conventions are respected, or
  if there are parts of the code which are not easily readable, and many other
  criteria. It's not mandatory for the output of `clang-tidy` to be empty,
  but it's generally a good idea to look at it.
  You can enable it by passing `PARCOACH_ENABLE_TIDY=ON` to CMake.
  - `clang-format`, which deals with pure formatting checks. It's mandatory
  for any code landing on the main branch to be correctly formatted.
  You can automatically format the code by running `ninja run-clang-format`.

## Publications

Van Man Nguyen, Emmanuelle Saillard, Julien Jaeger, Denis Barthou and Patrick Carribault,
 **[PARCOACH Extension for Static MPI Nonblocking and Persistent Communication Validation](https://ieeexplore.ieee.org/document/9296940)**
 *Fourth International Workshop on Software Correctness for HPC Applications, 2020.*

 Pierre Huchant, Emmanuelle Saillard, Denis Barthou and Patrick Carribault,
 **[Multi-Valued Expression Analysis for Collective Checking](https://link.springer.com/chapter/10.1007%2F978-3-030-29400-7_3)**
 *Euro-Par 2019: Parallel Processing, pages 29-43, 2019*

 Pierre Huchant, Emmanuelle Saillard, Denis Barthou, Hugo Brunie and Patrick Carribault,
 **[PARCOACH Extension for a Full-Interprocedural Collectives Verification](https://doi.org/10.1109/Correctness.2018.00013)**,
 *2nd International Workshop on Software Correctness for HPC Applications (Correctness), pages 69-76, 2018*

 Emmanuelle Saillard, Hugo Brunie, Patrick Carribault, and Denis Barthou,
 **[PARCOACH extension for Hybrid Applications with Interprocedural Analysis](https://doi.org/10.1007/978-3-319-39589-0_11)**,
 *In Tools for High Performance Computing 2015: Proceedings of the 9th International Workshop on Parallel Tools for High Performance Computing, pages 135-146, 2016*

 Emmanuelle Saillard, Patrick Carribault, and Denis Barthou,
 **[MPI Thread-Level Checking for MPI+OpenMP Applications](https://doi.org/10.1007/978-3-662-48096-0_3)**,
 *European Conference on Parallel Processing (EuroPar), pages 31-42, 2015*

 Julien Jaeger, Emmanuelle Saillard, Patrick Carribault, Denis Barthou,
 **[Correctness Analysis of MPI-3 Non-Blocking Communications in PARCOACH](https://dl.acm.org/doi/10.1145/2802658.2802674)**,
 *European MPI Users’Group Meeting, Sep 2015, Bordeaux, France. EuroMPI’15*

 Emmanuelle Saillard, Patrick Carribault, and Denis Barthou,
 **[PARCOACH: Combining static and dynamic validation of MPI collective communications](https://doi.org/10.1177%2F1094342014552204)**,
 *Intl. Journal on High Performance Computing Applications (IJHPCA), 28(4):425-434, 2014*

 Emmanuelle Saillard, Patrick Carribault, and Denis Barthou,
 **[Static Validation of Barriers and Worksharing Constructs in OpenMP Applications](https://doi.org/10.1007/978-3-319-11454-5_6)**,
 *Proc. IntL. Workshop on OpenMP (IWOMP), volume 8766 of Lect. Notes in Computer Science, pages 73-86, 2014*

 Emmanuelle Saillard, Patrick Carribault, and Denis Barthou,
 **[Combining Static and Dynamic Validation of MPI Collective Communications](https://doi.org/10.1145/2488551.2488555)**,
 *Proceedings of the European MPI User’s Group Meeting (EuroMPI), pages 117-122, 2013*



## License
The project is licensed under the LGPL 2.1 license.

## External links

- [Official website](https://parcoach.github.io)
- [Parcoach Microbenchmark Suite](https://github.com/parcoach/microbenchmarks)

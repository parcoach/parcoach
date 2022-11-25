# PARCOACH changelog

## 2.0

This is a major release changing a lot the internal structure of the tool,
improving the command line interface, and improving the tests and code coverage.

As far as features are concerned, there should be no change: the tests base
was improved before the internal changes, and there has been no regressions.

### General:

  - docker: slight improvement to the docker-compose wrapper. Running
  `docker compose run --rm shell` should now build the required image if it's
  not available locally.
  - cli: PARCOACH can now be used as a standalone tool using the command
  `parcoach`. It basically mimics `opt`'s behavior. Using it as a pass plugin
  is still possible using the `parcoachp` wrapper.
  - design: Probably the biggest internal change. The various analyses parts
  have been correctly isolated and registered as LLVM analyses. The default
  pass pipeline now consists in a pass showing the main analysis results, as
  well as a standalone transformation pass for instrumentation.
  - output: Some cleanups in the tool output. The goal is to get rid of the
  multiple `errs()` usage in favor of the more flexibug `LLVM_DEBUG` system.
  These outputs can still be used in debug mode by passing `-debug` or
  `-debug-only=somecomponent`.
  - misc: Many small refactor and improvements, where the goal was to get rid
  of unused code and be able to activate/deactivate features based on enabled
  paradigms.
  - coverage: With the addition of many more tests, this release has over 80%
  of its lines covered.
  - version: add a version in CMake, and introduce the `-parcoach-version`
  option to display it on the command line.

### Tests:

  - coverage: Introduced code coverage reports. Merge requests now reports their
  code coverage, and (un)covered lines are displayed in the diff view.
  Building the `coverage` target also produces an `html` folder with a detailed
  report about PARCOACH's coverage.
  - tools: Introduced a lit configuration, which makes it easy to run a lot of
  IR-based tests cases.
  - all: The tests folder has been changed to be able to be both configured
  as part of the main PARCOACH project, or as a standalone project. This allows
  convenient testing of a given PARCOACH release archive.
  - MBI: add the MBI tests suite as an external project, and run it as part of
  the test target.
  - pipenv: Introduced a `Pipfile` for the 2 python dependencies: lit
  and lcov-cobertura. It allows for a reproducible python environment and relies
  on virtual environments. Use `pipenv shell` to activate it in your shell,
  you may also activate the virtual environment manually (see our
  [ci](./.gitlab-ci.yml) file).

### Packaging:

  - cpack: Introduced a CPack configuration to produce release packages and
  installer. They can be generated using the build target `package`.
  - release: `parcoach` can now be built using shared libraries (which requires
  the user to have the appropriate LLVM dynamic library installed and
  available), or using static libraries (in which case LLVM is linked into
  `parcoach`, it removes the dependency on LLVM but implies a
  huge - 100MB - binary).

### Bug fixes:
  - MSSA: Fixed a bug when analyzing OpenMP/Cuda input that would break Mu/Chi
  computation.

## 1.3

### General changes:

  - llvm: Migration to a single version of LLVM (15).
  - llvm: Migration to the new pass manager.
  - ci: Make sure CI images are auto-generated.
  - code-quality: Introduced `clang-format`/`clang-tidy` integration and
  configuration based on LLVM coding style.

### Bug fixes:

  - bfs: Fixed a bug when analyzing OpenMP inputs.

## 1.2

This version was based on LLVM 9 to 12, and was used in several papers.
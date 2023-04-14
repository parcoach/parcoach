#!/usr/bin/env bash
set -euxo pipefail

SOURCE_DIR=$1

pushd $SOURCE_DIR

pipenv install
source `pipenv --venv`/bin/activate

popd

cmake --build build-atos --target coverage

lcov_cobertura build-atos/parcoach.lcov -o coverage.xml

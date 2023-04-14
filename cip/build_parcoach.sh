#!/usr/bin/env bash
set -euxo pipefail

SOURCE_DIR=$1

pushd $SOURCE_DIR

pipenv install
source `pipenv --venv`/bin/activate

popd

cmake $SOURCE_DIR -B build-atos -C $SOURCE_DIR/caches/Test-atos.cmake

cmake --build build-atos --target all

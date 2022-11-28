#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

MPI_VERSION=$1
MPI_SHA256=$2
MPI_URL="https://download.open-mpi.org/release/open-mpi/v4.1/openmpi-${MPI_VERSION}.tar.gz"

mkdir -p /build
pushd /build

wget "${MPI_URL}" -O mpi.tgz
echo "${MPI_SHA256} mpi.tgz" | sha256sum -c -

tar --strip-components=1 -xf mpi.tgz

./configure --prefix=/usr/local
make -j install

popd
rm -rf /build

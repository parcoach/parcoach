#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

LLVM_VERSION=$1

# This is required by some dark magic within the libomp runtime.
#dnf install -y perl-FindBin perl-File-Copy

mkdir -p /llvm/build
pushd /llvm

git clone --depth 1 https://github.com/llvm/llvm-project.git -b llvmorg-${LLVM_VERSION}

cd build

cmake ../llvm-project/llvm -G Ninja -C /scripts/llvm.cmake

ninja install

popd
rm -rf /llvm

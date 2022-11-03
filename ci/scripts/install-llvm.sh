#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

source "${SCRIPT_DIR}/apt_utils.sh"

UBUNTU_VERSION=$1
LLVM_VERSION=$2

# See: https://apt.llvm.org/
# We basically reproduce what's done in https://apt.llvm.org/llvm.sh
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
echo "deb http://apt.llvm.org/${UBUNTU_VERSION}/ llvm-toolchain-${UBUNTU_VERSION}-${LLVM_VERSION} main" | tee /etc/apt/sources.list.d/llvm.list

# FIXME: we should probably filter this a bit more.
apt_install -t "llvm-toolchain-${UBUNTU_VERSION}-${LLVM_VERSION}" \
  clang-${LLVM_VERSION} \
  lldb-${LLVM_VERSION} \
  lld-${LLVM_VERSION} \
  clangd-${LLVM_VERSION} \
  clang-tidy-${LLVM_VERSION} \
  clang-format-${LLVM_VERSION} \
  clang-tools-${LLVM_VERSION} \
  llvm-${LLVM_VERSION}-dev \
  lld-${LLVM_VERSION} \
  lldb-${LLVM_VERSION} \
  llvm-${LLVM_VERSION}-tools \
  libomp-${LLVM_VERSION}-dev \
  libc++-${LLVM_VERSION}-dev \
  libc++abi-${LLVM_VERSION}-dev \
  libclang-common-${LLVM_VERSION}-dev \
  libclang-${LLVM_VERSION}-dev \
  libclang-cpp${LLVM_VERSION}-dev \
  libunwind-${LLVM_VERSION}-dev \
  zlib1g-dev \

# Set clang/clang++ as the default compiler
update-alternatives --install /usr/bin/cc cc /usr/bin/clang-${LLVM_VERSION} 100
update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-${LLVM_VERSION} 100

cleanup

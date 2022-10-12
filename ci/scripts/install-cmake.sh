#!/usr/bin/env bash
set -euxo pipefail

CMAKE_VERSION=$1
CMAKE_SHA256=$2
CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz"

wget "${CMAKE_URL}" -O cmake.tgz
echo "${CMAKE_SHA256} cmake.tgz" | sha256sum -c -

tar -C /usr --strip-components=1 --wildcards -xzf cmake.tgz "cmake-${CMAKE_VERSION}-*-x86_64"/{bin,share,}
rm cmake.tgz

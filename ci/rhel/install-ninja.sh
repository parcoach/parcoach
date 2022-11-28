#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

NINJA_VERSION=$1
NINJA_SHA256=$2
NINJA_URL="https://github.com/ninja-build/ninja/releases/download/v${NINJA_VERSION}/ninja-linux.zip"

mkdir -p /build
pushd /build

wget "${NINJA_URL}" -O ninja.zip
echo "${NINJA_SHA256} ninja.zip" | sha256sum -c -

unzip ninja.zip
cp ninja /usr/local/bin

popd
rm -rf /build

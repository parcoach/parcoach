#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

source "${SCRIPT_DIR}/apt_utils.sh"

MPICH_VERSION=$1

# For now we install openmpi/mpich from ubuntu's repo
if [ "${MPICH_VERSION}" != "deb" ]; then
  echo "This script only supports installing from apt repository atm."
  exit 1
fi

apt_install libmpich-dev

cleanup

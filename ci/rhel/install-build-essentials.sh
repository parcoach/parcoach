#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

# NOTE: python38 because default is 3.6 for ubi8 and MBI needs > 3.7
dnf install -y \
  make \
  gcc \
  gcc-c++ \
  gcc-gfortran \
  git \
  python38 \
  rpm-build \
  vim \
  wget \
  xz \
  zip \

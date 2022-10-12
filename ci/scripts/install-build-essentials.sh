#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

source "${SCRIPT_DIR}/apt_utils.sh"

apt_install sudo \
  binutils \
  binutils-dev \
  build-essential \
  git \
  g++ \
  gcc \
  gnupg \
  ninja-build \
  openssh-client \
  patch \
  pipenv \
  python-is-python3 \
  python3 \
  python3-pip \
  python3-venv \
  vim \
  wget \

cleanup

#!/usr/bin/env bash
set -euxo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

dnf install -y \
  python38-pip \

pip3.8 install pipenv

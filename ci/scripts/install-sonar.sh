#!/usr/bin/env bash
set -euxo pipefail

SONARSCANNER_VERSION=$1
SONARSCANNER_SHA256=$2
SONARSCANNER_URL="https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${SONARSCANNER_VERSION}-linux.zip"

wget "${SONARSCANNER_URL}" -O sonar.zip
echo "${SONARSCANNER_SHA256} sonar.zip" | sha256sum -c -

mkdir -p /tmp/sonar
mkdir -p /opt/sonar
unzip sonar.zip -d /tmp/sonar
mv /tmp/sonar/sonar-scanner*/* /opt/sonar


rm -rf /tmp/sonar sonar.zip

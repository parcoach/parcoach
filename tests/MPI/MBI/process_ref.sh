#!/usr/bin/env bash
set -euxo pipefail

SOURCE_DIR=$1
DEST_DIR=$2

echo "Processing ref in $1 to put them into $2"

function process_ref() {
  FILE=$1
  BASENAME=$(basename "${FILE}")
  DEST=$2
  echo "Processing ${BASENAME}/${1}, to put it in ${DEST}/${BASENAME}"
  cat ${FILE} | grep "^PARCOACH" | sed 's/\/MBI/third_party\/MBI/g' > ${DEST}/${BASENAME}
}

export -f process_ref

find ${SOURCE_DIR} -type f -exec bash -c 'process_ref "$0" "$1"' {} $DEST_DIR \;

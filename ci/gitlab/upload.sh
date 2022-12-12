#!/usr/bin/env bash
set -euxo pipefail

FOLDER=$1
BASE_URL=$2

upload_file() {
  FILE=$1
  FILENAME=$(basename -- "$FILE")
  BASE_URL=$2
  echo "uploading $FILE (as $FILENAME) to $BASE_URL"
  curl --header "JOB-TOKEN: ${CI_JOB_TOKEN}" \
    --upload-file $FILE \
    ${BASE_URL}/$FILENAME
}
export -f upload_file

find "${FOLDER}" -type f -exec bash -c 'upload_file "$0" "$1"' {} $BASE_URL \;

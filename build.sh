#!/bin/bash
set -e
set -x

readonly PROG_NAME="daemon"
readonly EXT=".c"

# compile and install the binary
make install
RETVAL=$?
if [ "$RETVAL" -ne 0 ]; then
    echo "error compiling ${PROG_NAME}"
    exit "$RETVAL"
fi
exit "$RETVAL"

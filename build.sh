#!/bin/bash
set -e
set -x

readonly PROG_NAME="daemon"
readonly EXT=".c"

# compile the route solver to a shared obj for erlang
make build
RETVAL=$?
if [ "$RETVAL" -ne 0 ]; then
    echo "error compiling ${PROG_NAME}"
    exit "$RETVAL"
fi
exit "$RETVAL"

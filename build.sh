#!/bin/bash
set -e
set -x

readonly PROG_NAME="daemon"
readonly EXT=".c"

# compile the route solver to a shared obj for erlang
make build
status=$?
if [ $status -ne 0 ]; then
    echo "error compiling ${PROG_NAME}"
    exit $status
fi
exit $status

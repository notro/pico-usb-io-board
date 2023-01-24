#!/bin/bash

BASE_DIR="$(dirname ${BASH_SOURCE[0]})"
PICO_SDK_DIR=$BASE_DIR/pico-sdk
BUILD_DIR="${BUILD_DIR:-${BASE_DIR}/build}"

if [ ! -e "$PICO_SDK_DIR/.git" ]; then
    # don't do --recursive as it pulls in a lot of things we don't need
    git submodule update --init
    (cd "$PICO_SDK_DIR" && git submodule update --init)
fi

cmake -B $BUILD_DIR -S $BASE_DIR
make -C $BUILD_DIR -j$(nproc)

#!/bin/bash
# Build MicroPython + AMY for the Pimoroni Pico Plus 2.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/env.sh"
make -C "$HERE/micropython/ports/rp2" -j8 \
    BOARD_DIR="$HERE/boards/PIMORONI_PICO_PLUS2" \
    BUILD="$HERE/build-PIMORONI_PICO_PLUS2" \
    USER_C_MODULES="$HERE/modules/c_amy/micropython.cmake" "$@"

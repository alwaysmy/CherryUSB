#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -fno-pie -no-pie \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$root/tests/ch32_usbhs" -I"$root/core" -I"$root/common" \
    -I"$root/port/ch32/ch32hs" "$root/tests/ch32_usbhs/test_usbhs.c" \
    -o "$build/test_usbhs"
ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}" "$build/test_usbhs"

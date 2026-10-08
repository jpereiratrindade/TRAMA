#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
npm --prefix "$root/web" ci
npm --prefix "$root/web" run build
cmake -S "$root" -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" -DTRAMA_BUILD_TESTS=ON
cmake --build "$root/build" --parallel

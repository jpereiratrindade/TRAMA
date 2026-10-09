#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
node "$root/tests/web_layout_test.mjs"
ctest --test-dir "$root/build" --output-on-failure

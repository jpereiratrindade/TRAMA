#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$root/build/bin/trama-server" --source-dir "$root" --data-dir "${TRAMA_DATA_DIR:-$root/var}" --host 127.0.0.1 --port "${TRAMA_PORT:-8088}"

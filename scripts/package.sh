#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; out="$root/build/package/TRAMA-0.1.0"
mkdir -p "$out/bin" "$out/web" "$out/migrations"
cp "$root/build/bin/trama" "$root/build/bin/trama-server" "$out/bin/"
cp -r "$root/web/dist/." "$out/web/"; cp "$root"/migrations/*.sql "$out/migrations/"
cp "$root/LICENSE" "$out/LICENSE"
tar -C "$root/build/package" -czf "$root/build/TRAMA-0.1.0-linux.tar.gz" TRAMA-0.1.0
echo "$root/build/TRAMA-0.1.0-linux.tar.gz"

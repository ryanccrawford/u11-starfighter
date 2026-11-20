#!/usr/bin/env bash
# Builds an ARM64 Linux game binary from WSL for the DOOGEE U11 tablet.
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "Missing aarch64-linux-gnu-g++. Install g++-aarch64-linux-gnu in Ubuntu-24.04 WSL." >&2
    exit 1
fi

if [[ ! -f "$project_dir/tools/arm64-raylib/lib/libraylib.a" ]]; then
    echo "Missing the tablet-matched ARM64 raylib archive." >&2
    exit 1
fi

if [[ ! -f "$project_dir/tools/arm64-json/nlohmann/json.hpp" ]]; then
    echo "Missing the tablet-matched nlohmann/json headers." >&2
    exit 1
fi

if [[ ! -f "$project_dir/tools/arm64-gl/GL/gl.h" ]]; then
    echo "Missing the tablet-matched OpenGL headers." >&2
    exit 1
fi

jobs="${JOBS:-$(nproc)}"
exec make -C "$project_dir" -j"$jobs" shooter-arm64

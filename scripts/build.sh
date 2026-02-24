#!/usr/bin/env bash
# Build the C++ server (intended for Linux / Docker environment).
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "==> Building wow-server-sim (C++17, Debug)"
mkdir -p "$PROJECT_ROOT/build"
cd "$PROJECT_ROOT/build"
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j"$(nproc)"
echo "==> Build complete: $PROJECT_ROOT/build/wow-server-sim"

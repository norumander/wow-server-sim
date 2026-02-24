#!/usr/bin/env bash
# Run the full test suite: C++ (via ctest) and Python (via pytest).
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXIT_CODE=0

# ---- C++ tests ----
echo "==> Building C++ project"
mkdir -p "$PROJECT_ROOT/build"
cd "$PROJECT_ROOT/build"
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j"$(nproc)"

echo "==> Running C++ tests (ctest)"
ctest --output-on-failure || EXIT_CODE=1

# ---- Python tests ----
echo "==> Running Python tests (pytest)"
VENV_DIR="$PROJECT_ROOT/.venv"
if [ -d "$VENV_DIR" ]; then
    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"
fi

cd "$PROJECT_ROOT"
python -m pytest tests/python/ -v || EXIT_CODE=1

# ---- Summary ----
if [ $EXIT_CODE -eq 0 ]; then
    echo "==> All tests passed."
else
    echo "==> Some tests failed (exit code $EXIT_CODE)."
fi

exit $EXIT_CODE

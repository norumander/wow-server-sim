#!/usr/bin/env bash
# Create a Python virtual environment and install the wowsim package.
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV_DIR="$PROJECT_ROOT/.venv"

echo "==> Creating Python venv at $VENV_DIR"
python3 -m venv "$VENV_DIR"

echo "==> Activating venv and installing wowsim[dev]"
# shellcheck disable=SC1091
source "$VENV_DIR/bin/activate"
pip install --upgrade pip
pip install -e "$PROJECT_ROOT/tools[dev]"

echo "==> Done. Activate with: source $VENV_DIR/bin/activate"
echo "==> Verify with: wowsim --help"

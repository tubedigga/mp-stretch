#!/usr/bin/env bash
# Build MP Stretch with the mpc-plugin-builder-sdk (git submodule at ./sdk).
# Usage:
#   ./scripts/build.sh              # native + arm32 + arm64 package
#   ./scripts/build.sh --target arm64
#   ./scripts/build.sh --target native
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${SDK_ROOT:-$ROOT/sdk}"

if [[ ! -f "$SDK/tools/build.py" ]]; then
  echo "SDK not found at $SDK" >&2
  echo "Init the submodule:  git submodule update --init --recursive" >&2
  echo "Or set SDK_ROOT to a clone of https://github.com/inmusic-software/mpc-plugin-builder-sdk" >&2
  exit 1
fi

exec python3 "$SDK/tools/build.py" "$ROOT" "$@"

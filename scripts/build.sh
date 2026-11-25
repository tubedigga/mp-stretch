#!/usr/bin/env bash
# Build MP Sample Stretch with the mpc-plugin-builder-sdk (git submodule at ./sdk).
# After a successful assemble, copies LICENSE and NOTICE into the plugin folder
# so a binary install on the SD card still carries GPL-3.0 compliance files.
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

python3 "$SDK/tools/build.py" "$ROOT" "$@"

# Copy compliance files into every assembled deploy folder (if present).
shopt -s nullglob
for out in "$ROOT"/build/*" - VST - "*/; do
  [[ -d "$out" ]] || continue
  [[ -f "$out/plugin.json" ]] || continue
  cp -f "$ROOT/LICENSE" "$out/LICENSE"
  cp -f "$ROOT/NOTICE" "$out/NOTICE"
  echo "  copied LICENSE + NOTICE -> $out"
done

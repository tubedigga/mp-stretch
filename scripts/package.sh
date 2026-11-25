#!/usr/bin/env bash
# Package MP Sample Stretch for distribution with GPL-3.0 compliance files.
# Produces:
#   dist/mp-sample-stretch-<version>-mpc.zip
#   dist/Tubedigga_MP_Sample_Stretch.zip   (same contents; friendly name)
#
# Zip layout:
#   LICENSE                 (full GPL-3.0 at zip root)
#   NOTICE                  (attribution + Corresponding Source offer)
#   INSTALL.txt             (install steps + source offer)
#   README.md
#   Tubedigga - VST - MP Sample Stretch/
#     LICENSE, NOTICE, plugin.json, version.xml, VST/*.so, Plugin Skins/...
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [[ ! -f plugin.json ]]; then
  echo "ERROR: run from the plugin project root" >&2
  exit 1
fi

NAME=$(python3 -c 'import json; print(json.load(open("plugin.json"))["name"])')
MANU=$(python3 -c 'import json; print(json.load(open("plugin.json"))["manufacturer"])')
VER=$(python3 -c 'import json; print(json.load(open("plugin.json"))["version"])')
FOLDER="$MANU - VST - $NAME"
BUILT="build/$FOLDER"

if [[ ! -d "$BUILT" || ! -f "$BUILT/VST/mp-sample-stretch-arm64.so" ]]; then
  echo "Built folder missing; running ./scripts/build.sh --target all"
  ./scripts/build.sh --target all
fi

# Ensure compliance files are inside the installable plugin folder.
cp -f LICENSE "$BUILT/LICENSE"
cp -f NOTICE "$BUILT/NOTICE"

mkdir -p dist
SLUG_ZIP="dist/mp-sample-stretch-${VER}-mpc.zip"
FRIENDLY_ZIP="dist/Tubedigga_MP_Sample_Stretch.zip"
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT

cp -a "$BUILT" "$STAGE/$FOLDER"
cp -f LICENSE "$STAGE/LICENSE"
cp -f NOTICE "$STAGE/NOTICE"
cp -f README.md "$STAGE/README.md"

cat > "$STAGE/INSTALL.txt" <<EOF
$NAME $VER - plugin for Akai MPC standalone / MPC Sample
by $MANU
========================================================================

This is an EFFECT plugin (processes audio as an insert / Knob FX).

Licence: GNU General Public License version 3 (GPL-3.0). The full license
text is in LICENSE (zip root and inside the plugin folder). See NOTICE for
attribution and how to obtain the Corresponding Source.

Corresponding Source
--------------------
Complete source for this build is published at:
  https://github.com/tubedigga/mp-stretch
Branch while in review: cursor/mp-stretch-knob-fx-01d5
  https://github.com/tubedigga/mp-stretch/pull/1
After merge: use the repository default branch (main).

Rebuild:
  git submodule update --init --recursive
  ./scripts/build.sh
  ./scripts/package.sh

Attribution: Tubedigga; time-stretch DSP modified from potenza-time-stretch
(dar-io-p, GPL-3.0) https://github.com/dar-io-p/potenza-time-stretch

HOW TO INSTALL (MPC / Force)
----------------------------
1. Copy the folder

       "$FOLDER"

   into a folder named exactly   VST2 Plugins   at the root of a USB drive,
   SD card, or the MPC's internal drive. Create "VST2 Plugins" if needed.

2. On the MPC, Preferences > Enhancements (Beta): enable Enhancements and
   External Plugin Support (MPC OS 3.10.1 or later).

HOW TO INSTALL (MPC Sample)
---------------------------
Firmware v1.4.0 or later. MIDI Config > Dev Enhancements (Beta): enable
Dev Enhancements (Beta) and Custom Plug-In Support. From Project, enter
SD Card Access, copy the same plugin folder into VST2 Plugins on the
microSD card, eject on the computer, then exit SD Card Access.

TO UNINSTALL
------------
Delete the "$FOLDER" folder from "VST2 Plugins".

PLEASE READ - IMPORTANT
-----------------------
* Third-party plugins are NOT supported by inMusic / Akai.
* A faulty plugin can freeze or crash the MPC. Save often.
* More information: https://www.tubedigga.com
EOF

rm -f "$SLUG_ZIP" "$FRIENDLY_ZIP"
(
  cd "$STAGE"
  zip -r -q "$ROOT/$SLUG_ZIP" .
)
cp -f "$SLUG_ZIP" "$FRIENDLY_ZIP"

echo "Packaged:"
echo "  $ROOT/$SLUG_ZIP"
echo "  $ROOT/$FRIENDLY_ZIP"
unzip -l "$FRIENDLY_ZIP" | grep -E 'LICENSE|NOTICE|INSTALL|README|plugin.json|arm64' || true

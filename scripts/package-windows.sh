#!/bin/sh
# Portable Windows zip, matching the v1.1.2 layout.
set -eu
cd "$(dirname "$0")/.."
version=$(tr -d '[:space:]' < VERSION)
dir="GalaxyEditor-${version}-windows-x64"
test -f build-windows/GalaxyEditor.exe
test -f GE-Favorites-RandomMapGeneratorTiles.hpi
test -f GreenWorld.hpi
command -v zip >/dev/null
rm -rf "$dir" "${dir}.zip"
mkdir -p "$dir"
cp build-windows/GalaxyEditor.exe "$dir/GalaxyEditor.exe"
cp README.md "$dir/README.md"
cp GE-Favorites-RandomMapGeneratorTiles.hpi GreenWorld.hpi "$dir/"
zip -r -X "${dir}.zip" "$dir"

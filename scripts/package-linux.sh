#!/bin/sh
# Portable Linux archive, matching the v1.1.2 layout: the program, README.md,
# and the two companion tileset archives in one directory.
set -eu
cd "$(dirname "$0")/.."
version=$(tr -d '[:space:]' < VERSION)
dir="GalaxyEditor-${version}-linux-x64"
test -f build/galaxy-editor
test -f GE-Favorites-RandomMapGeneratorTiles.hpi
test -f GreenWorld.hpi
rm -rf "$dir" "${dir}.tar.gz"
mkdir -p "$dir"
cp build/galaxy-editor "$dir/galaxy-editor"
cp README.md "$dir/README.md"
cp GE-Favorites-RandomMapGeneratorTiles.hpi GreenWorld.hpi "$dir/"
tar -czf "${dir}.tar.gz" "$dir"

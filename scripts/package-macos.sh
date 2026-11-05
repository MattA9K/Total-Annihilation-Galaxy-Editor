#!/bin/sh
# Apple silicon disk image. The volume is "Install Galaxy Editor" and holds
# the Galaxy Editor folder (app plus tileset archives) and an Applications link,
# the same layout as the v1.1.2 image.
set -eu
cd "$(dirname "$0")/.."
version=$(tr -d '[:space:]' < VERSION)
test -d build/GalaxyEditor.app
test -f GE-Favorites-RandomMapGeneratorTiles.hpi
test -f GreenWorld.hpi
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
folder="$stage/Galaxy Editor"
mkdir -p "$folder"
cp -R build/GalaxyEditor.app "$folder/"
cp GE-Favorites-RandomMapGeneratorTiles.hpi GreenWorld.hpi "$folder/"
ln -s /Applications "$stage/Applications"
rm -f "GalaxyEditor-${version}-macos.dmg"
hdiutil create \
  -volname "Install Galaxy Editor" \
  -srcfolder "$stage" \
  -ov \
  -format UDZO \
  "GalaxyEditor-${version}-macos.dmg"

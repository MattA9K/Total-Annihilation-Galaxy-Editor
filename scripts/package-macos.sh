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
workdir=$(mktemp -d)
device=""
cleanup() {
  if [ -n "$device" ]; then
    hdiutil detach "$device" >/dev/null 2>&1 || hdiutil detach -force "$device" >/dev/null 2>&1 || true
  fi
  rm -rf "$stage" "$workdir"
}
trap cleanup EXIT
folder="$stage/Galaxy Editor"
mkdir -p "$folder"
cp -R build/GalaxyEditor.app "$folder/"
cp GE-Favorites-RandomMapGeneratorTiles.hpi GreenWorld.hpi "$folder/"
ln -s /Applications "$stage/Applications"
# hdiutil's automatic size leaves no spare blocks, so copying the favorites
# archive onto the image fails even when the runner disk has room.
kb=$(du -sk "$stage" | awk '{print $1}')
mb=$(( (kb + kb / 4 + 32768 + 1023) / 1024 ))
raw="$workdir/raw.dmg"
hdiutil create -size "${mb}m" -fs HFS+ -volname "Install Galaxy Editor" "$raw" >/dev/null
attach=$(hdiutil attach -readwrite -nobrowse -noverify -noautoopen "$raw")
device=$(printf '%s\n' "$attach" | awk '/^\/dev\// { print $1; exit }')
mount=$(printf '%s\n' "$attach" | awk 'index($0, "/Volumes/") { print substr($0, index($0, "/Volumes/")); exit }')
cp -R "$stage"/. "$mount"/
sync
hdiutil detach "$device" >/dev/null
device=""
rm -f "GalaxyEditor-${version}-macos.dmg"
hdiutil convert "$raw" -format UDZO -o "GalaxyEditor-${version}-macos.dmg" >/dev/null

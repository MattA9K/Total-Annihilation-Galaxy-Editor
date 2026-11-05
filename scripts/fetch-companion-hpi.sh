#!/bin/sh
# Copy the tileset archives that portable releases place beside the app.
# They are published with the GitHub releases and are not stored in git.
# Pass a tag to read them from a release other than v1.1.2.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
tag=${1:-v1.1.2}
fav=GE-Favorites-RandomMapGeneratorTiles.hpi
world=GreenWorld.hpi
if [ -f "$fav" ] && [ -f "$world" ]; then
  exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
version=${tag#v}
url="https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/${tag}/GalaxyEditor-${version}-linux-x64.tar.gz"
curl -fL --retry 3 --retry-delay 2 -o "$tmp/linux.tar.gz" "$url"
tar -xzf "$tmp/linux.tar.gz" -C "$tmp"
found_fav=$(find "$tmp" -name "$fav" -type f | head -n 1)
found_world=$(find "$tmp" -name "$world" -type f | head -n 1)
if [ -z "$found_fav" ] || [ -z "$found_world" ]; then
  echo "Companion archives were not in ${url}" >&2
  exit 1
fi
cp "$found_fav" "$root/$fav"
cp "$found_world" "$root/$world"

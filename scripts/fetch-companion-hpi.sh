#!/bin/sh
# Copy the tileset archives that portable releases place beside the app.
# They are published with the GitHub releases and are not stored in git.
# Pass a tag to read them from that release. The release may attach the two
# .hpi files directly, or include them inside its Linux archive.
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$root"
tag=${1:-v1.1.2}
fav=GE-Favorites-RandomMapGeneratorTiles.hpi
world=GreenWorld.hpi
if [ -f "$fav" ] && [ -f "$world" ]; then
  exit 0
fi
if [ -f "build/$fav" ] && [ -f "build/$world" ]; then
  cp "build/$fav" "build/$world" "$root/"
  exit 0
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
base="https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/${tag}"
if curl -fL --retry 3 --retry-delay 2 -o "$tmp/$fav" "$base/$fav" \
  && curl -fL --retry 3 --retry-delay 2 -o "$tmp/$world" "$base/$world"; then
  cp "$tmp/$fav" "$tmp/$world" "$root/"
  exit 0
fi
version=${tag#v}
url="${base}/GalaxyEditor-${version}-linux-x64.tar.gz"
curl -fL --retry 3 --retry-delay 2 -o "$tmp/linux.tar.gz" "$url"
tar -xzf "$tmp/linux.tar.gz" -C "$tmp"
found_fav=$(find "$tmp" -name "$fav" -type f | head -n 1)
found_world=$(find "$tmp" -name "$world" -type f | head -n 1)
if [ -z "$found_fav" ] || [ -z "$found_world" ]; then
  echo "Companion archives were not in ${tag}" >&2
  exit 1
fi
cp "$found_fav" "$root/$fav"
cp "$found_world" "$root/$world"

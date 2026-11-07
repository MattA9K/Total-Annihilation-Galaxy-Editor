#!/bin/sh
# Print the GitHub Release body for a version. A matching section below is the
# change list for that tag. Every tag keeps the install instructions.
set -eu
version=${1:?version}
cat <<EOF
Galaxy Editor ${version}

The macOS disk image, the Windows zip, and the Linux archive each include \`GE-Favorites-RandomMapGeneratorTiles.hpi\` beside the app. The tile brush reads that tileset, so leave it next to \`GalaxyEditor.app\`, \`GalaxyEditor.exe\`, or \`galaxy-editor\`. \`GreenWorld.hpi\` is included the same way.

On macOS, open the disk image and drag the Galaxy Editor folder to Applications. The app is built for Apple silicon. On Windows, unzip the archive and run \`GalaxyEditor.exe\`. On Linux, unpack the archive and run \`./galaxy-editor\`. The Linux build is a 64-bit program for a desktop with OpenGL and zlib, such as Ubuntu 22.04 or newer.

Debian and Ubuntu can install the package instead of the archive:

\`\`\`bash
wget https://github.com/MattA9K/Total-Annihilation-Galaxy-Editor/releases/download/v${version}/galaxy-editor_${version}_amd64.deb
sudo apt install ./galaxy-editor_${version}_amd64.deb
\`\`\`

\`apt\` installs the command \`galaxy-editor\` and pulls in zlib, OpenGL, X11, and Zenity. The \`.deb\` does not include the two \`.hpi\` files. Those stay in the Linux archive, beside the program.
EOF

if [ "$version" = "1.1.6" ]; then
  cat <<EOF

## New

**Metal.** The tile brush has a Metal tileset. Base-0 is deep water, Base-68 is shallow water, and Base-85 is land, with shores between them. Base-139 is the raised ground off Base-85. Base-219 is the cliff off Base-85, and Slope 85 to 219 turns those cliffs into walkable slopes. Base-255 sits on Base-139. Slope 139 to 255 turns the straight cliffs between them into slopes.

**Favorites.** Command-click on Mac, or Ctrl-click on Windows, selects more than one favorite tile. Right-click or the Delete key removes the selected tiles together.
EOF
fi

if [ "$version" = "1.1.5" ]; then
  cat <<EOF

## New

**Ice.** The tile brush has an Ice tileset. Base-0 is water, Base-85 is land, and Base-170 is the high ground. Shores join the water to the land. Cliffs join Base-85 to Base-170. Four straight slopes turn those cliffs into walkable slopes.

**Lava slopes.** Ravine ramps are in the favorites tileset, so a Lava cliff can become a slope.
EOF
fi

if [ "$version" = "1.1.4" ]; then
  cat <<EOF

## New

**Lava brush.** Lava liquid is Base-41 and lava ground is Base-86. Base-190 is the high plateau. The ravine cliffs transition between Base-86 and Base-190. The favorites tileset beside the app includes those tiles.

**Fixes.** The language window is only as tall as its contents. File > New Map opens again.
EOF
fi

if [ "$version" = "1.1.3" ]; then
  cat <<EOF

## New

**Languages.** The first time Galaxy Editor starts, before the splash screen, it asks which language to use. Continue saves the choice, and later launches skip that screen. Change it in Editor Options, under Language. Menus, the toolbar, settings, and the common dialogs switch immediately. A line with no translation yet stays in English. The languages are English, 简体中文, Suomi, Polski, Svenska, Hrvatski, Français, 한국어, Nederlands, Bahasa Melayu, Dansk, Беларуская, Norsk bokmål, Русский, and Slovenčina.

**Linux package.** This release adds \`galaxy-editor_${version}_amd64.deb\` for Debian and Ubuntu. The portable Linux program is named \`galaxy-editor\`.
EOF
fi

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

if [ "$version" = "1.1.8" ]; then
  cat <<EOF

## New

**Lush.** The tile brush has a Lush tileset. Base-27 is water and Base-77 is land, with shores between them. Base-183 rises off Base-77, and Slope 77 to 183 follows the straight cliffs. Base-255 is the mountain ground above Base-77. Rivers and river bridges run on the land and out to the shore. A river cuts north to south through a mountain range when that range is an even number of cells tall.

**Brush cursor.** A 32, 64, or 128 brush draws a square of that size. The pointer sits in the center of the square.
EOF
fi

if [ "$version" = "1.1.7" ]; then
  cat <<EOF

## New

**Crystal.** The tile brush has a Crystal tileset. Base-0 is water and Base-85 is land, with shores between them.

**Archipelago.** Base-0 is deep water, Base-68 is shallow water, and Base-78 is land. Shores join the deep water to the land. Shallow water sits between them, and one island can keep a deep shore on one side and a shallow shore on the other. Base-194 is the high ground above Base-78. Slope 78 to 194 turns those cliffs into walkable slopes. Archipelago land blends into GreenWorld Base-85.

**Desert.** Base-0 is deep water, Base-68 is shallow water, and Base-80 is land. Base-155 rises off Base-80, and Base-230 rises off Base-155. Slope 80 to 155 and Slope 155 to 230 follow the straight cliffs. Rivers and river bridges run on the land and out to the shore.

**Show Heights.** The grid lines are thicker, and the colors separate water, ground just above the sea, and the higher cliffs.
EOF
fi

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

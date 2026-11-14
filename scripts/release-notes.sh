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

if [ "$version" = "1.1.11" ]; then
  cat <<EOF

## New

**Lush.** Horizontal Mtn and Vertical Mtn place a mountain on Base-77 that grows as you drag along it. Island-27 16x16 places an island in the water. Hill-77 8x8, Hill-77 16x16, Swamp-77, and Pond-77 16x16 place hills, swamps, and ponds on Base-77, and Pools 8x8 has four more pools. Feature-255 places a mountain feature on Base-255.

**Desert.** Huge-Hill-80 and Plateaus-80 place hills, plateaus, and mesas of several sizes on Base-80, with the cursor showing the size of the next one and N skipping to another. Hill-80 16x16 and Roughs-80 8x8 place hills and rough patches. Dunes-80 paints dunes one 16 x 16 cell at a time, and neighbouring dunes join up.

**Acid.** Pools-85 places pools of two sizes on Base-85, and Hills-85 16x16, Mounds-85 16x16, Puddles-85 16x16, and Trails-85 16x16 place hills, mounds, puddles, and trails. Acid-0 16x16, Islands-0 16x16, and Islands-0 32x32 place acid patches and islands on Base-0.

## Fixed

**Lush Base-183.** Painting Base-183 on Base-77 no longer scatters cliff pieces across the map. Base-77 and Base-183 share the same grass picture, and the height now tells them apart.
EOF
fi

if [ "$version" = "1.1.10" ]; then
  cat <<EOF

## New

**Saving.** File > Save and File > Save As write the map in the background, so a large map no longer freezes the editor. The map is blurred with a spinner until the file is written.

**Grid.** View > Grid adds 64 x 64, 128 x 128, 256 x 256, and 512 x 512.

**Tile Brush.** The window shows three buttons in each row.

**Archipelago.** Hill-194 16x16 places a hill on Base-194. Hills-78 8x8 and Hills-78 16x16 place a hill on Base-78. Island-0 8x8, Rocky-0 8x8, Island-0 Medium, and Island-0 Large place an island on Base-0 water. Medium and Large islands come in several sizes, so the cursor shows the size of the next one, and N skips to another.

**Lava.** Pool-86 8x8, Pool-86 16x16, and Pool-86 32x32 place a lava pool on Base-86.

**Mars.** Hills-85 8x8, Pool-85 8x8, Pool-85 16x16, Rough-85 16x16, and Feature-85 place hills, pools, rough ground, and other features on Base-85. Island-0 8x8, Island-0 16x16, and Island-0 32x32 place an island on Base-0 water.

**Ice.** Hills-85 8x8, Hills-85 16x16, and Pools-85 place hills and pools on Base-85. Island-0 8x8 and Island-0 16x16 place an island on Base-0 water.

**Crystal.** Crater-85, Hills-85, Pond-85, and Rough-85 place craters, hills, ponds, and rough ground on Base-85. Island-85 and Reef-85 place islands and reefs on Base-0 water. Rough-85 mixes sizes, so the cursor shows the size of the next one, and N skips to another. Crystal water and coasts are now the dark Crystal water.
EOF
fi

if [ "$version" = "1.1.9" ]; then
  cat <<EOF

## New

**Brush sizes.** The tile brush adds 256 and 512.

**New Map.** A map larger than 63 x 63 shows a warning that Total Annihilation v3.1 may not be able to play it. The map can still be created.

**Height Offset.** The toolbar places a tile with every height cell raised or lowered by the same amount.

**Start locations.** On the Start Locations tab, 1 through 9 select that player and 0 selects Player 10.

**World blends.** GreenWorld, Lava, and Acid can meet in one tile. Archipelago blends into Lush and into Acid. Lush blends into Lava, and into Ice with the height stepping from 77 to 85. Ice blends into Mars and into GreenWorld.

**GreenWorld.** Hill-85 8x8 and Hill-85 16x16 place a random hill on Base-85. Rough-85 places a rough patch on an 8 x 8 block of Base-85. Island 8x8 places a 16 x 16 island on Base-0 water. Painting Base-85 stays on the grass.

**Lava.** Hill-86 8x8 and Hill-86 16x16 place a random hill on Base-86.

**Lush.** Pools 8x8 places a random pool on Base-77.
EOF
fi

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

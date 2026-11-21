#pragma once

#include "format/SctSection.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy {

// Makes 8 x 8 tile versions of the Tile Brush's 16 x 16 tile transition pieces: the coasts,
// shallow coasts and blends between lands. The 8 x 8 brush lays its corners every 8 tiles,
// so a coast or blend can turn twice as often.
//
// A halved piece keeps the shape of its transition, drawn at half size, so the band still runs
// between the middles of the cell's sides. Where the halved picture is plainly one land's flat
// ground, it takes that land's own full-size tiles instead, with a short dithered fade into the
// band. Its edges are then the same ground as the tiles around it, rather than a finer, paler
// copy, and a tile that is all flat ground is the very tile the base uses.

// A picture of palette indices, one byte a pixel, in rows.
struct PaletteImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    std::uint8_t at(int x, int y) const {
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }
};

// A section's tiles laid out as one picture, 32 pixels to a tile.
PaletteImage sectionImage(const SectionStamp& stamp);

// Half the width and height. Each 2 x 2 block keeps the one of its pixels nearest their
// average colour, so the grain of the ground survives rather than blurring into a flat colour.
PaletteImage halveImage(const PaletteImage& image);

// One land a piece can show: its flat ground and height, and the colour statistics that tell
// that ground apart from a transition drawn over it.
struct PieceLand {
    PaletteImage ground;
    int height = 0;
    // The average colour of the halved ground, and how far a 5 x 5 pixel patch of it strays.
    std::array<double, 3> mean = {0.0, 0.0, 0.0};
    double spread = 1.0;
};

// ground's width and height must be whole tiles.
PieceLand makePieceLand(const PaletteImage& ground, int height);

// The land each corner of a piece shows, as an index into lands, for the top left, top right,
// bottom left and bottom right. Only the lands listed in candidates are tried; -1 when none of
// them is near the corner's height. The corner's height decides first and its colour second, so
// GreenWorld and Mars ground at height 85 are told apart by colour.
std::array<int, 4> pieceCornerLands(const SectionStamp& piece, const std::vector<PieceLand>& lands, const std::vector<int>& candidates);

// How plainly each pixel of a picture shows one of lands, from 0 (the transition band) to 1
// (that land's flat ground), and which of those lands it is closest to. Both are row-major.
struct PieceGround {
    std::vector<float> plain;
    std::vector<int> land;
};
PieceGround pieceGround(const PaletteImage& image, const std::vector<PieceLand>& lands, const std::vector<int>& pieceLands);

// The 8 x 8 tile version of a 16 x 16 tile piece. pieceLands are the lands the piece joins, as
// indices into lands. Its flat ground is each land's own texture from the texture's top-left
// corner, as if the piece sat at the corner of the land's section; anchorPieceGround moves it to
// where the piece is placed. seed picks the dither of the fade.
SectionStamp halvePiece(const SectionStamp& piece, const std::vector<PieceLand>& lands, const std::vector<int>& pieceLands, std::uint32_t seed);

// The picture signature the Tile Brush uses for a 32 x 32 tile: FNV-1a over its 1024 bytes.
inline std::uint64_t brushTileHash(const std::uint8_t* bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 1024; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// The random map tileset lists the band tiles of its 16 x 16 and 8 x 8 transition pieces: every
// tile of a piece that is not also a tile of a flat base. A cell holding one of them holds that
// size of piece. The Tile Brush keeps one coast or blend band all one size, so an 8 x 8 stroke
// redraws the 16 x 16 pieces its band runs into, and the lists say which those are.
inline constexpr const char* kCoarseBandTilesPath = "brushtiles/band-16x16.hashes";
inline constexpr const char* kFineBandTilesPath = "brushtiles/band-8x8.hashes";
// The tiles of the pieces that only come in 16 x 16, such as cliffs, coastal cliffs, slopes, ridges and
// rivers, other than flat ground. An 8 x 8 stroke cannot redraw part of one without cutting its band.
inline constexpr const char* kFixedTilesPath = "brushtiles/fixed-16x16.hashes";

// Sorted, without repeats, as 8 little-endian bytes each.
std::vector<std::uint8_t> encodeTileHashes(std::vector<std::uint64_t> hashes);
std::vector<std::uint64_t> decodeTileHashes(const std::vector<std::uint8_t>& bytes);

// A halved piece placed with its top-left tile at tileX, tileY: its flat ground moved so each land's
// texture lines up with that land's own ground on the map, which is laid on the map's grid as its
// section is, the section's top-left corner on every multiple of its size. grounds are the lands the
// piece joins. A pixel is flat ground when it and the pixels around it are that land's texture where
// halvePiece put it; the band, and the dither where the ground fades into it, are left as they are.
SectionStamp anchorPieceGround(const SectionStamp& piece, const std::vector<const PaletteImage*>& grounds, int tileX, int tileY);

// The tilesets a blend piece joins, from its name, as the folder names under sections/BaseTiles:
// Blend-GreenMars-B-01 joins GreenWorld and Mars. A two-way name with one land, such as
// Blend-Lush-BL-02, joins that land and Lava, except Blend-Archi, which joins Archipelago and
// GreenWorld. A three-way name joins every land it names, and Lava when it names only two, as in
// Blend-Tri-Acid-T-Ice-R-01. Empty when the name is not a blend.
std::vector<std::string> blendPieceWorlds(std::string_view name);

}  // namespace galaxy

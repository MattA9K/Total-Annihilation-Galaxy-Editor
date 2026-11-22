#pragma once

#include "format/SctSection.hpp"

#include <array>
#include <cstdint>
#include <optional>
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
// Closeness is a pixel's distance from a land's mean colour over the spread of the land's texture.
// With byColour, the land nearest in colour wins instead whenever the pixel reads as its ground too:
// a land with a busy texture, like Acid, otherwise wins plain ground of a nearby colour, like Quagmire's
// mud. It is off for the older lands so their pieces stay as they were drawn.
struct PieceGround {
    std::vector<float> plain;
    std::vector<int> land;
};
PieceGround pieceGround(const PaletteImage& image, const std::vector<PieceLand>& lands, const std::vector<int>& pieceLands, bool byColour = false);

// The 8 x 8 tile version of a 16 x 16 tile piece. pieceLands are the lands the piece joins, as
// indices into lands. Its flat ground is each land's own texture from the texture's top-left
// corner, as if the piece sat at the corner of the land's section; anchorPieceGround moves it to
// where the piece is placed. seed picks the dither of the fade.
SectionStamp halvePiece(const SectionStamp& piece, const std::vector<PieceLand>& lands, const std::vector<int>& pieceLands, std::uint32_t seed, bool byColour = false);

// Which corners of a piece are land: the top left, top right, bottom left and bottom right.
using LandCorners = std::array<bool, 4>;

// The ground at each corner of a coast piece, from its name, for the top left, top right, bottom left and bottom
// right: 0 for land, 1 for shallows and 2 for water. Coast-Land-BL is land at the bottom left and water elsewhere,
// and Coast-Water-T water along the top; Coast-Lava pieces name their lava as water. ShallowCoast-Land and -Water
// pieces join land and shallows, ShallowDeep-Shallow and -Deep pieces shallows and water, and
// DSL-Land-B-Shallow-TL_and_BR-Water-TR names all three. Empty when the name is none of these.
std::optional<std::array<int, 4>> coastPieceLevels(std::string_view name);

// The land corners a Coast piece's name gives, as coastPieceLevels reads it. Empty for any other name.
std::optional<LandCorners> coastPieceCorners(std::string_view name);

// Makes 8 x 8 tile coast pieces that keep their 16 x 16 piece's texture at full size rather than halving it,
// so the rock cuts and shingle of a coast look as they do in the 16 x 16 pieces. Each ground's side has the
// halved piece's shape, and a band starts where its higher ground ends, as in a halved piece, but is cut down
// across its middle to about two thirds of its full width rather than shrunk. The picture is made of full-size
// patches of the 16 x 16 piece, each chosen to match the band where it lies and joined to the patches beside it
// along the seam where the two pictures differ least.
//
// Every side a coast crosses ends in a shared strip, one for each pair of grounds and way the coast can cross a
// side, so any two pieces that meet join cleanly, and near the side the piece's coast bends to meet the strip's.
// The strips come from each pair's four straight pieces, each made to join itself end to end.
class DetailedCoastMaker {
public:
    // A pair of grounds a coast joins, as indices into the maker's grounds with the higher first, and the coast's
    // four 16 x 16 straight pieces: those with the lower ground along the bottom, top, right and left.
    struct Join {
        int higher = 0;
        int lower = 1;
        std::array<SectionStamp, 4> straights;
    };

    // grounds are the coast's flat bases, highest first, such as land, shallows and water.
    DetailedCoastMaker(std::vector<PaletteImage> grounds, const std::vector<Join>& joins);
    // A coast between land and water alone, whose straight pieces are Coast-Water-B, -T, -R and -L.
    DetailedCoastMaker(PaletteImage land, PaletteImage water, const std::array<SectionStamp, 4>& straights);

    // Whether make can draw a piece with these grounds at its corners, as indices into grounds: it knows them all,
    // and a join between the grounds at the ends of each side the coast crosses.
    bool canMake(const std::array<int, 4>& corners) const;

    // The 8 x 8 tile version of a 16 x 16 tile coast piece with these grounds at its corners. Its flat ground is
    // each base's texture from the texture's top-left corner, as in halvePiece, and its heights are halved.
    SectionStamp make(const SectionStamp& piece, const std::array<int, 4>& corners) const;
    // The same for a coast between land, ground 0, and water, ground 1.
    SectionStamp make(const SectionStamp& piece, LandCorners corners) const;

private:
    // A side strip: the whole 8 x 8 straight piece it comes from, what each pixel is (a ground, or 255 in the
    // band), and the piece's distance from its higher ground's side and band width, for bending a coast to it.
    struct Strip {
        PaletteImage image;
        std::vector<std::uint8_t> kinds;
        std::vector<float> fromHigher;
        std::vector<float> width;
    };

    int joinOf(int higher, int lower) const;

    std::vector<PaletteImage> grounds_;
    std::vector<std::vector<std::uint64_t>> windows_;
    std::vector<int> joinHigher_;
    std::vector<int> joinLower_;
    std::vector<std::array<Strip, 4>> strips_;
};

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

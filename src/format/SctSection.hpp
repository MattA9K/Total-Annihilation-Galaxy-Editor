#pragma once

#include "format/MapFile.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace galaxy {

inline constexpr int kSectionMinimap = 128;

struct SectionStamp {
    int version = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tileCount = 0;
    bool heightsOnly = false;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint8_t> tileImages;
    std::vector<std::uint8_t> heights;
    std::vector<std::uint8_t> minimap;
    // Empty means every tile in the rectangle is included. Otherwise one byte
    // per tile, in row-major order, with 0 left untouched on paste.
    std::vector<std::uint8_t> tileMask;
};

// Tiles inside a bounding box. mask is row-major and uses 1 for a selected tile.
struct TileSelection {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> mask;

    bool empty() const { return width <= 0 || height <= 0 || mask.empty(); }

    bool contains(int tileX, int tileY) const {
        if (tileX < x || tileY < y || tileX >= x + width || tileY >= y + height || mask.empty()) {
            return false;
        }
        return mask[static_cast<std::size_t>(tileY - y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(tileX - x)] != 0;
    }

    int count() const {
        int total = 0;
        for (const std::uint8_t bit : mask) {
            total += bit != 0 ? 1 : 0;
        }
        return total;
    }

    bool solid() const { return !empty() && count() == width * height; }
};

bool sameTileSelection(const TileSelection& left, const TileSelection& right);

// Inclusive tile corners. The corners may be in either order.
TileSelection tileSelectionRect(int x0, int y0, int x1, int y1);
TileSelection tileSelectionAdd(const TileSelection& base, int x0, int y0, int x1, int y1);
TileSelection tileSelectionSubtract(const TileSelection& base, int x0, int y0, int x1, int y1);

struct TileDeduper {
    bool ready = false;
    int tileCount = 0;
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> buckets;
};

SectionStamp loadSectionStamp(const std::vector<std::uint8_t>& bytes);

// The 128 x 128 picture a section file carries for the sidebar. The section is
// scaled to fit, keeps its proportions, and sits in the top-left corner.
// Pixels outside it are palette index 0.
std::vector<std::uint8_t> buildSectionMinimap(const SectionStamp& section);

// Encodes a section as a version 3 .sct file that loadSectionStamp reads back.
// The stamp needs its tile pictures, so a heights-only copy is refused. A tile
// mask is not stored: every tile in the rectangle is written. The minimap is
// made from the tiles when the stamp has none.
std::vector<std::uint8_t> writeSectionStamp(const SectionStamp& section);
std::vector<std::uint8_t> renderMinimapRgba(const SectionStamp& section);
std::vector<std::uint8_t> renderSectionRgba(const SectionStamp& section, int maxEdge, int& width, int& height);

// Copies the section's tiles and heights onto the map. The origin is the
// top-left tile. Cells that fall outside the map are skipped. opacityPercent
// mixes the incoming picture and height into the tile already on the map;
// 100 replaces it. Returns how many map tiles were written.
int stampSection(LoadedMap& map, const SectionStamp& section, int originTileX, int originTileY, TileDeduper* deduper = nullptr, int opacityPercent = 100);

// The tiles and heights inside one rectangle, plus the map's tile-pool size
// at the time they were copied.
struct TilePatch {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int tileCount = 0;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint8_t> heights;
};

// One stamp. addedTiles holds the images appended to the pool, if any.
struct PlacementChange {
    TilePatch before;
    TilePatch after;
    std::vector<std::uint8_t> addedTiles;
};

// The rectangle is the part of a stamp that lands on the map. An empty result
// means the stamp misses the map entirely.
std::optional<TilePatch> captureTilePatch(const LoadedMap& map, int originTileX, int originTileY, int tileWidth, int tileHeight);

// Stamps the section and returns the edit needed to undo it. An empty result
// means nothing was written.
std::optional<PlacementChange> stampSectionChange(LoadedMap& map, const SectionStamp& section, int originTileX, int originTileY, TileDeduper* deduper = nullptr, int opacityPercent = 100);

void revertPlacement(LoadedMap& map, const PlacementChange& change);
void reapplyPlacement(LoadedMap& map, const PlacementChange& change);

// Copies a map rectangle into a stamp that can be pasted elsewhere. The
// rectangle is clipped to the map. heightsOnly keeps the height grid and
// leaves the tile pictures out.
SectionStamp copyTileRegion(const LoadedMap& map, int originTileX, int originTileY, int tileWidth, int tileHeight, bool heightsOnly = false, const TileSelection* included = nullptr);

// Copies the selected tiles. Holes stay out of the paste.
SectionStamp copyTileSelection(const LoadedMap& map, const TileSelection& selection, bool heightsOnly = false);

// Replaces a map rectangle with the blank tile and height 0. The returned edit
// restores the previous tiles and heights.
PlacementChange clearTileRegion(LoadedMap& map, int originTileX, int originTileY, int tileWidth, int tileHeight);

// Clears only the selected tiles. Tiles in the holes stay as they are.
PlacementChange clearTileSelection(LoadedMap& map, const TileSelection& selection);

enum class HeightAdjust { Add, Subtract, Set };

// Changes every height sample on the selected tiles. Holes and tiles outside
// the selection stay put. Results are clamped to 0–255. An empty result means
// every sample was already at the resulting value.
std::optional<PlacementChange> adjustSelectionHeight(LoadedMap& map, const TileSelection& selection, HeightAdjust adjust, int amount);

// Fades tile textures across edges shared by selected tiles. The fade reaches
// 16 pixels into each tile. Tiles outside the selection, holes, and heights
// stay as they are. An empty result means no pixel changed.
std::optional<PlacementChange> blendSelectionTextures(LoadedMap& map, const TileSelection& selection, TileDeduper* deduper = nullptr);

}  // namespace galaxy

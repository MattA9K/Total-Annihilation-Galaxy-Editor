#pragma once

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace galaxy {

// Auto Metal covers Metal ground with MegaMetal4, an invisible 255-metal zone whose
// footprint is 16 x 16 feature cells: 8 x 8 map tiles, so a 16 x 16 tile takes four.
inline constexpr const char* kAutoMetalFeature = "MegaMetal4";
inline constexpr int kAutoMetalBlockTiles = 8;

// The feature cell at the top-left of every 8 x 8 tile block, on a map tileWidth x
// tileHeight tiles, that holds at least one tile isMetal says yes to. Blocks follow
// the 8-tile grid from the map's top-left corner, and a block cut short by the map's
// edge still counts. Feature cells are 16 pixels, two to a tile.
inline std::vector<std::pair<int, int>> autoMetalCells(int tileWidth, int tileHeight, const std::function<bool(int tileX, int tileY)>& isMetal) {
    std::vector<std::pair<int, int>> cells;
    for (int by = 0; by * kAutoMetalBlockTiles < tileHeight; ++by) {
        for (int bx = 0; bx * kAutoMetalBlockTiles < tileWidth; ++bx) {
            bool metal = false;
            for (int y = by * kAutoMetalBlockTiles; y < (by + 1) * kAutoMetalBlockTiles && y < tileHeight && !metal; ++y) {
                for (int x = bx * kAutoMetalBlockTiles; x < (bx + 1) * kAutoMetalBlockTiles && x < tileWidth && !metal; ++x) {
                    metal = isMetal(x, y);
                }
            }
            if (metal) {
                cells.emplace_back(bx * kAutoMetalBlockTiles * 2, by * kAutoMetalBlockTiles * 2);
            }
        }
    }
    return cells;
}

// The game draws a feature half its ground height higher on screen than its cells,
// and a metal extractor goes where the metal shows. So a zone for the block at
// feature cell x, y moves down by its lift, in whole cells, to sit on the block's
// tiles on screen. lift gives the lift in pixels at a cell, and the zone stays on
// a map cellsHigh feature cells tall.
inline int autoMetalDrop(int x, int y, int cellsHigh, const std::function<int(int cellX, int cellY)>& lift) {
    int drop = 0;
    // The lift is read where the zone ends up, which a few rounds settle.
    for (int round = 0; round < 4; ++round) {
        const int next = std::clamp((lift(x, y + drop) + 8) / 16, 0, std::max(0, cellsHigh - 1 - y));
        if (next == drop) {
            break;
        }
        drop = next;
    }
    return drop;
}

}  // namespace galaxy

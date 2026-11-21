#pragma once

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

}  // namespace galaxy

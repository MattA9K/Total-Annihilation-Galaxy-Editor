#pragma once

#include "core/TilesetLibrary.hpp"
#include "format/MapFile.hpp"

#include <string>
#include <string_view>

namespace galaxy {

// Acid and Acid_New_1 share the key "acid". The suffix is "_New", optionally
// followed by "_" or a digit, so unrelated names are left alone.
std::string tilesetFamilyKey(std::string_view world);

struct MapGeneratorSettings {
    std::string world;
    int rivers = 35;
    int canyons = 40;
    int mountains = 20;
    int hills = 35;
    int shoreline = 25;
    int pools = 15;
    unsigned seed = 1;
};

// Replaces the map's tiles and heights using sections from the chosen world.
// Map size, sea level, start positions, and the OTA text stay. Feature cells
// are cleared. Throws if that world has no flat ground section.
void generateTerrain(LoadedMap& map, const TilesetLibrary& library, const MapGeneratorSettings& settings);

}  // namespace galaxy

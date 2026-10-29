#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace galaxy {

struct FeaturePlacement {
    int x = 0;
    int y = 0;
    std::string name;
};

struct StartPosition {
    int player = 0;
    int x = 0;
    int y = 0;
};

// Values Annihilator edits, plus the other OTA fields the game actually reads.
// Sea level is stored in the TNT terrain header. Everything else is in the OTA.
struct MapSettings {
    std::string name = "Untitled Map";
    std::string description;
    std::string planet;
    std::string players = "2";
    std::string memory = "16 mb";
    std::string aiProfile = "DEFAULT";
    int minWind = 0;
    int maxWind = 3000;
    int tidalStrength = 20;
    int solarStrength = 20;
    int gravity = 112;
    int seaLevel = 75;
    int surfaceMetal = 3;
    int mohoMetal = 30;
    bool lavaWorld = false;
    bool waterDoesDamage = false;
    int waterDamage = 0;
    bool waterBlocksShots = false;
    int humanMetal = 1000;
    int humanEnergy = 1000;
    int computerMetal = 1000;
    int computerEnergy = 1000;
    std::string meteorWeapon;
    int meteorRadius = 0;
    double meteorDensity = 0;
    int meteorDuration = 0;
    int meteorInterval = 0;
    bool lineOfSight = false;
    bool mapping = false;
    double killMultiplier = 50;
    double timeMultiplier = 0;
    int maxUnits = 0;
    std::string useOnlyUnits;
    std::string missionHint;
    std::string brief;
    std::string narration;
    std::string glamour;
};

struct LoadedMap {
    std::filesystem::path source;
    std::string displayName;
    std::string missionName;
    int tileWidth = 0;
    int tileHeight = 0;
    int seaLevel = 0;
    int tileCount = 0;
    std::vector<std::uint32_t> indices;
    std::vector<std::uint8_t> tileImages;
    std::vector<std::uint8_t> heights;
    std::vector<std::uint16_t> featureIndices;
    std::vector<std::uint8_t> featurePadding;
    std::vector<std::string> featureNames;
    std::vector<FeaturePlacement> features;
    std::vector<StartPosition> starts;
    std::vector<std::uint8_t> otaText;

    int pixelWidth() const { return tileWidth * 32; }
    int pixelHeight() const { return tileHeight * 32; }
    int heightWidth() const { return tileWidth * 2; }
    int heightHeight() const { return tileHeight * 2; }
};

inline constexpr int kMinMapTiles = 1;
inline constexpr int kMaxMapTiles = 2048;

// Vanilla Total Annihilation 3.1 stores a 16-bit tile index, so a map holds
// 65536 images. A larger pool is saved as TNT version 0x2001 with a 32-bit
// index per cell.
inline constexpr int kVanillaTileSlots = 65536;
inline constexpr std::uint32_t kTntVersion = 0x2000;
inline constexpr std::uint32_t kTntWideVersion = 0x2001;

LoadedMap loadMapFile(const std::filesystem::path& path);
LoadedMap createBlankMap(int tileWidth, int tileHeight);
void saveMapFile(const LoadedMap& map, const std::filesystem::path& path);

// Grows or shrinks the map by whole tiles on each edge. A positive count adds
// that many tiles; a negative count removes them. New tiles and heights
// continue the adjacent edge, and the new cells have no features. Features and
// start positions move with the ground. A start that would fall off the map is
// removed. Returns false, and leaves the map as it was, when the result would
// be smaller than 1 tile or larger than 2048 on either side.
bool resizeMap(LoadedMap& map, int left, int right, int top, int bottom);

// A map with no OTA yet uses the same defaults a new map is saved with.
// Applying settings rewrites those keys and leaves starts, units, and unknown keys in place.
MapSettings readMapSettings(const LoadedMap& map);
void applyMapSettings(LoadedMap& map, const MapSettings& settings);

// MapName-MM-DD-YYYY-HH-MM-Backup.ufo. The clock is the local time of the save.
std::string backupFilename(std::string_view mapName, int month, int day, int year, int hour, int minute);
bool isBackupFilename(std::string_view mapName, std::string_view filename, int& month, int& day, int& year, int& hour, int& minute);

// Names to delete so one new backup leaves at most maxBackups files. Oldest first.
std::vector<std::string> backupNamesToDelete(const std::vector<std::string>& filenames, std::string_view mapName, int maxBackups);
std::vector<std::uint8_t> renderMapRgba(const LoadedMap& map);

// Fills an RGBA buffer for the on-screen view. The buffer is top-down and covers
// the whole canvas. Map pixel (0, 0) is drawn at canvas position (panX, panY).
// Canvas pixels off the map are transparent.
void renderMapView(
    const LoadedMap& map,
    std::uint8_t* rgba,
    int textureWidth,
    int textureHeight,
    float canvasWidth,
    float canvasHeight,
    float panX,
    float panY,
    float zoom);

// Height byte of the 16px cell that contains this map pixel. -1 is outside the map.
int sampleHeight(const LoadedMap& map, int mapX, int mapY);

// One feature occupies one 16px height cell. The name is reused when it is already
// in the map's name table. Open space (void) cells are left alone.
enum class FeatureEdit {
    Placed,
    Removed,
    Unchanged,
    OpenSpace,
    Full,
};

// One cell edit. addedName is set only when this edit appended a name to the table.
struct FeatureChange {
    int cellX = 0;
    int cellY = 0;
    std::uint16_t before = 0xFFFF;
    std::uint16_t after = 0xFFFF;
    std::string addedName;
};

FeatureEdit placeMapFeature(LoadedMap& map, int cellX, int cellY, const std::string& name, FeatureChange* change = nullptr);
FeatureEdit removeMapFeature(LoadedMap& map, int cellX, int cellY, FeatureChange* change = nullptr);
void revertFeatureChange(LoadedMap& map, const FeatureChange& change);
void reapplyFeatureChange(LoadedMap& map, const FeatureChange& change);

// Transparent overlay for the same view as renderMapView. Grid lines and a light
// fill are colored from the height field. gridSize is in map pixels.
void renderHeightGrid(
    const LoadedMap& map,
    std::uint8_t* rgba,
    int textureWidth,
    int textureHeight,
    float canvasWidth,
    float canvasHeight,
    float panX,
    float panY,
    float zoom,
    int gridSize);

}  // namespace galaxy

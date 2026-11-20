#pragma once

#include "core/BrushStrokes.hpp"
#include "core/RmsScript.hpp"
#include "core/TilesetLibrary.hpp"

#include <array>
#include <functional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy {

// Random map scripts paint on the Tile Brush lattice: one node for every corner
// of the brush's 16 x 16 tile cells. A map W x H tiles across has
// ceil(W / 16) + 1 by ceil(H / 16) + 1 nodes, so one tile in a script is one
// square of the map's game size. Script coordinates run left to right and top to
// bottom: left_border is the left edge, and land_position 0 0 is the top-left corner.

// The ground a script paints. Terrains are numbers the caller hands out.
class RmsCanvas {
public:
    virtual ~RmsCanvas() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual int at(int x, int y) const = 0;
    virtual void fill(int terrain) = 0;
    // Paints one node. A brush canvas may also move the nodes around it so every
    // pair of neighbouring paints has a piece that joins them.
    virtual void paint(int x, int y, int terrain) = 0;
};

// Sets each node and nothing else.
class PlainCanvas : public RmsCanvas {
public:
    PlainCanvas(int width, int height, int terrain = 0);
    int width() const override { return width_; }
    int height() const override { return height_; }
    int at(int x, int y) const override;
    void fill(int terrain) override;
    void paint(int x, int y, int terrain) override;
    const std::vector<int>& nodes() const { return nodes_; }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<int> nodes_;
};

// Paints the way the Tile Brush does with its 8 x 8 corner brush. Terrains are
// the graph's levels, lowest height first.
class BrushCanvas : public RmsCanvas {
public:
    BrushCanvas(int width, int height, BrushGraph graph);
    int width() const override { return width_; }
    int height() const override { return height_; }
    int at(int x, int y) const override;
    void fill(int terrain) override;
    void paint(int x, int y, int terrain) override;
    const BrushGraph& graph() const { return graph_; }
    const std::vector<int>& nodes() const { return nodes_; }
    // The levels at a cell's corners: top left, top right, bottom left, bottom right.
    std::array<int, 4> cellCorners(int cellX, int cellY) const;
    // Cells whose corners have no picture take the paint most of their corners
    // have. Returns how many cells still have no picture.
    int repair(const std::function<bool(const std::array<int, 4>&)>& hasPicture, int passes = 8);

private:
    int width_ = 0;
    int height_ = 0;
    BrushGraph graph_;
    std::vector<int> nodes_;
};

// How the generator turns names such as GREENWORLD_BASE-85 into terrains.
struct RmsTerrainNames {
    // The terrain for a name, or -1 for a name the tileset does not have.
    std::function<int(std::string_view)> find;
    // What the map is without base_terrain, and a land without terrain_type.
    // The game uses GRASS; the editor uses GREENWORLD_BASE-85.
    int fallback = 0;
};

// A feature the game can place, such as Tree1, from the loaded archives' TDF files.
struct RmsFeature {
    std::string name;
    std::string world;     // the TDF's world, such as greenworld or Zhon
    std::string category;  // such as trees or rocks
    int footprintX = 1;
    int footprintZ = 1;
    int metal = 0;
    bool geothermal = false;
};

// Kinds of doodad a script can ask for by name. Each picks the features that
// suit the land under each spot: TREES are GreenWorld trees on GreenWorld grass
// and palms on Archipelago sand.
enum class DoodadKind {
    Trees,
    Rocks,
    Plants,
    Shrubs,
    Foliage,
    Fronds,
    Vines,
    IceChunks,
    LavaNodes,
    LavaSpires,
    MetalTowers,
    Pipes,
    CrystalSpires,
    Spires,
    // Resources: metal deposits a player mines, and vents a geothermal plant is built on.
    MetalNodes,
    GeoVents,
};

struct DoodadKindInfo {
    DoodadKind kind;
    const char* name;  // the script name, such as TREES
};
const std::vector<DoodadKindInfo>& doodadKinds();
// True when a feature is a doodad of this kind. Scars, smudges, corpses, wreckage
// and burnt or dead copies are never part of a kind; they can still be placed by name.
bool featureIsDoodad(const RmsFeature& feature, DoodadKind kind);
// The tileset a feature belongs to, as the brush names lands: "Green", "Lava",
// "Mars", "Ice", "Metal", "Desert", "Archi", "Lush", "Acid" or "Crystal". Empty for
// worlds the Tile Brush has no ground for, such as Zhon.
std::string doodadLand(const RmsFeature& feature, DoodadKind kind);

// What the generator needs to place features. Positions are in feature cells,
// 16 x 16 pixels, the footprint of one tree; a game-size square is 32 of them.
struct RmsObjectSettings {
    std::vector<RmsFeature> features;
    // For each terrain, its land ("Green") and whether it is water, lava or another
    // liquid that features stay off.
    std::vector<std::string> terrainLands;
    std::vector<bool> terrainLiquid;
    // The map in feature cells.
    int cellsWide = 0;
    int cellsHigh = 0;
    // Feature cells from one canvas node to the next.
    int cellsPerNode = 32;
};

struct RmsPlacedFeature {
    int x = 0;
    int y = 0;
    std::string name;
};

// A player's start position, at the middle of a feature cell.
struct RmsStart {
    int player = 1;
    int x = 0;
    int y = 0;
};

struct RandomMapOptions {
    int players = 2;
    // Null when features are not placed; a script with <OBJECTS_GENERATION> then gets a note.
    const RmsObjectSettings* objects = nullptr;
};

struct RandomMapOutcome {
    std::vector<RmsMessage> messages;
    // Where each player's land started, in nodes, for player 1 first.
    std::vector<std::pair<int, int>> playerOrigins;
    int lands = 0;
    int terrains = 0;
    int connections = 0;
    // Features to place, in feature cells, when the script has <OBJECTS_GENERATION>.
    bool placesObjects = false;
    std::vector<RmsPlacedFeature> features;
    // Start positions, when the script makes player lands or asks for START_POSITION.
    // They replace the map's starts.
    bool placesStarts = false;
    std::vector<RmsStart> starts;
};

// Runs the land, terrain, connection and object sections of a script. Elevation
// and cliffs are not generated yet; a script that has them gets a note.
RandomMapOutcome runRandomMapScript(const RmsScript& script, RmsCanvas& canvas, const RmsTerrainNames& names,
                                    const RandomMapOptions& options, std::mt19937& rng);

// Every name the script uses where it expects a terrain, in order of first use.
// fallback is included when the script relies on the default terrain.
std::vector<std::string> rmsTerrainNames(const RmsScript& script, const std::string& fallback);

// Conditions a script can test with if: N_PLAYER_GAME, the map size names
// (TINY_MAP and MAPSIZE_TINY up to LUDIKRIS_MAP), RANDOM_MAP, DE_AVAILABLE and
// GALAXY_EDITOR. gameWidth and gameHeight are the map's game size.
std::vector<std::string> rmsConditions(int players, int gameWidth, int gameHeight);

// One base of the random map tileset, as a script names it.
struct RandomMapPaint {
    std::string world;   // the tileset folder, such as GreenWorld
    std::string name;    // such as Base-85
    int height = 0;
    std::string terrain; // such as GREENWORLD_BASE-85
    std::string sectionPath;
};

struct RandomMapSection {
    std::string name;
    std::string category;
    std::string sectionPath;
};

// What the random map tileset (GE-Favorites-RandomMapGeneratorTiles.hpi) holds
// for the generator: its bases, brush strokes and transition pieces.
class RandomMapTiles {
public:
    RandomMapTiles() = default;
    explicit RandomMapTiles(const std::vector<TilesetSection>& sections);

    const std::vector<RandomMapPaint>& paints() const { return paints_; }
    // The paint a terrain name stands for, ignoring case, or -1.
    int findPaint(std::string_view terrain) const;
    // Every paint of the worlds these paints belong to, the way the Tile Brush
    // keeps its levels: lowest height first.
    std::vector<int> levelsFor(const std::vector<int>& paints) const;
    BrushGraph graphFor(const std::vector<int>& levels) const;
    // The word a world's coast pieces use for the sea, such as Lava in Coast-Lava-T.
    std::string liquidOf(std::string_view world) const;
    // Sections that cover a block tiles x tiles of one paint. A 16 x 16 block falls
    // back to the base section itself, which may need repeating to fill the cell.
    std::vector<const RandomMapSection*> strokes(const RandomMapPaint& paint, int tiles) const;
    // Sections for a cell whose corners sit on these levels. Empty when the
    // tileset has no piece for that layout.
    std::vector<const RandomMapSection*> pieces(const BrushGraph& graph, const std::vector<int>& levels,
                                                const std::array<int, 4>& corners) const;
    std::string pieceName(const BrushGraph& graph, const std::vector<int>& levels, const std::array<int, 4>& corners) const;

private:
    std::vector<RandomMapPaint> paints_;
    std::vector<RandomMapSection> strokes_;
    std::vector<RandomMapSection> pieces_;
    std::vector<std::pair<std::string, std::string>> liquids_;
};

struct RandomMapStamp {
    std::string sectionPath;
    int tileX = 0;
    int tileY = 0;
    // A section smaller than this repeats until it covers fill x fill tiles. 0 stamps it once.
    int fill = 0;
};

struct RandomMapStampPlan {
    std::vector<RandomMapStamp> stamps;
    // Cells that had no piece and were filled flat with their most common paint.
    int missingCells = 0;
    std::vector<std::string> missing;
};

// The sections that draw the canvas, cell by cell. Large flat areas use the
// largest brush stroke that fits.
RandomMapStampPlan planRandomMapStamps(const RandomMapTiles& tiles, const std::vector<int>& levels,
                                       const BrushCanvas& canvas, std::mt19937& rng);

}  // namespace galaxy

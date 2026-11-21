#pragma once

#include "core/BrushStrokes.hpp"
#include "core/RmsScript.hpp"
#include "core/TilesetLibrary.hpp"

#include <array>
#include <atomic>
#include <exception>
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
    // True for a Tile Brush stamp such as GREENWORLD_HILL-85_8X8. create_terrain with
    // one of these is set aside, and planStampBrushes places it once the map is painted.
    std::function<bool(std::string_view)> isStamp;
    // The high ground a cliff raises from this terrain, in the same world, such as
    // GREENWORLD_BASE-219 from GREENWORLD_BASE-85. -1 when there is none. Without it
    // <CLIFF_GENERATION> is skipped.
    std::function<int(int)> cliffAbove;
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
    Palms,
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
// False when a feature was drawn for only some Base tiles of its land and base, such
// as "Base-1", is not one of them: ArchMetal1 to 3 suit Archipelago Base-78,
// ArchMetalB1 to B3 Archipelago Base-194, and ArchMetalC1 to C3 Lava Base-86 and
// Base-190. True for every other feature.
bool featureSuitsBase(const RmsFeature& feature, std::string_view base);
// The lands a feature of this kind goes on: its own, plus any it is lent to. Acid has
// no rocks of its own, so Lush rocks, LushCrushed ones too, are its ROCKS; Desert
// has no trees, so Archipelago's trees and palms are its TREES and PALMS.
std::vector<std::string> featureLands(const RmsFeature& feature, DoodadKind kind);
bool featureSuitsLand(const RmsFeature& feature, DoodadKind kind, const std::string& land);
// True for features made for the sea floor, such as GreenAquaOre1, corals and kelp.
// TDF files do not say so, so the name and category decide. These only go in water,
// and every other feature only on dry land.
bool underwaterFeature(const RmsFeature& feature);
// How far the ground rises and falls under a feature: the highest minus the lowest
// height in the footprint, w x h feature cells at (x, y), far edge included. heights is
// the map's height grid, width x height, one value per feature cell.
int footprintRise(const std::vector<std::uint8_t>& heights, int width, int height, int x, int y, int w, int h);
// Metal nodes on ground that rises more than this, such as a hill, are removed: a metal
// extractor cannot be built there.
inline constexpr int kMetalNodeMaxRise = 16;
// Doodads such as trees stand on a blend between two grounds whose heights are this
// close, such as Lush at 77 and GreenWorld at 85, but not across a cliff or a coast.
inline constexpr int kBlendDoodadRise = 16;
// At most this many trees stand on one map tile, a 2 x 2 block of feature cells, so
// a forest is not packed tight: one tree to a tile.
inline constexpr int kMaxTreesPerTile = 1;
// How far, in feature cells, a metal node on a hill may move to reach flat ground.
inline constexpr int kMetalNodeMoveRadius = 48;
// The nearest place, out to radius feature cells from (x, y), for a w x h footprint
// whose cells are all free in occupied (one byte per feature cell, nonzero when taken),
// whose ground rises no more than maxRise, and whose every height is within maxRise
// of level, so the node stays on the ground it was placed for. Nearer rings are tried
// first. False when there is none.
bool findFlatSpot(const std::vector<std::uint8_t>& heights, const std::vector<std::uint8_t>& occupied, int width, int height, int w, int h,
                  int x, int y, int level, int maxRise, int radius, int& outX, int& outY);

// What the generator needs to place features. Positions are in feature cells,
// 16 x 16 pixels, the footprint of one tree; a game-size square is 32 of them.
struct RmsObjectSettings {
    std::vector<RmsFeature> features;
    // For each terrain, its land ("Green") and whether it is water, lava or another
    // liquid that features stay off.
    std::vector<std::string> terrainLands;
    std::vector<bool> terrainLiquid;
    // Each terrain's Base tile name, such as "Base-78". May be empty.
    std::vector<std::string> terrainBases;
    // Each terrain's ground height, such as 77. Doodads such as trees go on a blend
    // between two dry grounds when their heights are within kBlendDoodadRise; with
    // no heights they keep to cells of one ground.
    std::vector<int> terrainHeights;
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

// The steps of a random map run, in the order they happen, for a progress bar.
enum class RandomMapStage { Lands, Elevation, Cliffs, Terrain, Connections, Objects };

// Thrown by runRandomMapScript when the run is cancelled.
struct RandomMapCancelled : std::exception {
    const char* what() const noexcept override { return "The random map was cancelled."; }
};

struct RandomMapOptions {
    int players = 2;
    // Null when features are not placed; a script with <OBJECTS_GENERATION> then gets a note.
    const RmsObjectSettings* objects = nullptr;
    // Told the step under way and how far through it the run is, from 0 to 1. It is called
    // often, so it should be cheap; a run on another thread can store the values.
    std::function<void(RandomMapStage, double)> progress;
    // When set, the run stops at its next progress report by throwing RandomMapCancelled.
    const std::atomic<bool>* cancel = nullptr;
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
    // create_terrain commands that name a Tile Brush stamp, as indexes into the script's commands.
    std::vector<std::size_t> stampCommands;
    // create_elevation commands, placed as the hill stamps of their base_terrain.
    std::vector<std::size_t> elevationCommands;
    // Cliffs drawn by <CLIFF_GENERATION>.
    int cliffs = 0;
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

// One piece of a stamp brush, with its size in map tiles.
struct RandomMapStampSection {
    std::string path;
    int width = 0;
    int height = 0;
};

// A Tile Brush stamp a script places with create_terrain: a hill, pool, rough patch,
// island, crater and so on, stamped whole onto flat ground of its own base. Pieces of
// one brush may differ in size, as Desert's plateaus and Crystal's roughs do.
struct RandomMapStampBrush {
    std::string name;  // such as LAVA_POOL-86_16X16
    std::string base;  // the terrain it sits on, such as LAVA_BASE-86
    std::vector<RandomMapStampSection> sections;
};

// The script name of a Tile Brush stamp button: LAVA_POOL-86_16X16 for Lava's "Pool-86 16x16".
std::string stampBrushScriptName(std::string_view world, std::string_view button);

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

// Where the script's stamp brushes go, on the finished canvas, inside a map tileWidth
// x tileHeight tiles. Each snaps the way the Tile Brush snaps it: smaller than a cell
// to the 8 x 8 grid, a cell to the cell grid, larger centred on a cell corner. Every
// cell under it must be flat ground of its base, and no two overlap. Notes about
// stamps that found no room are added to messages.
std::vector<RandomMapStamp> planStampBrushes(const RmsScript& script, const RandomMapOutcome& outcome,
                                             const std::vector<RandomMapStampBrush>& brushes, const BrushCanvas& canvas,
                                             const RmsTerrainNames& names, int tileWidth, int tileHeight, std::mt19937& rng,
                                             std::vector<RmsMessage>& messages);

}  // namespace galaxy

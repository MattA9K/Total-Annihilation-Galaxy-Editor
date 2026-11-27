#pragma once

#include "core/BrushStrokes.hpp"
#include "core/PieceScaler.hpp"
#include "core/OrbitCamera.hpp"
#include "core/RandomMap.hpp"
#include "core/RiverBrush.hpp"
#include "core/TilesetLibrary.hpp"
#include "format/MapFile.hpp"
#include "format/SctSection.hpp"
#include "ui/GlassBlur.hpp"
#include "ui/HpiEditor.hpp"
#include "ui/Terrain3D.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

typedef unsigned int GLuint;

namespace galaxy {

// What a Tile Brush button paints. Base tiles use the brush strokes. Rivers, ridges
// and slopes place one 16 x 16 piece chosen from the neighbouring cells. Hills drop
// one random hill onto GreenWorld Base-85.
enum class BrushKind { Base, River, Bridge, Ridge, Spacer, Slope, Hill };

// The steps the random map progress window names, in order: the generator's own
// steps from Lands to Objects, then the tiles and stamps that draw them.
enum class RandomMapStep { Preparing, Lands, Elevation, Cliffs, Terrain, Connections, Objects, Tiles, Stamps, Building, Count };
struct RandomMapJob;

class EditorApp {
public:
    // Stops a random map still running on another thread.
    ~EditorApp();
    // Loads every tileset archive before returning.
    void loadTilesets();
    // The same work in small steps, so a splash screen can show progress.
    void beginLoadingTilesets();
    // Drops every loaded archive and starts over from the current game folder.
    // The archives open a few at a time as the editor draws.
    void reloadTilesets();
    bool loadingTilesets() const { return tilesetLoadNext_ < tilesetLoadQueue_.size(); }
    // Opens archives until the time budget is used. Always opens at least one.
    void continueLoadingTilesets(std::chrono::milliseconds budget);
    std::size_t tilesetLoadTotal() const { return tilesetLoadQueue_.size(); }
    std::size_t tilesetLoadDone() const { return tilesetLoadNext_; }
    // File name of the next archive to open, or empty when none is left.
    std::string tilesetLoadNextName() const;
    void openMap(const std::filesystem::path& path);
    bool draw();
    void requestClose();
    // Files dropped on the window from the system.
    void handleDroppedPaths(const std::vector<std::filesystem::path>& paths);
    bool selectionMode() const { return selectionMode_ && !featuresTab_ && !view3d_; }
    bool hasSelection() const { return !selection_.empty(); }

    std::filesystem::path suggestedOpenDirectory() const;

private:
    struct CachedSection {
        std::string world;
        std::string category;
        std::string name;
        SectionStamp stamp;
        GLuint preview = 0;
    };

    struct CategoryPages {
        std::vector<int> sections;
        int total = 0;
        int failed = 0;
        bool complete = false;
        std::string lastError;
    };

    struct CategoryRequest {
        std::string key;
        std::size_t archiveIndex = 0;
        std::string world;
        std::string category;
        std::vector<TilesetSection> sections;
        std::size_t next = 0;
    };

    // The sidebar tree of archives, worlds, and categories. It is built once per
    // set of loaded archives. Typing in the search box only updates the flags below.
    struct TilesetIndexCategory {
        std::string name;
        std::vector<std::uint32_t> sectionIds;  // positions in TilesetArchive::sections
        int total = 0;
        int shown = 0;
        bool visible = true;
        bool filterNames = false;  // show only the sections whose name matches
        bool openOnSearch = false;
    };
    struct TilesetIndexWorld {
        std::string name;
        std::vector<TilesetIndexCategory> categories;
        bool visible = true;
        bool openOnSearch = false;
    };
    struct TilesetIndexArchive {
        std::size_t archiveIndex = 0;
        std::string filename;
        std::string title;
        std::vector<TilesetIndexWorld> worlds;
        bool visible = true;
        bool favorite = false;  // a GE-Favorites-<name>.hpi archive: shown on the Favorites tab only
    };

    void rebuildTilesetIndex();
    void applyTilesetSearch(const std::string& query);

    void renderViewport(float canvasWidth, float canvasHeight);
    void renderHeightOverlay(float canvasWidth, float canvasHeight);
    void drawToolbar();
    void toggleSelectMode();
    void toggleDeleteMode();
    void drawSidebar();
    // The tree of archives, categories and tiles. The Tilesets tab lists every
    // archive except favorites archives, and the Favorites tab lists only those.
    void drawTilesetBrowser(bool favorites);
    void drawSidebarBackground();
    void updateAppearance();
    void drawToolbarBackground();

    // One piece of frosted glass: a small picture of the map behind a panel,
    // blurred on the graphics card. Each panel keeps its own, and redraws it only
    // when the map view or the panel's place on screen changes.
    struct GlassPanel {
        GlassBlur blur;
        std::vector<std::uint8_t> image;
        std::uint64_t generation = ~std::uint64_t{0};
        float panX = 0.0f;
        float panY = 0.0f;
        float zoom = 0.0f;
        int texW = 0;
        int texH = 0;
    };
    // force draws the glass even when frosted glass is turned off or the 3D view is showing.
    bool updateGlass(GlassPanel& panel, float left, float top, float width, float height, bool force = false);
    // Paints the glass over a screen rectangle. Returns false when there is no glass
    // to paint, so the caller can draw a flat color instead.
    bool drawGlass(GlassPanel& panel, ImDrawList* draw, float left, float top, float right, float bottom, float rounding, bool force = false);
    bool drawGlass(GlassPanel& panel, float left, float top, float right, float bottom, float rounding);
    // The tooltip ImGui opens for a hovered control. Its flat fill stays when glass
    // cannot be made (the 3D view, the Frosted Glass menu off, or no map).
    void drawTooltipGlass();
    void drawStartLocations();
    void drawFeatures();
    void setStartPosition(int player, int x, int y);
    void clearStartPosition(int player);
    void placeArmedFeature(int cellX, int cellY);

    struct FeatureSprite {
        GLuint texture = 0;
        int width = 0;
        int height = 0;
        int hotspotX = 0;
        int hotspotY = 0;
        int footprintX = 1;
        int footprintZ = 1;
        int metal = 0;
        int energy = 0;
    };

    const FeatureSprite* featurePicture(const std::string& name);
    bool featurePictureBounds(const std::string& name, int cellX, int cellY, float& x0, float& y0, float& x1, float& y1);
    void drawMap();
    void drawMap3d();
    void upload3d();
    void set3dView(bool on);
    void drawStatus();
    void drawOptions();
    void drawMapSettings();
    void loadEditorOptions();
    void chooseGameFolder();
    void setGameFolder(const std::filesystem::path& folder);
    void saveEditorOptions() const;
    // File > Random Map Generator: a floating window with an Age of Empires II
    // style random map script, run on the active map with the Tile Brush.
    void drawRandomMap();
    // Runs the window's script on the active map as one undoable edit, waiting for it to
    // finish. Returns false, and leaves the map alone, when nothing could be painted.
    bool generateRandomMap();
    // The same in the background: startRandomMap prepares the run on this thread and
    // hands the script to another; finishRandomMap, called every frame, writes the
    // result into the map once it is back. A progress window shows meanwhile.
    bool startRandomMap();
    void finishRandomMap();
    void cancelRandomMap();
    bool generatingRandomMap() const { return randomMapJob_ != nullptr; }
    void drawRandomMapProgress();
    std::filesystem::path randomMapScriptPath() const;
    void saveRandomMapScript() const;
    void drawNewMap();
    void drawResizeMap();
    void applyResize();
    void drawModifyHeight();
    void drawPlaceHeight();
    int placeHeightDelta() const;
    void drawSelectionMenu();
    // Favorites: tiles the user keeps in GE-Favorites-<name>.hpi archives.
    void openAddToFavorites(SectionStamp stamp, std::string description, std::string note = {});
    void drawAddFavorite();
    void drawCreateFavorites();
    void drawDeleteFavorite();
    void requestDeleteFavorite(std::size_t archiveIndex, const CachedSection& section);
    void requestDeleteFavoriteSelection();
    // Plain click selects one favorite and arms it for the map. Ctrl-click, which
    // is Command-click on macOS, adds or removes that tile from the selection.
    void selectFavoriteTile(std::size_t archiveIndex, int sectionIndex, bool extend);
    // Tile lists on the Tilesets and Favorites tabs: a plain click selects one tile and
    // arms it, Command/Ctrl-click toggles one, Shift-click selects the range from the
    // last clicked tile within the same category. shown is the category in display order.
    void clickSidebarTile(bool favorites, std::size_t archiveIndex, const std::vector<int>& shown, int position);
    // The selected tiles of one tab as (section index into sections_, archive index) pairs, in index order.
    std::vector<std::pair<int, std::size_t>> sidebarTileSelection(bool favorites) const;
    // archive path + "/" + path inside it, e.g. /x/Foo.hpi/sections/World/Cat/Name.sct.
    std::string sidebarTileFullPath(std::size_t archiveIndex, const CachedSection& section) const;
    void copySidebarTilePaths(bool favorites);
    void exportSidebarTilesBmp(bool favorites);
    void requestRenameTile(std::size_t archiveIndex, const CachedSection& section);
    void drawRenameTile();
    // Reads one tileset archive from disk again and drops the pictures cached from it.
    void refreshFavoritesArchive(const std::filesystem::path& path);
    void createFavoritesArchiveNamed();
    void drawClosePrompt();
    void drawAbout();
    void drawMinimap();
    void ensureMinimap();
    // Base tiles from the random-map tileset, shown in the Tile Brush window.
    void ensureBrushPaints();
    void drawTileBrush();
    void armBrush(const std::string& name);
    // Stamps one random BaseTileBrushStrokes variation for the armed paint and size.
    void paintBrushStroke(int tileX, int tileY);
    // One base brush stroke on cells of cellTiles tiles. NeedsCoarse, with the map left alone, when an
    // 8 tile stroke needs a piece only the 16 x 16 set has, such as a cliff. NeedsFine when a 16 tile
    // stroke would put a 16 x 16 piece beside an 8 x 8 band, which it checks only when mayRefine is set.
    // footprintTiles is the grid the painted block is placed on: a 16 x 16 stroke moved onto the 8 tile
    // cells keeps the footprint its outline showed.
    enum class BrushOutcome { Done, NeedsCoarse, NeedsFine };
    BrushOutcome paintBaseBrush(int tileX, int tileY, int cellTiles, bool mayRefine, int footprintTiles);
    // Which sizes of brush piece the 16 x 16 cell at cellX, cellY holds: kCoarseBand, kFineBand,
    // both, or 0. Read from the tile pictures and the tileset's lists of band tiles.
    int brushCellPieceSizes(int cellX, int cellY) const;
    // The band tiles of the tileset's 16 x 16 and 8 x 8 pieces, sorted. Empty for a tileset without the lists.
    std::vector<std::uint64_t> coarseBandTiles_;
    std::vector<std::uint64_t> fineBandTiles_;
    // The tiles of the pieces that only come in 16 x 16, such as cliffs and rivers, sorted.
    std::vector<std::uint64_t> fixedTiles_;
    // Pictures of the flat ground of other tilesets' bases, which this brush has no pieces to join,
    // such as Lava Base-41 while GreenWorld is chosen, with the base's name. A stroke that would
    // redraw a cell holding one is refused: read by its height, the lava would pass for shallow water.
    std::unordered_map<std::uint64_t, std::string> foreignPictures_;
    // The name of the other base in the cell, or empty, for the stroke being planned.
    std::string foreignGroundIn(int tileX, int tileY, int width, int height) const;
    mutable std::unordered_map<std::uint32_t, int> pictureForeign_;
    // Pictures of the flat ground of this brush's own bases and partner lands, with their strokes.
    std::unordered_set<std::uint64_t> brushBaseTiles_;
    mutable std::vector<std::string> foreignNames_;
    // The flat ground of each land, from its BaseTiles section, by "category/name" in lower case. The
    // 8 x 8 pieces are shifted onto the map's grid with it. Read on first use.
    std::unordered_map<std::string, PaletteImage> brushGrounds_;
    const PaletteImage* brushGround(const std::string& category, const std::string& name);
    // The cache of a tileset's brush pictures, kept beside the random map tileset: where it goes for this
    // catalogue, reading it back into brushPictures_, sharedPictures_ and brushCoastLiquid_, and writing it.
    std::filesystem::path brushPictureCachePath(const std::filesystem::path& tileset, const std::string& catalogueKey) const;
    bool loadBrushPictureCache(const std::filesystem::path& cache);
    void saveBrushPictureCache(const std::filesystem::path& cache) const;
    // The band sizes of each tile picture on the map, by picture index, for the stroke being planned.
    // Cleared at the start of every stroke, since a stroke adds pictures and undo takes them away.
    mutable std::unordered_map<std::uint32_t, int> pictureBandSizes_;
    // The first piece an 8 tile stroke needed that only the 16 x 16 set has, for the status bar.
    std::string coarseOnlyPiece_;
    // The cell size the armed base brush paints on: 8 for the 4 x 4 and 8 x 8 brushes when the
    // tileset has 8 x 8 pieces, otherwise 16.
    int brushCellTiles() const;
    // River, bridge, ridge and spacer brushes work on one 16 x 16 cell and read the map to choose the piece.
    void ensureRiverCatalog();
    RiverPlan riverPlanAt(int cellX, int cellY, BrushKind kind);
    void paintRiver(int tileX, int tileY, BrushKind kind);
    // 8 or 16 while a hill button is armed, otherwise 0.
    int hillSpan() const;
    // Tiles down for the armed hill brush. The same as hillSpan except for folder brushes
    // with rectangular tiles, such as Desert's 56 x 32 plateau.
    int hillSpanHigh() const;
    // The top-left tile of the hill under the pointer. Island-0 Medium and Large are
    // centered on the pointer; the other hills snap to the 8 x 8 or 16 x 16 grid.
    void hillOrigin(int tileX, int tileY, int width, int height, int& originX, int& originY) const;
    // Island-0 Medium and Large place the island chosen ahead of time, so the cursor
    // can show its size. This is that island's index in hillSections_, or -1.
    int nextSeaIsland() const;
    // Horizontal Mtn and Vertical Mtn (Lush) are rows of 32 x 32 pieces: an end, middle
    // pieces, and the other end. A click places the two ends, and dragging along the
    // mountain adds middle pieces. Horizontal runs west to east, Vertical north to south.
    // While the random map generator collects stamp brushes, the catalog skips the
    // pictures and blend partners that only painting by hand needs.
    bool brushCatalogStampsOnly_ = false;
    // Lush's mountains, and WaterWorld's reefs, which grow the same way from 16 x 16 pieces on its Base-0 water.
    bool reefArmed() const { return brushName_ == "Horizontal Reef" || brushName_ == "Vertical Reef"; }
    bool mountainArmed() const { return brushName_ == "Horizontal Mtn" || brushName_ == "Vertical Mtn" || reefArmed(); }
    bool mountainVertical() const { return brushName_ == "Vertical Mtn" || brushName_ == "Vertical Reef"; }
    // The side of one mountain piece: 32 tiles for Lush's mountains, 16 for WaterWorld's reefs.
    int mountainSpan() const { return reefArmed() ? 16 : 32; }
    // The tiles the mountain covers: the whole mountain while dragging, otherwise the
    // first two pieces centered on the cell corner nearest the pointer.
    void mountainFootprint(int tileX, int tileY, int& x, int& y, int& width, int& height) const;
    bool mountainCanPlace(int x, int y, int width, int height, std::string& refusal) const;
    void startMountain(int tileX, int tileY);
    void extendMountain(int tileX, int tileY);
    // A random loaded piece whose name starts with prefix (W_end, E_end, EW, N_end, S_end, or NS), or null.
    const SectionStamp* mountainPiece(std::string_view prefix);
    // Dunes-80 (Desert) paints whole 16 x 16 cells. Each cell is four 8 x 8 quarters. A quarter
    // is solid dune when the cells beside it across and above or below are both dune; otherwise
    // it is the corner piece for that quarter, with the Base-80 sand in its outer corner.
    bool duneArmed() const { return brushName_ == "Dunes-80"; }
    // True when most of the cell's tiles show dune pictures.
    bool duneCell(int cellX, int cellY) const;
    bool duneCanPlace(int cellX, int cellY, std::string& refusal) const;
    void paintDune(int tileX, int tileY);
    // Chooses another island of the armed size. N does this while the brush is armed.
    void skipSeaIsland();
    struct HillSection;
    // The first loaded section of the armed folder brush (Crystal), or null.
    const HillSection* groupHill() const;
    // True when the armed brush mixes sizes, so the next tile is chosen ahead of time:
    // Island-0 Medium and Large, and Crystal's Rough-85.
    bool brushVaries() const;
    // Whether a section belongs to the armed brush that mixes sizes.
    bool nextMatches(const HillSection& hill) const;
    bool hillCanPlace(int originX, int originY, int width, int height, std::string& refusal) const;
    void paintHill(int tileX, int tileY);
    // True when this map tile's picture is the named flat base, or same-world ground closer to it.
    bool tileMatchesBase(int tileX, int tileY, std::string_view world, std::string_view baseName, int baseHeight, bool sameWorldFringe) const;
    // True when this map tile's picture is GreenWorld Base-85 ground.
    bool tileIsGreenBase85(int tileX, int tileY) const;
    // True when this map tile's picture is flat GreenWorld water, Base-0.
    bool tileIsGreenBase0(int tileX, int tileY) const;
    // True when this map tile's picture is Lava Base-86 ground.
    bool tileIsLavaBase86(int tileX, int tileY) const;
    bool tileIsBrushWorldBase85(int tileX, int tileY) const;
    // True when this map tile's picture is Lush Base-77 ground.
    bool tileIsLushBase77(int tileX, int tileY) const;
    bool tileIsArchipelagoBase194(int tileX, int tileY) const;
    bool tileIsArchipelagoBase78(int tileX, int tileY) const;
    bool tileIsArchipelagoBase0(int tileX, int tileY) const;
    // Base-0 of the tileset chosen in the Tile Brush window. Island-0 buttons use it.
    bool tileIsBrushWorldBase0(int tileX, int tileY) const;
    BrushKind armedBrushKind() const;
    bool lineBrushArmed() const;
    void pumpSectionLoads();
    void requestCategory(std::size_t archiveIndex, const std::string& world, const std::string& category);
    void ensureGhost();
    const SectionStamp* placementStamp() const;
    void adjustPlaceOffset(int dx, int dy);
    void placeSelected(int tileX, int tileY);
    enum class SelectOp { Replace, Add, Subtract };

    struct EditorCommand {
        std::optional<PlacementChange> terrain;
        // Several terrain edits from one gesture, such as a base stroke plus its cliff.
        // Undo walks this from last to first.
        std::vector<PlacementChange> terrains;
        std::optional<FeatureChange> feature;
        std::vector<FeatureChange> features;
        // Every feature on the map replaced at once, as the random map generator does.
        std::optional<FeatureBatchChange> featureBatch;
        // Every start position replaced at once, as the random map generator does.
        std::optional<std::vector<StartPosition>> startsBefore;
        std::optional<std::vector<StartPosition>> startsAfter;
        bool selection = false;
        TileSelection selectionBefore;
        TileSelection selectionAfter;
    };

    void rememberPlacement(PlacementChange change);
    void rememberPlacements(std::vector<PlacementChange> changes);
    void rememberFeature(FeatureChange change);
    void rememberFeatures(std::vector<FeatureChange> changes);
    // The selected features that are still on the map, in map order.
    std::vector<FeaturePlacement> selectedFeaturePlacements();
    void copyFeatures(bool cut);
    void deleteSelectedFeatures();
    void armFeaturePaste();
    void toggleFeatureSelectMode();
    void pasteFeaturesAt(int cellX, int cellY);
    void rememberSelection(const TileSelection& before, const TileSelection& after);
    void pushCommand(EditorCommand command, bool editsMap);
    void copySelection(bool heightsOnly = false);
    void cutSelection();
    void blendSelection();
    void armPaste();
    void pasteAt(int tileX, int tileY);
    bool clampedSelection(int x0, int y0, int x1, int y1, int& left, int& top, int& right, int& bottom) const;
    void previewSelection(int x0, int y0, int x1, int y1);
    void commitSelection();
    void clearMapSelection();
    void ensureClipboardGhost();
    void undoPlacement();
    void redoPlacement();
    void clearHistory();
    void invalidateView();
    bool saveMap();
    bool saveMapAs();
    bool exportMapPng();
    // Starts writing the map on a background thread and returns at once. The map is
    // copied first, so editing can go on while the file is written. True when the
    // save started; the result arrives later through finishMapSave.
    bool saveMapTo(const std::filesystem::path& path);
    bool savingMap() const { return mapSave_.valid(); }
    // Picks up a finished background save. Called once a frame.
    void finishMapSave();
    // Blurs the map area and shows a spinner while a save runs.
    void drawSavingOverlay(float left, float top, float right, float bottom);
    void markMapDirty();
    void autosaveIfDue();
    // Writes a timed backup of one map next to its file. Returns the backup's file name.
    std::string writeAutosave(const LoadedMap& map);

    // Every open map is a document. The active one lives in the members below
    // (map_, undo_, selection_ and the rest); the others wait here with their
    // own copy of that state. documents_[activeDocument_] is an empty shell
    // while its document is active.
    struct MapDocument {
        std::optional<LoadedMap> map;
        TileDeduper deduper;
        std::vector<EditorCommand> undo;
        std::vector<EditorCommand> redo;
        TileSelection selection;
        std::set<std::pair<int, int>> selectedFeatures;
        bool dirty = false;
        std::chrono::steady_clock::time_point autosaveArmedAt{};
        bool fit = true;
        float zoom = 1.0f;
        float panX = 0.0f;
        float panY = 0.0f;
        OrbitCamera camera3d;
        std::uint64_t serial = 0;
        std::uint64_t framedSerial = 0;
        bool vanillaTileWarningShown = false;
        // N for a map called "Untitled Map N" that has never been saved, otherwise 0.
        int untitledNumber = 0;
    };
    // Moves a new map in as the active document and resets the view for it.
    void addDocument(LoadedMap map, int untitledNumber);
    // Clears the view caches and any gesture under way after the active map changes.
    void resetDocumentView();
    void parkActiveDocument();
    void unparkDocument(int index);
    void activateDocument(int index);
    // Drops a document without asking. The neighbour to its right, or left, becomes active.
    void closeDocument(int index);
    // Closes a document, first asking to save it when it has unsaved changes.
    void requestCloseDocument(int index);
    int documentIndexForSerial(std::uint64_t serial) const;
    int documentIndexForPath(const std::filesystem::path& path) const;
    bool documentDirty(int index) const;
    const LoadedMap* documentMap(int index) const;
    // The name on a document's tab: the file name without extension, or "Untitled Map N".
    std::string documentTitle(int index) const;
    int nextUntitledDocumentNumber() const;
    void drawDocumentTabs();
    float documentTabsHeight() const;
    void warnIfPastVanillaTiles(int tileCountBefore);
    void showVanillaTileWarning();

    TilesetLibrary tilesets_;
    std::optional<LoadedMap> map_;
    std::vector<std::uint8_t> image_;
    GLuint texture_ = 0;
    int viewTexW_ = 0;
    int viewTexH_ = 0;
    float viewPanX_ = 0.0f;
    float viewPanY_ = 0.0f;
    float viewZoom_ = 0.0f;
    float viewCanvasW_ = 0.0f;
    float viewCanvasH_ = 0.0f;
    // Counts every time the map picture is redrawn, so the sidebar glass knows when to follow.
    std::uint64_t viewGeneration_ = 0;
    bool frostedGlass_ = true;
    // The 3D view of the map, which replaces the flat view while it is on.
    bool view3d_ = false;
    OrbitCamera camera3d_;
    Terrain3D terrain3d_;
    std::vector<std::uint8_t> colors3d_;
    // Bumped whenever the map's tiles or heights change, so the 3D textures follow.
    std::uint64_t mapRevision_ = 1;
    std::uint64_t uploaded3dRevision_ = 0;
    // Bumped when a different map is opened or created, which resets the camera.
    std::uint64_t mapSerial_ = 1;
    std::uint64_t framed3dSerial_ = 0;
    // Sea plane in the 3D view: opacity in percent, and color as 0xRRGGBB.
    int seaOpacity_ = 45;
    int seaColor_ = 0x2E7FDC;
    GlassPanel sidebarGlass_;
    GlassPanel toolbarGlass_;
    GlassPanel minimapGlass_;
    GlassPanel documentTabsGlass_;
    float mapOriginX_ = 0.0f;
    float mapOriginY_ = 0.0f;
    bool fit_ = true;
    float zoom_ = 1.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    bool showFeatures_ = true;
    bool showFeatureBorders_ = true;
    // The white name drawn beside each feature on the map.
    bool showFeatureLabels_ = true;
    bool showStarts_ = true;
    int sidebarTab_ = 0;
    bool startLocationsTab_ = false;
    bool featuresTab_ = false;
    std::string armedFeature_;
    bool featurePressed_ = false;
    bool featureDelete_ = false;
    // Selected features, by the cell each one sits in.
    std::set<std::pair<int, int>> selectedFeatures_;
    // Copied features. Each keeps its offset from the top-left cell of the copy.
    struct FeatureClip {
        std::string name;
        int dx = 0;
        int dy = 0;
    };
    std::vector<FeatureClip> featureClipboard_;
    bool featureClipboardNewer_ = false;
    bool featurePasteArmed_ = false;
    bool featureSelectMode_ = false;
    bool featureMarquee_ = false;
    float featureMarqueeX0_ = 0.0f;
    float featureMarqueeY0_ = 0.0f;
    float featureMarqueeX1_ = 0.0f;
    float featureMarqueeY1_ = 0.0f;
    std::vector<MapDocument> documents_;
    int activeDocument_ = -1;
    // Hands out document serials, so each open map keeps its own.
    std::uint64_t serialCounter_ = 1;
    // The close prompt asks about one tab rather than quitting.
    bool closePromptForTab_ = false;
    // Set when Save in the close prompt should close that map once the save finishes.
    std::uint64_t closeAfterSaveSerial_ = 0;
    // Maps the user chose not to save while quitting. Cancel clears this.
    std::set<std::uint64_t> quitDiscarded_;
    int armedStartPlayer_ = 0;
    int draggingStartPlayer_ = 0;
    // Set when a number key chooses a player, so that row scrolls into view.
    int revealStartPlayer_ = 0;
    bool showHeightGrid_ = false;
    int gridTiles_ = 0;
    int heightGridSize_ = 32;
    bool showMinimap_ = true;
    // Floating palette of base tiles. Choosing one arms a square brush on the map.
    bool showTileBrush_ = true;
    // The left sidebar with the Tilesets, Favorites, Features and Start Locations tabs.
    bool showSidebar_ = true;
    // How far the sidebar is slid in, from 0 (hidden) to 1 (shown). It follows
    // showSidebar_ over a short animation.
    float sidebarReveal_ = 1.0f;
    bool sidebarRevealStarted_ = false;
    struct BrushPaint {
        std::string name;
        std::string category;
        // The button's caption when it differs from the name: Evergreen's two Base-85 grasses are
        // Base-85-1 and Base-85-2.
        std::string label;
        GLuint preview = 0;
        BrushKind kind = BrushKind::Base;
    };
    std::vector<BrushPaint> brushPaints_;
    // The other lands this brush can blend into, such as Mars Base-85 and GreenWorld
    // Base-85 while Lava is selected. They are not buttons.
    std::vector<BrushPaint> brushPartners_;
    // Tile pictures that identify a land when two paints share a height. A fringe
    // picture is a blend or a lava coast, and the corner height chooses between
    // its paint and partnerName.
    struct BrushPictureHint {
        std::string category;
        std::string name;
        bool fringe = false;
        std::string partnerCategory;
        std::string partnerName;
    };
    std::unordered_map<std::uint64_t, BrushPictureHint> brushPictures_;
    // Pictures that several bases of one tileset use, such as the Lush grass at Base-77 and
    // Base-183, or Desert Base-155 and Base-230. They stay out of brushPictures_, so the base
    // brush reads those tiles by height. Each entry lists the bases as category and name.
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::string, std::string>>> sharedPictures_;
    // The armed tileset's base paints and partner lands, lowest height first: the brush's levels.
    std::vector<const BrushPaint*> brushLevels() const;
    // What reading a corner needs, worked out once for a set of levels.
    struct BrushCornerReading {
        std::vector<const BrushPaint*> levels;
        std::vector<int> heights;
        // The armed tileset's own levels, the only ones a tile read by height alone can be.
        std::vector<std::size_t> own;
        // Each level's mean ground colour, or -1 when unknown.
        std::vector<std::array<double, 3>> colours;
        bool shared = false;
        bool pictures = false;
    };
    BrushCornerReading brushCornerReading(const std::vector<const BrushPaint*>& levels);
    // The level at corner (nodeX, nodeY) of the grid of cell x cell tile cells, read from the
    // four tiles around it, as the Tile Brush reads it before a stroke.
    int readBrushCorner(const BrushCornerReading& reading, const BrushGraph& graph, int nodeX, int nodeY, int cell) const;
    // View > Grid > Brush corners: each corner of the 16 x 16 tile cells labelled with the paint
    // the Tile Brush reads there. Worked out again only when the map or the tileset changes.
    bool showBrushCorners_ = false;
    // The level read at each corner, row by row, and each level's label, such as "Mars 0".
    std::vector<int> brushCornerLevels_;
    std::vector<std::string> brushCornerNames_;
    int brushCornerNodesX_ = 0;
    int brushCornerNodesY_ = 0;
    std::uint64_t brushCornerKey_ = 0;
    // Reads every corner again when the map's pictures, heights or the armed tileset changed.
    void refreshBrushCorners();
    // Stroke sections from BaseTileBrushStrokes. The stamp is read the first time that stroke is placed.
    // A land with no 8 x 8 stroke of its own gets four, one for each quarter of a 16 x 16 one:
    // cropTiles is then 8 and cropX and cropY say which quarter, in tiles.
    struct BrushStroke {
        std::string name;
        std::string category;
        std::string sectionPath;
        std::optional<SectionStamp> stamp;
        int cropX = 0;
        int cropY = 0;
        int cropTiles = 0;
    };
    std::vector<BrushStroke> brushStrokes_;
    // CliffTiles and CoastTiles sections, such as Cliff-High-BR-01 and Coast-Water-B-01. Loaded on first use.
    // tiles is 16, or 8 for the halved pieces in the -8x8 folders that the 4 x 4 and 8 x 8 brushes use.
    struct BrushPiece {
        std::string name;
        std::string category;
        std::string sectionPath;
        std::optional<SectionStamp> stamp;
        int tiles = kBrushCellTiles;
    };
    std::vector<BrushPiece> brushPieces_;
    // Sections from the Rivers folder, such as River-Horizontal-02 and RiverB-L-B-01. Loaded on first use.
    struct RiverSection {
        std::string name;
        std::string key;
        std::string sectionPath;
        std::optional<SectionStamp> stamp;
    };
    std::vector<RiverSection> riverSections_;
    // GreenWorld hills, Lava hills, and Lush pools. tiles is 8 or 16 once the section has been measured.
    struct HillSection {
        std::string name;
        std::string sectionPath;
        int tiles = 0;
        // Tiles down when the section is not square; 0 means the same as tiles.
        int tilesHigh = 0;
        int high() const { return tilesHigh > 0 ? tilesHigh : tiles; }
        bool rough = false;
        bool island = false;
        bool pool = false;
        // Archipelago Hills, which sit on Base-78 rather than Base-194.
        bool base78 = false;
        // Island-0 buttons: 1 Island-0 8x8, 2 Rocky-0 8x8, 3 Island-0 Medium, 4 Island-0 Large,
        // 5 Island-0 16x16, 6 Island-0 32x32. Only the islands of the chosen tileset are loaded.
        int seaIsland = 0;
        // Lava pools from worlds.hpi Lava/misc, raised by 41 so their rims sit on Base-86.
        bool lavaPool = false;
        // Hills-85 buttons: Mars-2.hpi Mars/hills and Ice.hpi Ice/Hills, on that tileset's Base-85.
        bool hill85 = false;
        // Pool-85 and Pools-85: Mars-2.hpi Mars/misc pools and Ice.hpi Ice/Pools, on that tileset's Base-85.
        bool pool85 = false;
        // Rough ground from Mars-2.hpi Mars/misc. These are also marked rough.
        bool marsRough = false;
        // The other 16 x 16 Mars/misc tiles from Mars-2.hpi, on Mars Base-85.
        bool marsFeature = false;
        // A Horizontal Mtn or Vertical Mtn piece from Lush.hpi Lush/S R Mts, raised by 27 to sit on Base-77.
        bool mountain = false;
        // A Dunes-80 piece: 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right corner,
        // 5 solid dune. 0 for every other section.
        int dune = 0;
        // Crystal.hpi folders (Craters, Hills, Islands, Ponds, Reefs, Roughs). The button
        // follows the folder and size once the size is read; group is that button's name.
        // Lush.hpi Islands, hills, swamps, and ponds, the Desert hills, and the Acid pools use
        // group too, set when they are found.
        std::string crystalFolder;
        std::string group;
        std::string noun;
        std::string baseName;
        int baseHeight = 0;
        std::optional<SectionStamp> stamp;
    };
    std::vector<HillSection> hillSections_;
    mutable int nextSeaIsland_ = -1;
    // The mountain being dragged: its top-left tile and how many pieces it has.
    bool mountainStroke_ = false;
    int mountainX_ = 0;
    int mountainY_ = 0;
    int mountainPieces_ = 0;
    // Every picture in the dune pieces, read the first time a dune is painted.
    std::unordered_set<std::uint64_t> dunePictures_;
    RiverCatalog riverCatalog_;
    bool riverCatalogReady_ = false;
    std::filesystem::path brushTilesetPath_;
    std::string brushCatalogKey_;
    // GreenWorld, Lava, or Mars. The Tile Brush window shows the pieces of this one.
    std::string brushWorld_ = "GreenWorld";
    // "Water" names Coast-Water. Lava names the same shapes Coast-Lava.
    std::string brushCoastLiquid_ = "Water";
    std::string brushName_;
    int brushSize_ = 16;
    bool brushStroking_ = false;
    int brushStrokeX_ = -1;
    int brushStrokeY_ = -1;
    GlassPanel tileBrushGlass_;
    GlassPanel tooltipGlass_;
    GlassPanel savingGlass_;
    // The background save: an empty string on success, otherwise the error.
    std::future<std::string> mapSave_;
    std::filesystem::path mapSavePath_;
    std::uint64_t mapSaveSerial_ = 0;
    // Save and Exit, or closing the window during a save, quits once the save finishes.
    bool quitAfterSave_ = false;
    bool miniDirty_ = true;
    bool heightDirty_ = true;
    bool quit_ = false;
    HpiEditor hpiEditor_;
    bool hpiQuitApproved_ = false;
    bool mapDirty_ = false;
    int autosaveMinutes_ = 60;
    int autosaveBackups_ = 5;
    std::chrono::steady_clock::time_point autosaveArmedAt_{};
    std::string error_;
    std::string statusNote_;
    std::string vanillaTileWarning_;
    bool vanillaTileWarningShown_ = false;

    std::vector<CachedSection> sections_;
    std::unordered_map<std::string, CategoryPages> categories_;
    std::deque<CategoryRequest> loadQueue_;
    int selectedSection_ = -1;
    int previewSize_ = 128;
    int sidebarFontPercent_ = 100;
    // 0 follows the system, 1 is light, 2 is dark.
    int appearanceMode_ = 0;
    bool appearanceApplied_ = false;
    bool aboutRequested_ = false;
    int nativeAppearanceApplied_ = -1;
    bool systemDark_ = false;
    bool systemDarkKnown_ = false;
    std::chrono::steady_clock::time_point systemDarkAsked_{};
    std::future<bool> systemDarkProbe_;
    char tilesetSearch_[128] = {};
    char favoritesSearch_[128] = {};
    // Right-click menu over the selected tiles.
    bool selectionMenuRequested_ = false;
    bool selectionMenuOpen_ = false;
    // The Add to Favorites sheet. The archive, category, and subcategory stay as
    // they were last time, since people add several tiles to the same place in a
    // row. The file name starts again from the suggested name for each tile.
    bool showAddFavorite_ = false;
    std::optional<SectionStamp> favoriteStamp_;
    std::string favoriteDescription_;
    std::string favoriteNote_;
    std::string favoriteProblem_;
    GLuint favoritePreview_ = 0;
    std::filesystem::path favoriteArchive_;
    char favoriteCategory_[128] = {};
    char favoriteSubcategory_[128] = {};
    char favoriteTileName_[128] = {};
    // The suggestion last written into the file name field. A name the user typed
    // is left alone when the suggested name changes.
    std::string favoriteShownSuggestion_;
    std::string favoriteNameSuggestion_;
    std::string favoriteNameKey_;
    bool favoriteTileNameEditing_ = false;
    bool showCreateFavorites_ = false;
    char favoriteNewName_[128] = {};
    bool showDeleteFavorite_ = false;
    // Tiles chosen on the Favorites tab. The section index is into sections_.
    // Command-click or Ctrl-click adds to this set. Delete and the right-click
    // menu remove every tile in it, from each archive, in one rewrite per archive.
    std::set<int> favoriteSelection_;
    std::unordered_map<int, std::size_t> favoriteSelectionArchive_;
    // Tiles chosen on the Tilesets tab, kept like favoriteSelection_.
    std::set<int> tilesetSelection_;
    std::unordered_map<int, std::size_t> tilesetSelectionArchive_;
    // The tile a Shift-click range starts from, per tab: 0 Tilesets, 1 Favorites.
    int tileSelectionAnchor_[2] = {-1, -1};
    // Set by the tile menu; the export runs after the list is drawn. 0 none, 1 Tilesets, 2 Favorites.
    int pendingTileExport_ = 0;
    std::filesystem::path tileExportFolder_;
    struct FavoriteDelete {
        std::filesystem::path archive;
        std::string sectionPath;
        std::string name;
    };
    std::vector<FavoriteDelete> deleteFavorites_;
    bool showRenameTile_ = false;
    std::filesystem::path renameArchive_;
    std::string renameArchiveLabel_;
    std::string renameSectionPath_;
    std::string renameWorld_;
    std::string renameCategory_;
    std::string renameOriginalName_;
    char renameFileName_[128] = {};
    // Where Total Annihilation is installed. The tileset archives come from here.
    std::filesystem::path gameFolder_;
    bool gameFolderAsked_ = false;
    bool gameFolderFound_ = false;
    std::size_t gameFolderArchives_ = 0;
    std::vector<std::filesystem::path> tilesetLoadQueue_;
    std::size_t tilesetLoadNext_ = 0;
    std::vector<std::string> tilesetLoadFailures_;
    std::vector<TilesetIndexArchive> tilesetIndex_;
    std::size_t tilesetIndexArchives_ = 0;
    int tilesetIndexSections_ = -1;
    std::string tilesetQueryApplied_;
    std::string tilesetQueryFolded_;
    bool tilesetQueryOpenPending_ = false;
    char featureSearch_[128] = {};
    std::unordered_map<std::string, FeatureSprite> featureSprites_;
    bool showOptions_ = false;
    bool showMapSettings_ = false;
    int mapSettingsTab_ = 0;
    bool mapSettingsFresh_ = false;
    bool showRandomMap_ = false;
    bool randomMapLoaded_ = false;
    std::string randomMapScript_;
    int randomMapPlayers_ = 2;
    int randomMapSeed_ = 1;
    // Point size of the script. Command + and Command - step through the code font sizes.
    int randomMapFontSize_ = 13;
    // What the last run said: notes about the script, and a line about the result.
    std::vector<RmsMessage> randomMapNotes_;
    std::string randomMapResult_;
    bool randomMapFailed_ = false;
    // The random map tileset's bases and pieces, read again when that archive changes.
    RandomMapTiles randomMapTiles_;
    // GreenWorld's hills, rough patches and islands as script stamps, measured once per tileset.
    std::vector<RandomMapStampBrush> randomMapStampBrushes_;
    std::string randomMapStampKey_;
    // The random map running in the background, if any.
    std::shared_ptr<RandomMapJob> randomMapJob_;
    std::future<void> randomMapWork_;
    // Auto Metal: MegaMetal4 on every 8 x 8 tile block of Metal ground.
    void autoMetal();
    // Every tile picture of the random map tileset's Metal sections, read once per tileset.
    std::unordered_set<std::uint64_t> metalPictures_;
    std::string metalPicturesKey_;
    // What the Auto Metal alert says, such as "no Metal tiles". Empty when it is closed.
    std::string autoMetalAlert_;
    // Every stamp button of every world's Tile Brush, with its pieces and the ground it
    // needs, read through the brush's own catalog so the two always agree.
    std::vector<RandomMapStampBrush> collectStampBrushes();
    // The world and base a stamp button goes on, as hillCanPlace decides. False for a
    // button the generator does not stamp, such as a mountain or dunes.
    bool stampBaseOf(const std::string& button, std::string& world, std::string& base) const;
    // Rebuilds randomMapStampBrushes_ when the random map tileset changes.
    void ensureStampBrushes();
    std::string randomMapTilesKey_;
    bool showNew_ = false;
    bool showResize_ = false;
    int resizeLeft_ = 0;
    int resizeRight_ = 0;
    int resizeTop_ = 0;
    int resizeBottom_ = 0;
    bool showModifyHeight_ = false;
    bool showPlaceHeight_ = false;
    bool showClosePrompt_ = false;
    int modifyHeightMode_ = static_cast<int>(HeightAdjust::Set);
    int modifyHeightAmount_ = 85;
    int newWidth_ = 64;
    int newHeight_ = 64;
    TileDeduper deduper_;
    std::vector<EditorCommand> undo_;
    std::vector<EditorCommand> redo_;
    bool selectionMode_ = false;
    bool selecting_ = false;
    SelectOp selectOp_ = SelectOp::Replace;
    TileSelection selection_;
    TileSelection selectionBefore_;
    bool pasteArmed_ = false;
    bool rightDown_ = false;
    bool rightDragged_ = false;
    bool middlePanning_ = false;
    int selectAnchorX_ = 0;
    int selectAnchorY_ = 0;
    std::optional<SectionStamp> clipboard_;
    GLuint clipboardGhost_ = 0;
    int clipboardGhostW_ = 0;
    int clipboardGhostH_ = 0;
    GLuint ghostTexture_ = 0;
    int ghostFor_ = -1;
    int ghostW_ = 0;
    int ghostH_ = 0;
    bool canvasPressed_ = false;
    int placeOpacity_ = 100;
    bool placeHeightAdd_ = true;
    int placeHeightAmount_ = 0;
    bool placeHeightDraftAdd_ = true;
    int placeHeightDraftAmount_ = 0;
    int placeOffsetX_ = 0;
    int placeOffsetY_ = 0;
    int placementOffsetFor_ = -1;
    int hoverTileX_ = 0;
    int hoverTileY_ = 0;
    bool hoverOverMap_ = false;
    int hoverHeight_ = -1;
    float pressX_ = 0.0f;
    float pressY_ = 0.0f;
    float mapCanvasW_ = 0.0f;
    float mapCanvasH_ = 0.0f;
    GLuint miniTexture_ = 0;
    int miniTexW_ = 0;
    int miniTexH_ = 0;
    float miniDisplayW_ = 0.0f;
    // The minimap's size in percent, 100 to 150, set by dragging its corner. While the
    // corner is dragged the picture is stretched, and drawn again at the new size after.
    int minimapPercent_ = 100;
    bool minimapResizing_ = false;
    float miniDisplayH_ = 0.0f;
    std::vector<std::uint8_t> miniImage_;
    GLuint heightTexture_ = 0;
    int heightTexW_ = 0;
    int heightTexH_ = 0;
    float heightPanX_ = 0.0f;
    float heightPanY_ = 0.0f;
    float heightZoom_ = 0.0f;
    float heightCanvasW_ = 0.0f;
    float heightCanvasH_ = 0.0f;
    int heightGridCached_ = 0;
    std::vector<std::uint8_t> heightImage_;
};

}  // namespace galaxy

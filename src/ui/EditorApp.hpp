#pragma once

#include "core/TilesetLibrary.hpp"
#include "format/MapFile.hpp"
#include "format/SctSection.hpp"
#include "ui/GlassBlur.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

typedef unsigned int GLuint;

namespace galaxy {

class EditorApp {
public:
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
    bool selectionMode() const { return selectionMode_ && !featuresTab_; }
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
    };

    void rebuildTilesetIndex();
    void applyTilesetSearch(const std::string& query);

    void renderViewport(float canvasWidth, float canvasHeight);
    void renderHeightOverlay(float canvasWidth, float canvasHeight);
    void drawToolbar();
    void toggleSelectMode();
    void toggleDeleteMode();
    void drawSidebar();
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
    bool updateGlass(GlassPanel& panel, float left, float top, float width, float height);
    // Paints the glass over a screen rectangle in the current window. Returns false
    // when there is no glass to paint, so the caller can draw a flat color instead.
    bool drawGlass(GlassPanel& panel, float left, float top, float right, float bottom, float rounding);
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
    void drawStatus();
    void drawOptions();
    void drawMapSettings();
    void loadEditorOptions();
    void chooseGameFolder();
    void setGameFolder(const std::filesystem::path& folder);
    void saveEditorOptions() const;
    void drawGenerate();
    void drawNewMap();
    void drawModifyHeight();
    void drawClosePrompt();
    void drawAbout();
    void drawMinimap();
    void ensureMinimap();
    void pumpSectionLoads();
    void requestCategory(std::size_t archiveIndex, const std::string& world, const std::string& category);
    void ensureGhost();
    const SectionStamp* placementStamp() const;
    void adjustPlaceOffset(int dx, int dy);
    void placeSelected(int tileX, int tileY);
    enum class SelectOp { Replace, Add, Subtract };

    struct EditorCommand {
        std::optional<PlacementChange> terrain;
        std::optional<FeatureChange> feature;
        bool selection = false;
        TileSelection selectionBefore;
        TileSelection selectionAfter;
    };

    void rememberPlacement(PlacementChange change);
    void rememberFeature(FeatureChange change);
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
    bool saveMapTo(const std::filesystem::path& path);
    void markMapDirty();
    void autosaveIfDue();
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
    GlassPanel sidebarGlass_;
    GlassPanel toolbarGlass_;
    GlassPanel minimapGlass_;
    float mapOriginX_ = 0.0f;
    float mapOriginY_ = 0.0f;
    bool fit_ = true;
    float zoom_ = 1.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    bool showFeatures_ = true;
    bool showStarts_ = true;
    int sidebarTab_ = 0;
    bool startLocationsTab_ = false;
    bool featuresTab_ = false;
    std::string armedFeature_;
    bool featurePressed_ = false;
    bool featureDelete_ = false;
    int armedStartPlayer_ = 0;
    int draggingStartPlayer_ = 0;
    bool showHeightGrid_ = false;
    int gridTiles_ = 0;
    int heightGridSize_ = 32;
    bool showMinimap_ = true;
    bool miniDirty_ = true;
    bool heightDirty_ = true;
    bool quit_ = false;
    bool mapDirty_ = false;
    int autosaveMinutes_ = 10;
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
    bool showGenerate_ = false;
    bool showNew_ = false;
    bool showModifyHeight_ = false;
    bool showClosePrompt_ = false;
    int modifyHeightMode_ = static_cast<int>(HeightAdjust::Set);
    int modifyHeightAmount_ = 85;
    int newWidth_ = 64;
    int newHeight_ = 64;
    std::string genWorld_;
    int genRivers_ = 35;
    int genCanyons_ = 40;
    int genMountains_ = 20;
    int genHills_ = 35;
    int genShore_ = 25;
    int genPools_ = 15;
    int genSeed_ = 1;
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

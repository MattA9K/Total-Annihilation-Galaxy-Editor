#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

namespace galaxy {

// Stable ids for user-visible text. English is the source string. Any missing
// translation falls back to English, so a new line can ship before every
// language has it.
enum class Text : int {
    PickerTitle,
    PickerBody,
    PickerContinue,
    SplashLoading,
    SplashCount,
    SplashReady,
    GameFolderMessage,
    GameFolderDetail,
    GameFolderMissing,
    Cancel,
    FindTaInstall,
    SelectTaFolder,
    MenuFile,
    MenuEdit,
    MenuMap,
    MenuView,
    MenuOptions,
    MenuHelp,
    NewMap,
    OpenMap,
    Save,
    SaveAs,
    GenerateMap,
    HpiEditor,
    Exit,
    QuitApp,
    Undo,
    Redo,
    CutFeatures,
    CopyFeatures,
    PasteFeatures,
    DeleteSelectedFeatures,
    SelectFeatures,
    Cut,
    CopyTerrain,
    CopyHeightMap,
    Paste,
    SelectTiles,
    ModifyHeight,
    BlendTextures,
    DeleteFeatures,
    TileOpacity,
    MapSettings,
    ResizeMap,
    FitMap,
    ActualSize,
    Grid,
    GridOff,
    View3d,
    Reset3dCamera,
    HideHeightMap,
    ShowHeightMap,
    FeatureMarkers,
    ShowFeatureBorders,
    StartPositions,
    Minimap,
    TileBrush,
    FrostedGlass,
    EditorOptions,
    AboutMenu,
    Selecting,
    Select,
    CopyHeights,
    Pasting,
    Blend,
    HideHeights,
    ShowHeights,
    Deleting,
    Delete,
    Brush,
    OpacityLabel,
    TipSelectFeatures,
    TipSelectTiles,
    TipCutFeatures,
    TipCutTiles,
    TipCopyFeatures,
    TipCopyTerrain,
    TipCopyHeights,
    TipPasteFeatures,
    TipPasteTiles,
    TipModifyHeight,
    TipBlend,
    TipFlatMap,
    TipView3d,
    TipHideGrid,
    TipShowGrid,
    TipUndo,
    TipRedo,
    TipOpacity,
    TipDelete,
    TipBrushFixed,
    TipBrushSize,
    TipHpi,
    Tilesets,
    Favorites,
    Features,
    StartLocations,
    TilesetHelp,
    FavoritesHelp,
    OptionsTitle,
    SectionLanguage,
    LanguageHelp,
    SectionAppearance,
    AppearanceAuto,
    AppearanceLight,
    AppearanceDark,
    AppearanceHelp,
    Section3d,
    SeaOpacity,
    SeaColor,
    View3dHelp,
    SectionTa,
    NotSet,
    LoadingArchives,
    FolderNotFound,
    FolderNoArchives,
    ArchivesLoaded,
    ArchivesBeside,
    ChooseFolder,
    Clear,
    GameFolderHelp,
    SectionPreviews,
    PreviewSize,
    PreviewHelp,
    SectionHeight,
    GridSize,
    HeightHelp,
    SectionTilesets,
    SidebarFont,
    SidebarFontHelp,
    SectionAutosave,
    AutosaveEvery,
    BackupCopies,
    AutosaveHelp,
    AboutTitle,
    AboutBody,
    Ok,
    UnsavedTitle,
    UnsavedMessage,
    UnsavedDetail,
    ExitWithoutSaving,
    SaveAndExit,
    ErrorTitle,
    ErrorLead,
    TileLimitTitle,
    NewMapHelp,
    WidthTiles,
    HeightTiles,
    NewMapSize,
    NewMapBadSize,
    NewMapGameLimit,
    Create,
    TabMap,
    TabWorld,
    TabSpecial,
    TabRules,
    Edited,
    SectionsIndexed,
    Copy,
    HeightOffset,
    HeightOffsetLabel,
    TipHeightOffset,
    Count
};

struct Language {
    const char* id;
    const char* nativeName;
};

// The locales the editor ships, in picker order. English is first.
int languageCount();
const Language& languageAt(int index);
// -1 when id is not one of the shipped locales.
int languageIndex(std::string_view id);
bool isKnownLanguage(std::string_view id);

// The locale used by text(). Starts as English until a saved choice is applied.
const char* activeLanguage();
void setActiveLanguage(std::string_view id);

// Translation for the active locale, or English when that locale has no line.
const char* text(Text id);
const char* textIn(std::string_view languageId, Text id);

// Replaces {0}, {1}, ... in order. A language may put the tokens in any order.
std::string formatText(Text id, std::initializer_list<std::string_view> args = {});

// "Translated###stable" so a window keeps its saved position when the label changes.
std::string withId(Text id, const char* stableId);

// Every shipped translation and native name, so the font atlas can cover them.
const std::string& translationCorpus();

}  // namespace galaxy

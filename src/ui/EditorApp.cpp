#include "ui/EditorApp.hpp"
#include "ui/ExampleScripts.hpp"

#include "core/BlendColours.hpp"
#include "core/AutoMetal.hpp"
#include "core/PieceScaler.hpp"
#include "core/BrushStrokes.hpp"
#include "core/DocumentNames.hpp"
#include "core/EditorSettings.hpp"
#include "core/I18n.hpp"
#include "core/SlopeBrush.hpp"
#include "core/MetalBridge.hpp"
#include "core/FavoritesArchive.hpp"
#include "core/Version.hpp"
#include "core/TouchpadPan.hpp"
#include "format/BmpImage.hpp"
#include "format/GafImage.hpp"
#include "format/TaPalette.hpp"
#include "platform/FileDialog.hpp"
#include "platform/SystemAppearance.hpp"
#include "ui/Theme.hpp"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
// The Windows header stops at OpenGL 1.1, which predates this constant.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#endif

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <exception>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace galaxy {

// The part of a random map that runs on another thread: the script, then the tiles and
// stamps that draw it. The map itself is only changed back on the main thread.
struct RandomMapJob {
    std::uint64_t serial = 0;
    const RandomMapTiles* tiles = nullptr;
    std::vector<RandomMapStampBrush> stampBrushes;
    RmsScript script;
    std::vector<int> levels;
    std::optional<BrushCanvas> canvas;
    RmsTerrainNames names;
    RmsObjectSettings objects;
    std::mt19937 rng;
    int players = 2;
    int tileWidth = 0;
    int tileHeight = 0;

    RandomMapOutcome outcome;
    RandomMapStampPlan plan;
    std::vector<RmsMessage> stampNotes;
    std::size_t groundStamps = 0;
    std::string failure;
    bool cancelled = false;

    std::atomic<int> step{0};
    std::atomic<double> fraction{0.0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    // Set once the work is back, so the window shows the last step for a frame first.
    bool building = false;

    void run() {
        try {
            RandomMapOptions options;
            options.players = players;
            options.objects = &objects;
            options.cancel = &cancel;
            options.progress = [this](RandomMapStage stage, double done) {
                step = static_cast<int>(RandomMapStep::Lands) + static_cast<int>(stage);
                fraction = done;
            };
            outcome = runRandomMapScript(script, *canvas, names, options, rng);
            step = static_cast<int>(RandomMapStep::Tiles);
            fraction = 0.0;
            canvas->repair([&](const std::array<int, 4>& corners) { return !tiles->pieces(canvas->graph(), levels, corners).empty(); });
            fraction = 0.3;
            if (cancel) {
                throw RandomMapCancelled();
            }
            plan = planRandomMapStamps(*tiles, levels, *canvas, rng);
            if (cancel) {
                throw RandomMapCancelled();
            }
            // Hills, rough patches and islands go over the painted ground.
            step = static_cast<int>(RandomMapStep::Stamps);
            fraction = 0.0;
            groundStamps = plan.stamps.size();
            for (RandomMapStamp& stamp : planStampBrushes(script, outcome, stampBrushes, *canvas, names, tileWidth, tileHeight, rng, stampNotes)) {
                plan.stamps.push_back(std::move(stamp));
            }
            fraction = 1.0;
        } catch (const RandomMapCancelled&) {
            cancelled = true;
        } catch (const std::exception& ex) {
            failure = ex.what();
        }
        done = true;
    }
};

namespace {

// Thrown inside the picture catalogue worker when another tileset is chosen. It is not a std::exception, so the
// catalogue's own catch blocks let it through.
struct CatalogueCancelled {};

// True on the picture catalogue worker, whose own calls to brushGround pass the guard that keeps the main thread out.
thread_local bool tCatalogueWorker = false;

// Which GLFW backend gives this run its scroll events; on Linux this matches how GLFW 3.4 picks one.
ScrollPlatform currentScrollPlatform() {
#if defined(__APPLE__)
    return ScrollPlatform::MacOS;
#elif defined(_WIN32)
    return ScrollPlatform::Windows;
#else
    static const ScrollPlatform platform = [] {
        const char* session = std::getenv("XDG_SESSION_TYPE");
        const bool wayland = session != nullptr && std::string_view(session) == "wayland" && std::getenv("WAYLAND_DISPLAY") != nullptr;
        return wayland ? ScrollPlatform::Wayland : ScrollPlatform::X11;
    }();
    return platform;
#endif
}

ImU32 featureColor(std::string_view name) {
    if (name.find("Tree") != std::string_view::npos) {
        return IM_COL32(40, 130, 45, 230);
    }
    if (name.find("Rock") != std::string_view::npos) {
        return IM_COL32(150, 145, 130, 230);
    }
    return IM_COL32(220, 180, 40, 230);
}

ImU32 startColor(int player) {
    static const ImU32 colors[] = {
        IM_COL32(70, 130, 255, 240),
        IM_COL32(230, 70, 70, 240),
        IM_COL32(80, 200, 90, 240),
        IM_COL32(230, 190, 40, 240),
        IM_COL32(180, 80, 220, 240),
        IM_COL32(255, 140, 40, 240),
        IM_COL32(40, 200, 210, 240),
        IM_COL32(230, 90, 160, 240),
        IM_COL32(235, 235, 235, 240),
        IM_COL32(160, 110, 60, 240),
    };
    const int index = std::max(player - 1, 0);
    return colors[static_cast<std::size_t>(index) % 10];
}

struct StartPin {
    ImVec2 tip;
    ImVec2 head;
    float radius = 0.0f;
};

StartPin startPinGeometry(ImVec2 tip, float zoom) {
    const float height = std::clamp(28.0f * std::min(std::max(zoom, 0.35f), 1.4f), 18.0f, 40.0f);
    const float radius = height * 0.34f;
    return StartPin{tip, ImVec2(tip.x, tip.y - height + radius), radius};
}

bool startPinHit(const StartPin& pin, ImVec2 screen) {
    const float dx = screen.x - pin.head.x;
    const float dy = screen.y - pin.head.y;
    const float reach = pin.radius + 4.0f;
    if (dx * dx + dy * dy <= reach * reach) {
        return true;
    }
    const float tx = screen.x - pin.tip.x;
    const float ty = screen.y - pin.tip.y;
    return tx * tx + ty * ty <= pin.radius * pin.radius;
}

void drawStartPin(ImDrawList* draw, ImVec2 tip, float zoom, ImU32 color, int player, bool armed) {
    const StartPin pin = startPinGeometry(tip, zoom);
    draw->AddTriangleFilled(
        ImVec2(pin.head.x - pin.radius * 0.72f, pin.head.y + pin.radius * 0.35f),
        ImVec2(pin.head.x + pin.radius * 0.72f, pin.head.y + pin.radius * 0.35f),
        pin.tip,
        color);
    draw->AddCircleFilled(pin.head, pin.radius, color);
    draw->AddCircle(pin.head, pin.radius, armed ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 220), 0.0f, armed ? 2.5f : 1.5f);
    const std::string label = std::to_string(player);
    const ImVec2 text = ImGui::CalcTextSize(label.c_str());
    const ImVec2 at(pin.head.x - text.x * 0.5f, pin.head.y - text.y * 0.5f);
    draw->AddText(ImVec2(at.x + 1.0f, at.y + 1.0f), IM_COL32(0, 0, 0, 220), label.c_str());
    draw->AddText(at, IM_COL32(255, 255, 255, 255), label.c_str());
}

ImTextureID textureId(GLuint texture) {
    return static_cast<ImTextureID>(static_cast<intptr_t>(texture));
}

GLuint uploadTexture(const std::uint8_t* rgba, int width, int height, bool nearest) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    const GLint filter = nearest ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

void destroyTexture(GLuint& texture) {
    if (texture != 0) {
        glDeleteTextures(1, &texture);
        texture = 0;
    }
}

std::string selectionSummary(const TileSelection& selection) {
    if (selection.solid()) {
        return std::to_string(selection.width) + " x " + std::to_string(selection.height);
    }
    return std::to_string(selection.count()) + " tiles";
}

std::string categoryKey(std::size_t archiveIndex, const std::string& world, const std::string& category) {
    return std::to_string(archiveIndex) + '\n' + world + '\n' + category;
}

std::string lowerText(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return out;
}

int featureLift(const LoadedMap& map, int cellX, int cellY) {
    auto at = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= map.heightWidth() || y >= map.heightHeight()) {
            return 0;
        }
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.heightWidth()) + static_cast<std::size_t>(x);
        if (index >= map.heights.size()) {
            return 0;
        }
        return map.heights[index];
    };
    return (at(cellX, cellY) + at(cellX + 1, cellY) + at(cellX, cellY + 1) + at(cellX + 1, cellY + 1)) / 8;
}

const HpiFile* findAnimFile(const HpiArchive& archive, const std::string& animFile) {
    std::string stem = animFile;
    if (stem.size() > 4 && lowerText(stem.substr(stem.size() - 4)) == ".gaf") {
        stem.resize(stem.size() - 4);
    }
    if (const HpiFile* direct = archive.find("anims/" + stem + ".gaf")) {
        return direct;
    }
    const std::string suffix = "/" + lowerText(stem) + ".gaf";
    for (const HpiFile& file : archive.files()) {
        std::string path = lowerText(file.path);
        std::replace(path.begin(), path.end(), '\\', '/');
        if (path.size() < suffix.size() || path.compare(path.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        if (path.find("anims/") != std::string::npos) {
            return &file;
        }
    }
    return nullptr;
}

std::optional<GafImage> imageFromArchive(const TilesetArchive& archive, const FeatureInfo& info) {
    if (archive.contents == nullptr || info.animFile.empty() || info.sequence.empty()) {
        return std::nullopt;
    }
    const HpiFile* file = findAnimFile(archive.archive(), info.animFile);
    if (file == nullptr) {
        return std::nullopt;
    }
    try {
        const std::vector<std::uint8_t> bytes = archive.archive().extract(*file);
        return readGafSequence(bytes.data(), bytes.size(), info.sequence);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool textHas(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    return lowerText(haystack).find(lowerText(needle)) != std::string::npos;
}

// needleFolded must already be lower case. Compares without building any strings.
bool containsFolded(std::string_view haystack, std::string_view needleFolded) {
    if (needleFolded.empty()) {
        return true;
    }
    if (needleFolded.size() > haystack.size()) {
        return false;
    }
    const auto fold = [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    };
    return std::search(haystack.begin(), haystack.end(), needleFolded.begin(), needleFolded.end(),
               [&](char left, char right) { return fold(left) == right; })
        != haystack.end();
}

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::string archiveLeaf(std::string_view path) {
    const auto slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

bool nameLess(const std::string& a, const std::string& b) {
    const std::size_t count = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < count; ++i) {
        const int ca = std::tolower(static_cast<unsigned char>(a[i]));
        const int cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) {
            return ca < cb;
        }
    }
    return a.size() < b.size();
}

// The random-map tileset is a favorites archive for now. Matching the name, rather
// than the GE-Favorites- prefix, keeps working after that prefix is removed.
bool isRandomMapTileset(const std::filesystem::path& path) {
    return lowerText(path.filename().string()).find("randommapgeneratortiles") != std::string::npos;
}

bool isBaseBrushTile(std::string_view world, std::string_view name) {
    return lowerText(world) == "basetiles" && lowerText(name).starts_with("base");
}

// Pieces for one tileset live in a category such as GreenWorld or Lava. River
// pieces are filed under GreenLand and still belong to the GreenWorld brush.
// The folder of Lava's shore pieces from Base-76 up to Base-86.
const std::string kLavaShoreFolder = "Lava-76";
// SnowyEvergreen's cliffs from Base-189 up to Base-255, apart from its Base-85 to Base-189 ravines.
const std::string kSnowPeakFolder = "SnowyEvergreen-189";
// SnowyEvergreen's snow, Base-65 shallows and deep water join through the pieces in SnowyEvergreen-65, apart from
// its frozen water's shallow coasts, which share their names.
const std::string kSnowShallowFolder = "SnowyEvergreen-65";
// SnowyEvergreen's sea cliffs from Base-0 up to Base-189, and its pieces with water, snow and Base-189 together.
const std::string kSnowSeaCliffFolder = "SnowyEvergreen-0";
// Its cliffs from the Base-75 frozen water up to Base-189, and its pieces with frozen water, snow and Base-189.
const std::string kSnowFrozenCliffFolder = "SnowyEvergreen-75";
// Metal's sea cliffs from Metal2's dark Base-0 water straight up to Metal's Base-139.
const std::string kMetalSeaCliffFolder = "Metal-0";
// Evergreen's cliffs from Base-85-1 grass up to the Base-190-2 stone plateau, the only Evergreen cliffs with ramps.
const std::string kEvergreenRampCliffFolder = "Evergreen2-Evergreen";
// The Evergreen brush's button for those ramps.
constexpr const char* kEvergreenRampButton = "Slope 85-1 to 190-2";
// Evergreen's cliffs from Base-190-1 up to Base-255-1, and the button for their ramps.
const std::string kEvergreenPeakCliffFolder = "Evergreen-190";
constexpr const char* kEvergreenPeakRampButton = "Slope 190-1 to 255-1";
// Evergreen's cliffs from Base-85-2 up to Base-190-2, and the button for their ramps.
const std::string kEvergreen2RampCliffFolder = "Evergreen2";
constexpr const char* kEvergreen2RampButton = "Slope 85-2 to 190-2";
// Evergreen's cliffs from Base-190-2 up to Base-255-2, and the button for their ramps.
const std::string kEvergreen2PeakCliffFolder = "Evergreen2-190";
constexpr const char* kEvergreen2PeakRampButton = "Slope 190-2 to 255-2";
// Evergreen's cliffs from Base-85-1 up to Base-190-1, and the button for their ramps.
const std::string kEvergreenPlateauCliffFolder = "Evergreen";
constexpr const char* kEvergreenPlateauRampButton = "Slope 85-1 to 190-1";
// The coasts from Lava's Base-41 up to Metal's Base-85, from Lava-Metal.hpi's Coast_ pieces. They are named like
// water coasts, Coast-Water-T and Coast-Land-BR, whichever brush paints them.
const std::string kLavaMetalFolder = "Lava-Metal";
// WaterWorld's pieces between its Base-0 water and its Base-1 sea floor. They share their ShallowDeep names with the
// reef's, so they have a folder of their own.
const std::string kWaterWorldFloorFolder = "WaterWorld-1";

bool brushCategoryMatches(std::string_view category, std::string_view world) {
    const std::string left = lowerText(category);
    const std::string right = lowerText(world);
    if (left == right) {
        return true;
    }
    return right == "greenworld" && left == "greenland";
}

// Evergreen's two grasses are two lands, Evergreen and Evergreen2, painted from one Tile Brush tileset.
bool isEvergreenWorld(std::string_view world) {
    const std::string lower = lowerText(world);
    return lower == "evergreen" || lower == "evergreen2";
}

// Metal's two waters are two lands too, Metal and Metal2, painted from one Tile Brush tileset: Metal2 holds the second
// water, Base-0-2.
bool isMetalWorld(std::string_view world) {
    const std::string lower = lowerText(world);
    return lower == "metal" || lower == "metal2";
}

// Folders whose bases one Tile Brush tileset shows together: Evergreen's two grasses, and Metal's two waters.
bool brushSiblingWorlds(std::string_view a, std::string_view b) {
    return (isEvergreenWorld(a) && isEvergreenWorld(b)) || (isMetalWorld(a) && isMetalWorld(b));
}

// The tileset a brush folder belongs to, as the Tileset list names it.
std::string brushTilesetOf(std::string_view world) {
    return isEvergreenWorld(world) ? std::string("Evergreen") : isMetalWorld(world) ? std::string("Metal") : std::string(world);
}

bool isBrushStrokeWorld(std::string_view world) {
    return lowerText(world) == "basetilebrushstrokes";
}

// Cliff, coast and shallow coast sections are the pieces that join one base to the next. The
// -8x8 folders, such as CoastTiles-8x8, hold the same kinds of piece at 8 x 8 tiles.
bool isTransitionWorld(std::string_view world) {
    std::string kind;
    brushPieceFolderTiles(world, kind);
    const std::string name = lowerText(kind);
    return name == "clifftiles" || name == "coasttiles" || name == "shallowcoasttiles" || name == "shallowdeeptiles" || name == "coastdsl" || name == "coastalcliff" || name == "blendtiles";
}

// River, ridge and slope sections. Slopes are the walkable cliff pieces.
bool isRiverWorld(std::string_view world) {
    const std::string name = lowerText(world);
    return name == "rivers" || name == "ridges" || name == "slopes";
}

std::uint64_t hashBrushTile(const std::uint8_t* bytes) {
    return brushTileHash(bytes);
}

// 0 is grass, 1 is lava, and 2 is acid. The three-way tiles share one height
// band, so the picture is what tells the lands apart.
int triTileLand(const std::uint8_t* bytes) {
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    if (score >= 12 * 1024) {
        return 0;
    }
    if (score <= -8 * 1024) {
        return 1;
    }
    return 2;
}

// 0 is ice, 1 is the third land, and 2 is lava, for the Ice and Lava three-way tiles: whichever
// colour covers most of the tile. third is "grass", "archi", or "mars".
int triIceTileLand(const std::uint8_t* bytes, std::string_view third) {
    int ice = 0;
    int other = 0;
    int lava = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const bool green = g > r + 15 && g > b + 10;
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        ice += !green && b + 3 >= r;
        lava += !green && r > b + 8 && lum <= 140 && r <= g + 25;
        if (third == "grass") {
            other += green;
        } else if (third == "archi") {
            other += !green && r > b + 15 && lum > 140;
        } else {
            other += !green && r > g + 25 && lum <= 140;
        }
    }
    if (ice >= other && ice >= lava) {
        return 0;
    }
    return other >= lava ? 1 : 2;
}

// 0 is Lava, 1 is the third land, and 2 is Lush grass, for the Lava and Lush three-way tiles:
// whichever colour covers most of the tile. The third land is GreenWorld grass, which is
// yellow-green with red in it and about 40% brown specks, or Mars ground, which is about 70% red;
// either count is weighted up for its specks.
int triLushTileLand(const std::uint8_t* bytes, bool mars) {
    int lava = 0;
    int grass = 0;
    int lush = 0;
    int red = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const bool greenish = g > r + 10;
        grass += greenish && r > b + 5;
        lush += g > r + 25 && b + 5 >= r;
        lava += !greenish && r > b + 8 && r <= g + 25 && (30 * r + 59 * g + 11 * b) / 100 <= 140;
        red += r > g + 25 && (30 * r + 59 * g + 11 * b) / 100 <= 140;
    }
    const double weightedGrass = mars ? red * 1.4 : grass * 1.7;
    if (lush >= weightedGrass && lush >= lava) {
        return 2;
    }
    return weightedGrass >= lava ? 1 : 0;
}

// 0 is Lava, 1 is Archipelago sand, and 2 is Crystal, for the Archipelago and Crystal blend and the
// Lava, Crystal and Archipelago three-way tiles: whichever colour covers most of the tile. Crystal
// is purple, sand is pale, and lava dirt is brown; Crystal is about 64% purple, so it is weighted up.
int crystalArchiTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int sand = 0;
    int crystal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool purple = b > g + 5 && lum <= 150;
        crystal += purple;
        sand += r > b + 15 && lum > 140;
        lava += !purple && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedCrystal = crystal * 1.5;
    if (weightedCrystal >= sand && weightedCrystal >= lava) {
        return 2;
    }
    return sand >= lava ? 1 : 0;
}

// 0 is GreenWorld grass, 1 is ice, and 2 is Lush grass, for the GreenWorld, Ice and Lush three-way
// tiles: whichever colour covers most of the tile. GreenWorld grass is yellow-green with red in it and
// about 40% brown specks, so its count is weighted up; ice is white and blue; Lush is blue-green.
// 0 is GreenWorld grass, 1 is ice, and 2 is Mars, for the GreenWorld, Ice and Mars three-way tiles.
// The Archipelago, GreenWorld and Mars tiles use it too, with 1 for the sand, which is about as bright
// as ice (about 222).
// The three are the same height, so the tile's average colour decides: ice is far brighter than either
// ground (about 228 against 75), and Mars has about 50 more red than green where grass has about 38
// less.
int triGreenIceMarsTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    int redOverGreen = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        redOverGreen += r - g;
    }
    if (brightness >= 150L * 100 * 1024) {
        return 1;
    }
    return redOverGreen >= 6 * 1024 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Metal, and 2 is Lush, for the GreenWorld, Metal and Lush three-way tiles:
// whichever land's typical colour the tile's average is nearest, by red over green and blue over red.
// Grass is about (-38, -28), Lush (-79, 38), and Metal plating, a warm grey, (8, -11).
int triGreenMetalLushTileLand(const std::uint8_t* bytes) {
    long redOverGreen = 0;
    long blueOverRed = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        redOverGreen += r - g;
        blueOverRed += b - r;
    }
    const double x = redOverGreen / 1024.0;
    const double y = blueOverRed / 1024.0;
    const double centres[3][2] = {{-38, -28}, {8, -11}, {-79, 38}};
    int best = 0;
    double bestDistance = 1e18;
    for (int land = 0; land < 3; ++land) {
        const double dx = x - centres[land][0];
        const double dy = y - centres[land][1];
        if (dx * dx + dy * dy < bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = land;
        }
    }
    return best;
}

// 0 is GreenWorld grass, 1 is Archipelago sand, and 2 is Lush, for the Archipelago, GreenWorld and Lush
// three-way tiles. Sand is far brighter than either grass (about 222 against 75 and 57); of the two
// grasses, Lush has about 38 more blue than red and GreenWorld about 28 less. The Desert, GreenWorld and
// Lush tiles use it with a lower cut for the sand, since Desert sand is about 150 bright.
int triArchiGreenLushTileLand(const std::uint8_t* bytes, int sandBrightness = 140) {
    long brightness = 0;
    int blueOverRed = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        blueOverRed += b - r;
    }
    if (brightness >= static_cast<long>(sandBrightness) * 100 * 1024) {
        return 1;
    }
    return blueOverRed >= 5 * 1024 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Crystal, and 2 is Lush, for the Crystal, GreenWorld and Lush three-way
// tiles: whichever land's typical colour the tile's average is nearest, by red over green and blue over
// green. Grass is about (-38, -66), Lush (-79, -41), and purple Crystal (21, 40).
int triGreenCrystalLushTileLand(const std::uint8_t* bytes) {
    long redOverGreen = 0;
    long blueOverGreen = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        redOverGreen += r - g;
        blueOverGreen += b - g;
    }
    const double x = redOverGreen / 1024.0;
    const double y = blueOverGreen / 1024.0;
    const double centres[3][2] = {{-38, -66}, {21, 40}, {-79, -41}};
    int best = 0;
    double bestDistance = 1e18;
    for (int land = 0; land < 3; ++land) {
        const double dx = x - centres[land][0];
        const double dy = y - centres[land][1];
        if (dx * dx + dy * dy < bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = land;
        }
    }
    return best;
}

// 0 is GreenWorld grass, 1 is Archipelago sand, and 2 is Desert sand, for the Archipelago, Desert and
// GreenWorld three-way tiles: whichever land's typical colour the tile's average is nearest, by
// brightness and red over green. Grass is about (74, -38), Desert (150, 25), and the paler, greyer
// Archipelago sand (222, 7).
int triArchiDesertGreenTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    long redOverGreen = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        redOverGreen += r - g;
    }
    const double x = brightness / (100.0 * 1024.0);
    const double y = redOverGreen / 1024.0;
    const double centres[3][2] = {{74, -38}, {222, 7}, {150, 25}};
    int best = 0;
    double bestDistance = 1e18;
    for (int land = 0; land < 3; ++land) {
        const double dx = x - centres[land][0];
        const double dy = y - centres[land][1];
        if (dx * dx + dy * dy < bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = land;
        }
    }
    return best;
}

// 0 is GreenWorld grass, 1 is Archipelago sand, and 2 is Acid, for the Acid, Archipelago and GreenWorld
// three-way tiles. Sand is far brighter than either (about 222 against 75 and 80). Acid ground is olive,
// with about 5 less red than green, where GreenWorld grass has about 38 less.
int triAcidArchiGreenTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    int redOverGreen = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        redOverGreen += r - g;
    }
    if (brightness >= 150L * 100 * 1024) {
        return 1;
    }
    return redOverGreen >= -24 * 1024 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Archipelago sand, and 2 is Crystal, for the Archipelago, Crystal and
// GreenWorld three-way tiles. Sand is far brighter than either (about 222 against 75 and 78). Purple
// Crystal ground has about 40 more blue than green, where GreenWorld grass has about 66 less.
int triArchiCrystalGreenTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    int blueOverGreen = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        blueOverGreen += b - g;
    }
    if (brightness >= 150L * 100 * 1024) {
        return 1;
    }
    return blueOverGreen >= -13 * 1024 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Archipelago sand, and 2 is Metal, for the Archipelago, GreenWorld and
// Metal three-way tiles. Sand is far brighter than either (about 222; Metal tiles reach about 104).
// Metal plating is a warm grey, with 2 to 25 more red than green, where GreenWorld grass has about 38
// less.
int triArchiGreenMetalTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    int redOverGreen = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        redOverGreen += r - g;
    }
    if (brightness >= 150L * 100 * 1024) {
        return 1;
    }
    return redOverGreen >= -18 * 1024 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Archipelago sand, and 2 is ice, for the Archipelago, GreenWorld and Ice
// three-way tiles. Sand and ice are both far brighter than grass (about 222 and 228 against 75), and
// blue tells them apart: ice has about 37 more blue than red, and sand about 30 less.
int triArchiGreenIceTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    int blueOverRed = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        brightness += 30 * r + 59 * g + 11 * b;
        blueOverRed += b - r;
    }
    if (brightness < 150L * 100 * 1024) {
        return 0;
    }
    return blueOverRed >= 4 * 1024 ? 2 : 1;
}

// 0 is GreenWorld grass, 1 is Mars, and 2 is Metal, for the GreenWorld, Mars and Metal three-way tiles.
// The three are the same height and differ in red over green: Mars about 50, Metal plating 2 to 25,
// and GreenWorld grass about -38. The cuts match marsMetalColourIsMars and the grass reading beside Metal.
int triGreenMarsMetalTileLand(const std::uint8_t* bytes) {
    const int red = redOverGreen(bytes);
    if (red >= 36) {
        return 1;
    }
    return red >= -18 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is ice, and 2 is Metal, for the GreenWorld, Ice and Metal three-way tiles.
// Ice is far brighter than either ground (about 228; Metal tiles reach about 104). Metal plating is a
// warm grey with 2 to 25 more red than green, where GreenWorld grass has about 38 less.
int triGreenIceMetalTileLand(const std::uint8_t* bytes) {
    long brightness = 0;
    for (int i = 0; i < 1024; ++i) {
        brightness += 30 * kTaPalette[bytes[i]][0] + 59 * kTaPalette[bytes[i]][1] + 11 * kTaPalette[bytes[i]][2];
    }
    if (brightness >= 150L * 100 * 1024) {
        return 1;
    }
    return redOverGreen(bytes) >= -18 ? 2 : 0;
}

// A tile's average brightness and colour differences, for telling lands apart by colour.
struct TileColourMeans {
    double brightness = 0;
    double redOverGreen = 0;
    double blueOverGreen = 0;
    double blueOverRed = 0;
};

TileColourMeans tileColourMeans(const std::uint8_t* bytes) {
    TileColourMeans means;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        means.brightness += (30 * r + 59 * g + 11 * b) / 100.0;
        means.redOverGreen += r - g;
        means.blueOverGreen += b - g;
        means.blueOverRed += b - r;
    }
    means.brightness /= 1024;
    means.redOverGreen /= 1024;
    means.blueOverGreen /= 1024;
    means.blueOverRed /= 1024;
    return means;
}

// Which of three lands' typical colours (x, y) is nearest.
int nearestLand(double x, double y, const double (&centres)[3][2]) {
    int best = 0;
    double bestDistance = 1e18;
    for (int land = 0; land < 3; ++land) {
        const double dx = x - centres[land][0];
        const double dy = y - centres[land][1];
        if (dx * dx + dy * dy < bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = land;
        }
    }
    return best;
}

// 0 is GreenWorld grass, 1 is Desert sand, and 2 is ice. Ice is the brightest (about 228) and the only
// one with more blue than red; Desert sand is about 150 bright and grass about 75.
int triDesertGreenIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150 && means.blueOverRed >= 0) {
        return 2;
    }
    return means.brightness >= 110 ? 1 : 0;
}

// 0 is GreenWorld grass, 1 is Desert sand, and 2 is Mars. Desert sand is about 150 bright, against
// about 75 for both grounds; Mars has about 50 more red than green, and grass about 38 less.
int triDesertGreenMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 110) {
        return 1;
    }
    return means.redOverGreen >= 6 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Desert sand, and 2 is Metal. Desert sand is at least 146 bright, and
// Metal plating at most about 104; Metal has 2 to 25 more red than green, and grass about 38 less.
int triDesertGreenMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 125) {
        return 1;
    }
    return means.redOverGreen >= -18 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is ice, and 2 is Crystal. Ice is far brighter than either ground (about
// 228); purple Crystal has about 40 more blue than green, where grass has about 66 less.
int triCrystalGreenIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.blueOverGreen >= -13 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Mars, and 2 is Crystal: the nearest typical colour by red over green and
// blue over green. Grass is about (-38, -66), Mars (50, -15), and Crystal (21, 40).
int triCrystalGreenMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{-38, -66}, {50, -15}, {21, 40}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is GreenWorld grass, 1 is Metal, and 2 is Crystal: the nearest typical colour by red over green and
// blue over green. Grass is about (-38, -66), Metal plating (8, -4), and Crystal (21, 40).
int triCrystalGreenMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{-38, -66}, {8, -4}, {21, 40}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is GreenWorld grass, 1 is Desert sand, and 2 is Crystal. Desert sand is about 150 bright, against
// at most about 86 for either ground; Crystal has about 40 more blue than green, and grass about 66 less.
int triCrystalDesertGreenTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 115) {
        return 1;
    }
    return means.blueOverGreen >= -13 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Acid, and 2 is Lush: the nearest typical colour by red over green and blue
// over red. Grass is about (-38, -28), olive Acid (-3, -18), and blue-green Lush (-79, 38).
int triAcidGreenLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{-38, -28}, {-3, -18}, {-79, 38}};
    return nearestLand(means.redOverGreen, means.blueOverRed, centres);
}

// 0 is GreenWorld grass, 1 is ice, and 2 is Acid. Ice is far brighter than either ground (about 228
// against 75 and at most about 94); olive Acid has about 5 less red than green, and grass about 38 less.
int triAcidGreenIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= -24 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Mars, and 2 is Acid. The three are the same height and differ in red over
// green: Mars about 50, olive Acid about -5, and grass about -38. The Mars cut matches acidMarsColourIsMars.
int triAcidGreenMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.redOverGreen >= 24) {
        return 1;
    }
    return means.redOverGreen >= -24 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Metal, and 2 is Acid, all the same height. Twice the green over red plus
// the green over blue, the score acidMetalTileIsAcid uses, puts the three far apart: grass tiles score
// 140 to 146, olive Acid 18 to 38, and warm-grey Metal plating 4 or less.
int triAcidGreenMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    const double score = -2 * means.redOverGreen - means.blueOverGreen;
    if (score >= 90) {
        return 0;
    }
    return score >= 11 ? 2 : 1;
}

// 0 is GreenWorld grass, 1 is Crystal, and 2 is Acid, all the same height. Blue over green puts them in
// order: purple Crystal has at least 35 more blue than green, olive Acid 9 to 30 less, and grass about
// 66 less.
int triAcidCrystalGreenTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.blueOverGreen >= 13) {
        return 1;
    }
    return means.blueOverGreen >= -48 ? 2 : 0;
}

// 0 is GreenWorld grass, 1 is Desert sand, and 2 is Acid. Desert sand is at least 146 bright, against at
// most about 94 for Acid and 75 for grass; olive Acid has about 5 less red than green, and grass about
// 38 less.
int triAcidDesertGreenTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 120) {
        return 1;
    }
    return means.redOverGreen >= -24 ? 2 : 0;
}

// 0 is Lush, 1 is Archipelago sand, and 2 is Desert sand, for the Archipelago, Desert and Lush three-way
// tiles: the nearest typical colour by brightness and red over green. Lush is about (57, -79), the pale,
// greyer Archipelago sand (222, 7), and Desert sand (150, 25).
int triArchiDesertLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{57, -79}, {222, 7}, {150, 25}};
    return nearestLand(means.brightness, means.redOverGreen, centres);
}

// 0 is Lush, 1 is Archipelago sand, and 2 is ice. Lush is dark (about 57 bright) and both others far
// brighter (about 222 and 228); blue tells them apart: ice has about 37 more blue than red, and sand
// about 30 less.
int triArchiIceLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness < 140) {
        return 0;
    }
    return means.blueOverRed >= 4 ? 2 : 1;
}

// 0 is Lush, 1 is Archipelago sand, and 2 is Mars. Sand is far brighter than either (about 222 against
// 57 and 75); Mars has about 50 more red than green, and blue-green Lush about 79 less.
int triArchiLushMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 140) {
        return 1;
    }
    return means.redOverGreen >= -15 ? 2 : 0;
}

// 0 is Lush, 1 is Archipelago sand, and 2 is Metal. Sand is far brighter than either (about 222; Metal
// tiles reach about 104); warm-grey Metal has 2 to 25 more red than green, and blue-green Lush about 79
// less.
int triArchiLushMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= -38 ? 2 : 0;
}

// 0 is ice, 1 is Archipelago sand, and 2 is Metal. Metal plating is dark (at most about 104 bright) and
// both others far brighter (about 228 and 222); blue tells those apart: ice has about 37 more blue than
// red, and sand about 30 less.
int triArchiIceMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness < 150) {
        return 2;
    }
    return means.blueOverRed >= 4 ? 0 : 1;
}

// 0 is Mars, 1 is Archipelago sand, and 2 is Metal. Sand is far brighter than either (about 222; Metal tiles
// reach about 104); Mars has at least 47 more red than green and Metal plating at most 25.
int triArchiMarsMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= 36 ? 0 : 2;
}

// 0 is Crystal, 1 is Archipelago sand, and 2 is Desert sand, by brightness alone: Archipelago sand is at
// least 221 bright, Desert sand 146 to 153, and Crystal ground at most about 86.
int triArchiCrystalDesertTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 186) {
        return 1;
    }
    return means.brightness >= 116 ? 2 : 0;
}

// 0 is Crystal, 1 is Archipelago sand, and 2 is ice. Crystal is dark (at most about 86 bright) and the
// others far brighter; ice has about 37 more blue than red, and sand about 30 less.
int triArchiCrystalIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness < 150) {
        return 0;
    }
    return means.blueOverRed >= 4 ? 2 : 1;
}

// 0 is Lush, 1 is Archipelago sand, and 2 is Crystal. Sand is far brighter than either; purple Crystal
// has about 40 more blue than green, and blue-green Lush about 41 less.
int triArchiCrystalLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.blueOverGreen >= 0 ? 2 : 0;
}

// 0 is Crystal, 1 is Archipelago sand, and 2 is Mars. Sand is far brighter than either; purple Crystal
// has about 40 more blue than green, and Mars about 15 less.
int triArchiCrystalMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.blueOverGreen >= 12 ? 0 : 2;
}

// 0 is Crystal, 1 is Archipelago sand, and 2 is Metal. Sand is far brighter than either; purple Crystal
// has at least 35 more blue than green, and Metal plating at most about 2.
int triArchiCrystalMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.blueOverGreen >= 18 ? 0 : 2;
}

// 0 is Desert sand, 1 is Archipelago sand, and 2 is ice. Desert sand is at most about 153 bright and the
// others at least 221; ice has about 37 more blue than red, and Archipelago sand about 30 less.
int triArchiDesertIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness < 186) {
        return 0;
    }
    return means.blueOverRed >= 4 ? 2 : 1;
}

// 0 is Desert sand, 1 is Archipelago sand, and 2 is Mars, by brightness alone: Archipelago sand is at
// least 221 bright, Desert sand 146 to 153, and Mars ground at most about 77.
int triArchiDesertMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 186) {
        return 1;
    }
    return means.brightness >= 112 ? 0 : 2;
}

// 0 is Desert sand, 1 is Archipelago sand, and 2 is Metal, by brightness alone: Archipelago sand is at
// least 221 bright, Desert sand 146 to 153, and Metal plating at most about 104.
int triArchiDesertMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 186) {
        return 1;
    }
    return means.brightness >= 125 ? 0 : 2;
}

// 0 is Desert sand, 1 is Crystal, and 2 is ice, by brightness alone: ice is at least 227 bright, Desert
// sand 146 to 153, and Crystal ground at most about 86.
int triCrystalDesertIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 190) {
        return 2;
    }
    return means.brightness >= 116 ? 0 : 1;
}

// 0 is Crystal, 1 is Archipelago sand, and 2 is Acid. Sand is far brighter than either; purple Crystal has
// at least 35 more blue than green, and olive Acid 9 to 30 less.
int triAcidArchiCrystalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.blueOverGreen >= 13 ? 0 : 2;
}

// 0 is Desert sand, 1 is Archipelago sand, and 2 is Acid, by brightness alone: Archipelago sand is at
// least 221 bright, Desert sand 146 to 153, and Acid ground at most about 94.
int triAcidArchiDesertTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 186) {
        return 1;
    }
    return means.brightness >= 120 ? 0 : 2;
}

// 0 is ice, 1 is Archipelago sand, and 2 is Acid. Acid is dark (at most about 94 bright) and the others
// far brighter; ice has about 37 more blue than red, and sand about 30 less.
int triAcidArchiIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness < 150) {
        return 2;
    }
    return means.blueOverRed >= 4 ? 0 : 1;
}

// 0 is Lush, 1 is Archipelago sand, and 2 is Acid. Sand is far brighter than either; olive Acid has about
// 5 less red than green, and blue-green Lush about 79 less.
int triAcidArchiLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= -40 ? 2 : 0;
}

// 0 is Mars, 1 is Archipelago sand, and 2 is Acid. Sand is far brighter than either; Mars has at least
// 47 more red than green, and olive Acid at most about 1.
int triAcidArchiMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= 24 ? 0 : 2;
}

// 0 is Metal, 1 is Archipelago sand, and 2 is Acid. Sand is far brighter than either; twice the green
// over red plus the green over blue, as acidMetalTileIsAcid uses, is at least 18 for Acid and at most 4
// for Metal plating.
int triAcidArchiMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return -2 * means.redOverGreen - means.blueOverGreen >= 11 ? 2 : 0;
}

// 0 is ice, 1 is Crystal, and 2 is Acid. Ice is far brighter than either ground (about 228 against at
// most 86 and 94); purple Crystal has at least 35 more blue than green, and olive Acid 9 to 30 less.
int triAcidCrystalIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 0;
    }
    return means.blueOverGreen >= 13 ? 1 : 2;
}

// 0 is ice, 1 is Desert sand, and 2 is Acid, by brightness alone: ice is at least 227 bright, Desert sand
// 146 to 153, and Acid ground at most about 94.
int triAcidDesertIceTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 190) {
        return 0;
    }
    return means.brightness >= 120 ? 1 : 2;
}

// 0 is Lush, 1 is ice, and 2 is Acid. Ice is far brighter than either ground; olive Acid has about 5 less
// red than green, and blue-green Lush about 79 less.
int triAcidIceLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= -40 ? 2 : 0;
}

// 0 is ice, 1 is Mars, and 2 is Acid. Ice is far brighter than either ground; Mars has at least 47 more
// red than green, and olive Acid at most about 1.
int triAcidIceMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 0;
    }
    return means.redOverGreen >= 24 ? 1 : 2;
}

// 0 is ice, 1 is Metal, and 2 is Acid. Ice is far brighter than either ground; twice the green over red
// plus the green over blue, as acidMetalTileIsAcid uses, is at least 18 for Acid and at most 4 for Metal.
int triAcidIceMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 0;
    }
    return -2 * means.redOverGreen - means.blueOverGreen >= 11 ? 2 : 1;
}

// 0 is Lush, 1 is ice, and 2 is Crystal. Ice is far brighter than either ground; purple Crystal has about
// 40 more blue than green, and blue-green Lush about 41 less.
int triCrystalIceLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.blueOverGreen >= 0 ? 2 : 0;
}

// 0 is ice, 1 is Mars, and 2 is Crystal. Ice is far brighter than either ground; purple Crystal has about
// 40 more blue than green, and Mars about 15 less.
int triCrystalIceMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 0;
    }
    return means.blueOverGreen >= 12 ? 2 : 1;
}

// 0 is ice, 1 is Metal, and 2 is Crystal. Ice is far brighter than either ground; purple Crystal has at
// least 35 more blue than green, and Metal plating at most about 2.
int triCrystalIceMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 0;
    }
    return means.blueOverGreen >= 18 ? 2 : 1;
}

// 0 is Lush, 1 is Desert sand, and 2 is ice, by brightness alone: ice is at least 227 bright, Desert sand
// 146 to 153, and Lush ground at most about 58.
int triDesertIceLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 190) {
        return 2;
    }
    return means.brightness >= 103 ? 1 : 0;
}

// 0 is Desert sand, 1 is Mars, and 2 is ice, by brightness alone: ice is at least 227 bright, Desert sand
// 146 to 153, and Mars ground at most about 77.
int triDesertIceMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 190) {
        return 2;
    }
    return means.brightness >= 112 ? 0 : 1;
}

// 0 is Desert sand, 1 is Metal, and 2 is ice, by brightness alone: ice is at least 227 bright, Desert sand
// 146 to 153, and Metal plating at most about 104.
int triDesertIceMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 190) {
        return 2;
    }
    return means.brightness >= 125 ? 0 : 1;
}

// 0 is Lush, 1 is ice, and 2 is Mars. Ice is far brighter than either ground; Mars has about 50 more red
// than green, and blue-green Lush about 79 less.
int triIceLushMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= -15 ? 2 : 0;
}

// 0 is Lush, 1 is ice, and 2 is Metal. Ice is far brighter than either ground; Metal plating has 2 to 25
// more red than green, and blue-green Lush about 79 less.
int triIceLushMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 1;
    }
    return means.redOverGreen >= -38 ? 2 : 0;
}

// 0 is ice, 1 is Mars, and 2 is Metal. Ice is far brighter than either ground; Mars has at least 47 more
// red than green, and Metal plating at most 25.
int triIceMarsMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 150) {
        return 0;
    }
    return means.redOverGreen >= 36 ? 1 : 2;
}

// 0 is Desert sand, 1 is Mars, and 2 is Metal. Desert sand is at least 146 bright, against at most about
// 77 for Mars and 104 for Metal; Mars has at least 47 more red than green, and Metal plating at most 25.
int triDesertMarsMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 125) {
        return 0;
    }
    return means.redOverGreen >= 36 ? 1 : 2;
}

// 0 is Lush, 1 is Desert sand, and 2 is Metal. Desert sand is at least 146 bright, against at most about
// 58 for Lush and 104 for Metal; Metal plating has 2 to 25 more red than green, and blue-green Lush about
// 79 less.
int triDesertLushMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 125) {
        return 1;
    }
    return means.redOverGreen >= -38 ? 2 : 0;
}

// 0 is Lush, 1 is Desert sand, and 2 is Mars. Desert sand is at least 146 bright, against at most about 58
// for Lush and 77 for Mars; Mars has about 50 more red than green, and blue-green Lush about 79 less.
int triDesertLushMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 112) {
        return 1;
    }
    return means.redOverGreen >= -15 ? 2 : 0;
}

// 0 is Crystal, 1 is Mars, and 2 is Metal: the nearest typical colour by red over green and blue over
// green. Crystal is about (21, 40), Mars (50, -15), and Metal plating (8, -4).
int triCrystalMarsMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{21, 40}, {50, -15}, {8, -4}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is Lush, 1 is Crystal, and 2 is Metal: the nearest typical colour by red over green and blue over
// green. Lush is about (-79, -41), Crystal (21, 40), and Metal plating (8, -4).
int triCrystalLushMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{-79, -41}, {21, 40}, {8, -4}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is Lush, 1 is Crystal, and 2 is Mars: the nearest typical colour by red over green and blue over
// green. Lush is about (-79, -41), Crystal (21, 40), and Mars (50, -15).
int triCrystalLushMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{-79, -41}, {21, 40}, {50, -15}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is Desert sand, 1 is Crystal, and 2 is Metal. Desert sand is at least 146 bright, against at most
// about 86 and 104; purple Crystal has at least 35 more blue than green, and Metal plating at most 2.
int triCrystalDesertMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 125) {
        return 0;
    }
    return means.blueOverGreen >= 18 ? 1 : 2;
}

// 0 is Desert sand, 1 is Crystal, and 2 is Mars. Desert sand is at least 146 bright, against at most
// about 86 and 77; purple Crystal has about 40 more blue than green, and Mars about 15 less.
int triCrystalDesertMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 116) {
        return 0;
    }
    return means.blueOverGreen >= 12 ? 1 : 2;
}

// 0 is Lush, 1 is Desert sand, and 2 is Crystal. Desert sand is at least 146 bright, against at most about
// 58 and 86; purple Crystal has about 40 more blue than green, and blue-green Lush about 41 less.
int triCrystalDesertLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 116) {
        return 1;
    }
    return means.blueOverGreen >= 0 ? 2 : 0;
}

// 0 is Desert sand, 1 is Crystal, and 2 is Acid. Desert sand is at least 146 bright, against at most about
// 86 and 94; purple Crystal has at least 35 more blue than green, and olive Acid 9 to 30 less.
int triAcidCrystalDesertTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 120) {
        return 0;
    }
    return means.blueOverGreen >= 13 ? 1 : 2;
}

// 0 is Lush, 1 is Desert sand, and 2 is Acid. Desert sand is at least 146 bright, against at most about 58
// and 94; olive Acid has about 5 less red than green, and blue-green Lush about 79 less.
int triAcidDesertLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 120) {
        return 1;
    }
    return means.redOverGreen >= -40 ? 2 : 0;
}

// 0 is Desert sand, 1 is Mars, and 2 is Acid. Desert sand is at least 146 bright, against at most about 77
// and 94; Mars has at least 47 more red than green, and olive Acid at most about 1.
int triAcidDesertMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 120) {
        return 0;
    }
    return means.redOverGreen >= 24 ? 1 : 2;
}

// 0 is Desert sand, 1 is Metal, and 2 is Acid. Desert sand is at least 146 bright, against at most about
// 104 and 94; twice the green over red plus the green over blue is at least 18 for Acid and at most 4 for
// Metal plating, as acidMetalTileIsAcid uses.
int triAcidDesertMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.brightness >= 125) {
        return 0;
    }
    return -2 * means.redOverGreen - means.blueOverGreen >= 11 ? 2 : 1;
}

// 0 is Lush, 1 is Crystal, and 2 is Acid: the nearest typical colour by red over green and blue over
// green. Lush is about (-79, -41), Crystal (21, 40), and olive Acid (-5, -20).
int triAcidCrystalLushTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{-79, -41}, {21, 40}, {-5, -20}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is Crystal, 1 is Mars, and 2 is Acid: the nearest typical colour by red over green and blue over
// green. Crystal is about (21, 40), Mars (50, -15), and olive Acid (-5, -20).
int triAcidCrystalMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    static constexpr double centres[3][2] = {{21, 40}, {50, -15}, {-5, -20}};
    return nearestLand(means.redOverGreen, means.blueOverGreen, centres);
}

// 0 is Crystal, 1 is Metal, and 2 is Acid. Purple Crystal has at least 35 more blue than green, and the
// others at most about 2; twice the green over red plus the green over blue is at least 18 for Acid and
// at most 4 for Metal plating, as acidMetalTileIsAcid uses.
int triAcidCrystalMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.blueOverGreen >= 18) {
        return 0;
    }
    return -2 * means.redOverGreen - means.blueOverGreen >= 11 ? 2 : 1;
}

// 0 is Lush, 1 is Mars, and 2 is Acid, by red over green: Mars at least 47, olive Acid about -11 to 1, and
// blue-green Lush about -79.
int triAcidLushMarsTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.redOverGreen >= 24) {
        return 1;
    }
    return means.redOverGreen >= -40 ? 2 : 0;
}

// 0 is Lush, 1 is Metal, and 2 is Acid. Blue-green Lush has about 79 less red than green, the others at
// most about 11 less; twice the green over red plus the green over blue is at least 18 for Acid and at
// most 4 for Metal plating, as acidMetalTileIsAcid uses.
int triAcidLushMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.redOverGreen < -40) {
        return 0;
    }
    return -2 * means.redOverGreen - means.blueOverGreen >= 11 ? 2 : 1;
}

// 0 is Mars, 1 is Metal, and 2 is Acid. Mars has at least 47 more red than green, and the others at most
// 25; twice the green over red plus the green over blue is at least 18 for Acid and at most 4 for Metal
// plating, as acidMetalTileIsAcid uses.
int triAcidMarsMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.redOverGreen >= 36) {
        return 0;
    }
    return -2 * means.redOverGreen - means.blueOverGreen >= 11 ? 2 : 1;
}

// 0 is Lush, 1 is Mars, and 2 is Metal, by red over green: Mars at least 47, Metal plating 2 to 25, and
// blue-green Lush about -79.
int triLushMarsMetalTileLand(const std::uint8_t* bytes) {
    const TileColourMeans means = tileColourMeans(bytes);
    if (means.redOverGreen >= 36) {
        return 1;
    }
    return means.redOverGreen >= -38 ? 2 : 0;
}

// With mars set, the second land is Mars instead: red-brown ground with more red than green.
int triGreenIceLushTileLand(const std::uint8_t* bytes, bool mars = false) {
    int grass = 0;
    int ice = 0;
    int lush = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const bool greenish = g > r + 10;
        grass += greenish && r > b + 5;
        lush += g > r + 25 && b + 5 >= r;
        ice += mars ? !greenish && r > b + 5 : !greenish && b + 3 >= r;
    }
    const double weightedGrass = grass * 1.7;
    if (ice >= weightedGrass && ice >= lush) {
        return 1;
    }
    return weightedGrass >= lush ? 0 : 2;
}

std::string_view blendShapeName(std::string_view loweredName, std::string_view prefix) {
    std::string_view rest = loweredName;
    if (rest.size() > prefix.size() && rest.substr(0, prefix.size()) == prefix) {
        rest.remove_prefix(prefix.size());
    }
    if (const auto dash = rest.rfind('-'); dash != std::string_view::npos) {
        bool digits = dash + 1 < rest.size();
        for (std::size_t i = dash + 1; i < rest.size(); ++i) {
            digits = digits && rest[i] >= '0' && rest[i] <= '9';
        }
        if (digits) {
            rest = rest.substr(0, dash);
        }
    }
    return rest;
}

// 1 is the unmarked land, 0 is the named land, -1 means the colour decides.
// The named side of a blend must stay that land at the rim, or a second cell
// rereads those corners as the other land and stamps a hole through the first.
int blendForceUnmarked(std::string_view shape, int tx, int ty, int tileWidth, int tileHeight) {
    constexpr int rim = 2;
    const bool topRim = ty < rim;
    const bool bottomRim = ty >= tileHeight - rim;
    const bool leftRim = tx < rim;
    const bool rightRim = tx >= tileWidth - rim;
    if (shape == "t" && topRim) {
        return 0;
    }
    if (shape == "t" && bottomRim) {
        return 1;
    }
    if (shape == "b" && bottomRim) {
        return 0;
    }
    if (shape == "b" && topRim) {
        return 1;
    }
    if (shape == "l" && leftRim) {
        return 0;
    }
    if (shape == "l" && rightRim) {
        return 1;
    }
    if (shape == "r" && rightRim) {
        return 0;
    }
    if (shape == "r" && leftRim) {
        return 1;
    }
    if (shape == "tl" && topRim && leftRim) {
        return 0;
    }
    if (shape == "tr" && topRim && rightRim) {
        return 0;
    }
    if (shape == "bl" && bottomRim && leftRim) {
        return 0;
    }
    if (shape == "br" && bottomRim && rightRim) {
        return 0;
    }
    if (shape == "low-tl" && topRim && leftRim) {
        return 1;
    }
    if (shape == "low-tr" && topRim && rightRim) {
        return 1;
    }
    if (shape == "low-bl" && bottomRim && leftRim) {
        return 1;
    }
    if (shape == "low-br" && bottomRim && rightRim) {
        return 1;
    }
    if ((shape == "tl" || shape == "tr" || shape == "bl" || shape == "br") && (topRim || bottomRim || leftRim || rightRim)
        && !((shape == "tl" && topRim && leftRim) || (shape == "tr" && topRim && rightRim)
            || (shape == "bl" && bottomRim && leftRim) || (shape == "br" && bottomRim && rightRim))) {
        return 1;
    }
    if ((shape == "low-tl" || shape == "low-tr" || shape == "low-bl" || shape == "low-br")
        && (topRim || bottomRim || leftRim || rightRim)
        && !((shape == "low-tl" && topRim && leftRim) || (shape == "low-tr" && topRim && rightRim)
            || (shape == "low-bl" && bottomRim && leftRim) || (shape == "low-br" && bottomRim && rightRim))) {
        return 0;
    }
    return -1;
}

// Lush grass is green; Acid ground is olive. The named Acid side of a
// Blend-LushAcid piece must stay Acid at the rim.
bool lushAcidTileIsLush(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-lushacid-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return archiLushTileIsLush(bytes);
}

// Lush grass is green; Crystal ground is purple. The named Crystal side of a
// Blend-LushCrystal piece must stay Crystal at the rim.
bool lushCrystalTileIsLush(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-lushcrystal-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return archiLushTileIsLush(bytes);
}

// Mars ground is red; Crystal ground is purple. The named Crystal side of a
// Blend-CrystalMars piece must stay Crystal at the rim.
bool crystalMarsTileIsMars(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-crystalmars-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return !crystalLavaTileIsCrystal(bytes);
}

// Mars ground is darker brown; Desert sand is pale. The named Desert side of a
// Blend-DesertMars piece must stay Desert at the rim.
bool desertMarsTileIsMars(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-desertmars-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return !desertLavaTileIsDesert(bytes);
}

// Mars ground is red; Metal plating is grey. The named Metal side of a
// Blend-MarsMetal piece must stay Metal at the rim.
bool marsMetalTileIsMars(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-marsmetal-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return marsMetalColourIsMars(bytes);
}

// Mars ground is red; Acid ground is olive. The named Acid side of a
// Blend-AcidMars piece must stay Acid at the rim.
bool acidMarsTileIsMars(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-acidmars-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return acidMarsColourIsMars(bytes);
}

// Metal plating is grey; Acid ground is olive. The named Acid side of a
// Blend-AcidMetal piece must stay Acid at the rim.
bool acidMetalTileIsMetal(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-acidmetal-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return !acidMetalTileIsAcid(bytes);
}

// Metal plating is grey; Crystal ground is purple. The named Crystal side of a
// Blend-CrystalMetal piece must stay Crystal at the rim.
bool crystalMetalTileIsMetal(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-crystalmetal-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return !crystalLavaTileIsCrystal(bytes);
}

// GreenWorld grass is green; Crystal ground is purple. The named Crystal side of a
// Blend-GreenCrystal piece must stay Crystal at the rim.
bool greenCrystalTileIsGreen(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-greencrystal-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return greenAcidTileIsGrass(bytes);
}

// 0 is Lava, 1 is Acid, and 2 is Crystal, for the Acid, Crystal and Lava three-way tiles.
int triAcidCrystalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int acid = 0;
    int crystal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool purple = b > g + 5 && lum <= 150;
        crystal += purple;
        acid += !purple && g + 2 >= r && g > b + 2;
        lava += !purple && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedCrystal = crystal * 1.5;
    if (weightedCrystal >= acid && weightedCrystal >= lava) {
        return 2;
    }
    return acid >= lava ? 1 : 0;
}

// Acid olive, Desert sand, and Lava brown. Pale sand is weighted so a mixed
// tile still reads as Desert when the sand is there.
int triAcidDesertTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int acid = 0;
    int desert = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool sand = lum >= 130 && r > b;
        desert += sand;
        acid += !sand && g + 2 >= r && g > b + 2;
        lava += !sand && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedDesert = desert * 1.3;
    if (weightedDesert >= acid && weightedDesert >= lava) {
        return 2;
    }
    return acid >= lava ? 1 : 0;
}

// 0 is Lava, 1 is Crystal, and 2 is Desert, for the Lava, Crystal and Desert three-way tiles.
int triCrystalDesertTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int crystal = 0;
    int desert = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool purple = b > g + 5 && lum <= 150;
        crystal += purple;
        desert += !purple && lum >= 130 && r > b;
        lava += !purple && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedCrystal = crystal * 1.5;
    const double weightedDesert = desert * 1.3;
    if (weightedCrystal >= weightedDesert && weightedCrystal >= lava) {
        return 1;
    }
    if (weightedDesert >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Grass, and 2 is Desert, for the Lava, GreenWorld and Desert three-way tiles.
int triGreenDesertTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int grass = 0;
    int desert = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool green = g > r + 4 && g > b;
        grass += green;
        desert += !green && lum >= 130 && r > b;
        lava += !green && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedDesert = desert * 1.3;
    if (grass >= weightedDesert && grass >= lava) {
        return 1;
    }
    if (weightedDesert >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Grass, and 2 is Mars, for the Lava, GreenWorld and Mars three-way tiles.
// Mars is redder than lava dirt and is weighted up for its brown specks.
int triGreenMarsTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int grass = 0;
    int mars = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool green = g > r + 4 && g > b;
        grass += green;
        mars += !green && r > g + 25 && lum <= 140;
        lava += !green && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedMars = mars * 1.4;
    if (grass >= weightedMars && grass >= lava) {
        return 1;
    }
    if (weightedMars >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Ice, and 2 is Lush, for the Lava, Ice and Lush three-way tiles.
// Lush is blue-green; ice is pale; lava dirt is brown. Ice is weighted up for snow specks.
int triIceLushTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int ice = 0;
    int lush = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool lushGreen = g > r + 25 && b + 5 >= r;
        lush += lushGreen;
        ice += !lushGreen && b + 3 >= r;
        lava += !lushGreen && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedIce = ice * 1.2;
    if (lush >= weightedIce && lush >= lava) {
        return 2;
    }
    if (weightedIce >= lava) {
        return 1;
    }
    return 0;
}

// 0 is Lava, 1 is Ice, and 2 is Crystal, for the Lava, Ice and Crystal three-way tiles.
// Ice is pale blue; Crystal is purple and is weighted up for its specks; lava dirt is brown.
int triIceCrystalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int ice = 0;
    int crystal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool purple = b > g + 5 && lum <= 150;
        crystal += purple;
        ice += !purple && b + 3 >= r;
        lava += !purple && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedCrystal = crystal * 1.5;
    const double weightedIce = ice * 1.2;
    if (weightedIce >= weightedCrystal && weightedIce >= lava) {
        return 1;
    }
    if (weightedCrystal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Ice, and 2 is Acid, for the Lava, Ice and Acid three-way tiles.
// Ice is pale blue; Acid is olive; lava dirt is brown.
int triIceAcidTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int ice = 0;
    int acid = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool pale = b + 3 >= r;
        const bool olive = !pale && g + 2 >= r && g > b + 2;
        ice += pale;
        acid += olive;
        lava += !pale && !olive && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedIce = ice * 1.2;
    if (weightedIce >= acid && weightedIce >= lava) {
        return 1;
    }
    if (acid >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Lush, and 2 is Desert, for the Lava, Desert and Lush three-way tiles.
// Lush is blue-green; Desert sand is pale orange; lava dirt is brown.
int triDesertLushTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int lush = 0;
    int desert = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool lushGreen = g > r + 20;
        lush += lushGreen;
        desert += !lushGreen && lum >= 130 && r > b;
        lava += !lushGreen && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedDesert = desert * 1.3;
    if (lush >= weightedDesert && lush >= lava) {
        return 1;
    }
    if (weightedDesert >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Grass, and 2 is Crystal, for the Lava, GreenWorld and Crystal three-way tiles.
// Grass is green; Crystal is purple and is weighted up for its specks; lava dirt is brown.
int triGreenCrystalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int grass = 0;
    int crystal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool green = g > r + 4 && g > b;
        const bool purple = !green && b > g + 5 && lum <= 150;
        grass += green;
        crystal += purple;
        lava += !green && !purple && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedCrystal = crystal * 1.5;
    if (grass >= weightedCrystal && grass >= lava) {
        return 1;
    }
    if (weightedCrystal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Grass, and 2 is Archipelago, for the Lava, Archipelago and GreenWorld three-way tiles.
// Grass is green; Archipelago sand is pale; lava dirt is brown.
int triArchiGreenTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int grass = 0;
    int archi = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool green = g > r + 4 && g > b;
        grass += green;
        archi += !green && lum >= 160 && r > b;
        lava += !green && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedArchi = archi * 1.2;
    if (grass >= weightedArchi && grass >= lava) {
        return 1;
    }
    if (weightedArchi >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Lush, and 2 is Archipelago, for the Lava, Archipelago and Lush three-way tiles.
// Lush is blue-green; Archipelago sand is pale; lava dirt is brown.
int triArchiLushTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int lush = 0;
    int archi = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool lushGreen = g > r + 25 && b + 5 >= r;
        const bool sand = !lushGreen && lum >= 160 && r > b;
        lush += lushGreen;
        archi += sand;
        lava += !lushGreen && !sand && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedArchi = archi * 1.2;
    if (lush >= weightedArchi && lush >= lava) {
        return 1;
    }
    if (weightedArchi >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Lush, and 2 is Crystal, for the Lava, Crystal and Lush three-way tiles.
// Lush is blue-green; Crystal is purple and is weighted up for its specks; lava dirt is brown.
int triLushCrystalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int lush = 0;
    int crystal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool lushGreen = g > r + 25 && b + 5 >= r;
        const bool purple = !lushGreen && b > g + 5 && lum <= 150;
        lush += lushGreen;
        crystal += purple;
        lava += !lushGreen && !purple && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedCrystal = crystal * 1.5;
    if (lush >= weightedCrystal && lush >= lava) {
        return 1;
    }
    if (weightedCrystal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Desert, and 2 is Mars, for the Lava, Desert and Mars three-way tiles.
// Desert sand is pale orange; Mars is redder and is weighted up for its specks; lava dirt is brown.
int triDesertMarsTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int desert = 0;
    int mars = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool red = r > g + 25 && lum <= 140;
        mars += red;
        desert += !red && lum >= 130 && r > b;
        lava += !red && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedMars = mars * 1.4;
    const double weightedDesert = desert * 1.3;
    if (weightedDesert >= weightedMars && weightedDesert >= lava) {
        return 1;
    }
    if (weightedMars >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Lush, and 2 is Acid, for the Lava, Acid and Lush three-way tiles.
// Lush is blue-green; Acid is olive; lava dirt is brown.
int triAcidLushTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int lush = 0;
    int acid = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool lushGreen = g > r + 25 && b + 5 >= r;
        const bool olive = !lushGreen && g + 2 >= r && g > b + 2;
        lush += lushGreen;
        acid += olive;
        lava += !lushGreen && !olive && r > g + 2 && r > b + 8 && lum <= 145;
    }
    if (lush >= acid && lush >= lava) {
        return 1;
    }
    if (acid >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Acid, and 2 is Mars, for the Lava, Acid and Mars three-way tiles.
// Acid is olive; Mars is redder and is weighted up for its specks; lava dirt is brown.
int triAcidMarsTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int acid = 0;
    int mars = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool olive = g + 2 >= r && g > b + 2;
        const bool red = !olive && r > g + 25 && lum <= 140;
        acid += olive;
        mars += red;
        lava += !olive && !red && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedMars = mars * 1.4;
    if (acid >= weightedMars && acid >= lava) {
        return 1;
    }
    if (weightedMars >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Archipelago, and 2 is Mars, for the Lava, Archipelago and Mars three-way tiles.
// Archipelago sand is pale; Mars is redder and is weighted up for its specks; lava dirt is brown.
int triArchiMarsTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int archi = 0;
    int mars = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool red = r > g + 25 && lum <= 140;
        const bool sand = !red && lum >= 160 && r > b;
        archi += sand;
        mars += red;
        lava += !red && !sand && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedArchi = archi * 1.2;
    const double weightedMars = mars * 1.4;
    if (weightedArchi >= weightedMars && weightedArchi >= lava) {
        return 1;
    }
    if (weightedMars >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Crystal, and 2 is Mars, for the Lava, Crystal and Mars three-way tiles.
// Crystal is purple and is weighted up for its specks; Mars is redder; lava dirt is brown.
int triCrystalMarsTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int crystal = 0;
    int mars = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool purple = b > g + 5 && lum <= 150;
        const bool red = !purple && r > g + 25 && lum <= 140;
        crystal += purple;
        mars += red;
        lava += !purple && !red && r > b + 8 && lum <= 140 && r <= g + 25;
    }
    const double weightedCrystal = crystal * 1.5;
    const double weightedMars = mars * 1.4;
    if (weightedCrystal >= weightedMars && weightedCrystal >= lava) {
        return 1;
    }
    if (weightedMars >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Grass, and 2 is Metal, for the Lava, GreenWorld and Metal three-way tiles.
// Grass is green; Metal plating is grey; lava dirt is brown.
int triGreenMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int grass = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool green = g > r + 4 && g > b;
        const bool brown = !green && r > g + 2 && r > b + 8 && lum <= 145;
        grass += green;
        lava += brown;
        metal += !green && !brown;
    }
    const double weightedMetal = metal * 1.2;
    if (grass >= weightedMetal && grass >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Desert, and 2 is Metal, for the Lava, Desert and Metal three-way tiles.
// Desert sand is pale orange; Metal plating is grey; lava dirt is brown.
int triDesertMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int desert = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool brown = r > g + 2 && r > b + 8 && lum <= 145;
        const bool sand = !brown && lum >= 130 && r > b;
        lava += brown;
        desert += sand;
        metal += !brown && !sand;
    }
    const double weightedDesert = desert * 1.3;
    const double weightedMetal = metal * 1.2;
    if (weightedDesert >= weightedMetal && weightedDesert >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Archipelago, and 2 is Metal, for the Lava, Archipelago and Metal three-way tiles.
// Archipelago sand is pale; Metal plating is grey; lava dirt is brown.
int triArchiMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int archi = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool sand = lum >= 160 && r > b;
        const bool brown = !sand && r > g + 2 && r > b + 8 && lum <= 145;
        archi += sand;
        lava += brown;
        metal += !sand && !brown;
    }
    const double weightedArchi = archi * 1.2;
    const double weightedMetal = metal * 1.2;
    if (weightedArchi >= weightedMetal && weightedArchi >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Lush, and 2 is Metal, for the Lava, Lush and Metal three-way tiles.
// Lush is blue-green; Metal plating is grey; lava dirt is brown.
int triLushMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int lush = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool lushGreen = g > r + 25 && b + 5 >= r;
        const bool brown = !lushGreen && r > g + 2 && r > b + 8 && lum <= 145;
        lush += lushGreen;
        lava += brown;
        metal += !lushGreen && !brown;
    }
    const double weightedMetal = metal * 1.2;
    if (lush >= weightedMetal && lush >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Ice, and 2 is Metal, for the Lava, Ice and Metal three-way tiles.
// Ice is pale blue or bright snow; Metal plating is grey; lava dirt is brown.
int triIceMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int ice = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool pale = (b > r + 4) || (lum >= 170 && b >= r);
        const bool brown = !pale && r > g + 2 && r > b + 8 && lum <= 145;
        ice += pale;
        lava += brown;
        metal += !pale && !brown;
    }
    const double weightedIce = ice * 1.2;
    const double weightedMetal = metal * 1.2;
    if (weightedIce >= weightedMetal && weightedIce >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Acid, and 2 is Metal, for the Lava, Acid and Metal three-way tiles.
// Acid is olive; Metal plating is grey; lava dirt is brown.
int triAcidMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int acid = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool olive = g + 2 >= r && g > b + 2;
        const bool brown = !olive && r > g + 2 && r > b + 8 && lum <= 145;
        acid += olive;
        lava += brown;
        metal += !olive && !brown;
    }
    const double weightedMetal = metal * 1.2;
    if (acid >= weightedMetal && acid >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Crystal, and 2 is Metal, for the Lava, Crystal and Metal three-way tiles.
// Crystal is purple and is weighted up for its specks; Metal plating is grey; lava dirt is brown.
int triCrystalMetalTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int crystal = 0;
    int metal = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool purple = b > g + 5 && lum <= 150;
        const bool brown = !purple && r > g + 2 && r > b + 8 && lum <= 145;
        crystal += purple;
        lava += brown;
        metal += !purple && !brown;
    }
    const double weightedCrystal = crystal * 1.5;
    const double weightedMetal = metal * 1.2;
    if (weightedCrystal >= weightedMetal && weightedCrystal >= lava) {
        return 1;
    }
    if (weightedMetal >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Acid, and 2 is Archipelago, for the Lava, Acid and Archipelago three-way tiles.
// Acid is olive; Archipelago sand is pale; lava dirt is brown.
int triAcidArchiTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int acid = 0;
    int archi = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool sand = lum >= 160 && r > b && !(g > r + 4);
        const bool olive = !sand && g + 2 >= r && g > b + 2;
        archi += sand;
        acid += olive;
        lava += !sand && !olive && r > g + 2 && r > b + 8 && lum <= 145;
    }
    const double weightedArchi = archi * 1.2;
    if (acid >= weightedArchi && acid >= lava) {
        return 1;
    }
    if (weightedArchi >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Archipelago, and 2 is Desert, for the Lava, Archipelago and Desert three-way tiles.
// Archipelago sand is pale; Desert sand is darker orange.
int triArchiDesertTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int archi = 0;
    int desert = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool rock = r > g + 2 && r > b + 8 && lum <= 145;
        lava += rock;
        archi += !rock && lum >= 185;
        desert += !rock && lum >= 130 && lum < 185 && r > b;
    }
    const double weightedDesert = desert * 1.3;
    const double weightedArchi = archi * 1.2;
    if (weightedArchi >= weightedDesert && weightedArchi >= lava) {
        return 1;
    }
    if (weightedDesert >= lava) {
        return 2;
    }
    return 0;
}

// 0 is Lava, 1 is Ice, and 2 is Desert, for the Lava, Ice and Desert three-way tiles.
int triIceDesertTileLand(const std::uint8_t* bytes) {
    int lava = 0;
    int ice = 0;
    int desert = 0;
    for (int i = 0; i < 1024; ++i) {
        const int r = kTaPalette[bytes[i]][0];
        const int g = kTaPalette[bytes[i]][1];
        const int b = kTaPalette[bytes[i]][2];
        const int lum = (30 * r + 59 * g + 11 * b) / 100;
        const bool rock = r > g + 2 && r > b + 8 && lum <= 145;
        lava += rock;
        ice += !rock && b + 4 >= r;
        desert += !rock && r > b + 4 && lum >= 130;
    }
    const double weightedIce = ice * 1.2;
    const double weightedDesert = desert * 1.3;
    if (weightedIce >= weightedDesert && weightedIce >= lava) {
        return 1;
    }
    if (weightedDesert >= lava) {
        return 2;
    }
    return 0;
}

// Ice is pale blue; Crystal ground is purple. The named Ice side of a
// Blend-IceCrystal piece must stay Ice at the rim.
bool iceCrystalTileIsCrystal(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    const std::string_view shape = blendShapeName(loweredName, "blend-icecrystal-");
    if (const int forced = blendForceUnmarked(shape, tx, ty, tileWidth, tileHeight); forced >= 0) {
        return forced == 1;
    }
    return !iceCrystalColourIsIce(bytes);
}

// Desert sand is pale; Acid ground is dark olive. The named Acid side of a
// Blend-AcidDesert piece must stay Acid at the rim, or a second Acid cell
// rereads those corners as Desert and stamps a sand square over the first.
bool acidDesertTileIsDesert(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    std::string_view rest = loweredName;
    constexpr std::string_view prefix = "blend-aciddesert-";
    if (rest.size() > prefix.size() && rest.substr(0, prefix.size()) == prefix) {
        rest.remove_prefix(prefix.size());
    }
    if (const auto dash = rest.rfind('-'); dash != std::string_view::npos) {
        bool digits = dash + 1 < rest.size();
        for (std::size_t i = dash + 1; i < rest.size(); ++i) {
            digits = digits && rest[i] >= '0' && rest[i] <= '9';
        }
        if (digits) {
            rest = rest.substr(0, dash);
        }
    }
    const bool left = tx * 2 < tileWidth;
    const bool top = ty * 2 < tileHeight;
    bool acidByShape = true;
    if (rest == "t") {
        acidByShape = top;
    } else if (rest == "b") {
        acidByShape = !top;
    } else if (rest == "l") {
        acidByShape = left;
    } else if (rest == "r") {
        acidByShape = !left;
    } else if (rest == "tl") {
        acidByShape = top && left;
    } else if (rest == "tr") {
        acidByShape = top && !left;
    } else if (rest == "bl") {
        acidByShape = !top && left;
    } else if (rest == "br") {
        acidByShape = !top && !left;
    } else if (rest == "low-tl") {
        acidByShape = !(top && left);
    } else if (rest == "low-tr") {
        acidByShape = !(top && !left);
    } else if (rest == "low-bl") {
        acidByShape = !(!top && left);
    } else if (rest == "low-br") {
        acidByShape = !(!top && !left);
    }
    constexpr int rim = 2;
    const bool topRim = ty < rim;
    const bool bottomRim = ty >= tileHeight - rim;
    const bool leftRim = tx < rim;
    const bool rightRim = tx >= tileWidth - rim;
    if (rest == "t" && topRim) {
        return false;
    }
    if (rest == "t" && bottomRim) {
        return true;
    }
    if (rest == "b" && bottomRim) {
        return false;
    }
    if (rest == "b" && topRim) {
        return true;
    }
    if (rest == "l" && leftRim) {
        return false;
    }
    if (rest == "l" && rightRim) {
        return true;
    }
    if (rest == "r" && rightRim) {
        return false;
    }
    if (rest == "r" && leftRim) {
        return true;
    }
    if (rest == "tl" && topRim && leftRim) {
        return false;
    }
    if (rest == "tr" && topRim && rightRim) {
        return false;
    }
    if (rest == "bl" && bottomRim && leftRim) {
        return false;
    }
    if (rest == "br" && bottomRim && rightRim) {
        return false;
    }
    if (rest == "low-tl" && topRim && leftRim) {
        return true;
    }
    if (rest == "low-tr" && topRim && rightRim) {
        return true;
    }
    if (rest == "low-bl" && bottomRim && leftRim) {
        return true;
    }
    if (rest == "low-br" && bottomRim && rightRim) {
        return true;
    }
    if ((rest == "tl" || rest == "tr" || rest == "bl" || rest == "br") && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "tl" && topRim && leftRim) || (rest == "tr" && topRim && rightRim)
            || (rest == "bl" && bottomRim && leftRim) || (rest == "br" && bottomRim && rightRim))) {
        return true;
    }
    if ((rest == "low-tl" || rest == "low-tr" || rest == "low-bl" || rest == "low-br")
        && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "low-tl" && topRim && leftRim) || (rest == "low-tr" && topRim && rightRim)
            || (rest == "low-bl" && bottomRim && leftRim) || (rest == "low-br" && bottomRim && rightRim))) {
        return false;
    }
    long brightness = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        brightness += 30 * color[0] + 59 * color[1] + 11 * color[2];
    }
    const bool desertByColor = brightness >= 120L * 100 * 1024;
    return acidByShape ? desertByColor && brightness >= 140L * 100 * 1024 : desertByColor;
}

// Desert sand is pale; Crystal ground is purple. The named Crystal side of a
// Blend-CrystalDesert piece must stay Crystal at the rim, or a second Crystal
// cell rereads those corners as Desert and stamps a sand square over the first.
bool crystalDesertTileIsDesert(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    std::string_view rest = loweredName;
    constexpr std::string_view prefix = "blend-crystaldesert-";
    if (rest.size() > prefix.size() && rest.substr(0, prefix.size()) == prefix) {
        rest.remove_prefix(prefix.size());
    }
    if (const auto dash = rest.rfind('-'); dash != std::string_view::npos) {
        bool digits = dash + 1 < rest.size();
        for (std::size_t i = dash + 1; i < rest.size(); ++i) {
            digits = digits && rest[i] >= '0' && rest[i] <= '9';
        }
        if (digits) {
            rest = rest.substr(0, dash);
        }
    }
    const bool left = tx * 2 < tileWidth;
    const bool top = ty * 2 < tileHeight;
    bool crystalByShape = true;
    if (rest == "t") {
        crystalByShape = top;
    } else if (rest == "b") {
        crystalByShape = !top;
    } else if (rest == "l") {
        crystalByShape = left;
    } else if (rest == "r") {
        crystalByShape = !left;
    } else if (rest == "tl") {
        crystalByShape = top && left;
    } else if (rest == "tr") {
        crystalByShape = top && !left;
    } else if (rest == "bl") {
        crystalByShape = !top && left;
    } else if (rest == "br") {
        crystalByShape = !top && !left;
    } else if (rest == "low-tl") {
        crystalByShape = !(top && left);
    } else if (rest == "low-tr") {
        crystalByShape = !(top && !left);
    } else if (rest == "low-bl") {
        crystalByShape = !(!top && left);
    } else if (rest == "low-br") {
        crystalByShape = !(!top && !left);
    }
    constexpr int rim = 2;
    const bool topRim = ty < rim;
    const bool bottomRim = ty >= tileHeight - rim;
    const bool leftRim = tx < rim;
    const bool rightRim = tx >= tileWidth - rim;
    if (rest == "t" && topRim) {
        return false;
    }
    if (rest == "t" && bottomRim) {
        return true;
    }
    if (rest == "b" && bottomRim) {
        return false;
    }
    if (rest == "b" && topRim) {
        return true;
    }
    if (rest == "l" && leftRim) {
        return false;
    }
    if (rest == "l" && rightRim) {
        return true;
    }
    if (rest == "r" && rightRim) {
        return false;
    }
    if (rest == "r" && leftRim) {
        return true;
    }
    if (rest == "tl" && topRim && leftRim) {
        return false;
    }
    if (rest == "tr" && topRim && rightRim) {
        return false;
    }
    if (rest == "bl" && bottomRim && leftRim) {
        return false;
    }
    if (rest == "br" && bottomRim && rightRim) {
        return false;
    }
    if (rest == "low-tl" && topRim && leftRim) {
        return true;
    }
    if (rest == "low-tr" && topRim && rightRim) {
        return true;
    }
    if (rest == "low-bl" && bottomRim && leftRim) {
        return true;
    }
    if (rest == "low-br" && bottomRim && rightRim) {
        return true;
    }
    if ((rest == "tl" || rest == "tr" || rest == "bl" || rest == "br") && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "tl" && topRim && leftRim) || (rest == "tr" && topRim && rightRim)
            || (rest == "bl" && bottomRim && leftRim) || (rest == "br" && bottomRim && rightRim))) {
        return true;
    }
    if ((rest == "low-tl" || rest == "low-tr" || rest == "low-bl" || rest == "low-br")
        && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "low-tl" && topRim && leftRim) || (rest == "low-tr" && topRim && rightRim)
            || (rest == "low-bl" && bottomRim && leftRim) || (rest == "low-br" && bottomRim && rightRim))) {
        return false;
    }
    long brightness = 0;
    long purple = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        brightness += 30 * color[0] + 59 * color[1] + 11 * color[2];
        purple += static_cast<int>(color[2]) - static_cast<int>(color[1]);
    }
    if (purple >= 0) {
        return false;
    }
    const bool desertByColor = brightness >= 120L * 100 * 1024;
    return crystalByShape ? desertByColor && brightness >= 140L * 100 * 1024 : desertByColor;
}

// Archipelago sand is pale almost-white; Desert sand is darker and more orange.
// The named Archipelago side of a Blend-ArchiDesert piece must stay Archipelago
// at the rim, or a second Archipelago cell rereads those corners as Desert.
bool archiDesertTileIsDesert(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    std::string_view rest = loweredName;
    constexpr std::string_view prefix = "blend-archidesert-";
    if (rest.size() > prefix.size() && rest.substr(0, prefix.size()) == prefix) {
        rest.remove_prefix(prefix.size());
    }
    if (const auto dash = rest.rfind('-'); dash != std::string_view::npos) {
        bool digits = dash + 1 < rest.size();
        for (std::size_t i = dash + 1; i < rest.size(); ++i) {
            digits = digits && rest[i] >= '0' && rest[i] <= '9';
        }
        if (digits) {
            rest = rest.substr(0, dash);
        }
    }
    const bool left = tx * 2 < tileWidth;
    const bool top = ty * 2 < tileHeight;
    bool archiByShape = true;
    if (rest == "t") {
        archiByShape = top;
    } else if (rest == "b") {
        archiByShape = !top;
    } else if (rest == "l") {
        archiByShape = left;
    } else if (rest == "r") {
        archiByShape = !left;
    } else if (rest == "tl") {
        archiByShape = top && left;
    } else if (rest == "tr") {
        archiByShape = top && !left;
    } else if (rest == "bl") {
        archiByShape = !top && left;
    } else if (rest == "br") {
        archiByShape = !top && !left;
    } else if (rest == "low-tl") {
        archiByShape = !(top && left);
    } else if (rest == "low-tr") {
        archiByShape = !(top && !left);
    } else if (rest == "low-bl") {
        archiByShape = !(!top && left);
    } else if (rest == "low-br") {
        archiByShape = !(!top && !left);
    }
    constexpr int rim = 2;
    const bool topRim = ty < rim;
    const bool bottomRim = ty >= tileHeight - rim;
    const bool leftRim = tx < rim;
    const bool rightRim = tx >= tileWidth - rim;
    if (rest == "t" && topRim) {
        return false;
    }
    if (rest == "t" && bottomRim) {
        return true;
    }
    if (rest == "b" && bottomRim) {
        return false;
    }
    if (rest == "b" && topRim) {
        return true;
    }
    if (rest == "l" && leftRim) {
        return false;
    }
    if (rest == "l" && rightRim) {
        return true;
    }
    if (rest == "r" && rightRim) {
        return false;
    }
    if (rest == "r" && leftRim) {
        return true;
    }
    if (rest == "tl" && topRim && leftRim) {
        return false;
    }
    if (rest == "tr" && topRim && rightRim) {
        return false;
    }
    if (rest == "bl" && bottomRim && leftRim) {
        return false;
    }
    if (rest == "br" && bottomRim && rightRim) {
        return false;
    }
    if (rest == "low-tl" && topRim && leftRim) {
        return true;
    }
    if (rest == "low-tr" && topRim && rightRim) {
        return true;
    }
    if (rest == "low-bl" && bottomRim && leftRim) {
        return true;
    }
    if (rest == "low-br" && bottomRim && rightRim) {
        return true;
    }
    if ((rest == "tl" || rest == "tr" || rest == "bl" || rest == "br") && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "tl" && topRim && leftRim) || (rest == "tr" && topRim && rightRim)
            || (rest == "bl" && bottomRim && leftRim) || (rest == "br" && bottomRim && rightRim))) {
        return true;
    }
    if ((rest == "low-tl" || rest == "low-tr" || rest == "low-bl" || rest == "low-br")
        && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "low-tl" && topRim && leftRim) || (rest == "low-tr" && topRim && rightRim)
            || (rest == "low-bl" && bottomRim && leftRim) || (rest == "low-br" && bottomRim && rightRim))) {
        return false;
    }
    long brightness = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        brightness += 30 * color[0] + 59 * color[1] + 11 * color[2];
    }
    const bool desertByColor = brightness < 185L * 100 * 1024;
    return archiByShape ? desertByColor && brightness < 170L * 100 * 1024 : desertByColor;
}

// Desert sand is orange; GreenWorld grass is green. The named Desert side of a
// Blend-DesertGreen piece must stay Desert at the rim, or a second Desert cell
// rereads those corners as grass.
bool desertGreenTileIsDesert(const std::uint8_t* bytes, std::string_view loweredName, int tx, int ty, int tileWidth, int tileHeight) {
    std::string_view rest = loweredName;
    constexpr std::string_view prefix = "blend-desertgreen-";
    if (rest.size() > prefix.size() && rest.substr(0, prefix.size()) == prefix) {
        rest.remove_prefix(prefix.size());
    }
    if (const auto dash = rest.rfind('-'); dash != std::string_view::npos) {
        bool digits = dash + 1 < rest.size();
        for (std::size_t i = dash + 1; i < rest.size(); ++i) {
            digits = digits && rest[i] >= '0' && rest[i] <= '9';
        }
        if (digits) {
            rest = rest.substr(0, dash);
        }
    }
    const bool left = tx * 2 < tileWidth;
    const bool top = ty * 2 < tileHeight;
    bool desertByShape = true;
    if (rest == "t") {
        desertByShape = top;
    } else if (rest == "b") {
        desertByShape = !top;
    } else if (rest == "l") {
        desertByShape = left;
    } else if (rest == "r") {
        desertByShape = !left;
    } else if (rest == "tl") {
        desertByShape = top && left;
    } else if (rest == "tr") {
        desertByShape = top && !left;
    } else if (rest == "bl") {
        desertByShape = !top && left;
    } else if (rest == "br") {
        desertByShape = !top && !left;
    } else if (rest == "low-tl") {
        desertByShape = !(top && left);
    } else if (rest == "low-tr") {
        desertByShape = !(top && !left);
    } else if (rest == "low-bl") {
        desertByShape = !(!top && left);
    } else if (rest == "low-br") {
        desertByShape = !(!top && !left);
    }
    constexpr int rim = 2;
    const bool topRim = ty < rim;
    const bool bottomRim = ty >= tileHeight - rim;
    const bool leftRim = tx < rim;
    const bool rightRim = tx >= tileWidth - rim;
    if (rest == "t" && topRim) {
        return true;
    }
    if (rest == "t" && bottomRim) {
        return false;
    }
    if (rest == "b" && bottomRim) {
        return true;
    }
    if (rest == "b" && topRim) {
        return false;
    }
    if (rest == "l" && leftRim) {
        return true;
    }
    if (rest == "l" && rightRim) {
        return false;
    }
    if (rest == "r" && rightRim) {
        return true;
    }
    if (rest == "r" && leftRim) {
        return false;
    }
    if (rest == "tl" && topRim && leftRim) {
        return true;
    }
    if (rest == "tr" && topRim && rightRim) {
        return true;
    }
    if (rest == "bl" && bottomRim && leftRim) {
        return true;
    }
    if (rest == "br" && bottomRim && rightRim) {
        return true;
    }
    if (rest == "low-tl" && topRim && leftRim) {
        return false;
    }
    if (rest == "low-tr" && topRim && rightRim) {
        return false;
    }
    if (rest == "low-bl" && bottomRim && leftRim) {
        return false;
    }
    if (rest == "low-br" && bottomRim && rightRim) {
        return false;
    }
    if ((rest == "tl" || rest == "tr" || rest == "bl" || rest == "br") && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "tl" && topRim && leftRim) || (rest == "tr" && topRim && rightRim)
            || (rest == "bl" && bottomRim && leftRim) || (rest == "br" && bottomRim && rightRim))) {
        return false;
    }
    if ((rest == "low-tl" || rest == "low-tr" || rest == "low-bl" || rest == "low-br")
        && (topRim || bottomRim || leftRim || rightRim)
        && !((rest == "low-tl" && topRim && leftRim) || (rest == "low-tr" && topRim && rightRim)
            || (rest == "low-bl" && bottomRim && leftRim) || (rest == "low-br" && bottomRim && rightRim))) {
        return true;
    }
    int score = 0;
    for (int i = 0; i < 1024; ++i) {
        const std::uint8_t* color = kTaPalette[bytes[i]];
        score += static_cast<int>(color[1]) - static_cast<int>(color[0]);
    }
    if (desertByShape) {
        return score < 20 * 1024;
    }
    return score < -20 * 1024;
}

int trailingNumber(std::string_view name) {
    int value = -1;
    int current = 0;
    bool digits = false;
    for (const char character : name) {
        if (character >= '0' && character <= '9') {
            digits = true;
            current = std::min(1000000, current * 10 + (character - '0'));
        } else if (digits) {
            value = current;
            current = 0;
            digits = false;
        }
    }
    return digits ? current : value;
}

std::string brushSizeLabel(int size) {
    return std::to_string(size) + "x" + std::to_string(size);
}

// "32x32" for a square, "56x32" for a rectangle.
std::string brushRectLabel(int width, int height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

// Which Archipelago Islands button a brush is: 1 Island-0 8x8, 2 Rocky-0 8x8,
// 3 Island-0 Medium, 4 Island-0 Large, or 0 for any other brush.
int seaIslandKind(std::string_view brushName) {
    if (brushName == "Island-0 8x8") {
        return 1;
    }
    if (brushName == "Rocky-0 8x8") {
        return 2;
    }
    if (brushName == "Island-0 Medium") {
        return 3;
    }
    if (brushName == "Island-0 Large") {
        return 4;
    }
    if (brushName == "Island-0 16x16") {
        return 5;
    }
    if (brushName == "Island-0 32x32") {
        return 6;
    }
    return 0;
}

// Which Crystal button a Crystal.hpi section belongs to, from its folder and size.
// Islands and Reefs sit in the water on Base-0; the others sit on Base-85.
bool crystalBrushFor(std::string_view folder, int tiles, std::string& group, std::string& noun, std::string& baseName, int& baseHeight) {
    const std::string size = std::to_string(tiles) + "x" + std::to_string(tiles);
    const std::string name = lowerText(folder);
    baseName = "base-85";
    baseHeight = 85;
    if (name == "craters" && (tiles == 8 || tiles == 16)) {
        group = "Crater-85 " + size;
        noun = "crater";
    } else if (name == "hills" && (tiles == 16 || tiles == 32)) {
        group = "Hills-85 " + size;
        noun = "hill";
    } else if (name == "islands" && (tiles == 16 || tiles == 32)) {
        group = "Island-85 " + size;
        noun = "island";
        baseName = "base-0";
        baseHeight = 0;
    } else if (name == "ponds" && tiles == 16) {
        group = "Pond-85";
        noun = "pond";
    } else if (name == "reefs" && (tiles == 16 || tiles == 32)) {
        group = "Reef-85 " + size;
        noun = "reef";
        baseName = "base-0";
        baseHeight = 0;
    } else if (name == "roughs" && (tiles == 16 || tiles == 32)) {
        // One button for every rough. The cursor takes the size of the next one.
        group = "Rough-85";
        noun = "rough patch";
    } else {
        return false;
    }
    return true;
}

// Pool-85 (Mars) and Pools-85 (Ice) buttons.
bool pool85Brush(std::string_view brushName) {
    return brushName.rfind("Pool-85", 0) == 0 || brushName == "Pools-85";
}

// Mars-2.hpi and Ice.hpi Islands by size: one button each for 8 x 8, 16 x 16 and 32 x 32.
int marsIslandKindOf(int tiles) {
    return tiles == 8 ? 1 : (tiles == 16 ? 5 : (tiles == 32 ? 6 : 0));
}

// Medium and Large islands come in several sizes, so each one is chosen before it is placed.
bool seaIslandVaries(int kind) {
    return kind == 3 || kind == 4;
}

// Archipelago.hpi Islands by name and size. a013 to a018 are the rocky 8 x 8 islands.
int seaIslandKindOf(std::string_view name, int tiles) {
    const int number = trailingNumber(name);
    if (tiles == 8) {
        return number >= 13 && number <= 18 ? 2 : 1;
    }
    if (tiles >= 16 && tiles <= 20) {
        return 3;
    }
    if (tiles >= 32) {
        return 4;
    }
    return 0;
}

// Map tiles on a side. 4 x 4 paints one corner and 8 x 8 one cell, both on 8 x 8 tile cells
// with the 8 x 8 pieces. The others fill whole 16 x 16 cells with the 16 x 16 pieces.
constexpr int kBrushSizes[] = {4, 8, 16, 32, 64, 128, 256, 512};
constexpr int kBrushSizeCount = static_cast<int>(sizeof(kBrushSizes) / sizeof(kBrushSizes[0]));

std::string fitLabel(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) {
        return text;
    }
    std::string fitted = text;
    while (fitted.size() > 1 && ImGui::CalcTextSize((fitted + "...").c_str()).x > width) {
        fitted.pop_back();
    }
    return fitted + "...";
}

}  // namespace

void EditorApp::loadTilesets() {
    beginLoadingTilesets();
    while (loadingTilesets()) {
        continueLoadingTilesets(std::chrono::milliseconds(1000));
    }
}

void EditorApp::beginLoadingTilesets() {
    loadEditorOptions();
    std::error_code folderError;
    gameFolderFound_ = !gameFolder_.empty() && std::filesystem::is_directory(gameFolder_, folderError);
    gameFolderArchives_ = TilesetLibrary::archivePathsIn(gameFolder_).size();
    tilesetLoadQueue_ = TilesetLibrary::archivePathsFor(gameFolder_, programDirectory());
    tilesetLoadNext_ = 0;
    tilesetLoadFailures_.clear();
    if (tilesetLoadQueue_.empty()) {
        tilesets_.sortArchives();
    }
}

void EditorApp::reloadTilesets() {
    // The catalogue worker reads the tileset archive these clear.
    stopCatalogueBuild();
    tilesets_.clear();
    tilesetLoadQueue_.clear();
    tilesetLoadNext_ = 0;
    loadQueue_.clear();
    categories_.clear();
    for (CachedSection& section : sections_) {
        destroyTexture(section.preview);
    }
    sections_.clear();
    selectedSection_ = -1;
    favoriteSelection_.clear();
    favoriteSelectionArchive_.clear();
    tilesetSelection_.clear();
    tilesetSelectionArchive_.clear();
    tileSelectionAnchor_[0] = tileSelectionAnchor_[1] = -1;
    ghostFor_ = -1;
    destroyTexture(ghostTexture_);
    for (auto& entry : featureSprites_) {
        destroyTexture(entry.second.texture);
    }
    featureSprites_.clear();
    armedFeature_.clear();
    tilesetIndex_.clear();
    tilesetIndexArchives_ = 0;
    tilesetIndexSections_ = -1;
    tilesetQueryApplied_.clear();
    beginLoadingTilesets();
}

std::string EditorApp::tilesetLoadNextName() const {
    if (tilesetLoadNext_ >= tilesetLoadQueue_.size()) {
        return {};
    }
    return tilesetLoadQueue_[tilesetLoadNext_].filename().string();
}

void EditorApp::continueLoadingTilesets(std::chrono::milliseconds budget) {
    const auto started = std::chrono::steady_clock::now();
    while (tilesetLoadNext_ < tilesetLoadQueue_.size()) {
        const std::filesystem::path& path = tilesetLoadQueue_[tilesetLoadNext_];
        try {
            tilesets_.addArchive(path);
        } catch (const std::exception& ex) {
            tilesetLoadFailures_.push_back(path.filename().string() + ": " + ex.what());
        }
        ++tilesetLoadNext_;
        if (std::chrono::steady_clock::now() - started >= budget) {
            break;
        }
    }
    if (tilesetLoadNext_ >= tilesetLoadQueue_.size()) {
        tilesets_.sortArchives();
        if (!tilesetLoadFailures_.empty()) {
            std::string message = "Some tileset archives could not be opened.";
            for (const std::string& failure : tilesetLoadFailures_) {
                message += "\n" + failure;
            }
            error_ = message;
            tilesetLoadFailures_.clear();
        }
    }
}

std::filesystem::path EditorApp::suggestedOpenDirectory() const {
    const auto examples = std::filesystem::path(GALAXY_SOURCE_DIR) / ".." / "ExampleMaps";
    if (std::filesystem::is_directory(examples)) {
        return std::filesystem::weakly_canonical(examples);
    }
    return std::filesystem::current_path();
}

void EditorApp::openMap(const std::filesystem::path& path) {
    // A map that is already open comes to the front instead of opening twice.
    if (const int open = documentIndexForPath(path); open >= 0) {
        activateDocument(open);
        return;
    }
    try {
        LoadedMap loaded = loadMapFile(path);
        addDocument(std::move(loaded), 0);
        statusNote_.clear();
        error_.clear();
        if (map_->tileCount > kVanillaTileSlots) {
            showVanillaTileWarning();
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::resetDocumentView() {
    ++mapRevision_;
    showMapSettings_ = false;
    image_.clear();
    viewTexW_ = 0;
    viewTexH_ = 0;
    invalidateView();
    // Gestures under way belong to the map that was showing.
    selecting_ = false;
    selectionBefore_ = {};
    brushStroking_ = false;
    mountainStroke_ = false;
    featureMarquee_ = false;
    featurePressed_ = false;
    canvasPressed_ = false;
    rightDown_ = false;
    rightDragged_ = false;
    middlePanning_ = false;
    draggingStartPlayer_ = 0;
    hoverOverMap_ = false;
    hoverHeight_ = -1;
    vanillaTileWarning_.clear();
    if (!map_ && view3d_) {
        set3dView(false);
    }
}

void EditorApp::addDocument(LoadedMap map, int untitledNumber) {
    parkActiveDocument();
    MapDocument shell;
    shell.serial = ++serialCounter_;
    shell.untitledNumber = untitledNumber;
    documents_.push_back(std::move(shell));
    activeDocument_ = static_cast<int>(documents_.size()) - 1;

    map_ = std::move(map);
    mapSerial_ = documents_.back().serial;
    framed3dSerial_ = 0;
    camera3d_ = OrbitCamera{};
    fit_ = true;
    zoom_ = 1.0f;
    panX_ = 0.0f;
    panY_ = 0.0f;
    deduper_ = {};
    clearHistory();
    clearMapSelection();
    mapDirty_ = false;
    autosaveArmedAt_ = {};
    vanillaTileWarningShown_ = false;
    resetDocumentView();
}

void EditorApp::parkActiveDocument() {
    if (activeDocument_ < 0 || activeDocument_ >= static_cast<int>(documents_.size())) {
        return;
    }
    MapDocument& document = documents_[static_cast<std::size_t>(activeDocument_)];
    // A moved-from optional still holds a value, so each one is reset after the move.
    document.map = std::move(map_);
    map_.reset();
    document.deduper = std::move(deduper_);
    deduper_ = {};
    document.undo = std::move(undo_);
    undo_.clear();
    document.redo = std::move(redo_);
    redo_.clear();
    document.selection = std::move(selection_);
    selection_ = {};
    document.selectedFeatures = std::move(selectedFeatures_);
    selectedFeatures_.clear();
    document.dirty = mapDirty_;
    mapDirty_ = false;
    document.autosaveArmedAt = autosaveArmedAt_;
    document.fit = fit_;
    document.zoom = zoom_;
    document.panX = panX_;
    document.panY = panY_;
    document.camera3d = camera3d_;
    document.framedSerial = framed3dSerial_;
    document.vanillaTileWarningShown = vanillaTileWarningShown_;
    activeDocument_ = -1;
}

void EditorApp::unparkDocument(int index) {
    MapDocument& document = documents_[static_cast<std::size_t>(index)];
    map_ = std::move(document.map);
    document.map.reset();
    deduper_ = std::move(document.deduper);
    document.deduper = {};
    undo_ = std::move(document.undo);
    document.undo.clear();
    redo_ = std::move(document.redo);
    document.redo.clear();
    selection_ = std::move(document.selection);
    document.selection = {};
    selectedFeatures_ = std::move(document.selectedFeatures);
    document.selectedFeatures.clear();
    mapDirty_ = document.dirty;
    autosaveArmedAt_ = document.autosaveArmedAt;
    fit_ = document.fit;
    zoom_ = document.zoom;
    panX_ = document.panX;
    panY_ = document.panY;
    camera3d_ = document.camera3d;
    mapSerial_ = document.serial;
    framed3dSerial_ = document.framedSerial;
    vanillaTileWarningShown_ = document.vanillaTileWarningShown;
    activeDocument_ = index;
    // An armed tile or feature paste stays armed: the clipboards are shared, so a
    // copy from one map pastes into another.
    resetDocumentView();
}

void EditorApp::activateDocument(int index) {
    if (index < 0 || index >= static_cast<int>(documents_.size()) || index == activeDocument_) {
        return;
    }
    parkActiveDocument();
    unparkDocument(index);
    statusNote_.clear();
}

void EditorApp::closeDocument(int index) {
    const int count = static_cast<int>(documents_.size());
    if (index < 0 || index >= count) {
        return;
    }
    const std::uint64_t serial = documents_[static_cast<std::size_t>(index)].serial;
    const int next = activeIndexAfterClose(count, activeDocument_, index);
    if (index == activeDocument_) {
        map_.reset();
        deduper_ = {};
        clearHistory();
        clearMapSelection();
        mapDirty_ = false;
        activeDocument_ = -1;
        documents_.erase(documents_.begin() + index);
        if (next >= 0) {
            unparkDocument(next);
        } else {
            // The last map closed. The editor shows its empty window, as it does at launch.
            mapSerial_ = ++serialCounter_;
            framed3dSerial_ = 0;
            vanillaTileWarningShown_ = false;
            resetDocumentView();
        }
    } else {
        documents_.erase(documents_.begin() + index);
        activeDocument_ = next;
    }
    quitDiscarded_.erase(serial);
    if (closeAfterSaveSerial_ == serial) {
        closeAfterSaveSerial_ = 0;
    }
}

void EditorApp::requestCloseDocument(int index) {
    if (index < 0 || index >= static_cast<int>(documents_.size())) {
        return;
    }
    if (documentDirty(index)) {
        // The prompt names the map, so that map comes to the front first.
        activateDocument(index);
        closePromptForTab_ = true;
        showClosePrompt_ = true;
        return;
    }
    closeDocument(index);
}

int EditorApp::documentIndexForSerial(std::uint64_t serial) const {
    for (std::size_t i = 0; i < documents_.size(); ++i) {
        if (documents_[i].serial == serial) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int EditorApp::documentIndexForPath(const std::filesystem::path& path) const {
    for (int i = 0; i < static_cast<int>(documents_.size()); ++i) {
        const LoadedMap* map = documentMap(i);
        if (map == nullptr || map->source.empty()) {
            continue;
        }
        std::error_code error;
        if (map->source == path || std::filesystem::equivalent(map->source, path, error)) {
            return i;
        }
    }
    return -1;
}

bool EditorApp::documentDirty(int index) const {
    if (index == activeDocument_) {
        return map_.has_value() && mapDirty_;
    }
    if (index < 0 || index >= static_cast<int>(documents_.size())) {
        return false;
    }
    const MapDocument& document = documents_[static_cast<std::size_t>(index)];
    return document.map.has_value() && document.dirty;
}

const LoadedMap* EditorApp::documentMap(int index) const {
    if (index == activeDocument_) {
        return map_ ? &*map_ : nullptr;
    }
    if (index < 0 || index >= static_cast<int>(documents_.size())) {
        return nullptr;
    }
    const MapDocument& document = documents_[static_cast<std::size_t>(index)];
    return document.map ? &*document.map : nullptr;
}

std::string EditorApp::documentTitle(int index) const {
    const LoadedMap* map = documentMap(index);
    if (map == nullptr) {
        return {};
    }
    return documentTabTitle(map->source, map->displayName);
}

float EditorApp::documentTabsHeight() const {
    // One map needs no tabs, so the row shows from the second map on.
    return documents_.size() > 1 ? theme::documentTabsHeight() : 0.0f;
}

void EditorApp::drawDocumentTabs() {
    const int count = static_cast<int>(documents_.size());
    std::vector<std::string> titles(static_cast<std::size_t>(count));
    std::vector<std::string> paths(static_cast<std::size_t>(count));
    std::vector<theme::DocumentTab> tabs(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t slot = static_cast<std::size_t>(i);
        titles[slot] = documentTitle(i);
        if (const LoadedMap* map = documentMap(i); map != nullptr && !map->source.empty()) {
            paths[slot] = map->source.string();
        }
        tabs[slot].title = titles[slot].c_str();
        tabs[slot].tooltip = paths[slot].c_str();
        tabs[slot].dirty = documentDirty(i);
    }
    // Frosted glass behind the row, like the toolbar above it. Without glass (the
    // Frosted Glass menu off, or the 3D view) the row draws its own flat colors.
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const bool glass = drawGlass(documentTabsGlass_, min.x, min.y, max.x, max.y, 0.0f);
    if (glass) {
        const theme::GlassStyle& style = theme::glassStyle();
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min, max, style.sheenTop, style.sheenTop, style.sheenBottom, style.sheenBottom);
    }
    const std::string closeTip = text(Text::CloseMap) + std::string("  ") + theme::shortcut("W");
    int tab = -1;
    const theme::DocumentTabEvent event =
        theme::documentTabs("document-tabs", tabs.data(), count, activeDocument_, tab, closeTip.c_str(), glass);
    // A save in progress writes the map it started with, so the tabs wait for it.
    if (savingMap()) {
        return;
    }
    if (event.close) {
        requestCloseDocument(tab);
    } else if (event.select) {
        activateDocument(tab);
    }
}

int EditorApp::nextUntitledDocumentNumber() const {
    std::vector<int> used;
    for (int i = 0; i < static_cast<int>(documents_.size()); ++i) {
        const LoadedMap* map = documentMap(i);
        if (map != nullptr && map->source.empty()) {
            used.push_back(documents_[static_cast<std::size_t>(i)].untitledNumber);
        }
    }
    return lowestUnusedUntitledNumber(std::move(used));
}

void EditorApp::invalidateView() {
    ++mapRevision_;
    viewZoom_ = -1.0f;
    miniDirty_ = true;
    heightDirty_ = true;
}

void EditorApp::renderViewport(float canvasWidth, float canvasHeight) {
    if (!map_ || canvasWidth < 1.0f || canvasHeight < 1.0f) {
        return;
    }

    GLint gpuMax = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gpuMax);
    const int maxTexture = std::max(256, std::min(static_cast<int>(gpuMax), 4096));
    const float framebufferScale = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
    int textureWidth = std::max(1, static_cast<int>(std::lround(canvasWidth * framebufferScale)));
    int textureHeight = std::max(1, static_cast<int>(std::lround(canvasHeight * framebufferScale)));
    if (textureWidth > maxTexture || textureHeight > maxTexture) {
        const float shrink = std::min(
            static_cast<float>(maxTexture) / static_cast<float>(textureWidth),
            static_cast<float>(maxTexture) / static_cast<float>(textureHeight));
        textureWidth = std::max(1, static_cast<int>(textureWidth * shrink));
        textureHeight = std::max(1, static_cast<int>(textureHeight * shrink));
    }

    const bool unchanged = texture_ != 0
        && textureWidth == viewTexW_
        && textureHeight == viewTexH_
        && panX_ == viewPanX_
        && panY_ == viewPanY_
        && zoom_ == viewZoom_
        && canvasWidth == viewCanvasW_
        && canvasHeight == viewCanvasH_;
    if (unchanged) {
        return;
    }

    image_.resize(static_cast<std::size_t>(textureWidth) * static_cast<std::size_t>(textureHeight) * 4);
    renderMapView(*map_, image_.data(), textureWidth, textureHeight, canvasWidth, canvasHeight, panX_, panY_, zoom_);

    if (texture_ == 0) {
        glGenTextures(1, &texture_);
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (textureWidth != viewTexW_ || textureHeight != viewTexH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_.data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, textureWidth, textureHeight, GL_RGBA, GL_UNSIGNED_BYTE, image_.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    viewTexW_ = textureWidth;
    viewTexH_ = textureHeight;
    viewPanX_ = panX_;
    viewPanY_ = panY_;
    viewZoom_ = zoom_;
    viewCanvasW_ = canvasWidth;
    viewCanvasH_ = canvasHeight;
    ++viewGeneration_;
}

void EditorApp::renderHeightOverlay(float canvasWidth, float canvasHeight) {
    if (!showHeightGrid_ || !map_ || viewTexW_ <= 0 || viewTexH_ <= 0 || zoom_ == 0.0f) {
        return;
    }
    const int grid = std::clamp((heightGridSize_ + 8) / 16 * 16, 16, 256);
    heightGridSize_ = grid;
    const bool unchanged = !heightDirty_
        && heightTexture_ != 0
        && heightTexW_ == viewTexW_
        && heightTexH_ == viewTexH_
        && heightPanX_ == panX_
        && heightPanY_ == panY_
        && heightZoom_ == zoom_
        && heightCanvasW_ == canvasWidth
        && heightCanvasH_ == canvasHeight
        && heightGridCached_ == grid;
    if (unchanged) {
        return;
    }

    heightImage_.resize(static_cast<std::size_t>(viewTexW_) * static_cast<std::size_t>(viewTexH_) * 4);
    renderHeightGrid(*map_, heightImage_.data(), viewTexW_, viewTexH_, canvasWidth, canvasHeight, panX_, panY_, zoom_, grid);

    if (heightTexture_ == 0) {
        glGenTextures(1, &heightTexture_);
    }
    glBindTexture(GL_TEXTURE_2D, heightTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (viewTexW_ != heightTexW_ || viewTexH_ != heightTexH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viewTexW_, viewTexH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, heightImage_.data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewTexW_, viewTexH_, GL_RGBA, GL_UNSIGNED_BYTE, heightImage_.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    heightTexW_ = viewTexW_;
    heightTexH_ = viewTexH_;
    heightPanX_ = panX_;
    heightPanY_ = panY_;
    heightZoom_ = zoom_;
    heightCanvasW_ = canvasWidth;
    heightCanvasH_ = canvasHeight;
    heightGridCached_ = grid;
    heightDirty_ = false;
}

void EditorApp::toggleFeatureSelectMode() {
    featureSelectMode_ = !featureSelectMode_;
    featureMarquee_ = false;
    if (featureSelectMode_) {
        armedFeature_.clear();
        featureDelete_ = false;
        featurePasteArmed_ = false;
        statusNote_ = "Drag a box around features to select them. Click one to pick it. Shift adds";
    }
}

void EditorApp::toggleSelectMode() {
    if (featuresTab_) {
        toggleFeatureSelectMode();
        return;
    }
    selectionMode_ = !selectionMode_;
    selecting_ = false;
    pasteArmed_ = false;
    if (selectionMode_) {
        brushName_.clear();
    }
}

void EditorApp::toggleDeleteMode() {
    featureDelete_ = !featureDelete_;
    if (featureDelete_) {
        armedFeature_.clear();
        featureSelectMode_ = false;
        featurePasteArmed_ = false;
    }
}

namespace {
void glyphLeftSidebar(ImDrawList* draw, ImVec2 c, float s, ImU32 color);
}  // namespace

void EditorApp::drawToolbar() {
    using theme::ToolIcon;
    drawToolbarBackground();
    const float gap = theme::toolbarGap();
    const auto next = [&]() { ImGui::SameLine(0.0f, gap); };

    // Select is a two-state control, so it carries a label for each state.
    // The tools follow the sidebar tab: the Features tab selects, copies and
    // pastes features only, every other tab works on terrain tiles only.
    const bool selectActive = featuresTab_ ? featureSelectMode_ : selectionMode_;
    if (theme::toolbarItem(
            "select",
            ToolIcon::Select,
            selectActive ? text(Text::Selecting) : text(Text::Select),
            selectActive,
            text(Text::Selecting))) {
        toggleSelectMode();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", featuresTab_ ? text(Text::TipSelectFeatures) : text(Text::TipSelectTiles));
    }
    theme::toolbarSeparator();

    const bool canCopy = featuresTab_ ? !selectedFeatures_.empty() : hasSelection();
    ImGui::BeginDisabled(!map_ || !canCopy);
    if (theme::toolbarItem("cut", ToolIcon::Cut, text(Text::Cut))) {
        if (featuresTab_) {
            copyFeatures(true);
        } else {
            cutSelection();
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", featuresTab_ ? text(Text::TipCutFeatures) : text(Text::TipCutTiles));
    }
    next();
    if (theme::toolbarItem("copy", ToolIcon::Copy, text(Text::Copy))) {
        if (featuresTab_) {
            copyFeatures(false);
        } else {
            copySelection(false);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", featuresTab_ ? text(Text::TipCopyFeatures) : text(Text::TipCopyTerrain));
    }
    ImGui::EndDisabled();
    if (!featuresTab_) {
        next();
        ImGui::BeginDisabled(!map_ || !hasSelection());
        if (theme::toolbarItem("copy-heights", ToolIcon::CopyHeights, text(Text::CopyHeights))) {
            copySelection(true);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", text(Text::TipCopyHeights));
        }
        ImGui::EndDisabled();
    }
    next();
    const bool canPaste = featuresTab_ ? !featureClipboard_.empty() : clipboard_.has_value();
    const bool pasting = featuresTab_ ? featurePasteArmed_ : pasteArmed_;
    ImGui::BeginDisabled(!map_ || !canPaste);
    if (theme::toolbarItem("paste", ToolIcon::Paste, pasting ? text(Text::Pasting) : text(Text::Paste), pasting, text(Text::Pasting))) {
        if (featuresTab_) {
            armFeaturePaste();
        } else {
            armPaste();
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", featuresTab_ ? text(Text::TipPasteFeatures) : text(Text::TipPasteTiles));
    }
    ImGui::EndDisabled();
    theme::toolbarSeparator();

    ImGui::BeginDisabled(!map_ || savingMap());
    if (theme::toolbarItem("auto-metal", ToolIcon::AutoMetal, text(Text::AutoMetal))) {
        autoMetal();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text(Text::TipAutoMetal));
    }
    ImGui::EndDisabled();
    next();

    ImGui::BeginDisabled(!map_ || !hasSelection() || featuresTab_);
    if (theme::toolbarItem("modify-height", ToolIcon::ModifyHeight, text(Text::ModifyHeight))) {
        showModifyHeight_ = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text(Text::TipModifyHeight));
    }
    next();
    if (theme::toolbarItem("blend", ToolIcon::Blend, text(Text::Blend))) {
        blendSelection();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text(Text::TipBlend));
    }
    ImGui::EndDisabled();
    theme::toolbarSeparator();

    ImGui::BeginDisabled(!map_);
    if (theme::toolbarItem("view-3d", ToolIcon::View3D, text(Text::View3d), view3d_)) {
        set3dView(!view3d_);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", view3d_ ? text(Text::TipFlatMap) : text(Text::TipView3d));
    }
    ImGui::EndDisabled();
    next();
    if (theme::toolbarItem(
            "height-map",
            ToolIcon::HeightMap,
            showHeightGrid_ ? text(Text::HideHeights) : text(Text::ShowHeights),
            showHeightGrid_,
            text(Text::ShowHeights))) {
        showHeightGrid_ = !showHeightGrid_;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", showHeightGrid_ ? text(Text::TipHideGrid) : text(Text::TipShowGrid));
    }
    theme::toolbarSeparator();

    ImGui::BeginDisabled(!map_ || undo_.empty());
    if (theme::toolbarItem("undo", ToolIcon::Undo, text(Text::Undo))) {
        undoPlacement();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text(Text::TipUndo));
    }
    ImGui::EndDisabled();
    next();
    ImGui::BeginDisabled(!map_ || redo_.empty());
    if (theme::toolbarItem("redo", ToolIcon::Redo, text(Text::Redo))) {
        redoPlacement();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text(Text::TipRedo));
    }
    ImGui::EndDisabled();
    theme::toolbarSeparator();

    const std::string opacityLabel = formatText(Text::OpacityLabel, {std::to_string(placeOpacity_)});
    const std::string opacityWidth = formatText(Text::OpacityLabel, {"100"});
    if (theme::toolbarItem("opacity", ToolIcon::Opacity, opacityLabel.c_str(), false, opacityWidth.c_str())) {
        ImGui::OpenPopup("opacity-menu");
    }
    const ImVec2 opacityMin = ImGui::GetItemRectMin();
    const ImVec2 opacityMax = ImGui::GetItemRectMax();
    float usedRight = opacityMax.x;
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("opacity-menu")) {
        ImGui::SetTooltip("%s", text(Text::TipOpacity));
    }
    ImGui::SetNextWindowPos(ImVec2(opacityMin.x, opacityMax.y + 2.0f));
    if (ImGui::BeginPopup("opacity-menu")) {
        for (int value = 5; value <= 100; value += 5) {
            const std::string choice = std::to_string(value) + "%";
            if (ImGui::MenuItem(choice.c_str(), nullptr, placeOpacity_ == value)) {
                placeOpacity_ = value;
            }
        }
        ImGui::EndPopup();
    }
    next();
    const int heightDelta = placeHeightDelta();
    const std::string heightNumber = heightDelta > 0 ? "+" + std::to_string(heightDelta) : (heightDelta == 0 ? "+0" : std::to_string(heightDelta));
    const std::string heightLabel = formatText(Text::HeightOffsetLabel, {heightNumber});
    const std::string heightWidth = formatText(Text::HeightOffsetLabel, {"-255"});
    if (theme::toolbarItem("height-offset", ToolIcon::HeightOffset, heightLabel.c_str(), heightDelta != 0, heightWidth.c_str())) {
        placeHeightDraftAdd_ = placeHeightAdd_;
        placeHeightDraftAmount_ = placeHeightAmount_;
        showPlaceHeight_ = true;
    }
    usedRight = ImGui::GetItemRectMax().x;
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text(Text::TipHeightOffset));
    }

    if (featuresTab_) {
        theme::toolbarSeparator();
        ImGui::BeginDisabled(!map_);
        if (theme::toolbarItem("delete", ToolIcon::Delete, featureDelete_ ? text(Text::Deleting) : text(Text::Delete), featureDelete_, text(Text::Deleting))) {
            toggleDeleteMode();
        }
        ImGui::EndDisabled();
        usedRight = ImGui::GetItemRectMax().x;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", text(Text::TipDelete));
        }
    }

    if (!brushName_.empty()) {
        theme::toolbarSeparator();
        const float topY = ImGui::GetCursorPosY();
        const float frameHeight = ImGui::GetFrameHeight();
        ImGui::PushFont(theme::toolbarLabelFont());
        const float captionWidth = ImGui::CalcTextSize(text(Text::Brush)).x;
        const float captionHeight = ImGui::GetTextLineHeight();
        ImGui::PopFont();
        const float comboWidth = std::max(std::floor(88.0f * theme::unit()), captionWidth);
        const float blockHeight = frameHeight + captionHeight + 2.0f;
        const float blockX = ImGui::GetCursorPosX();
        ImGui::BeginGroup();
        ImGui::SetCursorPos(ImVec2(blockX, topY + std::floor((theme::toolbarItemHeight() - blockHeight) * 0.5f)));
        ImGui::SetNextItemWidth(comboWidth);
        // Rivers, ridges and slopes always fill one 16 x 16 cell, so the size is fixed for them.
        // A hill keeps the size named on its button.
        const int hillTiles = hillSpan();
        const bool fixedSize = lineBrushArmed() || hillTiles > 0;
        const std::string currentSize = mountainArmed()
            ? (mountainVertical() ? brushRectLabel(mountainSpan(), mountainSpan() * 2) : brushRectLabel(mountainSpan() * 2, mountainSpan()))
            : hillTiles > 0 ? brushRectLabel(hillTiles, hillSpanHigh())
            : brushSizeLabel(fixedSize ? kBrushCellTiles : brushSize_);
        if (fixedSize) {
            ImGui::BeginDisabled();
        }
        if (ImGui::BeginCombo("##brush-size", currentSize.c_str())) {
            for (const int size : kBrushSizes) {
                const std::string label = brushSizeLabel(size);
                if (ImGui::Selectable(label.c_str(), brushSize_ == size)) {
                    brushSize_ = size;
                }
            }
            ImGui::EndCombo();
        }
        if (fixedSize) {
            ImGui::EndDisabled();
        }
        const bool sizeHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        ImGui::PushFont(theme::toolbarLabelFont());
        ImGui::SetCursorPosX(blockX + std::max(0.0f, (comboWidth - captionWidth) * 0.5f));
        ImGui::TextUnformatted(text(Text::Brush));
        ImGui::PopFont();
        ImGui::EndGroup();
        usedRight = std::max(usedRight, ImGui::GetItemRectMax().x);
        if (sizeHovered && !ImGui::IsPopupOpen("##brush-size")) {
            const char* sizeTip = fixedSize ? text(Text::TipBrushFixed) : text(Text::TipBrushSize);
            std::string groupTip;
            if (hillTiles > 0 && duneArmed()) {
                sizeTip = "Dunes-80 paints one 16 x 16 cell at a time";
            } else if (hillTiles > 0 && mountainArmed()) {
                sizeTip = reefArmed()
                    ? (mountainVertical() ? "Vertical Reef starts at 16 x 32 and grows 16 tiles at a time as you drag up or down"
                        : "Horizontal Reef starts at 32 x 16 and grows 16 tiles at a time as you drag left or right")
                    : mountainVertical() ? "Vertical Mtn starts at 32 x 64 and grows 32 tiles at a time as you drag up or down"
                    : "Horizontal Mtn starts at 64 x 32 and grows 32 tiles at a time as you drag left or right";
            } else if (const HillSection* group = hillTiles > 0 ? groupHill() : nullptr) {
                groupTip = brushVaries()
                    ? "The size of the next " + group->noun + ". Press N to skip to another one"
                    : brushName_ + " fills one " + brushRectLabel(hillTiles, hillSpanHigh()) + " Base-" + std::to_string(group->baseHeight) + " block";
                sizeTip = groupTip.c_str();
            } else if (hillTiles > 0) {
                const bool rough = brushName_.rfind("Rough", 0) == 0;
                const bool island = brushName_ == "Island 8x8";
                const bool lavaHill = brushName_.rfind("Hill-86", 0) == 0;
                const bool pool = brushName_ == "Pools 8x8";
                const bool archipelagoHill = brushName_.rfind("Hill-194", 0) == 0;
                const int seaIsland = seaIslandKind(brushName_);
                sizeTip = island
                    ? "Island 8x8 places one 16 x 16 island on GreenWorld Base-0"
                    : brushName_ == "Feature-85"
                    ? "Feature-85 fills one 16 x 16 Mars Base-85 block"
                    : brushName_ == "Rough-85 16x16"
                    ? "Rough-85 16x16 fills one 16 x 16 Mars Base-85 block"
                    : pool85Brush(brushName_)
                    ? (hillTiles >= kBrushCellTiles ? "Fills one 16 x 16 Base-85 block with a pool"
                        : "Fills one 8 x 8 Base-85 block with a pool")
                    : brushName_.rfind("Hills-85", 0) == 0
                    ? (hillTiles >= kBrushCellTiles ? "Hills-85 16x16 fills one 16 x 16 Base-85 block"
                        : "Hills-85 8x8 fills one 8 x 8 Base-85 block")
                    : brushName_.rfind("Pool-86", 0) == 0
                    ? (hillTiles > kBrushCellTiles ? "Pool-86 32x32 fills one 32 x 32 Base-86 block"
                        : hillTiles == kBrushCellTiles ? "Pool-86 16x16 fills one 16 x 16 Base-86 block"
                        : "Pool-86 8x8 fills one 8 x 8 Base-86 block")
                    : seaIsland == 5 ? "Island-0 16x16 fills one 16 x 16 Base-0 block"
                    : seaIsland == 6 ? "Island-0 32x32 fills one 32 x 32 Base-0 block"
                    : seaIsland == 1 ? "Island-0 8x8 fills one 8 x 8 Base-0 block"
                    : seaIsland == 2 ? "Rocky-0 8x8 fills one 8 x 8 Base-0 block"
                    : seaIslandVaries(seaIsland) ? "The size of the next island. Press N to skip to another island"
                    : archipelagoHill
                    ? "Hill-194 16x16 fills one 16 x 16 Base-194 block"
                    : brushName_.rfind("Hills-78", 0) == 0
                    ? (hillTiles >= kBrushCellTiles
                        ? "Hills-78 16x16 fills one 16 x 16 Base-78 block"
                        : "Hills-78 8x8 fills one 8 x 8 Base-78 block")
                    : (pool
                        ? "Pools 8x8 fills one 8 x 8 Base-77 block"
                        : (rough
                        ? "Rough-85 fills one 8 x 8 Base-85 block"
                        : (lavaHill
                            ? (hillTiles >= kBrushCellTiles
                                ? "Hill-86 16x16 fills one 16 x 16 Base-86 block"
                                : "Hill-86 8x8 fills one 8 x 8 Base-86 block")
                            : (hillTiles >= kBrushCellTiles
                                ? "Hill-85 16x16 fills one 16 x 16 Base-85 block"
                                : "Hill-85 8x8 fills one 8 x 8 Base-85 block"))));
            }
            ImGui::SetTooltip("%s", sizeTip);
        }
    }

    // The HPI editor sits at the right edge, apart from the map tools.
    {
        ImGui::PushFont(theme::toolbarLabelFont());
        const float labelWidth = ImGui::CalcTextSize(text(Text::HpiEditor)).x;
        ImGui::PopFont();
        const float itemWidth = std::floor(std::max(50.0f * theme::unit(), labelWidth + 16.0f * theme::unit()));
        // The sidebar toggle sits at the far right, as Xcode keeps its panel buttons, with the HPI
        // editor just before it.
        const float toggleWidth = std::floor(34.0f * theme::unit());
        const float rightEdge = ImGui::GetWindowContentRegionMax().x - toggleWidth - gap - itemWidth;
        const float fixedEnd = usedRight - ImGui::GetWindowPos().x + gap;
        ImGui::SameLine(std::max(rightEdge, fixedEnd));
        if (theme::toolbarItem("hpi-editor", ToolIcon::HpiEditor, text(Text::HpiEditor), hpiEditor_.visible())) {
            hpiEditor_.show();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", text(Text::TipHpi));
        }
        ImGui::SameLine(0.0f, gap);
        const float rowTop = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(rowTop + std::floor((theme::toolbarItemHeight() - ImGui::GetFrameHeight()) * 0.5f));
        static const theme::GlyphPainter kToggleGlyphs[] = {glyphLeftSidebar};
        const std::string toggleTip = std::string(showSidebar_ ? text(Text::HideSidebar) : text(Text::ShowSidebar)) + "  " + theme::shortcut("0");
        const char* toggleTips[] = {toggleTip.c_str()};
        if (theme::iconSegmented("sidebar-toggle", kToggleGlyphs, 1, showSidebar_ ? 0 : -1, nullptr, toggleTips) == 0) {
            showSidebar_ = !showSidebar_;
        }
    }
}

void EditorApp::requestCategory(std::size_t archiveIndex, const std::string& world, const std::string& category) {
    const std::string key = categoryKey(archiveIndex, world, category);
    CategoryPages& page = categories_[key];
    if (page.complete) {
        return;
    }
    for (const CategoryRequest& job : loadQueue_) {
        if (job.key == key) {
            return;
        }
    }

    CategoryRequest job;
    job.key = key;
    job.archiveIndex = archiveIndex;
    job.world = world;
    job.category = category;
    if (archiveIndex < tilesets_.archives().size()) {
        for (const TilesetSection& section : tilesets_.archives()[archiveIndex].sections) {
            if (section.world == world && section.category == category) {
                job.sections.push_back(section);
            }
        }
    }
    std::sort(job.sections.begin(), job.sections.end(), [](const TilesetSection& a, const TilesetSection& b) {
        return nameLess(a.name, b.name);
    });
    page.total = static_cast<int>(job.sections.size());
    if (job.sections.empty()) {
        page.complete = true;
        return;
    }
    loadQueue_.push_back(std::move(job));
    pumpSectionLoads();
}

void EditorApp::pumpSectionLoads() {
    constexpr int kBatch = 8;
    int loadedThisFrame = 0;
    while (!loadQueue_.empty() && loadedThisFrame < kBatch) {
        CategoryRequest& job = loadQueue_.front();
        if (job.next >= job.sections.size()) {
            const std::string key = job.key;
            categories_[key].complete = true;
            loadQueue_.pop_front();
            continue;
        }

        const TilesetSection info = job.sections[job.next];
        const std::string key = job.key;
        const std::size_t archiveIndex = job.archiveIndex;
        ++job.next;
        ++loadedThisFrame;

        try {
            if (archiveIndex >= tilesets_.archives().size()) {
                throw std::runtime_error("Tileset archive is no longer loaded");
            }
            const HpiArchive& archive = tilesets_.archives()[archiveIndex].archive();
            const HpiFile* file = archive.find(info.archivePath);
            if (file == nullptr) {
                throw std::runtime_error("Section file is missing from the archive");
            }
            const std::vector<std::uint8_t> bytes = archive.extract(*file);
            CachedSection cached;
            cached.world = info.world;
            cached.category = info.category;
            cached.name = info.name;
            cached.stamp = loadSectionStamp(bytes);
            const std::vector<std::uint8_t> preview = renderMinimapRgba(cached.stamp);
            cached.preview = uploadTexture(preview.data(), kSectionMinimap, kSectionMinimap, false);
            const int index = static_cast<int>(sections_.size());
            sections_.push_back(std::move(cached));
            categories_[key].sections.push_back(index);
        } catch (const std::exception& ex) {
            CategoryPages& page = categories_[key];
            ++page.failed;
            page.lastError = ex.what();
        }

        if (!loadQueue_.empty() && loadQueue_.front().key == key && loadQueue_.front().next >= loadQueue_.front().sections.size()) {
            categories_[key].complete = true;
            loadQueue_.pop_front();
        }
    }
}

void EditorApp::ensureGhost() {
    if (selectedSection_ < 0 || selectedSection_ >= static_cast<int>(sections_.size())) {
        destroyTexture(ghostTexture_);
        ghostFor_ = -1;
        return;
    }
    if (ghostFor_ == selectedSection_ && ghostTexture_ != 0) {
        return;
    }
    destroyTexture(ghostTexture_);
    const SectionStamp& stamp = sections_[static_cast<std::size_t>(selectedSection_)].stamp;
    const std::vector<std::uint8_t> rgba = renderSectionRgba(stamp, 2048, ghostW_, ghostH_);
    ghostTexture_ = uploadTexture(rgba.data(), ghostW_, ghostH_, true);
    ghostFor_ = selectedSection_;
}

const SectionStamp* EditorApp::placementStamp() const {
    if (!map_ || startLocationsTab_ || featuresTab_ || !brushName_.empty()) {
        return nullptr;
    }
    if (pasteArmed_ && clipboard_) {
        return &*clipboard_;
    }
    if (selectionMode_) {
        return nullptr;
    }
    if (selectedSection_ < 0 || selectedSection_ >= static_cast<int>(sections_.size())) {
        return nullptr;
    }
    return &sections_[static_cast<std::size_t>(selectedSection_)].stamp;
}

void EditorApp::adjustPlaceOffset(int dx, int dy) {
    const SectionStamp* stamp = placementStamp();
    if (stamp == nullptr || map_->tileWidth <= 0 || map_->tileHeight <= 0) {
        return;
    }
    const int originX = std::clamp(hoverTileX_ + placeOffsetX_ + dx, 1 - stamp->tileWidth, map_->tileWidth - 1);
    const int originY = std::clamp(hoverTileY_ + placeOffsetY_ + dy, 1 - stamp->tileHeight, map_->tileHeight - 1);
    placeOffsetX_ = originX - hoverTileX_;
    placeOffsetY_ = originY - hoverTileY_;
}

void EditorApp::setStartPosition(int player, int x, int y) {
    if (!map_ || player < 1 || player > 10) {
        return;
    }
    x = std::clamp(x, 0, std::max(0, map_->pixelWidth() - 1));
    y = std::clamp(y, 0, std::max(0, map_->pixelHeight() - 1));
    for (StartPosition& start : map_->starts) {
        if (start.player != player) {
            continue;
        }
        if (start.x == x && start.y == y) {
            return;
        }
        start.x = x;
        start.y = y;
        markMapDirty();
        return;
    }
    map_->starts.push_back(StartPosition{player, x, y});
    markMapDirty();
}

void EditorApp::clearStartPosition(int player) {
    if (!map_) {
        return;
    }
    const auto before = map_->starts.size();
    std::erase_if(map_->starts, [&](const StartPosition& start) { return start.player == player; });
    if (map_->starts.size() != before) {
        markMapDirty();
        statusNote_ = "Removed player " + std::to_string(player);
    }
}

void EditorApp::placeArmedFeature(int cellX, int cellY) {
    if (!map_ || armedFeature_.empty()) {
        return;
    }
    FeatureChange change;
    switch (placeMapFeature(*map_, cellX, cellY, armedFeature_, &change)) {
    case FeatureEdit::Placed:
        rememberFeature(std::move(change));
        statusNote_ = armedFeature_ + " at cell " + std::to_string(cellX) + ", " + std::to_string(cellY);
        break;
    case FeatureEdit::OpenSpace:
        statusNote_ = "That cell is open space";
        break;
    case FeatureEdit::Full:
        error_ = "This map already lists every feature name Total Annihilation can store.";
        break;
    case FeatureEdit::Removed:
    case FeatureEdit::Unchanged:
        break;
    }
}

const EditorApp::FeatureSprite* EditorApp::featurePicture(const std::string& name) {
    const std::string key = lowerText(name);
    if (const auto found = featureSprites_.find(key); found != featureSprites_.end()) {
        return &found->second;
    }
    FeatureSprite sprite;
    const TilesetArchive* owner = nullptr;
    const FeatureInfo* info = nullptr;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        for (const FeatureInfo& feature : archive.features) {
            if (lowerText(feature.name) == key) {
                owner = &archive;
                info = &feature;
                break;
            }
        }
        if (info != nullptr) {
            break;
        }
    }
    if (info != nullptr) {
        sprite.footprintX = std::max(1, info->footprintX);
        sprite.footprintZ = std::max(1, info->footprintZ);
        sprite.metal = info->metal;
        sprite.energy = info->energy;
        std::optional<GafImage> image;
        if (owner != nullptr) {
            image = imageFromArchive(*owner, *info);
        }
        if (!image) {
            for (const TilesetArchive& archive : tilesets_.archives()) {
                if (&archive == owner) {
                    continue;
                }
                image = imageFromArchive(archive, *info);
                if (image) {
                    break;
                }
            }
        }
        if (image && image->width > 0 && image->height > 0 && !image->pixels.empty()) {
            bool opaque = false;
            for (std::uint8_t pixel : image->pixels) {
                if (pixel != image->transparency) {
                    opaque = true;
                    break;
                }
            }
            if (opaque) {
                const std::vector<std::uint8_t> rgba = gafImageRgba(*image);
                sprite.texture = uploadTexture(rgba.data(), image->width, image->height, true);
                sprite.width = image->width;
                sprite.height = image->height;
                sprite.hotspotX = image->hotspotX;
                sprite.hotspotY = image->hotspotY;
            }
        }
    }
    const auto inserted = featureSprites_.emplace(key, sprite);
    return &inserted.first->second;
}

bool EditorApp::featurePictureBounds(const std::string& name, int cellX, int cellY, float& x0, float& y0, float& x1, float& y1) {
    const FeatureSprite* picture = featurePicture(name);
    if (picture == nullptr || picture->texture == 0 || !map_) {
        return false;
    }
    const float anchorX = static_cast<float>(cellX * 16 + picture->footprintX * 8);
    const float anchorY = static_cast<float>(cellY * 16 + picture->footprintZ * 8 - featureLift(*map_, cellX, cellY));
    x0 = anchorX - static_cast<float>(picture->hotspotX);
    y0 = anchorY - static_cast<float>(picture->hotspotY);
    x1 = x0 + static_cast<float>(picture->width);
    y1 = y0 + static_cast<float>(picture->height);
    return true;
}

void EditorApp::placeSelected(int tileX, int tileY) {
    if (!map_ || selectedSection_ < 0 || selectedSection_ >= static_cast<int>(sections_.size())) {
        return;
    }
    try {
        const int tileCountBefore = map_->tileCount;
        auto change = stampSectionChange(
            *map_,
            sections_[static_cast<std::size_t>(selectedSection_)].stamp,
            tileX,
            tileY,
            &deduper_,
            placeOpacity_,
            placeHeightDelta());
        if (change) {
            rememberPlacement(std::move(*change));
            statusNote_.clear();
            warnIfPastVanillaTiles(tileCountBefore);
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::pushCommand(EditorCommand command, bool editsMap) {
    undo_.push_back(std::move(command));
    constexpr std::size_t kPlacementHistory = 100;
    if (undo_.size() > kPlacementHistory) {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
    if (editsMap) {
        invalidateView();
        markMapDirty();
    }
}

void EditorApp::rememberPlacement(PlacementChange change) {
    EditorCommand command;
    command.terrain = std::move(change);
    pushCommand(std::move(command), true);
}

void EditorApp::rememberPlacements(std::vector<PlacementChange> changes) {
    if (changes.empty()) {
        return;
    }
    EditorCommand command;
    command.terrains = std::move(changes);
    pushCommand(std::move(command), true);
}

void EditorApp::rememberFeature(FeatureChange change) {
    EditorCommand command;
    command.feature = std::move(change);
    pushCommand(std::move(command), false);
    markMapDirty();
}

void EditorApp::rememberFeatures(std::vector<FeatureChange> changes) {
    if (changes.empty()) {
        return;
    }
    EditorCommand command;
    command.features = std::move(changes);
    pushCommand(std::move(command), false);
    markMapDirty();
}

std::vector<FeaturePlacement> EditorApp::selectedFeaturePlacements() {
    std::vector<FeaturePlacement> found;
    if (!map_ || selectedFeatures_.empty()) {
        return found;
    }
    std::set<std::pair<int, int>> kept;
    for (const FeaturePlacement& feature : map_->features) {
        if (selectedFeatures_.count({feature.x, feature.y}) != 0 && kept.insert({feature.x, feature.y}).second) {
            found.push_back(feature);
        }
    }
    selectedFeatures_ = std::move(kept);
    return found;
}

void EditorApp::copyFeatures(bool cut) {
    const std::vector<FeaturePlacement> items = selectedFeaturePlacements();
    if (items.empty()) {
        return;
    }
    int left = items.front().x;
    int top = items.front().y;
    for (const FeaturePlacement& item : items) {
        left = std::min(left, item.x);
        top = std::min(top, item.y);
    }
    featureClipboard_.clear();
    for (const FeaturePlacement& item : items) {
        featureClipboard_.push_back(FeatureClip{item.name, item.x - left, item.y - top});
    }
    featureClipboardNewer_ = true;
    const std::string count = std::to_string(items.size()) + (items.size() == 1 ? " feature" : " features");
    if (cut) {
        deleteSelectedFeatures();
        statusNote_ = "Cut " + count;
    } else {
        statusNote_ = "Copied " + count + ". Paste keeps their positions relative to each other";
    }
    error_.clear();
}

void EditorApp::deleteSelectedFeatures() {
    if (!map_) {
        return;
    }
    const std::vector<FeaturePlacement> items = selectedFeaturePlacements();
    std::vector<FeatureChange> changes;
    for (const FeaturePlacement& item : items) {
        FeatureChange change;
        if (removeMapFeature(*map_, item.x, item.y, &change) == FeatureEdit::Removed) {
            changes.push_back(change);
        }
    }
    selectedFeatures_.clear();
    if (!changes.empty()) {
        statusNote_ = "Removed " + std::to_string(changes.size()) + (changes.size() == 1 ? " feature" : " features");
        rememberFeatures(std::move(changes));
    }
}

void EditorApp::armFeaturePaste() {
    if (!map_ || featureClipboard_.empty()) {
        return;
    }
    featurePasteArmed_ = true;
    featureSelectMode_ = false;
    featureDelete_ = false;
    armedFeature_.clear();
    statusNote_ = "Click the map to paste " + std::to_string(featureClipboard_.size())
        + (featureClipboard_.size() == 1 ? " feature" : " features") + ". Esc cancels";
    error_.clear();
}

void EditorApp::pasteFeaturesAt(int cellX, int cellY) {
    if (!map_ || featureClipboard_.empty()) {
        return;
    }
    std::vector<FeatureChange> changes;
    std::set<std::pair<int, int>> pasted;
    int skipped = 0;
    for (const FeatureClip& item : featureClipboard_) {
        const int x = cellX + item.dx;
        const int y = cellY + item.dy;
        FeatureChange change;
        switch (placeMapFeature(*map_, x, y, item.name, &change)) {
        case FeatureEdit::Placed:
            changes.push_back(change);
            pasted.insert({x, y});
            break;
        case FeatureEdit::Unchanged:
            if (x >= 0 && y >= 0 && x < map_->heightWidth() && y < map_->heightHeight()) {
                pasted.insert({x, y});
            } else {
                ++skipped;
            }
            break;
        case FeatureEdit::OpenSpace:
        case FeatureEdit::Removed:
            ++skipped;
            break;
        case FeatureEdit::Full:
            error_ = "This map already lists every feature name Total Annihilation can store.";
            ++skipped;
            break;
        }
    }
    if (!changes.empty() || !pasted.empty()) {
        selectedFeatures_ = std::move(pasted);
    }
    const std::size_t placed = changes.size();
    rememberFeatures(std::move(changes));
    statusNote_ = "Pasted " + std::to_string(placed) + (placed == 1 ? " feature" : " features");
    if (skipped > 0) {
        statusNote_ += ", skipped " + std::to_string(skipped) + " off the map or on open space";
    }
}

void EditorApp::rememberSelection(const TileSelection& before, const TileSelection& after) {
    if (sameTileSelection(before, after)) {
        return;
    }
    EditorCommand command;
    command.selection = true;
    command.selectionBefore = before;
    command.selectionAfter = after;
    pushCommand(std::move(command), false);
}

bool EditorApp::clampedSelection(int x0, int y0, int x1, int y1, int& left, int& top, int& right, int& bottom) const {
    if (!map_ || map_->tileWidth <= 0 || map_->tileHeight <= 0) {
        return false;
    }
    left = std::clamp(std::min(x0, x1), 0, map_->tileWidth - 1);
    top = std::clamp(std::min(y0, y1), 0, map_->tileHeight - 1);
    right = std::clamp(std::max(x0, x1), 0, map_->tileWidth - 1);
    bottom = std::clamp(std::max(y0, y1), 0, map_->tileHeight - 1);
    return true;
}

void EditorApp::previewSelection(int x0, int y0, int x1, int y1) {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    if (!clampedSelection(x0, y0, x1, y1, left, top, right, bottom)) {
        selection_ = {};
        return;
    }
    if (selectOp_ == SelectOp::Add) {
        selection_ = tileSelectionAdd(selectionBefore_, left, top, right, bottom);
    } else if (selectOp_ == SelectOp::Subtract) {
        selection_ = tileSelectionSubtract(selectionBefore_, left, top, right, bottom);
    } else {
        selection_ = tileSelectionRect(left, top, right, bottom);
    }
}

void EditorApp::commitSelection() {
    rememberSelection(selectionBefore_, selection_);
}

void EditorApp::clearMapSelection() {
    selection_ = {};
    selectionBefore_ = {};
    selecting_ = false;
    pasteArmed_ = false;
}

void EditorApp::copySelection(bool heightsOnly) {
    if (!map_ || !hasSelection()) {
        return;
    }
    try {
        clipboard_ = copyTileSelection(*map_, selection_, heightsOnly);
        featureClipboardNewer_ = false;
        destroyTexture(clipboardGhost_);
        clipboardGhostW_ = 0;
        clipboardGhostH_ = 0;
        const std::string size = selectionSummary(selection_);
        statusNote_ = heightsOnly ? "Copied height map " + size : "Copied terrain and height " + size;
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::cutSelection() {
    if (!map_ || !hasSelection()) {
        return;
    }
    try {
        clipboard_ = copyTileSelection(*map_, selection_);
        featureClipboardNewer_ = false;
        destroyTexture(clipboardGhost_);
        clipboardGhostW_ = 0;
        clipboardGhostH_ = 0;
        const std::string size = selectionSummary(selection_);
        rememberPlacement(clearTileSelection(*map_, selection_));
        statusNote_ = "Cut " + size;
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::blendSelection() {
    if (!map_ || !hasSelection()) {
        return;
    }
    try {
        const int tileCountBefore = map_->tileCount;
        auto change = blendSelectionTextures(*map_, selection_, &deduper_);
        if (change) {
            rememberPlacement(std::move(*change));
            statusNote_ = "Blended " + selectionSummary(selection_);
            warnIfPastVanillaTiles(tileCountBefore);
        } else {
            statusNote_ = "Nothing to blend";
        }
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::armPaste() {
    if (!map_ || !clipboard_) {
        return;
    }
    brushName_.clear();
    selectionMode_ = false;
    selecting_ = false;
    pasteArmed_ = true;
    const std::string size = std::to_string(clipboard_->tileWidth) + " x " + std::to_string(clipboard_->tileHeight);
    statusNote_ = clipboard_->heightsOnly
        ? "Drag to pan, then click to paste the height map " + size
        : "Drag to pan, then click to paste " + size;
    error_.clear();
}

void EditorApp::pasteAt(int tileX, int tileY) {
    if (!map_ || !clipboard_) {
        return;
    }
    try {
        const int tileCountBefore = map_->tileCount;
        auto change = stampSectionChange(*map_, *clipboard_, tileX, tileY, &deduper_, placeOpacity_, placeHeightDelta());
        if (change) {
            rememberPlacement(std::move(*change));
            const std::string size = std::to_string(clipboard_->tileWidth) + " x " + std::to_string(clipboard_->tileHeight);
            statusNote_ = clipboard_->heightsOnly ? "Pasted height map " + size : "Pasted " + size;
            error_.clear();
            warnIfPastVanillaTiles(tileCountBefore);
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::ensureClipboardGhost() {
    if (!clipboard_ || clipboard_->heightsOnly) {
        destroyTexture(clipboardGhost_);
        return;
    }
    if (clipboardGhost_ != 0) {
        return;
    }
    const std::vector<std::uint8_t> rgba = renderSectionRgba(*clipboard_, 2048, clipboardGhostW_, clipboardGhostH_);
    clipboardGhost_ = uploadTexture(rgba.data(), clipboardGhostW_, clipboardGhostH_, true);
}

void EditorApp::undoPlacement() {
    if (!map_ || undo_.empty()) {
        return;
    }
    try {
        if (!undo_.back().terrains.empty()) {
            for (auto it = undo_.back().terrains.rbegin(); it != undo_.back().terrains.rend(); ++it) {
                revertPlacement(*map_, *it);
            }
            deduper_ = {};
            invalidateView();
            markMapDirty();
            statusNote_ = "Undid terrain edit";
        } else if (undo_.back().terrain) {
            revertPlacement(*map_, *undo_.back().terrain);
            deduper_ = {};
            invalidateView();
            markMapDirty();
            statusNote_ = "Undid terrain edit";
        }
        if (undo_.back().feature) {
            revertFeatureChange(*map_, *undo_.back().feature);
            markMapDirty();
            if (!undo_.back().terrain) {
                statusNote_ = "Undid feature edit";
            }
        }
        if (!undo_.back().features.empty()) {
            const auto& batch = undo_.back().features;
            for (auto it = batch.rbegin(); it != batch.rend(); ++it) {
                revertFeatureChange(*map_, *it);
            }
            markMapDirty();
            if (!undo_.back().terrain) {
                statusNote_ = "Undid feature edit";
            }
        }
        if (undo_.back().featureBatch) {
            revertFeatureBatch(*map_, *undo_.back().featureBatch);
            selectedFeatures_.clear();
            markMapDirty();
        }
        if (undo_.back().startsBefore) {
            map_->starts = *undo_.back().startsBefore;
            draggingStartPlayer_ = 0;
            markMapDirty();
        }
        if (undo_.back().selection) {
            selection_ = undo_.back().selectionBefore;
            if (!undo_.back().terrain) {
                statusNote_ = "Undid selection";
            }
        }
        selecting_ = false;
        redo_.push_back(std::move(undo_.back()));
        undo_.pop_back();
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::redoPlacement() {
    if (!map_ || redo_.empty()) {
        return;
    }
    try {
        const int tileCountBefore = map_->tileCount;
        if (!redo_.back().terrains.empty()) {
            for (const PlacementChange& change : redo_.back().terrains) {
                reapplyPlacement(*map_, change);
            }
            deduper_ = {};
            invalidateView();
            markMapDirty();
            statusNote_ = "Redid terrain edit";
            warnIfPastVanillaTiles(tileCountBefore);
        } else if (redo_.back().terrain) {
            reapplyPlacement(*map_, *redo_.back().terrain);
            deduper_ = {};
            invalidateView();
            markMapDirty();
            statusNote_ = "Redid terrain edit";
            warnIfPastVanillaTiles(tileCountBefore);
        }
        if (redo_.back().feature) {
            reapplyFeatureChange(*map_, *redo_.back().feature);
            markMapDirty();
            if (!redo_.back().terrain) {
                statusNote_ = "Redid feature edit";
            }
        }
        if (!redo_.back().features.empty()) {
            for (const FeatureChange& change : redo_.back().features) {
                reapplyFeatureChange(*map_, change);
            }
            markMapDirty();
            if (!redo_.back().terrain) {
                statusNote_ = "Redid feature edit";
            }
        }
        if (redo_.back().featureBatch) {
            reapplyFeatureBatch(*map_, *redo_.back().featureBatch);
            selectedFeatures_.clear();
            markMapDirty();
        }
        if (redo_.back().startsAfter) {
            map_->starts = *redo_.back().startsAfter;
            draggingStartPlayer_ = 0;
            markMapDirty();
        }
        if (redo_.back().selection) {
            selection_ = redo_.back().selectionAfter;
            if (!redo_.back().terrain) {
                statusNote_ = "Redid selection";
            }
        }
        selecting_ = false;
        undo_.push_back(std::move(redo_.back()));
        redo_.pop_back();
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::clearHistory() {
    undo_.clear();
    redo_.clear();
    selectedFeatures_.clear();
    featurePasteArmed_ = false;
    featureMarquee_ = false;
}

void EditorApp::drawStartLocations() {
    theme::helpText("Press 1-9 for that player, or 0 for Player 10. Click the map to place the pin. Drag a pin to move it. Right-click a pin to remove it.");
    if (!map_) {
        ImGui::TextWrapped("Open a map before placing start locations.");
    }
    if (!map_) {
        ImGui::BeginDisabled();
    }
    struct PlacedStart {
        bool present = false;
        int x = 0;
        int y = 0;
    };
    PlacedStart placed[11];
    if (map_) {
        for (const StartPosition& start : map_->starts) {
            if (start.player >= 1 && start.player <= 10 && !placed[start.player].present) {
                placed[start.player] = PlacedStart{true, start.x, start.y};
            }
        }
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 muted = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float textHeight = ImGui::GetTextLineHeight();
    const float rowHeight = std::max(52.0f, textHeight * 2.0f + 20.0f);
    for (int player = 1; player <= 10; ++player) {
        ImGui::PushID(player);
        const bool armed = armedStartPlayer_ == player;
        const bool hasPin = placed[player].present;
        const float clearWidth = hasPin ? 64.0f : 0.0f;
        const float gap = hasPin ? style.ItemSpacing.x : 0.0f;
        const float rowWidth = std::max(48.0f, ImGui::GetContentRegionAvail().x - clearWidth - gap);
        if (ImGui::Selectable("##player", armed, 0, ImVec2(rowWidth, rowHeight))) {
            armedStartPlayer_ = player;
            pasteArmed_ = false;
        }
        if (revealStartPlayer_ == player) {
            ImGui::SetScrollHereY(0.5f);
            revealStartPlayer_ = 0;
        }
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        drawStartPin(draw, ImVec2(rowMin.x + 22.0f, rowMin.y + rowHeight - 8.0f), 1.15f, startColor(player), player, armed);
        const std::string title = "Player " + std::to_string(player);
        draw->AddText(ImVec2(rowMin.x + 48.0f, rowMin.y + 8.0f), textColor, title.c_str());
        const std::string where = hasPin
            ? std::to_string(placed[player].x) + ", " + std::to_string(placed[player].y)
            : "Not placed";
        draw->AddText(ImVec2(rowMin.x + 48.0f, rowMin.y + 10.0f + textHeight), hasPin ? textColor : muted, where.c_str());
        if (hasPin) {
            ImGui::SameLine();
            if (ImGui::Button("Clear", ImVec2(clearWidth, rowHeight))) {
                clearStartPosition(player);
            }
        }
        ImGui::PopID();
    }
    if (!map_) {
        ImGui::EndDisabled();
    }
}

void EditorApp::rebuildTilesetIndex() {
    tilesetIndex_.clear();
    for (std::size_t archiveIndex = 0; archiveIndex < tilesets_.archives().size(); ++archiveIndex) {
        const TilesetArchive& archive = tilesets_.archives()[archiveIndex];
        std::map<std::string, std::map<std::string, std::vector<std::uint32_t>>> grouped;
        for (std::size_t i = 0; i < archive.sections.size(); ++i) {
            const TilesetSection& section = archive.sections[i];
            grouped[section.world][section.category].push_back(static_cast<std::uint32_t>(i));
        }
        TilesetIndexArchive row;
        row.archiveIndex = archiveIndex;
        row.filename = archive.path.filename().string();
        row.favorite = isFavoritesArchiveName(row.filename);
        row.title = row.filename + "  \xC2\xB7  " + std::to_string(archive.sections.size()) + (row.favorite ? " tiles" : " sections");
        for (auto& [world, categories] : grouped) {
            TilesetIndexWorld worldRow;
            worldRow.name = world;
            for (auto& [category, ids] : categories) {
                TilesetIndexCategory categoryRow;
                categoryRow.name = category;
                categoryRow.total = static_cast<int>(ids.size());
                categoryRow.shown = categoryRow.total;
                categoryRow.sectionIds = std::move(ids);
                worldRow.categories.push_back(std::move(categoryRow));
            }
            row.worlds.push_back(std::move(worldRow));
        }
        tilesetIndex_.push_back(std::move(row));
    }
    // An archive added while the editor runs sits at the end of the library, so settle the order here.
    std::stable_sort(tilesetIndex_.begin(), tilesetIndex_.end(), [](const TilesetIndexArchive& a, const TilesetIndexArchive& b) {
        return nameLess(a.filename, b.filename);
    });
    tilesetIndexArchives_ = tilesets_.archives().size();
    tilesetIndexSections_ = tilesets_.sectionCount();
    tilesetQueryApplied_ = "\x01";  // forces the next search pass
}

void EditorApp::applyTilesetSearch(const std::string& query) {
    tilesetQueryApplied_ = query;
    tilesetQueryFolded_ = lowerText(query);
    const std::string& needle = tilesetQueryFolded_;
    const bool searching = !needle.empty();

    // Opening a category loads and decodes every picture in it, so a short
    // query that matches hundreds of categories must not open them all.
    constexpr int kMaxAutoOpenCategories = 6;
    constexpr int kMaxAutoOpenWorlds = 60;
    int matchedCategories = 0;

    for (TilesetIndexArchive& archive : tilesetIndex_) {
        const TilesetArchive& source = tilesets_.archives()[archive.archiveIndex];
        const bool archiveNamed = searching && containsFolded(archive.filename, needle);
        // A new favorites archive has no tiles, but it still belongs on the Favorites tab.
        archive.visible = archive.favorite && !searching && archive.worlds.empty();
        for (TilesetIndexWorld& world : archive.worlds) {
            const bool worldHit = !searching || archiveNamed || containsFolded(world.name, needle);
            world.visible = false;
            world.openOnSearch = false;
            for (TilesetIndexCategory& category : world.categories) {
                category.openOnSearch = false;
                category.filterNames = false;
                category.shown = category.total;
                if (!searching) {
                    category.visible = true;
                    world.visible = true;
                    continue;
                }
                const bool categoryHit = containsFolded(category.name, needle);
                int nameHits = 0;
                if (!worldHit || !categoryHit) {
                    for (const std::uint32_t id : category.sectionIds) {
                        if (containsFolded(source.sections[id].name, needle)) {
                            ++nameHits;
                        }
                    }
                }
                category.visible = worldHit || categoryHit || nameHits > 0;
                if (!category.visible) {
                    continue;
                }
                world.visible = true;
                category.filterNames = !worldHit && !categoryHit;
                category.shown = category.filterNames ? nameHits : category.total;
                if (categoryHit || nameHits > 0) {
                    category.openOnSearch = true;
                    world.openOnSearch = true;
                    ++matchedCategories;
                }
            }
            archive.visible = archive.visible || world.visible;
        }
    }

    if (matchedCategories > kMaxAutoOpenWorlds) {
        for (TilesetIndexArchive& archive : tilesetIndex_) {
            for (TilesetIndexWorld& world : archive.worlds) {
                world.openOnSearch = false;
            }
        }
    }
    if (matchedCategories > kMaxAutoOpenCategories) {
        for (TilesetIndexArchive& archive : tilesetIndex_) {
            for (TilesetIndexWorld& world : archive.worlds) {
                for (TilesetIndexCategory& category : world.categories) {
                    category.openOnSearch = false;
                }
            }
        }
    }
    tilesetQueryOpenPending_ = searching;
}

// Settles on light or dark. While the choice is Auto the system is asked every
// couple of seconds, on a worker thread because some systems answer by running a
// helper program, so a change made in the system settings shows up here shortly.
void EditorApp::updateAppearance() {
    using namespace std::chrono_literals;
    if (appearanceMode_ == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (!systemDarkKnown_) {
            // The first answer is taken at once so the window opens in the right look.
            systemDark_ = systemPrefersDarkMode();
            systemDarkKnown_ = true;
            systemDarkAsked_ = now;
        } else if (systemDarkProbe_.valid()) {
            if (systemDarkProbe_.wait_for(0s) == std::future_status::ready) {
                systemDark_ = systemDarkProbe_.get();
            }
        } else if (now - systemDarkAsked_ >= 2s) {
            systemDarkAsked_ = now;
            systemDarkProbe_ = std::async(std::launch::async, systemPrefersDarkMode);
        }
    }
    const bool wantDark = appearanceMode_ == 2 || (appearanceMode_ == 0 && systemDark_);
    if (!appearanceApplied_ || wantDark != theme::dark()) {
        theme::setDark(wantDark);
        // The glass was tinted for the old look.
        ++viewGeneration_;
        appearanceApplied_ = true;
    }
    if (nativeAppearanceApplied_ != appearanceMode_) {
        applyNativeAppearance(appearanceMode_);
        nativeAppearanceApplied_ = appearanceMode_;
    }
}

// Redraws a panel's glass only when the map behind it has changed. The picture is
// a quarter-size copy of the map under the panel plus a strip around it (so the
// blur has real neighbors at the edges), which keeps the work tiny.
bool EditorApp::updateGlass(GlassPanel& panel, float left, float top, float width, float height, bool force) {
    constexpr float kEdgeStrip = 48.0f;
    constexpr float kPixelsPerGlassPixel = 4.0f;
    if ((!force && (!frostedGlass_ || view3d_)) || !map_ || zoom_ <= 0.0f || panel.blur.failed() || width < 8.0f || height < 8.0f) {
        return false;
    }
    const float regionW = width + kEdgeStrip * 2.0f;
    const float regionH = height + kEdgeStrip * 2.0f;
    const int texW = std::max(2, static_cast<int>(std::lround(regionW / kPixelsPerGlassPixel)));
    const int texH = std::max(2, static_cast<int>(std::lround(regionH / kPixelsPerGlassPixel)));
    // Where the map's top-left corner falls inside the glass region.
    const float panX = panX_ + mapOriginX_ - (left - kEdgeStrip);
    const float panY = panY_ + mapOriginY_ - (top - kEdgeStrip);

    const bool unchanged = panel.blur.texture() != 0
        && panel.generation == viewGeneration_
        && panel.texW == texW
        && panel.texH == texH
        && panel.panX == panX
        && panel.panY == panY
        && panel.zoom == zoom_;
    if (unchanged) {
        return true;
    }

    panel.image.resize(static_cast<std::size_t>(texW) * static_cast<std::size_t>(texH) * 4);
    renderMapView(*map_, panel.image.data(), texW, texH, regionW, regionH, panX, panY, zoom_);

    GlassBlur::Settings settings;
    const theme::GlassStyle& style = theme::glassStyle();
    settings.whiteness = style.amount;
    settings.saturation = style.saturation;
    settings.tintRed = style.tint.x;
    settings.tintGreen = style.tint.y;
    settings.tintBlue = style.tint.z;
    const ImVec4 canvas = ImGui::ColorConvertU32ToFloat4(theme::canvasColor());
    settings.backgroundRed = canvas.x;
    settings.backgroundGreen = canvas.y;
    settings.backgroundBlue = canvas.z;
    if (!panel.blur.update(panel.image.data(), texW, texH, settings)) {
        return false;
    }
    panel.generation = viewGeneration_;
    panel.texW = texW;
    panel.texH = texH;
    panel.panX = panX;
    panel.panY = panY;
    panel.zoom = zoom_;
    return true;
}

bool EditorApp::drawGlass(GlassPanel& panel, float left, float top, float right, float bottom, float rounding) {
    return drawGlass(panel, ImGui::GetWindowDrawList(), left, top, right, bottom, rounding);
}

bool EditorApp::drawGlass(GlassPanel& panel, ImDrawList* draw, float left, float top, float right, float bottom, float rounding, bool force) {
    constexpr float kEdgeStrip = 48.0f;
    const float width = right - left;
    const float height = bottom - top;
    if (draw == nullptr || !updateGlass(panel, left, top, width, height, force)) {
        return false;
    }
    // Show only the middle of the picture; the strip around it was there for the blur.
    const float regionW = width + kEdgeStrip * 2.0f;
    const float regionH = height + kEdgeStrip * 2.0f;
    const ImVec2 uv0(kEdgeStrip / regionW, kEdgeStrip / regionH);
    const ImVec2 uv1((kEdgeStrip + width) / regionW, (kEdgeStrip + height) / regionH);
    if (rounding > 0.0f) {
        draw->AddImageRounded(textureId(panel.blur.texture()), ImVec2(left, top), ImVec2(right, bottom), uv0, uv1, IM_COL32_WHITE, rounding);
    } else {
        draw->AddImage(textureId(panel.blur.texture()), ImVec2(left, top), ImVec2(right, bottom), uv0, uv1);
    }
    return true;
}

void EditorApp::drawTooltipGlass() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiWindow* tooltip = nullptr;
    for (ImGuiWindow* window : g.Windows) {
        if (window == nullptr || (window->Flags & ImGuiWindowFlags_Tooltip) == 0 || window->DrawList == nullptr) {
            continue;
        }
        if (!window->Active || window->Hidden || window->Size.x < 8.0f || window->Size.y < 8.0f) {
            continue;
        }
        tooltip = window;
    }
    if (tooltip == nullptr) {
        return;
    }

    const ImVec2 pos = tooltip->Pos;
    const ImVec2 max(pos.x + tooltip->Size.x, pos.y + tooltip->Size.y);
    ImDrawList* draw = tooltip->DrawList;
    const int indexBefore = draw->IdxBuffer.Size;
    if (!drawGlass(tooltipGlass_, draw, pos.x, pos.y, max.x, max.y, tooltip->WindowRounding)) {
        return;
    }
    draw->AddRect(pos, max, theme::glassStyle().outline, tooltip->WindowRounding);

    // ImGui already filled the tooltip with the flat popup color, and that fill
    // sits in front of anything drawn now. Drop that fill and its border, then
    // move the glass to the front of the list so the words stay on top of it.
    const ImRect& content = tooltip->InnerClipRect;
    for (const ImDrawCmd& command : draw->CmdBuffer) {
        if (command.UserCallback != nullptr || command.ElemCount == 0 || command.IdxOffset >= static_cast<unsigned>(indexBefore)) {
            continue;
        }
        const bool outsideContent = command.ClipRect.x < content.Min.x - 0.5f || command.ClipRect.y < content.Min.y - 0.5f
            || command.ClipRect.z > content.Max.x + 0.5f || command.ClipRect.w > content.Max.y + 0.5f;
        if (!outsideContent) {
            break;
        }
        for (unsigned element = 0; element < command.ElemCount; ++element) {
            const ImDrawIdx index = draw->IdxBuffer[command.IdxOffset + element];
            draw->VtxBuffer[command.VtxOffset + index].col &= ~IM_COL32_A_MASK;
        }
    }

    ImVector<ImDrawCmd> glassCommands;
    for (int i = draw->CmdBuffer.Size - 1; i >= 0; --i) {
        const ImDrawCmd& command = draw->CmdBuffer[i];
        if (command.UserCallback == nullptr && command.ElemCount > 0 && command.IdxOffset >= static_cast<unsigned>(indexBefore)) {
            glassCommands.push_back(command);
            draw->CmdBuffer.erase(draw->CmdBuffer.Data + i);
        }
    }
    for (const ImDrawCmd& command : glassCommands) {
        draw->CmdBuffer.push_front(command);
    }
}

void EditorApp::drawSidebarBackground() {
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    if (!drawGlass(sidebarGlass_, min.x, min.y, min.x + size.x, min.y + size.y, 0.0f)) {
        theme::sidebarBackground();
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(min.x + size.x - 1.0f, min.y), ImVec2(min.x + size.x - 1.0f, min.y + size.y), theme::glassStyle().border);
}

void EditorApp::drawToolbarBackground() {
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    if (!drawGlass(toolbarGlass_, min.x, min.y, max.x, max.y, 0.0f)) {
        theme::toolbarBackground();
        return;
    }
    // A soft sheen from light at the top to a touch of shade at the bottom keeps the bar reading as a bar.
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const theme::GlassStyle& style = theme::glassStyle();
    draw->AddRectFilledMultiColor(min, max, style.sheenTop, style.sheenTop, style.sheenBottom, style.sheenBottom);
    draw->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), style.border);
}

namespace {

// Glyphs for the sidebar's navigator bar. Each is plain line art centered on c, about s points
// across, drawn in one color the bar picks for its state.

// Tilesets: a 2 x 2 grid of map tiles.
void glyphTilesets(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    const float half = s * 0.5f;
    const float gap = std::max(1.5f, s * 0.12f);
    const float cell = (s - gap) * 0.5f;
    const float stroke = std::max(1.0f, s / 11.0f);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            const ImVec2 min(c.x - half + static_cast<float>(col) * (cell + gap), c.y - half + static_cast<float>(row) * (cell + gap));
            draw->AddRect(min, ImVec2(min.x + cell, min.y + cell), color, s * 0.08f, 0, stroke);
        }
    }
}

// Favorites: a five-point star outline.
void glyphFavorites(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    ImVec2 points[10];
    const float outer = s * 0.56f;
    const float inner = s * 0.24f;
    for (int i = 0; i < 10; ++i) {
        const float angle = -3.14159265f * 0.5f + static_cast<float>(i) * 3.14159265f / 5.0f;
        const float radius = (i % 2 == 0) ? outer : inner;
        points[i] = ImVec2(c.x + radius * std::cos(angle), c.y + s * 0.05f + radius * std::sin(angle));
    }
    draw->AddPolyline(points, 10, color, ImDrawFlags_Closed, std::max(1.0f, s / 11.0f));
}

// Features: a pine tree, like the trees and rocks placed on a map.
void glyphFeatures(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    const float half = s * 0.5f;
    const float stroke = std::max(1.0f, s / 11.0f);
    const ImVec2 upper[] = {ImVec2(c.x, c.y - half), ImVec2(c.x + half * 0.62f, c.y - half * 0.02f), ImVec2(c.x - half * 0.62f, c.y - half * 0.02f)};
    const ImVec2 lower[] = {ImVec2(c.x, c.y - half * 0.42f), ImVec2(c.x + half * 0.9f, c.y + half * 0.58f), ImVec2(c.x - half * 0.9f, c.y + half * 0.58f)};
    draw->AddPolyline(upper, 3, color, ImDrawFlags_Closed, stroke);
    draw->AddPolyline(lower, 3, color, ImDrawFlags_Closed, stroke);
    draw->AddLine(ImVec2(c.x, c.y + half * 0.58f), ImVec2(c.x, c.y + half), color, stroke * 1.4f);
}

// Start Locations: a flag on a pole.
void glyphStartLocations(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    const float half = s * 0.5f;
    const float stroke = std::max(1.0f, s / 11.0f);
    const float pole = c.x - half * 0.62f;
    draw->AddLine(ImVec2(pole, c.y - half), ImVec2(pole, c.y + half), color, stroke * 1.2f);
    const ImVec2 flag[] = {
        ImVec2(pole, c.y - half * 0.9f),
        ImVec2(c.x + half * 0.2f, c.y - half * 0.66f),
        ImVec2(c.x + half * 0.78f, c.y - half * 0.86f),
        ImVec2(c.x + half * 0.78f, c.y - half * 0.04f),
        ImVec2(c.x + half * 0.2f, c.y + half * 0.16f),
        ImVec2(pole, c.y - half * 0.08f),
    };
    draw->AddPolyline(flag, 6, color, ImDrawFlags_Closed, stroke);
}

// The sidebar toggle: a window with its left panel filled in, like Xcode's navigator button.
void glyphLeftSidebar(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    const float w = s * 1.15f;
    const float h = s * 0.9f;
    const float stroke = std::max(1.0f, s / 11.0f);
    const ImVec2 min(std::floor(c.x - w * 0.5f) + 0.5f, std::floor(c.y - h * 0.5f) + 0.5f);
    const ImVec2 max(min.x + std::floor(w), min.y + std::floor(h));
    const float rounding = s * 0.12f;
    draw->AddRect(min, max, color, rounding, 0, stroke);
    const float panel = std::floor(w * 0.36f);
    draw->AddRectFilled(min, ImVec2(min.x + panel, max.y), color, rounding, ImDrawFlags_RoundCornersLeft);
}

}  // namespace

void EditorApp::drawSidebar() {
    ImGui::BeginChild("sidebar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawSidebarBackground();
    ImGui::SetWindowFontScale(std::clamp(sidebarFontPercent_, 75, 200) / 100.0f);
    // The tabs are icons, as in Xcode's navigator, so more can be added without crowding the row.
    // Each tab's name shows on hover.
    static const theme::GlyphPainter kSidebarGlyphs[] = {glyphTilesets, glyphFavorites, glyphFeatures, glyphStartLocations};
    const char* sidebarTips[] = {text(Text::Tilesets), text(Text::Favorites), text(Text::Features), text(Text::StartLocations)};
    theme::navigatorBar("sidebar-tabs", kSidebarGlyphs, sidebarTips, 4, sidebarTab_);
    ImGui::Spacing();
    startLocationsTab_ = sidebarTab_ == 3;
    featuresTab_ = sidebarTab_ == 2;
    {
        if (sidebarTab_ == 0 || sidebarTab_ == 1) {
            ImGui::BeginChild(sidebarTab_ == 1 ? "favorites-scroll" : "tileset-scroll", ImVec2(0.0f, 0.0f), false);
            ImGui::SetWindowFontScale(std::clamp(sidebarFontPercent_, 75, 200) / 100.0f);
            drawTilesetBrowser(sidebarTab_ == 1);
            ImGui::EndChild();
        }
        if (featuresTab_) {
            showFeatures_ = true;
            ImGui::BeginChild("feature-scroll", ImVec2(0.0f, 0.0f), false);
            ImGui::SetWindowFontScale(std::clamp(sidebarFontPercent_, 75, 200) / 100.0f);
            drawFeatures();
            ImGui::EndChild();
        }
        if (startLocationsTab_) {
            showStarts_ = true;
            ImGui::BeginChild("start-scroll", ImVec2(0.0f, 0.0f), false);
            ImGui::SetWindowFontScale(std::clamp(sidebarFontPercent_, 75, 200) / 100.0f);
            drawStartLocations();
            ImGui::EndChild();
        }
    }
    if (!featuresTab_ || !map_) {
        featureDelete_ = false;
    }
    ImGui::EndChild();
}

void EditorApp::drawTilesetBrowser(bool favorites) {
    theme::helpText(favorites ? text(Text::FavoritesHelp) : text(Text::TilesetHelp));
    char* const searchText = favorites ? favoritesSearch_ : tilesetSearch_;
    const std::size_t searchSize = favorites ? sizeof(favoritesSearch_) : sizeof(tilesetSearch_);
    const std::string query = trimmed(searchText);
    const ImGuiStyle& searchStyle = ImGui::GetStyle();
    const float clearSize = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(query.empty() ? -FLT_MIN : -(clearSize + searchStyle.ItemSpacing.x));
    ImGui::InputTextWithHint("##tileset-search", favorites ? "Search favorites" : "Search tilesets", searchText, searchSize);
    if (!query.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("x##clear-tileset-search", ImVec2(clearSize, clearSize))) {
            searchText[0] = '\0';
        }
    }
    ImGui::Separator();
    if (!favorites && tilesets_.archives().empty()) {
        if (loadingTilesets()) {
            ImGui::TextWrapped("Loading tileset archives...");
        } else if (gameFolder_.empty()) {
            ImGui::TextWrapped("The Total Annihilation folder is not set. Choose it in Options to load the game's tilesets.");
        } else if (!gameFolderFound_) {
            ImGui::TextWrapped("The Total Annihilation folder was not found. Choose it again in Options.");
        } else {
            ImGui::TextWrapped("No tileset archives (.hpi, .ccx, .gp3, or .gpf) were found in the Total Annihilation folder or beside Galaxy Editor.");
        }
    }
    if (tilesetIndexArchives_ != tilesets_.archives().size() || tilesetIndexSections_ != tilesets_.sectionCount()) {
        rebuildTilesetIndex();
    }
    if (query != tilesetQueryApplied_) {
        applyTilesetSearch(query);
    }
    const bool openFromSearch = tilesetQueryOpenPending_;
    tilesetQueryOpenPending_ = false;
    int visibleArchives = 0;
    for (const TilesetIndexArchive& indexed : tilesetIndex_) {
        if (indexed.favorite != favorites || !indexed.visible) {
            continue;
        }
        ++visibleArchives;
        const std::size_t archiveIndex = indexed.archiveIndex;
        ImGui::PushID(static_cast<int>(archiveIndex));
        if (theme::sourceListHeading(indexed.title.c_str(), "reload")) {
            try {
                refreshFavoritesArchive(tilesets_.archives()[archiveIndex].path);
                statusNote_ = "Reloaded " + indexed.filename;
                error_.clear();
            } catch (const std::exception& ex) {
                error_ = ex.what();
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Reload\nRead %s from disk again", indexed.filename.c_str());
        }
        if (indexed.worlds.empty()) {
            ImGui::TextDisabled("No tiles yet");
        }
        for (const TilesetIndexWorld& world : indexed.worlds) {
            if (!world.visible) {
                continue;
            }
            if (openFromSearch && world.openOnSearch) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (ImGui::TreeNode(world.name.c_str(), "%s", world.name.c_str())) {
                for (const TilesetIndexCategory& category : world.categories) {
                    if (!category.visible) {
                        continue;
                    }
                    ImGui::PushID(category.name.c_str());
                    if (openFromSearch && category.openOnSearch) {
                        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                    }
                    const bool open = ImGui::TreeNodeEx("category", ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%d)", category.name.c_str(), category.shown);
                    if (open) {
                        requestCategory(archiveIndex, world.name, category.name);
                        const CategoryPages& page = categories_.at(categoryKey(archiveIndex, world.name, category.name));
                        const float image = static_cast<float>(previewSize_);
                        const ImGuiStyle& style = ImGui::GetStyle();
                        const float cardWidth = image + style.FramePadding.x * 2.0f;
                        const float contentWidth = ImGui::GetContentRegionAvail().x;
                        const float regionRight = ImGui::GetCursorScreenPos().x + contentWidth;
                        std::vector<int> shown;
                        shown.reserve(page.sections.size());
                        for (int index : page.sections) {
                            const CachedSection& cached = sections_[static_cast<std::size_t>(index)];
                            if (category.filterNames && !containsFolded(cached.name, tilesetQueryFolded_)) {
                                continue;
                            }
                            shown.push_back(index);
                        }
                        std::sort(shown.begin(), shown.end(), [&](int a, int b) {
                            const CachedSection& left = sections_[static_cast<std::size_t>(a)];
                            const CachedSection& right = sections_[static_cast<std::size_t>(b)];
                            const int leftArea = left.stamp.tileWidth * left.stamp.tileHeight;
                            const int rightArea = right.stamp.tileWidth * right.stamp.tileHeight;
                            if (leftArea != rightArea) {
                                return leftArea < rightArea;
                            }
                            if (left.stamp.tileWidth != right.stamp.tileWidth) {
                                return left.stamp.tileWidth < right.stamp.tileWidth;
                            }
                            if (left.stamp.tileHeight != right.stamp.tileHeight) {
                                return left.stamp.tileHeight < right.stamp.tileHeight;
                            }
                            return nameLess(left.name, right.name);
                        });
                        const int pageCount = static_cast<int>(shown.size());
                        for (int n = 0; n < pageCount; ++n) {
                            const int index = shown[static_cast<std::size_t>(n)];
                            const CachedSection& cached = sections_[static_cast<std::size_t>(index)];
                            const bool selected = favorites
                                ? favoriteSelection_.count(index) != 0
                                : tilesetSelection_.count(index) != 0 || (tilesetSelection_.empty() && selectedSection_ == index);
                            ImGui::PushID(index);
                            ImGui::BeginGroup();
                            if (selected) {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
                                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
                            }
                            const bool pressed = cached.preview != 0
                                && ImGui::ImageButton(
                                    "stamp",
                                    textureId(cached.preview),
                                    ImVec2(image, image),
                                    ImVec2(0.0f, 0.0f),
                                    ImVec2(1.0f, 1.0f),
                                    ImVec4(0.30f, 0.31f, 0.33f, 1.0f));
                            const std::string sizeSuffix = " - " + std::to_string(cached.stamp.tileWidth) + " x " + std::to_string(cached.stamp.tileHeight);
                            const float labelLimit = std::max(cardWidth, contentWidth);
                            std::string caption = cached.name + sizeSuffix;
                            if (ImGui::CalcTextSize(caption.c_str()).x > labelLimit) {
                                const float suffixWidth = ImGui::CalcTextSize(sizeSuffix.c_str()).x;
                                caption = fitLabel(cached.name, std::max(8.0f, labelLimit - suffixWidth)) + sizeSuffix;
                            }
                            const ImGuiIO& clickIo = ImGui::GetIO();
                            const bool extendSelection = clickIo.KeyCtrl || clickIo.KeySuper || clickIo.KeyShift;
                            if (cached.preview == 0 && ImGui::Selectable(caption.c_str(), selected, 0, ImVec2(std::max(cardWidth, ImGui::CalcTextSize(caption.c_str()).x), image))) {
                                clickSidebarTile(favorites, archiveIndex, shown, n);
                            }
                            const ImVec2 buttonMin = ImGui::GetItemRectMin();
                            const ImVec2 buttonMax = ImGui::GetItemRectMax();
                            if (selected) {
                                ImGui::PopStyleColor(3);
                                ImGui::GetWindowDrawList()->AddRect(
                                    ImVec2(buttonMin.x - 1.0f, buttonMin.y - 1.0f),
                                    ImVec2(buttonMax.x + 1.0f, buttonMax.y + 1.0f),
                                    theme::accentColor(),
                                    ImGui::GetStyle().FrameRounding,
                                    0,
                                    2.0f);
                            }
                            // The pointer over a picture brings up a small button in its corner.
                            const bool cardHovered = ImGui::IsItemHovered();
                            const theme::BadgeRect badge = theme::badgeRect(buttonMin, buttonMax);
                            const bool showBadge = cardHovered && cached.preview != 0;
                            const bool overBadge = showBadge && ImGui::IsMouseHoveringRect(badge.min, badge.max);
                            if (showBadge) {
                                theme::drawBadge(badge, favorites ? theme::BadgeIcon::Trash : theme::BadgeIcon::AddToFavorites, overBadge);
                            }
                            if (pressed && extendSelection) {
                                clickSidebarTile(favorites, archiveIndex, shown, n);
                            } else if (pressed && overBadge) {
                                if (favorites) {
                                    requestDeleteFavorite(archiveIndex, cached);
                                } else {
                                    openAddToFavorites(cached.stamp, cached.name);
                                }
                            } else if (pressed) {
                                clickSidebarTile(favorites, archiveIndex, shown, n);
                            }
                            if (overBadge) {
                                ImGui::SetTooltip("%s", favorites ? "delete tile from favorites" : "add to favorites");
                            } else if (cardHovered) {
                                ImGui::SetTooltip(
                                    "%s\n%s\n%d x %d tiles",
                                    cached.name.c_str(),
                                    cached.category.c_str(),
                                    cached.stamp.tileWidth,
                                    cached.stamp.tileHeight);
                            }
                            const float textWidth = ImGui::CalcTextSize(caption.c_str()).x;
                            const float itemWidth = std::max(cardWidth, textWidth);
                            const ImVec2 textPos = ImGui::GetCursorScreenPos();
                            const float textHeight = ImGui::GetTextLineHeight();
                            ImGui::Dummy(ImVec2(itemWidth, textHeight));
                            ImDrawList* list = ImGui::GetWindowDrawList();
                            list->PushClipRect(textPos, ImVec2(textPos.x + itemWidth, textPos.y + textHeight), true);
                            list->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), caption.c_str());
                            list->PopClipRect();
                            ImGui::EndGroup();
                            const float itemWidthOnRow = ImGui::GetItemRectSize().x;
                            const float itemRight = ImGui::GetItemRectMax().x;
                            // A right-click on a tile that is not part of the selection selects
                            // that tile alone, so Delete applies to the tile under the pointer.
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                                std::set<int>& tabSelection = favorites ? favoriteSelection_ : tilesetSelection_;
                                std::unordered_map<int, std::size_t>& tabArchives = favorites ? favoriteSelectionArchive_ : tilesetSelectionArchive_;
                                if (tabSelection.count(index) == 0) {
                                    tabSelection.clear();
                                    tabArchives.clear();
                                    tabSelection.insert(index);
                                    tabArchives[index] = archiveIndex;
                                    tileSelectionAnchor_[favorites ? 1 : 0] = index;
                                }
                            }
                            if (ImGui::BeginPopupContextItem("tile-file")) {
                                const TilesetSection* listed = nullptr;
                                if (archiveIndex < tilesets_.archives().size()) {
                                    for (const TilesetSection& section : tilesets_.archives()[archiveIndex].sections) {
                                        if (section.world == cached.world && section.category == cached.category && section.name == cached.name) {
                                            listed = &section;
                                            break;
                                        }
                                    }
                                }
                                const std::string filename = listed != nullptr ? archiveLeaf(listed->archivePath) : cached.name + ".sct";
                                if (ImGui::MenuItem("Copy file name")) {
                                    ImGui::SetClipboardText(filename.c_str());
                                    statusNote_ = "Copied " + filename;
                                }
                                if (ImGui::MenuItem("Rename file")) {
                                    requestRenameTile(archiveIndex, cached);
                                }
                                ImGui::Separator();
                                const std::size_t menuCount = (favorites ? favoriteSelection_ : tilesetSelection_).size();
                                const std::string countSuffix = menuCount > 1 ? " (" + std::to_string(menuCount) + ")" : std::string();
                                if (ImGui::MenuItem((theme::withEllipsis(menuCount > 1 ? "Export tiles as BMP" : "Export tile as BMP") + countSuffix).c_str())) {
                                    pendingTileExport_ = favorites ? 2 : 1;
                                }
                                if (ImGui::MenuItem(("Copy path and filename" + countSuffix).c_str())) {
                                    copySidebarTilePaths(favorites);
                                }
                                if (favorites && !favoriteSelection_.empty()) {
                                    ImGui::Separator();
                                    const int count = static_cast<int>(favoriteSelection_.size());
                                    const std::string label = count == 1 ? std::string("Delete") : "Delete " + std::to_string(count) + " tiles";
                                    if (ImGui::MenuItem(label.c_str(), "Delete")) {
                                        requestDeleteFavoriteSelection();
                                    }
                                }
                                ImGui::EndPopup();
                            }
                            const float advance = itemWidthOnRow + style.ItemSpacing.x;
                            if (n + 1 < pageCount && itemRight + advance < regionRight) {
                                ImGui::SameLine();
                            }
                            ImGui::PopID();
                        }
                        if (!page.complete) {
                            ImGui::TextDisabled("Loading %d / %d", static_cast<int>(page.sections.size()), page.total);
                        }
                        if (page.failed > 0) {
                            ImGui::TextWrapped("Could not read %d sections. %s", page.failed, page.lastError.c_str());
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (pendingTileExport_ != 0 && pendingTileExport_ == (favorites ? 2 : 1)) {
        pendingTileExport_ = 0;
        exportSidebarTilesBmp(favorites);
    }
    if (visibleArchives == 0 && !query.empty()) {
        if (favorites) {
            ImGui::TextWrapped("No favorites match \"%s\".", query.c_str());
        } else {
            ImGui::TextWrapped("No tilesets match \"%s\".", query.c_str());
        }
    }
    if (favorites && visibleArchives == 0 && query.empty()) {
        ImGui::TextWrapped("You have no favorites archives yet. A favorites archive is a tileset file of your own that you can share.");
        ImGui::Spacing();
        if (ImGui::Button(theme::withEllipsis("Create Favorites Archive").c_str())) {
            favoriteNewName_[0] = '\0';
            showCreateFavorites_ = true;
        }
    }
}

void EditorApp::drawFeatures() {
    theme::helpText("Click a feature, then click the map to place it. With none chosen, click a feature on the map to select it, or drag a box around several. Shift-click adds to the selection. Copy and Paste keep the features' positions. Delete removes the selection. Hold Space and drag to pan. Right-click removes one feature.");
    const std::string query = trimmed(featureSearch_);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##feature-search", "Search features", featureSearch_, sizeof(featureSearch_));

    struct FeatureCategoryGroup {
        std::string name;
        std::vector<const FeatureInfo*> features;
    };
    struct FeatureWorldGroup {
        std::string name;
        std::vector<FeatureCategoryGroup> categories;
    };
    std::vector<FeatureWorldGroup> groups;
    std::unordered_set<std::string> seen;
    int catalog = 0;

    for (const TilesetArchive& archive : tilesets_.archives()) {
        for (const FeatureInfo& feature : archive.features) {
            if (feature.name.empty()) {
                continue;
            }
            if (!seen.insert(lowerText(feature.name)).second) {
                continue;
            }
            ++catalog;
            const std::string world = feature.world.empty() ? std::string("All worlds") : feature.world;
            const std::string category = feature.category.empty() ? std::string("Other") : feature.category;
            if (!textHas(feature.name + " " + world + " " + category, query)) {
                continue;
            }
            FeatureWorldGroup* worldGroup = nullptr;
            for (FeatureWorldGroup& group : groups) {
                if (lowerText(group.name) == lowerText(world)) {
                    worldGroup = &group;
                    break;
                }
            }
            if (worldGroup == nullptr) {
                groups.push_back(FeatureWorldGroup{world, {}});
                worldGroup = &groups.back();
            }
            FeatureCategoryGroup* categoryGroup = nullptr;
            for (FeatureCategoryGroup& group : worldGroup->categories) {
                if (lowerText(group.name) == lowerText(category)) {
                    categoryGroup = &group;
                    break;
                }
            }
            if (categoryGroup == nullptr) {
                worldGroup->categories.push_back(FeatureCategoryGroup{category, {}});
                categoryGroup = &worldGroup->categories.back();
            }
            categoryGroup->features.push_back(&feature);
        }
    }

    if (catalog == 0) {
        ImGui::TextWrapped("No feature records were found. Trees, rocks, metal, and vents are listed in features/*.tdf inside a game archive (.hpi, .ccx, .gp3, or .gpf). Set the Total Annihilation folder in Options to load them.");
        return;
    }
    if (groups.empty()) {
        ImGui::TextWrapped("No features match \"%s\".", query.c_str());
        return;
    }

    const auto byName = [](const std::string& left, const std::string& right) {
        return lowerText(left) < lowerText(right);
    };
    std::sort(groups.begin(), groups.end(), [&](const FeatureWorldGroup& left, const FeatureWorldGroup& right) {
        return byName(left.name, right.name);
    });
    for (FeatureWorldGroup& world : groups) {
        std::sort(world.categories.begin(), world.categories.end(), [&](const FeatureCategoryGroup& left, const FeatureCategoryGroup& right) {
            return byName(left.name, right.name);
        });
        for (FeatureCategoryGroup& category : world.categories) {
            std::sort(category.features.begin(), category.features.end(), [&](const FeatureInfo* left, const FeatureInfo* right) {
                return byName(left->name, right->name);
            });
        }
    }

    for (const FeatureWorldGroup& world : groups) {
        int count = 0;
        for (const FeatureCategoryGroup& category : world.categories) {
            count += static_cast<int>(category.features.size());
        }
        if (!query.empty()) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        }
        ImGuiTreeNodeFlags worldFlags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (groups.size() == 1) {
            worldFlags |= ImGuiTreeNodeFlags_DefaultOpen;
        }
        if (!ImGui::TreeNodeEx(world.name.c_str(), worldFlags, "%s (%d)", world.name.c_str(), count)) {
            continue;
        }
        for (const FeatureCategoryGroup& category : world.categories) {
            if (!query.empty()) {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            const ImGuiTreeNodeFlags categoryFlags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
            if (!ImGui::TreeNodeEx(category.name.c_str(), categoryFlags, "%s (%d)", category.name.c_str(), static_cast<int>(category.features.size()))) {
                continue;
            }
            for (const FeatureInfo* feature : category.features) {
                ImGui::PushID(feature);
                std::string row = feature->name + " - " + std::to_string(feature->footprintX) + " x " + std::to_string(feature->footprintZ);
                if (feature->metal > 0) {
                    row += " - " + std::to_string(feature->metal) + " metal";
                }
                if (feature->energy > 0) {
                    row += " - " + std::to_string(feature->energy) + " energy";
                }
                const FeatureSprite* picture = featurePicture(feature->name);
                const bool hasPicture = picture != nullptr && picture->texture != 0 && picture->width > 0 && picture->height > 0;
                const float rowHeight = hasPicture ? 40.0f : 0.0f;
                bool choose = false;
                if (hasPicture) {
                    const float scale = 36.0f / static_cast<float>(std::max(picture->width, picture->height));
                    ImGui::Image(textureId(picture->texture), ImVec2(picture->width * scale, picture->height * scale));
                    choose = ImGui::IsItemClicked();
                    ImGui::SameLine();
                }
                if (choose || ImGui::Selectable(row.c_str(), armedFeature_ == feature->name && !featureDelete_, 0, ImVec2(0.0f, rowHeight))) {
                    armedFeature_ = feature->name;
                    featureDelete_ = false;
                    featureSelectMode_ = false;
                    featurePasteArmed_ = false;
                    pasteArmed_ = false;
                    armedStartPlayer_ = 0;
                    draggingStartPlayer_ = 0;
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
}

void EditorApp::set3dView(bool on) {
    if (on && !map_) {
        return;
    }
    view3d_ = on;
    // A tile drag or paste that was under way cannot continue in the other view.
    selecting_ = false;
    pasteArmed_ = false;
    rightDown_ = false;
    middlePanning_ = false;
    featurePressed_ = false;
    canvasPressed_ = false;
    draggingStartPlayer_ = 0;
    hoverOverMap_ = false;
    hoverHeight_ = -1;
    if (on) {
        // Make the ground follow the map as it is now.
        uploaded3dRevision_ = 0;
    } else {
        invalidateView();
    }
}

// Sends the map's heights and its flat picture to the graphics card.
void EditorApp::upload3d() {
    if (!map_ || terrain3d_.failed()) {
        return;
    }
    const std::size_t cells = static_cast<std::size_t>(map_->heightWidth()) * static_cast<std::size_t>(map_->heightHeight());
    if (map_->heightWidth() < 1 || map_->heightHeight() < 1 || map_->heights.size() < cells) {
        return;
    }
    GLint gpuMax = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gpuMax);
    const int maxTexture = std::max(256, std::min(static_cast<int>(gpuMax), 4096));
    const int mapW = map_->pixelWidth();
    const int mapH = map_->pixelHeight();
    const float scale = std::min(1.0f, static_cast<float>(maxTexture) / static_cast<float>(std::max(mapW, mapH)));
    const int texW = std::max(1, static_cast<int>(std::ceil(static_cast<float>(mapW) * scale)));
    const int texH = std::max(1, static_cast<int>(std::ceil(static_cast<float>(mapH) * scale)));
    colors3d_.resize(static_cast<std::size_t>(texW) * static_cast<std::size_t>(texH) * 4);
    renderMapView(*map_, colors3d_.data(), texW, texH, static_cast<float>(texW), static_cast<float>(texH), 0.0f, 0.0f, scale);
    terrain3d_.setColors(colors3d_.data(), texW, texH);
    terrain3d_.setHeights(map_->heights.data(), map_->heightWidth(), map_->heightHeight(), 16.0f);
    uploaded3dRevision_ = mapRevision_;
}

void EditorApp::drawMap3d() {
    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    mapCanvasW_ = canvas.x;
    mapCanvasH_ = canvas.y;
    mapOriginX_ = origin.x;
    mapOriginY_ = origin.y;
    hoverOverMap_ = false;
    hoverHeight_ = -1;
    featurePressed_ = false;
    if (canvas.x < 8.0f || canvas.y < 8.0f) {
        return;
    }

    const float mapW = static_cast<float>(map_->pixelWidth());
    const float mapH = static_cast<float>(map_->pixelHeight());
    if (framed3dSerial_ != mapSerial_) {
        camera3d_.frame(mapW, mapH);
        framed3dSerial_ = mapSerial_;
    }

    ImGui::InvisibleButton(
        "canvas3d", canvas, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    // Mouse: left drag orbits; right drag, middle drag, or Shift + left drag slides the ground; the wheel zooms.
    if (active) {
        const bool slide = io.MouseDown[ImGuiMouseButton_Right] || io.MouseDown[ImGuiMouseButton_Middle]
            || (io.MouseDown[ImGuiMouseButton_Left] && io.KeyShift);
        if (slide) {
            camera3d_.pan(io.MouseDelta.x, io.MouseDelta.y, canvas.y);
        } else if (io.MouseDown[ImGuiMouseButton_Left]) {
            camera3d_.orbit(io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        camera3d_.zoom(io.MouseWheel);
    }

    // Keyboard: W A S D or the arrow keys move over the ground, Q and E turn, Home starts over.
    const bool keysFree = !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper
        && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)
        && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
        && (!ImGui::IsAnyItemActive() || active);
    if (keysFree) {
        float forward = 0.0f;
        float right = 0.0f;
        if (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow)) {
            forward += 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) {
            forward -= 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) {
            right += 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) {
            right -= 1.0f;
        }
        const float seconds = std::min(io.DeltaTime, 0.1f);
        if (forward != 0.0f || right != 0.0f) {
            camera3d_.move(forward, right, seconds, io.KeyShift);
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            camera3d_.turn(-1.6f * seconds);
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            camera3d_.turn(1.6f * seconds);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
            camera3d_.frame(mapW, mapH);
        }
    }
    camera3d_.keepOver(mapW, mapH);

    // The camera circles the ground under it, so it rises and falls with the hills.
    const int groundHeight = sampleHeight(*map_, static_cast<int>(camera3d_.targetX), static_cast<int>(camera3d_.targetZ));
    if (groundHeight >= 0) {
        const float goal = static_cast<float>(groundHeight) * Terrain3D::kHeightScale;
        camera3d_.targetY += (goal - camera3d_.targetY) * std::min(1.0f, io.DeltaTime * 8.0f);
    }

    if (uploaded3dRevision_ != mapRevision_) {
        upload3d();
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float framebufferScale = std::max(io.DisplayFramebufferScale.x, 1.0f);
    GLint gpuMax = 4096;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &gpuMax);
    const int limit = std::max(256, static_cast<int>(gpuMax));
    const int targetW = std::clamp(static_cast<int>(std::lround(canvas.x * framebufferScale)), 1, limit);
    const int targetH = std::clamp(static_cast<int>(std::lround(canvas.y * framebufferScale)), 1, limit);

    float viewProjection[16];
    camera3d_.viewProjection(canvas.x / canvas.y, viewProjection);
    Terrain3D::Sea sea;
    sea.red = static_cast<float>((seaColor_ >> 16) & 0xFF) / 255.0f;
    sea.green = static_cast<float>((seaColor_ >> 8) & 0xFF) / 255.0f;
    sea.blue = static_cast<float>(seaColor_ & 0xFF) / 255.0f;
    sea.opacity = static_cast<float>(std::clamp(seaOpacity_, 0, 100)) / 100.0f;
    sea.level = static_cast<float>(map_->seaLevel);
    const ImVec4 canvasColor = ImGui::ColorConvertU32ToFloat4(theme::canvasColor());
    const float background[3] = {canvasColor.x, canvasColor.y, canvasColor.z};

    const GLuint picture = terrain3d_.render(targetW, targetH, viewProjection, sea, background);
    if (picture != 0) {
        // OpenGL draws upside down, so the picture is flipped back here.
        draw->AddImage(textureId(picture), origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else if (terrain3d_.failed()) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 16.0f, origin.y + 16.0f));
        ImGui::TextWrapped("The 3D view needs OpenGL 3.2 and could not start on this computer. Turn it off in the View menu to return to the flat map.");
    }

    // A reminder of the controls, in the corner.
    const char* hint = "Drag to orbit   Right-drag or Shift-drag to pan   Scroll to zoom   W A S D to move   Q E to turn   Home to reset";
    const ImVec2 hintSize = ImGui::CalcTextSize(hint);
    const float padding = 8.0f;
    if (hintSize.x + padding * 4.0f < canvas.x) {
        const ImVec2 min(origin.x + padding, origin.y + canvas.y - hintSize.y - padding * 2.5f);
        const ImVec2 max(min.x + hintSize.x + padding * 2.0f, min.y + hintSize.y + padding * 1.5f);
        draw->AddRectFilled(min, max, IM_COL32(20, 22, 26, 150), 6.0f);
        draw->AddText(ImVec2(min.x + padding, min.y + padding * 0.75f), IM_COL32(244, 245, 248, 235), hint);
    }
}

void EditorApp::drawMap() {
    ImGui::BeginChild("map", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
    if (!map_) {
        hoverOverMap_ = false;
        hoverHeight_ = -1;
        featurePressed_ = false;
        ImGui::TextWrapped("Open a .ufo map to see it. File -> Open, or pass a map path on the command line.");
        ImGui::EndChild();
        return;
    }

    if (savingMap()) {
        // The map stays blurred and out of reach until the file is written.
        const ImVec2 savingMin = ImGui::GetCursorScreenPos();
        const ImVec2 savingSize = ImGui::GetContentRegionAvail();
        hoverOverMap_ = false;
        hoverHeight_ = -1;
        featurePressed_ = false;
        brushStroking_ = false;
        drawSavingOverlay(savingMin.x, savingMin.y, savingMin.x + savingSize.x, savingMin.y + savingSize.y);
        ImGui::EndChild();
        return;
    }

    if (view3d_) {
        drawMap3d();
        ImGui::EndChild();
        return;
    }

    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    mapCanvasW_ = canvas.x;
    mapCanvasH_ = canvas.y;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    mapOriginX_ = origin.x;
    mapOriginY_ = origin.y;
    ImGui::InvisibleButton("canvas", canvas, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    const float mapW = static_cast<float>(map_->pixelWidth());
    const float mapH = static_cast<float>(map_->pixelHeight());
    if (fit_ && canvas.x > 1.0f && canvas.y > 1.0f && mapW > 0.0f && mapH > 0.0f) {
        zoom_ = std::min(canvas.x / mapW, canvas.y / mapH);
        panX_ = (canvas.x - mapW * zoom_) * 0.5f;
        panY_ = (canvas.y - mapH * zoom_) * 0.5f;
    }

    ImGuiIO& io = ImGui::GetIO();
    const bool stampRotating = io.KeyAlt && placementStamp() != nullptr;
    // Shift + two fingers on a touchpad (both scroll axes) pans the map instead of zooming.
    if (hovered && io.KeyShift && !stampRotating && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
        const PanDelta pan = touchpadPanDelta(io.MouseWheelH, io.MouseWheel, scrollPixelsPerUnit(currentScrollPlatform()));
        panX_ += pan.x;
        panY_ += pan.y;
        fit_ = false;
    }
    if (hovered && io.MouseWheel != 0.0f && !io.KeyShift && !stampRotating) {
        const float previous = zoom_;
        const float factor = io.MouseWheel > 0.0f ? 1.1f : 1.0f / 1.1f;
        zoom_ = std::clamp(zoom_ * factor, 0.02f, 16.0f);
        const ImVec2 mouse(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
        panX_ = mouse.x - (mouse.x - panX_) * (zoom_ / previous);
        panY_ = mouse.y - (mouse.y - panY_) * (zoom_ / previous);
        fit_ = false;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        middlePanning_ = true;
    }
    if (middlePanning_ && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        panX_ += io.MouseDelta.x;
        panY_ += io.MouseDelta.y;
        fit_ = false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        middlePanning_ = false;
    }
    if (selectionMode_ && !featuresTab_ && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        rightDown_ = true;
        rightDragged_ = false;
    }
    if (rightDown_ && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 6.0f)) {
        rightDragged_ = true;
        panX_ += io.MouseDelta.x;
        panY_ += io.MouseDelta.y;
        fit_ = false;
    }
    auto mapToScreen = [&](float x, float y) {
        return ImVec2(origin.x + panX_ + x * zoom_, origin.y + panY_ + y * zoom_);
    };
    auto playerAtPin = [&](ImVec2 screen) -> int {
        if (!showStarts_ && !startLocationsTab_) {
            return 0;
        }
        int found = 0;
        float best = 1.0e9f;
        for (const StartPosition& start : map_->starts) {
            const ImVec2 tip = mapToScreen(static_cast<float>(start.x), static_cast<float>(start.y));
            const StartPin pin = startPinGeometry(tip, zoom_);
            if (!startPinHit(pin, screen)) {
                continue;
            }
            const float dx = screen.x - pin.head.x;
            const float dy = screen.y - pin.head.y;
            const float distance = dx * dx + dy * dy;
            if (distance < best) {
                best = distance;
                found = start.player;
            }
        }
        return found;
    };
    if (!featuresTab_) {
        featurePressed_ = false;
        featureMarquee_ = false;
        featurePasteArmed_ = false;
        featureSelectMode_ = false;
    }
    // In feature select mode the left button selects features: a click picks one
    // and a drag draws a box around several. Holding Space pans instead.
    const bool featureSelecting = featuresTab_ && featureSelectMode_ && armedFeature_.empty() && !featureDelete_ && !featurePasteArmed_
        && !ImGui::IsKeyDown(ImGuiKey_Space);
    if (featuresTab_ && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        featurePressed_ = true;
        pressX_ = io.MousePos.x;
        pressY_ = io.MousePos.y;
        featureMarqueeX0_ = zoom_ > 0.0f ? (io.MousePos.x - origin.x - panX_) / zoom_ : 0.0f;
        featureMarqueeY0_ = zoom_ > 0.0f ? (io.MousePos.y - origin.y - panY_) / zoom_ : 0.0f;
        featureMarquee_ = false;
    }
    if (startLocationsTab_ && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int hit = playerAtPin(io.MousePos);
        pressX_ = io.MousePos.x;
        pressY_ = io.MousePos.y;
        if (hit != 0) {
            draggingStartPlayer_ = hit;
            armedStartPlayer_ = hit;
            pasteArmed_ = false;
        } else {
            canvasPressed_ = true;
        }
    }
    const bool brushOwnsMap = !brushName_.empty() && !featuresTab_ && !startLocationsTab_;
    if (draggingStartPlayer_ == 0 && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f) && !brushOwnsMap && (!selectionMode_ || startLocationsTab_ || featuresTab_) && !featureSelecting) {
        panX_ += io.MouseDelta.x;
        panY_ += io.MouseDelta.y;
        fit_ = false;
    }

    bool overMap = false;
    int tileX = 0;
    int tileY = 0;
    int pixelX = 0;
    int pixelY = 0;
    int featureCellX = -1;
    int featureCellY = -1;
    hoverOverMap_ = false;
    hoverHeight_ = -1;
    const float mapX = zoom_ > 0.0f ? (io.MousePos.x - origin.x - panX_) / zoom_ : -1.0f;
    const float mapY = zoom_ > 0.0f ? (io.MousePos.y - origin.y - panY_) / zoom_ : -1.0f;
    int pointerTileX = hoverTileX_;
    int pointerTileY = hoverTileY_;
    if (hovered && mapW > 0.0f && mapH > 0.0f) {
        pointerTileX = static_cast<int>(std::floor(mapX / 32.0f));
        pointerTileY = static_cast<int>(std::floor(mapY / 32.0f));
        hoverTileX_ = pointerTileX;
        hoverTileY_ = pointerTileY;
        if (mapX >= 0.0f && mapY >= 0.0f && mapX < mapW && mapY < mapH) {
            overMap = true;
            pixelX = static_cast<int>(mapX);
            pixelY = static_cast<int>(mapY);
            tileX = pixelX / 32;
            tileY = pixelY / 32;
            hoverHeight_ = sampleHeight(*map_, pixelX, pixelY);
            hoverOverMap_ = hoverHeight_ >= 0;
            featureCellX = pixelX / 16;
            featureCellY = pixelY / 16;
        }
    }
    if (hovered && io.MouseWheel != 0.0f && io.KeyAlt && placementStamp() != nullptr) {
        const int steps = io.MouseWheel > 0.0f ? -1 : 1;
        if (io.KeyShift) {
            adjustPlaceOffset(steps, 0);
        } else {
            adjustPlaceOffset(0, steps);
        }
    }
    const SectionStamp* placing = placementStamp();
    int originX = pointerTileX + placeOffsetX_;
    int originY = pointerTileY + placeOffsetY_;
    if (placing != nullptr && map_->tileWidth > 0 && map_->tileHeight > 0) {
        originX = std::clamp(originX, 1 - placing->tileWidth, map_->tileWidth - 1);
        originY = std::clamp(originY, 1 - placing->tileHeight, map_->tileHeight - 1);
    }
    const bool stampOverlaps = placing != nullptr && originX < map_->tileWidth && originY < map_->tileHeight
        && originX + placing->tileWidth > 0 && originY + placing->tileHeight > 0;
    const int clampedTileX = map_->tileWidth > 0 ? std::clamp(static_cast<int>(std::floor(mapX / 32.0f)), 0, map_->tileWidth - 1) : 0;
    const int clampedTileY = map_->tileHeight > 0 ? std::clamp(static_cast<int>(std::floor(mapY / 32.0f)), 0, map_->tileHeight - 1) : 0;

    auto featureAtPointer = [&](int& cellX, int& cellY, std::string& name) -> bool {
        name.clear();
        float bestHit = 1.0e9f;
        bool found = false;
        for (const FeaturePlacement& feature : map_->features) {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            if (!featurePictureBounds(feature.name, feature.x, feature.y, left, top, right, bottom)) {
                continue;
            }
            const ImVec2 minCorner = mapToScreen(left, top);
            const ImVec2 maxCorner = mapToScreen(right, bottom);
            const float minX = std::min(minCorner.x, maxCorner.x);
            const float maxX = std::max(minCorner.x, maxCorner.x);
            const float minY = std::min(minCorner.y, maxCorner.y);
            const float maxY = std::max(minCorner.y, maxCorner.y);
            if (io.MousePos.x < minX || io.MousePos.x > maxX || io.MousePos.y < minY || io.MousePos.y > maxY) {
                continue;
            }
            const float dx = io.MousePos.x - (minX + maxX) * 0.5f;
            const float dy = io.MousePos.y - (minY + maxY) * 0.5f;
            const float distance = dx * dx + dy * dy;
            if (distance < bestHit) {
                bestHit = distance;
                name = feature.name;
                cellX = feature.x;
                cellY = feature.y;
                found = true;
            }
        }
        if (found || !overMap) {
            return found;
        }
        for (const FeaturePlacement& feature : map_->features) {
            if (feature.x == featureCellX && feature.y == featureCellY) {
                name = feature.name;
                cellX = feature.x;
                cellY = feature.y;
                return true;
            }
        }
        return false;
    };

    if (startLocationsTab_) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            const int hit = playerAtPin(io.MousePos);
            if (hit != 0) {
                clearStartPosition(hit);
            } else {
                armedStartPlayer_ = 0;
                draggingStartPlayer_ = 0;
            }
        }
        if (draggingStartPlayer_ != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            if (overMap && dx * dx + dy * dy > 36.0f) {
                setStartPosition(draggingStartPlayer_, pixelX, pixelY);
            }
        }
        if (draggingStartPlayer_ != 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            draggingStartPlayer_ = 0;
        }
        if (canvasPressed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            canvasPressed_ = false;
            if (overMap && armedStartPlayer_ > 0 && dx * dx + dy * dy <= 36.0f) {
                setStartPosition(armedStartPlayer_, pixelX, pixelY);
                statusNote_ = "Player " + std::to_string(armedStartPlayer_) + " at " + std::to_string(pixelX) + ", " + std::to_string(pixelY);
            }
        } else if (canvasPressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            canvasPressed_ = false;
        }
        if (hovered && (armedStartPlayer_ > 0 || playerAtPin(io.MousePos) != 0)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    } else if (featuresTab_) {
        // The footprint of one feature on the ground, in map pixels, raised by half
        // the ground height as the border is drawn, so clicks and boxes find it there.
        auto footprintOf = [&](const FeaturePlacement& feature, float& left, float& top, float& right, float& bottom) {
            const FeatureSprite* picture = featurePicture(feature.name);
            const int footprintX = picture != nullptr ? std::max(1, picture->footprintX) : 1;
            const int footprintZ = picture != nullptr ? std::max(1, picture->footprintZ) : 1;
            const float lift = static_cast<float>(featureLift(*map_, feature.x, feature.y));
            left = static_cast<float>(feature.x * 16);
            top = static_cast<float>(feature.y * 16) - lift;
            right = static_cast<float>((feature.x + footprintX) * 16);
            bottom = static_cast<float>((feature.y + footprintZ) * 16) - lift;
        };
        // The feature under the pointer: its footprint first, so an invisible
        // metal zone can be picked, then its picture. The smallest wins.
        auto featureToSelect = [&](std::pair<int, int>& cell) -> bool {
            bool found = false;
            float bestRank = 1.0e30f;
            for (const FeaturePlacement& feature : map_->features) {
                float left = 0.0f;
                float top = 0.0f;
                float right = 0.0f;
                float bottom = 0.0f;
                footprintOf(feature, left, top, right, bottom);
                const bool onGround = mapX >= left && mapX < right && mapY >= top && mapY < bottom;
                float pictureLeft = 0.0f;
                float pictureTop = 0.0f;
                float pictureRight = 0.0f;
                float pictureBottom = 0.0f;
                const bool onPicture = featurePictureBounds(feature.name, feature.x, feature.y, pictureLeft, pictureTop, pictureRight, pictureBottom)
                    && mapX >= pictureLeft && mapX < pictureRight && mapY >= pictureTop && mapY < pictureBottom;
                if (!onGround && !onPicture) {
                    continue;
                }
                const float dx = mapX - (left + right) * 0.5f;
                const float dy = mapY - (top + bottom) * 0.5f;
                const float rank = (onGround ? 0.0f : 1.0e12f) + (right - left) * (bottom - top) * 1.0e3f + dx * dx + dy * dy;
                if (rank < bestRank) {
                    bestRank = rank;
                    cell = {feature.x, feature.y};
                    found = true;
                }
            }
            return found;
        };
        const bool toggleKey = io.KeyShift || io.KeyCtrl || io.KeySuper;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && featurePasteArmed_) {
            featurePasteArmed_ = false;
            statusNote_ = "Paste cancelled";
        } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            std::string removed;
            int removeX = featureCellX;
            int removeY = featureCellY;
            FeatureChange change;
            if (featureAtPointer(removeX, removeY, removed) && removeMapFeature(*map_, removeX, removeY, &change) == FeatureEdit::Removed) {
                rememberFeature(std::move(change));
                selectedFeatures_.erase({removeX, removeY});
                statusNote_ = "Removed " + removed;
            } else {
                armedFeature_.clear();
            }
        }
        if (featureSelecting && featurePressed_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            if (dx * dx + dy * dy > 36.0f) {
                featureMarquee_ = true;
            }
            featureMarqueeX1_ = mapX;
            featureMarqueeY1_ = mapY;
        }
        if (featurePressed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            featurePressed_ = false;
            if (featureSelecting && featureMarquee_) {
                const float boxLeft = std::min(featureMarqueeX0_, mapX);
                const float boxRight = std::max(featureMarqueeX0_, mapX);
                const float boxTop = std::min(featureMarqueeY0_, mapY);
                const float boxBottom = std::max(featureMarqueeY0_, mapY);
                std::set<std::pair<int, int>> inside;
                for (const FeaturePlacement& feature : map_->features) {
                    float left = 0.0f;
                    float top = 0.0f;
                    float right = 0.0f;
                    float bottom = 0.0f;
                    footprintOf(feature, left, top, right, bottom);
                    if (right > boxLeft && left < boxRight && bottom > boxTop && top < boxBottom) {
                        inside.insert({feature.x, feature.y});
                    }
                }
                if (toggleKey) {
                    selectedFeatures_.insert(inside.begin(), inside.end());
                } else {
                    selectedFeatures_ = std::move(inside);
                }
                statusNote_ = std::to_string(selectedFeatures_.size()) + (selectedFeatures_.size() == 1 ? " feature selected" : " features selected");
            } else if (dx * dx + dy * dy <= 36.0f) {
                if (featureDelete_) {
                    std::string removed;
                    int removeX = featureCellX;
                    int removeY = featureCellY;
                    FeatureChange change;
                    if (featureAtPointer(removeX, removeY, removed) && removeMapFeature(*map_, removeX, removeY, &change) == FeatureEdit::Removed) {
                        rememberFeature(std::move(change));
                        selectedFeatures_.erase({removeX, removeY});
                        statusNote_ = "Removed " + removed;
                    }
                } else if (featurePasteArmed_) {
                    if (overMap) {
                        pasteFeaturesAt(featureCellX, featureCellY);
                    }
                } else if (overMap && !armedFeature_.empty()) {
                    placeArmedFeature(featureCellX, featureCellY);
                } else if (featureSelecting) {
                    std::pair<int, int> cell;
                    if (overMap && featureToSelect(cell)) {
                        if (toggleKey) {
                            if (selectedFeatures_.erase(cell) == 0) {
                                selectedFeatures_.insert(cell);
                            }
                        } else {
                            selectedFeatures_ = {cell};
                        }
                        statusNote_ = std::to_string(selectedFeatures_.size()) + (selectedFeatures_.size() == 1 ? " feature selected" : " features selected");
                    } else if (!toggleKey) {
                        selectedFeatures_.clear();
                    }
                }
            }
            featureMarquee_ = false;
        } else if (featurePressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            featurePressed_ = false;
            featureMarquee_ = false;
        }
        std::string underCursor;
        int underX = 0;
        int underY = 0;
        const bool featureUnderCursor = featureAtPointer(underX, underY, underCursor);
        if (hovered && (featureDelete_ ? featureUnderCursor : (!armedFeature_.empty() || featureUnderCursor || featurePasteArmed_))) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    } else if (brushOwnsMap) {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            brushName_.clear();
            brushStroking_ = false;
            statusNote_ = "Brush cancelled";
        } else if (overMap && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mountainArmed()) {
            startMountain(tileX, tileY);
            brushStroking_ = true;
        } else if (brushStroking_ && overMap && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mountainArmed()) {
            // Dragging past either end of the mountain makes it longer.
            extendMountain(tileX, tileY);
        } else if (overMap && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            paintBrushStroke(tileX, tileY);
            const int brushCell = brushCellTiles();
            const int cellsAcross = (map_->tileWidth + brushCell - 1) / brushCell;
            const int cellsDown = (map_->tileHeight + brushCell - 1) / brushCell;
            if (const int span = hillSpan(); span > 0) {
                hillOrigin(tileX, tileY, span, hillSpanHigh(), brushStrokeX_, brushStrokeY_);
            } else if (brushSize_ < brushCell) {
                brushCornerNode(tileX, tileY, cellsAcross, cellsDown, brushStrokeX_, brushStrokeY_, brushCell);
            } else {
                brushBlockOrigin(tileX, tileY, brushSize_, cellsAcross, cellsDown, brushStrokeX_, brushStrokeY_, brushCell);
            }
            brushStroking_ = true;
        } else if (brushStroking_ && overMap && ImGui::IsMouseDown(ImGuiMouseButton_Left)
            && !brushVaries()) {
            // Medium and Large islands are one per click, since each click changes the island size.
            // A drag paints again when the snap point changes. A brush smaller than a cell follows the
            // meeting of four cells. A larger even brush follows the same point, because that point is the
            // center of its square. A one-cell brush follows the cell under the pointer: 8 x 8 on the
            // 8 tile cells, 16 x 16 on the 16 tile cells. A hill follows the 8 x 8 quadrant or 16 x 16 cell under the pointer.
            const int brushCell = brushCellTiles();
            const int cellsAcross = (map_->tileWidth + brushCell - 1) / brushCell;
            const int cellsDown = (map_->tileHeight + brushCell - 1) / brushCell;
            int stepX = tileX / brushCell;
            int stepY = tileY / brushCell;
            if (const int span = hillSpan(); span > 0) {
                hillOrigin(tileX, tileY, span, hillSpanHigh(), stepX, stepY);
            } else if (brushSize_ < brushCell) {
                brushCornerNode(tileX, tileY, cellsAcross, cellsDown, stepX, stepY, brushCell);
            } else if (brushSize_ > brushCell) {
                brushBlockOrigin(tileX, tileY, brushSize_, cellsAcross, cellsDown, stepX, stepY, brushCell);
            }
            if (stepX != brushStrokeX_ || stepY != brushStrokeY_) {
                paintBrushStroke(tileX, tileY);
                brushStrokeX_ = stepX;
                brushStrokeY_ = stepY;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            brushStroking_ = false;
            mountainStroke_ = false;
        }
    } else if (selectionMode_) {
        if (overMap && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selecting_ = true;
            selectAnchorX_ = tileX;
            selectAnchorY_ = tileY;
            pressX_ = io.MousePos.x;
            pressY_ = io.MousePos.y;
            selectionBefore_ = selection_;
            if (io.KeyShift && (io.KeyCtrl || io.KeySuper)) {
                selectOp_ = SelectOp::Subtract;
            } else if (io.KeyShift) {
                selectOp_ = SelectOp::Add;
            } else {
                selectOp_ = SelectOp::Replace;
            }
            const bool plainPaste = selectOp_ == SelectOp::Replace && pasteArmed_ && clipboard_;
            if (!plainPaste) {
                previewSelection(tileX, tileY, tileX, tileY);
            }
        } else if (hovered && pasteArmed_ && clipboard_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            canvasPressed_ = true;
            pressX_ = io.MousePos.x;
            pressY_ = io.MousePos.y;
        }
        if (selecting_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            const bool plainPaste = selectOp_ == SelectOp::Replace && pasteArmed_ && clipboard_;
            if (!plainPaste || dx * dx + dy * dy > 36.0f) {
                previewSelection(selectAnchorX_, selectAnchorY_, clampedTileX, clampedTileY);
            }
        }
        if (selecting_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            selecting_ = false;
            const bool plainPaste = selectOp_ == SelectOp::Replace && pasteArmed_ && clipboard_ && hovered && stampOverlaps
                && dx * dx + dy * dy <= 36.0f;
            if (plainPaste) {
                selection_ = selectionBefore_;
                pasteAt(originX, originY);
            } else {
                previewSelection(selectAnchorX_, selectAnchorY_, clampedTileX, clampedTileY);
                commitSelection();
            }
        } else if (selecting_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            selection_ = selectionBefore_;
            selecting_ = false;
        }
        if (!selecting_ && canvasPressed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            canvasPressed_ = false;
            if (dx * dx + dy * dy <= 36.0f && pasteArmed_ && clipboard_ && hovered && stampOverlaps) {
                pasteAt(originX, originY);
            }
        } else if (!selecting_ && canvasPressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            canvasPressed_ = false;
        }
        if (rightDown_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            rightDown_ = false;
            if (!rightDragged_ && pasteArmed_) {
                pasteArmed_ = false;
                statusNote_ = "Paste cancelled";
            } else if (!rightDragged_ && !selecting_ && hasSelection()) {
                selectionMenuRequested_ = true;
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            rightDown_ = false;
        }
    } else {
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (pasteArmed_) {
                pasteArmed_ = false;
                statusNote_ = "Paste cancelled";
            } else {
                if (favoriteSelection_.size() <= 1) {
                    favoriteSelection_.clear();
                    favoriteSelectionArchive_.clear();
                }
                if (tilesetSelection_.size() <= 1) {
                    tilesetSelection_.clear();
                    tilesetSelectionArchive_.clear();
                }
                selectedSection_ = -1;
            }
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            canvasPressed_ = true;
            pressX_ = io.MousePos.x;
            pressY_ = io.MousePos.y;
        }
        if (canvasPressed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            canvasPressed_ = false;
            if (hovered && stampOverlaps && dx * dx + dy * dy <= 36.0f) {
                if (pasteArmed_) {
                    pasteAt(originX, originY);
                } else {
                    placeSelected(originX, originY);
                }
            }
        } else if (canvasPressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            canvasPressed_ = false;
        }
        if (hovered && (pasteArmed_ || selectedSection_ >= 0)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
    }

    ensureGhost();
    renderViewport(canvas.x, canvas.y);
    renderHeightOverlay(canvas.x, canvas.y);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), theme::canvasColor());
    draw->PushClipRect(origin, ImVec2(origin.x + canvas.x, origin.y + canvas.y), true);

    if (texture_ != 0) {
        draw->AddImage(
            textureId(texture_),
            origin,
            ImVec2(origin.x + canvas.x, origin.y + canvas.y));
    }
    if (showHeightGrid_ && heightTexture_ != 0) {
        draw->AddImage(
            textureId(heightTexture_),
            origin,
            ImVec2(origin.x + canvas.x, origin.y + canvas.y));
    }

    auto toScreen = [&](float x, float y) {
        return ImVec2(origin.x + panX_ + x * zoom_, origin.y + panY_ + y * zoom_);
    };
    if (gridTiles_ > 0 && zoom_ > 0.0f && mapW > 0.0f && mapH > 0.0f) {
        const int step = gridTiles_ * 32;
        const float slop = 4.0f / zoom_;
        const float visibleLeft = -panX_ / zoom_ - slop;
        const float visibleRight = (canvas.x - panX_) / zoom_ + slop;
        const float visibleTop = -panY_ / zoom_ - slop;
        const float visibleBottom = (canvas.y - panY_) / zoom_ + slop;
        const int mapPixelW = map_->pixelWidth();
        const int mapPixelH = map_->pixelHeight();
        auto stroke = [&](float mapX0, float mapY0, float mapX1, float mapY1, bool vertical, bool border) {
            const ImVec2 a = toScreen(mapX0, mapY0);
            const ImVec2 b = toScreen(mapX1, mapY1);
            if (vertical) {
                const float x = std::floor(a.x);
                const float y0 = std::min(a.y, b.y);
                const float y1 = std::max(a.y, b.y);
                if (border) {
                    draw->AddRectFilled(ImVec2(x - 1.0f, y0), ImVec2(x, y1), IM_COL32(0, 0, 0, 255));
                    draw->AddRectFilled(ImVec2(x + 1.0f, y0), ImVec2(x + 2.0f, y1), IM_COL32(0, 0, 0, 255));
                } else {
                    draw->AddRectFilled(ImVec2(x, y0), ImVec2(x + 1.0f, y1), IM_COL32(255, 255, 255, 255));
                }
            } else {
                const float y = std::floor(a.y);
                const float x0 = std::min(a.x, b.x);
                const float x1 = std::max(a.x, b.x);
                if (border) {
                    draw->AddRectFilled(ImVec2(x0, y - 1.0f), ImVec2(x1, y), IM_COL32(0, 0, 0, 255));
                    draw->AddRectFilled(ImVec2(x0, y + 1.0f), ImVec2(x1, y + 2.0f), IM_COL32(0, 0, 0, 255));
                } else {
                    draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + 1.0f), IM_COL32(255, 255, 255, 255));
                }
            }
        };
        auto eachLine = [&](bool border) {
            for (int x = 0; x <= mapPixelW; x += step) {
                if (static_cast<float>(x) >= visibleLeft && static_cast<float>(x) <= visibleRight) {
                    stroke(static_cast<float>(x), 0.0f, static_cast<float>(x), mapH, true, border);
                }
            }
            if (mapPixelW % step != 0 && static_cast<float>(mapPixelW) >= visibleLeft && static_cast<float>(mapPixelW) <= visibleRight) {
                stroke(static_cast<float>(mapPixelW), 0.0f, static_cast<float>(mapPixelW), mapH, true, border);
            }
            for (int y = 0; y <= mapPixelH; y += step) {
                if (static_cast<float>(y) >= visibleTop && static_cast<float>(y) <= visibleBottom) {
                    stroke(0.0f, static_cast<float>(y), mapW, static_cast<float>(y), false, border);
                }
            }
            if (mapPixelH % step != 0 && static_cast<float>(mapPixelH) >= visibleTop && static_cast<float>(mapPixelH) <= visibleBottom) {
                stroke(0.0f, static_cast<float>(mapPixelH), mapW, static_cast<float>(mapPixelH), false, border);
            }
        };
        eachLine(true);
        eachLine(false);
    }
    if (showBrushCorners_ && zoom_ > 0.0f) {
        refreshBrushCorners();
        const float spacing = static_cast<float>(kBrushCellTiles * 32) * zoom_;
        if (brushCornerNodesX_ > 0 && spacing >= 6.0f) {
            const float cellPixels = static_cast<float>(kBrushCellTiles * 32);
            const int firstX = std::max(0, static_cast<int>(std::floor((-panX_ / zoom_) / cellPixels)));
            const int lastX = std::min(brushCornerNodesX_ - 1, static_cast<int>(std::ceil(((canvas.x - panX_) / zoom_) / cellPixels)));
            const int firstY = std::max(0, static_cast<int>(std::floor((-panY_ / zoom_) / cellPixels)));
            const int lastY = std::min(brushCornerNodesY_ - 1, static_cast<int>(std::ceil(((canvas.y - panY_) / zoom_) / cellPixels)));
            // The cell lines, then a dot at each corner coloured by its land, and its name when there is room.
            const float mapPixelW = static_cast<float>(map_->pixelWidth());
            const float mapPixelH = static_cast<float>(map_->pixelHeight());
            for (int x = firstX; x <= lastX; ++x) {
                const ImVec2 a = toScreen(std::min(static_cast<float>(x) * cellPixels, mapPixelW), 0.0f);
                const ImVec2 b = toScreen(std::min(static_cast<float>(x) * cellPixels, mapPixelW), mapPixelH);
                draw->AddLine(a, b, IM_COL32(255, 220, 60, 150), 1.0f);
            }
            for (int y = firstY; y <= lastY; ++y) {
                const ImVec2 a = toScreen(0.0f, std::min(static_cast<float>(y) * cellPixels, mapPixelH));
                const ImVec2 b = toScreen(mapPixelW, std::min(static_cast<float>(y) * cellPixels, mapPixelH));
                draw->AddLine(a, b, IM_COL32(255, 220, 60, 150), 1.0f);
            }
            const bool labels = spacing >= 90.0f;
            const float radius = std::clamp(spacing * 0.06f, 3.0f, 7.0f);
            for (int y = firstY; y <= lastY; ++y) {
                for (int x = firstX; x <= lastX; ++x) {
                    const int level = brushCornerLevels_[static_cast<std::size_t>(y) * static_cast<std::size_t>(brushCornerNodesX_) + static_cast<std::size_t>(x)];
                    if (level < 0 || static_cast<std::size_t>(level) >= brushCornerNames_.size()) {
                        continue;
                    }
                    const std::string& name = brushCornerNames_[static_cast<std::size_t>(level)];
                    const ImVec2 at = toScreen(std::min(static_cast<float>(x) * cellPixels, mapPixelW), std::min(static_cast<float>(y) * cellPixels, mapPixelH));
                    // One hue per land, lighter for higher ground.
                    const std::size_t space = name.rfind(' ');
                    const std::string world = space == std::string::npos ? name : name.substr(0, space);
                    const float hue = static_cast<float>(std::hash<std::string>{}(world) % 360u) / 360.0f;
                    const float height = static_cast<float>(std::max(0, trailingNumber(name)));
                    const ImU32 colour = ImColor::HSV(hue, 0.75f, 0.45f + 0.55f * std::min(1.0f, height / 255.0f));
                    draw->AddCircleFilled(at, radius, colour);
                    draw->AddCircle(at, radius, IM_COL32(0, 0, 0, 230), 0.0f, 1.5f);
                    if (labels) {
                        const ImVec2 size = ImGui::CalcTextSize(name.c_str());
                        const ImVec2 textAt(at.x + radius + 3.0f, at.y + radius + 1.0f);
                        draw->AddRectFilled(ImVec2(textAt.x - 2.0f, textAt.y - 1.0f), ImVec2(textAt.x + size.x + 2.0f, textAt.y + size.y + 1.0f),
                                            IM_COL32(0, 0, 0, 170), 3.0f);
                        draw->AddText(textAt, IM_COL32(255, 255, 255, 255), name.c_str());
                    }
                }
            }
        }
    }
    const ImVec2 clipMin(origin.x - 48.0f, origin.y - 48.0f);
    const ImVec2 clipMax(origin.x + canvas.x + 48.0f, origin.y + canvas.y + 48.0f);
    auto onScreen = [&](const ImVec2& at) {
        return at.x >= clipMin.x && at.x <= clipMax.x && at.y >= clipMin.y && at.y <= clipMax.y;
    };

    if (showFeatures_) {
        auto featureLabel = [&](const std::string& featureName) -> std::string {
            const FeatureSprite* picture = featurePicture(featureName);
            if (picture != nullptr && picture->metal > 0) {
                return featureName + " " + std::to_string(picture->metal);
            }
            return featureName;
        };
        for (const FeaturePlacement& feature : map_->features) {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            if (featurePictureBounds(feature.name, feature.x, feature.y, left, top, right, bottom)) {
                const ImVec2 minCorner = toScreen(left, top);
                const ImVec2 maxCorner = toScreen(right, bottom);
                const bool visible = maxCorner.x >= clipMin.x && minCorner.x <= clipMax.x && maxCorner.y >= clipMin.y && minCorner.y <= clipMax.y;
                if (!visible) {
                    continue;
                }
                const FeatureSprite* picture = featurePicture(feature.name);
                if (picture != nullptr && picture->texture != 0) {
                    draw->AddImage(textureId(picture->texture), minCorner, maxCorner);
                }
                if (showFeatureLabels_ && zoom_ > 0.45f) {
                    const std::string label = featureLabel(feature.name);
                    draw->AddText(ImVec2(minCorner.x + 1.0f, minCorner.y + 1.0f), IM_COL32(0, 0, 0, 220), label.c_str());
                    draw->AddText(minCorner, IM_COL32(255, 255, 255, 230), label.c_str());
                }
                continue;
            }
            const ImVec2 at = toScreen(feature.x * 16.0f + 8.0f, feature.y * 16.0f + 8.0f);
            if (!onScreen(at)) {
                continue;
            }
            const float radius = std::max(4.0f, 6.0f * zoom_);
            draw->AddCircleFilled(at, radius, featureColor(feature.name));
            if (showFeatureLabels_ && zoom_ > 0.35f) {
                const std::string label = featureLabel(feature.name);
                draw->AddText(ImVec2(at.x + radius + 2.0f, at.y - 7.0f), IM_COL32(255, 255, 255, 230), label.c_str());
            }
        }
        if (featuresTab_ && !featureDelete_ && !armedFeature_.empty() && overMap) {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            if (featurePictureBounds(armedFeature_, featureCellX, featureCellY, left, top, right, bottom)) {
                const ImVec2 minCorner = toScreen(left, top);
                const ImVec2 maxCorner = toScreen(right, bottom);
                const FeatureSprite* picture = featurePicture(armedFeature_);
                if (picture != nullptr && picture->texture != 0) {
                    draw->AddImage(textureId(picture->texture), minCorner, maxCorner, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 210));
                }
            } else {
                const ImVec2 ghost = toScreen(static_cast<float>(featureCellX) * 16.0f + 8.0f, static_cast<float>(featureCellY) * 16.0f + 8.0f);
                if (onScreen(ghost)) {
                    const float ghostRadius = std::max(4.0f, 6.0f * zoom_);
                    draw->AddCircle(ghost, ghostRadius + 3.0f, featureColor(armedFeature_), 0, 2.0f);
                    if (zoom_ > 0.35f) {
                        draw->AddText(ImVec2(ghost.x + ghostRadius + 2.0f, ghost.y - 7.0f), IM_COL32(255, 255, 255, 230), armedFeature_.c_str());
                    }
                }
            }
        }
    }
    if (showFeatureBorders_) {
        // A feature covers footprintX x footprintZ cells of 16 pixels from its
        // cell, whether or not it has a picture, so an invisible feature such
        // as a metal zone still shows how much ground it takes. The game draws
        // everything half its ground height higher on screen, and that is where
        // a metal extractor has to go, so the border rises with the picture.
        const ImU32 borderColor = IM_COL32(60, 255, 90, 255);
        const float thickness = zoom_ >= 0.5f ? 1.5f : 1.0f;
        auto drawBorder = [&](const std::string& featureName, int cellX, int cellY, ImU32 color) {
            const FeatureSprite* picture = featurePicture(featureName);
            const int footprintX = picture != nullptr ? std::max(1, picture->footprintX) : 1;
            const int footprintZ = picture != nullptr ? std::max(1, picture->footprintZ) : 1;
            const float lift = static_cast<float>(featureLift(*map_, cellX, cellY));
            const ImVec2 minCorner = toScreen(static_cast<float>(cellX * 16), static_cast<float>(cellY * 16) - lift);
            const ImVec2 maxCorner = toScreen(static_cast<float>((cellX + footprintX) * 16), static_cast<float>((cellY + footprintZ) * 16) - lift);
            if (maxCorner.x < clipMin.x || minCorner.x > clipMax.x || maxCorner.y < clipMin.y || minCorner.y > clipMax.y) {
                return;
            }
            draw->AddRect(minCorner, maxCorner, color, 0.0f, 0, thickness);
        };
        for (const FeaturePlacement& feature : map_->features) {
            drawBorder(feature.name, feature.x, feature.y, borderColor);
        }
        if (featuresTab_ && !featureDelete_ && !armedFeature_.empty() && overMap) {
            drawBorder(armedFeature_, featureCellX, featureCellY, IM_COL32(60, 255, 90, 170));
        }
    }
    auto footprintCorners = [&](const std::string& featureName, int cellX, int cellY, ImVec2& minCorner, ImVec2& maxCorner) {
        const FeatureSprite* picture = featurePicture(featureName);
        const int footprintX = picture != nullptr ? std::max(1, picture->footprintX) : 1;
        const int footprintZ = picture != nullptr ? std::max(1, picture->footprintZ) : 1;
        // Raised like the border and the picture, half the ground height.
        const float lift = static_cast<float>(featureLift(*map_, cellX, cellY));
        minCorner = toScreen(static_cast<float>(cellX * 16), static_cast<float>(cellY * 16) - lift);
        maxCorner = toScreen(static_cast<float>((cellX + footprintX) * 16), static_cast<float>((cellY + footprintZ) * 16) - lift);
    };
    if (!selectedFeatures_.empty()) {
        for (const FeaturePlacement& feature : selectedFeaturePlacements()) {
            ImVec2 minCorner;
            ImVec2 maxCorner;
            footprintCorners(feature.name, feature.x, feature.y, minCorner, maxCorner);
            if (maxCorner.x < clipMin.x || minCorner.x > clipMax.x || maxCorner.y < clipMin.y || minCorner.y > clipMax.y) {
                continue;
            }
            draw->AddRectFilled(minCorner, maxCorner, IM_COL32(255, 220, 60, 70));
            draw->AddRect(minCorner, maxCorner, IM_COL32(255, 220, 60, 255), 0.0f, 0, 2.5f);
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            if (featurePictureBounds(feature.name, feature.x, feature.y, left, top, right, bottom)) {
                draw->AddRect(toScreen(left, top), toScreen(right, bottom), IM_COL32(255, 220, 60, 160), 0.0f, 0, 1.0f);
            }
        }
    }
    if (featurePasteArmed_ && !featureClipboard_.empty() && featuresTab_ && overMap) {
        for (const FeatureClip& item : featureClipboard_) {
            const int cellX = featureCellX + item.dx;
            const int cellY = featureCellY + item.dy;
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
            const FeatureSprite* picture = featurePicture(item.name);
            if (picture != nullptr && picture->texture != 0 && featurePictureBounds(item.name, cellX, cellY, left, top, right, bottom)) {
                draw->AddImage(textureId(picture->texture), toScreen(left, top), toScreen(right, bottom), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, 190));
            }
            ImVec2 minCorner;
            ImVec2 maxCorner;
            footprintCorners(item.name, cellX, cellY, minCorner, maxCorner);
            draw->AddRectFilled(minCorner, maxCorner, IM_COL32(255, 220, 60, 50));
            draw->AddRect(minCorner, maxCorner, IM_COL32(255, 220, 60, 220), 0.0f, 0, 1.5f);
        }
    }
    if (featureMarquee_) {
        const ImVec2 first = toScreen(featureMarqueeX0_, featureMarqueeY0_);
        const ImVec2 second = toScreen(featureMarqueeX1_, featureMarqueeY1_);
        draw->AddRectFilled(first, second, IM_COL32(90, 160, 255, 50));
        draw->AddRect(first, second, IM_COL32(90, 160, 255, 230), 0.0f, 0, 1.5f);
    }
    if (showStarts_) {
        for (const StartPosition& start : map_->starts) {
            const ImVec2 at = toScreen(static_cast<float>(start.x), static_cast<float>(start.y));
            if (at.x < clipMin.x - 64.0f || at.x > clipMax.x + 64.0f || at.y < clipMin.y - 64.0f || at.y > clipMax.y + 64.0f) {
                continue;
            }
            drawStartPin(draw, at, zoom_, startColor(start.player), start.player, start.player == armedStartPlayer_);
        }
        if (startLocationsTab_ && armedStartPlayer_ > 0 && draggingStartPlayer_ == 0 && overMap) {
            drawStartPin(draw, toScreen(static_cast<float>(pixelX), static_cast<float>(pixelY)), zoom_, startColor(armedStartPlayer_), armedStartPlayer_, true);
        }
    }

    if (!selection_.empty()) {
        const int width = selection_.width;
        const int height = selection_.height;
        for (int y = 0; y < height; ++y) {
            int run = -1;
            for (int x = 0; x <= width; ++x) {
                const bool on = x < width && selection_.mask[static_cast<std::size_t>(y * width + x)] != 0;
                if (on && run < 0) {
                    run = x;
                }
                if (!on && run >= 0) {
                    const float x0 = static_cast<float>((selection_.x + run) * 32);
                    const float y0 = static_cast<float>((selection_.y + y) * 32);
                    const float x1 = static_cast<float>((selection_.x + x) * 32);
                    const float y1 = y0 + 32.0f;
                    draw->AddRectFilled(toScreen(x0, y0), toScreen(x1, y1), IM_COL32(90, 170, 255, 48));
                    run = -1;
                }
            }
        }
        const ImU32 edge = IM_COL32(140, 200, 255, 255);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                if (selection_.mask[static_cast<std::size_t>(y * width + x)] == 0) {
                    continue;
                }
                const int tileX = selection_.x + x;
                const int tileY = selection_.y + y;
                const float x0 = static_cast<float>(tileX * 32);
                const float y0 = static_cast<float>(tileY * 32);
                const float x1 = x0 + 32.0f;
                const float y1 = y0 + 32.0f;
                if (!selection_.contains(tileX - 1, tileY)) {
                    draw->AddLine(toScreen(x0, y0), toScreen(x0, y1), edge, 2.0f);
                }
                if (!selection_.contains(tileX + 1, tileY)) {
                    draw->AddLine(toScreen(x1, y0), toScreen(x1, y1), edge, 2.0f);
                }
                if (!selection_.contains(tileX, tileY - 1)) {
                    draw->AddLine(toScreen(x0, y0), toScreen(x1, y0), edge, 2.0f);
                }
                if (!selection_.contains(tileX, tileY + 1)) {
                    draw->AddLine(toScreen(x0, y1), toScreen(x1, y1), edge, 2.0f);
                }
            }
        }
        const std::string selectionLabel = selectionSummary(selection_);
        draw->AddText(toScreen(static_cast<float>(selection_.x * 32 + 4), static_cast<float>(selection_.y * 32 + 4)), IM_COL32(255, 255, 255, 240), selectionLabel.c_str());
    }
    if (selecting_ && selectOp_ != SelectOp::Replace) {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;
        if (clampedSelection(selectAnchorX_, selectAnchorY_, clampedTileX, clampedTileY, left, top, right, bottom)) {
            const ImU32 gesture = selectOp_ == SelectOp::Subtract ? IM_COL32(255, 90, 90, 255) : IM_COL32(120, 230, 150, 255);
            const float x0 = static_cast<float>(left * 32);
            const float y0 = static_cast<float>(top * 32);
            const float x1 = static_cast<float>((right + 1) * 32);
            const float y1 = static_cast<float>((bottom + 1) * 32);
            draw->AddRect(toScreen(x0, y0), toScreen(x1, y1), gesture, 0.0f, 0, 2.0f);
        }
    }

    if (pasteArmed_ && !selecting_ && hovered && stampOverlaps && clipboard_ && clipboard_->heightsOnly) {
        const SectionStamp& stamp = *clipboard_;
        const int alpha = std::max(40, placeOpacity_ * 150 / 100);
        const int sea = map_->seaLevel;
        const int stampStride = stamp.tileWidth * 2;
        for (int sy = 0; sy < stamp.tileHeight; ++sy) {
            for (int sx = 0; sx < stamp.tileWidth; ++sx) {
                if (!stamp.tileMask.empty()) {
                    const std::size_t cell = static_cast<std::size_t>(sy * stamp.tileWidth + sx);
                    if (cell >= stamp.tileMask.size() || stamp.tileMask[cell] == 0) {
                        continue;
                    }
                }
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        float x0 = static_cast<float>((originX + sx) * 32 + dx * 16);
                        float y0 = static_cast<float>((originY + sy) * 32 + dy * 16);
                        float x1 = x0 + 16.0f;
                        float y1 = y0 + 16.0f;
                        x0 = std::max(x0, 0.0f);
                        y0 = std::max(y0, 0.0f);
                        x1 = std::min(x1, mapW);
                        y1 = std::min(y1, mapH);
                        if (x1 <= x0 || y1 <= y0) {
                            continue;
                        }
                        const std::size_t sample = static_cast<std::size_t>(sy * 2 + dy) * static_cast<std::size_t>(stampStride)
                            + static_cast<std::size_t>(sx * 2 + dx);
                        const int height = stamp.heights[sample];
                        ImU32 color = IM_COL32(40, 90, 190, alpha);
                        if (height >= sea) {
                            const int lift = std::clamp((height - sea) * 2, 0, 160);
                            color = IM_COL32(30, 80 + lift, 45, alpha);
                        }
                        draw->AddRectFilled(toScreen(x0, y0), toScreen(x1, y1), color);
                    }
                }
            }
        }
        const float outlineX0 = static_cast<float>(originX * 32);
        const float outlineY0 = static_cast<float>(originY * 32);
        const float outlineX1 = static_cast<float>((originX + stamp.tileWidth) * 32);
        const float outlineY1 = static_cast<float>((originY + stamp.tileHeight) * 32);
        draw->AddRect(toScreen(outlineX0, outlineY0), toScreen(outlineX1, outlineY1), IM_COL32(120, 220, 255, 255), 0.0f, 0, 2.0f);
    }

    if (pasteArmed_ && !selecting_ && hovered && stampOverlaps && clipboard_ && !clipboard_->heightsOnly) {
        ensureClipboardGhost();
        const SectionStamp& stamp = *clipboard_;
        const float x0 = static_cast<float>(originX * 32);
        const float y0 = static_cast<float>(originY * 32);
        const float x1 = static_cast<float>((originX + stamp.tileWidth) * 32);
        const float y1 = static_cast<float>((originY + stamp.tileHeight) * 32);
        const float cx0 = std::max(x0, 0.0f);
        const float cy0 = std::max(y0, 0.0f);
        const float cx1 = std::min(x1, mapW);
        const float cy1 = std::min(y1, mapH);
        const float spanX = x1 - x0;
        const float spanY = y1 - y0;
        if (clipboardGhost_ != 0 && spanX > 0.0f && spanY > 0.0f && cx1 > cx0 && cy1 > cy0) {
            const ImVec2 uv0((cx0 - x0) / spanX, (cy0 - y0) / spanY);
            const ImVec2 uv1((cx1 - x0) / spanX, (cy1 - y0) / spanY);
            draw->AddImage(textureId(clipboardGhost_), toScreen(cx0, cy0), toScreen(cx1, cy1), uv0, uv1, IM_COL32(255, 255, 255, static_cast<int>(placeOpacity_) * 190 / 100));
            draw->AddRect(toScreen(x0, y0), toScreen(x1, y1), IM_COL32(255, 220, 80, 255), 0.0f, 0, 2.0f);
        }
    }

    if (!pasteArmed_ && !selectionMode_ && hovered && stampOverlaps && ghostTexture_ != 0 && selectedSection_ >= 0 && selectedSection_ < static_cast<int>(sections_.size())) {
        const SectionStamp& stamp = sections_[static_cast<std::size_t>(selectedSection_)].stamp;
        const float x0 = static_cast<float>(originX * 32);
        const float y0 = static_cast<float>(originY * 32);
        const float x1 = static_cast<float>((originX + stamp.tileWidth) * 32);
        const float y1 = static_cast<float>((originY + stamp.tileHeight) * 32);
        const float cx0 = std::max(x0, 0.0f);
        const float cy0 = std::max(y0, 0.0f);
        const float cx1 = std::min(x1, mapW);
        const float cy1 = std::min(y1, mapH);
        const float spanX = x1 - x0;
        const float spanY = y1 - y0;
        if (spanX > 0.0f && spanY > 0.0f && cx1 > cx0 && cy1 > cy0) {
            const ImVec2 uv0((cx0 - x0) / spanX, (cy0 - y0) / spanY);
            const ImVec2 uv1((cx1 - x0) / spanX, (cy1 - y0) / spanY);
            draw->AddImage(textureId(ghostTexture_), toScreen(cx0, cy0), toScreen(cx1, cy1), uv0, uv1, IM_COL32(255, 255, 255, static_cast<int>(placeOpacity_) * 190 / 100));
            draw->AddRect(toScreen(x0, y0), toScreen(x1, y1), IM_COL32(255, 220, 80, 255), 0.0f, 0, 2.0f);
        }
    }
    // Rivers and bridges say whether the cell under the pointer takes them.
    const bool riverTool = brushOwnsMap && (lineBrushArmed());
    const int hillTiles = brushOwnsMap ? hillSpan() : 0;
    std::string hillRefusal;
    bool hillBlocked = false;
    int hillWidth = hillTiles;
    int hillHeight = brushOwnsMap ? hillSpanHigh() : 0;
    if (hillTiles > 0 && hoverOverMap_ && mountainArmed()) {
        int hillX = 0;
        int hillY = 0;
        mountainFootprint(tileX, tileY, hillX, hillY, hillWidth, hillHeight);
        // While dragging, the mountain already placed is shown and is not checked again.
        hillBlocked = !mountainStroke_ && !mountainCanPlace(hillX, hillY, hillWidth, hillHeight, hillRefusal);
    } else if (hillTiles > 0 && hoverOverMap_) {
        int hillX = 0;
        int hillY = 0;
        hillOrigin(tileX, tileY, hillWidth, hillHeight, hillX, hillY);
        hillBlocked = !hillCanPlace(hillX, hillY, hillWidth, hillHeight, hillRefusal);
    }
    RiverPlan riverHover;
    if (riverTool && hoverOverMap_) {
        riverHover = riverPlanAt(tileX / kBrushCellTiles, tileY / kBrushCellTiles, armedBrushKind());
    }
    const bool riverBlocked = riverTool && hoverOverMap_ && !riverHover.allowed();
    if (hillTiles > 0 && hoverOverMap_) {
        int hillX = 0;
        int hillY = 0;
        if (mountainArmed()) {
            mountainFootprint(tileX, tileY, hillX, hillY, hillWidth, hillHeight);
        } else {
            hillOrigin(tileX, tileY, hillWidth, hillHeight, hillX, hillY);
        }
        const float x0 = static_cast<float>(hillX * 32);
        const float y0 = static_cast<float>(hillY * 32);
        const float x1 = x0 + static_cast<float>(hillWidth * 32);
        const float y1 = y0 + static_cast<float>(hillHeight * 32);
        const ImVec2 brushMin = toScreen(x0, y0);
        const ImVec2 brushMax = toScreen(x1, y1);
        draw->AddRectFilled(brushMin, brushMax, hillBlocked ? IM_COL32(230, 50, 40, 70) : IM_COL32(255, 255, 255, 36));
        draw->AddRect(brushMin, brushMax, IM_COL32(0, 0, 0, 230), 0.0f, 0, 3.0f);
        draw->AddRect(brushMin, brushMax, hillBlocked ? IM_COL32(255, 96, 84, 245) : IM_COL32(255, 255, 255, 245), 0.0f, 0, 1.25f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    } else if (brushOwnsMap && hoverOverMap_) {
        // 8 x 8 is centered on the point where four cells meet. 16 x 16 is the cell
        // under the pointer. Larger brushes are centered on that same point, so the
        // pointer sits in the middle of the square.
        const int brushTiles = riverTool ? kBrushCellTiles : std::max(kFineCellTiles / 2, brushSize_);
        int originCellX = tileX / kBrushCellTiles;
        int originCellY = tileY / kBrushCellTiles;
        int spanCellsX = 1;
        int spanCellsY = 1;
        if (riverTool && riverHover.allowed()) {
            bool pass = false;
            int minX = originCellX;
            int minY = originCellY;
            int maxX = originCellX + 1;
            int maxY = originCellY + 1;
            for (const RiverEdit& edit : riverHover.edits) {
                if (edit.key.rfind("Pass-", 0) != 0) {
                    continue;
                }
                pass = true;
                minX = std::min(minX, edit.x);
                minY = std::min(minY, edit.y);
                maxX = std::max(maxX, edit.x + 2);
                maxY = std::max(maxY, edit.y + 2);
            }
            if (pass) {
                originCellX = minX;
                originCellY = minY;
                spanCellsX = maxX - minX;
                spanCellsY = maxY - minY;
            }
        }
        float x0 = 0.0f;
        float y0 = 0.0f;
        float x1 = 0.0f;
        float y1 = 0.0f;
        // The base brush lays its cells every 8 tiles at 4 x 4 and 8 x 8, otherwise every 16.
        const int brushCell = riverTool ? kBrushCellTiles : brushCellTiles();
        if (!riverTool && brushTiles < brushCell) {
            const int cellsAcross = (map_->tileWidth + brushCell - 1) / brushCell;
            const int cellsDown = (map_->tileHeight + brushCell - 1) / brushCell;
            int nodeX = 0;
            int nodeY = 0;
            brushCornerNode(tileX, tileY, cellsAcross, cellsDown, nodeX, nodeY, brushCell);
            const float halfPx = static_cast<float>(brushTiles / 2 * 32);
            x0 = static_cast<float>(nodeX * brushCell * 32) - halfPx;
            y0 = static_cast<float>(nodeY * brushCell * 32) - halfPx;
            x1 = x0 + static_cast<float>(brushTiles * 32);
            y1 = y0 + static_cast<float>(brushTiles * 32);
        } else if (!riverTool) {
            const int cellsAcross = (map_->tileWidth + brushCell - 1) / brushCell;
            const int cellsDown = (map_->tileHeight + brushCell - 1) / brushCell;
            int blockX = 0;
            int blockY = 0;
            brushBlockOrigin(tileX, tileY, brushTiles, cellsAcross, cellsDown, blockX, blockY, brushCell);
            const int blockCells = std::max(1, brushTiles / brushCell);
            x0 = static_cast<float>(blockX * brushCell * 32);
            y0 = static_cast<float>(blockY * brushCell * 32);
            x1 = x0 + static_cast<float>(blockCells * brushCell * 32);
            y1 = y0 + static_cast<float>(blockCells * brushCell * 32);
        } else {
            x0 = static_cast<float>(originCellX * kBrushCellTiles * 32);
            y0 = static_cast<float>(originCellY * kBrushCellTiles * 32);
            // A mountain pass covers every cell it stamps. A river stays on its one cell.
            const bool mountainPass = spanCellsX != 1 || spanCellsY != 1;
            const float width = mountainPass ? static_cast<float>(spanCellsX * kBrushCellTiles * 32) : static_cast<float>(brushTiles * 32);
            const float height = mountainPass ? static_cast<float>(spanCellsY * kBrushCellTiles * 32) : static_cast<float>(brushTiles * 32);
            x1 = x0 + width;
            y1 = y0 + height;
        }
        const ImVec2 brushMin = toScreen(x0, y0);
        const ImVec2 brushMax = toScreen(x1, y1);
        draw->AddRectFilled(brushMin, brushMax, riverBlocked ? IM_COL32(230, 50, 40, 70) : IM_COL32(255, 255, 255, 36));
        draw->AddRect(brushMin, brushMax, IM_COL32(0, 0, 0, 230), 0.0f, 0, 3.0f);
        draw->AddRect(brushMin, brushMax, riverBlocked ? IM_COL32(255, 96, 84, 245) : IM_COL32(255, 255, 255, 245), 0.0f, 0, 1.25f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    } else if (hoverOverMap_) {
        const float cellX = static_cast<float>(pixelX & ~15);
        const float cellY = static_cast<float>(pixelY & ~15);
        draw->AddRect(toScreen(cellX, cellY), toScreen(cellX + 16.0f, cellY + 16.0f), IM_COL32(255, 255, 255, 230), 0.0f, 0, 1.0f);
    }
    draw->PopClipRect();

    if (hovered && startLocationsTab_) {
        if (armedStartPlayer_ > 0 && overMap) {
            ImGui::SetTooltip("Player %d\nClick to place at %d, %d\nDrag a pin to move it. Right-click a pin to remove it\nHeight %d", armedStartPlayer_, pixelX, pixelY, hoverHeight_);
        } else if (overMap) {
            ImGui::SetTooltip("Choose a player on the left\nDrag a pin to move it. Right-click a pin to remove it\nHeight %d", hoverHeight_);
        }
    } else if (hovered && featuresTab_) {
        if (featureDelete_ && overMap) {
            ImGui::SetTooltip("Click a feature to remove it\nHeight %d", hoverHeight_);
        } else if (!armedFeature_.empty() && overMap) {
            ImGui::SetTooltip("%s\nClick to place at cell %d, %d\nRight-click a marker to remove it\nHeight %d", armedFeature_.c_str(), featureCellX, featureCellY, hoverHeight_);
        } else if (featurePasteArmed_ && overMap) {
            ImGui::SetTooltip("Click to paste %d features with their cell %d, %d as the top-left\nEsc cancels", static_cast<int>(featureClipboard_.size()), featureCellX, featureCellY);
        } else if (overMap) {
            ImGui::SetTooltip(featureSelectMode_ ? "Drag a box around features, or click one\nHeight %d" : "Choose a feature on the left to place it, or press Select to select features\nHeight %d", hoverHeight_);
        }
    } else if (hovered && hillTiles > 0 && duneArmed()) {
        if (hillBlocked) {
            ImGui::SetTooltip("%s\nHeight %d", hillRefusal.c_str(), hoverHeight_);
        } else {
            ImGui::SetTooltip("Dunes-80\nClick or drag to paint 16 x 16 dune cells on Desert Base-80\nDunes beside each other join up\nEsc cancels\nHeight %d", hoverHeight_);
        }
    } else if (hovered && hillTiles > 0 && mountainArmed()) {
        if (hillBlocked) {
            ImGui::SetTooltip("%s\nHeight %d", hillRefusal.c_str(), hoverHeight_);
        } else if (mountainStroke_) {
            ImGui::SetTooltip("%s %d x %d\nDrag past either end to make it longer\nHeight %d", brushName_.c_str(), hillWidth, hillHeight, hoverHeight_);
        } else {
            ImGui::SetTooltip("%s\nClick to place a %d x %d %s\nKeep dragging %s to make it longer\nEsc cancels\nHeight %d",
                brushName_.c_str(), hillWidth, hillHeight, reefArmed() ? "reef on WaterWorld Base-0" : "mountain on Lush Base-77",
                mountainVertical() ? "up or down" : "left or right", hoverHeight_);
        }
    } else if (hovered && hillTiles > 0) {
        const bool rough = brushName_.rfind("Rough", 0) == 0;
        const bool island = brushName_ == "Island 8x8";
        const bool lavaPool = brushName_.rfind("Pool-86", 0) == 0;
        const bool lavaHill = brushName_.rfind("Hill-86", 0) == 0 || lavaPool;
        const bool pool = brushName_ == "Pools 8x8";
        const bool archipelagoHill = brushName_.rfind("Hill-194", 0) == 0;
        const bool archipelagoLowHill = brushName_.rfind("Hills-78", 0) == 0;
        const int seaIsland = seaIslandKind(brushName_);
        const char* piece = island || seaIsland == 1 ? "island" : (seaIsland == 2 ? "rocky island" : (brushName_ == "Feature-85" ? "feature" : lavaPool ? "lava pool" : pool || pool85Brush(brushName_) ? "pool" : (rough ? "rough patch" : "hill")));
        const char* ground = island || seaIsland != 0 ? "Base-0"
            : (pool ? "Base-77" : (lavaHill ? "Base-86" : (archipelagoHill ? "Base-194" : (archipelagoLowHill ? "Base-78" : "Base-85"))));
        const char* land = pool ? "Lush"
            : brushName_.rfind("Hills-85", 0) == 0 || pool85Brush(brushName_) || brushName_ == "Rough-85 16x16"
                || brushName_ == "Feature-85" ? brushWorld_.c_str()
            : seaIsland != 0 ? brushWorld_.c_str()
            : (lavaHill ? "Lava" : (archipelagoHill || archipelagoLowHill ? "Archipelago" : "GreenWorld"));
        const int nextIsland = nextSeaIsland();
        const HillSection* group = groupHill();
        std::string groupGround;
        if (group != nullptr) {
            piece = group->noun.c_str();
            groupGround = "Base-" + std::to_string(group->baseHeight);
            ground = groupGround.c_str();
            land = brushWorld_.c_str();
        }
        if (nextIsland >= 0) {
            // Brushes that mix sizes show the tile that the next click places.
            const HillSection& next = hillSections_[static_cast<std::size_t>(nextIsland)];
            const std::string nextNoun = next.noun.empty() ? std::string("island") : next.noun;
            if (hillBlocked) {
                ImGui::SetTooltip("%s\nNext %s %s, %s\nPress N to skip tile\nHeight %d",
                    hillRefusal.c_str(), nextNoun.c_str(), next.name.c_str(), brushRectLabel(next.tiles, next.high()).c_str(), hoverHeight_);
            } else {
                ImGui::SetTooltip("%s\nClick to place %s, %s, on %s %s\nPress N to skip tile\nEsc cancels\nHeight %d",
                    brushName_.c_str(), next.name.c_str(), brushRectLabel(next.tiles, next.high()).c_str(), land, ground, hoverHeight_);
            }
        } else if (hillBlocked) {
            ImGui::SetTooltip("%s\nHeight %d", hillRefusal.c_str(), hoverHeight_);
        } else if (hoverOverMap_) {
            ImGui::SetTooltip("%s\nClick to place a random %s on this %s block\nEsc cancels\nHeight %d", brushName_.c_str(), piece, ground, hoverHeight_);
        } else {
            ImGui::SetTooltip("%s\nClick to place a random %s on %s %s\nEsc cancels", brushName_.c_str(), piece, land, ground);
        }
    } else if (hovered && riverTool) {
        const char* what = brushName_.c_str();
        if (riverBlocked) {
            ImGui::SetTooltip("%s\nHeight %d", riverHover.reason.c_str(), hoverHeight_);
        } else {
            const char* action = "Click to paint one 16 x 16 cell";
            bool mountainPass = false;
            for (const RiverEdit& edit : riverHover.edits) {
                if (edit.key.rfind("Pass-", 0) == 0) {
                    mountainPass = true;
                }
            }
            if (mountainPass) {
                action = "Click to cut a river through this mountain range";
            } else if (brushName_ == "Wall 85") {
                action = "Click Base-85 to lay a wall, or a Base-219 cliff to start one";
            } else if (brushName_ == "Wall 139") {
                action = "Click Base-139 to lay a wall";
            } else if (brushName_ == "Bridge 139") {
                action = "Click a straight high coast to start a bridge, or the dark water to lay one";
            } else if (armedBrushKind() == BrushKind::Slope) {
                if (brushName_ == "Ramp") {
                    action = "Click a straight stretch of high coast to turn it into a ramp";
                } else if (brushName_ == "Slope 189") {
                    action = "Click a Base-85 to Base-189 cliff to turn it into a ramp";
                } else if (brushName_ == "Slope 255") {
                    action = "Click a Base-189 to Base-255 cliff to turn it into a ramp";
                } else if (brushName_ == kEvergreenPlateauRampButton) {
                    action = "Click a straight Base-85-1 to Base-190-1 cliff to turn it into a ramp";
                } else if (brushName_ == kEvergreen2PeakRampButton) {
                    action = "Click a straight Base-190-2 to Base-255-2 cliff to turn it into a ramp";
                } else if (brushName_ == kEvergreen2RampButton) {
                    action = "Click a straight Base-85-2 to Base-190-2 cliff or inside corner to turn it into a ramp";
                } else if (brushName_ == kEvergreenPeakRampButton) {
                    action = "Click a straight Base-190-1 to Base-255-1 cliff to turn it into a ramp";
                } else if (brushName_ == kEvergreenRampButton) {
                    action = "Click a straight Base-85-1 to Base-190-2 cliff or inside corner to turn it into a ramp";
                } else if (brushName_ == "Slope 139 to 255") {
                    action = "Click a straight Base-139 to Base-255 cliff to turn it into a slope";
                } else if (brushName_ == "Slope 85 to 219") {
                    action = "Click a Base-85 to Base-219 cliff to turn it into a slope";
                } else if (brushName_ == "Slope 78 to 194") {
                    action = "Click a Base-78 to Base-194 cliff to turn it into a slope";
                } else if (brushName_ == "Slope 80 to 155") {
                    action = "Click a straight Base-80 to Base-155 cliff to turn it into a slope";
                } else if (brushName_ == "Slope 155 to 230") {
                    action = "Click a straight Base-155 to Base-230 cliff to turn it into a slope";
                } else if (brushName_ == "Slope 77 to 183") {
                    action = "Click a straight Base-77 to Base-183 cliff to turn it into a slope";
                } else {
                    action = "Click a cliff to turn it into a slope";
                }
            }
            if (hoverOverMap_) {
                ImGui::SetTooltip("%s\n%s\nEsc cancels\nHeight %d", what, action, hoverHeight_);
            } else {
                ImGui::SetTooltip("%s\n%s\nEsc cancels", what, action);
            }
        }
    } else if (hovered && brushOwnsMap) {
        if (hoverOverMap_) {
            ImGui::SetTooltip("%s\nBrush %s\nClick to paint. Cliffs and coasts keep the bases from touching\n[ smaller    ] larger\nEsc cancels\nHeight %d", brushName_.c_str(), brushSizeLabel(brushSize_).c_str(), hoverHeight_);
        } else {
            ImGui::SetTooltip("%s\nBrush %s\nClick to paint. Cliffs and coasts keep the bases from touching\n[ smaller    ] larger\nEsc cancels", brushName_.c_str(), brushSizeLabel(brushSize_).c_str());
        }
    } else if (hovered && pasteArmed_ && clipboard_) {
        if (clipboard_->heightsOnly) {
            if (hoverOverMap_) {
                ImGui::SetTooltip(
                    "Click to paste the height map %d x %d at %d, %d\nTiles stay as they are. Drag to pan\nRight-click or Esc cancels paste\nHeight %d",
                    clipboard_->tileWidth,
                    clipboard_->tileHeight,
                    originX,
                    originY,
                    hoverHeight_);
            } else {
                ImGui::SetTooltip(
                    "Click to paste the height map %d x %d at %d, %d\nTiles stay as they are. Drag to pan\nRight-click or Esc cancels paste",
                    clipboard_->tileWidth,
                    clipboard_->tileHeight,
                    originX,
                    originY);
            }
        } else if (hoverOverMap_) {
            ImGui::SetTooltip(
                "Click to paste %d x %d at %d, %d\nDrag to pan. Arrow keys or Alt+wheel slide it off the map\nRight-click or Esc cancels paste\nHeight %d",
                clipboard_->tileWidth,
                clipboard_->tileHeight,
                originX,
                originY,
                hoverHeight_);
        } else {
            ImGui::SetTooltip(
                "Click to paste %d x %d at %d, %d\nDrag to pan. Arrow keys or Alt+wheel slide it off the map\nRight-click or Esc cancels paste",
                clipboard_->tileWidth,
                clipboard_->tileHeight,
                originX,
                originY);
        }
    } else if (hovered && !selectionMode_ && selectedSection_ >= 0 && selectedSection_ < static_cast<int>(sections_.size())) {
        const CachedSection& selected = sections_[static_cast<std::size_t>(selectedSection_)];
        if (hoverOverMap_) {
            ImGui::SetTooltip(
                "%s\n%d x %d tiles\nClick to place at %d, %d\nArrow keys or Alt+wheel slide it off the map. Up cuts off the top\nDrag to pan, right-click or Esc cancels\nHeight %d",
                selected.name.c_str(),
                selected.stamp.tileWidth,
                selected.stamp.tileHeight,
                originX,
                originY,
                hoverHeight_);
        } else {
            ImGui::SetTooltip(
                "%s\n%d x %d tiles\nClick to place at %d, %d\nArrow keys or Alt+wheel slide it off the map. Up cuts off the top\nDrag to pan, right-click or Esc cancels",
                selected.name.c_str(),
                selected.stamp.tileWidth,
                selected.stamp.tileHeight,
                originX,
                originY);
        }
    } else if (overMap) {
        if (selectionMode_) {
            if (hasSelection()) {
                if (selection_.solid()) {
                    ImGui::SetTooltip("Selection %d x %d\nShift-drag adds. Shift+Ctrl-drag removes\nCtrl+C copies, Ctrl+X cuts\nHeight %d", selection_.width, selection_.height, hoverHeight_);
                } else {
                    ImGui::SetTooltip("Selection %d tiles\nShift-drag adds. Shift+Ctrl-drag removes\nCtrl+C copies, Ctrl+X cuts\nHeight %d", selection_.count(), hoverHeight_);
                }
            } else {
                ImGui::SetTooltip("Drag to select tiles\nShift-drag adds. Shift+Ctrl-drag removes\nHeight %d", hoverHeight_);
            }
        } else {
            ImGui::SetTooltip("Tile %d, %d\nPixel %d, %d\nHeight %d", tileX, tileY, pixelX, pixelY, hoverHeight_);
        }
    }
    drawSelectionMenu();
    ImGui::EndChild();
}

void EditorApp::drawStatus() {
    const CachedSection* selected = nullptr;
    if (selectedSection_ >= 0 && selectedSection_ < static_cast<int>(sections_.size())) {
        selected = &sections_[static_cast<std::size_t>(selectedSection_)];
    }
    if (!map_) {
        if (featuresTab_) {
            if (featureDelete_) {
                ImGui::TextUnformatted("Delete. Open a map, then click a feature to remove it.");
            } else if (armedFeature_.empty()) {
                ImGui::TextUnformatted("Features. Open a map, then choose a feature to place.");
            } else {
                ImGui::Text("%s. Open a map, then click to place it.", armedFeature_.c_str());
            }
            return;
        }
        if (selected != nullptr) {
            ImGui::Text(
                "Selected %s / %s (%d x %d). Open a map, then click to place it.   %d sections indexed",
                selected->category.c_str(),
                selected->name.c_str(),
                selected->stamp.tileWidth,
                selected->stamp.tileHeight,
                tilesets_.sectionCount());
        } else {
            const std::string indexed = formatText(Text::SectionsIndexed, {std::to_string(tilesets_.sectionCount())});
            ImGui::TextUnformatted(indexed.c_str());
        }
        return;
    }
    // A map Total Annihilation v3.1 cannot play says so in yellow at the left end of
    // the bar, whatever the editor is doing. Hovering it gives the reason.
    const bool tooManyTiles = tooManyTilesForVanilla(map_->tileCount);
    const bool tooLarge = tooLargeForVanilla(map_->tileWidth, map_->tileHeight);
    if (tooManyTiles || tooLarge) {
        const ImVec4 warning = theme::dark() ? ImVec4(0.98f, 0.80f, 0.25f, 1.0f) : ImVec4(0.66f, 0.45f, 0.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, warning);
        ImGui::TextUnformatted(text(Text::NotVanillaTa));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            std::string reason;
            if (tooManyTiles) {
                reason = text(Text::NotVanillaTiles);
            }
            if (tooLarge) {
                reason += (reason.empty() ? "" : "\n") + std::string(text(Text::NewMapGameLimit));
            }
            ImGui::SetTooltip("%s", reason.c_str());
        }
        ImGui::SameLine(0.0f, std::floor(14.0f * theme::unit()));
    }
    const auto finishStatus = [&]() {
        if (!statusNote_.empty()) {
            ImGui::SameLine();
            ImGui::TextUnformatted(statusNote_.c_str());
        }
        if (hoverOverMap_) {
            ImGui::SameLine();
            ImGui::Text("Height %d", hoverHeight_);
        }
    };
    if (startLocationsTab_) {
        if (armedStartPlayer_ > 0) {
            ImGui::Text("Player %d. Click the map to place the pin. Drag a pin to move it. Right-click removes it.   %s   zoom %.0f%%",
                armedStartPlayer_,
                map_->displayName.c_str(),
                zoom_ * 100.0f);
        } else {
            ImGui::Text("Start locations. Press 1-9, or 0 for Player 10, then click the map.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
        }
        finishStatus();
        return;
    }
    if (featuresTab_) {
        if (featureDelete_) {
            ImGui::Text("Delete. Click a feature to remove it.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
        } else if (featurePasteArmed_) {
            ImGui::Text("Pasting %d features. Click the map to place them, Esc cancels.   %s   zoom %.0f%%",
                static_cast<int>(featureClipboard_.size()),
                map_->displayName.c_str(),
                zoom_ * 100.0f);
        } else if (armedFeature_.empty()) {
            if (featureSelectMode_ && selectedFeatures_.empty()) {
                ImGui::Text("Select. Drag a box around features, or click one. Shift adds.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
            } else if (selectedFeatures_.empty()) {
                ImGui::Text("Features. Choose one on the left to place, or press Select to select features.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
            } else {
                ImGui::Text("%d selected. Copy, Cut, Paste and Delete work on them.   %s   zoom %.0f%%",
                    static_cast<int>(selectedFeatures_.size()),
                    map_->displayName.c_str(),
                    zoom_ * 100.0f);
            }
        } else {
            ImGui::Text("%s. Click the map to place it. Right-click a marker to remove it.   %s   zoom %.0f%%",
                armedFeature_.c_str(),
                map_->displayName.c_str(),
                zoom_ * 100.0f);
        }
        finishStatus();
        return;
    }
    if (pasteArmed_ && clipboard_) {
        if (clipboard_->heightsOnly) {
            ImGui::Text(
                "Pasting height map %d x %d, shifted %d, %d. Tiles stay in place. Drag to pan, click to place, Esc cancels.   %s   zoom %.0f%%",
                clipboard_->tileWidth,
                clipboard_->tileHeight,
                placeOffsetX_,
                placeOffsetY_,
                map_->displayName.c_str(),
                zoom_ * 100.0f);
        } else {
            ImGui::Text(
                "Pasting %d x %d, shifted %d, %d. Drag to pan, click to place, Esc cancels.   %s   zoom %.0f%%",
                clipboard_->tileWidth,
                clipboard_->tileHeight,
                placeOffsetX_,
                placeOffsetY_,
                map_->displayName.c_str(),
                zoom_ * 100.0f);
        }
        finishStatus();
        return;
    }
    if (selectionMode_) {
        if (hasSelection()) {
            if (selection_.solid()) {
                ImGui::Text(
                    "Selected %d x %d tiles at %d, %d. Shift-drag adds, Shift+Ctrl-drag removes.   %s   zoom %.0f%%",
                    selection_.width,
                    selection_.height,
                    selection_.x,
                    selection_.y,
                    map_->displayName.c_str(),
                    zoom_ * 100.0f);
            } else {
                ImGui::Text(
                    "Selected %d tiles. Shift-drag adds, Shift+Ctrl-drag removes.   %s   zoom %.0f%%",
                    selection_.count(),
                    map_->displayName.c_str(),
                    zoom_ * 100.0f);
            }
        } else {
            ImGui::Text("Drag to select tiles. Shift-drag adds, Shift+Ctrl-drag removes.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
        }
        finishStatus();
        return;
    }
    if (selected != nullptr) {
        ImGui::Text(
            "Placing %s / %s (%d x %d), shifted %d, %d. Arrows or Alt+wheel slide it off the map.   %s   zoom %.0f%%",
            selected->category.c_str(),
            selected->name.c_str(),
            selected->stamp.tileWidth,
            selected->stamp.tileHeight,
            placeOffsetX_,
            placeOffsetY_,
            map_->displayName.c_str(),
            zoom_ * 100.0f);
        finishStatus();
        return;
    }
    ImGui::Text(
        "%s   %d x %d tiles   %d x %d px   %d tile images in the UFO   sea %d   %d features   zoom %.0f%%",
        map_->displayName.c_str(),
        map_->tileWidth,
        map_->tileHeight,
        map_->pixelWidth(),
        map_->pixelHeight(),
        map_->tileCount,
        map_->seaLevel,
        static_cast<int>(map_->features.size()),
        zoom_ * 100.0f);
    finishStatus();
}

bool EditorApp::saveMap() {
    if (!map_) {
        return false;
    }
    if (map_->source.empty()) {
        return saveMapAs();
    }
    return saveMapTo(map_->source);
}

bool EditorApp::saveMapAs() {
    if (!map_) {
        return false;
    }
    const auto directory = map_->source.has_parent_path() ? map_->source.parent_path() : suggestedOpenDirectory();
    std::filesystem::path suggested = map_->source.filename();
    if (suggested.empty()) {
        suggested = (map_->displayName.empty() ? std::string("map") : map_->displayName) + ".ufo";
    }
    if (const auto chosen = saveMapDialog(directory, suggested)) {
        return saveMapTo(*chosen);
    }
    return false;
}

bool EditorApp::exportMapPng() {
    if (!map_) {
        return false;
    }
    const auto directory = map_->source.has_parent_path() ? map_->source.parent_path() : suggestedOpenDirectory();
    std::string stem = map_->source.stem().string();
    if (stem.empty()) {
        stem = map_->displayName.empty() ? std::string("map") : map_->displayName;
    }
    const std::filesystem::path suggested = stem + ".png";
    const auto chosen = savePngDialog(directory, suggested);
    if (!chosen) {
        return false;
    }
    std::filesystem::path destination = *chosen;
    if (destination.extension().empty()) {
        destination += ".png";
    }
    try {
        writeMapPng(*map_, destination);
        statusNote_ = "Exported " + destination.filename().string();
        error_.clear();
        return true;
    } catch (const std::exception& ex) {
        error_ = ex.what();
        statusNote_ = "Could not export " + destination.filename().string();
        return false;
    }
}

bool EditorApp::saveMapTo(const std::filesystem::path& path) {
    if (!map_) {
        return false;
    }
    if (savingMap()) {
        statusNote_ = "Wait for the save to finish";
        return false;
    }
    try {
        std::filesystem::path destination = path;
        if (destination.extension().empty()) {
            destination += ".ufo";
        }
        // The thread writes its own copy, so edits made during the save do not race with it.
        auto snapshot = std::make_shared<const LoadedMap>(*map_);
        mapSavePath_ = destination;
        mapSaveSerial_ = mapSerial_;
        // An edit made while the file is written marks the map dirty again.
        mapDirty_ = false;
        mapSave_ = std::async(std::launch::async, [snapshot, destination]() -> std::string {
            try {
                saveMapFile(*snapshot, destination);
                return {};
            } catch (const std::exception& ex) {
                std::string message = ex.what();
                return message.empty() ? std::string("The map could not be saved.") : message;
            }
        });
        statusNote_ = "Saving " + destination.filename().string() + "...";
        error_.clear();
        return true;
    } catch (const std::exception& ex) {
        error_ = ex.what();
        return false;
    }
}

void EditorApp::finishMapSave() {
    if (!mapSave_.valid() || mapSave_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    const std::string failure = mapSave_.get();
    // The saved map may no longer be the active one, so it is found by its serial.
    const int savedIndex = documentIndexForSerial(mapSaveSerial_);
    const bool active = savedIndex >= 0 && savedIndex == activeDocument_;
    if (failure.empty()) {
        if (savedIndex >= 0) {
            MapDocument& document = documents_[static_cast<std::size_t>(savedIndex)];
            std::optional<LoadedMap>& saved = active ? map_ : document.map;
            if (saved) {
                saved->source = mapSavePath_;
                saved->displayName = mapSavePath_.stem().string();
            }
            document.untitledNumber = 0;
        }
        statusNote_ = "Saved " + mapSavePath_.filename().string();
        error_.clear();
        if (closeAfterSaveSerial_ != 0 && closeAfterSaveSerial_ == mapSaveSerial_) {
            closeAfterSaveSerial_ = 0;
            closeDocument(savedIndex);
        }
        if (quitAfterSave_) {
            quitAfterSave_ = false;
            requestClose();
        }
        return;
    }
    if (active) {
        mapDirty_ = true;
    } else if (savedIndex >= 0) {
        documents_[static_cast<std::size_t>(savedIndex)].dirty = true;
    }
    closeAfterSaveSerial_ = 0;
    error_ = failure;
    statusNote_ = "Could not save " + mapSavePath_.filename().string();
    if (quitAfterSave_) {
        // The quit waits on the user again, since the changes were not written.
        quitAfterSave_ = false;
        if (map_ && mapDirty_) {
            showClosePrompt_ = true;
        }
    }
}

void EditorApp::drawSavingOverlay(float left, float top, float right, float bottom) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    // The blurred picture is the 2D map, even when the 3D view was showing.
    if (!drawGlass(savingGlass_, draw, left, top, right, bottom, 0.0f, true)) {
        draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), theme::canvasColor());
    }
    // Keeps the pointer from reaching the map underneath.
    ImGui::SetCursorScreenPos(ImVec2(left, top));
    ImGui::InvisibleButton("saving-blocker", ImVec2(std::max(1.0f, right - left), std::max(1.0f, bottom - top)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    const std::string name = map_ && !map_->displayName.empty() && mapSerial_ == mapSaveSerial_
        ? map_->displayName
        : mapSavePath_.stem().string();
    const std::string label = "Saving " + (name.empty() ? std::string("map") : name) + "...";
    const ImVec2 center((left + right) * 0.5f, (top + bottom) * 0.5f);
    const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
    constexpr float kRadius = 18.0f;
    constexpr float kGap = 14.0f;
    const float panelW = std::max(textSize.x, kRadius * 2.0f) + 48.0f;
    const float panelH = kRadius * 2.0f + kGap + textSize.y + 40.0f;
    const ImVec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const ImVec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);
    draw->AddRectFilled(panelMin, panelMax, IM_COL32(20, 22, 26, 200), 10.0f);
    draw->AddRect(panelMin, panelMax, theme::glassStyle().outline, 10.0f);

    const ImVec2 spinner(center.x, panelMin.y + 20.0f + kRadius);
    const float time = static_cast<float>(ImGui::GetTime());
    constexpr float kPi = 3.14159265f;
    draw->PathArcTo(spinner, kRadius, 0.0f, 2.0f * kPi, 40);
    draw->PathStroke(IM_COL32(255, 255, 255, 50), 0, 3.0f);
    const float start = time * 5.0f;
    draw->PathArcTo(spinner, kRadius, start, start + kPi * 1.3f, 32);
    draw->PathStroke(theme::accentColor(), 0, 3.0f);
    draw->AddText(ImVec2(center.x - textSize.x * 0.5f, spinner.y + kRadius + kGap), IM_COL32(240, 240, 240, 255), label.c_str());
}

void EditorApp::handleDroppedPaths(const std::vector<std::filesystem::path>& paths) {
    hpiEditor_.importPaths(paths);
}

void EditorApp::requestClose() {
    if (savingMap()) {
        // The file is still being written. Quit once it is done.
        quitAfterSave_ = true;
        quit_ = false;
        return;
    }
    if (hpiEditor_.dirty() && !hpiQuitApproved_) {
        // The HPI editor asks about its own changes first, then calls back through takeQuitApproval.
        hpiEditor_.requestQuit();
        quit_ = false;
        return;
    }
    // Every open map with unsaved changes is asked about in turn, the active one first.
    if (map_ && mapDirty_ && quitDiscarded_.count(mapSerial_) == 0) {
        closePromptForTab_ = false;
        showClosePrompt_ = true;
        quit_ = false;
        return;
    }
    for (int i = 0; i < static_cast<int>(documents_.size()); ++i) {
        if (i != activeDocument_ && documentDirty(i) && quitDiscarded_.count(documents_[static_cast<std::size_t>(i)].serial) == 0) {
            activateDocument(i);
            closePromptForTab_ = false;
            showClosePrompt_ = true;
            quit_ = false;
            return;
        }
    }
    quit_ = true;
}

void EditorApp::drawAbout() {
    // The ### part is the popup id. OpenPopup and BeginPopupModal have to use that same string.
    const std::string aboutTitle = withId(Text::AboutTitle, "About Galaxy Editor");
    if (aboutRequested_) {
        ImGui::OpenPopup(aboutTitle.c_str());
        aboutRequested_ = false;
    }
    if (!ImGui::BeginPopupModal(aboutTitle.c_str(), nullptr, theme::alertFlags())) {
        return;
    }
    theme::alertText(applicationTitle().c_str(), text(Text::AboutBody));
    const float okWidth = theme::pushButtonWidth(text(Text::Ok));
    theme::alignRight(okWidth);
    if (theme::defaultButton(text(Text::Ok), ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::drawClosePrompt() {
    if (!showClosePrompt_) {
        return;
    }
    const std::string unsavedTitle = withId(Text::UnsavedTitle, "Unsaved Changes");
    ImGui::OpenPopup(unsavedTitle.c_str());
    if (!ImGui::BeginPopupModal(unsavedTitle.c_str(), nullptr, theme::alertFlags())) {
        return;
    }
    // Closing one tab, or quitting with several maps open, names the map being asked about.
    const bool forTab = closePromptForTab_;
    const bool named = forTab || documents_.size() > 1;
    if (named) {
        const std::string message = formatText(Text::SaveChangesTo, {documentTitle(activeDocument_)});
        theme::alertText(message.c_str(), text(forTab ? Text::CloseUnsavedDetail : Text::UnsavedDetail));
    } else {
        theme::alertText(text(Text::UnsavedMessage), text(Text::UnsavedDetail));
    }
    if (ImGui::Button(text(named ? Text::DontSave : Text::ExitWithoutSaving))) {
        showClosePrompt_ = false;
        closePromptForTab_ = false;
        ImGui::CloseCurrentPopup();
        if (forTab) {
            closeDocument(activeDocument_);
        } else if (named) {
            // Ask about the next map with changes, or quit when none is left.
            quitDiscarded_.insert(mapSerial_);
            requestClose();
        } else {
            quit_ = true;
        }
    }
    ImGui::SameLine();
    const char* cancelLabel = text(Text::Cancel);
    const char* saveLabel = text(named ? Text::Save : Text::SaveAndExit);
    const float cancelWidth = theme::pushButtonWidth(cancelLabel);
    const float saveWidth = theme::pushButtonWidth(saveLabel);
    theme::alignRight(cancelWidth + ImGui::GetStyle().ItemSpacing.x + saveWidth);
    if (ImGui::Button(cancelLabel, ImVec2(cancelWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        showClosePrompt_ = false;
        closePromptForTab_ = false;
        quitDiscarded_.clear();
        // The HPI editor asks again next time, since nothing quit.
        hpiQuitApproved_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (theme::defaultButton(saveLabel, ImVec2(saveWidth, 0.0f))) {
        if (saveMap()) {
            // The save runs in the background. The tab closes, or the editor moves on
            // to quitting, when it finishes.
            showClosePrompt_ = false;
            if (forTab) {
                closeAfterSaveSerial_ = mapSerial_;
            } else {
                quitAfterSave_ = true;
            }
            closePromptForTab_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

namespace {

struct MapSettingsDraft {
    MapSettings settings;
    char name[160] = {};
    char description[512] = {};
    char planet[80] = {};
    char players[64] = {};
    char memory[64] = {};
    char aiProfile[64] = {};
    char meteorWeapon[64] = {};
    char useOnlyUnits[80] = {};
    char missionHint[160] = {};
    char brief[80] = {};
    char narration[80] = {};
    char glamour[80] = {};
};

MapSettingsDraft mapSettingsDraft;

void copyField(char* dest, std::size_t size, std::string_view text) {
    if (size == 0) {
        return;
    }
    const std::size_t count = std::min(size - 1, text.size());
    for (std::size_t i = 0; i < count; ++i) {
        dest[i] = text[i];
    }
    dest[count] = '\0';
}

void loadMapSettingsDraft(const MapSettings& settings) {
    mapSettingsDraft.settings = settings;
    copyField(mapSettingsDraft.name, sizeof(mapSettingsDraft.name), settings.name);
    copyField(mapSettingsDraft.description, sizeof(mapSettingsDraft.description), settings.description);
    copyField(mapSettingsDraft.planet, sizeof(mapSettingsDraft.planet), settings.planet);
    copyField(mapSettingsDraft.players, sizeof(mapSettingsDraft.players), settings.players);
    copyField(mapSettingsDraft.memory, sizeof(mapSettingsDraft.memory), settings.memory);
    copyField(mapSettingsDraft.aiProfile, sizeof(mapSettingsDraft.aiProfile), settings.aiProfile);
    copyField(mapSettingsDraft.meteorWeapon, sizeof(mapSettingsDraft.meteorWeapon), settings.meteorWeapon);
    copyField(mapSettingsDraft.useOnlyUnits, sizeof(mapSettingsDraft.useOnlyUnits), settings.useOnlyUnits);
    copyField(mapSettingsDraft.missionHint, sizeof(mapSettingsDraft.missionHint), settings.missionHint);
    copyField(mapSettingsDraft.brief, sizeof(mapSettingsDraft.brief), settings.brief);
    copyField(mapSettingsDraft.narration, sizeof(mapSettingsDraft.narration), settings.narration);
    copyField(mapSettingsDraft.glamour, sizeof(mapSettingsDraft.glamour), settings.glamour);
}

struct AiProfileChoice {
    const char* id;
    const char* label;
};

constexpr AiProfileChoice kAiProfiles[] = {
    {"DEFAULT", "DEFAULT"},
    {"Metal", "Metal"},
    {"SeaBattle", "Sea Battle"},
    {"Hover", "Hover"},
    {"Acid", "Acid"},
    {"AirBattle", "Air Battle"},
    {"Krogoth", "Krogoth"},
    {"Missions", "Missions"},
    {"Urban", "Urban"},
    {"Waterwrld", "Water World"},
};

constexpr const char* kPlanets[] = {
    "Acid",
    "Archipelago",
    "Crystal",
    "Desert",
    "Green planet",
    "Ice",
    "Lava",
    "Lunar",
    "Lush",
    "Metal",
    "Red Planet",
    "Slate",
    "Urban",
    "Water World",
    "Wet Desert",
};

bool editBoundedInt(const char* label, int& value, int min, int max) {
    ImGui::SetNextItemWidth(150.0f);
    const bool changed = ImGui::InputInt(label, &value);
    if (value < min) {
        value = min;
    }
    if (value > max) {
        value = max;
    }
    return changed;
}

const char* aiProfileLabel(const char* id) {
    const std::string current = lowerText(id);
    for (const AiProfileChoice& choice : kAiProfiles) {
        if (current == lowerText(choice.id)) {
            return choice.label;
        }
    }
    return id[0] == '\0' ? "DEFAULT" : id;
}

}  // namespace

void EditorApp::loadEditorOptions() {
    const EditorSettings settings = loadEditorSettings(editorSettingsPath());
    previewSize_ = settings.previewSize;
    heightGridSize_ = settings.heightGridSize;
    sidebarFontPercent_ = settings.sidebarFontPercent;
    autosaveMinutes_ = settings.autosaveMinutes;
    autosaveBackups_ = settings.autosaveBackups;
    appearanceMode_ = settings.appearance;
    seaOpacity_ = settings.seaOpacity;
    seaColor_ = settings.seaColor;
    randomMapFontSize_ = settings.randomMapFontSize;
    minimapPercent_ = settings.minimapPercent;
    gameFolder_ = settings.gameFolder;
    gameFolderAsked_ = settings.gameFolderAsked;
    if (isKnownLanguage(settings.language)) {
        setActiveLanguage(settings.language);
    }
}

void EditorApp::saveEditorOptions() const {
    EditorSettings settings;
    settings.previewSize = previewSize_;
    settings.heightGridSize = heightGridSize_;
    settings.sidebarFontPercent = sidebarFontPercent_;
    settings.autosaveMinutes = autosaveMinutes_;
    settings.autosaveBackups = autosaveBackups_;
    settings.appearance = appearanceMode_;
    settings.seaOpacity = seaOpacity_;
    settings.randomMapFontSize = randomMapFontSize_;
    settings.minimapPercent = minimapPercent_;
    settings.seaColor = seaColor_;
    settings.gameFolder = gameFolder_;
    settings.gameFolderAsked = gameFolderAsked_;
    settings.language = activeLanguage();
    saveEditorSettings(editorSettingsPath(), settings);
}

void EditorApp::drawMapSettings() {
    if (!showMapSettings_) {
        return;
    }
    if (!map_) {
        showMapSettings_ = false;
        return;
    }
    if (mapSettingsFresh_) {
        loadMapSettingsDraft(readMapSettings(*map_));
        mapSettingsFresh_ = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 280.0f, viewport->WorkPos.y + 36.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(640.0f, 680.0f), ImGuiCond_FirstUseEver);
    const std::string mapSettingsTitle = withId(Text::MapSettings, "Map Settings");
    if (!ImGui::Begin(mapSettingsTitle.c_str(), &showMapSettings_)) {
        ImGui::End();
        return;
    }
    const char* settingsTabs[] = {text(Text::TabMap), text(Text::TabWorld), text(Text::TabSpecial), text(Text::TabRules)};
    theme::segmented("map-settings-tabs", settingsTabs, 4, mapSettingsTab_);
    ImGui::Spacing();
    const float footer = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 3.0f + 8.0f;
    ImGui::BeginChild("map-settings-body", ImVec2(0.0f, -footer), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    {
        if (mapSettingsTab_ == 0) {
            theme::helpText("These are the map settings Annihilator stores in the OTA file. Sea level is stored in the terrain. Start positions stay on the Start Locations tab.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-80.0f);
            ImGui::InputText("Name", mapSettingsDraft.name, sizeof(mapSettingsDraft.name));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stored as missionname. The skirmish list shows the file name. The description is the line under it.");
            }
            ImGui::InputTextMultiline("Description", mapSettingsDraft.description, sizeof(mapSettingsDraft.description), ImVec2(-80.0f, 80.0f));
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("Planet", mapSettingsDraft.planet[0] == '\0' ? "(none)" : mapSettingsDraft.planet)) {
                if (ImGui::Selectable("(none)", mapSettingsDraft.planet[0] == '\0')) {
                    mapSettingsDraft.planet[0] = '\0';
                }
                for (const char* planet : kPlanets) {
                    if (ImGui::Selectable(planet, std::string(mapSettingsDraft.planet) == planet)) {
                        copyField(mapSettingsDraft.planet, sizeof(mapSettingsDraft.planet), planet);
                    }
                }
                if (mapSettingsDraft.planet[0] != '\0') {
                    bool known = false;
                    for (const char* planet : kPlanets) {
                        if (std::string(mapSettingsDraft.planet) == planet) {
                            known = true;
                            break;
                        }
                    }
                    if (!known && ImGui::Selectable(mapSettingsDraft.planet, true)) {
                        // Keep a planet name already stored on this map.
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Planet name", mapSettingsDraft.planet, sizeof(mapSettingsDraft.planet));
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Players", mapSettingsDraft.players, sizeof(mapSettingsDraft.players));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A note such as 2 or 2, 4, 6, 8, 10. The game counts players from the start positions.");
            }
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Memory", mapSettingsDraft.memory, sizeof(mapSettingsDraft.memory));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A note such as 16 mb. The game does not read it.");
            }
            ImGui::Separator();
            const char* profilePreview = aiProfileLabel(mapSettingsDraft.aiProfile);
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("AI profile", profilePreview)) {
                for (const AiProfileChoice& choice : kAiProfiles) {
                    const bool selected = lowerText(mapSettingsDraft.aiProfile) == lowerText(choice.id);
                    if (ImGui::Selectable(choice.label, selected)) {
                        copyField(mapSettingsDraft.aiProfile, sizeof(mapSettingsDraft.aiProfile), choice.id);
                    }
                }
                bool known = false;
                for (const AiProfileChoice& choice : kAiProfiles) {
                    if (lowerText(mapSettingsDraft.aiProfile) == lowerText(choice.id)) {
                        known = true;
                        break;
                    }
                }
                if (!known && mapSettingsDraft.aiProfile[0] != '\0' && ImGui::Selectable(mapSettingsDraft.aiProfile, true)) {
                    // Keep a custom profile shipped with this map.
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("DEFAULT is ordinary land. Metal, Sea Battle, and Hover match those kinds of maps. The other names are the standard TA profiles.");
            }
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Profile file", mapSettingsDraft.aiProfile, sizeof(mapSettingsDraft.aiProfile));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The AI file name without .txt. Sea Battle is saved as SeaBattle.");
            }
        }
        if (mapSettingsTab_ == 1) {
            MapSettings& world = mapSettingsDraft.settings;
            editBoundedInt("Min wind speed", world.minWind, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The least energy a wind generator can make. 0 is calm.");
            }
            editBoundedInt("Max wind speed", world.maxWind, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("The most energy a wind generator can make. Most maps use 3000.");
            }
            editBoundedInt("Tidal strength", world.tidalStrength, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Energy from each tidal generator. 20 is the usual value. 0 means tides make nothing.");
            }
            editBoundedInt("Solar strength", world.solarStrength, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Saved with the map, as in Annihilator. A solar collector's output comes from the unit itself.");
            }
            editBoundedInt("Sea level", world.seaLevel, 0, 255);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Heights below this are water. 75 is the usual shoreline. This is stored in the terrain file.");
            }
            editBoundedInt("Gravity", world.gravity, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("112 is normal. Lower gravity lets shots travel farther. Leave it alone except on a moon map.");
            }
            editBoundedInt("Surface metal", world.surfaceMetal, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Metal that can be extracted from ordinary ground. Most maps use a small number such as 3.");
            }
            editBoundedInt("Moho metal", world.mohoMetal, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stored with the map, as in Annihilator. A moho mine's yield comes from the unit itself.");
            }
        }
        if (mapSettingsTab_ == 2) {
            MapSettings& special = mapSettingsDraft.settings;
            ImGui::Checkbox("Impassable water", &special.lavaWorld);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Lava world. Units cannot cross the water.");
            }
            ImGui::Checkbox("Water does damage", &special.waterDoesDamage);
            editBoundedInt("Water damage", special.waterDamage, 0, 20000);
            ImGui::Checkbox("Shots stop at the water", &special.waterBlocksShots);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Projectiles and debris do not pass through the water surface.");
            }
            ImGui::SeparatorText("Meteors");
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Weapon", mapSettingsDraft.meteorWeapon, sizeof(mapSettingsDraft.meteorWeapon));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Leave this empty to turn meteors off. A weapon name turns the meteor shower on.");
            }
            editBoundedInt("Radius", special.meteorRadius, 0, 20000);
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputDouble("Density", &special.meteorDensity, 0.1, 1.0, "%.4g");
            if (special.meteorDensity < 0) {
                special.meteorDensity = 0;
            }
            editBoundedInt("Duration", special.meteorDuration, 0, 20000);
            editBoundedInt("Interval", special.meteorInterval, 0, 20000);
        }
        if (mapSettingsTab_ == 3) {
            MapSettings& rules = mapSettingsDraft.settings;
            ImGui::SeparatorText("Starting resources");
            editBoundedInt("Human metal", rules.humanMetal, 0, 2000000);
            editBoundedInt("Human energy", rules.humanEnergy, 0, 2000000);
            editBoundedInt("Computer metal", rules.computerMetal, 0, 2000000);
            editBoundedInt("Computer energy", rules.computerEnergy, 0, 2000000);
            ImGui::SeparatorText("Rules");
            ImGui::Checkbox("True line of sight", &rules.lineOfSight);
            ImGui::Checkbox("Mapping rules", &rules.mapping);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Most skirmish maps leave this off.");
            }
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputDouble("Kill score multiplier", &rules.killMultiplier, 1.0, 10.0, "%.4g");
            ImGui::SetNextItemWidth(150.0f);
            ImGui::InputDouble("Time score multiplier", &rules.timeMultiplier, 1.0, 10.0, "%.4g");
            editBoundedInt("Max units", rules.maxUnits, 0, 5000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Campaign unit cap. 0 leaves the key off unless the map already has one. Skirmish uses the game setup.");
            }
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("Use only units", mapSettingsDraft.useOnlyUnits, sizeof(mapSettingsDraft.useOnlyUnits));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Optional TDF file in camps/useonly that limits what can be built.");
            }
            ImGui::SeparatorText("Mission text");
            ImGui::SetNextItemWidth(-80.0f);
            ImGui::InputText("Hint", mapSettingsDraft.missionHint, sizeof(mapSettingsDraft.missionHint));
            ImGui::SetNextItemWidth(-80.0f);
            ImGui::InputText("Brief", mapSettingsDraft.brief, sizeof(mapSettingsDraft.brief));
            ImGui::SetNextItemWidth(-80.0f);
            ImGui::InputText("Narration", mapSettingsDraft.narration, sizeof(mapSettingsDraft.narration));
            ImGui::SetNextItemWidth(-80.0f);
            ImGui::InputText("Glamour", mapSettingsDraft.glamour, sizeof(mapSettingsDraft.glamour));
        }
    }
    ImGui::EndChild();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    const float buttonWidth = theme::pushButtonWidth("Update Map");
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) {
        showMapSettings_ = false;
    }
    ImGui::SameLine();
    if (theme::defaultButton("Update Map", ImVec2(buttonWidth, 0.0f))) {
        MapSettings settings = mapSettingsDraft.settings;
        settings.name = mapSettingsDraft.name;
        settings.description = mapSettingsDraft.description;
        settings.planet = mapSettingsDraft.planet;
        settings.players = mapSettingsDraft.players;
        settings.memory = mapSettingsDraft.memory;
        settings.aiProfile = mapSettingsDraft.aiProfile;
        settings.meteorWeapon = mapSettingsDraft.meteorWeapon;
        settings.useOnlyUnits = mapSettingsDraft.useOnlyUnits;
        settings.missionHint = mapSettingsDraft.missionHint;
        settings.brief = mapSettingsDraft.brief;
        settings.narration = mapSettingsDraft.narration;
        settings.glamour = mapSettingsDraft.glamour;
        const int previousSea = map_->seaLevel;
        const auto previousOta = map_->otaText;
        const std::string previousName = map_->missionName;
        applyMapSettings(*map_, settings);
        if (map_->seaLevel != previousSea) {
            heightDirty_ = true;
        }
        if (map_->seaLevel != previousSea || map_->otaText != previousOta || map_->missionName != previousName) {
            markMapDirty();
            statusNote_ = "Map settings updated";
        }
        showMapSettings_ = false;
    }
    ImGui::End();
}

void EditorApp::setGameFolder(const std::filesystem::path& folder) {
    gameFolder_ = folder;
    // Clearing the folder returns to a first launch, so the next start asks again.
    gameFolderAsked_ = !folder.empty();
    saveEditorOptions();
    reloadTilesets();
}

void EditorApp::chooseGameFolder() {
    std::filesystem::path start = gameFolder_;
    std::error_code error;
    if (!start.empty() && !std::filesystem::is_directory(start, error)) {
        start = start.parent_path();
        if (!std::filesystem::is_directory(start, error)) {
            start.clear();
        }
    }
    const auto chosen = chooseFolderDialog("Select the Total Annihilation install folder", start);
    if (chosen && *chosen != gameFolder_) {
        setGameFolder(*chosen);
    }
}

void EditorApp::drawOptions() {
    if (!showOptions_) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 320.0f, viewport->WorkPos.y + 48.0f), ImGuiCond_FirstUseEver);
    const std::string optionsTitle = withId(Text::OptionsTitle, "Galaxy Editor Options");
    if (!ImGui::Begin(
            optionsTitle.c_str(),
            &showOptions_,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const float width = 420.0f * ImGui::GetFontSize() / 13.0f;
    ImGui::Dummy(ImVec2(width, 0.0f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::PushItemWidth(width * 0.62f);
    bool optionsChanged = false;
    ImGui::SeparatorText(text(Text::SectionLanguage));
    {
        const int current = std::max(0, languageIndex(activeLanguage()));
        if (ImGui::BeginCombo("##editor-language", languageAt(current).nativeName)) {
            for (int i = 0; i < languageCount(); ++i) {
                const bool selected = i == current;
                if (ImGui::Selectable(languageAt(i).nativeName, selected)) {
                    setActiveLanguage(languageAt(i).id);
                    optionsChanged = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    theme::helpText(text(Text::LanguageHelp));
    ImGui::SeparatorText(text(Text::SectionAppearance));
    {
        const char* appearances[] = {text(Text::AppearanceAuto), text(Text::AppearanceLight), text(Text::AppearanceDark)};
        if (theme::segmented("appearance", appearances, 3, appearanceMode_)) {
            optionsChanged = true;
        }
    }
    theme::helpText(text(Text::AppearanceHelp));
    ImGui::SeparatorText(text(Text::Section3d));
    {
        const std::string seaOpacity = withId(Text::SeaOpacity, "Sea opacity");
        optionsChanged |= ImGui::SliderInt(seaOpacity.c_str(), &seaOpacity_, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    }
    {
        float rgb[3] = {
            static_cast<float>((seaColor_ >> 16) & 0xFF) / 255.0f,
            static_cast<float>((seaColor_ >> 8) & 0xFF) / 255.0f,
            static_cast<float>(seaColor_ & 0xFF) / 255.0f};
        const std::string seaColor = withId(Text::SeaColor, "Sea color");
        if (ImGui::ColorEdit3(seaColor.c_str(), rgb, ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_PickerHueWheel)) {
            const auto channel = [](float value) { return std::clamp(static_cast<int>(std::lround(value * 255.0f)), 0, 255); };
            seaColor_ = (channel(rgb[0]) << 16) | (channel(rgb[1]) << 8) | channel(rgb[2]);
            optionsChanged = true;
        }
    }
    theme::helpText(text(Text::View3dHelp));
    ImGui::SeparatorText(text(Text::SectionTa));
    if (gameFolder_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextUnformatted(text(Text::NotSet));
        ImGui::PopStyleColor();
    } else {
        ImGui::TextWrapped("%s", pathToUtf8(gameFolder_).c_str());
    }
    if (loadingTilesets()) {
        const std::string loading = formatText(Text::LoadingArchives, {std::to_string(tilesetLoadDone()), std::to_string(tilesetLoadTotal())});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextUnformatted(loading.c_str());
        ImGui::PopStyleColor();
    } else if (!gameFolder_.empty() && !gameFolderFound_) {
        ImGui::TextWrapped("%s", text(Text::FolderNotFound));
    } else if (!gameFolder_.empty() && gameFolderArchives_ == 0) {
        ImGui::TextWrapped("%s", text(Text::FolderNoArchives));
    } else if (!gameFolder_.empty()) {
        const std::string loaded = formatText(Text::ArchivesLoaded, {std::to_string(gameFolderArchives_)});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextUnformatted(loaded.c_str());
        ImGui::PopStyleColor();
    }
    if (!loadingTilesets() && tilesetLoadTotal() > gameFolderArchives_) {
        const std::string beside = formatText(Text::ArchivesBeside, {std::to_string(tilesetLoadTotal() - gameFolderArchives_)});
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextUnformatted(beside.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    {
        const std::string chooseLabel = theme::withEllipsis(text(Text::ChooseFolder));
        const float chooseWidth = theme::pushButtonWidth(chooseLabel.c_str());
        if (ImGui::Button(chooseLabel.c_str(), ImVec2(chooseWidth, 0.0f))) {
            chooseGameFolder();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(gameFolder_.empty());
        const std::string clearLabel = std::string(text(Text::Clear)) + "###Clear";
        if (ImGui::Button(clearLabel.c_str(), ImVec2(theme::pushButtonWidth(text(Text::Clear)), 0.0f))) {
            setGameFolder({});
        }
        ImGui::EndDisabled();
    }
    theme::helpText(text(Text::GameFolderHelp));
    ImGui::SeparatorText(text(Text::SectionPreviews));
    {
        const std::string previewSize = withId(Text::PreviewSize, "Preview size");
        optionsChanged |= ImGui::SliderInt(previewSize.c_str(), &previewSize_, 32, 256, "%d px", ImGuiSliderFlags_AlwaysClamp);
    }
    theme::helpText(text(Text::PreviewHelp));
    ImGui::SeparatorText(text(Text::SectionHeight));
    {
        const std::string gridSize = withId(Text::GridSize, "Grid size");
        if (ImGui::SliderInt(gridSize.c_str(), &heightGridSize_, 16, 256, "%d px", ImGuiSliderFlags_AlwaysClamp)) {
            heightGridSize_ = std::clamp((heightGridSize_ + 8) / 16 * 16, 16, 256);
            optionsChanged = true;
        }
    }
    theme::helpText(text(Text::HeightHelp));
    ImGui::SeparatorText(text(Text::SectionTilesets));
    {
        const std::string sidebarFont = withId(Text::SidebarFont, "Sidebar font");
        optionsChanged |= ImGui::SliderInt(sidebarFont.c_str(), &sidebarFontPercent_, 75, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    }
    theme::helpText(text(Text::SidebarFontHelp));
    ImGui::SeparatorText(text(Text::SectionAutosave));
    {
        const std::string autosaveEvery = withId(Text::AutosaveEvery, "Autosave every");
        const std::string backupCopies = withId(Text::BackupCopies, "Backup copies");
        optionsChanged |= ImGui::SliderInt(autosaveEvery.c_str(), &autosaveMinutes_, 1, 120, "%d min", ImGuiSliderFlags_AlwaysClamp);
        optionsChanged |= ImGui::SliderInt(backupCopies.c_str(), &autosaveBackups_, 1, 20, "%d", ImGuiSliderFlags_AlwaysClamp);
    }
    theme::helpText(text(Text::AutosaveHelp));
    ImGui::PopItemWidth();
    ImGui::PopTextWrapPos();
    if (optionsChanged) {
        saveEditorOptions();
    }
    ImGui::End();
}

void EditorApp::drawNewMap() {
    if (!showNew_) {
        return;
    }
    const std::string newMapTitle = withId(Text::NewMap, "New Map");
    ImGui::OpenPopup(newMapTitle.c_str());
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(newMapTitle.c_str(), &showNew_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const bool sizeOk = newWidth_ >= kMinMapTiles && newHeight_ >= kMinMapTiles && newWidth_ <= kMaxMapTiles && newHeight_ <= kMaxMapTiles;
    theme::helpText(text(Text::NewMapHelp));
    ImGui::Spacing();
    const std::string widthLabel = withId(Text::WidthTiles, "Width (tiles)");
    const std::string heightLabel = withId(Text::HeightTiles, "Height (tiles)");
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt(widthLabel.c_str(), &newWidth_);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt(heightLabel.c_str(), &newHeight_);
    if (sizeOk) {
        const int pixelsWide = newWidth_ * 32;
        const int pixelsHigh = newHeight_ * 32;
        char gameWide[32];
        char gameHigh[32];
        if (newWidth_ % 16 == 0 && newHeight_ % 16 == 0) {
            std::snprintf(gameWide, sizeof(gameWide), "%d", newWidth_ / 16);
            std::snprintf(gameHigh, sizeof(gameHigh), "%d", newHeight_ / 16);
        } else {
            std::snprintf(gameWide, sizeof(gameWide), "%.2f", newWidth_ / 16.0);
            std::snprintf(gameHigh, sizeof(gameHigh), "%.2f", newHeight_ / 16.0);
        }
        const std::string sizeLine = formatText(
            Text::NewMapSize,
            {std::to_string(pixelsWide), std::to_string(pixelsHigh), gameWide, gameHigh});
        ImGui::TextUnformatted(sizeLine.c_str());
        // 1008 tiles is game size 63. A larger map can still be created.
        if (newWidth_ > 1008 || newHeight_ > 1008) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.76f, 0.32f, 1.0f));
            ImGui::TextWrapped("%s", text(Text::NewMapGameLimit));
            ImGui::PopStyleColor();
        }
    } else {
        const std::string badSize = formatText(Text::NewMapBadSize, {std::to_string(kMinMapTiles), std::to_string(kMaxMapTiles)});
        ImGui::TextWrapped("%s", badSize.c_str());
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const float buttonWidth = std::max(theme::pushButtonWidth(text(Text::Cancel)), theme::pushButtonWidth(text(Text::Create)));
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button(text(Text::Cancel), ImVec2(buttonWidth, 0.0f))) {
        showNew_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!sizeOk);
    if (theme::defaultButton(text(Text::Create), ImVec2(buttonWidth, 0.0f))) {
        try {
            LoadedMap blank = createBlankMap(newWidth_, newHeight_);
            const int untitled = nextUntitledDocumentNumber();
            blank.displayName = formatText(Text::UntitledMap, {std::to_string(untitled)});
            addDocument(std::move(blank), untitled);
            statusNote_ = "New " + std::to_string(newWidth_) + " x " + std::to_string(newHeight_) + " map";
            error_.clear();
            showNew_ = false;
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& ex) {
            error_ = ex.what();
            showNew_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void EditorApp::applyResize() {
    if (!map_) {
        return;
    }
    const int left = resizeLeft_;
    const int top = resizeTop_;
    if (!resizeMap(*map_, left, resizeRight_, top, resizeBottom_)) {
        error_ = "That size is outside the 1 to 2048 tile limit.";
        showResize_ = false;
        return;
    }
    panX_ -= static_cast<float>(left) * 32.0f * zoom_;
    panY_ -= static_cast<float>(top) * 32.0f * zoom_;
    camera3d_.targetX += static_cast<float>(left) * 32.0f;
    camera3d_.targetZ += static_cast<float>(top) * 32.0f;
    camera3d_.keepOver(static_cast<float>(map_->pixelWidth()), static_cast<float>(map_->pixelHeight()));
    clearHistory();
    clearMapSelection();
    selectedFeatures_.clear();
    featureSelectMode_ = false;
    featureMarquee_ = false;
    featurePasteArmed_ = false;
    featureDelete_ = false;
    pasteArmed_ = false;
    selecting_ = false;
    armedFeature_.clear();
    draggingStartPlayer_ = 0;
    armedStartPlayer_ = 0;
    invalidateView();
    markMapDirty();
    statusNote_ = "Resized to " + std::to_string(map_->tileWidth) + " x " + std::to_string(map_->tileHeight) + " tiles";
    showResize_ = false;
}

void EditorApp::drawResizeMap() {
    if (!showResize_) {
        return;
    }
    if (!map_) {
        showResize_ = false;
        return;
    }
    ImGui::OpenPopup("Resize Map");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Resize Map", &showResize_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    theme::helpText(
        "The outward arrow adds one tile on that edge. The inward arrow removes one. Hold an arrow to keep going. New ground continues the tiles and heights already on that edge. Features and start positions on a removed edge are deleted.");
    ImGui::Spacing();

    const int newWidth = map_->tileWidth + resizeLeft_ + resizeRight_;
    const int newHeight = map_->tileHeight + resizeTop_ + resizeBottom_;
    const bool canGrowWidth = newWidth < kMaxMapTiles;
    const bool canShrinkWidth = newWidth > kMinMapTiles;
    const bool canGrowHeight = newHeight < kMaxMapTiles;
    const bool canShrinkHeight = newHeight > kMinMapTiles;
    const bool changed = resizeLeft_ != 0 || resizeRight_ != 0 || resizeTop_ != 0 || resizeBottom_ != 0;

    ensureMinimap();
    constexpr float kPreview = 280.0f;
    const float tilesWide = static_cast<float>(std::max(newWidth, 1));
    const float tilesHigh = static_cast<float>(std::max(newHeight, 1));
    float previewW = kPreview;
    float previewH = kPreview;
    if (tilesWide >= tilesHigh) {
        previewH = std::max(24.0f, kPreview * tilesHigh / tilesWide);
    } else {
        previewW = std::max(24.0f, kPreview * tilesWide / tilesHigh);
    }
    const float arrow = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float side = arrow * 2.0f + gap;
    const float blockW = side + gap + previewW + gap + side;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float originX = ImGui::GetCursorPosX() + std::max(0.0f, (avail - blockW) * 0.5f);

    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    const auto edgeArrow = [&](const char* id, ImGuiDir direction, bool enabled, int& delta, int step, const char* tip) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::ArrowButton(id, direction)) {
            delta += step;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", tip);
        }
    };

    ImGui::SetCursorPosX(originX + side + gap + (previewW - side) * 0.5f);
    edgeArrow("resize-top-add", ImGuiDir_Up, canGrowHeight, resizeTop_, 1, "Add one tile to the top");
    ImGui::SameLine();
    edgeArrow("resize-top-sub", ImGuiDir_Down, canShrinkHeight, resizeTop_, -1, "Remove one tile from the top");
    ImGui::NewLine();

    ImGui::SetCursorPosX(originX);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(side, std::max(0.0f, (previewH - arrow) * 0.5f)));
    edgeArrow("resize-left-add", ImGuiDir_Left, canGrowWidth, resizeLeft_, 1, "Add one tile to the left");
    ImGui::SameLine();
    edgeArrow("resize-left-sub", ImGuiDir_Right, canShrinkWidth, resizeLeft_, -1, "Remove one tile from the left");
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, gap);

    ImGui::InvisibleButton("resize-preview", ImVec2(previewW, previewH));
    const ImVec2 previewMin = ImGui::GetItemRectMin();
    const ImVec2 previewMax = ImGui::GetItemRectMax();
    ImGui::SameLine(0.0f, gap);

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(side, std::max(0.0f, (previewH - arrow) * 0.5f)));
    edgeArrow("resize-right-sub", ImGuiDir_Left, canShrinkWidth, resizeRight_, -1, "Remove one tile from the right");
    ImGui::SameLine();
    edgeArrow("resize-right-add", ImGuiDir_Right, canGrowWidth, resizeRight_, 1, "Add one tile to the right");
    ImGui::EndGroup();
    ImGui::NewLine();

    ImGui::SetCursorPosX(originX + side + gap + (previewW - side) * 0.5f);
    edgeArrow("resize-bottom-sub", ImGuiDir_Up, canShrinkHeight, resizeBottom_, -1, "Remove one tile from the bottom");
    ImGui::SameLine();
    edgeArrow("resize-bottom-add", ImGuiDir_Down, canGrowHeight, resizeBottom_, 1, "Add one tile to the bottom");
    ImGui::PopItemFlag();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(previewMin, previewMax, theme::canvasColor());
    if (miniTexture_ != 0 && map_->tileWidth > 0 && map_->tileHeight > 0) {
        const float cellW = (previewMax.x - previewMin.x) / tilesWide;
        const float cellH = (previewMax.y - previewMin.y) / tilesHigh;
        float x0 = previewMin.x + static_cast<float>(resizeLeft_) * cellW;
        float y0 = previewMin.y + static_cast<float>(resizeTop_) * cellH;
        float x1 = x0 + static_cast<float>(map_->tileWidth) * cellW;
        float y1 = y0 + static_cast<float>(map_->tileHeight) * cellH;
        const float fullW = x1 - x0;
        const float fullH = y1 - y0;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
        if (x0 < previewMin.x && fullW > 0.0f) {
            u0 = (previewMin.x - x0) / fullW;
            x0 = previewMin.x;
        }
        if (y0 < previewMin.y && fullH > 0.0f) {
            v0 = (previewMin.y - y0) / fullH;
            y0 = previewMin.y;
        }
        if (x1 > previewMax.x && fullW > 0.0f) {
            u1 = 1.0f - (x1 - previewMax.x) / fullW;
            x1 = previewMax.x;
        }
        if (y1 > previewMax.y && fullH > 0.0f) {
            v1 = 1.0f - (y1 - previewMax.y) / fullH;
            y1 = previewMax.y;
        }
        if (x1 > x0 && y1 > y0 && u1 > u0 && v1 > v0) {
            draw->PushClipRect(previewMin, previewMax, true);
            draw->AddImage(textureId(miniTexture_), ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(u0, v0), ImVec2(u1, v1));
            draw->PopClipRect();
        }
    }
    draw->AddRect(previewMin, previewMax, IM_COL32(20, 22, 26, 255));

    ImGui::Spacing();
    if (changed) {
        ImGui::Text(
            "%d x %d tiles  will become  %d x %d",
            map_->tileWidth,
            map_->tileHeight,
            newWidth,
            newHeight);
    } else {
        ImGui::Text("%d x %d tiles", map_->tileWidth, map_->tileHeight);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
    ImGui::Text("%d x %d pixels", newWidth * 32, newHeight * 32);
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const float applyWidth = theme::pushButtonWidth("Apply Resize");
    const float cancelWidth = theme::pushButtonWidth("Cancel");
    theme::alignRight(applyWidth + cancelWidth + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(cancelWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showResize_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!changed);
    if (theme::defaultButton("Apply Resize", ImVec2(applyWidth, 0.0f))) {
        applyResize();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void EditorApp::drawModifyHeight() {
    if (!showModifyHeight_) {
        return;
    }
    ImGui::OpenPopup("Modify Height");
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Modify Height", &showModifyHeight_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    theme::helpText("Changes every height sample on the selected tiles. The result stays between 0 and 255.");
    ImGui::Spacing();
    ImGui::RadioButton("Add", &modifyHeightMode_, static_cast<int>(HeightAdjust::Add));
    ImGui::RadioButton("Subtract", &modifyHeightMode_, static_cast<int>(HeightAdjust::Subtract));
    ImGui::RadioButton("Set", &modifyHeightMode_, static_cast<int>(HeightAdjust::Set));
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Value", &modifyHeightAmount_, 0, 0);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const float buttonWidth = theme::pushButtonWidth("Accept");
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showModifyHeight_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!map_ || !hasSelection());
    if (theme::defaultButton("Accept", ImVec2(buttonWidth, 0.0f))) {
        try {
            const auto adjust = static_cast<HeightAdjust>(modifyHeightMode_);
            auto change = adjustSelectionHeight(*map_, selection_, adjust, modifyHeightAmount_);
            if (change) {
                rememberPlacement(std::move(*change));
                if (adjust == HeightAdjust::Add) {
                    statusNote_ = "Added " + std::to_string(modifyHeightAmount_) + " to height";
                } else if (adjust == HeightAdjust::Subtract) {
                    statusNote_ = "Subtracted " + std::to_string(modifyHeightAmount_) + " from height";
                } else {
                    statusNote_ = "Set height to " + std::to_string(std::clamp(modifyHeightAmount_, 0, 255));
                }
            } else {
                statusNote_ = "Height unchanged";
            }
            error_.clear();
            showModifyHeight_ = false;
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& ex) {
            error_ = ex.what();
            showModifyHeight_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

int EditorApp::placeHeightDelta() const {
    const int amount = std::clamp(placeHeightAmount_, 0, 255);
    return placeHeightAdd_ ? amount : -amount;
}

void EditorApp::drawPlaceHeight() {
    if (!showPlaceHeight_) {
        return;
    }
    ImGui::OpenPopup("Height Offset");
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Height Offset", &showPlaceHeight_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    theme::helpText("Adds or subtracts this amount from every height sample of a tile as it is placed. The result stays between 0 and 255.");
    ImGui::Spacing();
    int mode = placeHeightDraftAdd_ ? 0 : 1;
    if (ImGui::RadioButton("Add", &mode, 0)) {
        placeHeightDraftAdd_ = true;
    }
    if (ImGui::RadioButton("Subtract", &mode, 1)) {
        placeHeightDraftAdd_ = false;
    }
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Value", &placeHeightDraftAmount_, 0, 0);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const float buttonWidth = theme::pushButtonWidth("Accept");
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        showPlaceHeight_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (theme::defaultButton("Accept", ImVec2(buttonWidth, 0.0f))) {
        placeHeightAdd_ = placeHeightDraftAdd_;
        placeHeightAmount_ = std::clamp(placeHeightDraftAmount_, 0, 255);
        const int delta = placeHeightDelta();
        if (delta == 0) {
            statusNote_ = "Placed tiles keep their height";
        } else if (delta > 0) {
            statusNote_ = "Placed tiles use height +" + std::to_string(delta);
        } else {
            statusNote_ = "Placed tiles use height " + std::to_string(delta);
        }
        showPlaceHeight_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

namespace {

bool sameName(std::string_view a, std::string_view b) {
    return lowerText(a) == lowerText(b);
}

// Names in alphabetical order, one for each spelling ignoring case.
std::vector<std::string> sortedUnique(std::vector<std::string> names) {
    std::sort(names.begin(), names.end(), nameLess);
    names.erase(std::unique(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return sameName(a, b); }), names.end());
    return names;
}

// The spelling already in the archive when the typed name matches one ignoring case.
std::string spelledLikeExisting(const std::string& typed, const std::vector<std::string>& existing) {
    const std::string name = trimmed(typed);
    for (const std::string& candidate : existing) {
        if (sameName(candidate, name)) {
            return candidate;
        }
    }
    return name;
}

void copyText(char* buffer, std::size_t size, const std::string& text) {
    std::snprintf(buffer, size, "%s", text.c_str());
}

}  // namespace

void EditorApp::drawSelectionMenu() {
    if (selectionMenuRequested_) {
        selectionMenuRequested_ = false;
        ImGui::OpenPopup("selection-menu");
    }
    selectionMenuOpen_ = ImGui::IsPopupOpen("selection-menu");
    if (!ImGui::BeginPopup("selection-menu")) {
        return;
    }
    const bool ready = map_.has_value() && hasSelection();
    if (ImGui::MenuItem("Cut", theme::shortcut("X").c_str(), false, ready)) {
        cutSelection();
    }
    if (ImGui::MenuItem("Copy", theme::shortcut("C").c_str(), false, ready)) {
        copySelection(false);
    }
    if (ImGui::MenuItem("Copy Heights", theme::shortcut("Shift+C").c_str(), false, ready)) {
        copySelection(true);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(theme::withEllipsis("Modify Height").c_str(), nullptr, false, ready)) {
        showModifyHeight_ = true;
    }
    if (ImGui::MenuItem("Blend", nullptr, false, ready)) {
        blendSelection();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(theme::withEllipsis("Add to Favorites").c_str(), nullptr, false, ready)) {
        try {
            // A saved tile is a whole rectangle, so the box around the selection is what gets saved.
            SectionStamp stamp = copyTileRegion(*map_, selection_.x, selection_.y, selection_.width, selection_.height);
            const std::string size = std::to_string(stamp.tileWidth) + " x " + std::to_string(stamp.tileHeight) + " tiles";
            openAddToFavorites(
                std::move(stamp),
                size,
                selection_.solid() ? std::string{} : "A saved tile is a full rectangle, so the tiles between the ones you selected are saved too.");
        } catch (const std::exception& ex) {
            error_ = ex.what();
        }
    }
    ImGui::EndPopup();
}

void EditorApp::openAddToFavorites(SectionStamp stamp, std::string description, std::string note) {
    destroyTexture(favoritePreview_);
    favoriteProblem_.clear();
    if (stamp.heightsOnly || stamp.tileImages.empty()) {
        favoriteProblem_ = "This copy has no tile pictures, so it cannot be saved.";
    } else if (stamp.tileWidth > 512 || stamp.tileHeight > 512) {
        favoriteProblem_ = "A favorite can be at most 512 tiles on each side. Select a smaller area.";
    } else if (stamp.tileCount > 65535) {
        favoriteProblem_ = "This area uses more than 65535 different tile pictures. Select a smaller area.";
    }
    stamp.tileMask.clear();
    if (stamp.minimap.size() != static_cast<std::size_t>(kSectionMinimap) * kSectionMinimap) {
        stamp.minimap = buildSectionMinimap(stamp);
    }
    const std::vector<std::uint8_t> picture = renderMinimapRgba(stamp);
    favoritePreview_ = uploadTexture(picture.data(), kSectionMinimap, kSectionMinimap, false);
    favoriteStamp_ = std::move(stamp);
    favoriteDescription_ = std::move(description);
    favoriteNote_ = std::move(note);
    favoriteTileName_[0] = '\0';
    favoriteShownSuggestion_.clear();
    favoriteNameSuggestion_.clear();
    favoriteNameKey_.clear();
    favoriteTileNameEditing_ = false;
    showAddFavorite_ = true;
}

void EditorApp::drawAddFavorite() {
    if (!showAddFavorite_) {
        destroyTexture(favoritePreview_);
        favoriteStamp_.reset();
        drawCreateFavorites();
        return;
    }
    ImGui::OpenPopup("Add to Favorites");
    if (!ImGui::BeginPopupModal("Add to Favorites", &showAddFavorite_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float scale = ImGui::GetFontSize() / 13.0f;
    const float contentWidth = std::floor(440.0f * scale);

    std::vector<const TilesetArchive*> archives;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (isFavoritesArchiveName(archive.path.filename().string())) {
            archives.push_back(&archive);
        }
    }
    std::sort(archives.begin(), archives.end(), [](const TilesetArchive* a, const TilesetArchive* b) {
        return nameLess(a->path.filename().string(), b->path.filename().string());
    });
    const TilesetArchive* chosen = nullptr;
    for (const TilesetArchive* archive : archives) {
        if (archive->path == favoriteArchive_) {
            chosen = archive;
        }
    }
    if (chosen == nullptr && archives.size() == 1) {
        chosen = archives.front();
        favoriteArchive_ = chosen->path;
    } else if (chosen == nullptr) {
        favoriteArchive_.clear();
    }

    // What is being added.
    const float previewSize = std::floor(72.0f * scale);
    if (favoritePreview_ != 0) {
        ImGui::Image(textureId(favoritePreview_), ImVec2(previewSize, previewSize), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImVec4(1, 1, 1, 1), ImVec4(0.30f, 0.31f, 0.33f, 1.0f));
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth - previewSize - style.ItemSpacing.x);
    ImGui::PushFont(theme::boldFont());
    ImGui::TextWrapped("%s", favoriteDescription_.c_str());
    ImGui::PopFont();
    ImGui::PushFont(theme::smallFont());
    if (!favoriteProblem_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.16f, 0.13f, 1.0f));
        ImGui::TextWrapped("%s", favoriteProblem_.c_str());
        ImGui::PopStyleColor();
    } else if (!favoriteNote_.empty()) {
        ImGui::TextWrapped("%s", favoriteNote_.c_str());
    } else {
        ImGui::TextWrapped("Saved tiles appear on the Favorites tab, where you click one to place it on a map.");
    }
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();

    // Which archive.
    ImGui::Spacing();
    theme::sourceListTitle("Archive");
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const int rows = std::clamp(static_cast<int>(archives.size()), 2, 5);
    if (ImGui::BeginListBox("##favorites-archives", ImVec2(contentWidth, rowHeight * static_cast<float>(rows) + style.FramePadding.y * 2.0f))) {
        if (archives.empty()) {
            ImGui::TextDisabled("No favorites archives yet");
        }
        for (const TilesetArchive* archive : archives) {
            const std::string filename = archive->path.filename().string();
            if (ImGui::Selectable(filename.c_str(), archive == chosen)) {
                favoriteArchive_ = archive->path;
                chosen = archive;
            }
        }
        ImGui::EndListBox();
    }
    if (ImGui::Button(theme::withEllipsis("Create Favorites Archive").c_str())) {
        favoriteNewName_[0] = '\0';
        showCreateFavorites_ = true;
    }

    // Which category and subcategory inside it.
    std::vector<std::string> categories;
    if (chosen != nullptr) {
        for (const TilesetSection& section : chosen->sections) {
            categories.push_back(section.world);
        }
    }
    categories = sortedUnique(std::move(categories));
    const std::string categoryName = spelledLikeExisting(favoriteCategory_, categories);
    std::vector<std::string> subcategories;
    if (chosen != nullptr) {
        for (const TilesetSection& section : chosen->sections) {
            if (sameName(section.world, categoryName)) {
                subcategories.push_back(section.category);
            }
        }
    }
    subcategories = sortedUnique(std::move(subcategories));

    ImGui::Spacing();
    const float labelWidth = std::floor(96.0f * scale);
    const float arrowWidth = ImGui::GetFrameHeight();
    const float inputWidth = contentWidth - labelWidth - arrowWidth - style.ItemInnerSpacing.x;
    const auto nameField = [&](const char* label, const char* id, const char* hint, char* buffer, std::size_t size, const std::vector<std::string>& choices) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(inputWidth);
        ImGui::InputTextWithHint(id, hint, buffer, size);
        ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
        ImGui::BeginDisabled(choices.empty());
        ImGui::SetNextItemWidth(arrowWidth);
        if (ImGui::BeginCombo((std::string(id) + "-choices").c_str(), "", ImGuiComboFlags_NoPreview)) {
            for (const std::string& choice : choices) {
                if (ImGui::Selectable(choice.c_str(), sameName(choice, buffer))) {
                    copyText(buffer, size, choice);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
    };
    nameField("Category:", "##favorite-category", "Type a new name or choose one", favoriteCategory_, sizeof(favoriteCategory_), categories);
    nameField("Subcategory:", "##favorite-subcategory", "Type a new name or choose one", favoriteSubcategory_, sizeof(favoriteSubcategory_), subcategories);

    const std::string categoryNow = spelledLikeExisting(favoriteCategory_, categories);
    const std::string subcategoryNow = spelledLikeExisting(favoriteSubcategory_, subcategories);
    std::string suggestion = "Tile01";
    if (chosen != nullptr && favoritesNameProblem(categoryNow).empty() && favoritesNameProblem(subcategoryNow).empty()) {
        const std::string key = chosen->path.string() + "\n" + categoryNow + "\n" + subcategoryNow;
        if (key != favoriteNameKey_) {
            favoriteNameKey_ = key;
            try {
                favoriteNameSuggestion_ = nextFavoriteSectionName(chosen->path, categoryNow, subcategoryNow);
            } catch (const std::exception&) {
                favoriteNameSuggestion_.clear();
            }
        }
        if (!favoriteNameSuggestion_.empty()) {
            suggestion = favoriteNameSuggestion_;
        }
    } else {
        favoriteNameKey_.clear();
    }
    if (std::string(favoriteTileName_) == favoriteShownSuggestion_ || (favoriteTileName_[0] == '\0' && !favoriteTileNameEditing_)) {
        if (std::string(favoriteTileName_) != suggestion) {
            copyText(favoriteTileName_, sizeof(favoriteTileName_), suggestion);
        }
        favoriteShownSuggestion_ = suggestion;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("File name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(contentWidth - labelWidth);
    ImGui::InputText("##favorite-tile-name", favoriteTileName_, sizeof(favoriteTileName_));
    favoriteTileNameEditing_ = ImGui::IsItemActive();

    const std::string tileFileName = favoriteTileFileName(favoriteTileName_);
    std::string nameProblem;
    if (!trimmed(favoriteCategory_).empty()) {
        nameProblem = favoritesNameProblem(categoryNow);
        if (!nameProblem.empty()) {
            nameProblem = "Category: " + nameProblem;
        }
    }
    if (nameProblem.empty() && !trimmed(favoriteSubcategory_).empty()) {
        nameProblem = favoritesNameProblem(subcategoryNow);
        if (!nameProblem.empty()) {
            nameProblem = "Subcategory: " + nameProblem;
        }
    }
    if (nameProblem.empty()) {
        const std::string problem = favoritesNameProblem(tileFileName);
        if (!problem.empty()) {
            nameProblem = "File name: " + problem;
        }
    }
    if (nameProblem.empty() && chosen != nullptr) {
        for (const TilesetSection& section : chosen->sections) {
            if (sameName(section.world, categoryNow) && sameName(section.category, subcategoryNow) && sameName(section.name, tileFileName)) {
                nameProblem = "A tile named " + tileFileName + " is already in that subcategory.";
                break;
            }
        }
    }
    ImGui::PushFont(theme::smallFont());
    if (!nameProblem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.16f, 0.13f, 1.0f));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextWrapped("%s", nameProblem.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextWrapped("A category groups subcategories, like Acid_New_1 holding Rivers and Sm Lakes. The file name is filled in; change it or leave it.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const bool canAdd = chosen != nullptr && favoriteProblem_.empty() && nameProblem.empty()
        && !trimmed(favoriteCategory_).empty() && !trimmed(favoriteSubcategory_).empty()
        && !tileFileName.empty() && favoriteStamp_.has_value();
    const float buttonWidth = theme::pushButtonWidth("Cancel");
    theme::alignRight(buttonWidth * 2.0f + style.ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || (!showCreateFavorites_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        showAddFavorite_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canAdd || showCreateFavorites_);
    if (theme::defaultButton("Add", ImVec2(buttonWidth, 0.0f))) {
        try {
            const std::filesystem::path archivePath = chosen->path;
            const std::string tile = addFavoriteSection(archivePath, categoryNow, subcategoryNow, *favoriteStamp_, tileFileName);
            refreshFavoritesArchive(archivePath);
            copyText(favoriteCategory_, sizeof(favoriteCategory_), categoryNow);
            copyText(favoriteSubcategory_, sizeof(favoriteSubcategory_), subcategoryNow);
            statusNote_ = "Added " + tile + " to " + archivePath.filename().string();
            error_.clear();
        } catch (const std::exception& ex) {
            error_ = ex.what();
        }
        showAddFavorite_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    drawCreateFavorites();
    ImGui::EndPopup();
}

void EditorApp::createFavoritesArchiveNamed() {
    try {
        const std::filesystem::path folder = programDirectory();
        if (folder.empty()) {
            throw std::runtime_error("Galaxy Editor could not find the folder it runs from, so it has nowhere to keep a favorites archive.");
        }
        const std::filesystem::path path = folder / favoritesArchiveFilename(favoriteNewName_);
        createFavoritesArchive(path);
        const std::size_t index = tilesets_.refreshArchive(path);
        favoriteArchive_ = tilesets_.archives()[index].path;
        tilesetIndexSections_ = -1;
        statusNote_ = "Created " + path.filename().string();
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::drawCreateFavorites() {
    if (!showCreateFavorites_) {
        return;
    }
    ImGui::OpenPopup("Create Favorites Archive");
    if (!ImGui::BeginPopupModal("Create Favorites Archive", &showCreateFavorites_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float scale = ImGui::GetFontSize() / 13.0f;
    const float contentWidth = std::floor(380.0f * scale);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
    theme::helpText("Choose a name for the new archive. Anyone you share the file with sees your tiles on their Favorites tab.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float labelWidth = std::floor(52.0f * scale);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(contentWidth - labelWidth);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool returned = ImGui::InputTextWithHint("##favorite-archive-name", "FirstOne", favoriteNewName_, sizeof(favoriteNewName_), ImGuiInputTextFlags_EnterReturnsTrue);

    const std::string name = trimmed(favoriteNewName_);
    std::string problem = name.empty() ? std::string{} : favoritesNameProblem(name);
    if (problem.empty() && !name.empty()) {
        const std::string filename = favoritesArchiveFilename(name);
        const std::filesystem::path folder = programDirectory();
        std::error_code ignored;
        bool taken = !folder.empty() && std::filesystem::exists(folder / filename, ignored);
        for (const TilesetArchive& archive : tilesets_.archives()) {
            taken = taken || sameName(archive.path.filename().string(), filename);
        }
        if (taken) {
            problem = "An archive named " + filename + " already exists.";
        }
    }
    ImGui::PushFont(theme::smallFont());
    ImGui::Indent(labelWidth);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth - labelWidth);
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.16f, 0.13f, 1.0f));
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        if (name.empty()) {
            ImGui::TextWrapped("The file is named GE-Favorites-(name).hpi.");
        } else {
            ImGui::TextWrapped("File name: %s", favoritesArchiveFilename(name).c_str());
        }
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::Unindent(labelWidth);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const bool canCreate = !name.empty() && problem.empty();
    const float buttonWidth = theme::pushButtonWidth("Cancel");
    theme::alignRight(buttonWidth * 2.0f + style.ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        showCreateFavorites_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canCreate);
    if (theme::defaultButton("Create", ImVec2(buttonWidth, 0.0f), false) || (canCreate && returned)) {
        createFavoritesArchiveNamed();
        showCreateFavorites_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void EditorApp::selectFavoriteTile(std::size_t archiveIndex, int sectionIndex, bool extend) {
    if (extend) {
        if (favoriteSelection_.erase(sectionIndex) != 0) {
            favoriteSelectionArchive_.erase(sectionIndex);
            if (selectedSection_ == sectionIndex) {
                selectedSection_ = -1;
            }
        } else {
            favoriteSelection_.insert(sectionIndex);
            favoriteSelectionArchive_[sectionIndex] = archiveIndex;
        }
        return;
    }
    favoriteSelection_.clear();
    favoriteSelectionArchive_.clear();
    favoriteSelection_.insert(sectionIndex);
    favoriteSelectionArchive_[sectionIndex] = archiveIndex;
    selectedSection_ = sectionIndex;
    pasteArmed_ = false;
    brushName_.clear();
}

void EditorApp::clickSidebarTile(bool favorites, std::size_t archiveIndex, const std::vector<int>& shown, int position) {
    if (position < 0 || position >= static_cast<int>(shown.size())) {
        return;
    }
    const int sectionIndex = shown[static_cast<std::size_t>(position)];
    const ImGuiIO& io = ImGui::GetIO();
    const bool toggle = io.KeyCtrl || io.KeySuper;
    const bool range = io.KeyShift && !toggle;
    int& anchor = tileSelectionAnchor_[favorites ? 1 : 0];
    std::set<int>& selection = favorites ? favoriteSelection_ : tilesetSelection_;
    std::unordered_map<int, std::size_t>& archives = favorites ? favoriteSelectionArchive_ : tilesetSelectionArchive_;
    if (range) {
        const auto from = std::find(shown.begin(), shown.end(), anchor);
        if (from != shown.end()) {
            int first = static_cast<int>(from - shown.begin());
            int last = position;
            if (first > last) {
                std::swap(first, last);
            }
            selection.clear();
            archives.clear();
            for (int i = first; i <= last; ++i) {
                const int listed = shown[static_cast<std::size_t>(i)];
                selection.insert(listed);
                archives[listed] = archiveIndex;
            }
            statusNote_ = std::to_string(selection.size()) + " tiles selected";
            return;
        }
        // No anchor in this category: a Shift-click acts as a plain click.
    }
    anchor = sectionIndex;
    if (favorites) {
        selectFavoriteTile(archiveIndex, sectionIndex, toggle);
        return;
    }
    if (toggle) {
        if (tilesetSelection_.erase(sectionIndex) != 0) {
            tilesetSelectionArchive_.erase(sectionIndex);
            if (selectedSection_ == sectionIndex) {
                selectedSection_ = -1;
            }
        } else {
            tilesetSelection_.insert(sectionIndex);
            tilesetSelectionArchive_[sectionIndex] = archiveIndex;
        }
        return;
    }
    tilesetSelection_.clear();
    tilesetSelectionArchive_.clear();
    tilesetSelection_.insert(sectionIndex);
    tilesetSelectionArchive_[sectionIndex] = archiveIndex;
    selectedSection_ = sectionIndex;
    pasteArmed_ = false;
    brushName_.clear();
}

std::vector<std::pair<int, std::size_t>> EditorApp::sidebarTileSelection(bool favorites) const {
    const std::set<int>& selection = favorites ? favoriteSelection_ : tilesetSelection_;
    const std::unordered_map<int, std::size_t>& archives = favorites ? favoriteSelectionArchive_ : tilesetSelectionArchive_;
    std::vector<std::pair<int, std::size_t>> chosen;
    for (const int sectionIndex : selection) {
        const auto archive = archives.find(sectionIndex);
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections_.size()) || archive == archives.end()) {
            continue;
        }
        chosen.emplace_back(sectionIndex, archive->second);
    }
    return chosen;
}

std::string EditorApp::sidebarTileFullPath(std::size_t archiveIndex, const CachedSection& section) const {
    if (archiveIndex >= tilesets_.archives().size()) {
        return section.name + ".sct";
    }
    const TilesetArchive& archive = tilesets_.archives()[archiveIndex];
    std::string inside = "sections/" + section.world + "/" + section.category + "/" + section.name + ".sct";
    for (const TilesetSection& listed : archive.sections) {
        if (listed.world == section.world && listed.category == section.category && listed.name == section.name) {
            inside = listed.archivePath;
            break;
        }
    }
    std::replace(inside.begin(), inside.end(), '\\', '/');
    while (!inside.empty() && inside.front() == '/') {
        inside.erase(inside.begin());
    }
    // The path starts at the HPI file itself, such as WaterWorld-Greenworld.hpi/sections/..., without the folders
    // the HPI sits in on disk.
    return archive.path.filename().generic_string() + "/" + inside;
}

void EditorApp::copySidebarTilePaths(bool favorites) {
    const auto chosen = sidebarTileSelection(favorites);
    if (chosen.empty()) {
        return;
    }
    std::string lines;
    for (const auto& [sectionIndex, archiveIndex] : chosen) {
        if (!lines.empty()) {
            lines += '\n';
        }
        lines += sidebarTileFullPath(archiveIndex, sections_[static_cast<std::size_t>(sectionIndex)]);
    }
    ImGui::SetClipboardText(lines.c_str());
    statusNote_ = chosen.size() == 1 ? "Copied " + lines : "Copied " + std::to_string(chosen.size()) + " tile paths";
    error_.clear();
}

void EditorApp::exportSidebarTilesBmp(bool favorites) {
    const auto chosen = sidebarTileSelection(favorites);
    if (chosen.empty()) {
        return;
    }
    const std::filesystem::path start = tileExportFolder_.empty() ? suggestedOpenDirectory() : tileExportFolder_;
    const auto folder = chooseFolderDialog(chosen.size() == 1 ? "Choose where to save the tile BMP" : "Choose where to save the tile BMPs", start);
    if (!folder) {
        return;
    }
    tileExportFolder_ = *folder;
    std::set<std::string> usedNames;
    int written = 0;
    int skipped = 0;
    std::string lastError;
    for (const auto& [sectionIndex, archiveIndex] : chosen) {
        (void)archiveIndex;
        const CachedSection& section = sections_[static_cast<std::size_t>(sectionIndex)];
        const PaletteImage picture = sectionImage(section.stamp);
        if (picture.width <= 0 || picture.height <= 0 || section.stamp.tileImages.empty()) {
            ++skipped;
            continue;
        }
        std::string base = section.name.empty() ? std::string("tile") : section.name;
        for (char& c : base) {
            if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 32) {
                c = '_';
            }
        }
        std::string filename = base + ".bmp";
        for (int copy = 2; usedNames.count(lowerText(filename)) != 0; ++copy) {
            filename = base + " (" + std::to_string(copy) + ").bmp";
        }
        usedNames.insert(lowerText(filename));
        try {
            writeFileBytes(*folder / filename, encodeBmp24(picture.width, picture.height, picture.pixels.data(), kTaPalette));
            ++written;
        } catch (const std::exception& ex) {
            ++skipped;
            lastError = ex.what();
        }
    }
    statusNote_ = "Exported " + std::to_string(written) + (written == 1 ? " tile" : " tiles") + " as BMP to " + folder->string();
    if (skipped > 0) {
        statusNote_ += " (" + std::to_string(skipped) + " skipped)";
    }
    if (!lastError.empty()) {
        error_ = lastError;
    } else {
        error_.clear();
    }
}

void EditorApp::requestDeleteFavorite(std::size_t archiveIndex, const CachedSection& section) {
    deleteFavorites_.clear();
    if (archiveIndex >= tilesets_.archives().size()) {
        return;
    }
    const TilesetArchive& archive = tilesets_.archives()[archiveIndex];
    for (const TilesetSection& listed : archive.sections) {
        if (listed.world == section.world && listed.category == section.category && listed.name == section.name) {
            deleteFavorites_.push_back(FavoriteDelete{archive.path, listed.archivePath, listed.name});
            showDeleteFavorite_ = true;
            return;
        }
    }
}

void EditorApp::requestDeleteFavoriteSelection() {
    deleteFavorites_.clear();
    for (const int sectionIndex : favoriteSelection_) {
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections_.size())) {
            continue;
        }
        const auto archive = favoriteSelectionArchive_.find(sectionIndex);
        if (archive == favoriteSelectionArchive_.end()) {
            continue;
        }
        const CachedSection& section = sections_[static_cast<std::size_t>(sectionIndex)];
        if (archive->second >= tilesets_.archives().size()) {
            continue;
        }
        const TilesetArchive& tileset = tilesets_.archives()[archive->second];
        for (const TilesetSection& listed : tileset.sections) {
            if (listed.world == section.world && listed.category == section.category && listed.name == section.name) {
                deleteFavorites_.push_back(FavoriteDelete{tileset.path, listed.archivePath, listed.name});
                break;
            }
        }
    }
    showDeleteFavorite_ = !deleteFavorites_.empty();
}

void EditorApp::drawDeleteFavorite() {
    if (!showDeleteFavorite_) {
        return;
    }
    ImGui::OpenPopup("Delete Favorite");
    if (!ImGui::BeginPopupModal("Delete Favorite", nullptr, theme::alertFlags())) {
        return;
    }
    const int count = static_cast<int>(deleteFavorites_.size());
    std::string message;
    std::string informative;
    if (count <= 1) {
        const std::string name = deleteFavorites_.empty() ? std::string("this tile") : deleteFavorites_.front().name;
        const std::string archive = deleteFavorites_.empty() ? std::string("its archive") : deleteFavorites_.front().archive.filename().string();
        message = "Do you want to delete \"" + name + "\" from your favorites?";
        informative = "It will be removed from " + archive + ". You can't undo this.";
    } else {
        std::set<std::filesystem::path> archives;
        for (const FavoriteDelete& item : deleteFavorites_) {
            archives.insert(item.archive);
        }
        message = "Do you want to delete " + std::to_string(count) + " tiles from your favorites?";
        if (archives.size() == 1) {
            informative = "They will be removed from " + archives.begin()->filename().string() + ". You can't undo this.";
        } else {
            informative = "They will be removed from " + std::to_string(archives.size()) + " favorites archives. You can't undo this.";
        }
    }
    theme::alertText(message.c_str(), informative.c_str());
    const float buttonWidth = theme::pushButtonWidth("Delete");
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Delete", ImVec2(buttonWidth, 0.0f))) {
        try {
            std::map<std::filesystem::path, std::vector<std::string>> grouped;
            for (const FavoriteDelete& item : deleteFavorites_) {
                grouped[item.archive].push_back(item.sectionPath);
            }
            for (const auto& [archive, paths] : grouped) {
                removeFavoriteSections(archive, paths);
                refreshFavoritesArchive(archive);
            }
            const std::string archiveName = deleteFavorites_.empty() ? std::string() : deleteFavorites_.front().archive.filename().string();
            const std::string tileName = deleteFavorites_.empty() ? std::string() : deleteFavorites_.front().name;
            favoriteSelection_.clear();
            favoriteSelectionArchive_.clear();
            if (count <= 1) {
                statusNote_ = "Deleted " + tileName + " from " + archiveName;
            } else if (grouped.size() == 1) {
                statusNote_ = "Deleted " + std::to_string(count) + " tiles from " + archiveName;
            } else {
                statusNote_ = "Deleted " + std::to_string(count) + " tiles from favorites";
            }
            error_.clear();
        } catch (const std::exception& ex) {
            error_ = ex.what();
        }
        showDeleteFavorite_ = false;
        deleteFavorites_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    // Cancel answers Return, so the destructive choice is never the quick one.
    if (theme::defaultButton("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        showDeleteFavorite_ = false;
        deleteFavorites_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::requestRenameTile(std::size_t archiveIndex, const CachedSection& section) {
    if (archiveIndex >= tilesets_.archives().size()) {
        return;
    }
    const TilesetArchive& archive = tilesets_.archives()[archiveIndex];
    for (const TilesetSection& listed : archive.sections) {
        if (listed.world == section.world && listed.category == section.category && listed.name == section.name) {
            renameArchive_ = archive.path;
            renameArchiveLabel_ = archive.path.filename().string();
            renameSectionPath_ = listed.archivePath;
            renameWorld_ = listed.world;
            renameCategory_ = listed.category;
            renameOriginalName_ = listed.name;
            copyText(renameFileName_, sizeof(renameFileName_), listed.name);
            showRenameTile_ = true;
            return;
        }
    }
}

void EditorApp::drawRenameTile() {
    if (!showRenameTile_) {
        return;
    }
    ImGui::OpenPopup("Rename File");
    if (!ImGui::BeginPopupModal("Rename File", &showRenameTile_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const ImGuiStyle& style = ImGui::GetStyle();
    const float scale = ImGui::GetFontSize() / 13.0f;
    const float contentWidth = std::floor(380.0f * scale);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
    theme::helpText("Choose a new file name. The tile stays in the same category.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();

    const float labelWidth = std::floor(78.0f * scale);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("File name:");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(contentWidth - labelWidth);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    const bool returned = ImGui::InputText("##rename-tile", renameFileName_, sizeof(renameFileName_), ImGuiInputTextFlags_EnterReturnsTrue);

    const std::string tileFileName = favoriteTileFileName(renameFileName_);
    std::string problem = tileFileName.empty() && trimmed(renameFileName_).empty() ? std::string{} : favoritesNameProblem(tileFileName);
    bool unchanged = false;
    if (problem.empty()) {
        unchanged = tileFileName == renameOriginalName_;
        if (!unchanged) {
            for (const TilesetArchive& archive : tilesets_.archives()) {
                if (archive.path != renameArchive_) {
                    continue;
                }
                for (const TilesetSection& section : archive.sections) {
                    if (section.archivePath == renameSectionPath_) {
                        continue;
                    }
                    if (sameName(section.world, renameWorld_) && sameName(section.category, renameCategory_) && sameName(section.name, tileFileName)) {
                        problem = "A file named " + tileFileName + " is already in that category.";
                        break;
                    }
                }
            }
        }
    }
    ImGui::PushFont(theme::smallFont());
    ImGui::Indent(labelWidth);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth - labelWidth);
    if (!problem.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.16f, 0.13f, 1.0f));
        ImGui::TextWrapped("%s", problem.c_str());
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextWrapped("Saved as %s.sct in %s.", tileFileName.empty() ? renameOriginalName_.c_str() : tileFileName.c_str(), renameArchiveLabel_.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::Unindent(labelWidth);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const bool canRename = !tileFileName.empty() && problem.empty() && !unchanged;
    const float buttonWidth = theme::pushButtonWidth("Cancel");
    theme::alignRight(buttonWidth * 2.0f + style.ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        showRenameTile_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRename);
    if (theme::defaultButton("Rename", ImVec2(buttonWidth, 0.0f), false) || (canRename && returned)) {
        try {
            const std::string renamed = renameTilesetSection(renameArchive_, renameSectionPath_, tileFileName);
            refreshFavoritesArchive(renameArchive_);
            statusNote_ = "Renamed " + renameOriginalName_ + " to " + renamed;
            error_.clear();
        } catch (const std::exception& ex) {
            error_ = ex.what();
        }
        showRenameTile_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void EditorApp::refreshFavoritesArchive(const std::filesystem::path& path) {
    const auto dropFeaturePictures = [this](const TilesetArchive& archive) {
        for (const FeatureInfo& feature : archive.features) {
            const auto found = featureSprites_.find(lowerText(feature.name));
            if (found == featureSprites_.end()) {
                continue;
            }
            destroyTexture(found->second.texture);
            featureSprites_.erase(found);
        }
    };
    for (const TilesetArchive& archive : tilesets_.archives()) {
        std::error_code error;
        if (std::filesystem::exists(archive.path, error) && std::filesystem::exists(path, error)
            && std::filesystem::equivalent(archive.path, path, error)) {
            dropFeaturePictures(archive);
            break;
        }
    }
    const std::size_t index = tilesets_.refreshArchive(path);
    if (index < tilesets_.archives().size()) {
        dropFeaturePictures(tilesets_.archives()[index]);
    }
    const std::string prefix = std::to_string(index) + '\n';
    for (auto it = categories_.begin(); it != categories_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) {
            ++it;
            continue;
        }
        for (const int sectionIndex : it->second.sections) {
            CachedSection& cached = sections_[static_cast<std::size_t>(sectionIndex)];
            destroyTexture(cached.preview);
            cached.stamp = SectionStamp{};
            if (selectedSection_ == sectionIndex) {
                selectedSection_ = -1;
            }
            if (favoriteSelection_.erase(sectionIndex) != 0) {
                favoriteSelectionArchive_.erase(sectionIndex);
            }
            if (tilesetSelection_.erase(sectionIndex) != 0) {
                tilesetSelectionArchive_.erase(sectionIndex);
            }
            if (ghostFor_ == sectionIndex) {
                destroyTexture(ghostTexture_);
                ghostFor_ = -1;
            }
        }
        it = categories_.erase(it);
    }
    loadQueue_.erase(
        std::remove_if(loadQueue_.begin(), loadQueue_.end(), [&](const CategoryRequest& job) { return job.archiveIndex == index; }),
        loadQueue_.end());
    tilesetIndexSections_ = -1;
    // A rename keeps the section count, so the brush catalog has to be read again
    // for the new file paths.
    brushCatalogKey_.clear();
}

namespace {


// What a map is without base_terrain, and a land without terrain_type. The game uses GRASS.
constexpr const char* kRandomMapFallbackTerrain = "GREENWORLD_BASE-85";

int resizeStringCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

ImU32 scriptWordColor(RmsWordKind kind, const theme::CodeColors& colors) {
    switch (kind) {
    case RmsWordKind::Comment:
        return colors.comment;
    case RmsWordKind::Section:
    case RmsWordKind::Keyword:
        return colors.keyword;
    case RmsWordKind::Directive:
        return colors.preprocessor;
    case RmsWordKind::Command:
        return colors.function;
    case RmsWordKind::Attribute:
        return colors.property;
    case RmsWordKind::Terrain:
        return colors.string;
    case RmsWordKind::Number:
        return colors.number;
    case RmsWordKind::Plain:
    case RmsWordKind::Brace:
        break;
    }
    return colors.plain;
}

// A multiline text box that colours a random map script like Xcode does. ImGui's
// text box draws one colour, so its own text and caret are made invisible and the
// words are drawn again over it, at the places ImGui put them, in their colours.
// Returns true when an edit has just finished.
bool scriptEditor(const char* id, std::string& text, const ImVec2& size, int points) {
    const theme::CodeColors& colors = theme::codeColors();
    const float u = theme::unit();
    ImGui::PushFont(theme::codeFont(points));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colors.background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, colors.background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, colors.background);
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, colors.selection);
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::floor(8.0f * u), std::floor(6.0f * u)));
    ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, size,
                              ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize, resizeStringCallback, &text);
    const ImGuiID inputId = ImGui::GetItemID();
    const bool finished = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopStyleColor(5);

    // The text box is a child window; its draw list, scroll and padding place the text.
    ImGuiWindow* box = nullptr;
    for (ImGuiWindow* child : ImGui::GetCurrentWindow()->DC.ChildWindows) {
        if (child != nullptr && child->ChildId == inputId) {
            box = child;
        }
    }
    if (box != nullptr && box->DrawList != nullptr && box->Active) {
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const ImVec2 padding = ImGui::GetStyle().FramePadding;
        ImGuiInputTextState* state = ImGui::GetInputTextState(inputId);
        const bool active = state != nullptr && ImGui::GetActiveID() == inputId;
        const ImVec2 origin(box->Pos.x + padding.x - (active ? state->Scroll.x : 0.0f), box->Pos.y + padding.y - box->Scroll.y);
        const ImRect clip = box->InnerClipRect;
        ImDrawList* draw = box->DrawList;
        draw->PushClipRect(clip.Min, clip.Max, true);
        const char* base = text.c_str();
        std::size_t scanned = 0;
        std::size_t lineStart = 0;
        int line = 0;
        const auto advanceTo = [&](std::size_t position) {
            for (; scanned < position && scanned < text.size(); ++scanned) {
                if (text[scanned] == '\n') {
                    ++line;
                    lineStart = scanned + 1;
                }
            }
        };
        for (const RmsWordSpan& span : rmsWordSpans(text)) {
            advanceTo(span.start);
            const float y = origin.y + static_cast<float>(line) * fontSize;
            if (y > clip.Max.y) {
                break;
            }
            if (y + fontSize < clip.Min.y) {
                continue;
            }
            const float x = origin.x + font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, base + lineStart, base + span.start).x;
            draw->AddText(font, fontSize, ImVec2(x, y), scriptWordColor(span.kind, colors), base + span.start, base + span.start + span.length);
        }
        if (active) {
            const bool blinkOn = !ImGui::GetIO().ConfigInputTextCursorBlink || state->CursorAnim <= 0.0f
                || std::fmod(state->CursorAnim, 1.20f) <= 0.80f;
            const std::size_t cursor = static_cast<std::size_t>(std::clamp(state->GetCursorPos(), 0, static_cast<int>(text.size())));
            std::size_t caretLineStart = 0;
            int caretLine = 0;
            for (std::size_t i = 0; i < cursor; ++i) {
                if (text[i] == '\n') {
                    ++caretLine;
                    caretLineStart = i + 1;
                }
            }
            if (blinkOn) {
                const float x = std::floor(origin.x + font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, base + caretLineStart, base + cursor).x);
                const float top = origin.y + static_cast<float>(caretLine) * fontSize + 0.5f;
                draw->AddRectFilled(ImVec2(x, top), ImVec2(x + std::max(1.0f, std::floor(1.5f * u)), top + fontSize - 2.0f), colors.caret);
            }
        }
        draw->PopClipRect();
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
    return finished;
}

}  // namespace

std::filesystem::path EditorApp::randomMapScriptPath() const {
    return editorSettingsPath().parent_path() / "RandomMapScript.rms";
}

void EditorApp::saveRandomMapScript() const {
    if (!randomMapLoaded_) {
        return;
    }
    try {
        const std::filesystem::path path = randomMapScriptPath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << randomMapScript_;
    } catch (const std::exception&) {
    }
}

void EditorApp::autoMetal() {
    if (!map_) {
        return;
    }
    // Metal tiles are known by their pictures, read from the random map tileset's Metal sections.
    const TilesetArchive* source = nullptr;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (isRandomMapTileset(archive.path)) {
            source = &archive;
            break;
        }
    }
    if (source == nullptr) {
        autoMetalAlert_ = text(Text::RandomMapNoTileset);
        return;
    }
    bool haveFeature = false;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        for (const FeatureInfo& feature : archive.features) {
            haveFeature = haveFeature || lowerText(feature.name) == lowerText(kAutoMetalFeature);
        }
    }
    if (!haveFeature) {
        autoMetalAlert_ = text(Text::AutoMetalMissing);
        return;
    }
    const std::string key = source->path.string() + "\n" + std::to_string(source->sections.size());
    if (key != metalPicturesKey_) {
        metalPictures_.clear();
        for (const TilesetSection& section : source->sections) {
            std::string kind;
            brushPieceFolderTiles(section.world, kind);
            const std::string world = lowerText(kind);
            const bool ground = world == "basetiles" || world == "basetilebrushstrokes" || world == "clifftiles" || world == "coasttiles"
                || world == "coastdsl" || world == "shallowcoasttiles" || world == "shallowdeeptiles" || world == "slopes";
            if (!ground || lowerText(section.category) != "metal") {
                continue;
            }
            try {
                const HpiFile* file = source->archive().find(section.archivePath);
                if (file == nullptr) {
                    continue;
                }
                const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
                for (const std::uint32_t image : stamp.indices) {
                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                    if (bytes + 1024 <= stamp.tileImages.size()) {
                        metalPictures_.insert(hashBrushTile(stamp.tileImages.data() + bytes));
                    }
                }
            } catch (const std::exception&) {
            }
        }
        metalPicturesKey_ = key;
    }
    const LoadedMap& map = *map_;
    const std::vector<std::pair<int, int>> cells = autoMetalCells(map.tileWidth, map.tileHeight, [&](int x, int y) {
        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(x);
        if (index >= map.indices.size()) {
            return false;
        }
        const std::size_t bytes = static_cast<std::size_t>(map.indices[index]) * 1024;
        return bytes + 1024 <= map.tileImages.size() && metalPictures_.count(hashBrushTile(map.tileImages.data() + bytes)) != 0;
    });
    if (cells.empty()) {
        autoMetalAlert_ = text(Text::AutoMetalNone);
        return;
    }
    // Each zone moves down by its lift, so that raised as the game draws it, it covers
    // its block's tiles. Zones an earlier Auto Metal left on the block's corner cell
    // move with it.
    const int cellsHigh = map.tileHeight * 2;
    std::set<std::pair<int, int>> corners(cells.begin(), cells.end());
    std::vector<FeaturePlacement> next;
    next.reserve(map.features.size() + cells.size());
    std::map<std::pair<int, int>, std::string> taken;
    for (const FeaturePlacement& feature : map.features) {
        if (lowerText(feature.name) == lowerText(kAutoMetalFeature) && corners.count({feature.x, feature.y}) != 0) {
            continue;
        }
        next.push_back(feature);
        taken[{feature.x, feature.y}] = feature.name;
    }
    int placed = 0;
    int blocked = 0;
    int moved = 0;
    for (const auto& [x, y] : cells) {
        const int drop = autoMetalDrop(x, y, cellsHigh, [&](int cellX, int cellY) { return featureLift(map, cellX, cellY); });
        const std::pair<int, int> spot{x, y + drop};
        if (const auto found = taken.find(spot); found != taken.end()) {
            blocked += lowerText(found->second) == lowerText(kAutoMetalFeature) ? 0 : 1;
            continue;
        }
        next.push_back(FeaturePlacement{spot.first, spot.second, kAutoMetalFeature});
        taken[spot] = kAutoMetalFeature;
        const bool wasHere = std::any_of(map.features.begin(), map.features.end(), [&](const FeaturePlacement& feature) {
            return feature.x == x && feature.y == y && lowerText(feature.name) == lowerText(kAutoMetalFeature);
        });
        if (!wasHere) {
            ++placed;
        } else if (drop > 0) {
            ++moved;
        }
    }
    const std::string inTheWay = blocked > 0
        ? ", " + std::to_string(blocked) + (blocked == 1 ? " block has" : " blocks have") + " another feature in its corner cell"
        : std::string();
    if (placed == 0 && moved == 0) {
        statusNote_ = "Every Metal block already has " + std::string(kAutoMetalFeature) + inTheWay;
        return;
    }
    FeatureBatchChange change = replaceMapFeatures(*map_, next);
    EditorCommand command;
    command.featureBatch = std::move(change);
    pushCommand(std::move(command), true);
    selectedFeatures_.clear();
    statusNote_ = "Auto Metal placed " + std::to_string(placed) + " " + kAutoMetalFeature
        + (moved > 0 ? " and moved " + std::to_string(moved) + " onto the tile grid" : std::string()) + inTheWay;
}

bool EditorApp::stampBaseOf(const std::string& button, std::string& world, std::string& base) const {
    // The same order hillCanPlace checks in.
    if (const HillSection* group = groupHill()) {
        world = brushWorld_;
        base = group->baseName;
        return !base.empty();
    }
    world = brushWorld_;
    if (button == "Island 8x8") {
        world = "GreenWorld";
        base = "Base-0";
    } else if (button == "Feature-85" || button == "Rough-85 16x16" || pool85Brush(button) || button.rfind("Hills-85", 0) == 0) {
        base = "Base-85";
    } else if (button.rfind("Pool-86", 0) == 0 || button.rfind("Hill-86", 0) == 0) {
        world = "Lava";
        base = "Base-86";
    } else if (seaIslandKind(button) != 0) {
        base = "Base-0";
    } else if (button.rfind("Hills-78", 0) == 0) {
        world = "Archipelago";
        base = "Base-78";
    } else if (button.rfind("Hill-194", 0) == 0) {
        world = "Archipelago";
        base = "Base-194";
    } else if (button == "Pools 8x8") {
        world = "Lush";
        base = "Base-77";
    } else {
        world = "GreenWorld";
        base = "Base-85";
    }
    return true;
}

std::vector<RandomMapStampBrush> EditorApp::collectStampBrushes() {
    // The generator swaps the brush's state out and back; a catalogue building on it is stopped first and is read
    // again afterwards.
    stopCatalogueBuild();
    std::vector<RandomMapStampBrush> brushes;
    std::vector<std::string> worlds;
    for (const RandomMapPaint& paint : randomMapTiles_.paints()) {
        if (std::find(worlds.begin(), worlds.end(), paint.world) == worlds.end()) {
            worlds.push_back(paint.world);
        }
    }
    const std::string savedWorld = brushWorld_;
    const std::string savedName = brushName_;
    const int savedNext = nextSeaIsland_;
    // The Tile Brush's own catalog is set aside while each world's stamps are read, and put
    // back after, so the brush does not rebuild it.
    auto savedBrushCatalogKey = std::move(brushCatalogKey_);
    auto savedBrushCoastLiquid = std::move(brushCoastLiquid_);
    auto savedBrushPaints = std::move(brushPaints_);
    auto savedBrushPartners = std::move(brushPartners_);
    auto savedBrushPictures = std::move(brushPictures_);
    auto savedBrushPieces = std::move(brushPieces_);
    auto savedBrushStrokes = std::move(brushStrokes_);
    auto savedBrushTilesetPath = std::move(brushTilesetPath_);
    auto savedDunePictures = std::move(dunePictures_);
    auto savedHillSections = std::move(hillSections_);
    auto savedRiverCatalogReady = std::move(riverCatalogReady_);
    auto savedRiverCatalog = std::move(riverCatalog_);
    auto savedRiverSections = std::move(riverSections_);
    auto savedSharedPictures = std::move(sharedPictures_);
    auto savedCoarseBandTiles = std::move(coarseBandTiles_);
    auto savedFineBandTiles = std::move(fineBandTiles_);
    auto savedFixedTiles = std::move(fixedTiles_);
    auto savedForeignPictures = std::move(foreignPictures_);
    auto savedForeignNames = std::move(foreignNames_);
    auto savedBrushGrounds = std::move(brushGrounds_);
    auto savedBrushBaseTiles = std::move(brushBaseTiles_);
    brushPaints_.clear();
    brushCatalogKey_.clear();
    brushCatalogStampsOnly_ = true;
    for (const std::string& world : worlds) {
        // Arm each world in turn: its catalog lists the stamp buttons and measures their pieces.
        brushWorld_ = world;
        ensureBrushPaints();
        std::vector<std::string> buttons;
        for (const BrushPaint& paint : brushPaints_) {
            if (paint.kind == BrushKind::Hill) {
                buttons.push_back(paint.name);
            }
        }
        for (const std::string& button : buttons) {
            brushName_ = button;
            nextSeaIsland_ = -1;
            // Mountains grow along a drag and dunes join their neighbours, so they are not single stamps.
            if (mountainArmed() || duneArmed()) {
                continue;
            }
            std::string baseWorld;
            std::string baseName;
            if (!stampBaseOf(button, baseWorld, baseName)) {
                continue;
            }
            RandomMapStampBrush brush;
            brush.name = stampBrushScriptName(world, button);
            brush.base = stampBrushScriptName(baseWorld, baseName);
            const auto add = [&](const HillSection& hill, int width, int height) {
                if (width > 0 && height > 0) {
                    brush.sections.push_back(RandomMapStampSection{hill.sectionPath, width, height});
                }
            };
            if (brushVaries()) {
                // Island-0 Medium and Large, and Crystal's Rough-85, mix sizes.
                for (const HillSection& hill : hillSections_) {
                    if (nextMatches(hill)) {
                        add(hill, hill.tiles, hill.high());
                    }
                }
            } else if (groupHill() != nullptr) {
                for (const HillSection& hill : hillSections_) {
                    if (hill.group == button && hill.tiles > 0) {
                        add(hill, hill.tiles, hill.high());
                    }
                }
            } else {
                // The pieces paintHill chooses from for this button.
                const int span = hillSpan();
                const bool island = button == "Island 8x8";
                const bool rough = button.rfind("Rough", 0) == 0;
                const bool pool = button == "Pools 8x8";
                const bool base78 = button.rfind("Hills-78", 0) == 0;
                const int seaIsland = seaIslandKind(button);
                const bool lavaPool = button.rfind("Pool-86", 0) == 0;
                const bool hill85 = button.rfind("Hills-85", 0) == 0;
                const bool pool85 = pool85Brush(button);
                const bool marsRough = button == "Rough-85 16x16";
                const bool marsFeature = button == "Feature-85";
                for (const HillSection& hill : hillSections_) {
                    if (hill.tiles == span && hill.crystalFolder.empty() && hill.group.empty() && hill.island == island && hill.rough == rough
                        && hill.pool == pool && hill.base78 == base78 && hill.seaIsland == seaIsland && hill.lavaPool == lavaPool
                        && hill.hill85 == hill85 && hill.pool85 == pool85 && hill.marsRough == marsRough && hill.marsFeature == marsFeature) {
                        add(hill, span, span);
                    }
                }
            }
            if (!brush.sections.empty()) {
                brushes.push_back(std::move(brush));
            }
        }
    }
    brushCatalogStampsOnly_ = false;
    brushWorld_ = savedWorld;
    brushName_ = savedName;
    nextSeaIsland_ = savedNext;
    for (BrushPaint& paint : brushPaints_) {
        destroyTexture(paint.preview);
    }
    brushCatalogKey_ = std::move(savedBrushCatalogKey);
    brushCoastLiquid_ = std::move(savedBrushCoastLiquid);
    brushPaints_ = std::move(savedBrushPaints);
    brushPartners_ = std::move(savedBrushPartners);
    brushPictures_ = std::move(savedBrushPictures);
    brushPieces_ = std::move(savedBrushPieces);
    brushStrokes_ = std::move(savedBrushStrokes);
    brushTilesetPath_ = std::move(savedBrushTilesetPath);
    dunePictures_ = std::move(savedDunePictures);
    hillSections_ = std::move(savedHillSections);
    riverCatalogReady_ = std::move(savedRiverCatalogReady);
    riverCatalog_ = std::move(savedRiverCatalog);
    riverSections_ = std::move(savedRiverSections);
    sharedPictures_ = std::move(savedSharedPictures);
    coarseBandTiles_ = std::move(savedCoarseBandTiles);
    fineBandTiles_ = std::move(savedFineBandTiles);
    fixedTiles_ = std::move(savedFixedTiles);
    foreignPictures_ = std::move(savedForeignPictures);
    foreignNames_ = std::move(savedForeignNames);
    brushGrounds_ = std::move(savedBrushGrounds);
    brushBaseTiles_ = std::move(savedBrushBaseTiles);
    ensureBrushPaints();
    return brushes;
}

void EditorApp::ensureStampBrushes() {
    if (randomMapTilesKey_.empty() || randomMapTilesKey_ == randomMapStampKey_ || randomMapTiles_.paints().empty()) {
        return;
    }
    randomMapStampBrushes_ = collectStampBrushes();
    randomMapStampKey_ = randomMapTilesKey_;
}

bool EditorApp::generateRandomMap() {
    if (!startRandomMap()) {
        return false;
    }
    if (randomMapWork_.valid()) {
        randomMapWork_.wait();
    }
    while (randomMapJob_) {
        finishRandomMap();
    }
    return !randomMapFailed_;
}

bool EditorApp::startRandomMap() {
    if (randomMapJob_) {
        return false;
    }
    randomMapNotes_.clear();
    randomMapResult_.clear();
    randomMapFailed_ = true;
    if (!map_) {
        randomMapResult_ = text(Text::RandomMapNeedsMap);
        return false;
    }
    const TilesetArchive* source = nullptr;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (isRandomMapTileset(archive.path)) {
            source = &archive;
            break;
        }
    }
    if (source == nullptr) {
        randomMapResult_ = text(Text::RandomMapNoTileset);
        return false;
    }
    const std::string key = source->path.string() + "\n" + std::to_string(source->sections.size());
    if (key != randomMapTilesKey_) {
        randomMapTiles_ = RandomMapTiles(source->sections);
        randomMapTilesKey_ = key;
    }
    const RandomMapTiles& tiles = randomMapTiles_;

    try {
        ensureStampBrushes();
        auto job = std::make_shared<RandomMapJob>();
        job->serial = mapSerial_;
        job->tiles = &tiles;
        job->stampBrushes = randomMapStampBrushes_;
        const auto stampNamed = [job = job.get()](std::string_view name) -> const RandomMapStampBrush* {
            const std::vector<RandomMapStampBrush>& stampBrushes = job->stampBrushes;
            std::string wanted(name);
            for (char& character : wanted) {
                character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            }
            for (const RandomMapStampBrush& brush : stampBrushes) {
                if (brush.name == wanted) {
                    return &brush;
                }
            }
            return nullptr;
        };
        const int cell = kBrushCellTiles;
        const int cellsX = (map_->tileWidth + cell - 1) / cell;
        const int cellsY = (map_->tileHeight + cell - 1) / cell;
        const int players = std::clamp(randomMapPlayers_, 1, 10);
        job->rng.seed(static_cast<std::uint32_t>(randomMapSeed_));
        std::mt19937& rng = job->rng;
        RmsScriptSettings settings;
        settings.conditions = rmsConditions(players, cellsX, cellsY);
        job->script = readRandomMapScript(randomMapScript_, settings, rng);
        const RmsScript& script = job->script;

        // The brush's levels are every base of each world the script names.
        std::vector<int> used;
        for (const std::string& name : rmsTerrainNames(script, kRandomMapFallbackTerrain)) {
            const int paint = tiles.findPaint(name);
            if (paint >= 0) {
                used.push_back(paint);
            }
        }
        // A hill or island needs its own ground in the brush, such as GreenWorld Base-85.
        for (const RmsCommand& command : script.commands) {
            if (command.name == "create_terrain" && !command.args.empty()) {
                if (const RandomMapStampBrush* brush = stampNamed(command.args[0].text)) {
                    if (const int paint = tiles.findPaint(brush->base); paint >= 0) {
                        used.push_back(paint);
                    }
                }
            }
        }
        if (used.empty() && tiles.findPaint(kRandomMapFallbackTerrain) >= 0) {
            used.push_back(tiles.findPaint(kRandomMapFallbackTerrain));
        }
        job->levels = tiles.levelsFor(used);
        const std::vector<int>& levels = job->levels;
        if (levels.empty()) {
            randomMapResult_ = text(Text::RandomMapNoTileset);
            return false;
        }
        job->canvas.emplace(cellsX + 1, cellsY + 1, tiles.graphFor(levels));
        RmsTerrainNames& names = job->names;
        names.find = [job = job.get()](std::string_view name) {
            const RandomMapTiles& tiles = *job->tiles;
            const std::vector<int>& levels = job->levels;
            const int paint = tiles.findPaint(name);
            const auto found = std::find(levels.begin(), levels.end(), paint);
            return paint < 0 || found == levels.end() ? -1 : static_cast<int>(found - levels.begin());
        };
        names.fallback = std::max(0, names.find(kRandomMapFallbackTerrain));
        names.isStamp = [stampNamed](std::string_view name) { return stampNamed(name) != nullptr; };
        // A cliff rises to the nearest higher base of the same world that the Tile
        // Brush joins with cliff faces: GreenWorld Base-85 to Base-219, Lava Base-86 to Base-190.
        names.cliffAbove = [job = job.get()](int terrain) {
            const RandomMapTiles& tiles = *job->tiles;
            const std::vector<int>& levels = job->levels;
            if (terrain < 0 || static_cast<std::size_t>(terrain) >= levels.size()) {
                return -1;
            }
            const RandomMapPaint& low = tiles.paints()[static_cast<std::size_t>(levels[static_cast<std::size_t>(terrain)])];
            int best = -1;
            for (std::size_t i = 0; i < levels.size(); ++i) {
                const RandomMapPaint& high = tiles.paints()[static_cast<std::size_t>(levels[i])];
                if (lowerText(high.world) != lowerText(low.world) || high.height <= low.height) {
                    continue;
                }
                const bool cliff = std::any_of(brushTransitionRules().begin(), brushTransitionRules().end(), [&](const BrushTransitionRule& rule) {
                    return rule.style == TransitionStyle::Cliff && rule.lowerHeight == low.height && rule.upperHeight == high.height;
                });
                if (cliff && (best < 0 || high.height < tiles.paints()[static_cast<std::size_t>(levels[static_cast<std::size_t>(best)])].height)) {
                    best = static_cast<int>(i);
                }
            }
            return best;
        };
        // Features come from every loaded archive: the game's own and TA_Features_2013.ccx.
        // Where two archives define one name, the first one loaded wins, as in the Features tab.
        RmsObjectSettings& objects = job->objects;
        {
            std::unordered_set<std::string> seen;
            for (const TilesetArchive& archive : tilesets_.archives()) {
                for (const FeatureInfo& feature : archive.features) {
                    if (feature.name.empty() || !seen.insert(lowerText(feature.name)).second) {
                        continue;
                    }
                    objects.features.push_back(RmsFeature{feature.name, feature.world, feature.category,
                                                          std::max(1, feature.footprintX), std::max(1, feature.footprintZ),
                                                          feature.metal, feature.geothermal});
                }
            }
            for (const int level : levels) {
                const RandomMapPaint& paint = tiles.paints()[static_cast<std::size_t>(level)];
                objects.terrainLands.push_back(blendWorldName(paint.world));
                objects.terrainBases.push_back(paint.name);
                objects.terrainHeights.push_back(paint.height);
                // Water, shallows, lava and the other seas are the low side of a coast.
                bool liquid = false;
                for (const BrushTransitionRule& rule : brushTransitionRules()) {
                    const bool sea = rule.style == TransitionStyle::Coast || rule.style == TransitionStyle::ShallowCoast
                        || rule.style == TransitionStyle::ShallowDeep;
                    liquid = liquid || (sea && rule.lowerHeight == paint.height);
                }
                objects.terrainLiquid.push_back(liquid);
            }
            objects.cellsWide = map_->heightWidth();
            objects.cellsHigh = map_->heightHeight();
            objects.cellsPerNode = kBrushCellTiles * 2;
        }
        job->players = players;
        job->tileWidth = map_->tileWidth;
        job->tileHeight = map_->tileHeight;
        job->step = static_cast<int>(RandomMapStep::Lands);
        randomMapJob_ = job;
        randomMapWork_ = std::async(std::launch::async, [job]() { job->run(); });
        statusNote_ = text(Text::RandomMapGenerating);
        return true;
    } catch (const std::exception& ex) {
        randomMapResult_ = ex.what();
        return false;
    }
}

void EditorApp::cancelRandomMap() {
    if (randomMapJob_) {
        randomMapJob_->cancel = true;
    }
}

EditorApp::~EditorApp() {
    stopCatalogueBuild();
    // A random map still running is told to stop; the future then waits for it.
    cancelRandomMap();
    if (randomMapWork_.valid()) {
        randomMapWork_.wait();
    }
}

void EditorApp::drawRandomMapProgress() {
    const std::string title = withId(Text::RandomMapProgressTitle, "Random Map Progress");
    if (randomMapJob_ && !ImGui::IsPopupOpen(title.c_str())) {
        ImGui::OpenPopup(title.c_str());
    }
    if (!ImGui::BeginPopupModal(title.c_str(), nullptr, theme::alertFlags())) {
        return;
    }
    if (!randomMapJob_) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    // Each step's share of the bar, roughly how long it takes on a big map.
    static constexpr double kWeights[] = {0.03, 0.10, 0.02, 0.05, 0.10, 0.05, 0.45, 0.12, 0.03, 0.05};
    static constexpr Text kLabels[] = {Text::RandomMapStepPreparing, Text::RandomMapStepLands, Text::RandomMapStepElevation,
                                       Text::RandomMapStepCliffs, Text::RandomMapStepTerrain, Text::RandomMapStepConnections,
                                       Text::RandomMapStepObjects, Text::RandomMapStepTiles, Text::RandomMapStepStamps,
                                       Text::RandomMapStepBuilding};
    static_assert(std::size(kWeights) == static_cast<std::size_t>(RandomMapStep::Count));
    static_assert(std::size(kLabels) == static_cast<std::size_t>(RandomMapStep::Count));
    const int step = std::clamp(randomMapJob_->step.load(), 0, static_cast<int>(RandomMapStep::Count) - 1);
    double done = 0.0;
    for (int i = 0; i < step; ++i) {
        done += kWeights[i];
    }
    done += kWeights[step] * std::clamp(randomMapJob_->fraction.load(), 0.0, 1.0);
    const bool cancelling = randomMapJob_->cancel.load();
    const std::string label = std::string(text(cancelling ? Text::Cancel : kLabels[step])) + (cancelling ? "" : "...");
    ImGui::TextUnformatted(label.c_str());
    const float width = std::max(ImGui::GetFontSize() * 22.0f, ImGui::CalcTextSize(label.c_str()).x);
    const std::string percent = std::to_string(static_cast<int>(std::lround(done * 100.0))) + "%";
    ImGui::ProgressBar(static_cast<float>(done), ImVec2(width, 0.0f), percent.c_str());
    ImGui::Spacing();
    const char* cancelLabel = text(Text::Cancel);
    const float cancelWidth = theme::pushButtonWidth(cancelLabel);
    theme::alignRight(cancelWidth);
    ImGui::BeginDisabled(cancelling || randomMapJob_->building);
    if (ImGui::Button(cancelLabel, ImVec2(cancelWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        cancelRandomMap();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

void EditorApp::finishRandomMap() {
    if (!randomMapJob_ || !randomMapJob_->done) {
        return;
    }
    RandomMapJob& job = *randomMapJob_;
    if (!job.building && !job.cancelled && job.failure.empty() && randomMapWork_.valid()) {
        // One frame with the last step showing before the main thread is busy writing the map.
        job.building = true;
        job.step = static_cast<int>(RandomMapStep::Building);
        job.fraction = 0.0;
        return;
    }
    if (randomMapWork_.valid()) {
        randomMapWork_.get();
    }
    const std::shared_ptr<RandomMapJob> held = std::move(randomMapJob_);
    randomMapJob_.reset();
    // A run that fails says why in the Random Map window; a finished one sets its own note.
    statusNote_.clear();
    if (job.cancelled) {
        randomMapResult_ = text(Text::RandomMapCancelled);
        statusNote_ = randomMapResult_;
        return;
    }
    if (!job.failure.empty()) {
        randomMapResult_ = job.failure;
        return;
    }
    // The map being generated is the one showing; the progress window keeps it so.
    if (!map_ || job.serial != mapSerial_) {
        randomMapResult_ = text(Text::RandomMapCancelled);
        return;
    }
    const TilesetArchive* source = nullptr;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (isRandomMapTileset(archive.path)) {
            source = &archive;
            break;
        }
    }
    if (source == nullptr) {
        randomMapResult_ = text(Text::RandomMapNoTileset);
        return;
    }
    try {
        const RmsScript& script = job.script;
        const RandomMapOutcome& outcome = job.outcome;
        const RandomMapStampPlan& plan = job.plan;
        const RmsObjectSettings& objects = job.objects;
        const std::vector<RmsMessage>& stampNotes = job.stampNotes;
        // Every section is read before the map changes, so a missing file leaves the map alone.
        std::unordered_map<std::string, SectionStamp> loaded;
        for (const RandomMapStamp& stamp : plan.stamps) {
            if (loaded.count(stamp.sectionPath) != 0) {
                continue;
            }
            const HpiFile* file = source->archive().find(stamp.sectionPath);
            if (file == nullptr) {
                throw std::runtime_error(stamp.sectionPath + " is missing from the random map tileset.");
            }
            loaded.emplace(stamp.sectionPath, loadSectionStamp(source->archive().extract(*file)));
        }

        const int tileCountBefore = map_->tileCount;
        std::optional<TilePatch> before = captureTilePatch(*map_, 0, 0, map_->tileWidth, map_->tileHeight);
        if (!before) {
            randomMapResult_ = text(Text::RandomMapNeedsMap);
            return;
        }
        for (std::size_t index = 0; index < plan.stamps.size(); ++index) {
            const RandomMapStamp& stamp = plan.stamps[index];
            const SectionStamp& section = loaded.at(stamp.sectionPath);
            if (stamp.fill > 0 && section.tileWidth > 0 && section.tileHeight > 0
                && (section.tileWidth < stamp.fill || section.tileHeight < stamp.fill)) {
                // A base smaller than the cell repeats until the cell is covered.
                for (int y = 0; y < stamp.fill; y += section.tileHeight) {
                    for (int x = 0; x < stamp.fill; x += section.tileWidth) {
                        stampSection(*map_, section, stamp.tileX + x, stamp.tileY + y, &deduper_, 100);
                    }
                }
            } else {
                stampSection(*map_, section, stamp.tileX, stamp.tileY, &deduper_, 100);
            }
        }
        std::optional<TilePatch> after = captureTilePatch(*map_, 0, 0, map_->tileWidth, map_->tileHeight);
        PlacementChange change;
        change.before = std::move(*before);
        change.after = std::move(*after);
        if (change.after.tileCount > change.before.tileCount) {
            const std::size_t start = static_cast<std::size_t>(change.before.tileCount) * 1024;
            change.addedTiles.assign(map_->tileImages.begin() + static_cast<std::ptrdiff_t>(start), map_->tileImages.end());
        }
        EditorCommand command;
        command.terrain = std::move(change);
        int featuresSkipped = 0;
        int nodesUneven = 0;
        int ventsUneven = 0;
        int treesOnSlopes = 0;
        if (outcome.placesObjects) {
            // A script with <OBJECTS_GENERATION> replaces every feature on the map.
            // A metal node or geothermal vent goes only where every height under its
            // footprint is the same, since no extractor or plant can be built on a slope.
            // One on uneven ground is left out.
            std::unordered_map<std::string, const RmsFeature*> byName;
            for (const RmsFeature& feature : objects.features) {
                byName.emplace(lowerText(feature.name), &feature);
            }
            const int cellsWide = map_->heightWidth();
            const int cellsHigh = map_->heightHeight();
            std::vector<std::uint8_t> occupied(static_cast<std::size_t>(cellsWide) * static_cast<std::size_t>(cellsHigh), 0);
            const auto footprintOf = [&](const std::string& name) {
                const auto found = byName.find(lowerText(name));
                return found == byName.end() ? std::make_pair(1, 1)
                                             : std::make_pair(std::max(1, found->second->footprintX), std::max(1, found->second->footprintZ));
            };
            const auto take = [&](int x, int y, const std::string& name) {
                const auto [w, h] = footprintOf(name);
                for (int cy = std::max(0, y); cy < std::min(cellsHigh, y + h); ++cy) {
                    for (int cx = std::max(0, x); cx < std::min(cellsWide, x + w); ++cx) {
                        occupied[static_cast<std::size_t>(cy) * static_cast<std::size_t>(cellsWide) + static_cast<std::size_t>(cx)] = 1;
                    }
                }
            };
            std::vector<FeaturePlacement> placements;
            placements.reserve(outcome.features.size());
            for (const RmsPlacedFeature& feature : outcome.features) {
                const auto found = byName.find(lowerText(feature.name));
                // A tree goes only on a level tile: all four of its height cells the same.
                // A feature is drawn raised by half its ground height, so the tile checked
                // is the one the tree's base shows on, worked out the way it is drawn.
                if (found != byName.end() && featureIsDoodad(*found->second, DoodadKind::Trees)) {
                    const int depth = std::max(1, found->second->footprintZ);
                    const int shownY = (feature.y * 16 + depth * 8 - featureLift(*map_, feature.x, feature.y)) >> 4;
                    if (!tileIsLevel(map_->heights, cellsWide, cellsHigh, feature.x, std::max(0, shownY))) {
                        ++treesOnSlopes;
                        continue;
                    }
                    // Trees reach onto coast tiles, but a level tile down at or under the sea is water.
                    const std::size_t shownAt = static_cast<std::size_t>(std::clamp(shownY, 0, cellsHigh - 1)) * static_cast<std::size_t>(cellsWide)
                        + static_cast<std::size_t>(std::clamp(feature.x, 0, cellsWide - 1));
                    if (shownAt < map_->heights.size() && map_->heights[shownAt] <= map_->seaLevel) {
                        ++treesOnSlopes;
                        continue;
                    }
                }
                if (found != byName.end()) {
                    const bool metal = featureIsDoodad(*found->second, DoodadKind::MetalNodes);
                    const bool vent = !metal && featureIsDoodad(*found->second, DoodadKind::GeoVents);
                    const auto [w, h] = footprintOf(feature.name);
                    if ((metal || vent) && !footprintIsLevel(map_->heights, cellsWide, cellsHigh, feature.x, feature.y, w, h)) {
                        ++(metal ? nodesUneven : ventsUneven);
                        continue;
                    }
                }
                placements.push_back(FeaturePlacement{feature.x, feature.y, feature.name});
                take(feature.x, feature.y, feature.name);
            }
            command.featureBatch = replaceMapFeatures(*map_, placements, &featuresSkipped);
            selectedFeatures_.clear();
        }
        if (outcome.placesStarts) {
            // Player lands and START_POSITION replace the map's start positions. A start
            // sits at the middle of its clear square, in map pixels.
            command.startsBefore = map_->starts;
            std::vector<StartPosition> starts;
            for (const RmsStart& start : outcome.starts) {
                starts.push_back(StartPosition{start.player, std::clamp(start.x * 16, 0, std::max(0, map_->pixelWidth() - 1)),
                                               std::clamp(start.y * 16, 0, std::max(0, map_->pixelHeight() - 1))});
            }
            map_->starts = starts;
            command.startsAfter = std::move(starts);
            draggingStartPlayer_ = 0;
        }
        pushCommand(std::move(command), true);
        warnIfPastVanillaTiles(tileCountBefore);

        randomMapNotes_ = script.messages;
        randomMapNotes_.insert(randomMapNotes_.end(), stampNotes.begin(), stampNotes.end());
        randomMapNotes_.insert(randomMapNotes_.end(), outcome.messages.begin(), outcome.messages.end());
        std::stable_sort(randomMapNotes_.begin(), randomMapNotes_.end(), [](const RmsMessage& left, const RmsMessage& right) {
            return left.line < right.line;
        });
        if (plan.missingCells > 0) {
            std::string names;
            for (std::size_t i = 0; i < plan.missing.size() && i < 3; ++i) {
                names += (i == 0 ? "" : ", ") + plan.missing[i];
            }
            randomMapNotes_.push_back(RmsMessage{0, std::to_string(plan.missingCells) + " cells had no piece in the tileset (" + names
                                                         + "), so they were filled with their most common base."});
        }
        if (nodesUneven > 0) {
            randomMapNotes_.push_back(RmsMessage{0, std::to_string(nodesUneven) + (nodesUneven == 1 ? " metal node" : " metal nodes")
                                                         + " landed where the heights under " + (nodesUneven == 1 ? "it" : "them")
                                                         + " were not all the same, where no metal extractor can be built, so "
                                                         + (nodesUneven == 1 ? "it was" : "they were") + " left out."});
        }
        if (ventsUneven > 0) {
            randomMapNotes_.push_back(RmsMessage{0, std::to_string(ventsUneven) + (ventsUneven == 1 ? " geothermal vent" : " geothermal vents")
                                                         + " landed where the heights under " + (ventsUneven == 1 ? "it" : "them")
                                                         + " were not all the same, where no geothermal plant can be built, so "
                                                         + (ventsUneven == 1 ? "it was" : "they were") + " left out."});
        }
        if (treesOnSlopes > 0) {
            randomMapNotes_.push_back(RmsMessage{0, std::to_string(treesOnSlopes) + (treesOnSlopes == 1 ? " tree" : " trees")
                                                         + " landed on a tile whose four heights were not all the same, or under the sea, so "
                                                         + (treesOnSlopes == 1 ? "it was" : "they were") + " left out."});
        }
        if (featuresSkipped > 0) {
            randomMapNotes_.push_back(RmsMessage{0, std::to_string(featuresSkipped) + " features landed on open space or past the map's limit of feature names, so they were left out."});
        }
        randomMapResult_ = formatText(Text::RandomMapDone, {std::to_string(randomMapSeed_), std::to_string(randomMapNotes_.size())});
        randomMapFailed_ = false;
        statusNote_ = "Generated a random map: " + std::to_string(outcome.lands) + (outcome.lands == 1 ? " land, " : " lands, ")
            + std::to_string(outcome.terrains) + (outcome.terrains == 1 ? " terrain, " : " terrains, ")
            + std::to_string(outcome.connections) + (outcome.connections == 1 ? " connection" : " connections")
            + (outcome.placesObjects ? ", " + std::to_string(outcome.features.size() - static_cast<std::size_t>(featuresSkipped + nodesUneven + ventsUneven + treesOnSlopes)) + " features" : std::string())
            + (outcome.placesStarts ? ", " + std::to_string(outcome.starts.size()) + (outcome.starts.size() == 1 ? " start" : " starts") : std::string());
        error_.clear();
    } catch (const std::exception& ex) {
        randomMapResult_ = ex.what();
    }
}

void EditorApp::drawRandomMap() {
    if (!showRandomMap_) {
        return;
    }
    if (!randomMapLoaded_) {
        randomMapLoaded_ = true;
        std::ifstream in(randomMapScriptPath(), std::ios::binary);
        std::ostringstream saved;
        if (in) {
            saved << in.rdbuf();
        }
        randomMapScript_ = saved.str().empty() ? std::string(kExampleRandomMapScripts[0].text) : saved.str();
        std::mt19937 seedRng{std::random_device{}()};
        randomMapSeed_ = std::uniform_int_distribution<int>(1, 999983)(seedRng);
    }
    const float u = theme::unit();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + std::floor(240.0f * u), viewport->WorkPos.y + std::floor(60.0f * u)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::floor(900.0f * u), std::floor(660.0f * u)), ImGuiCond_FirstUseEver);
    const std::string title = withId(Text::RandomMapGenerator, "Random Map Generator");
    const bool wasOpen = showRandomMap_;
    if (!ImGui::Begin(title.c_str(), &showRandomMap_)) {
        ImGui::End();
        return;
    }
    if (wasOpen && !showRandomMap_) {
        saveRandomMapScript();
    }
    // Command + and Command - make the script larger or smaller, as in Xcode.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        int step = 0;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Equal)
            || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd)) {
            step = 1;
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract)) {
            step = -1;
        }
        if (step != 0) {
            const std::vector<int>& sizes = theme::codeFontSizes();
            int index = 0;
            for (int i = 0; i < static_cast<int>(sizes.size()); ++i) {
                if (std::abs(sizes[static_cast<std::size_t>(i)] - randomMapFontSize_) < std::abs(sizes[static_cast<std::size_t>(index)] - randomMapFontSize_)) {
                    index = i;
                }
            }
            index = std::clamp(index + step, 0, static_cast<int>(sizes.size()) - 1);
            if (sizes[static_cast<std::size_t>(index)] != randomMapFontSize_) {
                randomMapFontSize_ = sizes[static_cast<std::size_t>(index)];
                saveEditorOptions();
            }
            statusNote_ = "Script text " + std::to_string(randomMapFontSize_) + " pt";
        }
    }
    theme::helpText(text(Text::RandomMapHelp));
    ImGui::Spacing();

    const ImGuiStyle& style = ImGui::GetStyle();
    const bool showNotes = !randomMapResult_.empty() || !randomMapNotes_.empty();
    const float notesHeight = showNotes ? std::floor(110.0f * u) : 0.0f;
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + style.ItemSpacing.y * 2.0f + (showNotes ? notesHeight + style.ItemSpacing.y : 0.0f);
    const float listWidth = std::floor(250.0f * u);
    const float editorWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - listWidth - style.ItemSpacing.x);
    const float bodyHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y - footer);

    if (scriptEditor("##random-map-script", randomMapScript_, ImVec2(editorWidth, bodyHeight), randomMapFontSize_)) {
        saveRandomMapScript();
    }
    ImGui::SameLine();
    ImGui::BeginChild("random-map-terrains", ImVec2(listWidth, bodyHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    theme::sourceListTitle(text(Text::RandomMapTerrains));
    ImGui::PushFont(theme::smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
    ImGui::TextWrapped("%s", text(Text::RandomMapTerrainsHelp));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Spacing();
    // The terrain list reads the random map tileset; it fills in once that archive is loaded.
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (!isRandomMapTileset(archive.path)) {
            continue;
        }
        const std::string key = archive.path.string() + "\n" + std::to_string(archive.sections.size());
        // A random map running in the background reads these tiles, so they wait for it.
        if (key != randomMapTilesKey_ && !generatingRandomMap()) {
            randomMapTiles_ = RandomMapTiles(archive.sections);
            randomMapTilesKey_ = key;
        }
        break;
    }
    if (randomMapTiles_.paints().empty()) {
        ImGui::TextWrapped("%s", text(Text::RandomMapNoTileset));
    }
    std::string world;
    // GreenWorld's hills, rough patches and islands follow its bases.
    // Each world's hills, pools, islands and other stamps follow its bases.
    ensureStampBrushes();
    const auto listStamps = [&](const std::string& finished) {
        if (finished.empty()) {
            return;
        }
        std::string prefix = finished + "_";
        for (char& character : prefix) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        for (const RandomMapStampBrush& stamp : randomMapStampBrushes_) {
            if (stamp.name.compare(0, prefix.size(), prefix) == 0 && ImGui::Selectable(stamp.name.c_str())) {
                ImGui::SetClipboardText(stamp.name.c_str());
                statusNote_ = "Copied " + stamp.name;
            }
        }
    };
    for (const RandomMapPaint& paint : randomMapTiles_.paints()) {
        if (paint.world != world) {
            listStamps(world);
            world = paint.world;
            if (ImGui::GetCursorPosY() > ImGui::GetTextLineHeightWithSpacing() * 3.0f) {
                ImGui::Spacing();
            }
            ImGui::PushFont(theme::boldFont());
            ImGui::TextUnformatted(world.c_str());
            ImGui::PopFont();
        }
        if (ImGui::Selectable(paint.terrain.c_str())) {
            ImGui::SetClipboardText(paint.terrain.c_str());
            statusNote_ = "Copied " + paint.terrain;
        }
    }
    listStamps(world);
    // Doodad kinds for create_object. Each follows the ground under each spot.
    ImGui::Spacing();
    ImGui::Spacing();
    theme::sourceListTitle(text(Text::RandomMapDoodads));
    if (ImGui::Selectable("START_POSITION")) {
        ImGui::SetClipboardText("START_POSITION");
        statusNote_ = "Copied START_POSITION";
    }
    for (const DoodadKindInfo& kind : doodadKinds()) {
        if (ImGui::Selectable(kind.name)) {
            ImGui::SetClipboardText(kind.name);
            statusNote_ = std::string("Copied ") + kind.name;
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text(Text::RandomMapPlayers));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::floor(110.0f * u));
    if (ImGui::InputInt("##random-map-players", &randomMapPlayers_)) {
        randomMapPlayers_ = std::clamp(randomMapPlayers_, 1, 10);
    }
    ImGui::SameLine(0.0f, std::floor(18.0f * u));
    ImGui::TextUnformatted(text(Text::RandomMapSeed));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::floor(140.0f * u));
    ImGui::InputInt("##random-map-seed", &randomMapSeed_);
    ImGui::SameLine();
    if (ImGui::Button(text(Text::RandomMapNewSeed))) {
        std::mt19937 seedRng{std::random_device{}()};
        randomMapSeed_ = std::uniform_int_distribution<int>(1, 999983)(seedRng);
    }

    if (showNotes) {
        ImGui::Spacing();
        ImGui::BeginChild("random-map-notes", ImVec2(0.0f, notesHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        if (!randomMapResult_.empty()) {
            if (randomMapFailed_) {
                const ImVec4 warning = theme::dark() ? ImVec4(0.98f, 0.80f, 0.25f, 1.0f) : ImVec4(0.66f, 0.45f, 0.0f, 1.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, warning);
                ImGui::TextWrapped("%s", randomMapResult_.c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::TextWrapped("%s", randomMapResult_.c_str());
            }
        }
        ImGui::PushFont(theme::smallFont());
        for (const RmsMessage& note : randomMapNotes_) {
            const std::string line = note.line > 0 ? formatText(Text::RandomMapLine, {std::to_string(note.line), note.text}) : note.text;
            ImGui::TextWrapped("%s", line.c_str());
        }
        ImGui::PopFont();
        ImGui::EndChild();
    }

    ImGui::Spacing();
    const char* exampleLabel = text(Text::RandomMapExample);
    const char* generateLabel = text(Text::RandomMapGenerate);
    const float exampleWidth = theme::pushButtonWidth(exampleLabel);
    const float generateWidth = theme::pushButtonWidth(generateLabel);
    if (ImGui::Button(exampleLabel, ImVec2(exampleWidth, 0.0f))) {
        ImGui::OpenPopup("random-map-examples");
    }
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("random-map-examples")) {
        ImGui::SetTooltip("%s", text(Text::RandomMapExampleTip));
    }
    // One example for each base tileset. Choosing one replaces the script in the box.
    if (ImGui::BeginPopup("random-map-examples")) {
        float widest = 0.0f;
        std::vector<std::string> labels;
        for (std::size_t i = 0; i < kExampleRandomMapScriptCount; ++i) {
            const ExampleRandomMapScript& example = kExampleRandomMapScripts[i];
            labels.push_back(std::string(example.world) + "  \xE2\x80\x94  " + example.title);
            widest = std::max(widest, theme::pushButtonWidth(labels.back().c_str()));
        }
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (ImGui::Button(labels[i].c_str(), ImVec2(widest, 0.0f))) {
                randomMapScript_ = kExampleRandomMapScripts[i].text;
                saveRandomMapScript();
                statusNote_ = std::string("Loaded the ") + kExampleRandomMapScripts[i].world + " example";
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (!map_) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextUnformatted(text(Text::RandomMapNeedsMap));
        ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    theme::alignRight(generateWidth);
    ImGui::BeginDisabled(!map_ || savingMap() || generatingRandomMap());
    // Return types a new line in the script, so it does not press Generate.
    if (theme::defaultButton(generateLabel, ImVec2(generateWidth, 0.0f), false)) {
        saveRandomMapScript();
        startRandomMap();
    }
    ImGui::EndDisabled();
    ImGui::End();
}

std::string backupMapName(std::string name) {
    for (char& character : name) {
        const bool illegal = static_cast<unsigned char>(character) < 32
            || character == '\\' || character == '/' || character == ':' || character == '*'
            || character == '?' || character == '"' || character == '<' || character == '>' || character == '|';
        if (illegal) {
            character = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    if (name.empty()) {
        return "map";
    }
    return name;
}

std::tm localClock() {
    const std::time_t now = std::time(nullptr);
    std::tm parts{};
#if defined(_WIN32)
    localtime_s(&parts, &now);
#else
    localtime_r(&now, &parts);
#endif
    return parts;
}

void EditorApp::showVanillaTileWarning() {
    if (vanillaTileWarningShown_) {
        return;
    }
    vanillaTileWarningShown_ = true;
    vanillaTileWarning_ = "This map has too many tiles to support Vanilla Total Annihilation 3.1. It will only work on Open Annihilation.";
}

void EditorApp::warnIfPastVanillaTiles(int tileCountBefore) {
    if (map_ && tileCountBefore <= kVanillaTileSlots && map_->tileCount > kVanillaTileSlots) {
        showVanillaTileWarning();
    }
}

void EditorApp::markMapDirty() {
    ++mapRevision_;
    if (!mapDirty_) {
        mapDirty_ = true;
        autosaveArmedAt_ = std::chrono::steady_clock::now();
    }
}

void EditorApp::autosaveIfDue() {
    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::minutes(std::clamp(autosaveMinutes_, 1, 120));
    // Maps in the other tabs are backed up on the same schedule as the one showing.
    for (int i = 0; i < static_cast<int>(documents_.size()); ++i) {
        const bool active = i == activeDocument_;
        MapDocument& document = documents_[static_cast<std::size_t>(i)];
        const std::optional<LoadedMap>& map = active ? map_ : document.map;
        bool& dirty = active ? mapDirty_ : document.dirty;
        std::chrono::steady_clock::time_point& armedAt = active ? autosaveArmedAt_ : document.autosaveArmedAt;
        if (!map || !dirty || now - armedAt < interval) {
            continue;
        }
        try {
            const std::string filename = writeAutosave(*map);
            dirty = false;
            statusNote_ = "Autosaved " + filename;
            error_.clear();
        } catch (const std::exception& ex) {
            armedAt = now;
            error_ = ex.what();
        }
    }
}

std::string EditorApp::writeAutosave(const LoadedMap& map) {
    const int copies = std::clamp(autosaveBackups_, 1, 20);
    {
        const std::string mapName = backupMapName(map.source.empty() ? map.displayName : map.source.stem().string());
        std::filesystem::path directory = map.source.has_parent_path() ? map.source.parent_path() : suggestedOpenDirectory();
        if (directory.empty()) {
            directory = std::filesystem::current_path();
        }
        const std::tm now = localClock();
        const std::string filename = backupFilename(mapName, now.tm_mon + 1, now.tm_mday, now.tm_year + 1900, now.tm_hour, now.tm_min);
        const std::filesystem::path destination = directory / filename;

        std::vector<std::string> existing;
        std::error_code listError;
        if (std::filesystem::is_directory(directory, listError)) {
            for (const auto& entry : std::filesystem::directory_iterator(directory, listError)) {
                if (listError) {
                    break;
                }
                if (!entry.is_regular_file()) {
                    continue;
                }
                existing.push_back(entry.path().filename().string());
            }
        }
        if (listError) {
            throw std::runtime_error("Could not read the backup folder " + directory.string());
        }

        std::error_code existsError;
        const bool replacing = std::filesystem::exists(destination, existsError) && !existsError;
        if (!replacing) {
            for (const std::string& oldName : backupNamesToDelete(existing, mapName, copies)) {
                std::error_code removeError;
                std::filesystem::remove(directory / oldName, removeError);
                if (removeError) {
                    throw std::runtime_error("Could not remove old backup " + oldName);
                }
            }
        }

        saveMapFile(map, destination);
        return filename;
    }
}

bool EditorApp::draw() {
    updateAppearance();
    if (const std::string hpiError = hpiEditor_.takeError(); !hpiError.empty()) {
        error_ = hpiError;
    }
    if (const std::string hpiStatus = hpiEditor_.takeStatus(); !hpiStatus.empty()) {
        statusNote_ = hpiStatus;
    }
    if (hpiEditor_.takeQuitApproval()) {
        hpiQuitApproved_ = true;
        requestClose();
    }
    hpiEditor_.setCurrentMapPath(map_ ? map_->source : std::filesystem::path());
    if (loadingTilesets()) {
        continueLoadingTilesets(std::chrono::milliseconds(16));
    }
    finishMapSave();
    finishRandomMap();
    if (!savingMap() && !generatingRandomMap()) {
        autosaveIfDue();
    }
    const int placementId = pasteArmed_ ? -2 : selectedSection_;
    if (placementId != placementOffsetFor_) {
        placeOffsetX_ = 0;
        placeOffsetY_ = 0;
        placementOffsetFor_ = placementId;
    }
    if (!ImGui::GetIO().WantTextInput && !hpiEditor_.focused() && !generatingRandomMap()) {
        // The Features tab copies, cuts and pastes features only, and every
        // other tab does the same for terrain tiles only.
        const bool featureKeys = featuresTab_ && map_.has_value();
        const bool tileKeys = !featuresTab_;
        if (featureKeys && !selectedFeatures_.empty() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            copyFeatures(false);
        } else if (featureKeys && !selectedFeatures_.empty() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
            copyFeatures(true);
        } else if (featureKeys && !featureClipboard_.empty() && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            armFeaturePaste();
        } else if (featureKeys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
            selectedFeatures_.clear();
            for (const FeaturePlacement& feature : map_->features) {
                selectedFeatures_.insert({feature.x, feature.y});
            }
            statusNote_ = std::to_string(selectedFeatures_.size()) + " features selected";
        } else if (featureKeys && !selectedFeatures_.empty()
            && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            deleteSelectedFeatures();
        } else if (sidebarTab_ == 1 && !favoriteSelection_.empty()
            && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)
            && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            requestDeleteFavoriteSelection();
        } else if (tileKeys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_C)) {
            copySelection(true);
        } else if (tileKeys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            copySelection(false);
        } else if (tileKeys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
            cutSelection();
        } else if (tileKeys && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            armPaste();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) {
            showNew_ = true;
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
            if (const auto chosen = openMapDialog(suggestedOpenDirectory())) {
                openMap(*chosen);
            }
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_W)) {
            if (map_ && !savingMap() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
                requestCloseDocument(activeDocument_);
            }
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) {
            saveMapAs();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
            saveMap();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)
            || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) {
            redoPlacement();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
            undoPlacement();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) {
            showOptions_ = true;
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_3)) {
            set3dView(!view3d_);
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0)) {
            showSidebar_ = !showSidebar_;
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
            requestClose();
        } else if (!view3d_ && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                adjustPlaceOffset(-1, 0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                adjustPlaceOffset(1, 0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                adjustPlaceOffset(0, -1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                adjustPlaceOffset(0, 1);
            }
        }
        // N skips to another random island while Island-0 Medium or Large is armed.
        if (brushVaries() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeySuper && !ImGui::GetIO().KeyAlt
            && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
            skipSeaIsland();
        }
        // [ and ] step the base brush the way Photoshop does. Rivers, ridges, slopes and hills stay the size on their button.
        if (!brushName_.empty() && !lineBrushArmed() && hillSpan() == 0 && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
            int index = 0;
            for (int i = 0; i < kBrushSizeCount; ++i) {
                if (kBrushSizes[i] == brushSize_) {
                    index = i;
                }
            }
            int next = index;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket) && index > 0) {
                next = index - 1;
            } else if (ImGui::IsKeyPressed(ImGuiKey_RightBracket) && index + 1 < kBrushSizeCount) {
                next = index + 1;
            }
            if (next != index) {
                brushSize_ = kBrushSizes[next];
                statusNote_ = "Brush " + brushSizeLabel(brushSize_);
            }
        }
        // 1-9 choose that start, and 0 chooses Player 10. The keypad does the same.
        if (startLocationsTab_ && map_ && draggingStartPlayer_ == 0
            && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && !ImGui::GetIO().KeySuper
            && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
            constexpr ImGuiKey kNumberKeys[10][2] = {
                {ImGuiKey_0, ImGuiKey_Keypad0},
                {ImGuiKey_1, ImGuiKey_Keypad1},
                {ImGuiKey_2, ImGuiKey_Keypad2},
                {ImGuiKey_3, ImGuiKey_Keypad3},
                {ImGuiKey_4, ImGuiKey_Keypad4},
                {ImGuiKey_5, ImGuiKey_Keypad5},
                {ImGuiKey_6, ImGuiKey_Keypad6},
                {ImGuiKey_7, ImGuiKey_Keypad7},
                {ImGuiKey_8, ImGuiKey_Keypad8},
                {ImGuiKey_9, ImGuiKey_Keypad9},
            };
            for (int number = 0; number <= 9; ++number) {
                if (!ImGui::IsKeyPressed(kNumberKeys[number][0], false) && !ImGui::IsKeyPressed(kNumberKeys[number][1], false)) {
                    continue;
                }
                const int player = number == 0 ? 10 : number;
                armedStartPlayer_ = player;
                pasteArmed_ = false;
                revealStartPlayer_ = player;
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) && !hpiEditor_.focused()
        && !showAddFavorite_ && !showCreateFavorites_ && !showDeleteFavorite_ && !showRenameTile_ && !selectionMenuOpen_) {
        if (showMapSettings_ && !ImGui::GetIO().WantTextInput) {
            showMapSettings_ = false;
        } else if (pasteArmed_) {
            pasteArmed_ = false;
            statusNote_ = "Paste cancelled";
        } else if (featureDelete_) {
            featureDelete_ = false;
            statusNote_ = "Delete cancelled";
        } else if (featurePasteArmed_) {
            featurePasteArmed_ = false;
            statusNote_ = "Paste cancelled";
        } else if (featuresTab_ && !armedFeature_.empty()) {
            armedFeature_.clear();
        } else if (featuresTab_ && !selectedFeatures_.empty()) {
            selectedFeatures_.clear();
        } else if (armedStartPlayer_ != 0 || draggingStartPlayer_ != 0) {
            armedStartPlayer_ = 0;
            draggingStartPlayer_ = 0;
        } else if (selectionMode_ && selecting_) {
            selection_ = selectionBefore_;
            selecting_ = false;
        } else if (selectionMode_ && hasSelection()) {
            const TileSelection before = selection_;
            selection_ = {};
            rememberSelection(before, selection_);
        } else if (!brushName_.empty() && !ImGui::IsPopupOpen("##brush-size")) {
            brushName_.clear();
            statusNote_ = "Brush cancelled";
        } else {
            selectedSection_ = -1;
            favoriteSelection_.clear();
            favoriteSelectionArchive_.clear();
            tilesetSelection_.clear();
            tilesetSelectionArchive_.clear();
        }
    }

    pumpSectionLoads();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("Galaxy Editor", nullptr, flags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu(text(Text::MenuFile))) {
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::NewMap)).c_str(), theme::shortcut("N").c_str())) {
                showNew_ = true;
            }
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::OpenMap)).c_str(), theme::shortcut("O").c_str())) {
                if (const auto chosen = openMapDialog(suggestedOpenDirectory())) {
                    openMap(*chosen);
                }
            }
            if (ImGui::MenuItem(text(Text::CloseMap), theme::shortcut("W").c_str(), false, map_.has_value() && !savingMap())) {
                requestCloseDocument(activeDocument_);
            }
            ImGui::Separator();
            if (ImGui::MenuItem(text(Text::Save), theme::shortcut("S").c_str(), false, map_.has_value())) {
                saveMap();
            }
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::SaveAs)).c_str(), theme::shortcut("Shift+S").c_str(), false, map_.has_value())) {
                saveMapAs();
            }
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::ExportPng)).c_str(), nullptr, false, map_.has_value())) {
                exportMapPng();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::RandomMapGenerator)).c_str(), nullptr, showRandomMap_)) {
                showRandomMap_ = true;
                ImGui::SetWindowFocus(withId(Text::RandomMapGenerator, "Random Map Generator").c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem(text(Text::HpiEditor))) {
                hpiEditor_.show();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(theme::quitLabel(), theme::shortcut("Q").c_str())) {
                requestClose();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(text(Text::MenuEdit))) {
            if (ImGui::MenuItem(text(Text::Undo), theme::shortcut("Z").c_str(), false, map_.has_value() && !undo_.empty())) {
                undoPlacement();
            }
            if (ImGui::MenuItem(text(Text::Redo), theme::shortcut("Shift+Z").c_str(), false, map_.has_value() && !redo_.empty())) {
                redoPlacement();
            }
            ImGui::Separator();
            if (featuresTab_) {
                const bool hasFeatures = map_.has_value() && !selectedFeatures_.empty();
                if (ImGui::MenuItem(text(Text::CutFeatures), theme::shortcut("X").c_str(), false, hasFeatures)) {
                    copyFeatures(true);
                }
                if (ImGui::MenuItem(text(Text::CopyFeatures), theme::shortcut("C").c_str(), false, hasFeatures)) {
                    copyFeatures(false);
                }
                if (ImGui::MenuItem(text(Text::PasteFeatures), theme::shortcut("V").c_str(), false, map_.has_value() && !featureClipboard_.empty())) {
                    armFeaturePaste();
                }
                if (ImGui::MenuItem(text(Text::DeleteSelectedFeatures), nullptr, false, hasFeatures)) {
                    deleteSelectedFeatures();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(text(Text::SelectFeatures), nullptr, featureSelectMode_, map_.has_value())) {
                    toggleFeatureSelectMode();
                }
            } else {
                if (ImGui::MenuItem(text(Text::Cut), theme::shortcut("X").c_str(), false, map_.has_value() && hasSelection())) {
                    cutSelection();
                }
                if (ImGui::MenuItem(text(Text::CopyTerrain), theme::shortcut("C").c_str(), false, map_.has_value() && hasSelection())) {
                    copySelection(false);
                }
                if (ImGui::MenuItem(text(Text::CopyHeightMap), theme::shortcut("Shift+C").c_str(), false, map_.has_value() && hasSelection())) {
                    copySelection(true);
                }
                if (ImGui::MenuItem(text(Text::Paste), theme::shortcut("V").c_str(), false, map_.has_value() && clipboard_.has_value())) {
                    armPaste();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(text(Text::SelectTiles), nullptr, selectionMode_, map_.has_value())) {
                    toggleSelectMode();
                }
            }
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::ModifyHeight)).c_str(), nullptr, false, map_.has_value() && hasSelection() && !featuresTab_)) {
                showModifyHeight_ = true;
            }
            if (ImGui::MenuItem(text(Text::BlendTextures), nullptr, false, map_.has_value() && hasSelection() && !featuresTab_)) {
                blendSelection();
            }
            if (ImGui::MenuItem(text(Text::DeleteFeatures), nullptr, featureDelete_, map_.has_value() && featuresTab_)) {
                toggleDeleteMode();
            }
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::HeightOffset)).c_str())) {
                placeHeightDraftAdd_ = placeHeightAdd_;
                placeHeightDraftAmount_ = placeHeightAmount_;
                showPlaceHeight_ = true;
            }
            if (ImGui::BeginMenu(text(Text::TileOpacity))) {
                for (int value = 5; value <= 100; value += 5) {
                    const std::string choice = std::to_string(value) + "%";
                    if (ImGui::MenuItem(choice.c_str(), nullptr, placeOpacity_ == value)) {
                        placeOpacity_ = value;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(text(Text::MenuMap))) {
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::MapSettings)).c_str(), nullptr, false, map_.has_value())) {
                showMapSettings_ = true;
                mapSettingsFresh_ = true;
            }
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::ResizeMap)).c_str(), nullptr, false, map_.has_value())) {
                resizeLeft_ = 0;
                resizeRight_ = 0;
                resizeTop_ = 0;
                resizeBottom_ = 0;
                showResize_ = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(text(Text::MenuView))) {
            if (ImGui::MenuItem(text(Text::FitMap), nullptr, false, map_.has_value())) {
                fit_ = true;
            }
            if (ImGui::MenuItem(text(Text::ActualSize), nullptr, false, map_.has_value())) {
                fit_ = false;
                zoom_ = 1.0f;
            }
            if (ImGui::BeginMenu(text(Text::Grid))) {
                if (ImGui::MenuItem(text(Text::GridOff), nullptr, gridTiles_ == 0)) {
                    gridTiles_ = 0;
                }
                for (const int size : {1, 4, 8, 16, 32, 64, 128, 256, 512}) {
                    const std::string label = std::to_string(size) + " x " + std::to_string(size);
                    if (ImGui::MenuItem(label.c_str(), nullptr, gridTiles_ == size)) {
                        gridTiles_ = size;
                    }
                }
                ImGui::Separator();
                // Each corner of the 16 x 16 tile cells with the paint the Tile Brush reads there.
                ImGui::MenuItem(text(Text::GridBrushCorners), nullptr, &showBrushCorners_, map_.has_value());
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(text(Text::View3d), theme::shortcut("3").c_str(), view3d_, map_.has_value())) {
                set3dView(!view3d_);
            }
            if (ImGui::MenuItem(text(Text::Reset3dCamera), nullptr, false, map_.has_value() && view3d_)) {
                camera3d_.frame(static_cast<float>(map_->pixelWidth()), static_cast<float>(map_->pixelHeight()));
            }
            ImGui::Separator();
            if (ImGui::MenuItem(showHeightGrid_ ? text(Text::HideHeightMap) : text(Text::ShowHeightMap))) {
                showHeightGrid_ = !showHeightGrid_;
            }
            ImGui::MenuItem(text(Text::FeatureMarkers), nullptr, &showFeatures_);
            ImGui::MenuItem(text(Text::ShowFeatureBorders), nullptr, &showFeatureBorders_);
            ImGui::MenuItem(text(Text::ShowFeatureLabels), nullptr, &showFeatureLabels_);
            ImGui::MenuItem(text(Text::StartPositions), nullptr, &showStarts_);
            ImGui::MenuItem(text(Text::Minimap), nullptr, &showMinimap_);
            if (ImGui::MenuItem(showSidebar_ ? text(Text::HideSidebar) : text(Text::ShowSidebar), theme::shortcut("0").c_str())) {
                showSidebar_ = !showSidebar_;
            }
            ImGui::MenuItem(text(Text::TileBrush), nullptr, &showTileBrush_);
            ImGui::MenuItem(text(Text::FrostedGlass), nullptr, &frostedGlass_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(text(Text::MenuOptions))) {
            if (ImGui::MenuItem(theme::withEllipsis(text(Text::EditorOptions)).c_str(), theme::shortcut(",").c_str())) {
                showOptions_ = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(text(Text::MenuHelp))) {
            if (ImGui::MenuItem(text(Text::AboutMenu))) {
                aboutRequested_ = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float top = ImGui::GetCursorPosY();
    const ImVec2 body = ImGui::GetContentRegionAvail();
    const float toolbarHeight = theme::toolbarHeight();
    const float cardWidth = static_cast<float>(previewSize_) + style.FramePadding.x * 2.0f;
    const float sidebarFontScale = std::clamp(sidebarFontPercent_, 75, 200) / 100.0f;
    // A hidden sidebar gives its whole width to the map.
    const float sidebarFullWidth = std::clamp(
        cardWidth + style.WindowPadding.x * 2.0f + style.ScrollbarSize + style.IndentSpacing * 2.0f + 16.0f
            + (sidebarFontScale - 1.0f) * 180.0f,
        348.0f * sidebarFontScale,
        720.0f);
    // Showing or hiding the sidebar slides it in or out over about a fifth of a
    // second, close to the macOS sidebar, easing at both ends.
    constexpr float kSidebarSlideSeconds = 0.22f;
    const float sidebarTarget = showSidebar_ ? 1.0f : 0.0f;
    if (!sidebarRevealStarted_) {
        sidebarReveal_ = sidebarTarget;
        sidebarRevealStarted_ = true;
    }
    if (sidebarReveal_ != sidebarTarget) {
        const float step = std::min(ImGui::GetIO().DeltaTime, 0.05f) / kSidebarSlideSeconds;
        sidebarReveal_ = sidebarTarget > sidebarReveal_ ? std::min(sidebarTarget, sidebarReveal_ + step)
                                                        : std::max(sidebarTarget, sidebarReveal_ - step);
    }
    const float sidebarEase = sidebarReveal_ * sidebarReveal_ * (3.0f - 2.0f * sidebarReveal_);
    // The map gets whatever the sidebar does not cover. Whole pixels keep the map steady.
    const float sidebarWidth = std::floor(sidebarFullWidth * sidebarEase);
    const float statusHeight = std::floor(ImGui::GetFrameHeight() + 2.0f);
    const float bodyTop = top + toolbarHeight;
    const float bodyHeight = std::max(1.0f, body.y - toolbarHeight - statusHeight);
    // With two or more maps open, a row of tabs sits over the map, under the toolbar.
    const float tabsHeight = std::min(documentTabsHeight(), std::max(0.0f, bodyHeight - 1.0f));
    // The map goes first so that panning and zooming are settled before the
    // toolbar and sidebar glass copy the part of the map that lies behind them.
    ImGui::SetCursorPos(ImVec2(sidebarWidth, bodyTop + tabsHeight));
    ImGui::BeginChild("map-slot", ImVec2(std::max(1.0f, body.x - sidebarWidth), std::max(1.0f, bodyHeight - tabsHeight)), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawMap();
    ImGui::EndChild();
    if (tabsHeight > 0.0f) {
        ImGui::SetCursorPos(ImVec2(sidebarWidth, bodyTop));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("document-tab-row", ImVec2(std::max(1.0f, body.x - sidebarWidth), tabsHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        drawDocumentTabs();
        ImGui::EndChild();
    }
    ImGui::SetCursorPos(ImVec2(0.0f, top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, std::floor((toolbarHeight - theme::toolbarItemHeight()) * 0.5f)));
    ImGui::BeginChild("toolbar", ImVec2(body.x, toolbarHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    drawToolbar();
    ImGui::EndChild();

    if (sidebarWidth >= 1.0f) {
        // The sidebar keeps its full width while it slides, so its contents move
        // rather than squeeze. The part past the window's left edge is cut off.
        ImGui::SetCursorPos(ImVec2(sidebarWidth - sidebarFullWidth, bodyTop));
        ImGui::BeginChild("sidebar-slot", ImVec2(sidebarFullWidth, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        drawSidebar();
        ImGui::EndChild();
    }
    ImGui::SetCursorPos(ImVec2(0.0f, bodyTop + bodyHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, std::floor((statusHeight - ImGui::GetTextLineHeight() * 11.0f / 13.0f) * 0.5f)));
    ImGui::BeginChild("status", ImVec2(body.x, statusHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    theme::bottomBarBackground();
    ImGui::PushFont(theme::smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
    drawStatus();
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndChild();

    drawClosePrompt();
    drawAbout();

    const std::string errorTitle = withId(Text::ErrorTitle, "Error");
    if (!error_.empty()) {
        ImGui::OpenPopup(errorTitle.c_str());
    }
    if (ImGui::BeginPopupModal(errorTitle.c_str(), nullptr, theme::alertFlags())) {
        theme::alertText(text(Text::ErrorLead), error_.c_str());
        const float okWidth = theme::pushButtonWidth(text(Text::Ok));
        theme::alignRight(okWidth);
        if (theme::defaultButton(text(Text::Ok), ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            error_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    const std::string autoMetalTitle = withId(Text::AutoMetal, "Auto Metal");
    if (!autoMetalAlert_.empty()) {
        ImGui::OpenPopup(autoMetalTitle.c_str());
    }
    if (ImGui::BeginPopupModal(autoMetalTitle.c_str(), nullptr, theme::alertFlags())) {
        theme::alertText(autoMetalAlert_.c_str());
        const float okWidth = theme::pushButtonWidth(text(Text::Ok));
        theme::alignRight(okWidth);
        if (theme::defaultButton(text(Text::Ok), ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            autoMetalAlert_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    const std::string tileLimitTitle = withId(Text::TileLimitTitle, "Tile Limit");
    if (!vanillaTileWarning_.empty()) {
        ImGui::OpenPopup(tileLimitTitle.c_str());
    }
    if (ImGui::BeginPopupModal(tileLimitTitle.c_str(), nullptr, theme::alertFlags())) {
        theme::alertText(vanillaTileWarning_.c_str());
        const float okWidth = theme::pushButtonWidth(text(Text::Ok));
        theme::alignRight(okWidth);
        if (theme::defaultButton(text(Text::Ok), ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            vanillaTileWarning_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawRandomMapProgress();
    drawNewMap();
    drawResizeMap();
    drawModifyHeight();
    drawPlaceHeight();
    drawAddFavorite();
    drawDeleteFavorite();
    drawRenameTile();

    ImGui::End();
    drawOptions();
    drawMapSettings();
    drawRandomMap();
    drawMinimap();
    drawTileBrush();
    hpiEditor_.draw();
    drawTooltipGlass();
    if (featureDelete_ && featuresTab_) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* overlay = ImGui::GetForegroundDrawList();
        const ImVec2 square(mouse.x + 10.0f, mouse.y + 12.0f);
        overlay->AddRectFilled(square, ImVec2(square.x + 6.0f, square.y + 6.0f), IM_COL32(220, 32, 32, 240));
        overlay->AddRect(square, ImVec2(square.x + 6.0f, square.y + 6.0f), IM_COL32(0, 0, 0, 230));
    }
    return !quit_;
}

void EditorApp::armBrush(const std::string& name) {
    if (name.empty()) {
        return;
    }
    if (brushName_ == name) {
        brushName_.clear();
        statusNote_ = "Brush cancelled";
        return;
    }
    brushName_ = name;
    if (lineBrushArmed()) {
        // Reading every river, coast and base picture takes a moment, so it happens on the click that needs it.
        ensureRiverCatalog();
    }
    brushStroking_ = false;
    selectionMode_ = false;
    selecting_ = false;
    pasteArmed_ = false;
    selectedSection_ = -1;
    statusNote_ = "Brush " + name;
}

int EditorApp::brushCellTiles() const {
    if (brushSize_ > kFineCellTiles || armedBrushKind() != BrushKind::Base) {
        return kBrushCellTiles;
    }
    for (const BrushPiece& piece : brushPieces_) {
        if (piece.tiles == kFineCellTiles) {
            return kFineCellTiles;
        }
    }
    return kBrushCellTiles;
}

void EditorApp::paintBrushStroke(int tileX, int tileY) {
    if (!map_ || brushName_.empty() || brushTilesetPath_.empty()) {
        return;
    }
    if (catalogueBuilding_) {
        statusNote_ = "The Tile Brush is still building its picture catalogue";
        return;
    }
    if (armedBrushKind() == BrushKind::Hill) {
        paintHill(tileX, tileY);
        return;
    }
    if (lineBrushArmed()) {
        paintRiver(tileX, tileY, armedBrushKind());
        return;
    }
    const int cell = brushCellTiles();
    BrushOutcome outcome = paintBaseBrush(tileX, tileY, cell, true, cell);
    bool movedToFine = false;
    if (outcome == BrushOutcome::NeedsFine) {
        // A 16 tile stroke beside an 8 x 8 band is painted on the 8 tile cells, so the band stays one
        // size. It keeps the footprint of the 16 tile stroke.
        movedToFine = true;
        outcome = paintBaseBrush(tileX, tileY, kFineCellTiles, false, kBrushCellTiles);
        if (outcome == BrushOutcome::Done && error_.empty()) {
            statusNote_ += ". It meets 8 x 8 pieces, so its edges are 8 x 8 too";
        }
    }
    if (outcome != BrushOutcome::NeedsCoarse) {
        return;
    }
    if (!movedToFine) {
        // A 4 x 4 or 8 x 8 stroke that reached a cliff. Painted 16 x 16 instead, it would land off its
        // outline and leave its edges against the 8 x 8 work around it, so the map is left alone.
        const bool coast = coarseOnlyPiece_.rfind("Coast-", 0) == 0 || coarseOnlyPiece_.rfind("ShallowCoast-", 0) == 0
            || coarseOnlyPiece_.rfind("ShallowDeep-", 0) == 0 || coarseOnlyPiece_.rfind("DSL-", 0) == 0;
        statusNote_ = coast && !kFineCoastsEnabled
            ? "This stroke reaches a coast, and coasts are painted with the 16 x 16 brush for now. Paint here with the 16 x 16 brush"
            : coarseOnlyPiece_ + " only comes in 16 x 16, so a 4 x 4 or 8 x 8 stroke cannot reach it. Paint here with the 16 x 16 brush";
        error_.clear();
        return;
    }
    // The stroke reached a cliff, and cliffs only have 16 x 16 pieces, so the stroke is painted on the
    // 16 tile cells, the footprint its outline showed.
    const std::string coarsePiece = coarseOnlyPiece_;
    paintBaseBrush(tileX, tileY, kBrushCellTiles, false, kBrushCellTiles);
    if (error_.empty()) {
        statusNote_ += ". " + coarsePiece + " only comes in 16 x 16, so this stroke used the 16 x 16 pieces";
    }
}

int EditorApp::brushCellPieceSizes(int cellX, int cellY) const {
    if (!map_ || (coarseBandTiles_.empty() && fineBandTiles_.empty())) {
        return 0;
    }
    // Another tileset's ground the brush cannot join, such as a lava coast while GreenWorld is chosen:
    // its band is left alone.
    if (!foreignGroundIn(cellX * kBrushCellTiles, cellY * kBrushCellTiles, kBrushCellTiles, kBrushCellTiles).empty()) {
        return 0;
    }
    int sizes = 0;
    const int x0 = cellX * kBrushCellTiles;
    const int y0 = cellY * kBrushCellTiles;
    for (int ty = std::max(0, y0); ty < std::min(map_->tileHeight, y0 + kBrushCellTiles) && sizes != (kCoarseBand | kFineBand); ++ty) {
        for (int tx = std::max(0, x0); tx < std::min(map_->tileWidth, x0 + kBrushCellTiles); ++tx) {
            const std::size_t index = static_cast<std::size_t>(ty) * static_cast<std::size_t>(map_->tileWidth) + static_cast<std::size_t>(tx);
            if (index >= map_->indices.size()) {
                continue;
            }
            const std::size_t bytes = static_cast<std::size_t>(map_->indices[index]) * 1024;
            if (bytes + 1024 > map_->tileImages.size()) {
                continue;
            }
            const std::uint32_t picture = map_->indices[index];
            auto found = pictureBandSizes_.find(picture);
            if (found == pictureBandSizes_.end()) {
                const std::uint64_t hash = hashBrushTile(map_->tileImages.data() + bytes);
                int pictureSizes = 0;
                if (std::binary_search(coarseBandTiles_.begin(), coarseBandTiles_.end(), hash)) {
                    pictureSizes |= kCoarseBand;
                } else if (std::binary_search(fixedTiles_.begin(), fixedTiles_.end(), hash)) {
                    pictureSizes |= kFixedPiece;
                } else if (std::binary_search(fineBandTiles_.begin(), fineBandTiles_.end(), hash)
                           || (!brushBaseTiles_.empty() && brushBaseTiles_.count(hash) == 0)) {
                    // An 8 x 8 piece's band, or a tile that is neither a 16 x 16 piece's band nor flat
                    // ground: an 8 x 8 piece whose ground was moved onto the map's grid, or something the
                    // brush did not lay. Either way a 16 x 16 piece beside it would not continue its band.
                    pictureSizes |= kFineBand;
                }
                found = pictureBandSizes_.emplace(picture, pictureSizes).first;
            }
            sizes |= found->second;
        }
    }
    return sizes;
}

// The brush picture cache. Each tileset's catalogue has its own file in <random map tileset>.brushcache, named
// by a hash of the catalogue key. Its header holds the tileset's size and modification time and this build's
// compile time, so a new tileset or any rebuilt editor reads the pictures again. Strings are stored once in a
// table, and each picture refers to them by number.
namespace {

constexpr std::uint32_t kPictureCacheMagic = 0x47454243; // "GEBC"
constexpr std::uint32_t kPictureCacheVersion = 1;

std::string pictureCacheStamp(const std::filesystem::path& tileset) {
    std::error_code error;
    const auto size = std::filesystem::file_size(tileset, error);
    const auto modified = std::filesystem::last_write_time(tileset, error);
    const long long ticks = error ? 0LL : static_cast<long long>(modified.time_since_epoch().count());
    return std::to_string(error ? 0ULL : static_cast<unsigned long long>(size)) + "/" + std::to_string(ticks) + "/" + __DATE__
        + " " + __TIME__;
}

void putU32(std::string& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void putU64(std::string& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void putText(std::string& out, const std::string& text) {
    putU32(out, static_cast<std::uint32_t>(text.size()));
    out += text;
}

struct CacheReader {
    const std::string& data;
    std::size_t at = 0;
    bool ok = true;

    std::uint64_t number(int bytes) {
        if (at + static_cast<std::size_t>(bytes) > data.size()) {
            ok = false;
            return 0;
        }
        std::uint64_t value = 0;
        for (int i = 0; i < bytes; ++i) {
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[at + static_cast<std::size_t>(i)])) << (8 * i);
        }
        at += static_cast<std::size_t>(bytes);
        return value;
    }
    std::string text() {
        const std::size_t size = static_cast<std::size_t>(number(4));
        if (!ok || at + size > data.size()) {
            ok = false;
            return {};
        }
        std::string value = data.substr(at, size);
        at += size;
        return value;
    }
};

}  // namespace

std::filesystem::path EditorApp::brushPictureCachePath(const std::filesystem::path& tileset, const std::string& catalogueKey) const {
    const std::uint64_t hash = std::hash<std::string>{}(catalogueKey);
    char name[40];
    std::snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(hash));
    std::filesystem::path folder = tileset;
    folder += ".brushcache";
    return folder / name;
}

bool EditorApp::loadBrushPictureCache(const std::filesystem::path& cache) {
    std::string data;
    {
        std::ifstream in(cache, std::ios::binary);
        if (!in) {
            return false;
        }
        data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CacheReader reader{data};
    if (reader.number(4) != kPictureCacheMagic || reader.number(4) != kPictureCacheVersion
        || reader.text() != pictureCacheStamp(brushTilesetPath_) || !reader.ok) {
        return false;
    }
    const std::string liquid = reader.text();
    std::vector<std::string> strings(static_cast<std::size_t>(reader.number(4)));
    for (std::string& text : strings) {
        text = reader.text();
    }
    const auto stringAt = [&](std::uint64_t index) -> const std::string& {
        static const std::string none;
        if (index >= strings.size()) {
            reader.ok = false;
            return none;
        }
        return strings[static_cast<std::size_t>(index)];
    };
    std::unordered_map<std::uint64_t, BrushPictureHint> pictures;
    const std::size_t pictureCount = static_cast<std::size_t>(reader.number(4));
    pictures.reserve(pictureCount);
    for (std::size_t i = 0; i < pictureCount && reader.ok; ++i) {
        const std::uint64_t hash = reader.number(8);
        BrushPictureHint hint;
        hint.category = stringAt(reader.number(4));
        hint.name = stringAt(reader.number(4));
        hint.fringe = reader.number(1) != 0;
        hint.partnerCategory = stringAt(reader.number(4));
        hint.partnerName = stringAt(reader.number(4));
        pictures.emplace(hash, std::move(hint));
    }
    std::unordered_map<std::uint64_t, std::vector<std::pair<std::string, std::string>>> shared;
    const std::size_t sharedCount = static_cast<std::size_t>(reader.number(4));
    for (std::size_t i = 0; i < sharedCount && reader.ok; ++i) {
        const std::uint64_t hash = reader.number(8);
        std::vector<std::pair<std::string, std::string>>& owners = shared[hash];
        const std::size_t ownerCount = static_cast<std::size_t>(reader.number(4));
        for (std::size_t j = 0; j < ownerCount && reader.ok; ++j) {
            const std::string& category = stringAt(reader.number(4));
            owners.emplace_back(category, stringAt(reader.number(4)));
        }
    }
    if (!reader.ok || reader.at != data.size()) {
        return false;
    }
    brushPictures_ = std::move(pictures);
    sharedPictures_ = std::move(shared);
    brushCoastLiquid_ = liquid;
    return true;
}

void EditorApp::saveBrushPictureCache(const std::filesystem::path& cache) const {
    std::vector<std::string> strings;
    std::unordered_map<std::string, std::uint32_t> numbers;
    const auto numberOf = [&](const std::string& text) {
        const auto [found, added] = numbers.emplace(text, static_cast<std::uint32_t>(strings.size()));
        if (added) {
            strings.push_back(text);
        }
        return found->second;
    };
    std::string body;
    putU32(body, static_cast<std::uint32_t>(brushPictures_.size()));
    for (const auto& [hash, hint] : brushPictures_) {
        putU64(body, hash);
        putU32(body, numberOf(hint.category));
        putU32(body, numberOf(hint.name));
        body.push_back(hint.fringe ? 1 : 0);
        putU32(body, numberOf(hint.partnerCategory));
        putU32(body, numberOf(hint.partnerName));
    }
    putU32(body, static_cast<std::uint32_t>(sharedPictures_.size()));
    for (const auto& [hash, owners] : sharedPictures_) {
        putU64(body, hash);
        putU32(body, static_cast<std::uint32_t>(owners.size()));
        for (const auto& [category, name] : owners) {
            putU32(body, numberOf(category));
            putU32(body, numberOf(name));
        }
    }
    std::string out;
    putU32(out, kPictureCacheMagic);
    putU32(out, kPictureCacheVersion);
    putText(out, pictureCacheStamp(brushTilesetPath_));
    putText(out, brushCoastLiquid_);
    putU32(out, static_cast<std::uint32_t>(strings.size()));
    for (const std::string& text : strings) {
        putText(out, text);
    }
    out += body;
    // Written beside the cache and moved into place, so a cache is never read half written.
    std::error_code error;
    std::filesystem::create_directories(cache.parent_path(), error);
    std::filesystem::path partial = cache;
    partial += ".partial";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file.write(out.data(), static_cast<std::streamsize>(out.size()));
        if (!file) {
            return;
        }
    }
    std::filesystem::rename(partial, cache, error);
}

const PaletteImage* EditorApp::brushGround(const std::string& category, const std::string& name) {
    // The catalogue worker fills brushGrounds_ while it builds; until then only it may use them.
    if (catalogueBuilding_ && !tCatalogueWorker) {
        return nullptr;
    }
    const std::string key = lowerText(category) + "/" + lowerText(name);
    if (const auto found = brushGrounds_.find(key); found != brushGrounds_.end()) {
        return found->second.width > 0 ? &found->second : nullptr;
    }
    PaletteImage& ground = brushGrounds_[key];
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (archive.path != brushTilesetPath_) {
            continue;
        }
        for (const TilesetSection& section : archive.sections) {
            if (isBaseBrushTile(section.world, section.name) && lowerText(section.category) == lowerText(category)
                && lowerText(section.name) == lowerText(name)) {
                try {
                    if (const HpiFile* file = archive.archive().find(section.archivePath)) {
                        ground = sectionImage(loadSectionStamp(archive.archive().extract(*file)));
                    }
                } catch (const std::exception&) {
                }
                break;
            }
        }
    }
    return ground.width > 0 ? &ground : nullptr;
}

std::string EditorApp::foreignGroundIn(int tileX, int tileY, int width, int height) const {
    if (!map_ || foreignPictures_.empty()) {
        return {};
    }
    for (int ty = std::max(0, tileY); ty < std::min(map_->tileHeight, tileY + height); ++ty) {
        for (int tx = std::max(0, tileX); tx < std::min(map_->tileWidth, tileX + width); ++tx) {
            const std::size_t index = static_cast<std::size_t>(ty) * static_cast<std::size_t>(map_->tileWidth) + static_cast<std::size_t>(tx);
            if (index >= map_->indices.size()) {
                continue;
            }
            const std::uint32_t picture = map_->indices[index];
            auto found = pictureForeign_.find(picture);
            if (found == pictureForeign_.end()) {
                int named = -1;
                const std::size_t bytes = static_cast<std::size_t>(picture) * 1024;
                if (bytes + 1024 <= map_->tileImages.size()) {
                    if (const auto foreign = foreignPictures_.find(hashBrushTile(map_->tileImages.data() + bytes)); foreign != foreignPictures_.end()) {
                        const auto at = std::find(foreignNames_.begin(), foreignNames_.end(), foreign->second);
                        named = static_cast<int>(at - foreignNames_.begin());
                        if (at == foreignNames_.end()) {
                            foreignNames_.push_back(foreign->second);
                        }
                    }
                }
                found = pictureForeign_.emplace(picture, named).first;
            }
            if (found->second >= 0) {
                return foreignNames_[static_cast<std::size_t>(found->second)];
            }
        }
    }
    return {};
}

std::vector<const EditorApp::BrushPaint*> EditorApp::brushLevels() const {
    std::vector<const BrushPaint*> levels;
    for (const BrushPaint& paint : brushPaints_) {
        if (paint.kind == BrushKind::Base && trailingNumber(paint.name) >= 0) {
            levels.push_back(&paint);
        }
    }
    for (const BrushPaint& partner : brushPartners_) {
        if (trailingNumber(partner.name) >= 0) {
            levels.push_back(&partner);
        }
    }
    std::stable_sort(levels.begin(), levels.end(), [](const BrushPaint* left, const BrushPaint* right) {
        return trailingNumber(left->name) < trailingNumber(right->name);
    });
    return levels;
}

// How the Tile Brush reads the ground at its cell corners: the armed tileset's paints with
// what it takes to tell them apart. A corner sits on the level whose paint height is closest
// to the tiles around it. Two lands can share a height, so a known picture names its land,
// and a blend fringe follows the height at that corner.
EditorApp::BrushCornerReading EditorApp::brushCornerReading(const std::vector<const BrushPaint*>& levels) {
    BrushCornerReading reading;
    reading.levels = levels;
    std::vector<int> levelHeights;
    for (const BrushPaint* paint : levels) {
        levelHeights.push_back(trailingNumber(paint->name));
    }
    const bool pictures = !brushPictures_.empty();
    // A tile whose picture is unknown, such as one from a game map or another section of the
    // tileset, is read by its height alone. Only the armed tileset's own bases are in the
    // running then: the partner lands sit at 77 to 86, so Metal plating at 86 would be read as
    // Lava Base-86, and plating at 85 would tie with five other Base-85 lands and fall to Base-0.
    // A partner land is still found by its picture, which is how a blend is read.
    std::vector<std::size_t> ownLevels;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        // Evergreen's two grasses share their water, so each brush owns both folders' bases, and so do Metal's two waters.
        if (brushCategoryMatches(levels[i]->category, brushWorld_) || brushSiblingWorlds(levels[i]->category, brushWorld_)) {
            ownLevels.push_back(i);
        }
    }
    if (ownLevels.empty()) {
        for (std::size_t i = 0; i < levels.size(); ++i) {
            ownLevels.push_back(i);
        }
    }
    // Each paint's ground colour, for tiles whose picture is unknown where several lands share a
    // height. An 8 x 8 piece's flat ground is moved onto the map grid as it is placed, so its tiles
    // are pictures the brush has never seen; at Base-85 the height alone would read GreenWorld grass
    // beside Evergreen as Evergreen. The nearest ground colour names the land when the tile plainly
    // looks like one; a tile like none of them, such as one from a game map, is read by height.
    std::vector<std::array<double, 3>> levelColours(levels.size(), {-1.0, -1.0, -1.0});
    bool sharedLevelHeight = false;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        for (std::size_t j = i + 1; j < levels.size(); ++j) {
            sharedLevelHeight = sharedLevelHeight || levelHeights[i] == levelHeights[j];
        }
    }
    if (pictures && sharedLevelHeight) {
        for (std::size_t i = 0; i < levels.size(); ++i) {
            const PaletteImage* ground = brushGround(levels[i]->category, levels[i]->name);
            if (ground == nullptr || ground->pixels.empty()) {
                continue;
            }
            std::array<double, 3> mean = {0, 0, 0};
            for (const std::uint8_t index : ground->pixels) {
                for (int channel = 0; channel < 3; ++channel) {
                    mean[static_cast<std::size_t>(channel)] += kTaPalette[index][channel];
                }
            }
            for (double& channel : mean) {
                channel /= static_cast<double>(ground->pixels.size());
            }
            levelColours[i] = mean;
        }
    }
    reading.heights = std::move(levelHeights);
    reading.own = std::move(ownLevels);
    reading.colours = std::move(levelColours);
    reading.shared = sharedLevelHeight;
    reading.pictures = pictures;
    return reading;
}

// The level the four tiles around corner (nodeX, nodeY) of the cell grid vote for, cells of cell tiles.
int EditorApp::readBrushCorner(const BrushCornerReading& reading, const BrushGraph& graph, int nodeX, int nodeY, int cell) const {
    const std::vector<const BrushPaint*>& levels = reading.levels;
    const std::vector<int>& levelHeights = reading.heights;
    const std::vector<std::size_t>& ownLevels = reading.own;
    const std::vector<std::array<double, 3>>& levelColours = reading.colours;
    const bool pictures = reading.pictures;
    const bool sharedLevelHeight = reading.shared;
    const auto nearestLevel = [&](int height, bool abstainOnTie) {
        return nearestOwnLevel(levelHeights, ownLevels, height, abstainOnTie);
    };
    const auto levelByColour = [&](const std::uint8_t* pixels, int height) {
        std::array<double, 3> tileMean = {0, 0, 0};
        for (int i = 0; i < 1024; ++i) {
            for (int channel = 0; channel < 3; ++channel) {
                tileMean[static_cast<std::size_t>(channel)] += kTaPalette[pixels[i]][channel] / 1024.0;
            }
        }
        int best = -1;
        int candidates = 0;
        double bestDistance = 1.0e30;
        for (std::size_t i = 0; i < levels.size(); ++i) {
            // Coast and blend tiles stand a little off their base's height.
            if (std::abs(levelHeights[i] - height) > 3 || levelColours[i][0] < 0) {
                continue;
            }
            ++candidates;
            double distance = 0;
            for (int channel = 0; channel < 3; ++channel) {
                const double gap = tileMean[static_cast<std::size_t>(channel)] - levelColours[i][static_cast<std::size_t>(channel)];
                distance += gap * gap;
            }
            if (distance < bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        return candidates >= 2 && bestDistance < 24.0 * 24.0 ? best : -1;
    };
    std::vector<int> votes(levels.size(), 0);
    for (int dy = -1; dy <= 0; ++dy) {
        for (int dx = -1; dx <= 0; ++dx) {
            const int tx = std::clamp(nodeX * cell + dx, 0, map_->tileWidth - 1);
            const int ty = std::clamp(nodeY * cell + dy, 0, map_->tileHeight - 1);
            int voted = -1;
            if (pictures) {
                const std::size_t cellIndex = static_cast<std::size_t>(ty) * static_cast<std::size_t>(map_->tileWidth) + static_cast<std::size_t>(tx);
                if (cellIndex < map_->indices.size()) {
                    const std::uint32_t image = map_->indices[cellIndex];
                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                    if (bytes + 1024 <= map_->tileImages.size()) {
                        const std::uint64_t pictureHash = hashBrushTile(map_->tileImages.data() + bytes);
                        const auto found = brushPictures_.find(pictureHash);
                        // A picture several bases share, such as Desert Base-80 and Base-155
                        // sand, is one of those bases: the nearest of them by height. Other
                        // paints at a nearer height, such as Lava Base-86, are not in the running.
                        if (const auto shared = found == brushPictures_.end() ? sharedPictures_.find(pictureHash) : sharedPictures_.end();
                            shared != sharedPictures_.end()) {
                            const std::size_t heightIndex = static_cast<std::size_t>(ty * 2) * static_cast<std::size_t>(map_->heightWidth()) + static_cast<std::size_t>(tx * 2);
                            if (heightIndex < map_->heights.size()) {
                                const int height = map_->heights[heightIndex];
                                int bestDistance = 1 << 30;
                                for (std::size_t i = 0; i < levels.size(); ++i) {
                                    bool sharesPicture = false;
                                    for (const auto& [category, name] : shared->second) {
                                        sharesPicture = sharesPicture || (lowerText(levels[i]->category) == lowerText(category) && lowerText(levels[i]->name) == lowerText(name));
                                    }
                                    const int distance = std::abs(height - levelHeights[i]);
                                    if (sharesPicture && distance < bestDistance) {
                                        bestDistance = distance;
                                        voted = static_cast<int>(i);
                                    }
                                }
                            }
                        }
                        if (found != brushPictures_.end()) {
                            int matched = -1;
                            for (std::size_t i = 0; i < levels.size(); ++i) {
                                if (lowerText(levels[i]->category) == lowerText(found->second.category) && lowerText(levels[i]->name) == lowerText(found->second.name)) {
                                    matched = static_cast<int>(i);
                                    break;
                                }
                            }
                            if (matched >= 0 && !found->second.fringe) {
                                voted = matched;
                            } else if (matched >= 0) {
                                int other = -1;
                                const bool namedPartner = !found->second.partnerName.empty();
                                for (int i = 0; i < graph.size(); ++i) {
                                    if (namedPartner) {
                                        if (lowerText(levels[static_cast<std::size_t>(i)]->category) == lowerText(found->second.partnerCategory)
                                            && lowerText(levels[static_cast<std::size_t>(i)]->name) == lowerText(found->second.partnerName)) {
                                            other = i;
                                            break;
                                        }
                                    } else if (graph.style(i, matched) == TransitionStyle::WorldBlend) {
                                        other = i;
                                    }
                                }
                                // A shore's water held under another land of its water family, such as an
                                // Archipelago shore beside the Desert water the GreenWorld brush holds.
                                for (int i = 0; other < 0 && namedPartner && i < graph.size(); ++i) {
                                    const BrushPaint* level = levels[static_cast<std::size_t>(i)];
                                    if (lowerText(level->name) == lowerText(found->second.partnerName) && levelHeights[static_cast<std::size_t>(i)] < 68
                                        && sameWaterFamily(blendWorldName(level->category), blendWorldName(found->second.partnerCategory))) {
                                        other = i;
                                    }
                                }
                                const int sx = tx * 2 + (tx < nodeX * cell ? 1 : 0);
                                const int sy = ty * 2 + (ty < nodeY * cell ? 1 : 0);
                                const std::size_t heightIndex = static_cast<std::size_t>(sy) * static_cast<std::size_t>(map_->heightWidth()) + static_cast<std::size_t>(sx);
                                const int height = heightIndex < map_->heights.size() ? map_->heights[heightIndex] : levelHeights[static_cast<std::size_t>(matched)];
                                if (other < 0 && namedPartner && trailingNumber(found->second.partnerName) >= 0) {
                                    // A partner land's shore, whose liquid this brush cannot paint: the
                                    // land where the height is nearer it, otherwise read by height below.
                                    const int liquid = trailingNumber(found->second.partnerName);
                                    const int land = levelHeights[static_cast<std::size_t>(matched)];
                                    voted = std::abs(height - land) <= std::abs(height - liquid) ? matched : -1;
                                } else if (other < 0) {
                                    voted = matched;
                                } else {
                                    const int matchedDistance = std::abs(height - levelHeights[static_cast<std::size_t>(matched)]);
                                    const int otherDistance = std::abs(height - levelHeights[static_cast<std::size_t>(other)]);
                                    voted = otherDistance < matchedDistance ? other : matched;
                                }
                            }
                        }
                    }
                }
            }
            if (voted < 0) {
                const std::size_t index = static_cast<std::size_t>(ty * 2) * static_cast<std::size_t>(map_->heightWidth()) + static_cast<std::size_t>(tx * 2);
                const std::size_t cellIndex = static_cast<std::size_t>(ty) * static_cast<std::size_t>(map_->tileWidth) + static_cast<std::size_t>(tx);
                if (pictures && sharedLevelHeight && index < map_->heights.size() && cellIndex < map_->indices.size()) {
                    const std::size_t bytes = static_cast<std::size_t>(map_->indices[cellIndex]) * 1024;
                    if (bytes + 1024 <= map_->tileImages.size()) {
                        voted = levelByColour(map_->tileImages.data() + bytes, map_->heights[index]);
                    }
                }
                if (voted < 0 && index < map_->heights.size()) {
                    voted = nearestLevel(map_->heights[index], pictures);
                }
            }
            if (voted >= 0) {
                ++votes[static_cast<std::size_t>(voted)];
            }
        }
    }
    int winner = -1;
    int bestVotes = 0;
    for (std::size_t i = 0; i < votes.size(); ++i) {
        if (votes[i] > bestVotes) {
            bestVotes = votes[i];
            winner = static_cast<int>(i);
        }
    }
    if (winner >= 0) {
        return winner;
    }
    return 0;
}

void EditorApp::refreshBrushCorners() {
    if (!map_) {
        brushCornerLevels_.clear();
        brushCornerNames_.clear();
        brushCornerNodesX_ = 0;
        brushCornerNodesY_ = 0;
        brushCornerKey_ = 0;
        return;
    }
    ensureBrushPaints();
    if (catalogueBuilding_) {
        // No readings until the picture catalogue is built; brushCornerKey_ stays 0 so they are read then.
        brushCornerLevels_.clear();
        brushCornerNames_.clear();
        brushCornerNodesX_ = 0;
        brushCornerNodesY_ = 0;
        brushCornerKey_ = 0;
        return;
    }
    const std::uint64_t key = (mapRevision_ * 0x9E3779B97F4A7C15ull) ^ (mapSerial_ * 0xC2B2AE3D27D4EB4Full)
        ^ static_cast<std::uint64_t>(std::hash<std::string>{}(brushCatalogKey_)) ^ static_cast<std::uint64_t>(map_->tileWidth) << 40
        ^ static_cast<std::uint64_t>(map_->tileHeight) << 20;
    if (key == brushCornerKey_) {
        return;
    }
    brushCornerKey_ = key;
    brushCornerLevels_.clear();
    brushCornerNames_.clear();
    brushCornerNodesX_ = 0;
    brushCornerNodesY_ = 0;
    const std::vector<const BrushPaint*> levels = brushLevels();
    if (levels.empty()) {
        return;
    }
    const BrushCornerReading reading = brushCornerReading(levels);
    std::vector<std::string> worlds;
    for (const BrushPaint* paint : levels) {
        worlds.push_back(blendWorldName(paint->category));
        brushCornerNames_.push_back(worlds.back() + " " + std::to_string(trailingNumber(paint->name)));
    }
    // The same graph a stroke builds, since a blend fringe is read through it.
    const BrushGraph graph(reading.heights, worlds, false, blendWorldName(brushWorld_));
    const int cell = kBrushCellTiles;
    brushCornerNodesX_ = (map_->tileWidth + cell - 1) / cell + 1;
    brushCornerNodesY_ = (map_->tileHeight + cell - 1) / cell + 1;
    brushCornerLevels_.reserve(static_cast<std::size_t>(brushCornerNodesX_) * static_cast<std::size_t>(brushCornerNodesY_));
    for (int y = 0; y < brushCornerNodesY_; ++y) {
        for (int x = 0; x < brushCornerNodesX_; ++x) {
            brushCornerLevels_.push_back(readBrushCorner(reading, graph, x, y, cell));
        }
    }
}

EditorApp::BrushOutcome EditorApp::paintBaseBrush(int tileX, int tileY, int cellTiles, bool mayRefine, int footprintTiles) {
    if (catalogueBuilding_) {
        statusNote_ = "The Tile Brush is still building its picture catalogue";
        return BrushOutcome::Done;
    }
    const int cell = cellTiles;
    pictureBandSizes_.clear();
    pictureForeign_.clear();
    // A brush smaller than a cell sets one corner: 4 x 4 on the 8 tile cells, or 8 x 8 on the 16
    // tile cells when the tileset has no 8 x 8 pieces. Larger brushes fill whole cells, centered on the pointer.
    const bool cornerBrush = brushSize_ < cell;
    const int size = cornerBrush ? brushSize_ : std::max(cell, brushSize_ / cell * cell);
    const int blockCells = cornerBrush ? 0 : size / cell;
    const int cellsX = (map_->tileWidth + cell - 1) / cell;
    const int cellsY = (map_->tileHeight + cell - 1) / cell;
    int blockX = 0;
    int blockY = 0;
    if (!cornerBrush && footprintTiles > cell && size % footprintTiles == 0) {
        // Placed on the coarser grid, then counted in this stroke's cells.
        const int footprintX = (map_->tileWidth + footprintTiles - 1) / footprintTiles;
        const int footprintY = (map_->tileHeight + footprintTiles - 1) / footprintTiles;
        brushBlockOrigin(tileX, tileY, size, footprintX, footprintY, blockX, blockY, footprintTiles);
        blockX *= footprintTiles / cell;
        blockY *= footprintTiles / cell;
    } else if (!cornerBrush) {
        brushBlockOrigin(tileX, tileY, size, cellsX, cellsY, blockX, blockY, cell);
    }
    int nodeX = 0;
    int nodeY = 0;
    if (tileX < 0 || tileY < 0 || tileX >= map_->tileWidth || tileY >= map_->tileHeight
        || (cornerBrush && !brushCornerNode(tileX, tileY, cellsX, cellsY, nodeX, nodeY, cell))
        || (!cornerBrush && (blockX >= cellsX || blockY >= cellsY || blockX + blockCells <= 0 || blockY + blockCells <= 0))) {
        statusNote_ = "The brush is outside the map";
        return BrushOutcome::Done;
    }

    static std::mt19937 rng{std::random_device{}()};
    // category keeps Base-85 strokes of Mars, GreenWorld, and Acid apart. They share a name.
    const auto strokesFor = [&](const std::string& paint, int strokeSize, const std::string& category) {
        std::vector<BrushStroke*> matches;
        for (BrushStroke& stroke : brushStrokes_) {
            if (brushStrokeVariation(stroke.name, paint, strokeSize) < 0) {
                continue;
            }
            if (!category.empty() && !brushCategoryMatches(stroke.category, category)) {
                continue;
            }
            matches.push_back(&stroke);
        }
        return matches;
    };
    const auto piecesFor = [&](const std::string& category, std::string_view piece, int tiles) {
        std::vector<BrushPiece*> matches;
        for (BrushPiece& candidate : brushPieces_) {
            if (candidate.tiles != tiles) {
                continue;
            }
            const bool own = lowerText(candidate.category) == lowerText(category);
            // Blend pieces are shared, so Lava, Mars and GreenWorld can place them.
            // Two-way blends live in BlendTiles/Shared and three-way blends in BlendTiles/3-Way.
            const bool shared = lowerText(candidate.category) == "shared" || lowerText(candidate.category) == "3-way";
            if ((own || shared) && transitionVariation(candidate.name, piece) >= 0) {
                matches.push_back(&candidate);
            }
        }
        return matches;
    };
    const auto pickOne = [&](auto& matches) {
        std::uniform_int_distribution<int> pick(0, static_cast<int>(matches.size()) - 1);
        return matches[static_cast<std::size_t>(pick(rng))];
    };
    // The stroke for one flat cell at cell (cx, cy). An 8 x 8 cell cut from a 16 x 16 stroke takes the
    // quarter its place on the 16 tile grid calls for, from one stroke for the whole 16 x 16 cell, so the
    // ground continues across the 8 tile lines exactly as a 16 x 16 stroke lays it.
    const auto flatStrokeAt = [&](std::vector<BrushStroke*> strokes, int cx, int cy) -> BrushStroke* {
        if (strokes.empty()) {
            return nullptr;
        }
        if (cell != kFineCellTiles || strokes.front()->cropTiles == 0) {
            return pickOne(strokes);
        }
        const int quarterX = (cx & 1) * kFineCellTiles;
        const int quarterY = (cy & 1) * kFineCellTiles;
        std::vector<BrushStroke*> quarters;
        for (BrushStroke* stroke : strokes) {
            if (stroke->cropTiles > 0 && stroke->cropX == quarterX && stroke->cropY == quarterY) {
                quarters.push_back(stroke);
            }
        }
        if (quarters.empty()) {
            return pickOne(strokes);
        }
        const std::uint32_t parent = static_cast<std::uint32_t>(cx >> 1) * 73856093u ^ static_cast<std::uint32_t>(cy >> 1) * 19349663u;
        return quarters[parent % static_cast<std::uint32_t>(quarters.size())];
    };

    std::vector<BrushStroke*> blockStrokes;
    bool repeatCell = false;
    if (!cornerBrush) {
        blockStrokes = strokesFor(brushName_, size, brushWorld_);
        // A tileset with only a 16 x 16 base, such as Lava, repeats that cell across a larger brush.
        repeatCell = blockStrokes.empty() && size > cell;
        if (repeatCell) {
            blockStrokes = strokesFor(brushName_, cell, brushWorld_);
        }
        if (blockStrokes.empty()) {
            statusNote_ = "No " + brushName_ + " " + brushSizeLabel(repeatCell ? cell : size) + " brush stroke";
            return BrushOutcome::Done;
        }
    }

    // The paints, lowest to highest, are the levels. The graph says which pairs
    // have pieces, such as Coast for Base-0 and Base-85.
    const std::vector<const BrushPaint*> levels = brushLevels();
    std::vector<int> levelHeights;
    std::vector<std::string> levelWorlds;
    for (const BrushPaint* paint : levels) {
        levelHeights.push_back(trailingNumber(paint->name));
        levelWorlds.push_back(blendWorldName(paint->category));
    }
    const std::string ownWorld = blendWorldName(brushWorld_);
    const BrushGraph graph(levelHeights, levelWorlds, false, ownWorld);
    int armedLevel = -1;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (lowerText(levels[i]->name) == lowerText(brushName_) && brushCategoryMatches(levels[i]->category, brushWorld_)) {
            armedLevel = static_cast<int>(i);
        }
    }
    const bool transitions = armedLevel >= 0 && graph.connected(armedLevel);

    try {
        auto loadStamp = [&](std::optional<SectionStamp>& slot, const std::string& sectionPath, const std::string& label) {
            if (slot) {
                return;
            }
            const TilesetArchive* source = nullptr;
            for (const TilesetArchive& archive : tilesets_.archives()) {
                if (archive.path == brushTilesetPath_) {
                    source = &archive;
                    break;
                }
            }
            if (source == nullptr) {
                throw std::runtime_error("The random map tileset is not loaded.");
            }
            const HpiFile* file = source->archive().find(sectionPath);
            if (file == nullptr) {
                throw std::runtime_error(label + " is missing from the tileset.");
            }
            slot = loadSectionStamp(source->archive().extract(*file));
        };
        // A stroke cut from a quarter of a 16 x 16 one is cut once, when it is first loaded.
        auto loadStroke = [&](BrushStroke& stroke) {
            if (stroke.stamp) {
                return;
            }
            loadStamp(stroke.stamp, stroke.sectionPath, stroke.name);
            if (stroke.cropTiles > 0) {
                stroke.stamp = cropSectionStamp(*stroke.stamp, stroke.cropX, stroke.cropY, stroke.cropTiles, stroke.cropTiles);
            }
        };

        struct PlannedStamp {
            const SectionStamp* stamp = nullptr;
            int x = 0;
            int y = 0;
        };
        std::vector<PlannedStamp> planned;
        // 8 x 8 pieces whose flat ground was shifted onto the map's grid where they go.
        std::deque<SectionStamp> anchored;
        int transitionPieces = 0;
        // 16 x 16 cells beside the stroke redrawn as 8 x 8 so their band keeps one size.
        int matchedCells = 0;

        BrushStroke* chosen = nullptr;
        if (!cornerBrush) {
            chosen = flatStrokeAt(blockStrokes, blockX, blockY);
            loadStroke(*chosen);
            if (repeatCell) {
                for (int cy = std::max(0, blockY); cy < std::min(cellsY, blockY + blockCells); ++cy) {
                    for (int cx = std::max(0, blockX); cx < std::min(cellsX, blockX + blockCells); ++cx) {
                        BrushStroke* here = flatStrokeAt(blockStrokes, cx, cy);
                        loadStroke(*here);
                        planned.push_back(PlannedStamp{&*here->stamp, cx * cell, cy * cell});
                    }
                }
            } else {
                planned.push_back(PlannedStamp{&*chosen->stamp, blockX * cell, blockY * cell});
            }
        }

        if (transitions) {
            const BrushCornerReading cornerReading = brushCornerReading(levels);
            const auto readCorner = [&](int nodeX, int nodeY) { return readBrushCorner(cornerReading, graph, nodeX, nodeY, cell); };
            // On the 8 tile cells, a corner inside a 16 x 16 piece is one of that piece's own lands, and
            // never a cliff top unless a corner of the 16 tile grid beside it is one.
            const bool bandLists = !coarseBandTiles_.empty() && !fineBandTiles_.empty();
            const BrushPieceSizes pieceSizes = [&](int x, int y) { return brushCellPieceSizes(x, y); };
            const BrushNodeReader readNode = cell != kFineCellTiles ? BrushNodeReader(readCorner)
                : bandLists ? fineNodeReader(graph, coarseInteriorReader(levelHeights, readCorner, pieceSizes))
                : fineNodeReader(graph, readCorner);
            BrushPlan plan = cornerBrush
                ? planBrush(cellsX, cellsY, nodeX, nodeY, 0, armedLevel, graph, readNode)
                : planBrush(cellsX, cellsY, blockX, blockY, blockCells, armedLevel, graph, readNode);
            if (plan.unsettled) {
                statusNote_ = "The ground around here cannot settle for this stroke, so it was not painted. Try a smaller brush or paint the ground beside it first";
                return BrushOutcome::Done;
            }
            // Evergreen's water has no piece that meets both grasses at once, so where a stroke brings the water
            // and both grasses together the shore takes one grass, and that can run on round a shore nearby. A
            // stroke whose changes would reach past the cells around it is refused rather than recolouring a
            // shore the stroke never touched.
            if (isEvergreenWorld(brushWorld_)) {
                const int lowX = (cornerBrush ? nodeX : blockX) - 1;
                const int lowY = (cornerBrush ? nodeY : blockY) - 1;
                const int highX = (cornerBrush ? nodeX : blockX + blockCells) + 1;
                const int highY = (cornerBrush ? nodeY : blockY + blockCells) + 1;
                // Swapping one grass for the other away from the water is fine: a plateau painted beside the other
                // grass needs a ring of its own grass round its cliffs. Any other change that far out, such as a
                // plateau flattened or water spreading, or any change on a shore, refuses the stroke.
                const auto onShore = [&](int x, int y) {
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int level = std::clamp(readNode(x + dx, y + dy), 0, graph.size() - 1);
                            if (graph.height(level) < 77) {
                                return true;
                            }
                        }
                    }
                    return false;
                };
                for (const BrushNode& node : plan.nodes) {
                    const int before = std::clamp(readNode(node.x, node.y), 0, graph.size() - 1);
                    const bool grassSwap = graph.height(before) == 85 && graph.height(node.level) == 85
                        && evergreenWorld(graph.world(before)) && evergreenWorld(graph.world(node.level));
                    // Ground rising toward the painted paint, such as land growing a corner where Evergreen has no
                    // piece for the layout, reshapes nothing; ground cut down or a grass swapped on a shore does.
                    // Rings rising toward a high paint stamped on low ground, such as Base-85 and Base-190 round a
                    // Base-255 stamped on water, are rises too.
                    const bool raised = graph.height(node.level) > graph.height(before)
                        && graph.height(node.level) <= graph.height(armedLevel);
                    if (before != node.level && (node.x < lowX || node.x > highX || node.y < lowY || node.y > highY)
                        && !raised && (!grassSwap || onShore(node.x, node.y))) {
                        statusNote_ = "Painting here would reshape the ground nearby. No piece joins Evergreen's water to both grasses at "
                                      "once, and its two plateaus cannot touch. Leave a cell of grass between them";
                        return BrushOutcome::Done;
                    }
                }
            }
            // Water blends are for islands: a stroke that would move another land's coast, such as a Mars
            // Base-85 stroke over a GreenWorld shore, is refused, so the two coasts never meet.
            if (brushPlanMovesForeignCoast(plan, graph, ownWorld, cellsX, cellsY, readNode)) {
                statusNote_ = brushWorld_ + " can't be painted over another land's coast. Water blends are for islands.";
                return BrushOutcome::Done;
            }
            // Each band stays one size along its length, so no band jumps where 8 x 8 pieces meet 16 x 16 ones.
            if (cell == kFineCellTiles && bandLists) {
                matchedCells = matchBandSizes(plan, cellsX, cellsY, blockX, blockY, cornerBrush ? 0 : blockCells, readNode, pieceSizes);
            } else if (cell == kFineCellTiles) {
                // Without the lists, at least no part of a 16 x 16 piece is left beside the new 8 x 8 ones.
                redrawWholeCoarseCells(plan, cellsX, cellsY, blockX, blockY, cornerBrush ? 0 : blockCells, readNode);
            }
            if (cell == kFineCellTiles && bandLists && !fixedTiles_.empty()) {
                // An 8 x 8 stroke cannot redraw part of a piece that only comes in 16 x 16, such as a
                // cliff, without cutting its band, so it stays off them.
                const auto onFixed = [&](int cx, int cy) { return (brushCellPieceSizes(cx / 2, cy / 2) & kFixedPiece) != 0; };
                bool fixed = false;
                for (const BrushCell& planCell : plan.cells) {
                    fixed = fixed || onFixed(planCell.x, planCell.y);
                }
                for (int cy = blockY; !cornerBrush && !fixed && cy < blockY + blockCells; ++cy) {
                    for (int cx = blockX; cx < blockX + blockCells && !fixed; ++cx) {
                        fixed = cx >= 0 && cy >= 0 && cx < cellsX && cy < cellsY && onFixed(cx, cy);
                    }
                }
                if (fixed) {
                    coarseOnlyPiece_ = "The cliff, slope, ridge or river here";
                    return BrushOutcome::NeedsCoarse;
                }
            }
            if (cell == kBrushCellTiles && mayRefine && bandLists && brushCellTiles() == kBrushCellTiles
                && coarseStrokeMeetsFineBand(plan, cellsX, cellsY, blockX, blockY, cornerBrush ? 0 : blockCells, pieceSizes)) {
                bool haveFine = false;
                for (const BrushPiece& piece : brushPieces_) {
                    haveFine = haveFine || piece.tiles == kFineCellTiles;
                }
                if (haveFine) {
                    return BrushOutcome::NeedsFine;
                }
            }
            std::vector<std::string> missing;
            // An 8 tile stroke that needs a piece only the 16 x 16 set has, such as a cliff.
            bool coarseOnly = false;
            const auto noteMissing = [&](const std::string& what) {
                if (std::find(missing.begin(), missing.end(), what) == missing.end()) {
                    missing.push_back(what);
                }
            };
            std::string foreignGround;
            for (const BrushCell& ringCell : plan.cells) {
                // A cell holding ground this brush cannot join would be read wrongly by its height and
                // redrawn with pieces that do not fit it. Cells redrawn only to match are skipped.
                if (const std::string foreign = foreignGroundIn(ringCell.x * cell, ringCell.y * cell, cell, cell); !foreign.empty()) {
                    if (!ringCell.matched && foreignGround.empty()) {
                        foreignGround = foreign;
                    }
                    continue;
                }
                const BrushPaint* lower = levels[static_cast<std::size_t>(ringCell.level)];
                if (ringCell.mask == 0) {
                    // Flat ground: the cell is all one base again.
                    const std::vector<BrushStroke*> flat = strokesFor(lower->name, cell, lower->category);
                    if (flat.empty()) {
                        if (!ringCell.matched) {
                            noteMissing(lower->name + " " + brushSizeLabel(cell) + " brush stroke");
                        }
                        continue;
                    }
                    BrushStroke* stroke = flatStrokeAt(flat, ringCell.x, ringCell.y);
                    loadStroke(*stroke);
                    planned.push_back(PlannedStamp{&*stroke->stamp, ringCell.x * cell, ringCell.y * cell});
                    continue;
                }
                const BrushPaint* upper = levels[static_cast<std::size_t>(ringCell.upper)];
                // A cell holding Lava's Base-41 and Metal's Base-85 takes its coast from Lava-Metal.
                bool lavaLiquidCorner = false;
                bool metalGroundCorner = false;
                for (const int corner : ringCell.corners) {
                    const BrushPaint* paint = levels[static_cast<std::size_t>(corner)];
                    lavaLiquidCorner = lavaLiquidCorner || (lowerText(paint->category) == "lava" && trailingNumber(paint->name) == 41);
                    metalGroundCorner = metalGroundCorner || (lowerText(paint->category) == "metal" && trailingNumber(paint->name) == 85);
                }
                const bool lavaMetalCoast = lavaLiquidCorner && metalGroundCorner;
                std::string piece = brushPieceName(graph, ringCell.corners, lavaMetalCoast ? std::string_view("Water") : std::string_view(brushCoastLiquid_));
                if (piece.empty()) {
                    // A cell redrawn only to match its neighbours keeps its old picture when no piece fits it.
                    if (!ringCell.matched) {
                        noteMissing("a piece joining " + lower->name + " to " + upper->name);
                    }
                    continue;
                }
                // Evergreen's water has a coast for each grass, and its cliffs rise from one grass: the higher ground's
                // own folder holds those pieces, whichever Evergreen paint is armed.
                const bool evergreenCoast = isEvergreenWorld(lower->category) && isEvergreenWorld(upper->category)
                    && (graph.style(ringCell.level, ringCell.upper) == TransitionStyle::Coast
                        || graph.style(ringCell.level, ringCell.upper) == TransitionStyle::ShallowCoast
                        || graph.style(ringCell.level, ringCell.upper) == TransitionStyle::ShallowDeep
                        || graph.style(ringCell.level, ringCell.upper) == TransitionStyle::Cliff);
                // Cliffs from one Evergreen grass up to the other's plateau have a folder of their own, named for the
                // plateau's folder and then the low ground's: Evergreen2-Evergreen rises from Base-85-1 to Base-190-2.
                const bool evergreenCliff = evergreenCoast && graph.style(ringCell.level, ringCell.upper) == TransitionStyle::Cliff;
                // Cliffs up to Base-255 are filed apart from those up to Base-190, in Evergreen-190 and Evergreen2-190,
                // and the sea cliffs from Base-0 up to Base-190-2 in Evergreen2-0.
                const int evergreenLow = trailingNumber(lower->name);
                // Water, Base-85-2 and Base-190-2 together are filed apart from the same with Base-85-1, in Evergreen2-0-2.
                bool secondGrass = false;
                for (const int corner : ringCell.corners) {
                    secondGrass = secondGrass || (trailingNumber(levels[static_cast<std::size_t>(corner)]->name) == 85
                        && lowerText(levels[static_cast<std::size_t>(corner)]->category) == "evergreen2");
                }
                // Water with both grasses is filed in Evergreen-Mix.
                bool firstGrass = false;
                for (const int corner : ringCell.corners) {
                    firstGrass = firstGrass || (trailingNumber(levels[static_cast<std::size_t>(corner)]->name) == 85
                        && lowerText(levels[static_cast<std::size_t>(corner)]->category) == "evergreen");
                }
                const std::string evergreenFolder = !evergreenCoast ? std::string()
                    : !evergreenCliff && evergreenLow == 0 && firstGrass && secondGrass ? std::string("Evergreen-Mix")
                    : evergreenCliff && evergreenLow == 0 && secondGrass ? upper->category + "-0-2"
                    : evergreenCliff && (evergreenLow == 190 || evergreenLow == 0) ? upper->category + "-" + std::to_string(evergreenLow)
                    : !evergreenCliff || lowerText(lower->category) == lowerText(upper->category) ? upper->category
                    : upper->category + "-" + lower->category;
                // Lava's shore from Base-76 up to Base-86 is filed in Lava-76, apart from its Base-190 cliffs.
                const bool lavaShore = lowerText(lower->category) == "lava" && lowerText(upper->category) == "lava"
                    && trailingNumber(lower->name) == 76 && graph.style(ringCell.level, ringCell.upper) == TransitionStyle::Cliff;
                // SnowyEvergreen's cliffs from Base-189 up to Base-255 are filed in SnowyEvergreen-189.
                const bool snowPeak = lowerText(lower->category) == "snowyevergreen" && lowerText(upper->category) == "snowyevergreen"
                    && trailingNumber(lower->name) == 189 && graph.style(ringCell.level, ringCell.upper) == TransitionStyle::Cliff;
                // Any cell holding SnowyEvergreen's Base-65 shallows takes its piece from SnowyEvergreen-65.
                bool snowShallow = false;
                for (const int corner : ringCell.corners) {
                    const BrushPaint* paint = levels[static_cast<std::size_t>(corner)];
                    snowShallow = snowShallow || (lowerText(paint->category) == "snowyevergreen" && trailingNumber(paint->name) == 65);
                }
                // Any SnowyEvergreen cell holding both its water and its Base-189 takes its piece from SnowyEvergreen-0, and
                // one holding its frozen water and Base-189 from SnowyEvergreen-75.
                bool snowWater = false;
                bool snowFrozen = false;
                bool snowPlateau = false;
                for (const int corner : ringCell.corners) {
                    const BrushPaint* paint = levels[static_cast<std::size_t>(corner)];
                    snowWater = snowWater || (lowerText(paint->category) == "snowyevergreen" && trailingNumber(paint->name) == 0);
                    snowFrozen = snowFrozen || (lowerText(paint->category) == "snowyevergreen" && trailingNumber(paint->name) == 75);
                    snowPlateau = snowPlateau || (lowerText(paint->category) == "snowyevergreen" && trailingNumber(paint->name) == 189);
                }
                // Metal's two waters each have their own shallows and coasts into its Base-85: a cell holding Metal2's dark water
                // or dark shallows takes its piece from the Metal2 folders, and any other cell of Metal's from Metal's, whichever
                // paint is armed.
                const bool metalCell = isMetalWorld(lower->category) && isMetalWorld(upper->category);
                bool metal2Corner = false;
                for (const int corner : ringCell.corners) {
                    metal2Corner = metal2Corner || lowerText(levels[static_cast<std::size_t>(corner)]->category) == "metal2";
                }
                // A Metal cell holding both water and Base-139 is a sea cliff, filed in Metal-0.
                bool metalWater = false;
                bool metal139 = false;
                for (const int corner : ringCell.corners) {
                    const BrushPaint* paint = levels[static_cast<std::size_t>(corner)];
                    metalWater = metalWater || (isMetalWorld(paint->category) && trailingNumber(paint->name) == 0);
                    metal139 = metal139 || (isMetalWorld(paint->category) && trailingNumber(paint->name) == 139);
                }
                const std::string metalFolder = metalWater && metal139 ? kMetalSeaCliffFolder : metal2Corner ? "Metal2" : "Metal";
                // A cell holding WaterWorld's Base-1 sea floor takes its piece from WaterWorld-1.
                bool waterWorldFloor = false;
                for (const int corner : ringCell.corners) {
                    const BrushPaint* paint = levels[static_cast<std::size_t>(corner)];
                    waterWorldFloor = waterWorldFloor || (lowerText(paint->category) == "waterworld" && trailingNumber(paint->name) == 1);
                }
                const std::string& pieceCategory = evergreenCoast ? evergreenFolder
                    : waterWorldFloor ? kWaterWorldFloorFolder
                    : lavaMetalCoast ? kLavaMetalFolder
                    : metalCell ? metalFolder
                    : lavaShore ? kLavaShoreFolder
                    : snowWater && snowPlateau ? kSnowSeaCliffFolder
                    : snowFrozen && snowPlateau ? kSnowFrozenCliffFolder
                    : snowPeak ? kSnowPeakFolder
                    : snowShallow ? kSnowShallowFolder
                    : armedLevel >= 0 ? levels[static_cast<std::size_t>(armedLevel)]->category : lower->category;
                const std::vector<BrushPiece*> pieces = piecesFor(pieceCategory, piece, cell);
                if (pieces.empty() && ringCell.matched) {
                    continue;
                }
                if (pieces.empty()) {
                    if (cell != kBrushCellTiles && !piecesFor(pieceCategory, piece, kBrushCellTiles).empty()) {
                        // Only a 16 x 16 piece draws this, such as a cliff. A 16 x 16 piece among 8 x 8 ones
                        // would leave its band jumping at the cell's edge, so the stroke is not painted 8 x 8.
                        if (!coarseOnly) {
                            coarseOnlyPiece_ = piece;
                        }
                        coarseOnly = true;
                    }
                    noteMissing(piece);
                    continue;
                }
                BrushPiece* transition = pickOne(pieces);
                loadStamp(transition->stamp, transition->sectionPath, transition->name);
                const SectionStamp* stamp = &*transition->stamp;
                if (transition->tiles == kFineCellTiles) {
                    // Its flat ground lines up with the same ground around it.
                    std::vector<const PaletteImage*> grounds;
                    for (const int corner : ringCell.corners) {
                        const BrushPaint* land = levels[static_cast<std::size_t>(corner)];
                        const PaletteImage* ground = brushGround(land->category, land->name);
                        if (ground != nullptr && std::find(grounds.begin(), grounds.end(), ground) == grounds.end()) {
                            grounds.push_back(ground);
                        }
                    }
                    anchored.push_back(anchorPieceGround(*stamp, grounds, ringCell.x * cell, ringCell.y * cell));
                    stamp = &anchored.back();
                }
                planned.push_back(PlannedStamp{stamp, ringCell.x * cell, ringCell.y * cell});
                ++transitionPieces;
            }
            if (!foreignGround.empty()) {
                statusNote_ = foreignGround + " is in the way: " + brushWorld_ + " has no pieces that join it";
                error_.clear();
                return BrushOutcome::Done;
            }
            if (coarseOnly) {
                return BrushOutcome::NeedsCoarse;
            }
            if (!missing.empty()) {
                // The tileset is not finished for this paint yet, so the map is left alone.
                std::string note = "The tileset needs ";
                for (std::size_t i = 0; i < missing.size() && i < 3; ++i) {
                    note += (i == 0 ? "" : ", ") + missing[i];
                }
                if (missing.size() > 3) {
                    note += " and " + std::to_string(missing.size() - 3) + " more";
                }
                statusNote_ = note + " for " + brushName_;
                error_.clear();
                return BrushOutcome::Done;
            }
        }

        // Every stamp is loaded before the first edit, so a missing piece leaves the map alone.
        const int tileCountBefore = map_->tileCount;
        std::vector<PlacementChange> changes;
        for (const PlannedStamp& piece : planned) {
            auto change = stampSectionChange(*map_, *piece.stamp, piece.x, piece.y, &deduper_, 100);
            if (change) {
                changes.push_back(std::move(*change));
            }
        }
        if (changes.empty()) {
            statusNote_ = cornerBrush ? "The brush is already here" : "The brush is outside the map";
        } else {
            std::string note = cornerBrush
                ? "Placed " + std::to_string(static_cast<int>(changes.size())) + (changes.size() == 1 ? " corner tile" : " corner tiles")
                : "Placed " + chosen->name;
            if (!cornerBrush && transitionPieces > 0) {
                note += " with " + std::to_string(transitionPieces) + (transitionPieces == 1 ? " transition piece" : " transition pieces");
            }
            if (matchedCells > 0) {
                note += ", and redrew " + std::to_string(matchedCells) + (matchedCells == 1 ? " 16 x 16 cell" : " 16 x 16 cells")
                    + " of its band as 8 x 8 to match";
            }

            if (changes.size() == 1) {
                rememberPlacement(std::move(changes.front()));
            } else {
                rememberPlacements(std::move(changes));
            }
            statusNote_ = note;
            warnIfPastVanillaTiles(tileCountBefore);
        }
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
    return BrushOutcome::Done;
}

int EditorApp::hillSpan() const {
    if (armedBrushKind() != BrushKind::Hill) {
        return 0;
    }
    if (brushName_ == "Island 8x8") {
        for (const HillSection& hill : hillSections_) {
            if (hill.island && hill.tiles > 0) {
                return hill.tiles;
            }
        }
        return 16;
    }
    if (brushVaries()) {
        const int next = nextSeaIsland();
        return next >= 0 ? hillSections_[static_cast<std::size_t>(next)].tiles : 16;
    }
    if (const HillSection* group = groupHill()) {
        return group->tiles;
    }
    if (brushName_.find("32x32") != std::string::npos) {
        return 32;
    }
    if (brushName_ == "Feature-85" || brushName_ == "Pools-85") {
        return 16;
    }
    if (mountainArmed()) {
        return mountainSpan();
    }
    if (duneArmed()) {
        return 16;
    }
    return brushName_.find("16x16") != std::string::npos ? 16 : 8;
}

int EditorApp::hillSpanHigh() const {
    if (armedBrushKind() != BrushKind::Hill) {
        return 0;
    }
    if (brushVaries()) {
        const int next = nextSeaIsland();
        return next >= 0 ? hillSections_[static_cast<std::size_t>(next)].high() : hillSpan();
    }
    if (const HillSection* group = groupHill()) {
        return group->high();
    }
    return hillSpan();
}

void EditorApp::hillOrigin(int tileX, int tileY, int width, int height, int& originX, int& originY) const {
    if (seaIslandVaries(seaIslandKind(brushName_))) {
        originX = tileX - width / 2;
        originY = tileY - height / 2;
        return;
    }
    // Each side snaps on its own, so a 56 x 32 plateau lines up the same way a square does.
    // An odd number of cells, such as a 48 x 48 island: centered on the cell under the pointer.
    // Wider than a cell otherwise: centered on the cell corner nearest the pointer, the way a
    // 32 x 32 pool covers four cells. A cell or less: the cell, or the 8 x 8 quadrant, under it.
    const auto axis = [](int tile, int size) {
        const int cell = kBrushCellTiles;
        if (size > cell && size % cell == 0 && (size / cell) % 2 == 1) {
            return (tile >= 0 ? tile / cell : -1) * cell - (size / cell / 2) * cell;
        }
        if (size > cell) {
            return (tile + cell / 2) / cell * cell - size / 2;
        }
        int origin = tile >= 0 ? tile / cell * cell : 0;
        if (size > 0 && size < cell && tile >= 0 && tile % cell >= size) {
            origin += size;
        }
        return origin;
    };
    originX = axis(tileX, width);
    originY = axis(tileY, height);
}

const EditorApp::HillSection* EditorApp::groupHill() const {
    if (brushName_.empty()) {
        return nullptr;
    }
    for (const HillSection& hill : hillSections_) {
        if (hill.group == brushName_ && hill.tiles > 0) {
            return &hill;
        }
    }
    return nullptr;
}

bool EditorApp::brushVaries() const {
    if (seaIslandVaries(seaIslandKind(brushName_))) {
        return true;
    }
    const HillSection* first = groupHill();
    if (first == nullptr) {
        return false;
    }
    for (const HillSection& hill : hillSections_) {
        if (hill.group == brushName_ && hill.tiles > 0 && (hill.tiles != first->tiles || hill.high() != first->high())) {
            return true;
        }
    }
    return false;
}

bool EditorApp::nextMatches(const HillSection& hill) const {
    if (hill.tiles <= 0) {
        return false;
    }
    const int kind = seaIslandKind(brushName_);
    if (seaIslandVaries(kind)) {
        return hill.seaIsland == kind;
    }
    if (hill.group.empty() || hill.group != brushName_) {
        return false;
    }
    // Pools made for one grass are chosen only over that grass. Off the grass any of them may be shown.
    if (!hill.baseWorld.empty()) {
        const std::string ground = groundWorldUnderHover();
        return ground.empty() || lowerText(ground) == lowerText(hill.baseWorld);
    }
    return true;
}

std::string EditorApp::groundWorldUnderHover() const {
    std::vector<std::string> worlds;
    for (const HillSection& hill : hillSections_) {
        if (hill.group == brushName_ && !hill.baseWorld.empty()
            && std::find(worlds.begin(), worlds.end(), hill.baseWorld) == worlds.end()) {
            worlds.push_back(hill.baseWorld);
        }
    }
    const HillSection* group = groupHill();
    if (group == nullptr) {
        return {};
    }
    for (const std::string& world : worlds) {
        if (tileMatchesBase(hoverTileX_, hoverTileY_, world, group->baseName, group->baseHeight, group->baseFringe)) {
            return world;
        }
    }
    return {};
}

int EditorApp::nextSeaIsland() const {
    if (!brushVaries()) {
        return -1;
    }
    if (nextSeaIsland_ >= 0 && static_cast<std::size_t>(nextSeaIsland_) < hillSections_.size()
        && nextMatches(hillSections_[static_cast<std::size_t>(nextSeaIsland_)])) {
        return nextSeaIsland_;
    }
    std::vector<int> choices;
    for (std::size_t i = 0; i < hillSections_.size(); ++i) {
        if (nextMatches(hillSections_[i])) {
            choices.push_back(static_cast<int>(i));
        }
    }
    if (choices.empty()) {
        nextSeaIsland_ = -1;
        return -1;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, choices.size() - 1);
    nextSeaIsland_ = choices[pick(rng)];
    return nextSeaIsland_;
}

void EditorApp::skipSeaIsland() {
    if (!brushVaries()) {
        return;
    }
    const int current = nextSeaIsland();
    std::vector<int> choices;
    for (std::size_t i = 0; i < hillSections_.size(); ++i) {
        if (nextMatches(hillSections_[i]) && static_cast<int>(i) != current) {
            choices.push_back(static_cast<int>(i));
        }
    }
    if (choices.empty()) {
        return;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, choices.size() - 1);
    nextSeaIsland_ = choices[pick(rng)];
    const HillSection& next = hillSections_[static_cast<std::size_t>(nextSeaIsland_)];
    statusNote_ = "Next " + (next.noun.empty() ? std::string("island") : next.noun) + " " + next.name + ", " + brushRectLabel(next.tiles, next.high());
}

bool EditorApp::tileMatchesBase(int tileX, int tileY, std::string_view world, std::string_view baseName, int baseHeight, bool sameWorldFringe) const {
    // The pictures it reads are still being catalogued.
    if (catalogueBuilding_) {
        return false;
    }
    if (!map_ || tileX < 0 || tileY < 0 || tileX >= map_->tileWidth || tileY >= map_->tileHeight) {
        return false;
    }
    const std::size_t cellIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(map_->tileWidth) + static_cast<std::size_t>(tileX);
    if (cellIndex >= map_->indices.size()) {
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(map_->indices[cellIndex]) * 1024;
    if (bytes + 1024 > map_->tileImages.size()) {
        return false;
    }
    const std::uint64_t pictureHash = hashBrushTile(map_->tileImages.data() + bytes);
    const auto found = brushPictures_.find(pictureHash);
    if (found == brushPictures_.end()) {
        // A picture several bases share is this base when the base is one of them and the
        // ground is nearer this height than any other base that uses the picture.
        const auto shared = sharedPictures_.find(pictureHash);
        if (shared == sharedPictures_.end()) {
            return false;
        }
        bool ours = false;
        std::vector<int> otherHeights;
        for (const auto& [category, name] : shared->second) {
            if (brushCategoryMatches(category, world) && lowerText(name) == baseName) {
                ours = true;
            } else if (const int height = trailingNumber(name); height >= 0) {
                otherHeights.push_back(height);
            }
        }
        if (!ours) {
            return false;
        }
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                const std::size_t heightIndex = static_cast<std::size_t>(tileY * 2 + y) * static_cast<std::size_t>(map_->heightWidth())
                    + static_cast<std::size_t>(tileX * 2 + x);
                if (heightIndex >= map_->heights.size()) {
                    return false;
                }
                const int height = map_->heights[heightIndex];
                for (const int other : otherHeights) {
                    if (std::abs(height - baseHeight) >= std::abs(height - other)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    const BrushPictureHint& hint = found->second;
    const auto isFlat = [&](const std::string& category, const std::string& name) {
        return brushCategoryMatches(category, world) && lowerText(name) == baseName;
    };
    if (!hint.fringe) {
        return isFlat(hint.category, hint.name);
    }
    // Shared ground belongs to this base or another height of the same land. Another land's blend does not.
    if (!sameWorldFringe || !brushCategoryMatches(hint.category, world) || !brushCategoryMatches(hint.partnerCategory, world)) {
        return false;
    }
    const bool matchedFlat = isFlat(hint.category, hint.name);
    const bool partnerFlat = isFlat(hint.partnerCategory, hint.partnerName);
    if (matchedFlat == partnerFlat) {
        return false;
    }
    const int otherHeight = trailingNumber(matchedFlat ? hint.partnerName : hint.name);
    if (otherHeight < 0) {
        return false;
    }
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const std::size_t heightIndex = static_cast<std::size_t>(tileY * 2 + y) * static_cast<std::size_t>(map_->heightWidth())
                + static_cast<std::size_t>(tileX * 2 + x);
            if (heightIndex >= map_->heights.size()) {
                return false;
            }
            const int height = map_->heights[heightIndex];
            if (std::abs(height - baseHeight) >= std::abs(height - otherHeight)) {
                return false;
            }
        }
    }
    return true;
}

bool EditorApp::tileIsGreenBase85(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "GreenWorld", "base-85", 85, true);
}

bool EditorApp::tileIsGreenBase0(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "GreenWorld", "base-0", 0, false);
}

bool EditorApp::tileIsBrushWorldBase85(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, brushWorld_, "base-85", 85, true);
}

bool EditorApp::tileIsLavaBase86(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "Lava", "base-86", 86, true);
}

bool EditorApp::tileIsLushBase77(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "Lush", "base-77", 77, true);
}

bool EditorApp::tileIsArchipelagoBase194(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "Archipelago", "base-194", 194, true);
}

bool EditorApp::tileIsBrushWorldBase0(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, brushWorld_, "base-0", 0, false);
}

bool EditorApp::tileIsArchipelagoBase0(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "Archipelago", "base-0", 0, false);
}

bool EditorApp::tileIsArchipelagoBase78(int tileX, int tileY) const {
    return tileMatchesBase(tileX, tileY, "Archipelago", "base-78", 78, true);
}

bool EditorApp::duneCell(int cellX, int cellY) const {
    if (!map_ || cellX < 0 || cellY < 0 || dunePictures_.empty()) {
        return false;
    }
    const int cell = kBrushCellTiles;
    if ((cellX + 1) * cell > map_->tileWidth || (cellY + 1) * cell > map_->tileHeight) {
        return false;
    }
    int dune = 0;
    for (int y = cellY * cell; y < (cellY + 1) * cell; ++y) {
        for (int x = cellX * cell; x < (cellX + 1) * cell; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(map_->tileWidth) + static_cast<std::size_t>(x);
            const std::size_t bytes = static_cast<std::size_t>(map_->indices[index]) * 1024;
            if (bytes + 1024 <= map_->tileImages.size() && dunePictures_.count(hashBrushTile(map_->tileImages.data() + bytes)) != 0) {
                ++dune;
            }
        }
    }
    return dune * 2 >= cell * cell;
}

bool EditorApp::duneCanPlace(int cellX, int cellY, std::string& refusal) const {
    const int cell = kBrushCellTiles;
    if (!map_ || cellX < 0 || cellY < 0 || (cellX + 1) * cell > map_->tileWidth || (cellY + 1) * cell > map_->tileHeight) {
        refusal = "The brush is outside the map";
        return false;
    }
    if (duneCell(cellX, cellY)) {
        return true;
    }
    for (int y = cellY * cell; y < (cellY + 1) * cell; ++y) {
        for (int x = cellX * cell; x < (cellX + 1) * cell; ++x) {
            if (!tileMatchesBase(x, y, "Desert", "base-80", 80, true)) {
                refusal = "Dunes only go on a 16 x 16 Desert Base-80 tile";
                return false;
            }
        }
    }
    return true;
}

void EditorApp::paintDune(int tileX, int tileY) {
    if (!map_ || tileX < 0 || tileY < 0) {
        return;
    }
    // Load every piece once, and remember its pictures so dune cells can be found on the map.
    std::vector<HillSection*> pieces[6];
    for (HillSection& hill : hillSections_) {
        if (hill.dune <= 0 || hill.dune > 5) {
            continue;
        }
        if (!hill.stamp) {
            for (const TilesetArchive& archive : tilesets_.archives()) {
                if (archive.path != brushTilesetPath_) {
                    continue;
                }
                if (const HpiFile* file = archive.archive().find(hill.sectionPath)) {
                    hill.stamp = loadSectionStamp(archive.archive().extract(*file));
                }
                break;
            }
        }
        if (!hill.stamp || hill.stamp->tileWidth != 8 || hill.stamp->tileHeight != 8) {
            continue;
        }
        pieces[hill.dune].push_back(&hill);
        for (int tile = 0; tile < 64; ++tile) {
            const std::size_t bytes = static_cast<std::size_t>(hill.stamp->indices[static_cast<std::size_t>(tile)]) * 1024;
            if (bytes + 1024 <= hill.stamp->tileImages.size()) {
                dunePictures_.insert(hashBrushTile(hill.stamp->tileImages.data() + bytes));
            }
        }
    }
    for (int role = 1; role <= 5; ++role) {
        if (pieces[role].empty()) {
            statusNote_ = "The tileset needs every Dunes-80 piece";
            return;
        }
    }
    const int cell = kBrushCellTiles;
    const int cellX = tileX / cell;
    const int cellY = tileY / cell;
    std::string refusal;
    if (!duneCanPlace(cellX, cellY, refusal)) {
        statusNote_ = refusal;
        return;
    }
    // The painted cell counts as dune before its pictures change.
    const auto isDune = [&](int x, int y) { return (x == cellX && y == cellY) || duneCell(x, y); };
    try {
        const int tileCountBefore = map_->tileCount;
        std::vector<PlacementChange> changes;
        // Redraw the cell and every dune cell around it, since a new neighbour can turn their
        // corner quarters into solid dune.
        for (int y = cellY - 1; y <= cellY + 1; ++y) {
            for (int x = cellX - 1; x <= cellX + 1; ++x) {
                if (!isDune(x, y)) {
                    continue;
                }
                for (int quarter = 0; quarter < 4; ++quarter) {
                    const int right = quarter % 2;
                    const int bottom = quarter / 2;
                    const bool solid = isDune(right ? x + 1 : x - 1, y) && isDune(x, bottom ? y + 1 : y - 1);
                    const int role = solid ? 5 : 1 + quarter;
                    const int pieceX = x * cell + right * (cell / 2);
                    const int pieceY = y * cell + bottom * (cell / 2);
                    // The same quarter always gets the same variation, so redrawing a neighbour
                    // does not shuffle its rocks.
                    const std::vector<HillSection*>& choices = pieces[role];
                    const std::uint32_t mix = static_cast<std::uint32_t>(pieceX) * 73856093u ^ static_cast<std::uint32_t>(pieceY) * 19349663u;
                    const SectionStamp& stamp = *choices[mix % choices.size()]->stamp;
                    if (auto change = stampSectionChange(*map_, stamp, pieceX, pieceY, &deduper_, 100)) {
                        changes.push_back(std::move(*change));
                    }
                }
            }
        }
        if (changes.empty()) {
            statusNote_ = "The dune is already here";
            return;
        }
        rememberPlacements(std::move(changes));
        statusNote_ = "Painted Dunes-80";
        warnIfPastVanillaTiles(tileCountBefore);
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

bool EditorApp::hillCanPlace(int originX, int originY, int width, int height, std::string& refusal) const {
    if (!map_ || width <= 0 || height <= 0 || originX < 0 || originY < 0 || originX + width > map_->tileWidth || originY + height > map_->tileHeight) {
        refusal = "The brush is outside the map";
        return false;
    }
    if (duneArmed()) {
        return duneCanPlace(originX / kBrushCellTiles, originY / kBrushCellTiles, refusal);
    }
    // Only folder brushes have rectangles; every other hill is square.
    const int span = width;
    const HillSection* group = groupHill();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (group != nullptr) {
                // Evergreen's two grass buttons share one water, filed under Evergreen. A section made for another world's
                // base (an Evergreen pool for one grass) checks that base: the one the cursor is about to paint.
                std::string groundWorld = isEvergreenWorld(brushWorld_) ? std::string("Evergreen") : brushWorld_;
                if (const int next = nextSeaIsland(); next >= 0 && !hillSections_[static_cast<std::size_t>(next)].baseWorld.empty()) {
                    groundWorld = hillSections_[static_cast<std::size_t>(next)].baseWorld;
                } else if (!group->baseWorld.empty()) {
                    groundWorld = group->baseWorld;
                }
                if (!tileMatchesBase(originX + x, originY + y, groundWorld, group->baseName, group->baseHeight, group->baseHeight != 0 && group->baseFringe)) {
                    refusal = "The " + group->noun + " only goes on " + groundWorld + " Base-" + std::to_string(group->baseHeight)
                        + ", and the whole " + brushRectLabel(width, height) + " block has to be Base-" + std::to_string(group->baseHeight);
                    if (!group->baseWorld.empty()) {
                        // Evergreen's pools and hills: the grass buttons are captioned Base-85-1 and Base-85-2.
                        refusal = "The " + group->noun + " only goes on Base-85-1 or Base-85-2, and the whole " + brushRectLabel(width, height)
                            + " block has to be the same grass";
                    }
                    return false;
                }
                continue;
            }
            if (brushName_ == "Island 8x8") {
                if (!tileIsGreenBase0(originX + x, originY + y)) {
                    refusal = "Islands only go on a 16 x 16 GreenWorld Base-0 tile";
                    return false;
                }
                continue;
            }
            if (brushName_ == "Feature-85") {
                if (!tileIsBrushWorldBase85(originX + x, originY + y)) {
                    refusal = "Features only go on a 16 x 16 " + brushWorld_ + " Base-85 tile";
                    return false;
                }
                continue;
            }
            if (brushName_ == "Rough-85 16x16") {
                if (!tileIsBrushWorldBase85(originX + x, originY + y)) {
                    refusal = "Rough tiles only go on a 16 x 16 " + brushWorld_ + " Base-85 tile";
                    return false;
                }
                continue;
            }
            if (pool85Brush(brushName_)) {
                if (!tileIsBrushWorldBase85(originX + x, originY + y)) {
                    refusal = span >= kBrushCellTiles
                        ? "Pools only go on a 16 x 16 " + brushWorld_ + " Base-85 tile"
                        : "Pools only go on " + brushWorld_ + " Base-85";
                    return false;
                }
                continue;
            }
            if (brushName_.rfind("Hills-85", 0) == 0) {
                if (!tileIsBrushWorldBase85(originX + x, originY + y)) {
                    refusal = span >= kBrushCellTiles ? "Hills only go on a 16 x 16 " + brushWorld_ + " Base-85 tile" : "Hills only go on " + brushWorld_ + " Base-85";
                    return false;
                }
                continue;
            }
            if (brushName_.rfind("Pool-86", 0) == 0) {
                if (!tileIsLavaBase86(originX + x, originY + y)) {
                    refusal = "Lava pools only go on Lava Base-86, and the whole " + brushSizeLabel(span) + " block has to be Base-86";
                    return false;
                }
                continue;
            }
            if (brushName_.rfind("Hill-86", 0) == 0) {
                if (!tileIsLavaBase86(originX + x, originY + y)) {
                    refusal = span >= kBrushCellTiles
                        ? "Hills only go on a 16 x 16 Lava Base-86 tile"
                        : "Hills only go on Lava Base-86";
                    return false;
                }
                continue;
            }
            if (seaIslandKind(brushName_) != 0) {
                if (!tileIsBrushWorldBase0(originX + x, originY + y)) {
                    refusal = "Islands only go on " + brushWorld_ + " Base-0, and the whole " + brushSizeLabel(span) + " block has to be Base-0";
                    return false;
                }
                continue;
            }
            if (brushName_.rfind("Hills-78", 0) == 0) {
                if (!tileIsArchipelagoBase78(originX + x, originY + y)) {
                    refusal = span >= kBrushCellTiles
                        ? "Hills only go on a 16 x 16 Archipelago Base-78 tile"
                        : "Hills only go on Archipelago Base-78";
                    return false;
                }
                continue;
            }
            if (brushName_.rfind("Hill-194", 0) == 0) {
                if (!tileIsArchipelagoBase194(originX + x, originY + y)) {
                    refusal = "Hills only go on a 16 x 16 Archipelago Base-194 tile";
                    return false;
                }
                continue;
            }
            if (brushName_ == "Pools 8x8") {
                if (!tileIsLushBase77(originX + x, originY + y)) {
                    refusal = "Pools only go on Lush Base-77";
                    return false;
                }
                continue;
            }
            if (!tileIsGreenBase85(originX + x, originY + y)) {
                const bool rough = brushName_.rfind("Rough", 0) == 0;
                refusal = rough
                    ? "Rough tiles only go on GreenWorld Base-85"
                    : (span >= kBrushCellTiles
                        ? "Hills only go on a 16 x 16 GreenWorld Base-85 tile"
                        : "Hills only go on GreenWorld Base-85");
                return false;
            }
        }
    }
    return true;
}

void EditorApp::mountainFootprint(int tileX, int tileY, int& x, int& y, int& width, int& height) const {
    const bool vertical = mountainVertical();
    const int span = mountainSpan();
    if (mountainStroke_) {
        x = mountainX_;
        y = mountainY_;
        width = vertical ? span : mountainPieces_ * span;
        height = vertical ? mountainPieces_ * span : span;
        return;
    }
    const int cell = kBrushCellTiles;
    width = vertical ? span : span * 2;
    height = vertical ? span * 2 : span;
    x = (tileX + cell / 2) / cell * cell - width / 2;
    y = (tileY + cell / 2) / cell * cell - height / 2;
    // A reef is one 16 x 16 cell across, so its narrow side sits on the cell under the cursor.
    if (span == cell) {
        (vertical ? x : y) = (vertical ? tileX : tileY) / cell * cell;
    }
}

bool EditorApp::mountainCanPlace(int x, int y, int width, int height, std::string& refusal) const {
    const bool reef = reefArmed();
    if (!map_ || x < 0 || y < 0 || x + width > map_->tileWidth || y + height > map_->tileHeight) {
        refusal = reef ? "The reef is outside the map" : "The mountain is outside the map";
        return false;
    }
    for (int ty = y; ty < y + height; ++ty) {
        for (int tx = x; tx < x + width; ++tx) {
            if (reef ? !tileMatchesBase(tx, ty, "WaterWorld", "base-0", 0, false) : !tileIsLushBase77(tx, ty)) {
                refusal = brushName_ + (reef ? " only goes on WaterWorld Base-0" : " only goes on Lush Base-77");
                return false;
            }
        }
    }
    return true;
}

const SectionStamp* EditorApp::mountainPiece(std::string_view prefix) {
    std::vector<HillSection*> pieces;
    for (HillSection& hill : hillSections_) {
        if (hill.mountain && lowerText(hill.name).starts_with(lowerText(prefix))) {
            pieces.push_back(&hill);
        }
    }
    if (pieces.empty()) {
        return nullptr;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, pieces.size() - 1);
    HillSection* chosen = pieces[pick(rng)];
    if (!chosen->stamp) {
        for (const TilesetArchive& archive : tilesets_.archives()) {
            if (archive.path != brushTilesetPath_) {
                continue;
            }
            if (const HpiFile* file = archive.archive().find(chosen->sectionPath)) {
                chosen->stamp = loadSectionStamp(archive.archive().extract(*file));
            }
            break;
        }
    }
    if (!chosen->stamp || chosen->stamp->tileWidth != mountainSpan() || chosen->stamp->tileHeight != mountainSpan()) {
        return nullptr;
    }
    return &*chosen->stamp;
}

void EditorApp::startMountain(int tileX, int tileY) {
    mountainStroke_ = false;
    if (!map_ || brushTilesetPath_.empty()) {
        return;
    }
    const bool vertical = mountainVertical();
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    mountainFootprint(tileX, tileY, x, y, width, height);
    std::string refusal;
    if (!mountainCanPlace(x, y, width, height, refusal)) {
        statusNote_ = refusal;
        return;
    }
    try {
        // West and east ends for Horizontal Mtn, north and south ends for Vertical Mtn.
        const SectionStamp* first = mountainPiece(vertical ? "N_end" : "W_end");
        const SectionStamp* last = mountainPiece(vertical ? "S_end" : "E_end");
        if (first == nullptr || last == nullptr) {
            statusNote_ = "The tileset needs the " + brushName_ + " end pieces";
            return;
        }
        const int tileCountBefore = map_->tileCount;
        std::vector<PlacementChange> changes;
        if (auto change = stampSectionChange(*map_, *first, x, y, &deduper_, 100)) {
            changes.push_back(std::move(*change));
        }
        if (auto change = stampSectionChange(*map_, *last, vertical ? x : x + mountainSpan(), vertical ? y + mountainSpan() : y, &deduper_, 100)) {
            changes.push_back(std::move(*change));
        }
        if (changes.empty()) {
            statusNote_ = reefArmed() ? "The reef is already here" : "The mountain is already here";
            return;
        }
        rememberPlacements(std::move(changes));
        mountainX_ = x;
        mountainY_ = y;
        mountainPieces_ = 2;
        mountainStroke_ = true;
        statusNote_ = "Placed " + brushName_ + (vertical ? ". Drag up or down to make it longer" : ". Drag left or right to make it longer");
        warnIfPastVanillaTiles(tileCountBefore);
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::extendMountain(int tileX, int tileY) {
    if (!mountainStroke_ || !map_) {
        return;
    }
    // Positions along the mountain: x for Horizontal Mtn, y for Vertical Mtn.
    const bool vertical = mountainVertical();
    const int span = mountainSpan();
    const int pointer = vertical ? tileY : tileX;
    int& start = vertical ? mountainY_ : mountainX_;
    const int across = vertical ? mountainX_ : mountainY_;
    try {
        // A fast drag can pass several pieces in one frame, so keep adding until the pointer is covered.
        for (int added = 0; added < 64; ++added) {
            const int farEdge = start + mountainPieces_ * span;
            const bool forward = pointer >= farEdge;
            const bool backward = pointer < start;
            if (!forward && !backward) {
                return;
            }
            // The old end becomes a middle piece, and a new end goes one piece further out.
            const int newEnd = forward ? farEdge : start - span;
            const int oldEnd = forward ? farEdge - span : start;
            const int newX = vertical ? across : newEnd;
            const int newY = vertical ? newEnd : across;
            std::string refusal;
            if (!mountainCanPlace(newX, newY, span, span, refusal)) {
                statusNote_ = refusal;
                return;
            }
            const SectionStamp* middle = mountainPiece(vertical ? "NS" : "EW");
            const SectionStamp* end = mountainPiece(vertical ? (forward ? "S_end" : "N_end") : (forward ? "E_end" : "W_end"));
            if (middle == nullptr || end == nullptr) {
                statusNote_ = "The tileset needs the " + brushName_ + " middle pieces";
                return;
            }
            const int tileCountBefore = map_->tileCount;
            std::vector<PlacementChange> changes;
            if (auto change = stampSectionChange(*map_, *middle, vertical ? across : oldEnd, vertical ? oldEnd : across, &deduper_, 100)) {
                changes.push_back(std::move(*change));
            }
            if (auto change = stampSectionChange(*map_, *end, newX, newY, &deduper_, 100)) {
                changes.push_back(std::move(*change));
            }
            rememberPlacements(std::move(changes));
            if (backward) {
                start = newEnd;
            }
            ++mountainPieces_;
            statusNote_ = brushName_ + " is " + (vertical ? std::to_string(span) + " x " + std::to_string(mountainPieces_ * span)
                : std::to_string(mountainPieces_ * span) + " x " + std::to_string(span));
            warnIfPastVanillaTiles(tileCountBefore);
        }
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::paintHill(int tileX, int tileY) {
    if (!map_ || brushTilesetPath_.empty()) {
        return;
    }
    if (mountainArmed()) {
        startMountain(tileX, tileY);
        return;
    }
    if (duneArmed()) {
        paintDune(tileX, tileY);
        return;
    }
    const int span = hillSpan();
    const int spanHigh = hillSpanHigh();
    if (tileX < 0 || tileY < 0 || tileX >= map_->tileWidth || tileY >= map_->tileHeight) {
        statusNote_ = "The brush is outside the map";
        return;
    }
    int originX = 0;
    int originY = 0;
    hillOrigin(tileX, tileY, span, spanHigh, originX, originY);
    std::string refusal;
    if (!hillCanPlace(originX, originY, span, spanHigh, refusal)) {
        statusNote_ = refusal;
        return;
    }
    std::vector<HillSection*> matches;
    const bool island = brushName_ == "Island 8x8";
    const bool rough = brushName_.rfind("Rough", 0) == 0;
    const bool pool = brushName_ == "Pools 8x8";
    const bool base78 = brushName_.rfind("Hills-78", 0) == 0;
    const int seaIsland = seaIslandKind(brushName_);
    const bool lavaPool = brushName_.rfind("Pool-86", 0) == 0;
    const bool hill85 = brushName_.rfind("Hills-85", 0) == 0;
    const bool pool85 = pool85Brush(brushName_);
    const bool marsRough = brushName_ == "Rough-85 16x16";
    const bool marsFeature = brushName_ == "Feature-85";
    if (brushVaries()) {
        // Island-0 Medium and Large, and Crystal's Rough-85, place the tile the cursor is already showing.
        if (const int next = nextSeaIsland(); next >= 0) {
            matches.push_back(&hillSections_[static_cast<std::size_t>(next)]);
        }
    } else if (groupHill() != nullptr) {
        for (HillSection& hill : hillSections_) {
            if (hill.group == brushName_ && hill.tiles == span && hill.high() == spanHigh) {
                matches.push_back(&hill);
            }
        }
    } else {
        for (HillSection& hill : hillSections_) {
            if (hill.tiles == span && hill.crystalFolder.empty() && hill.group.empty() && hill.island == island && hill.rough == rough && hill.pool == pool && hill.base78 == base78
                && hill.seaIsland == seaIsland && hill.lavaPool == lavaPool && hill.hill85 == hill85
                && hill.pool85 == pool85 && hill.marsRough == marsRough
                && hill.marsFeature == marsFeature) {
                matches.push_back(&hill);
            }
        }
    }
    if (matches.empty()) {
        statusNote_ = "The tileset needs " + brushName_;
        return;
    }
    static std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> pick(0, static_cast<int>(matches.size()) - 1);
    HillSection* chosen = matches[static_cast<std::size_t>(pick(rng))];
    try {
        if (!chosen->stamp) {
            const TilesetArchive* source = nullptr;
            for (const TilesetArchive& archive : tilesets_.archives()) {
                if (archive.path == brushTilesetPath_) {
                    source = &archive;
                    break;
                }
            }
            if (source == nullptr) {
                throw std::runtime_error("The random map tileset is not loaded.");
            }
            const HpiFile* file = source->archive().find(chosen->sectionPath);
            if (file == nullptr) {
                throw std::runtime_error(chosen->name + " is missing from the tileset.");
            }
            chosen->stamp = loadSectionStamp(source->archive().extract(*file));
        }
        if (chosen->stamp->tileWidth != span || chosen->stamp->tileHeight != spanHigh) {
            statusNote_ = chosen->name + " is not " + brushSizeLabel(span);
            return;
        }
        const int tileCountBefore = map_->tileCount;
        auto change = stampSectionChange(*map_, *chosen->stamp, originX, originY, &deduper_, 100);
        if (!change) {
            statusNote_ = island || seaIsland != 0 ? "The island is already here" : (pool || lavaPool || pool85 ? "The pool is already here" : (rough ? "The rough patch is already here" : "The hill is already here"));
            return;
        }
        rememberPlacement(std::move(*change));
        statusNote_ = "Placed " + chosen->name;
        if (brushVaries()) {
            // Choose the next tile now, so the cursor shows its size.
            nextSeaIsland_ = -1;
            nextSeaIsland();
        }
        warnIfPastVanillaTiles(tileCountBefore);
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

BrushKind EditorApp::armedBrushKind() const {
    for (const BrushPaint& paint : brushPaints_) {
        if (paint.name == brushName_) {
            return paint.kind;
        }
    }
    return BrushKind::Base;
}

bool EditorApp::lineBrushArmed() const {
    return armedBrushKind() != BrushKind::Base;
}

// The brush tells cells apart by their pictures. This reads the base strokes, the coast
// pieces and the river pieces once, and keeps only their signatures.
std::string EditorApp::evergreenRiverFolder() const {
    if (!isEvergreenWorld(brushWorld_)) {
        return {};
    }
    if (brushName_ == "River 85-1" || brushName_ == "River Bridge 85-1") {
        return "Evergreen";
    }
    if (brushName_ == "River 85-2" || brushName_ == "River Bridge 85-2") {
        return "Evergreen2";
    }
    return {};
}

bool EditorApp::riverSectionUsable(const RiverSection& section) const {
    const std::string folder = evergreenRiverFolder();
    if (folder.empty() || !lowerText(section.key).starts_with("river")) {
        return true;
    }
    return lowerText(section.category) == lowerText(folder);
}

void EditorApp::ensureRiverCatalog() {
    // Evergreen's two river buttons read the map differently: the other grass, its rivers and its coasts are not theirs.
    if (riverCatalogReady_ && evergreenRiverFolder() != riverCatalogFolder_) {
        riverCatalogReady_ = false;
    }
    if (riverCatalogReady_ || brushTilesetPath_.empty()) {
        return;
    }
    const std::string riverFolder = evergreenRiverFolder();
    riverCatalogFolder_ = riverFolder;
    // A label for a piece that belongs to the other Evergreen grass, which the armed river button must not use.
    const auto otherGrass = [&](std::string_view category) {
        return !riverFolder.empty() && isEvergreenWorld(category) && lowerText(category) != lowerText(riverFolder);
    };
    const TilesetArchive* source = nullptr;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (archive.path == brushTilesetPath_) {
            source = &archive;
            break;
        }
    }
    if (source == nullptr) {
        return;
    }
    riverCatalog_.clear();
    const auto addSection = [&](const std::string& archivePath, const std::string& label) {
        try {
            const HpiFile* file = source->archive().find(archivePath);
            if (file != nullptr) {
                riverCatalog_.add(loadSectionStamp(source->archive().extract(*file)), label);
            }
        } catch (const std::exception&) {
        }
    };
    for (const RiverSection& river : riverSections_) {
        const std::string key = lowerText(river.key);
        if (key == "pass-n" || key == "pass-s" || key == "pass-mid") {
            try {
                const HpiFile* file = source->archive().find(river.sectionPath);
                if (file == nullptr) {
                    continue;
                }
                const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
                for (int cy = 0; cy < 2; ++cy) {
                    for (int cx = 0; cx < 2; ++cx) {
                        std::string label = "Pass-Body";
                        if (key == "pass-n" && cx == 0 && cy == 0) {
                            label = "Pass-Mouth-N";
                        } else if (key == "pass-s" && cx == 1 && cy == 1) {
                            label = "Pass-Mouth-S";
                        }
                        riverCatalog_.addAt(stamp, cx * kRiverCellTiles, cy * kRiverCellTiles, label);
                    }
                }
            } catch (const std::exception&) {
            }
            continue;
        }
        // The other Evergreen grass's rivers are not this button's: they read as foreign pieces it neither joins nor rebuilds.
        if (lowerText(river.key).starts_with("river") && otherGrass(river.category)) {
            addSection(river.sectionPath, "Other-" + river.name);
            continue;
        }
        addSection(river.sectionPath, river.name);
    }
    // SnowyEvergreen's rivers are frozen: they start on the shores of its Base-75 frozen water, the ShallowCoast pieces
    // in its own folder, read here as coasts. Its open water's coasts and its Base-65 shallows' coasts take no river.
    const bool frozenRivers = lowerText(brushWorld_) == "snowyevergreen";
    for (const BrushPiece& piece : brushPieces_) {
        const std::string lowered = lowerText(piece.name);
        if (frozenRivers && lowered.starts_with("shallowcoast-") && lowerText(piece.category) == "snowyevergreen") {
            addSection(piece.sectionPath, piece.name.substr(std::string_view("Shallow").size()));
        } else if (lowered.starts_with("cliff-") && lowerText(piece.category) == lowerText(kMetalSeaCliffFolder)) {
            // Metal's sea cliffs share their plain names with its Base-85 to Base-219 cliffs. Read as Cliff-Ramp-, only
            // the Ramp button turns them, and the Slope 85 to 219 button leaves them alone.
            addSection(piece.sectionPath, "Cliff-Ramp-" + piece.name.substr(std::string_view("Cliff-").size()));
        } else if (frozenRivers && lowered.starts_with("cliff-") && lowerText(piece.category) != "snowyevergreen") {
            // SnowyEvergreen's other cliffs share their plain names with the Base-85 to Base-189 cliffs. Its Base-189 to
            // Base-255 cliffs read as Cliff-Snow255-, which only the Slope 255 button turns; the ones up from its water and
            // frozen water read as Cliff-Other-, which no slope button turns.
            const bool peak = lowerText(piece.category) == lowerText(kSnowPeakFolder);
            addSection(piece.sectionPath, std::string(peak ? "Cliff-Snow255-" : "Cliff-Other-") + piece.name.substr(std::string_view("Cliff-").size()));
        } else if (isEvergreenWorld(brushWorld_) && lowered.starts_with("cliff-") && lowerText(piece.category) != lowerText(kEvergreenRampCliffFolder)) {
            // Evergreen's other cliffs share their plain names with the Base-85-1 to Base-190-2 cliffs. Its Base-190-1 to
            // Base-255-1 cliffs read as Cliff-Ever255-, which only the Slope 190-1 to 255-1 button turns; the rest read as
            // Cliff-Other-, which no ramp button turns.
            // Its Base-85-2 to Base-190-2 cliffs read as Cliff-EG2-, which only the Slope 85-2 to 190-2 button turns.
            const bool peak = lowerText(piece.category) == lowerText(kEvergreenPeakCliffFolder);
            // Its Base-190-2 to Base-255-2 cliffs read as Cliff-Stone255-, for the Slope 190-2 to 255-2 button.
            const bool second = lowerText(piece.category) == lowerText(kEvergreen2RampCliffFolder);
            const bool stone = lowerText(piece.category) == lowerText(kEvergreen2PeakCliffFolder);
            // Its Base-85-1 to Base-190-1 cliffs read as Cliff-Ever190-, for the Slope 85-1 to 190-1 button.
            const bool plateau = lowerText(piece.category) == lowerText(kEvergreenPlateauCliffFolder);
            addSection(piece.sectionPath, std::string(peak ? "Cliff-Ever255-" : second ? "Cliff-EG2-" : stone ? "Cliff-Stone255-" : plateau ? "Cliff-Ever190-" : "Cliff-Other-")
                    + piece.name.substr(std::string_view("Cliff-").size()));
        } else if (lowered.starts_with("coast-") && !riverFolder.empty()
            && (otherGrass(piece.category) || lowerText(piece.category).starts_with("evergreen-mix"))) {
            // An Evergreen river starts only on its own grass's shore, not on the other grass's or a shore holding both.
            addSection(piece.sectionPath, "Other-" + piece.name);
        } else if ((lowered.starts_with("coast-") && !frozenRivers) || lowered.starts_with("cliff-")) {
            addSection(piece.sectionPath, piece.name);
        }
    }
    for (const BrushStroke& stroke : brushStrokes_) {
        for (const BrushPaint& paint : brushPaints_) {
            if (paint.kind != BrushKind::Base) {
                continue;
            }
            bool matched = false;
            for (const int size : {16, 32, 64, 128, 256, 512}) {
                if (brushStrokeVariation(stroke.name, paint.name, size) >= 0) {
                    matched = true;
                    break;
                }
            }
            if (matched) {
                // Metal2's dark water is Base-0-2 here, so Metal's Bridge 139 only crosses the dark water.
                const bool darkWater = lowerText(stroke.category) == "metal2" && trailingNumber(paint.name) == 0;
                // An Evergreen river button reads the other grass as ground it may not cross.
                const bool otherLand = otherGrass(stroke.category) && trailingNumber(paint.name) == 85;
                addSection(stroke.sectionPath, darkWater ? std::string(kMetalBridgeWater)
                        : otherLand ? std::string(lowerText(stroke.category) == "evergreen2" ? "Base-85-2 grass" : "Base-85-1 grass")
                        : paint.name);
                break;
            }
        }
    }
    riverCatalogReady_ = true;
}

RiverPlan EditorApp::riverPlanAt(int cellX, int cellY, BrushKind kind) {
    RiverPlan none;
    if (!map_) {
        none.reason = "There is no map";
        return none;
    }
    ensureRiverCatalog();
    const RiverLookup lookup = [&](int cx, int cy) { return riverCatalog_.classify(*map_, cx, cy); };
    const auto available = [&](std::string_view key) {
        for (const RiverSection& section : riverSections_) {
            if (sameRiverKey(section.key, key) && riverSectionUsable(section)) {
                return true;
            }
        }
        return false;
    };
    switch (kind) {
    case BrushKind::River: return planRiver(cellX, cellY, lookup, available);
    case BrushKind::Bridge:
        // Metal's Bridge 139 crosses its dark water from the high coast. The other lands' bridges sit on rivers.
        if (brushName_ == "Bridge 139") {
            return planMetalBridge(cellX, cellY, lookup);
        }
        return planRiverBridge(cellX, cellY, lookup, available);
    case BrushKind::Ridge:
        // Metal's walls: Wall 85 on Base-85, starting from its Base-219 cliffs, and Wall 139 on Base-139.
        if (brushName_ == "Wall 85") {
            return planMetalLine(metalWall85Rules(), cellX, cellY, lookup);
        }
        if (brushName_ == "Wall 139") {
            return planMetalLine(metalWall139Rules(), cellX, cellY, lookup);
        }
        return planRidge(cellX, cellY, lookup);
    case BrushKind::Spacer: return planRidgeSpacer(cellX, cellY, lookup);
    case BrushKind::Slope: {
        // The plan only asks for slope pieces this world actually has. Mars keeps
        // straight singles on neighbouring 16 x 16 cells and refuses corners.
        // Metal's second slope button is the four straight Base-139 to Base-255 ramps.
        int family = 0;
        if (brushName_ == "Slope 139 to 255") {
            family = 255;
        } else if (brushName_ == "Slope 78 to 194") {
            family = 202;
        } else if (brushName_ == "Slope 80 to 155") {
            family = 155;
        } else if (brushName_ == "Slope 155 to 230") {
            family = 230;
        } else if (brushName_ == "Slope 77 to 183") {
            family = 183;
        } else if (brushName_ == "Ramp") {
            family = kSlopeRampFamily;
        } else if (brushName_ == "Slope 189") {
            family = kSlopeSnowFamily;
        } else if (brushName_ == "Slope 255") {
            family = kSlopeSnowPeakFamily;
        } else if (brushName_ == kEvergreenRampButton) {
            family = kSlopeEvergreenFamily;
        } else if (brushName_ == kEvergreenPeakRampButton) {
            family = kSlopeEvergreenPeakFamily;
        } else if (brushName_ == kEvergreen2RampButton) {
            family = kSlopeEvergreen2Family;
        } else if (brushName_ == kEvergreen2PeakRampButton) {
            family = kSlopeEvergreen2PeakFamily;
        } else if (brushName_ == kEvergreenPlateauRampButton) {
            family = kSlopeEvergreenPlateauFamily;
        }
        return planSlope(cellX, cellY, lookup, available, family);
    }
    case BrushKind::Hill: break;
    case BrushKind::Base: break;
    }
    none.reason = "This brush does not use river rules";
    return none;
}

void EditorApp::paintRiver(int tileX, int tileY, BrushKind kind) {
    if (!map_ || brushTilesetPath_.empty()) {
        return;
    }
    const int cell = kBrushCellTiles;
    if (tileX < 0 || tileY < 0 || tileX >= map_->tileWidth || tileY >= map_->tileHeight) {
        statusNote_ = "The brush is outside the map";
        return;
    }
    const RiverPlan plan = riverPlanAt(tileX / cell, tileY / cell, kind);
    if (!plan.allowed()) {
        // Nothing is painted. The tooltip over the map says the same thing.
        statusNote_ = plan.reason.substr(0, plan.reason.find('\n'));
        return;
    }

    static std::mt19937 rng{std::random_device{}()};
    try {
        const TilesetArchive* source = nullptr;
        for (const TilesetArchive& archive : tilesets_.archives()) {
            if (archive.path == brushTilesetPath_) {
                source = &archive;
                break;
            }
        }
        if (source == nullptr) {
            throw std::runtime_error("The random map tileset is not loaded.");
        }

        struct PlannedRiver {
            const SectionStamp* stamp = nullptr;
            int x = 0;
            int y = 0;
        };
        std::vector<PlannedRiver> planned;
        std::string firstName;
        for (const RiverEdit& edit : plan.edits) {
            std::vector<RiverSection*> matches;
            for (RiverSection& section : riverSections_) {
                if (sameRiverKey(section.key, edit.key) && riverSectionUsable(section)) {
                    matches.push_back(&section);
                }
            }
            if (matches.empty()) {
                statusNote_ = "The tileset needs " + edit.key + " for " + brushName_;
                error_.clear();
                return;
            }
            std::uniform_int_distribution<int> pick(0, static_cast<int>(matches.size()) - 1);
            RiverSection* chosen = matches[static_cast<std::size_t>(pick(rng))];
            if (!chosen->stamp) {
                const HpiFile* file = source->archive().find(chosen->sectionPath);
                if (file == nullptr) {
                    throw std::runtime_error(chosen->name + " is missing from the tileset.");
                }
                chosen->stamp = loadSectionStamp(source->archive().extract(*file));
            }
            if (firstName.empty()) {
                firstName = chosen->name;
            }
            planned.push_back(PlannedRiver{&*chosen->stamp, edit.x * cell, edit.y * cell});
        }

        const int tileCountBefore = map_->tileCount;
        std::vector<PlacementChange> changes;
        for (const PlannedRiver& piece : planned) {
            auto change = stampSectionChange(*map_, *piece.stamp, piece.x, piece.y, &deduper_, 100);
            if (change) {
                changes.push_back(std::move(*change));
            }
        }
        if (changes.empty()) {
            statusNote_ = "The brush is outside the map";
        } else {
            std::string note = "Placed " + firstName;
            bool mountainPass = false;
            for (const RiverEdit& edit : plan.edits) {
                if (edit.key.rfind("Pass-", 0) == 0) {
                    mountainPass = true;
                }
            }
            if (mountainPass) {
                note = "Cut a river through the mountains";
            } else if (plan.edits.size() > 1) {
                const std::size_t others = plan.edits.size() - 1;
                note += " and redrew " + std::to_string(others) + (others == 1 ? " neighbor" : " neighbors");
            }
            if (changes.size() == 1) {
                rememberPlacement(std::move(changes.front()));
            } else {
                rememberPlacements(std::move(changes));
            }
            statusNote_ = note;
            warnIfPastVanillaTiles(tileCountBefore);
        }
        error_.clear();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::finishCatalogueBuild(bool wait) {
    if (!catalogueBuilding_ || !catalogueWork_.valid()) {
        return;
    }
    if (!wait && catalogueWork_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    try {
        catalogueWork_.get();
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
    const bool cancelled = catalogueCancel_ && catalogueCancel_->load();
    catalogueCancel_.reset();
    catalogueDone_.reset();
    catalogueBuilding_ = false;
    // Corner readings taken before the catalogue existed are thrown away.
    brushCornerKey_ = 0;
    if (cancelled) {
        // Half a catalogue: the next ensureBrushPaints reads the tileset again.
        brushCatalogKey_.clear();
    }
}

float EditorApp::catalogueProgress() const {
    if (!catalogueBuilding_ || !catalogueDone_ || catalogueTotal_ <= 0) {
        return 0.0f;
    }
    const float done = static_cast<float>(catalogueDone_->load(std::memory_order_relaxed));
    const float total = static_cast<float>(catalogueTotal_);
    // Past 95% of the expected count, as when a first build's guess is short, the ring keeps creeping toward 99%
    // instead of sitting full while the work goes on.
    if (done <= total * 0.95f) {
        return done / total;
    }
    return 0.95f + 0.04f * (1.0f - std::exp(-(done - total * 0.95f) / total));
}

void EditorApp::stopCatalogueBuild() {
    if (catalogueCancel_) {
        catalogueCancel_->store(true);
    }
    finishCatalogueBuild(true);
}

void EditorApp::ensureBrushPaints() {
    finishCatalogueBuild(false);
    const TilesetArchive* source = nullptr;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        if (isRandomMapTileset(archive.path)) {
            source = &archive;
            break;
        }
    }
    std::string key = "missing";
    if (source != nullptr) {
        // Evergreen's two grasses are one catalogue: its folders hold the same buttons, pieces and partners, so
        // moving the brush from a Base-0 button to a Base-255-2 one does not read the tileset again.
        key = source->path.string() + "\n" + std::to_string(source->sections.size()) + "\n"
            + brushTilesetOf(brushWorld_)
            + (brushCatalogStampsOnly_ ? "\nstamps" : "");
    } else if (loadingTilesets()) {
        key = "loading";
    }
    if (key == brushCatalogKey_) {
        return;
    }
    if (catalogueBuilding_) {
        // Another tileset was chosen while this one's catalogue builds. Its state stays until the worker unwinds,
        // a moment later, and the new tileset is read on the frame after that.
        if (catalogueCancel_) {
            catalogueCancel_->store(true);
        }
        return;
    }
    for (BrushPaint& paint : brushPaints_) {
        destroyTexture(paint.preview);
    }
    brushPaints_.clear();
    brushPartners_.clear();
    brushPictures_.clear();
    sharedPictures_.clear();
    brushStrokes_.clear();
    brushPieces_.clear();
    riverSections_.clear();
    hillSections_.clear();
    dunePictures_.clear();
    riverCatalog_.clear();
    riverCatalogReady_ = false;
    brushTilesetPath_.clear();
    coarseBandTiles_.clear();
    fineBandTiles_.clear();
    fixedTiles_.clear();
    foreignPictures_.clear();
    foreignNames_.clear();
    brushGrounds_.clear();
    brushBaseTiles_.clear();
    brushCoastLiquid_ = "Water";
    brushCatalogKey_ = key;
    if (source == nullptr) {
        if (!loadingTilesets()) {
            brushName_.clear();
        }
        return;
    }
    brushTilesetPath_ = source->path;
    // The picture catalogue, when it has to be built, runs on a worker started at the end of this function.
    std::function<void()> catalogueJob;

    const bool blendWorld = lowerText(brushWorld_) == "lava" || lowerText(brushWorld_) == "mars" || lowerText(brushWorld_) == "greenworld"
        || lowerText(brushWorld_) == "archipelago" || lowerText(brushWorld_) == "desert" || lowerText(brushWorld_) == "acid"
        || lowerText(brushWorld_) == "lush" || lowerText(brushWorld_) == "ice" || lowerText(brushWorld_) == "crystal"
        || isMetalWorld(brushWorld_) || lowerText(brushWorld_) == "quagmire" || isEvergreenWorld(brushWorld_)
        || lowerText(brushWorld_) == "snowyevergreen" || lowerText(brushWorld_) == "waterworld";
    std::vector<const TilesetSection*> chosen;
    for (const TilesetSection& section : source->sections) {
        std::string sectionKind;
        const int sectionTiles = brushPieceFolderTiles(section.world, sectionKind);
        const bool blend = blendWorld && lowerText(sectionKind) == "blendtiles";
        const bool greenHill = lowerText(brushWorld_) == "greenworld" && lowerText(section.world) == "greenworld"
            && lowerText(section.category) == "hills";
        const bool greenIsland = lowerText(brushWorld_) == "greenworld" && lowerText(section.world) == "greenworld"
            && lowerText(section.category) == "island";
        const bool lavaHill = lowerText(brushWorld_) == "lava" && lowerText(section.world) == "lava"
            && lowerText(section.category) == "hill_";
        const bool lavaPoolSection = lowerText(brushWorld_) == "lava" && lowerText(section.world) == "lava"
            && lowerText(section.category) == "pools";
        // Lava.hpi isle L-isle01 to L-isle10, 48 x 48 islands in the lava sea. Their copies in
        // the favorites have every height under 41 raised to 41, so the lava sits on Base-41.
        const bool lavaIsleSection = lowerText(brushWorld_) == "lava" && lowerText(section.world) == "lava"
            && lowerText(section.category) == "isle";
        const bool marsHillSection = lowerText(brushWorld_) == "mars" && lowerText(section.world) == "mars"
            && lowerText(section.category) == "hills";
        const bool marsPoolSection = lowerText(brushWorld_) == "mars" && lowerText(section.world) == "mars"
            && lowerText(section.category) == "pools";
        const bool marsRoughSection = lowerText(brushWorld_) == "mars" && lowerText(section.world) == "mars"
            && lowerText(section.category) == "rough";
        const bool marsFeatureSection = lowerText(brushWorld_) == "mars" && lowerText(section.world) == "mars"
            && lowerText(section.category) == "feature";
        const bool marsIslandSection = (lowerText(brushWorld_) == "mars" && lowerText(section.world) == "mars"
            && lowerText(section.category) == "islands")
            || (lowerText(brushWorld_) == "ice" && lowerText(section.world) == "ice" && lowerText(section.category) == "islands");
        const bool iceHillSection = lowerText(brushWorld_) == "ice" && lowerText(section.world) == "ice"
            && lowerText(section.category) == "hills";
        const std::string crystalCategory = lowerText(section.category);
        const bool crystalSection = lowerText(brushWorld_) == "crystal" && lowerText(section.world) == "crystal"
            && (crystalCategory == "craters" || crystalCategory == "hills" || crystalCategory == "islands"
                || crystalCategory == "ponds" || crystalCategory == "reefs" || crystalCategory == "roughs");
        const bool icePoolSection = lowerText(brushWorld_) == "ice" && lowerText(section.world) == "ice"
            && lowerText(section.category) == "pools";
        const bool lushPool = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "256s";
        const bool lushMountain = (lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "mountains")
            // WaterWorld's reefs, from its H & V Sections, grow the way Lush's mountains do.
            || (lowerText(brushWorld_) == "waterworld" && lowerText(section.world) == "waterworld" && lowerText(section.category) == "reefs");
        const bool lushIsland = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "islands";
        // WaterWorld-Greenworld.hpi Floor Sections D01_85 to D06_85, the sea floor's decorations, filed as
        // WaterWorld/Base1Misc. Each is one 16 x 16 stamp on Base-1, heights as they are.
        const bool waterWorldFloorMisc = lowerText(brushWorld_) == "waterworld" && lowerText(section.world) == "waterworld"
            && lowerText(section.category) == "base1misc";
        // WaterWorld-Greenworld.hpi Extras Arm01_85 to Arm15_85, arms of land rising from WaterWorld Base-0 water, heights
        // as they are. ArmsMedium holds the 48 x 48 and 64 x 48 ones, ArmsLarge the 64 x 64 ones.
        const bool waterWorldArmMedium = lowerText(brushWorld_) == "waterworld" && lowerText(section.world) == "waterworld"
            && lowerText(section.category) == "armsmedium";
        const bool waterWorldArmLarge = lowerText(brushWorld_) == "waterworld" && lowerText(section.world) == "waterworld"
            && lowerText(section.category) == "armslarge";
        // Evergreen-UTAS-2.hpi Islands, ringed by Evergreen's Base-0 water, heights as they are. Islands holds the ones
        // smaller than 24 x 24 (up to 24 x 20 and 20 x 24), IslandsLarge the 24 x 24 and bigger ones.
        const bool evergreenIsland = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "islands";
        const bool evergreenIslandLarge = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "islandslarge";
        // Evergreen-UTAS-2.hpi Pools, lowered by 1 to sit on Base-85: Pools1 holds the ones on Base-85-1 grass, Pools2 the
        // ones on Base-85-2 grass. One Pool button paints both.
        const bool evergreenPool1 = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "pools1";
        const bool evergreenPool2 = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "pools2";
        // Evergreen-UTAS-1.hpi Small Hills (not the EG2 ones), lowered by 1: Hills1 as drawn on Base-85-1 grass, Hills2 the same
        // hills repainted with Base-85-2 grass. One Hill button paints both, by the grass under the cursor.
        const bool evergreenHill1 = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "hills1";
        const bool evergreenHill2 = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "hills2";
        // Evergreen-UTAS-1.hpi Large hills, the same way: HillsLarge1 as drawn, HillsLarge2 repainted with Base-85-2 grass.
        const bool evergreenHillLarge1 = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "hillslarge1";
        const bool evergreenHillLarge2 = isEvergreenWorld(brushWorld_) && lowerText(section.world) == "evergreen"
            && lowerText(section.category) == "hillslarge2";
        const bool lushHill = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "hills";
        const bool lushSwamp = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "swamps";
        const bool lushPond = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "ponds";
        const bool lushHill16 = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "hills16";
        const bool lushFeature255 = lowerText(brushWorld_) == "lush" && lowerText(section.world) == "lush"
            && lowerText(section.category) == "features255";
        const bool desertHugeHill = lowerText(brushWorld_) == "desert" && lowerText(section.world) == "desert"
            && lowerText(section.category) == "hugehills";
        const bool desertHill16 = lowerText(brushWorld_) == "desert" && lowerText(section.world) == "desert"
            && lowerText(section.category) == "hills16";
        const bool desertPlateau = lowerText(brushWorld_) == "desert" && lowerText(section.world) == "desert"
            && lowerText(section.category) == "plateaus";
        const bool desertRough = lowerText(brushWorld_) == "desert" && lowerText(section.world) == "desert"
            && lowerText(section.category) == "roughs";
        const bool desertDune = lowerText(brushWorld_) == "desert" && lowerText(section.world) == "desert"
            && lowerText(section.category) == "dunes";
        // Acid.hpi folders, one button each, with no height offset: the button, what the
        // tooltips call one piece, and the base it sits on.
        struct AcidFolder {
            const char* category;
            const char* group;
            const char* noun;
            const char* baseName;
            int baseHeight;
        };
        static constexpr AcidFolder kAcidFolders[] = {
            {"pools", "Pools-85", "pool", "base-85", 85},
            {"flats", "Acid-0 16x16", "acid patch", "base-0", 0},
            {"hills", "Hills-85 16x16", "hill", "base-85", 85},
            {"mounds", "Mounds-85 16x16", "mound", "base-85", 85},
            {"puddles", "Puddles-85 16x16", "puddle", "base-85", 85},
            {"trails", "Trails-85 16x16", "trail", "base-85", 85},
            {"islands16", "Islands-0 16x16", "island", "base-0", 0},
            {"islands32", "Islands-0 32x32", "island", "base-0", 0},
        };
        const AcidFolder* acidFolder = nullptr;
        if (lowerText(brushWorld_) == "acid" && lowerText(section.world) == "acid") {
            for (const AcidFolder& folder : kAcidFolders) {
                if (lowerText(section.category) == folder.category) {
                    acidFolder = &folder;
                }
            }
        }
        const bool archipelagoHill = lowerText(brushWorld_) == "archipelago" && lowerText(section.world) == "archipelago"
            && lowerText(section.category) == "hills_203";
        const bool archipelagoLowHill = lowerText(brushWorld_) == "archipelago" && lowerText(section.world) == "archipelago"
            && lowerText(section.category) == "hills";
        const bool archipelagoIsland = lowerText(brushWorld_) == "archipelago" && lowerText(section.world) == "archipelago"
            && lowerText(section.category) == "islands";
        // Both Evergreen grasses are buttons whichever of them is armed, and both grasses' coasts into their
        // shared water are loaded, since either grass can meet that water.
        // Evergreen2-Evergreen holds the cliffs from the first grass up to the second grass's plateau.
        const bool evergreenBase = (isEvergreenWorld(brushWorld_) && lowerText(section.category).starts_with("evergreen"))
            || (isMetalWorld(brushWorld_) && (isMetalWorld(section.category) || lowerText(section.category) == lowerText(kMetalSeaCliffFolder)));
        // Lava's shore from Base-76 up to Base-86 has a folder of its own.
        const bool lavaShore = (lowerText(brushWorld_) == "lava" && lowerText(section.category) == "lava-76")
            || ((lowerText(brushWorld_) == "lava" || isMetalWorld(brushWorld_)) && lowerText(section.category) == lowerText(kLavaMetalFolder))
            || (lowerText(brushWorld_) == "waterworld" && lowerText(section.category) == lowerText(kWaterWorldFloorFolder));
        // So do SnowyEvergreen's cliffs from Base-189 up to Base-255, its Base-65 shallows' pieces, its sea cliffs and its frozen water's cliffs.
        const bool snowPeak = lowerText(brushWorld_) == "snowyevergreen"
            && (lowerText(section.category) == lowerText(kSnowPeakFolder) || lowerText(section.category) == lowerText(kSnowShallowFolder)
                || lowerText(section.category) == lowerText(kSnowSeaCliffFolder) || lowerText(section.category) == lowerText(kSnowFrozenCliffFolder));
        if (!brushCategoryMatches(section.category, brushWorld_) && !evergreenBase && !lavaShore && !snowPeak && !blend && !greenHill && !greenIsland && !lavaHill && !lushPool
            && !archipelagoHill && !archipelagoLowHill && !archipelagoIsland && !lavaPoolSection && !lavaIsleSection
            && !marsHillSection && !marsPoolSection && !marsRoughSection
            && !marsFeatureSection && !marsIslandSection && !iceHillSection && !icePoolSection
            && !crystalSection && !lushMountain && !lushIsland && !lushHill && !lushSwamp
            && !lushPond && !lushHill16 && !lushFeature255 && !desertHugeHill
            && !desertHill16 && !desertPlateau && !desertRough && !desertDune
            && !waterWorldFloorMisc && !waterWorldArmMedium && !waterWorldArmLarge && !evergreenIsland && !evergreenIslandLarge
            && !evergreenPool1 && !evergreenPool2 && !evergreenHill1 && !evergreenHill2
            && !evergreenHillLarge1 && !evergreenHillLarge2 && acidFolder == nullptr) {
            continue;
        }
        if (isBaseBrushTile(section.world, section.name)) {
            chosen.push_back(&section);
        } else if (isBrushStrokeWorld(section.world)) {
            BrushStroke stroke;
            stroke.name = section.name;
            stroke.category = section.category;
            stroke.sectionPath = section.archivePath;
            brushStrokes_.push_back(std::move(stroke));
        } else if (isRiverWorld(section.world)) {
            RiverSection river;
            river.name = section.name;
            river.key = riverKeyOf(section.name);
            river.category = section.category;
            river.sectionPath = section.archivePath;
            riverSections_.push_back(std::move(river));
        } else if (isTransitionWorld(section.world) && !(sectionTiles == kFineCellTiles && !kFineCoastsEnabled && brushCoastFolder(sectionKind))) {
            // 8 x 8 coast pieces are skipped while kFineCoastsEnabled is off.
            BrushPiece cliff;
            cliff.name = section.name;
            cliff.category = section.category;
            cliff.sectionPath = section.archivePath;
            cliff.tiles = sectionTiles;
            brushPieces_.push_back(std::move(cliff));
        }
        if (greenHill) {
            int number = 0;
            if (greenHillNumber(section.name, number) && (listedGreenHillSpan(number) != 0 || listedGreenRough(number))) {
                HillSection hill;
                hill.name = section.name;
                hill.sectionPath = section.archivePath;
                hill.rough = listedGreenRough(number);
                hillSections_.push_back(std::move(hill));
            }
        }
        if (greenIsland) {
            int number = 0;
            if (greenIslandNumber(section.name, number)) {
                HillSection hill;
                hill.name = section.name;
                hill.sectionPath = section.archivePath;
                hill.island = true;
                hillSections_.push_back(std::move(hill));
            }
        }
        if (crystalSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.crystalFolder = section.category;
            hillSections_.push_back(std::move(hill));
        }
        if (iceHillSection || icePoolSection) {
            // Ice.hpi Hills and Pools sit on Ice Base-85, like the Mars ones.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.hill85 = iceHillSection;
            hill.pool85 = icePoolSection;
            hillSections_.push_back(std::move(hill));
        }
        if (marsIslandSection) {
            // Mars-2.hpi Islands. The button follows the size, read below.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.seaIsland = -1;
            hillSections_.push_back(std::move(hill));
        }
        if (marsFeatureSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.marsFeature = true;
            hillSections_.push_back(std::move(hill));
        }
        if (marsRoughSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.rough = true;
            hill.marsRough = true;
            hillSections_.push_back(std::move(hill));
        }
        if (marsPoolSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.pool85 = true;
            hillSections_.push_back(std::move(hill));
        }
        if (marsHillSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.hill85 = true;
            hillSections_.push_back(std::move(hill));
        }
        if (lavaIsleSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = "Island 48x48";
            hill.noun = "island";
            hill.baseName = "base-41";
            hill.baseHeight = 41;
            hillSections_.push_back(std::move(hill));
        }
        if (lavaPoolSection) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.lavaPool = true;
            hillSections_.push_back(std::move(hill));
        }
        if (archipelagoIsland) {
            // Archipelago.hpi Islands. Which button each one belongs to is known once its size is read.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.seaIsland = -1;
            hillSections_.push_back(std::move(hill));
        }
        if (archipelagoLowHill) {
            // Archipelago.hpi Hills, lowered by 8 so their rims sit on Base-78.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.base78 = true;
            hillSections_.push_back(std::move(hill));
        }
        if (archipelagoHill) {
            // Hills_203 from Archipelago_WithGreenTransition, lowered by 8 so their rims sit on Base-194.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hillSections_.push_back(std::move(hill));
        }
        if (lavaHill) {
            int number = 0;
            if (lavaHillNumber(section.name, number)) {
                HillSection hill;
                hill.name = section.name;
                hill.sectionPath = section.archivePath;
                hillSections_.push_back(std::move(hill));
            }
        }
        if (acidFolder != nullptr) {
            // Pools mixes 16 x 16 and 32 x 32, so its cursor takes the size of the next pool.
            // Flats (AcidFlats), Hills, Mounds, Puddles, and Trails are all 16 x 16. UneatenIslands
            // is split by size into Islands16 and Islands32, one button each.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = acidFolder->group;
            hill.noun = acidFolder->noun;
            hill.baseName = acidFolder->baseName;
            hill.baseHeight = acidFolder->baseHeight;
            hillSections_.push_back(std::move(hill));
        }
        if (desertDune) {
            // Desert-Archipelago-Lava.hpi Dunes01 to Dunes11, raised by 30. Each is one quarter
            // of a 16 x 16 dune cell; the number says which quarter it is.
            const int number = trailingNumber(section.name);
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.dune = number == 1 || number == 5 ? 1
                : number == 2 || number == 6 ? 2
                : number == 4 || number == 8 ? 3
                : number == 3 || number == 7 ? 4
                : number >= 9 && number <= 11 ? 5 : 0;
            if (hill.dune != 0) {
                hillSections_.push_back(std::move(hill));
            }
        }
        if (desertHugeHill || desertHill16 || desertPlateau || desertRough) {
            // Desert-Archipelago-Lava.hpi pieces, raised by 30 so they sit on Base-80.
            // Huge-Hill-80: Hills01 (64 x 64) and Hills02 to Hills05 (40 x 40 and 32 x 32).
            // Hill-80 16x16: Hills06 to Hills15. Plateaus-80: Plateaus01 to Plateaus05 and
            // Mesas01 and Mesas02, some of them rectangles. Roughs-80 8x8: Roughs01 to Roughs10.
            // A button whose pieces differ in size shows the size of the next one.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = desertHugeHill ? "Huge-Hill-80"
                : (desertHill16 ? "Hill-80 16x16" : (desertPlateau ? "Plateaus-80" : "Roughs-80 8x8"));
            hill.noun = desertHugeHill ? "huge hill" : (desertHill16 ? "hill" : (desertPlateau ? "plateau" : "rough patch"));
            hill.baseName = "base-80";
            hill.baseHeight = 80;
            hillSections_.push_back(std::move(hill));
        }
        if (lushFeature255) {
            // Lush.hpi Mod Mts Flats01 to Flats06, raised by 27 so they sit on the Base-255 mountaintop.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = "Feature-255";
            hill.noun = "mountain feature";
            hill.baseName = "base-255";
            hill.baseHeight = 255;
            hillSections_.push_back(std::move(hill));
        }
        if (lushHill || lushSwamp || lushPond || lushHill16) {
            // Lush.hpi tiles raised by 27 so they sit on Base-77. From 256s, the 8 x 8 hills and
            // swamps; from 512s, the 16 x 16 ponds and hills.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = lushHill ? "Hill-77 8x8" : (lushSwamp ? "Swamp-77" : (lushPond ? "Pond-77 16x16" : "Hill-77 16x16"));
            hill.noun = lushHill || lushHill16 ? "hill" : (lushSwamp ? "swamp" : "pond");
            hill.baseName = "base-77";
            hill.baseHeight = 77;
            hillSections_.push_back(std::move(hill));
        }
        if (waterWorldFloorMisc) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = "Base-1 Misc 16x16";
            hill.noun = "sea floor decoration";
            hill.baseName = "base-1";
            hill.baseHeight = 1;
            // Base-1 and Base-0 are a few heights apart, so a tile half floor and half water is not Base-1.
            hill.baseFringe = false;
            hillSections_.push_back(std::move(hill));
        }
        if (waterWorldArmMedium || waterWorldArmLarge) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = waterWorldArmMedium ? "Arm-85 Medium" : "Arm-85 Large";
            hill.noun = "arm";
            hill.baseName = "base-0";
            hill.baseHeight = 0;
            hillSections_.push_back(std::move(hill));
        }
        if (evergreenIsland || evergreenIslandLarge) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = evergreenIsland ? "Island" : "Island Large";
            hill.noun = "island";
            hill.baseName = "base-0";
            hill.baseHeight = 0;
            hillSections_.push_back(std::move(hill));
        }
        if (evergreenPool1 || evergreenPool2) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = "Pool";
            hill.noun = "pool";
            hill.baseName = "base-85";
            hill.baseHeight = 85;
            hill.baseWorld = evergreenPool1 ? "Evergreen" : "Evergreen2";
            hillSections_.push_back(std::move(hill));
        }
        if (evergreenHill1 || evergreenHill2 || evergreenHillLarge1 || evergreenHillLarge2) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = evergreenHill1 || evergreenHill2 ? "Hill" : "Hill Large";
            hill.noun = "hill";
            hill.baseName = "base-85";
            hill.baseHeight = 85;
            hill.baseWorld = evergreenHill1 || evergreenHillLarge1 ? "Evergreen" : "Evergreen2";
            hillSections_.push_back(std::move(hill));
        }
        if (lushIsland) {
            // Lush.hpi Islands, raised by 27 so their water sits on Base-27.
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.group = "Island-27 16x16";
            hill.noun = "island";
            hill.baseName = "base-27";
            hill.baseHeight = 27;
            hillSections_.push_back(std::move(hill));
        }
        if (lushMountain) {
            HillSection hill;
            hill.name = section.name;
            hill.sectionPath = section.archivePath;
            hill.mountain = true;
            hillSections_.push_back(std::move(hill));
        }
        if (lushPool) {
            int number = 0;
            if (lushPoolNumber(section.name, number)) {
                HillSection hill;
                hill.name = section.name;
                hill.sectionPath = section.archivePath;
                hill.pool = true;
                hillSections_.push_back(std::move(hill));
            }
        }
    }
    std::sort(chosen.begin(), chosen.end(), [](const TilesetSection* left, const TilesetSection* right) {
        const int leftNumber = trailingNumber(left->name);
        const int rightNumber = trailingNumber(right->name);
        if (leftNumber != rightNumber) {
            return leftNumber < rightNumber;
        }
        return nameLess(left->name, right->name);
    });
    for (const TilesetSection* section : chosen) {
        try {
            const HpiFile* file = source->archive().find(section->archivePath);
            if (file == nullptr) {
                continue;
            }
            const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
            int width = 0;
            int height = 0;
            // Full size: the Base button shows the middle of the ground at its own scale.
            const std::vector<std::uint8_t> rgba = renderSectionRgba(stamp, 512, width, height);
            BrushPaint paint;
            paint.name = section->name;
            paint.category = section->category;
            paint.previewWidth = width;
            paint.previewHeight = height;
            // Evergreen's grounds are numbered by grass: Base-85-1 and Base-190-1 are the first grass's.
            if (isEvergreenWorld(section->category) && trailingNumber(section->name) >= 77) {
                paint.label = section->name + (lowerText(section->category) == "evergreen2" ? "-2" : "-1");
            }
            // Metal's blue water and blue shallows are its first water: Base-0-1 and Base-68-1. Its second water and
            // that water's shallows, filed as Metal2, are Base-0-2 and Base-68-2.
            if (lowerText(section->category) == "metal" && (trailingNumber(section->name) == 0 || trailingNumber(section->name) == 68)) {
                paint.label = section->name + "-1";
            } else if (lowerText(section->category) == "metal2") {
                paint.label = section->name + "-2";
            }
            paint.preview = uploadTexture(rgba.data(), width, height, false);
            if (paint.preview == 0) {
                continue;
            }
            brushPaints_.push_back(std::move(paint));
            // Lava bases have no brush-stroke variations, so the base section itself is the 16 x 16 stroke.
            if (stamp.tileWidth == kBrushCellTiles && stamp.tileHeight == kBrushCellTiles) {
                bool hasCell = false;
                for (const BrushStroke& stroke : brushStrokes_) {
                    if (brushStrokeVariation(stroke.name, section->name, kBrushCellTiles) >= 0
                        && brushCategoryMatches(stroke.category, section->category)) {
                        hasCell = true;
                        break;
                    }
                }
                if (!hasCell) {
                    BrushStroke stroke;
                    stroke.name = section->name + "-" + std::to_string(kBrushCellTiles) + "x" + std::to_string(kBrushCellTiles) + "-01";
                    stroke.category = section->category;
                    stroke.sectionPath = section->archivePath;
                    brushStrokes_.push_back(std::move(stroke));
                }
            }
        } catch (const std::exception&) {
        }
    }
    // The blend partners and the pictures that tell each tile's land apart are for painting
    // by hand. The random map generator only reads a world's hill and pool buttons, so it
    // skips them: they take seconds for each world.
    if (!brushCatalogStampsOnly_) {
    // Lava Base-86 blends into Mars, GreenWorld, Acid, Crystal, Ice, and Metal at Base-85, into Desert
    // at Base-80, into Archipelago at Base-78, and into Lush at Base-77. GreenWorld
    // Base-85 also blends into Mars, Metal, Acid, Crystal, Ice, Lush, and Desert.
    // Archipelago Base-78 also blends into GreenWorld, Lush, Acid, Crystal, Ice,
    // Mars, Metal, Desert, and Lava. Acid Base-85 blends into Crystal, Desert, GreenWorld,
    // Archipelago, Mars, Ice, Metal, and Lush. Crystal Base-85 also blends into
    // Desert, Ice, Mars, Metal, GreenWorld, and Lush. Ice Base-85 blends
    // into Mars, GreenWorld, Lush, Desert, Acid, Crystal, and Metal.
    // A partner is a paint the graph can see, and a stroke it can place, without
    // becoming a button.
    struct PartnerWant {
        const char* world;
        const char* name;
    };
    std::vector<PartnerWant> wanted;
    if (lowerText(brushWorld_) == "lava") {
        wanted.push_back(PartnerWant{"Mars", "Base-85"});
        wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
        wanted.push_back(PartnerWant{"Acid", "Base-85"});
        wanted.push_back(PartnerWant{"Desert", "Base-80"});
        wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
        wanted.push_back(PartnerWant{"Lush", "Base-77"});
        wanted.push_back(PartnerWant{"Crystal", "Base-85"});
        wanted.push_back(PartnerWant{"Ice", "Base-85"});
        wanted.push_back(PartnerWant{"Metal", "Base-85"});
    } else if (lowerText(brushWorld_) == "mars" || lowerText(brushWorld_) == "greenworld") {
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        if (lowerText(brushWorld_) == "greenworld") {
            wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
            wanted.push_back(PartnerWant{"Acid", "Base-85"});
            wanted.push_back(PartnerWant{"Ice", "Base-85"});
            wanted.push_back(PartnerWant{"Mars", "Base-85"});
            wanted.push_back(PartnerWant{"Metal", "Base-85"});
            wanted.push_back(PartnerWant{"Lush", "Base-77"});
            wanted.push_back(PartnerWant{"Desert", "Base-80"});
            wanted.push_back(PartnerWant{"Crystal", "Base-85"});
            // GreenWorld water fades into Mars water, so a GreenWorld island can sit in a Mars sea.
            wanted.push_back(PartnerWant{"Mars", "Base-0"});
            wanted.push_back(PartnerWant{"Crystal", "Base-0"});
            // Archipelago and Desert share one water, read as Desert's; Lush water stands at Base-27.
            wanted.push_back(PartnerWant{"Desert", "Base-0"});
            wanted.push_back(PartnerWant{"Lush", "Base-27"});
            wanted.push_back(PartnerWant{"Acid", "Base-0"});
        }
        if (lowerText(brushWorld_) == "mars") {
            wanted.push_back(PartnerWant{"Ice", "Base-85"});
            wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
            wanted.push_back(PartnerWant{"Lush", "Base-77"});
            wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
            wanted.push_back(PartnerWant{"Acid", "Base-85"});
            wanted.push_back(PartnerWant{"Crystal", "Base-85"});
            wanted.push_back(PartnerWant{"Metal", "Base-85"});
            wanted.push_back(PartnerWant{"Desert", "Base-80"});
            // Mars water fades into GreenWorld water, so a Mars island can sit in a GreenWorld sea.
            wanted.push_back(PartnerWant{"GreenWorld", "Base-0"});
            // Archipelago and Desert share one water, read as Desert's, which fades into this water too.
            wanted.push_back(PartnerWant{"Desert", "Base-0"});
            // Lush, Crystal and Acid water fade into Mars water too.
            wanted.push_back(PartnerWant{"Lush", "Base-27"});
            wanted.push_back(PartnerWant{"Crystal", "Base-0"});
            wanted.push_back(PartnerWant{"Acid", "Base-0"});
        }
    } else if (lowerText(brushWorld_) == "crystal") {
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
        wanted.push_back(PartnerWant{"Acid", "Base-85"});
        wanted.push_back(PartnerWant{"Desert", "Base-80"});
        wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
        wanted.push_back(PartnerWant{"Mars", "Base-85"});
        wanted.push_back(PartnerWant{"Ice", "Base-85"});
        wanted.push_back(PartnerWant{"Metal", "Base-85"});
        wanted.push_back(PartnerWant{"Lush", "Base-77"});
        // Crystal water fades into the blue water GreenWorld, Ice and Metal share.
        wanted.push_back(PartnerWant{"GreenWorld", "Base-0"});
        // Archipelago and Desert share one water, read as Desert's, which fades into this water too.
        wanted.push_back(PartnerWant{"Desert", "Base-0"});
        // This water fades into Mars water too.
        wanted.push_back(PartnerWant{"Mars", "Base-0"});
        // This water fades into Lush water too.
        wanted.push_back(PartnerWant{"Lush", "Base-27"});
        // Crystal and Acid water fade into each other.
        wanted.push_back(PartnerWant{"Acid", "Base-0"});
    } else if (isMetalWorld(brushWorld_)) {
        wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        // Metal's Base-85 meets Lava's Base-41 through the Lava-Metal coasts.
        wanted.push_back(PartnerWant{"Lava", "Base-41"});
        wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
        wanted.push_back(PartnerWant{"Lush", "Base-77"});
        wanted.push_back(PartnerWant{"Acid", "Base-85"});
        wanted.push_back(PartnerWant{"Mars", "Base-85"});
        wanted.push_back(PartnerWant{"Ice", "Base-85"});
        wanted.push_back(PartnerWant{"Crystal", "Base-85"});
        wanted.push_back(PartnerWant{"Desert", "Base-80"});
        // Metal's blue water fades into Mars and Crystal water, as GreenWorld's does.
        wanted.push_back(PartnerWant{"Mars", "Base-0"});
        wanted.push_back(PartnerWant{"Crystal", "Base-0"});
        // Archipelago and Desert share one water, read as Desert's; Lush water stands at Base-27.
        wanted.push_back(PartnerWant{"Desert", "Base-0"});
        wanted.push_back(PartnerWant{"Lush", "Base-27"});
        wanted.push_back(PartnerWant{"Acid", "Base-0"});
    } else if (lowerText(brushWorld_) == "ice") {
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
        wanted.push_back(PartnerWant{"Mars", "Base-85"});
        wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
        wanted.push_back(PartnerWant{"Lush", "Base-77"});
        wanted.push_back(PartnerWant{"Desert", "Base-80"});
        wanted.push_back(PartnerWant{"Acid", "Base-85"});
        wanted.push_back(PartnerWant{"Crystal", "Base-85"});
        wanted.push_back(PartnerWant{"Metal", "Base-85"});
        // Ice's blue water fades into Mars and Crystal water, as GreenWorld's does.
        wanted.push_back(PartnerWant{"Mars", "Base-0"});
        wanted.push_back(PartnerWant{"Crystal", "Base-0"});
        // Archipelago and Desert share one water, read as Desert's; Lush water stands at Base-27.
        wanted.push_back(PartnerWant{"Desert", "Base-0"});
        wanted.push_back(PartnerWant{"Lush", "Base-27"});
        wanted.push_back(PartnerWant{"Acid", "Base-0"});
    } else if (lowerText(brushWorld_) == "archipelago") {
        // Archipelago water fades into the blue water GreenWorld, Ice and Metal share.
        wanted.push_back(PartnerWant{"GreenWorld", "Base-0"});
        // Acid, Lush, Crystal and Mars water fade into this water too.
        wanted.push_back(PartnerWant{"Acid", "Base-0"});
        wanted.push_back(PartnerWant{"Lush", "Base-27"});
        wanted.push_back(PartnerWant{"Crystal", "Base-0"});
        wanted.push_back(PartnerWant{"Mars", "Base-0"});
        wanted.push_back(PartnerWant{"Ice", "Base-85"});
        wanted.push_back(PartnerWant{"Crystal", "Base-85"});
        wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        wanted.push_back(PartnerWant{"Lush", "Base-77"});
        wanted.push_back(PartnerWant{"Acid", "Base-85"});
        wanted.push_back(PartnerWant{"Desert", "Base-80"});
        wanted.push_back(PartnerWant{"Mars", "Base-85"});
        wanted.push_back(PartnerWant{"Metal", "Base-85"});
    } else if (lowerText(brushWorld_) == "lush") {
        // Lush's Base-27 water fades down into the blue water GreenWorld, Ice and Metal share.
        wanted.push_back(PartnerWant{"GreenWorld", "Base-0"});
        // Archipelago and Desert share one water, read as Desert's, which fades into this water too.
        wanted.push_back(PartnerWant{"Desert", "Base-0"});
        // This water fades into Mars water too.
        wanted.push_back(PartnerWant{"Mars", "Base-0"});
        // Crystal and Acid water fade into Lush water too.
        wanted.push_back(PartnerWant{"Crystal", "Base-0"});
        wanted.push_back(PartnerWant{"Acid", "Base-0"});
        wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
        wanted.push_back(PartnerWant{"Mars", "Base-85"});
        wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        wanted.push_back(PartnerWant{"Ice", "Base-85"});
        wanted.push_back(PartnerWant{"Acid", "Base-85"});
        wanted.push_back(PartnerWant{"Crystal", "Base-85"});
        wanted.push_back(PartnerWant{"Metal", "Base-85"});
        wanted.push_back(PartnerWant{"Desert", "Base-80"});
    } else if (lowerText(brushWorld_) == "desert" || lowerText(brushWorld_) == "acid") {
        wanted.push_back(PartnerWant{"Lava", "Base-86"});
        if (lowerText(brushWorld_) == "desert") {
            // Desert water fades into the blue water GreenWorld, Ice and Metal share.
            wanted.push_back(PartnerWant{"GreenWorld", "Base-0"});
            // Acid, Lush, Crystal and Mars water fade into this water too.
            wanted.push_back(PartnerWant{"Acid", "Base-0"});
            wanted.push_back(PartnerWant{"Lush", "Base-27"});
            wanted.push_back(PartnerWant{"Crystal", "Base-0"});
            wanted.push_back(PartnerWant{"Mars", "Base-0"});
        }
        if (lowerText(brushWorld_) == "acid") {
            // Acid water fades into the blue water GreenWorld, Ice and Metal share.
            wanted.push_back(PartnerWant{"GreenWorld", "Base-0"});
            // Archipelago and Desert share one water, read as Desert's, which fades into this water too.
            wanted.push_back(PartnerWant{"Desert", "Base-0"});
            // This water fades into Mars water too.
            wanted.push_back(PartnerWant{"Mars", "Base-0"});
            // This water fades into Lush water too.
            wanted.push_back(PartnerWant{"Lush", "Base-27"});
            // Crystal and Acid water fade into each other.
            wanted.push_back(PartnerWant{"Crystal", "Base-0"});
            wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
            wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
            wanted.push_back(PartnerWant{"Crystal", "Base-85"});
            wanted.push_back(PartnerWant{"Desert", "Base-80"});
            wanted.push_back(PartnerWant{"Mars", "Base-85"});
            wanted.push_back(PartnerWant{"Ice", "Base-85"});
            wanted.push_back(PartnerWant{"Metal", "Base-85"});
            wanted.push_back(PartnerWant{"Lush", "Base-77"});
        }
        if (lowerText(brushWorld_) == "desert") {
            wanted.push_back(PartnerWant{"Acid", "Base-85"});
            wanted.push_back(PartnerWant{"Crystal", "Base-85"});
            wanted.push_back(PartnerWant{"Archipelago", "Base-78"});
            wanted.push_back(PartnerWant{"GreenWorld", "Base-85"});
            wanted.push_back(PartnerWant{"Ice", "Base-85"});
            wanted.push_back(PartnerWant{"Mars", "Base-85"});
            wanted.push_back(PartnerWant{"Metal", "Base-85"});
            wanted.push_back(PartnerWant{"Lush", "Base-77"});
        }
    }
    // Quagmire Base-85 blends into the Base-85 ground of GreenWorld, Mars, Acid, Crystal, Ice and Metal,
    // and those lands blend into it.
    static constexpr const char* kQuagmirePartners[] = {"GreenWorld", "Mars", "Acid", "Crystal", "Ice", "Metal"};
    if (lowerText(brushWorld_) == "quagmire") {
        for (const char* partner : kQuagmirePartners) {
            wanted.push_back(PartnerWant{partner, "Base-85"});
        }
    } else if (std::any_of(std::begin(kQuagmirePartners), std::end(kQuagmirePartners),
                   [&](const char* partner) { return lowerText(brushWorld_) == lowerText(partner); })) {
        wanted.push_back(PartnerWant{"Quagmire", "Base-85"});
    }
    // Both Evergreen grasses blend into every Base-85 land, Lava Base-86, Archipelago Base-78, Lush Base-77
    // and Desert Base-80, and those lands blend into both. The two grasses are each other's buttons, not
    // partners, since the Evergreen brush shows them both.
    static constexpr PartnerWant kEvergreenPartners[] = {
        {"GreenWorld", "Base-85"}, {"Mars", "Base-85"}, {"Acid", "Base-85"}, {"Crystal", "Base-85"}, {"Ice", "Base-85"},
        {"Metal", "Base-85"}, {"Quagmire", "Base-85"}, {"Lava", "Base-86"}, {"Archipelago", "Base-78"}, {"Lush", "Base-77"},
        {"Desert", "Base-80"},
    };
    if (isEvergreenWorld(brushWorld_)) {
        for (const PartnerWant& partner : kEvergreenPartners) {
            wanted.push_back(partner);
        }
    } else if (std::any_of(std::begin(kEvergreenPartners), std::end(kEvergreenPartners),
                   [&](const PartnerWant& partner) { return lowerText(brushWorld_) == lowerText(partner.world); })) {
        wanted.push_back(PartnerWant{"Evergreen", "Base-85"});
        wanted.push_back(PartnerWant{"Evergreen2", "Base-85"});
    }
    // SnowyEvergreen Base-85 blends into the ground of every other land, both Evergreen grasses among them,
    // and every one of those lands blends into it.
    if (lowerText(brushWorld_) == "snowyevergreen") {
        for (const PartnerWant& partner : kEvergreenPartners) {
            wanted.push_back(partner);
        }
        wanted.push_back(PartnerWant{"Evergreen", "Base-85"});
        wanted.push_back(PartnerWant{"Evergreen2", "Base-85"});
    } else if (isEvergreenWorld(brushWorld_)
               || std::any_of(std::begin(kEvergreenPartners), std::end(kEvergreenPartners),
                              [&](const PartnerWant& partner) { return lowerText(brushWorld_) == lowerText(partner.world); })) {
        wanted.push_back(PartnerWant{"SnowyEvergreen", "Base-85"});
    }
    // Quagmire water fades into every other land's water: Archipelago and Desert share one water, read as
    // Desert's, GreenWorld's stands for the blue water it shares with Ice and Metal, and Lush's stands at Base-27.
    if (lowerText(brushWorld_) == "quagmire") {
        for (const PartnerWant water : {PartnerWant{"GreenWorld", "Base-0"}, PartnerWant{"Desert", "Base-0"}, PartnerWant{"Mars", "Base-0"},
                 PartnerWant{"Crystal", "Base-0"}, PartnerWant{"Acid", "Base-0"}, PartnerWant{"Lush", "Base-27"}}) {
            wanted.push_back(water);
        }
    } else if (lowerText(brushWorld_) != "lava" && lowerText(brushWorld_) != "snowyevergreen") {
        wanted.push_back(PartnerWant{"Quagmire", "Base-0"});
    }
    // WaterWorld's water fades into every other water, and every other water into it. SnowyEvergreen's water does too,
    // through Evergreen's pieces.
    if (lowerText(brushWorld_) == "waterworld") {
        for (const PartnerWant water : {PartnerWant{"GreenWorld", "Base-0"}, PartnerWant{"Desert", "Base-0"}, PartnerWant{"Mars", "Base-0"},
                 PartnerWant{"Crystal", "Base-0"}, PartnerWant{"Acid", "Base-0"}, PartnerWant{"Lush", "Base-27"}}) {
            wanted.push_back(water);
        }
    } else if (lowerText(brushWorld_) != "lava") {
        wanted.push_back(PartnerWant{"WaterWorld", "Base-0"});
    }
    // Metal's second water fades into every other water; the Metal brush shows it as a button of its own.
    if (!isMetalWorld(brushWorld_) && lowerText(brushWorld_) != "lava" && lowerText(brushWorld_) != "snowyevergreen") {
        wanted.push_back(PartnerWant{"Metal2", "Base-0"});
    }
    // Evergreen water fades into the same waters, and into Quagmire's.
    if (isEvergreenWorld(brushWorld_)) {
        for (const PartnerWant water : {PartnerWant{"GreenWorld", "Base-0"}, PartnerWant{"Desert", "Base-0"}, PartnerWant{"Mars", "Base-0"},
                 PartnerWant{"Crystal", "Base-0"}, PartnerWant{"Acid", "Base-0"}, PartnerWant{"Lush", "Base-27"}}) {
            wanted.push_back(water);
        }
    } else if (lowerText(brushWorld_) != "lava" && lowerText(brushWorld_) != "snowyevergreen") {
        wanted.push_back(PartnerWant{"Evergreen", "Base-0"});
    }
    for (const PartnerWant& want : wanted) {
        for (const TilesetSection& section : source->sections) {
            if (!isBaseBrushTile(section.world, section.name) || lowerText(section.category) != lowerText(want.world)
                || lowerText(section.name) != lowerText(want.name)) {
                continue;
            }
            BrushPaint partner;
            partner.name = section.name;
            partner.category = section.category;
            brushPartners_.push_back(std::move(partner));
            // GreenWorld's 16 x 16 ground is a brush stroke, not the 8 x 8 base.
            // The flat fill has to use that stroke, or it stamps the wrong land.
            for (const TilesetSection& strokeSection : source->sections) {
                if (!isBrushStrokeWorld(strokeSection.world) || lowerText(strokeSection.category) != lowerText(section.category)) {
                    continue;
                }
                bool named = false;
                for (const int size : {1, 2, 4, 8, 16, 32, 64, 128, 256, 512}) {
                    named = named || brushStrokeVariation(strokeSection.name, section.name, size) >= 0;
                }
                if (!named) {
                    continue;
                }
                bool have = false;
                for (const BrushStroke& stroke : brushStrokes_) {
                    have = have || stroke.sectionPath == strokeSection.archivePath;
                }
                if (have) {
                    continue;
                }
                BrushStroke stroke;
                stroke.name = strokeSection.name;
                stroke.category = strokeSection.category;
                stroke.sectionPath = strokeSection.archivePath;
                brushStrokes_.push_back(std::move(stroke));
            }
            bool haveCell = false;
            for (const BrushStroke& stroke : brushStrokes_) {
                haveCell = haveCell || (brushCategoryMatches(stroke.category, section.category)
                    && brushStrokeVariation(stroke.name, section.name, kBrushCellTiles) >= 0);
            }
            if (!haveCell) {
                BrushStroke stroke;
                stroke.name = section.name + "-" + std::to_string(kBrushCellTiles) + "x" + std::to_string(kBrushCellTiles) + "-01";
                stroke.category = section.category;
                stroke.sectionPath = section.archivePath;
                brushStrokes_.push_back(std::move(stroke));
            }
            break;
        }
    }
    // The band tiles of both sizes of piece, so a stroke can keep each band one size.
    for (const auto& [path, into] : {std::pair<const char*, std::vector<std::uint64_t>*>{kCoarseBandTilesPath, &coarseBandTiles_},
             std::pair<const char*, std::vector<std::uint64_t>*>{kFineBandTilesPath, &fineBandTiles_},
             std::pair<const char*, std::vector<std::uint64_t>*>{kFixedTilesPath, &fixedTiles_}}) {
        try {
            if (const HpiFile* file = source->archive().find(path)) {
                *into = decodeTileHashes(source->archive().extract(*file));
            }
        } catch (const std::exception&) {
        }
    }
    // The 4 x 4 and 8 x 8 brushes fill flat cells with 8 x 8 strokes. A land with none of its own,
    // which is every land but GreenWorld, uses the four quarters of each of its 16 x 16 strokes.
    {
        std::vector<std::pair<std::string, std::string>> lands;
        for (const BrushPaint& paint : brushPaints_) {
            if (paint.kind == BrushKind::Base && trailingNumber(paint.name) >= 0) {
                lands.emplace_back(paint.category, paint.name);
            }
        }
        for (const BrushPaint& partner : brushPartners_) {
            lands.emplace_back(partner.category, partner.name);
        }
        std::vector<BrushStroke> quarters;
        for (const auto& [category, name] : lands) {
            bool haveFine = false;
            for (const BrushStroke& stroke : brushStrokes_) {
                haveFine = haveFine || (brushCategoryMatches(stroke.category, category) && brushStrokeVariation(stroke.name, name, kFineCellTiles) >= 0);
            }
            if (haveFine) {
                continue;
            }
            int number = 0;
            for (const BrushStroke& stroke : brushStrokes_) {
                if (!brushCategoryMatches(stroke.category, category) || brushStrokeVariation(stroke.name, name, kBrushCellTiles) < 0) {
                    continue;
                }
                for (int quarter = 0; quarter < 4; ++quarter) {
                    BrushStroke fine;
                    char suffix[24];
                    std::snprintf(suffix, sizeof(suffix), "-%dx%d-%02d", kFineCellTiles, kFineCellTiles, ++number);
                    fine.name = name + suffix;
                    fine.category = stroke.category;
                    fine.sectionPath = stroke.sectionPath;
                    fine.cropX = (quarter & 1) * kFineCellTiles;
                    fine.cropY = (quarter >> 1) * kFineCellTiles;
                    fine.cropTiles = kFineCellTiles;
                    quarters.push_back(std::move(fine));
                }
            }
        }
        for (BrushStroke& stroke : quarters) {
            brushStrokes_.push_back(std::move(stroke));
        }
    }
    // Reading every piece's tile pictures takes most of a tileset's load, so the result is kept in a cache file
    // beside the random map tileset and read back while the tileset and this build of the editor are unchanged.
    // Without a cache file the catalogue is built on a worker thread, and the Tile Brush shows a spinner until it
    // is done. The worker only writes brushPictures_, sharedPictures_, brushCoastLiquid_ and brushGrounds_, which
    // the main thread leaves alone while catalogueBuilding_ is set, and only reads the archive and this tileset's
    // paints, pieces and sections, which ensureBrushPaints does not change until the build is collected.
    const std::filesystem::path pictureCache = brushPictureCachePath(source->path, key);
    if (!loadBrushPictureCache(pictureCache)) {
        auto cancel = std::make_shared<std::atomic<bool>>(false);
        catalogueCancel_ = cancel;
        // The hill buttons below sort hillSections_ while the worker runs, so it reads this answer instead.
        const bool haveHillSections = !hillSections_.empty();
        // Progress: the pieces read so far, out of what the last build of this tileset read. Without a record, about
        // one read for each piece.
        auto done = std::make_shared<std::atomic<int>>(0);
        catalogueDone_ = done;
        std::filesystem::path readsPath = source->path;
        readsPath += ".brushcache";
        readsPath /= "reads-" + lowerText(brushTilesetOf(brushWorld_)) + ".txt";
        catalogueTotal_ = 0;
        {
            std::ifstream reads(readsPath);
            reads >> catalogueTotal_;
        }
        if (catalogueTotal_ <= 0) {
            catalogueTotal_ = std::max<int>(1, static_cast<int>(brushPieces_.size()));
        }
        catalogueJob = [this, source, key, pictureCache, cancel, haveHillSections, done, readsPath]() {
            tCatalogueWorker = true;
            // Choosing another tileset cancels the build: the next helper call unwinds it, and no cache file is written.
            const auto keepGoing = [&cancel]() {
                if (cancel->load(std::memory_order_relaxed)) {
                    throw CatalogueCancelled{};
                }
            };
            // Every piece the catalogue reads goes through here, so the count is the build's progress.
            const auto readStamp = [&](const HpiFile& file) {
                keepGoing();
                done->fetch_add(1, std::memory_order_relaxed);
                return loadSectionStamp(source->archive().extract(file));
            };
            try {
            std::vector<const BrushPaint*> pictured;
            for (const BrushPaint& paint : brushPaints_) {
                if (paint.kind == BrushKind::Base && trailingNumber(paint.name) >= 0) {
                    pictured.push_back(&paint);
                }
            }
            for (const BrushPaint& partner : brushPartners_) {
                pictured.push_back(&partner);
            }
            std::vector<int> picturedHeights;
            picturedHeights.reserve(pictured.size());
            for (const BrushPaint* paint : pictured) {
                picturedHeights.push_back(trailingNumber(paint->name));
            }
            const bool sharedHeight = paintsShareHeight(picturedHeights);
            // Reading every piece's tile pictures takes most of a tileset's load, so the result is kept in a cache file
            // beside the random map tileset and read back while the tileset and this build of the editor are unchanged.
            const bool picturesCached = false;
            if (!picturesCached) {
                if (sharedHeight) {
                    // The shore and the world blend belong to Base-86. Base-190 is a higher
                    // plateau, so the tallest Lava paint is not the ground those pictures join.
                    const auto lavaPaintAt = [&](int height) -> const BrushPaint* {
                        for (const BrushPaint* paint : pictured) {
                            if (blendWorldName(paint->category) == "Lava" && trailingNumber(paint->name) == height) {
                                return paint;
                            }
                        }
                        return nullptr;
                    };
                    const BrushPaint* lavaGround = nullptr;
                    const BrushPaint* lavaLiquid = nullptr;
                    const BrushPaint* lavaCliffLow = nullptr;
                    const BrushPaint* lavaCliffHigh = nullptr;
                    for (const BrushTransitionRule& rule : brushTransitionRules()) {
                        const BrushPaint* lower = lavaPaintAt(rule.lowerHeight);
                        const BrushPaint* upper = lavaPaintAt(rule.upperHeight);
                        if (lower == nullptr || upper == nullptr) {
                            continue;
                        }
                        if (rule.style == TransitionStyle::Coast && lavaGround == nullptr) {
                            lavaLiquid = lower;
                            lavaGround = upper;
                        } else if (rule.style == TransitionStyle::Cliff && lavaCliffHigh == nullptr) {
                            lavaCliffLow = lower;
                            lavaCliffHigh = upper;
                        }
                    }
                    const auto rememberTiles = [&](const SectionStamp& stamp, const std::string& category, const std::string& name, bool fringe,
                                                    const std::string& partnerCategory, const std::string& partnerName) {
                        keepGoing();
                        const int tiles = stamp.tileWidth * stamp.tileHeight;
                        for (int tile = 0; tile < tiles; ++tile) {
                            const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                            const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                            if (bytes + 1024 > stamp.tileImages.size()) {
                                continue;
                            }
                            const std::uint64_t hash = hashBrushTile(stamp.tileImages.data() + bytes);
                            const auto existing = brushPictures_.find(hash);
                            if (fringe && existing != brushPictures_.end()) {
                                continue;
                            }
                            // Base-85 and Base-219 share the grass picture, and Base-86 and
                            // Base-190 share the lava dirt. A later paint must not claim that
                            // picture outright, or flat ground is read as the cliff.
                            const std::string pictureWorld = blendWorldName(category);
                            const bool sameWorld = existing != brushPictures_.end() && !pictureWorld.empty()
                                && blendWorldName(existing->second.category) == pictureWorld;
                            const bool sameName = existing != brushPictures_.end() && lowerText(existing->second.name) == lowerText(name);
                            const bool partnerSameWorld = existing != brushPictures_.end() && !existing->second.partnerCategory.empty()
                                && blendWorldName(existing->second.partnerCategory) == pictureWorld;
                            if (!fringe && sameWorld && existing->second.fringe && sameName && partnerSameWorld) {
                                continue;
                            }
                            if (!fringe && sameWorld && !existing->second.fringe && !sameName) {
                                existing->second = BrushPictureHint{category, name, true, existing->second.category, existing->second.name};
                                continue;
                            }
                            // Base-86 and Base-190 use the same dirt picture. The height chooses
                            // between them, and a repeated tile must not turn that choice back
                            // into a hard Base-190, or every Base-86 click is read as the plateau.
                            if (!fringe && existing != brushPictures_.end() && blendWorldName(category) == "Lava"
                                && blendWorldName(existing->second.category) == "Lava") {
                                const bool same = lowerText(existing->second.name) == lowerText(name);
                                if (same && existing->second.fringe) {
                                    continue;
                                }
                                if (!same && !existing->second.fringe) {
                                    existing->second = BrushPictureHint{category, name, true, existing->second.category, existing->second.name};
                                    continue;
                                }
                                if (!same && existing->second.fringe) {
                                    continue;
                                }
                            }
                            brushPictures_[hash] = BrushPictureHint{category, name, fringe, partnerCategory, partnerName};
                        }
                    };
                    const auto rememberPath = [&](const std::string& path, const std::string& category, const std::string& name, bool fringe,
                                                   const std::string& partnerCategory, const std::string& partnerName) {
                        keepGoing();
                        try {
                            const HpiFile* file = source->archive().find(path);
                            if (file == nullptr) {
                                return;
                            }
                            rememberTiles(readStamp(*file), category, name, fringe, partnerCategory, partnerName);
                        } catch (const std::exception&) {
                        }
                    };
                    const std::string lavaGroundCategory = lavaGround != nullptr ? lavaGround->category : std::string();
                    const std::string lavaGroundName = lavaGround != nullptr ? lavaGround->name : std::string();
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const auto paintAt = [&](const char* family, int height) -> const BrushPaint* {
                            for (const BrushPaint* paint : pictured) {
                                if (blendWorldName(paint->category) == family && trailingNumber(paint->name) == height) {
                                    return paint;
                                }
                            }
                            return nullptr;
                        };
                        if (lowered.starts_with("blend-archilava-")) {
                            const BrushPaint* archipelago = paintAt("Archi", 78);
                            const BrushPaint* lava = paintAt("Lava", 86);
                            if (archipelago != nullptr && lava != nullptr) {
                                rememberPath(piece.sectionPath, archipelago->category, archipelago->name, true, lava->category, lava->name);
                            }
                            continue;
                        }
                        if (lowered.starts_with("blend-desert-")) {
                            const BrushPaint* desert = paintAt("Desert", 80);
                            const BrushPaint* lava = paintAt("Lava", 86);
                            if (desert != nullptr && lava != nullptr) {
                                rememberPath(piece.sectionPath, desert->category, desert->name, true, lava->category, lava->name);
                            }
                            continue;
                        }
                        if (lowered.starts_with("blend-greenacid-") || lowered.starts_with("blend-acidcrystal-")
                            || lowered.starts_with("blend-aciddesert-") || lowered.starts_with("blend-crystaldesert-")
                            || lowered.starts_with("blend-archidesert-") || lowered.starts_with("blend-desertgreen-")
                            || lowered.starts_with("blend-icedesert-") || lowered.starts_with("blend-archimars-")
                            || lowered.starts_with("blend-archimetal-") || lowered.starts_with("blend-lushacid-")
                            || lowered.starts_with("blend-lushcrystal-") || lowered.starts_with("blend-lushmetal-")
                            || lowered.starts_with("blend-desertlush-") || lowered.starts_with("blend-metallava-")
                            || lowered.starts_with("blend-greencrystal-") || lowered.starts_with("blend-acidmars-")
                            || lowered.starts_with("blend-iceacid-") || lowered.starts_with("blend-acidmetal-")
                            || lowered.starts_with("blend-crystalmars-") || lowered.starts_with("blend-marsmetal-")
                            || lowered.starts_with("blend-desertmars-") || lowered.starts_with("blend-icecrystal-")
                            || lowered.starts_with("blend-icemetal-") || lowered.starts_with("blend-crystalmetal-")
                            || lowered.starts_with("blend-desertmetal-")) {
                            continue;
                        }
                        if (lowered.starts_with("blend-acid-")) {
                            const BrushPaint* acid = paintAt("Acid", 85);
                            const BrushPaint* lava = paintAt("Lava", 86);
                            if (acid != nullptr && lava != nullptr) {
                                rememberPath(piece.sectionPath, acid->category, acid->name, true, lava->category, lava->name);
                            }
                            continue;
                        }
                        if (lowered.starts_with("blend-archi-")) {
                            const BrushPaint* grass = nullptr;
                            const BrushPaint* archipelago = nullptr;
                            for (const BrushPaint* paint : pictured) {
                                if (blendWorldName(paint->category) == "Green" && trailingNumber(paint->name) == 85) {
                                    grass = paint;
                                } else if (blendWorldName(paint->category) == "Archi" && trailingNumber(paint->name) == 78) {
                                    archipelago = paint;
                                }
                            }
                            if (grass != nullptr && archipelago != nullptr) {
                                rememberPath(piece.sectionPath, grass->category, grass->name, true, archipelago->category, archipelago->name);
                            }
                            continue;
                        }
                        const char* family = nullptr;
                        if (lowered.starts_with("blend-green-")) {
                            family = "Green";
                        } else if (lowered.starts_with("blend-mars-")) {
                            family = "Mars";
                        }
                        if (family == nullptr) {
                            continue;
                        }
                        const BrushPaint* upper = nullptr;
                        for (const BrushPaint* paint : pictured) {
                            if (blendWorldName(paint->category) == family && trailingNumber(paint->name) == 85) {
                                upper = paint;
                                break;
                            }
                        }
                        if (upper != nullptr) {
                            rememberPath(piece.sectionPath, upper->category, upper->name, true, lavaGroundCategory, lavaGroundName);
                        }
                    }
                    for (const BrushPaint* paint : pictured) {
                        for (const TilesetSection& section : source->sections) {
                            if (lowerText(section.category) != lowerText(paint->category)) {
                                continue;
                            }
                            const bool base = isBaseBrushTile(section.world, section.name) && lowerText(section.name) == lowerText(paint->name);
                            bool stroke = false;
                            if (isBrushStrokeWorld(section.world)) {
                                constexpr int kStrokeSizes[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512};
                                for (const int size : kStrokeSizes) {
                                    stroke = stroke || brushStrokeVariation(section.name, paint->name, size) >= 0;
                                }
                            }
                            if (base || stroke) {
                                rememberPath(section.archivePath, paint->category, paint->name, false, {}, {});
                            }
                        }
                    }
                    // GreenWorld, Ice and Metal draw the same blue water, though Metal has a few tiles of its own,
                    // and Archipelago and Desert share one water too. A brush that holds one water of a family
                    // reads the others as that one, so Metal water beside a Mars island is not read as Mars water
                    // by its height.
                    for (const auto& isFamily : {std::function<bool(const std::string&)>([](const std::string& world) { return blueWaterWorld(world); }),
                             std::function<bool(const std::string&)>([](const std::string& world) { return world == "Archi" || world == "Desert"; })}) {
                        const BrushPaint* blueWater = nullptr;
                        for (const BrushPaint* paint : pictured) {
                            if (trailingNumber(paint->name) == 0 && isFamily(blendWorldName(paint->category))) {
                                blueWater = paint;
                                break;
                            }
                        }
                        if (blueWater != nullptr) {
                            for (const TilesetSection& section : source->sections) {
                                const std::string family = blendWorldName(section.category);
                                if (!isFamily(family) || family == blendWorldName(blueWater->category) || !isBaseBrushTile(section.world, section.name)
                                    || trailingNumber(section.name) != 0) {
                                    continue;
                                }
                                bool held = false;
                                for (const BrushPaint* paint : pictured) {
                                    held = held || (blendWorldName(paint->category) == family && trailingNumber(paint->name) == 0);
                                }
                                if (!held) {
                                    rememberPath(section.archivePath, blueWater->category, blueWater->name, false, {}, {});
                                }
                            }
                        }
                    }
                    // A lava shore rises from Base-41 to Base-86, through heights that also sit near
                    // Mars and GreenWorld Base-85. The shore picture keeps that corner on lava.
                    if (lavaGround != nullptr && lavaLiquid != nullptr && lavaGround != lavaLiquid) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (blendWorldName(piece.category) != "Lava" || !lowerText(piece.name).starts_with("coast-")) {
                                continue;
                            }
                            rememberPath(piece.sectionPath, lavaGround->category, lavaGround->name, true, lavaLiquid->category, lavaLiquid->name);
                        }
                    }
                    // A partner land's own coasts, such as a lava shore beside GreenWorld: the brush has no pieces
                    // for that land's liquid, but the shore's land side is the partner land. Its rim rises above
                    // the land, nearer GreenWorld's Base-85 than its own liquid, so without the pictures that rim
                    // is read as GreenWorld ground. Each tile is the partner land when its height is nearer that
                    // land than the liquid; the liquid side is read by its height as before.
                    for (const BrushPaint& partner : brushPartners_) {
                        int liquid = -1;
                        std::string liquidName;
                        for (const TilesetSection& section : source->sections) {
                            if (isBaseBrushTile(section.world, section.name) && lowerText(section.category) == lowerText(partner.category)) {
                                const int height = trailingNumber(section.name);
                                if (height >= 0 && (liquid < 0 || height < liquid)) {
                                    liquid = height;
                                    liquidName = section.name;
                                }
                            }
                        }
                        if (liquid < 0 || liquid >= trailingNumber(partner.name)) {
                            continue;
                        }
                        for (const TilesetSection& section : source->sections) {
                            std::string kind;
                            brushPieceFolderTiles(section.world, kind);
                            const std::string folder = lowerText(kind);
                            if (lowerText(section.category) != lowerText(partner.category) || (folder != "coasttiles" && folder != "coastdsl")) {
                                continue;
                            }
                            rememberPath(section.archivePath, partner.category, partner.name, true, partner.category, liquidName);
                        }
                    }
                    // A lava cliff rises from Base-86 to Base-190. Heights along that face also
                    // sit near Mars and GreenWorld Base-85, so the picture keeps the corner on lava.
                    if (lavaCliffLow != nullptr && lavaCliffHigh != nullptr && lavaCliffLow != lavaCliffHigh) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (blendWorldName(piece.category) != "Lava" || !lowerText(piece.name).starts_with("cliff-")
                                || lowerText(piece.category) == lowerText(kLavaShoreFolder)) {
                                continue;
                            }
                            rememberPath(piece.sectionPath, lavaCliffHigh->category, lavaCliffHigh->name, true, lavaCliffLow->category, lavaCliffLow->name);
                        }
                    }
                    // The Lava-Metal coasts rise from Lava's Base-41 to Metal's Base-85. The picture keeps the land on Metal and the
                    // liquid on lava.
                    {
                        const BrushPaint* metal85 = nullptr;
                        const BrushPaint* lava41 = nullptr;
                        for (const BrushPaint* paint : pictured) {
                            if (lowerText(paint->category) == "metal" && trailingNumber(paint->name) == 85) {
                                metal85 = paint;
                            } else if (lowerText(paint->category) == "lava" && trailingNumber(paint->name) == 41) {
                                lava41 = paint;
                            }
                        }
                        if (metal85 != nullptr && lava41 != nullptr) {
                            for (const BrushPiece& piece : brushPieces_) {
                                if (lowerText(piece.category) == lowerText(kLavaMetalFolder)) {
                                    rememberPath(piece.sectionPath, metal85->category, metal85->name, true, lava41->category, lava41->name);
                                }
                            }
                        }
                    }
                    // The cracked shore in Lava-76 rises from Base-76 to Base-86 instead.
                    if (const BrushPaint* shoreLow = lavaPaintAt(76), *shoreHigh = lavaPaintAt(86); shoreLow != nullptr && shoreHigh != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (lowerText(piece.category) == lowerText(kLavaShoreFolder) && lowerText(piece.name).starts_with("cliff-")) {
                                rememberPath(piece.sectionPath, shoreHigh->category, shoreHigh->name, true, shoreLow->category, shoreLow->name);
                            }
                        }
                    }
                    // WaterWorld's floor pieces join its Base-0 water to its Base-1 floor at almost one height, so only the pictures
                    // tell them apart. Their whole water and floor tiles are the bases' own; a tile that mixes the two is floor
                    // when it is a touch redder.
                    {
                        const BrushPaint* floorPaint = nullptr;
                        const BrushPaint* waterPaint = nullptr;
                        for (const BrushPaint* paint : pictured) {
                            if (lowerText(paint->category) == "waterworld" && trailingNumber(paint->name) == 1) {
                                floorPaint = paint;
                            } else if (lowerText(paint->category) == "waterworld" && trailingNumber(paint->name) == 0) {
                                waterPaint = paint;
                            }
                        }
                        if (floorPaint != nullptr && waterPaint != nullptr) {
                            for (const BrushPiece& piece : brushPieces_) {
                                if (lowerText(piece.category) != lowerText(kWaterWorldFloorFolder)) {
                                    continue;
                                }
                                try {
                                    const HpiFile* file = source->archive().find(piece.sectionPath);
                                    if (file == nullptr) {
                                        continue;
                                    }
                                    const SectionStamp stamp = readStamp(*file);
                                    for (int tile = 0; tile < stamp.tileWidth * stamp.tileHeight; ++tile) {
                                        const std::size_t bytes = static_cast<std::size_t>(stamp.indices[static_cast<std::size_t>(tile)]) * 1024;
                                        if (bytes + 1024 > stamp.tileImages.size()) {
                                            continue;
                                        }
                                        const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                        const std::uint64_t hash = hashBrushTile(pixels);
                                        const auto existing = brushPictures_.find(hash);
                                        if (existing != brushPictures_.end() && !existing->second.fringe) {
                                            continue;
                                        }
                                        const BrushPaint* land = waterWorldFloorTileIsFloor(pixels) ? floorPaint : waterPaint;
                                        brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                    }
                                } catch (const std::exception&) {
                                }
                            }
                        }
                    }
                    // Metal's sea cliffs in Metal-0 rise from Metal2's dark water to Metal's Base-139. Their water is not the
                    // dark base's own picture, so the picture keeps the low side on the dark water and not on Metal's blue water.
                    {
                        const BrushPaint* darkWater = nullptr;
                        const BrushPaint* metal139 = nullptr;
                        for (const BrushPaint* paint : pictured) {
                            if (lowerText(paint->category) == "metal2" && trailingNumber(paint->name) == 0) {
                                darkWater = paint;
                            } else if (lowerText(paint->category) == "metal" && trailingNumber(paint->name) == 139) {
                                metal139 = paint;
                            }
                        }
                        if (darkWater != nullptr && metal139 != nullptr) {
                            for (const BrushPiece& piece : brushPieces_) {
                                if (lowerText(piece.category) == lowerText(kMetalSeaCliffFolder) && lowerText(piece.name).starts_with("cliff-")) {
                                    rememberPath(piece.sectionPath, metal139->category, metal139->name, true, darkWater->category, darkWater->name);
                                }
                            }
                        }
                    }
                    // Green and Acid are the same height, so a blend tile cannot be split by
                    // height. A picture that no base has claimed yet is grass or acid by color.
                    const BrushPaint* greenLand = nullptr;
                    const BrushPaint* acidLand = nullptr;
                    for (const BrushPaint* paint : pictured) {
                        if (blendWorldName(paint->category) == "Green" && trailingNumber(paint->name) == 85) {
                            greenLand = paint;
                        } else if (blendWorldName(paint->category) == "Acid" && trailingNumber(paint->name) == 85) {
                            acidLand = paint;
                        }
                    }
                    if (greenLand != nullptr && acidLand != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (!lowerText(piece.name).starts_with("blend-greenacid-")) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(piece.sectionPath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                    const std::uint64_t hash = hashBrushTile(pixels);
                                    if (brushPictures_.find(hash) != brushPictures_.end()) {
                                        continue;
                                    }
                                    const BrushPaint* land = greenAcidTileIsGrass(pixels) ? greenLand : acidLand;
                                    brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                    // Acid fills one side of a three-way tile and grass and lava split the
                    // other. Each picture is remembered as the land its color belongs to.
                    if (greenLand != nullptr && acidLand != nullptr && lavaGround != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            // The Acid, GreenWorld and Lava set: any three-way name with both acid and grass in it,
                            // such as Blend-Tri-Acid-T-Grass-L, Blend-Tri-Grass-T-Acid-L, or Blend-Tri-Lava-T-Acid-L-Grass.
                            if (!lowerText(piece.name).starts_with("blend-tri-") || lowerText(piece.name).find("acid") == std::string::npos
                                || lowerText(piece.name).find("grass") == std::string::npos || lowerText(piece.name).find("crystal") != std::string::npos
                                || lowerText(piece.name).find("desert") != std::string::npos
                                || lowerText(piece.name).find("archi") != std::string::npos
                                || lowerText(piece.name).find("ice") != std::string::npos
                                || lowerText(piece.name).find("lush") != std::string::npos
                                || lowerText(piece.name).find("mars") != std::string::npos
                                || lowerText(piece.name).find("metal") != std::string::npos) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(piece.sectionPath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                    const std::uint64_t hash = hashBrushTile(pixels);
                                    if (brushPictures_.find(hash) != brushPictures_.end()) {
                                        continue;
                                    }
                                    const BrushPaint* land = greenLand;
                                    const int which = triTileLand(pixels);
                                    if (which == 1) {
                                        land = lavaGround;
                                    } else if (which == 2) {
                                        land = acidLand;
                                    }
                                    brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                } else if (haveHillSections) {
                    // Tilesets whose bases all differ in height, such as Crystal, still need the
                    // flat base pictures: the hill, pool, island and reef brushes check the ground
                    // under them by its picture. A tileset without those brushes, such as Metal,
                    // keeps reading its ground by height alone.
                    for (const BrushPaint* paint : pictured) {
                        for (const TilesetSection& section : source->sections) {
                            if (lowerText(section.category) != lowerText(paint->category) || !isBaseBrushTile(section.world, section.name)
                                || lowerText(section.name) != lowerText(paint->name)) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(section.archivePath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::size_t bytes = static_cast<std::size_t>(stamp.indices[static_cast<std::size_t>(tile)]) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint64_t hash = hashBrushTile(stamp.tileImages.data() + bytes);
                                    const auto samePaint = [&](const std::string& category, const std::string& name) {
                                        return lowerText(category) == lowerText(paint->category) && lowerText(name) == lowerText(paint->name);
                                    };
                                    // Several bases can share one picture: Lush Base-77 and Base-183 are the same
                                    // grass, and Desert Base-155 and Base-230 the same sand. No base may claim it,
                                    // so the picture moves to sharedPictures_ and the height decides.
                                    if (const auto shared = sharedPictures_.find(hash); shared != sharedPictures_.end()) {
                                        bool listed = false;
                                        for (const auto& [category, name] : shared->second) {
                                            listed = listed || samePaint(category, name);
                                        }
                                        if (!listed) {
                                            shared->second.emplace_back(paint->category, paint->name);
                                        }
                                        continue;
                                    }
                                    const auto [existing, added] = brushPictures_.try_emplace(hash, BrushPictureHint{paint->category, paint->name, false, {}, {}});
                                    if (!added && !samePaint(existing->second.category, existing->second.name)) {
                                        sharedPictures_[hash] = {{existing->second.category, existing->second.name}, {paint->category, paint->name}};
                                        brushPictures_.erase(existing);
                                    }
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
                // Three-way sets with GreenWorld, named with triGreenLushName: GreenWorld fills or meets two other
                // lands, the second and last, and each tile's land comes from its colour. These run before the other
                // three-way checks, several of which look for a single land word such as ice, lush, or grass and
                // would claim these tiles. Every name has grass and both other lands' words in it.
                struct GreenTriSet {
                    const char* secondWorld;
                    const char* secondWord;
                    int secondHeight;
                    const char* lastWorld;
                    const char* lastWord;
                    int lastHeight;
                    int (*landOf)(const std::uint8_t*);  // 0 the pivot land, 1 the second land, 2 the last
                    const char* pivotWorld = "Green";
                    const char* pivotWord = "grass";
                    int pivotHeight = 85;
                };
                static constexpr GreenTriSet kGreenTriSets[] = {
                    {"Ice", "ice", 85, "Lush", "lush", 77, [](const std::uint8_t* p) { return triGreenIceLushTileLand(p, false); }},
                    {"Mars", "mars", 85, "Lush", "lush", 77, [](const std::uint8_t* p) { return triGreenIceLushTileLand(p, true); }},
                    {"Ice", "ice", 85, "Mars", "mars", 85, triGreenIceMarsTileLand},
                    {"Metal", "metal", 85, "Lush", "lush", 77, triGreenMetalLushTileLand},
                    {"Archi", "archi", 78, "Lush", "lush", 77, [](const std::uint8_t* p) { return triArchiGreenLushTileLand(p, 140); }},
                    {"Archi", "archi", 78, "Mars", "mars", 85, triGreenIceMarsTileLand},
                    {"Desert", "desert", 80, "Lush", "lush", 77, [](const std::uint8_t* p) { return triArchiGreenLushTileLand(p, 110); }},
                    {"Crystal", "crystal", 85, "Lush", "lush", 77, triGreenCrystalLushTileLand},
                    {"Archi", "archi", 78, "Desert", "desert", 80, triArchiDesertGreenTileLand},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiGreenTileLand},
                    {"Archi", "archi", 78, "Crystal", "crystal", 85, triArchiCrystalGreenTileLand},
                    {"Archi", "archi", 78, "Metal", "metal", 85, triArchiGreenMetalTileLand},
                    {"Archi", "archi", 78, "Ice", "ice", 85, triArchiGreenIceTileLand},
                    {"Mars", "mars", 85, "Metal", "metal", 85, triGreenMarsMetalTileLand},
                    {"Ice", "ice", 85, "Metal", "metal", 85, triGreenIceMetalTileLand},
                    {"Desert", "desert", 80, "Ice", "ice", 85, triDesertGreenIceTileLand},
                    {"Desert", "desert", 80, "Mars", "mars", 85, triDesertGreenMarsTileLand},
                    {"Desert", "desert", 80, "Metal", "metal", 85, triDesertGreenMetalTileLand},
                    {"Ice", "ice", 85, "Crystal", "crystal", 85, triCrystalGreenIceTileLand},
                    {"Mars", "mars", 85, "Crystal", "crystal", 85, triCrystalGreenMarsTileLand},
                    {"Metal", "metal", 85, "Crystal", "crystal", 85, triCrystalGreenMetalTileLand},
                    {"Desert", "desert", 80, "Crystal", "crystal", 85, triCrystalDesertGreenTileLand},
                    {"Acid", "acid", 85, "Lush", "lush", 77, triAcidGreenLushTileLand},
                    {"Ice", "ice", 85, "Acid", "acid", 85, triAcidGreenIceTileLand},
                    {"Mars", "mars", 85, "Acid", "acid", 85, triAcidGreenMarsTileLand},
                    {"Metal", "metal", 85, "Acid", "acid", 85, triAcidGreenMetalTileLand},
                    {"Crystal", "crystal", 85, "Acid", "acid", 85, triAcidCrystalGreenTileLand},
                    {"Desert", "desert", 80, "Acid", "acid", 85, triAcidDesertGreenTileLand},
                    {"Archi", "archi", 78, "Desert", "desert", 80, triArchiDesertLushTileLand, "Lush", "lush", 77},
                    {"Archi", "archi", 78, "Ice", "ice", 85, triArchiIceLushTileLand, "Lush", "lush", 77},
                    {"Archi", "archi", 78, "Mars", "mars", 85, triArchiLushMarsTileLand, "Lush", "lush", 77},
                    {"Archi", "archi", 78, "Metal", "metal", 85, triArchiLushMetalTileLand, "Lush", "lush", 77},
                    {"Archi", "archi", 78, "Metal", "metal", 85, triArchiIceMetalTileLand, "Ice", "ice", 85},
                    {"Archi", "archi", 78, "Metal", "metal", 85, triArchiMarsMetalTileLand, "Mars", "mars", 85},
                    {"Archi", "archi", 78, "Desert", "desert", 80, triArchiCrystalDesertTileLand, "Crystal", "crystal", 85},
                    {"Archi", "archi", 78, "Ice", "ice", 85, triArchiCrystalIceTileLand, "Crystal", "crystal", 85},
                    {"Archi", "archi", 78, "Crystal", "crystal", 85, triArchiCrystalLushTileLand, "Lush", "lush", 77},
                    {"Archi", "archi", 78, "Mars", "mars", 85, triArchiCrystalMarsTileLand, "Crystal", "crystal", 85},
                    {"Archi", "archi", 78, "Metal", "metal", 85, triArchiCrystalMetalTileLand, "Crystal", "crystal", 85},
                    {"Archi", "archi", 78, "Ice", "ice", 85, triArchiDesertIceTileLand, "Desert", "desert", 80},
                    {"Archi", "archi", 78, "Mars", "mars", 85, triArchiDesertMarsTileLand, "Desert", "desert", 80},
                    {"Archi", "archi", 78, "Metal", "metal", 85, triArchiDesertMetalTileLand, "Desert", "desert", 80},
                    {"Crystal", "crystal", 85, "Ice", "ice", 85, triCrystalDesertIceTileLand, "Desert", "desert", 80},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiCrystalTileLand, "Crystal", "crystal", 85},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiDesertTileLand, "Desert", "desert", 80},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiIceTileLand, "Ice", "ice", 85},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiLushTileLand, "Lush", "lush", 77},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiMarsTileLand, "Mars", "mars", 85},
                    {"Archi", "archi", 78, "Acid", "acid", 85, triAcidArchiMetalTileLand, "Metal", "metal", 85},
                    {"Crystal", "crystal", 85, "Acid", "acid", 85, triAcidCrystalIceTileLand, "Ice", "ice", 85},
                    {"Desert", "desert", 80, "Acid", "acid", 85, triAcidDesertIceTileLand, "Ice", "ice", 85},
                    {"Ice", "ice", 85, "Acid", "acid", 85, triAcidIceLushTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Acid", "acid", 85, triAcidIceMarsTileLand, "Ice", "ice", 85},
                    {"Metal", "metal", 85, "Acid", "acid", 85, triAcidIceMetalTileLand, "Ice", "ice", 85},
                    {"Ice", "ice", 85, "Crystal", "crystal", 85, triCrystalIceLushTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Crystal", "crystal", 85, triCrystalIceMarsTileLand, "Ice", "ice", 85},
                    {"Metal", "metal", 85, "Crystal", "crystal", 85, triCrystalIceMetalTileLand, "Ice", "ice", 85},
                    {"Desert", "desert", 80, "Ice", "ice", 85, triDesertIceLushTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Ice", "ice", 85, triDesertIceMarsTileLand, "Desert", "desert", 80},
                    {"Metal", "metal", 85, "Ice", "ice", 85, triDesertIceMetalTileLand, "Desert", "desert", 80},
                    {"Ice", "ice", 85, "Mars", "mars", 85, triIceLushMarsTileLand, "Lush", "lush", 77},
                    {"Ice", "ice", 85, "Metal", "metal", 85, triIceLushMetalTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Metal", "metal", 85, triIceMarsMetalTileLand, "Ice", "ice", 85},
                    {"Mars", "mars", 85, "Metal", "metal", 85, triDesertMarsMetalTileLand, "Desert", "desert", 80},
                    {"Desert", "desert", 80, "Metal", "metal", 85, triDesertLushMetalTileLand, "Lush", "lush", 77},
                    {"Desert", "desert", 80, "Mars", "mars", 85, triDesertLushMarsTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Metal", "metal", 85, triCrystalMarsMetalTileLand, "Crystal", "crystal", 85},
                    {"Crystal", "crystal", 85, "Metal", "metal", 85, triCrystalLushMetalTileLand, "Lush", "lush", 77},
                    {"Crystal", "crystal", 85, "Mars", "mars", 85, triCrystalLushMarsTileLand, "Lush", "lush", 77},
                    {"Crystal", "crystal", 85, "Metal", "metal", 85, triCrystalDesertMetalTileLand, "Desert", "desert", 80},
                    {"Crystal", "crystal", 85, "Mars", "mars", 85, triCrystalDesertMarsTileLand, "Desert", "desert", 80},
                    {"Desert", "desert", 80, "Crystal", "crystal", 85, triCrystalDesertLushTileLand, "Lush", "lush", 77},
                    {"Crystal", "crystal", 85, "Acid", "acid", 85, triAcidCrystalDesertTileLand, "Desert", "desert", 80},
                    {"Desert", "desert", 80, "Acid", "acid", 85, triAcidDesertLushTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Acid", "acid", 85, triAcidDesertMarsTileLand, "Desert", "desert", 80},
                    {"Metal", "metal", 85, "Acid", "acid", 85, triAcidDesertMetalTileLand, "Desert", "desert", 80},
                    {"Crystal", "crystal", 85, "Acid", "acid", 85, triAcidCrystalLushTileLand, "Lush", "lush", 77},
                    {"Mars", "mars", 85, "Acid", "acid", 85, triAcidCrystalMarsTileLand, "Crystal", "crystal", 85},
                    {"Metal", "metal", 85, "Acid", "acid", 85, triAcidCrystalMetalTileLand, "Crystal", "crystal", 85},
                    {"Mars", "mars", 85, "Acid", "acid", 85, triAcidLushMarsTileLand, "Lush", "lush", 77},
                    {"Metal", "metal", 85, "Acid", "acid", 85, triAcidLushMetalTileLand, "Lush", "lush", 77},
                    {"Metal", "metal", 85, "Acid", "acid", 85, triAcidMarsMetalTileLand, "Mars", "mars", 85},
                    {"Mars", "mars", 85, "Metal", "metal", 85, triLushMarsMetalTileLand, "Lush", "lush", 77},
                };
                for (const GreenTriSet& set : kGreenTriSets) {
                    const char* thirdWorld = set.secondWorld;
                    const char* thirdWord = set.secondWord;
                    const int thirdHeight = set.secondHeight;
                    const char* lastWorld = set.lastWorld;
                    const char* lastWord = set.lastWord;
                    const int lastHeight = set.lastHeight;
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == set.pivotWorld && height == set.pivotHeight) {
                            lands[0] = paint;
                        } else if (family == thirdWorld && height == thirdHeight) {
                            lands[1] = paint;
                        } else if (family == lastWorld && height == lastHeight) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        // Metal2's water pieces hold "metal" in their names too; they are read with the other waters.
                        if (lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr || !lowered.starts_with("blend-tri-")
                            || lowered.find("metal2") != std::string::npos
                            || lowered.find(set.pivotWord) == std::string::npos || lowered.find(thirdWord) == std::string::npos
                            || lowered.find(lastWord) == std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            // The four corner tiles are the lands the name gives. Colour alone can misread one, such
                            // as a grey Acid rock beside Metal, and a corner read as another land makes the brush
                            // paint the wrong piece in the next cell.
                            const std::array<int, 4> cornerLands = threeWayCornerLands(lowered, {set.pivotWord, thirdWord, lastWord});
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int tx = tile % stamp.tileWidth;
                                const int ty = tile / stamp.tileWidth;
                                const bool edgeX = tx == 0 || tx == stamp.tileWidth - 1;
                                const bool edgeY = ty == 0 || ty == stamp.tileHeight - 1;
                                const int corner = (tx == 0 ? 0 : 1) + (ty == 0 ? 0 : 2);
                                const int named = edgeX && edgeY ? cornerLands[static_cast<std::size_t>(corner)] : -1;
                                const BrushPaint* land = lands[named >= 0 ? named : set.landOf(pixels)];
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lush and Archipelago use different heights, and a lush rock can sit nearer
                // another paint. The picture keeps each tile on the land its color belongs to.
                const BrushPaint* lushLand = nullptr;
                const BrushPaint* archiLand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Lush" && trailingNumber(paint->name) == 77) {
                        lushLand = paint;
                    } else if (blendWorldName(paint->category) == "Archi" && trailingNumber(paint->name) == 78) {
                        archiLand = paint;
                    }
                }
                if (lushLand != nullptr && archiLand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-archilush-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = archiLushTileIsLush(pixels) ? lushLand : archiLand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Acid and Archipelago use different heights, and a rock on the sand can sit
                // nearer another paint. The picture keeps each tile on the land its color belongs to.
                const BrushPaint* acidBlendLand = nullptr;
                const BrushPaint* archiAcidLand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Acid" && trailingNumber(paint->name) == 85) {
                        acidBlendLand = paint;
                    } else if (blendWorldName(paint->category) == "Archi" && trailingNumber(paint->name) == 78) {
                        archiAcidLand = paint;
                    }
                }
                if (acidBlendLand != nullptr && archiAcidLand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-archiacid-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = archiAcidTileIsSand(pixels) ? archiAcidLand : acidBlendLand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Acid Base-85 and Desert Base-80 sit in the same height band, so the colour
                // tells each tile's land. Pale sand is Desert; darker olive is Acid.
                {
                    const BrushPaint* acidLand = nullptr;
                    const BrushPaint* desertLand = nullptr;
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Acid" && height == 85) {
                            acidLand = paint;
                        } else if (family == "Desert" && height == 80) {
                            desertLand = paint;
                        }
                    }
                    if (acidLand != nullptr && desertLand != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (!lowerText(piece.name).starts_with("blend-aciddesert-")) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(piece.sectionPath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                    const std::uint64_t hash = hashBrushTile(pixels);
                                    const auto existing = brushPictures_.find(hash);
                                    if (existing != brushPictures_.end() && !existing->second.fringe) {
                                        continue;
                                    }
                                    const int tx = tile % stamp.tileWidth;
                                    const int ty = tile / stamp.tileWidth;
                                    const BrushPaint* land = acidDesertTileIsDesert(pixels, lowerText(piece.name), tx, ty, stamp.tileWidth, stamp.tileHeight)
                                        ? desertLand : acidLand;
                                    brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
                // Crystal Base-85 and Desert Base-80 sit in the same height band, so the colour
                // tells each tile's land. Pale sand is Desert; purple is Crystal.
                {
                    const BrushPaint* crystalLand = nullptr;
                    const BrushPaint* desertLand = nullptr;
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Crystal" && height == 85) {
                            crystalLand = paint;
                        } else if (family == "Desert" && height == 80) {
                            desertLand = paint;
                        }
                    }
                    if (crystalLand != nullptr && desertLand != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (!lowerText(piece.name).starts_with("blend-crystaldesert-")) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(piece.sectionPath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                    const std::uint64_t hash = hashBrushTile(pixels);
                                    const auto existing = brushPictures_.find(hash);
                                    if (existing != brushPictures_.end() && !existing->second.fringe) {
                                        continue;
                                    }
                                    const int tx = tile % stamp.tileWidth;
                                    const int ty = tile / stamp.tileWidth;
                                    const BrushPaint* land = crystalDesertTileIsDesert(pixels, lowerText(piece.name), tx, ty, stamp.tileWidth, stamp.tileHeight)
                                        ? desertLand : crystalLand;
                                    brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
                // Archipelago Base-78 and Desert Base-80 sit close in height, so the colour
                // tells each tile's land. Paler sand is Archipelago; darker orange sand is Desert.
                {
                    const BrushPaint* archiLand = nullptr;
                    const BrushPaint* desertLand = nullptr;
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Archi" && height == 78) {
                            archiLand = paint;
                        } else if (family == "Desert" && height == 80) {
                            desertLand = paint;
                        }
                    }
                    if (archiLand != nullptr && desertLand != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (!lowerText(piece.name).starts_with("blend-archidesert-")) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(piece.sectionPath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                    const std::uint64_t hash = hashBrushTile(pixels);
                                    const auto existing = brushPictures_.find(hash);
                                    if (existing != brushPictures_.end() && !existing->second.fringe) {
                                        continue;
                                    }
                                    const int tx = tile % stamp.tileWidth;
                                    const int ty = tile / stamp.tileWidth;
                                    const BrushPaint* land = archiDesertTileIsDesert(pixels, lowerText(piece.name), tx, ty, stamp.tileWidth, stamp.tileHeight)
                                        ? desertLand : archiLand;
                                    brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
                // GreenWorld Base-85 and Desert Base-80 sit in the same height band, so the colour
                // tells each tile's land. Green is grass; orange sand is Desert.
                {
                    const BrushPaint* greenLand = nullptr;
                    const BrushPaint* desertLand = nullptr;
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Green" && height == 85) {
                            greenLand = paint;
                        } else if (family == "Desert" && height == 80) {
                            desertLand = paint;
                        }
                    }
                    if (greenLand != nullptr && desertLand != nullptr) {
                        for (const BrushPiece& piece : brushPieces_) {
                            if (!lowerText(piece.name).starts_with("blend-desertgreen-")) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(piece.sectionPath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                const int tiles = stamp.tileWidth * stamp.tileHeight;
                                for (int tile = 0; tile < tiles; ++tile) {
                                    const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                    const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                    const std::uint64_t hash = hashBrushTile(pixels);
                                    const auto existing = brushPictures_.find(hash);
                                    if (existing != brushPictures_.end() && !existing->second.fringe) {
                                        continue;
                                    }
                                    const int tx = tile % stamp.tileWidth;
                                    const int ty = tile / stamp.tileWidth;
                                    const BrushPaint* land = desertGreenTileIsDesert(pixels, lowerText(piece.name), tx, ty, stamp.tileWidth, stamp.tileHeight)
                                        ? desertLand : greenLand;
                                    brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
                // Lush grass and lava dirt meet across heights that also sit near other paints.
                // The picture keeps each tile on the land its color belongs to.
                const BrushPaint* lushLavaLand = nullptr;
                const BrushPaint* lavaLushGround = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Lush" && trailingNumber(paint->name) == 77) {
                        lushLavaLand = paint;
                    } else if (blendWorldName(paint->category) == "Lava" && trailingNumber(paint->name) == 86) {
                        lavaLushGround = paint;
                    }
                }
                if (lushLavaLand != nullptr && lavaLushGround != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-lush-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = lushLavaTileIsLush(pixels) ? lushLavaLand : lavaLushGround;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Ice and Mars share Base-85, so the picture tells the two lands apart.
                const BrushPaint* iceLand = nullptr;
                const BrushPaint* marsLand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Ice" && trailingNumber(paint->name) == 85) {
                        iceLand = paint;
                    } else if (blendWorldName(paint->category) == "Mars" && trailingNumber(paint->name) == 85) {
                        marsLand = paint;
                    }
                }
                if (iceLand != nullptr && marsLand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-icemars-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = iceMarsTileIsIce(pixels) ? iceLand : marsLand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Ice Base-85 and Lava Base-86 are one apart, too close for the height to choose, so the
                // picture tells the two lands apart: snow is white and blue, lava dirt is brown.
                const BrushPaint* iceLavaIce = nullptr;
                const BrushPaint* iceLavaLava = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Ice" && trailingNumber(paint->name) == 85) {
                        iceLavaIce = paint;
                    } else if (blendWorldName(paint->category) == "Lava" && trailingNumber(paint->name) == 86) {
                        iceLavaLava = paint;
                    }
                }
                if (iceLavaIce != nullptr && iceLavaLava != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-icelava-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = iceMarsTileIsIce(pixels) ? iceLavaIce : iceLavaLava;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Ice Base-85 and Archipelago Base-78 tiles: snow is white and blue, sand is cream.
                const BrushPaint* iceArchiIce = nullptr;
                const BrushPaint* iceArchiSand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Ice" && trailingNumber(paint->name) == 85) {
                        iceArchiIce = paint;
                    } else if (blendWorldName(paint->category) == "Archi" && trailingNumber(paint->name) == 78) {
                        iceArchiSand = paint;
                    }
                }
                if (iceArchiIce != nullptr && iceArchiSand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-icearchi-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = iceMarsTileIsIce(pixels) ? iceArchiIce : iceArchiSand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Ice Base-85 and Desert Base-80 sit in the same height band, so the colour
                // tells each tile's land. Snow is white and blue; Desert sand is orange.
                const BrushPaint* iceDesertIce = nullptr;
                const BrushPaint* iceDesertSand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Ice" && trailingNumber(paint->name) == 85) {
                        iceDesertIce = paint;
                    } else if (blendWorldName(paint->category) == "Desert" && trailingNumber(paint->name) == 80) {
                        iceDesertSand = paint;
                    }
                }
                if (iceDesertIce != nullptr && iceDesertSand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-icedesert-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                const auto existing = brushPictures_.find(hash);
                                // A picture that is plain ground keeps its reading; a blend reading by height is replaced.
                                if (existing != brushPictures_.end() && !existing->second.fringe) {
                                    continue;
                                }
                                const BrushPaint* land = iceMarsTileIsIce(pixels) ? iceDesertIce : iceDesertSand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                const auto paintWorldAt = [&](const char* family, int height) -> const BrushPaint* {
                    for (const BrushPaint* paint : pictured) {
                        if (blendWorldName(paint->category) == family && trailingNumber(paint->name) == height) {
                            return paint;
                        }
                    }
                    return nullptr;
                };
                // Every pair of lands read here with a colour test from core/BlendColours.hpp is listed in its
                // kBlendColourPairs, which format_test checks against both lands' base tiles.
                const auto rememberTwoWayColour = [&](const char* prefix, const BrushPaint* landA, const BrushPaint* landB,
                                                      bool (*isA)(const std::uint8_t*)) {
                    keepGoing();
                    if (landA == nullptr || landB == nullptr) {
                        return;
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with(prefix)) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                const auto existing = brushPictures_.find(hash);
                                if (existing != brushPictures_.end() && !existing->second.fringe) {
                                    continue;
                                }
                                const BrushPaint* land = isA(pixels) ? landA : landB;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                };
                const auto rememberTwoWayRim = [&](const char* prefix, const BrushPaint* unmarked, const BrushPaint* marked,
                                                   bool (*unmarkedIf)(const std::uint8_t*, std::string_view, int, int, int, int)) {
                    keepGoing();
                    if (unmarked == nullptr || marked == nullptr) {
                        return;
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        if (!lowered.starts_with(prefix)) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                const auto existing = brushPictures_.find(hash);
                                if (existing != brushPictures_.end() && !existing->second.fringe) {
                                    continue;
                                }
                                const int tx = tile % stamp.tileWidth;
                                const int ty = tile / stamp.tileWidth;
                                const BrushPaint* land = unmarkedIf(pixels, lowered, tx, ty, stamp.tileWidth, stamp.tileHeight)
                                    ? unmarked : marked;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                };
                rememberTwoWayRim("blend-lushacid-", paintWorldAt("Lush", 77), paintWorldAt("Acid", 85), lushAcidTileIsLush);
                rememberTwoWayRim("blend-lushcrystal-", paintWorldAt("Lush", 77), paintWorldAt("Crystal", 85), lushCrystalTileIsLush);
                rememberTwoWayColour("blend-archimars-", paintWorldAt("Archi", 78), paintWorldAt("Mars", 85), archiAcidTileIsSand);
                rememberTwoWayColour("blend-archimetal-", paintWorldAt("Archi", 78), paintWorldAt("Metal", 85), archiAcidTileIsSand);
                rememberTwoWayColour("blend-lushmetal-", paintWorldAt("Lush", 77), paintWorldAt("Metal", 85), greenMetalTileIsGrass);
                rememberTwoWayColour("blend-desertlush-", paintWorldAt("Desert", 80), paintWorldAt("Lush", 77), desertLavaTileIsDesert);
                rememberTwoWayColour("blend-metallava-", paintWorldAt("Metal", 85), paintWorldAt("Lava", 86), metalLavaTileIsMetal);
                rememberTwoWayRim("blend-greencrystal-", paintWorldAt("Green", 85), paintWorldAt("Crystal", 85), greenCrystalTileIsGreen);
                rememberTwoWayRim("blend-acidmars-", paintWorldAt("Mars", 85), paintWorldAt("Acid", 85), acidMarsTileIsMars);
                rememberTwoWayColour("blend-iceacid-", paintWorldAt("Ice", 85), paintWorldAt("Acid", 85), iceMarsTileIsIce);
                rememberTwoWayRim("blend-acidmetal-", paintWorldAt("Metal", 85), paintWorldAt("Acid", 85), acidMetalTileIsMetal);
                rememberTwoWayRim("blend-crystalmars-", paintWorldAt("Mars", 85), paintWorldAt("Crystal", 85), crystalMarsTileIsMars);
                rememberTwoWayRim("blend-marsmetal-", paintWorldAt("Mars", 85), paintWorldAt("Metal", 85), marsMetalTileIsMars);
                rememberTwoWayRim("blend-desertmars-", paintWorldAt("Mars", 85), paintWorldAt("Desert", 80), desertMarsTileIsMars);
                rememberTwoWayRim("blend-icecrystal-", paintWorldAt("Crystal", 85), paintWorldAt("Ice", 85), iceCrystalTileIsCrystal);
                rememberTwoWayColour("blend-icemetal-", paintWorldAt("Ice", 85), paintWorldAt("Metal", 85), iceMarsTileIsIce);
                rememberTwoWayRim("blend-crystalmetal-", paintWorldAt("Metal", 85), paintWorldAt("Crystal", 85), crystalMetalTileIsMetal);
                rememberTwoWayColour("blend-desertmetal-", paintWorldAt("Desert", 80), paintWorldAt("Metal", 85), desertLavaTileIsDesert);
                {
                    // GreenWorld, Ice and Metal share the blue water; the brush holds one of them.
                    const BrushPaint* blueWater = paintWorldAt("Green", 0);
                    blueWater = blueWater != nullptr ? blueWater : paintWorldAt("Ice", 0);
                    blueWater = blueWater != nullptr ? blueWater : paintWorldAt("Metal", 0);
                    rememberTwoWayColour("blend-watergreenmars-", blueWater, paintWorldAt("Mars", 0), greenMarsWaterTileIsGreen);
                    rememberTwoWayColour("blend-watergreencrystal-", blueWater, paintWorldAt("Crystal", 0), greenMarsWaterTileIsGreen);
                    const BrushPaint* sandWater = paintWorldAt("Desert", 0) != nullptr ? paintWorldAt("Desert", 0) : paintWorldAt("Archi", 0);
                    rememberTwoWayColour("blend-watergreendesert-", blueWater, sandWater, greenMarsWaterTileIsGreen);
                    rememberTwoWayColour("blend-watergreenlush-", blueWater, paintWorldAt("Lush", 27), greenMarsWaterTileIsGreen);
                    rememberTwoWayColour("blend-watergreenacid-", blueWater, paintWorldAt("Acid", 0), greenMarsWaterTileIsGreen);
                    // Archipelago and Desert water is greener than Acid, Lush, Crystal and Mars water.
                    rememberTwoWayColour("blend-waterdesertacid-", sandWater, paintWorldAt("Acid", 0), desertWaterTileIsDesert);
                    rememberTwoWayColour("blend-waterdesertlush-", sandWater, paintWorldAt("Lush", 27), desertWaterTileIsDesert);
                    rememberTwoWayColour("blend-waterdesertcrystal-", sandWater, paintWorldAt("Crystal", 0), desertWaterTileIsDesert);
                    rememberTwoWayColour("blend-waterdesertmars-", sandWater, paintWorldAt("Mars", 0), desertWaterTileIsDesert);
                    // Mars water is redder than Lush, Crystal and Acid water.
                    rememberTwoWayColour("blend-watermarslush-", paintWorldAt("Mars", 0), paintWorldAt("Lush", 27), marsWaterTileIsMars);
                    rememberTwoWayColour("blend-watermarscrystal-", paintWorldAt("Mars", 0), paintWorldAt("Crystal", 0), marsWaterTileIsMars);
                    rememberTwoWayColour("blend-watermarsacid-", paintWorldAt("Mars", 0), paintWorldAt("Acid", 0), marsWaterTileIsMars);
                    // Lush water is bluer than Crystal and Acid water.
                    rememberTwoWayColour("blend-waterlushcrystal-", paintWorldAt("Lush", 27), paintWorldAt("Crystal", 0), lushWaterTileIsLush);
                    rememberTwoWayColour("blend-waterlushacid-", paintWorldAt("Lush", 27), paintWorldAt("Acid", 0), lushWaterTileIsLush);
                    // Acid water is greener than Crystal water.
                    rememberTwoWayColour("blend-watercrystalacid-", paintWorldAt("Acid", 0), paintWorldAt("Crystal", 0), acidWaterTileIsAcid);
                }
                {
                    // Quagmire's blends into the Base-85 lands and into every other water. The brush holds one land
                    // of a water family, so the blue and the Archipelago and Desert water are found as any of theirs.
                    const BrushPaint* quagmireLand = paintWorldAt("Quag", 85);
                    for (const char* partner : {"Green", "Mars", "Acid", "Crystal", "Ice", "Metal"}) {
                        const std::string prefix = "blend-quag" + lowerText(partner) + "-";
                        rememberTwoWayColour(prefix.c_str(), quagmireLand, paintWorldAt(partner, 85), quagmireTileIsQuagmire);
                    }
                    const auto waterOf = [&](std::initializer_list<const char*> family, int height) -> const BrushPaint* {
                        for (const char* world : family) {
                            if (const BrushPaint* paint = paintWorldAt(world, height); paint != nullptr) {
                                return paint;
                            }
                        }
                        return nullptr;
                    };
                    // The lands the Evergreen grasses blend into, at their heights.
                    struct EvergreenPartner {
                        const char* word;
                        int height;
                    };
                    static constexpr EvergreenPartner kEvergreenLands[] = {
                        {"Green", 85}, {"Mars", 85}, {"Acid", 85}, {"Crystal", 85}, {"Ice", 85}, {"Metal", 85}, {"Quag", 85},
                        {"Lava", 86}, {"Archi", 78}, {"Lush", 77}, {"Desert", 80}, {"Ever2", 85}, {"Ever", 85},
                    };
                    const BrushPaint* evergreenLand = paintWorldAt("Ever", 85);
                    const BrushPaint* evergreen2Land = paintWorldAt("Ever2", 85);
                    for (const EvergreenPartner& partner : kEvergreenLands) {
                        if (std::string_view(partner.word) != "Ever") {
                            const std::string prefix = "blend-ever" + lowerText(partner.word) + "-";
                            rememberTwoWayColour(prefix.c_str(), evergreenLand, paintWorldAt(partner.word, partner.height), evergreenTileIsEvergreen);
                        }
                        if (std::string_view(partner.word) != "Ever" && std::string_view(partner.word) != "Ever2") {
                            const std::string prefix = "blend-ever2" + lowerText(partner.word) + "-";
                            rememberTwoWayColour(prefix.c_str(), evergreen2Land, paintWorldAt(partner.word, partner.height), evergreen2TileIsEvergreen2);
                        }
                    }
                    // SnowyEvergreen's blends into every other land's ground, both Evergreen grasses among them.
                    const BrushPaint* snowLand = paintWorldAt("Snow", 85);
                    for (const EvergreenPartner& partner : kEvergreenLands) {
                        const std::string prefix = "blend-snow" + lowerText(partner.word) + "-";
                        rememberTwoWayColour(prefix.c_str(), snowLand, paintWorldAt(partner.word, partner.height), snowTileIsSnow);
                    }
                    const auto landOf = [&](const std::string& word) -> const BrushPaint* {
                        if (word == "Snow") {
                            return snowLand;
                        }
                        for (const EvergreenPartner& partner : kEvergreenLands) {
                            if (word == partner.word) {
                                return paintWorldAt(partner.word, partner.height);
                            }
                        }
                        return nullptr;
                    };
                    // Their three-way pieces, and Quagmire's: each tile is the land of the three whose base it is
                    // nearest in colour. Any of the three lands may fill the side the name starts with.
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        if (!lowered.starts_with("blend-tri-")) {
                            continue;
                        }
                        const std::vector<std::string> pieceWorlds = blendPieceWorlds(piece.name);
                        const bool ours = std::any_of(pieceWorlds.begin(), pieceWorlds.end(), [](const std::string& world) {
                            return world == "Quagmire" || world == "Evergreen" || world == "Evergreen2" || world == "SnowyEvergreen" || world == "Metal2";
                        });
                        if (!ours) {
                            continue;
                        }
                        // Metal2's three-way pieces join three waters: each word is a water, at its own height.
                        const bool waters = std::find(pieceWorlds.begin(), pieceWorlds.end(), "Metal2") != pieceWorlds.end();
                        const auto waterLandOf = [&](const std::string& word) -> const BrushPaint* {
                            return word == "Green" ? waterOf({"Green", "Ice", "Metal"}, 0)
                                : word == "Desert" ? waterOf({"Desert", "Archi"}, 0)
                                : word == "Lush" ? paintWorldAt("Lush", 27) : paintWorldAt(word.c_str(), 0);
                        };
                        std::vector<const BrushPaint*> triLands;
                        std::vector<std::array<double, 3>> means;
                        for (const std::string& world : pieceWorlds) {
                            const BrushPaint* land = waters ? waterLandOf(blendWorldName(world)) : landOf(blendWorldName(world));
                            const PaletteImage* ground = land != nullptr ? brushGround(land->category, land->name) : nullptr;
                            if (ground == nullptr || ground->pixels.empty()) {
                                triLands.clear();
                                break;
                            }
                            std::array<double, 3> mean = {0, 0, 0};
                            for (const std::uint8_t index : ground->pixels) {
                                for (int channel = 0; channel < 3; ++channel) {
                                    mean[static_cast<std::size_t>(channel)] += kTaPalette[index][channel];
                                }
                            }
                            for (double& channel : mean) {
                                channel /= static_cast<double>(ground->pixels.size());
                            }
                            triLands.push_back(land);
                            means.push_back(mean);
                        }
                        if (triLands.size() != 3) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            for (int tile = 0; tile < stamp.tileWidth * stamp.tileHeight; ++tile) {
                                const std::size_t bytes = static_cast<std::size_t>(stamp.indices[static_cast<std::size_t>(tile)]) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (const auto existing = brushPictures_.find(hash); existing != brushPictures_.end() && !existing->second.fringe) {
                                    continue;
                                }
                                std::array<double, 3> tileMean = {0, 0, 0};
                                for (int i = 0; i < 1024; ++i) {
                                    for (int channel = 0; channel < 3; ++channel) {
                                        tileMean[static_cast<std::size_t>(channel)] += kTaPalette[pixels[i]][channel] / 1024.0;
                                    }
                                }
                                std::size_t nearest = 0;
                                double nearestDistance = 1.0e30;
                                for (std::size_t i = 0; i < means.size(); ++i) {
                                    double distance = 0;
                                    for (int channel = 0; channel < 3; ++channel) {
                                        const double gap = tileMean[static_cast<std::size_t>(channel)] - means[i][static_cast<std::size_t>(channel)];
                                        distance += gap * gap;
                                    }
                                    if (distance < nearestDistance) {
                                        nearestDistance = distance;
                                        nearest = i;
                                    }
                                }
                                brushPictures_[hash] = BrushPictureHint{triLands[nearest]->category, triLands[nearest]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                    const BrushPaint* quagmireWater = paintWorldAt("Quag", 0);
                    rememberTwoWayColour("blend-waterquaggreen-", quagmireWater, waterOf({"Green", "Ice", "Metal"}, 0), quagmireWaterTileIsQuagmire);
                    rememberTwoWayColour("blend-waterquagdesert-", quagmireWater, waterOf({"Desert", "Archi"}, 0), quagmireWaterTileIsQuagmire);
                    rememberTwoWayColour("blend-waterquagmars-", quagmireWater, paintWorldAt("Mars", 0), quagmireWaterTileIsQuagmire);
                    rememberTwoWayColour("blend-waterquagcrystal-", quagmireWater, paintWorldAt("Crystal", 0), quagmireWaterTileIsQuagmire);
                    rememberTwoWayColour("blend-waterquagacid-", quagmireWater, paintWorldAt("Acid", 0), quagmireWaterTileIsQuagmire);
                    rememberTwoWayColour("blend-waterquaglush-", quagmireWater, paintWorldAt("Lush", 27), quagmireWaterTileIsQuagmire);
                    // Evergreen water's, into the same waters and Quagmire's.
                    const BrushPaint* evergreenWater = paintWorldAt("Ever", 0);
                    rememberTwoWayColour("blend-waterevergreen-", evergreenWater, waterOf({"Green", "Ice", "Metal"}, 0), evergreenWaterTileIsEvergreen);
                    rememberTwoWayColour("blend-watereverdesert-", evergreenWater, waterOf({"Desert", "Archi"}, 0), evergreenWaterTileIsEvergreen);
                    rememberTwoWayColour("blend-waterevermars-", evergreenWater, paintWorldAt("Mars", 0), evergreenWaterTileIsEvergreen);
                    rememberTwoWayColour("blend-waterevercrystal-", evergreenWater, paintWorldAt("Crystal", 0), evergreenWaterTileIsEvergreen);
                    rememberTwoWayColour("blend-watereveracid-", evergreenWater, paintWorldAt("Acid", 0), evergreenWaterTileIsEvergreen);
                    rememberTwoWayColour("blend-watereverlush-", evergreenWater, paintWorldAt("Lush", 27), evergreenWaterTileIsEvergreen);
                    rememberTwoWayColour("blend-watereverquag-", evergreenWater, paintWorldAt("Quag", 0), evergreenWaterTileIsEvergreen);
                    // WaterWorld's water's, into every other water. It is told from them by how blue it is.
                    const BrushPaint* waterWorldWater = paintWorldAt("WWorld", 0);
                    rememberTwoWayColour("blend-waterwworldgreen-", waterWorldWater, waterOf({"Green", "Ice", "Metal"}, 0), waterWorldTileIsWaterWorld);
                    rememberTwoWayColour("blend-waterwworlddesert-", waterWorldWater, waterOf({"Desert", "Archi"}, 0), waterWorldTileIsWaterWorld);
                    for (const auto& [word, height] : {std::pair<const char*, int>{"Mars", 0}, {"Crystal", 0}, {"Acid", 0}, {"Lush", 27}, {"Quag", 0}, {"Metal2", 0}}) {
                        const std::string prefix = "blend-waterwworld" + lowerText(word) + "-";
                        rememberTwoWayColour(prefix.c_str(), waterWorldWater, paintWorldAt(word, height), waterWorldTileIsWaterWorld);
                    }
                    // The Evergreen pieces also serve SnowyEvergreen's water, the same water, which the Snow brush holds instead.
                    rememberTwoWayColour("blend-waterwworldever-", waterWorldWater, waterOf({"Ever", "Snow"}, 0), waterWorldTileIsWaterWorld);
                    // Metal's second water's, into every other water. Its nearly black water is told from them by brightness.
                    const BrushPaint* metal2Water = paintWorldAt("Metal2", 0);
                    rememberTwoWayColour("blend-watermetal2green-", metal2Water, waterOf({"Green", "Ice", "Metal"}, 0), metal2WaterTileIsMetal2);
                    rememberTwoWayColour("blend-watermetal2desert-", metal2Water, waterOf({"Desert", "Archi"}, 0), metal2WaterTileIsMetal2);
                    for (const auto& [word, height] : kMetal2TriWaters) {
                        if (std::string_view(word) == "Green" || std::string_view(word) == "Desert") {
                            continue;
                        }
                        const std::string prefix = "blend-watermetal2" + lowerText(word) + "-";
                        rememberTwoWayColour(prefix.c_str(), metal2Water, paintWorldAt(word, height), metal2WaterTileIsMetal2);
                    }
                }
                // Ice and Lava three-way tiles sit at 78, 85 and 86, too close for the height to choose,
                // so the picture tells each tile's land. The third land is GreenWorld, Archipelago, or Mars.
                if (iceLavaIce != nullptr && iceLavaLava != nullptr) {
                    const BrushPaint* thirds[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Green" && height == 85) {
                            thirds[0] = paint;
                        } else if (family == "Archi" && height == 78) {
                            thirds[1] = paint;
                        } else if (family == "Mars" && height == 85) {
                            thirds[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        if (!lowered.starts_with("blend-tri-") || lowered.starts_with("blend-tri-acid-") || lowered.find("lush") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("acid") != std::string::npos
                            || lowered.find("desert") != std::string::npos || lowered.find("metal") != std::string::npos
                            || (lowered.find("grass") != std::string::npos && lowered.find("mars") != std::string::npos)
                            || (lowered.find("grass") != std::string::npos && lowered.find("archi") != std::string::npos)
                            || (lowered.find("archi") != std::string::npos && lowered.find("mars") != std::string::npos)) {
                            continue;
                        }
                        // The third land is in the name: Blend-Tri-Ice-T-Archi-L or Blend-Tri-Mars-T-Ice-L.
                        const char* third = lowered.find("grass") != std::string::npos ? "grass"
                            : lowered.find("archi") != std::string::npos ? "archi"
                            : lowered.find("mars") != std::string::npos ? "mars" : nullptr;
                        const BrushPaint* thirdPaint = third == nullptr ? nullptr
                            : thirds[std::string_view(third) == "grass" ? 0 : std::string_view(third) == "archi" ? 1 : 2];
                        if (thirdPaint == nullptr) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triIceTileLand(pixels, third);
                                const BrushPaint* land = which == 0 ? iceLavaIce : which == 1 ? thirdPaint : iceLavaLava;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Crystal Base-85 and Lava Base-86 are one apart, too close for the height to choose,
                // so the picture tells the two lands apart.
                const BrushPaint* crystalLand = nullptr;
                const BrushPaint* crystalLava = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Crystal" && trailingNumber(paint->name) == 85) {
                        crystalLand = paint;
                    } else if (blendWorldName(paint->category) == "Lava" && trailingNumber(paint->name) == 86) {
                        crystalLava = paint;
                    }
                }
                if (crystalLand != nullptr && crystalLava != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-crystallava-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = crystalLavaTileIsCrystal(pixels) ? crystalLand : crystalLava;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // GreenWorld Base-85 and Lush Base-77 tiles in their blend: the colour of the grass tells
                // them apart, since the blend's heights ramp between the two.
                const BrushPaint* greenLushGreen = nullptr;
                const BrushPaint* greenLushLush = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Green" && trailingNumber(paint->name) == 85) {
                        greenLushGreen = paint;
                    } else if (blendWorldName(paint->category) == "Lush" && trailingNumber(paint->name) == 77) {
                        greenLushLush = paint;
                    }
                }
                if (greenLushGreen != nullptr && greenLushLush != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-greenlush-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = greenLushTileIsGreen(pixels) ? greenLushGreen : greenLushLush;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lush Base-77 and Mars Base-85 tiles in their blend: the heights ramp between the two, so
                // the colour tells them apart.
                const BrushPaint* lushMarsLush = nullptr;
                const BrushPaint* lushMarsMars = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Lush" && trailingNumber(paint->name) == 77) {
                        lushMarsLush = paint;
                    } else if (blendWorldName(paint->category) == "Mars" && trailingNumber(paint->name) == 85) {
                        lushMarsMars = paint;
                    }
                }
                if (lushMarsLush != nullptr && lushMarsMars != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-lushmars-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = lushMarsTileIsMars(pixels) ? lushMarsMars : lushMarsLush;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, GreenWorld and Lush three-way tiles sit at 77, 85 and 86, and their heights ramp, so the
                // picture tells each tile's land.
                {
                    const BrushPaint* triLava = nullptr;
                    const BrushPaint* triGrass = nullptr;
                    const BrushPaint* triLush = nullptr;
                    const BrushPaint* triMars = nullptr;
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            triLava = paint;
                        } else if (family == "Green" && height == 85) {
                            triGrass = paint;
                        } else if (family == "Lush" && height == 77) {
                            triLush = paint;
                        } else if (family == "Mars" && height == 85) {
                            triMars = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        if (!lowered.starts_with("blend-tri-") || lowered.find("lush") == std::string::npos
                            || lowered.find("ice") != std::string::npos || lowered.find("desert") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("crystal") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        // A Lush three-way name with Mars in it is the Lava, Lush and Mars set.
                        const bool marsSet = lowered.find("mars") != std::string::npos;
                        const BrushPaint* triThird = marsSet ? triMars : triGrass;
                        if (triLava == nullptr || triThird == nullptr || triLush == nullptr) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triLushTileLand(pixels, marsSet);
                                const BrushPaint* land = which == 0 ? triLava : which == 1 ? triThird : triLush;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Archipelago and Crystal blend tiles, and Lava, Crystal and Archipelago three-way tiles: the
                // colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[1] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool twoWay = lowered.starts_with("blend-archicrystal-");
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("crystal") != std::string::npos
                            && lowered.find("acid") == std::string::npos && lowered.find("desert") == std::string::npos
                            && lowered.find("grass") == std::string::npos && lowered.find("ice") == std::string::npos
                            && lowered.find("lush") == std::string::npos && lowered.find("mars") == std::string::npos
                            && lowered.find("metal") == std::string::npos;
                        if ((!twoWay && !threeWay) || lands[1] == nullptr || lands[2] == nullptr || (threeWay && lands[0] == nullptr)) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                int which = crystalArchiTileLand(pixels);
                                if (twoWay && which == 0) {
                                    which = 2;
                                }
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Acid Base-85 and Crystal Base-85, and Acid, Crystal and Lava three-way tiles: the colour
                // tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool twoWay = lowered.starts_with("blend-acidcrystal-");
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("acid") != std::string::npos
                            && (lowered.find("crystal") != std::string::npos || lowered.starts_with("blend-tri-lava-"));
                        if ((!twoWay && !threeWay) || lands[1] == nullptr || lands[2] == nullptr || (threeWay && lands[0] == nullptr)) {
                            continue;
                        }
                        if (threeWay && (lowered.find("grass") != std::string::npos || lowered.find("desert") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("lush") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos)) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                int which = twoWay ? (acidCrystalTileIsCrystal(pixels) ? 2 : 1) : triAcidCrystalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Acid, Desert and Lava three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && (lowered.find("acid") != std::string::npos || lowered.starts_with("blend-tri-lava-"));
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("crystal") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("lush") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triAcidDesertTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Crystal and Desert three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("crystal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("grass") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("ice") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("mars") != std::string::npos || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triCrystalDesertTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, GreenWorld and Desert three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Green" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("grass") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("archi") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("mars") != std::string::npos || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triGreenDesertTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Archipelago and Desert three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[1] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("archi") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("ice") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triArchiDesertTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Ice and Desert three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Ice" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("ice") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("mars") != std::string::npos || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triIceDesertTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, GreenWorld and Mars three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Green" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Mars" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("grass") != std::string::npos
                            && lowered.find("mars") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("ice") != std::string::npos || lowered.find("acid") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("crystal") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triGreenMarsTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Ice and Lush three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Ice" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Lush" && height == 77) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("ice") != std::string::npos
                            && lowered.find("lush") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("mars") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("acid") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triIceLushTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Ice and Crystal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Ice" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("ice") != std::string::npos
                            && lowered.find("crystal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("grass") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triIceCrystalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Ice and Acid three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Ice" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("ice") != std::string::npos
                            && lowered.find("acid") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triIceAcidTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Desert and Lush three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Lush" && height == 77) {
                            lands[1] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("lush") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("grass") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("mars") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("acid") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triDesertLushTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, GreenWorld and Crystal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Green" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("grass") != std::string::npos
                            && lowered.find("crystal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("ice") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triGreenCrystalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Archipelago and GreenWorld three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Green" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("grass") != std::string::npos
                            && lowered.find("archi") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("mars") != std::string::npos || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triArchiGreenTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Archipelago and Lush three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Lush" && height == 77) {
                            lands[1] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("lush") != std::string::npos
                            && lowered.find("archi") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triArchiLushTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Crystal and Lush three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Lush" && height == 77) {
                            lands[1] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("lush") != std::string::npos
                            && lowered.find("crystal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triLushCrystalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Desert and Mars three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[1] = paint;
                        } else if (family == "Mars" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("mars") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("lush") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("crystal") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triDesertMarsTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Acid and Lush three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Lush" && height == 77) {
                            lands[1] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("lush") != std::string::npos
                            && lowered.find("acid") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triAcidLushTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Acid and Mars three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Mars" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("acid") != std::string::npos
                            && lowered.find("mars") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triAcidMarsTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Archipelago and Mars three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[1] = paint;
                        } else if (family == "Mars" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("archi") != std::string::npos
                            && lowered.find("mars") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triArchiMarsTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Crystal and Mars three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Mars" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("crystal") != std::string::npos
                            && lowered.find("mars") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triCrystalMarsTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, GreenWorld and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Green" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("grass") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("mars") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triGreenMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Desert and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Desert" && height == 80) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("desert") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("ice") != std::string::npos || lowered.find("acid") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("crystal") != std::string::npos
                            || lowered.find("lush") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("grass") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triDesertMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Archipelago and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("archi") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("mars") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triArchiMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Lush and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Lush" && height == 77) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("lush") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("mars") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triLushMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Ice and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Ice" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("ice") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("mars") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triIceMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Acid and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("acid") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("archi") != std::string::npos || lowered.find("crystal") != std::string::npos
                            || lowered.find("lush") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("mars") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triAcidMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Crystal and Metal three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Crystal" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Metal" && height == 85) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("crystal") != std::string::npos
                            && lowered.find("metal") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("acid") != std::string::npos || lowered.find("archi") != std::string::npos
                            || lowered.find("lush") != std::string::npos || lowered.find("grass") != std::string::npos
                            || lowered.find("mars") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triCrystalMetalTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Lava, Acid and Archipelago three-way tiles: the colour tells each tile's land.
                {
                    const BrushPaint* lands[3] = {};
                    for (const BrushPaint* paint : pictured) {
                        const std::string family = blendWorldName(paint->category);
                        const int height = trailingNumber(paint->name);
                        if (family == "Lava" && height == 86) {
                            lands[0] = paint;
                        } else if (family == "Acid" && height == 85) {
                            lands[1] = paint;
                        } else if (family == "Archi" && height == 78) {
                            lands[2] = paint;
                        }
                    }
                    for (const BrushPiece& piece : brushPieces_) {
                        const std::string lowered = lowerText(piece.name);
                        const bool threeWay = lowered.starts_with("blend-tri-") && lowered.find("acid") != std::string::npos
                            && lowered.find("archi") != std::string::npos;
                        if (!threeWay || lands[0] == nullptr || lands[1] == nullptr || lands[2] == nullptr
                            || lowered.find("desert") != std::string::npos || lowered.find("ice") != std::string::npos
                            || lowered.find("crystal") != std::string::npos || lowered.find("lush") != std::string::npos
                            || lowered.find("grass") != std::string::npos || lowered.find("mars") != std::string::npos
                            || lowered.find("metal") != std::string::npos) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const int which = triAcidArchiTileLand(pixels);
                                brushPictures_[hash] = BrushPictureHint{lands[which]->category, lands[which]->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Desert Base-80 is really 81 to 90 high, so by height its sand is often nearer Lava Base-86
                // than Base-80. The Desert and Lava blend tiles are told apart by their colour instead, and
                // that replaces any reading by height they were given above.
                const BrushPaint* desertSand = nullptr;
                const BrushPaint* desertLava = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Desert" && trailingNumber(paint->name) == 80) {
                        desertSand = paint;
                    } else if (blendWorldName(paint->category) == "Lava" && trailingNumber(paint->name) == 86) {
                        desertLava = paint;
                    }
                }
                if (desertSand != nullptr && desertLava != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-desert-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                const auto existing = brushPictures_.find(hash);
                                // A picture that is plain ground keeps its reading, and a picture several Desert
                                // bases share stays shared; only a blend reading by height is replaced.
                                if ((existing != brushPictures_.end() && !existing->second.fringe) || sharedPictures_.count(hash) != 0) {
                                    continue;
                                }
                                const BrushPaint* land = desertLavaTileIsDesert(pixels) ? desertSand : desertLava;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // GreenWorld and Metal share Base-85, so the picture tells the two lands apart.
                const BrushPaint* greenMetalGrass = nullptr;
                const BrushPaint* greenMetalMetal = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Green" && trailingNumber(paint->name) == 85) {
                        greenMetalGrass = paint;
                    } else if (blendWorldName(paint->category) == "Metal" && trailingNumber(paint->name) == 85) {
                        greenMetalMetal = paint;
                    }
                }
                if (greenMetalGrass != nullptr && greenMetalMetal != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-greenmetal-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = greenMetalTileIsGrass(pixels) ? greenMetalGrass : greenMetalMetal;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // GreenWorld and Mars share Base-85, so the picture tells the two lands apart.
                const BrushPaint* greenMarsGrass = nullptr;
                const BrushPaint* greenMarsMars = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Green" && trailingNumber(paint->name) == 85) {
                        greenMarsGrass = paint;
                    } else if (blendWorldName(paint->category) == "Mars" && trailingNumber(paint->name) == 85) {
                        greenMarsMars = paint;
                    }
                }
                if (greenMarsGrass != nullptr && greenMarsMars != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-greenmars-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = greenMarsTileIsGrass(pixels) ? greenMarsGrass : greenMarsMars;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // Ice and GreenWorld share Base-85, so the picture tells the two lands apart.
                const BrushPaint* iceGreenLand = nullptr;
                const BrushPaint* greenIceLand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Ice" && trailingNumber(paint->name) == 85) {
                        iceGreenLand = paint;
                    } else if (blendWorldName(paint->category) == "Green" && trailingNumber(paint->name) == 85) {
                        greenIceLand = paint;
                    }
                }
                if (iceGreenLand != nullptr && greenIceLand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-icegreen-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = iceGreenTileIsIce(pixels) ? iceGreenLand : greenIceLand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                // The lush side of this blend sits at Base-77 and the ice side ramps up to
                // Base-85, so height alone would call the slope the wrong land.
                const BrushPaint* lushIceLand = nullptr;
                const BrushPaint* iceLushLand = nullptr;
                for (const BrushPaint* paint : pictured) {
                    if (blendWorldName(paint->category) == "Lush" && trailingNumber(paint->name) == 77) {
                        lushIceLand = paint;
                    } else if (blendWorldName(paint->category) == "Ice" && trailingNumber(paint->name) == 85) {
                        iceLushLand = paint;
                    }
                }
                if (lushIceLand != nullptr && iceLushLand != nullptr) {
                    for (const BrushPiece& piece : brushPieces_) {
                        if (!lowerText(piece.name).starts_with("blend-lushice-")) {
                            continue;
                        }
                        try {
                            const HpiFile* file = source->archive().find(piece.sectionPath);
                            if (file == nullptr) {
                                continue;
                            }
                            const SectionStamp stamp = readStamp(*file);
                            const int tiles = stamp.tileWidth * stamp.tileHeight;
                            for (int tile = 0; tile < tiles; ++tile) {
                                const std::uint32_t image = stamp.indices[static_cast<std::size_t>(tile)];
                                const std::size_t bytes = static_cast<std::size_t>(image) * 1024;
                                if (bytes + 1024 > stamp.tileImages.size()) {
                                    continue;
                                }
                                const std::uint8_t* pixels = stamp.tileImages.data() + bytes;
                                const std::uint64_t hash = hashBrushTile(pixels);
                                if (brushPictures_.find(hash) != brushPictures_.end()) {
                                    continue;
                                }
                                const BrushPaint* land = lushIceTileIsLush(pixels) ? lushIceLand : iceLushLand;
                                brushPictures_[hash] = BrushPictureHint{land->category, land->name, false, {}, {}};
                            }
                        } catch (const std::exception&) {
                        }
                    }
                }
                for (const BrushPiece& piece : brushPieces_) {
                    const std::string lowered = lowerText(piece.name);
                    constexpr std::string_view prefix = "coast-";
                    if (!lowered.starts_with(prefix) || lowered.starts_with("coast-land-") || lowered.starts_with("coast-water-")) {
                        continue;
                    }
                    const std::size_t dash = piece.name.find('-', prefix.size());
                    if (dash != std::string::npos && dash > prefix.size()) {
                        brushCoastLiquid_ = piece.name.substr(prefix.size(), dash - prefix.size());
                        break;
                    }
                }
                // Pictures that several bases of one land share, such as Desert Base-80, Base-155 and Base-230
                // sand, belong to none of them: the height chooses. The shared-height path above can leave such a
                // picture claimed by whichever base came last, so every pair of a land's bases is checked here,
                // whichever path ran. It runs after every blend check, so none of them can claim such a picture.
                {
                    std::unordered_map<std::uint64_t, std::vector<std::pair<std::string, std::string>>> users;
                    for (const BrushPaint* paint : pictured) {
                        for (const TilesetSection& section : source->sections) {
                            if (lowerText(section.category) != lowerText(paint->category) || !isBaseBrushTile(section.world, section.name)
                                || lowerText(section.name) != lowerText(paint->name)) {
                                continue;
                            }
                            try {
                                const HpiFile* file = source->archive().find(section.archivePath);
                                if (file == nullptr) {
                                    continue;
                                }
                                const SectionStamp stamp = readStamp(*file);
                                for (int tile = 0; tile < stamp.tileWidth * stamp.tileHeight; ++tile) {
                                    const std::size_t bytes = static_cast<std::size_t>(stamp.indices[static_cast<std::size_t>(tile)]) * 1024;
                                    if (bytes + 1024 > stamp.tileImages.size()) {
                                        continue;
                                    }
                                    auto& list = users[hashBrushTile(stamp.tileImages.data() + bytes)];
                                    bool listed = false;
                                    for (const auto& [category, name] : list) {
                                        listed = listed || (lowerText(category) == lowerText(paint->category) && lowerText(name) == lowerText(paint->name));
                                    }
                                    if (!listed) {
                                        list.emplace_back(paint->category, paint->name);
                                    }
                                }
                            } catch (const std::exception&) {
                            }
                        }
                    }
                    for (const auto& [hash, list] : users) {
                        if (list.size() < 2) {
                            continue;
                        }
                        // Only bases of one land share this way; two lands that happen to share a picture keep
                        // whatever reading they were given.
                        bool oneLand = true;
                        for (const auto& [category, name] : list) {
                            oneLand = oneLand && lowerText(category) == lowerText(list.front().first);
                        }
                        if (!oneLand) {
                            continue;
                        }
                        sharedPictures_[hash] = list;
                        brushPictures_.erase(hash);
                    }
                }
                }
            saveBrushPictureCache(pictureCache);
            // The next build of this tileset, after the tileset or the editor changes, measures its progress by this.
            {
                std::ofstream reads(readsPath);
                reads << done->load();
            }
            } catch (const CatalogueCancelled&) {
            }
        };
    }
    }
    // Rivers and bridges get a button of their own after the base tiles. Their pictures
    // are the first horizontal piece of each.
    const auto addRiverButton = [&](const char* buttonName, const char* iconSection, BrushKind kind, std::string_view folder = {}) {
        const RiverSection* chosen = nullptr;
        for (const RiverSection& river : riverSections_) {
            if (sameRiverKey(river.name, iconSection) && (folder.empty() || lowerText(river.category) == lowerText(folder))) {
                chosen = &river;
                break;
            }
        }
        // Mars has the four corner singles and no Slope-High-B, so that picture stands in for the button.
        if (chosen == nullptr && kind == BrushKind::Slope) {
            for (const RiverSection& river : riverSections_) {
                const std::string key = lowerText(riverKeyOf(river.name));
                if (key.starts_with("single-high-") || key.starts_with("slope-")) {
                    chosen = &river;
                    break;
                }
            }
        }
        if (chosen == nullptr) {
            return;
        }
        try {
            const HpiFile* file = source->archive().find(chosen->sectionPath);
            if (file == nullptr) {
                return;
            }
            const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
            int width = 0;
            int height = 0;
            const std::vector<std::uint8_t> rgba = renderSectionRgba(stamp, 128, width, height);
            BrushPaint paint;
            paint.name = buttonName;
            paint.kind = kind;
            paint.preview = uploadTexture(rgba.data(), width, height, false);
            if (paint.preview != 0) {
                brushPaints_.push_back(std::move(paint));
            }
        } catch (const std::exception&) {
        }
    };
    if (isEvergreenWorld(brushWorld_)) {
        // Each Evergreen grass has its own rivers and bridges, in Rivers/Evergreen and Rivers/Evergreen2.
        addRiverButton("River 85-1", "River-Horizontal-01", BrushKind::River, "Evergreen");
        addRiverButton("River Bridge 85-1", "RiverB-Horizontal-01", BrushKind::Bridge, "Evergreen");
        addRiverButton("River 85-2", "River-Horizontal-01", BrushKind::River, "Evergreen2");
        addRiverButton("River Bridge 85-2", "RiverB-Horizontal-01", BrushKind::Bridge, "Evergreen2");
    } else {
        addRiverButton("River", "River-Horizontal-01", BrushKind::River);
        addRiverButton("River Bridge", "RiverB-Horizontal-01", BrushKind::Bridge);
    }
    // Metal's bridges over its dark water, once the tileset has them.
    if (isMetalWorld(brushWorld_)
        && std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.name, "Bridge139-R-L"); })) {
        addRiverButton("Bridge 139", "Bridge139-R-L", BrushKind::Bridge);
    }
    // Metal's walls, once the tileset has them.
    for (const auto& [wallButton, wallIcon] : {std::pair<const char*, const char*>{"Wall 85", "Wall85-R-L"}, {"Wall 139", "Wall139-R-L"}}) {
        const std::string icon = wallIcon;
        if (isMetalWorld(brushWorld_)
            && std::any_of(riverSections_.begin(), riverSections_.end(), [&](const RiverSection& river) { return sameRiverKey(river.name, icon); })) {
            addRiverButton(wallButton, wallIcon, BrushKind::Ridge);
        }
    }
    addRiverButton("Ridge", "Ridge-Horizontal-01", BrushKind::Ridge);
    addRiverButton("Ridge Spacer", "Ridge-L_R-Spacer-01", BrushKind::Spacer);
    // Metal's slope sections are the walkable Base-85 to Base-219 cliffs. The other
    // tilesets keep the shorter Slope button.
    const bool metalSlopes = isMetalWorld(brushWorld_);
    const bool archipelagoSlopes = lowerText(brushWorld_) == "archipelago";
    if (archipelagoSlopes) {
        addRiverButton("Slope 78 to 194", "Slope-202-High-B-01", BrushKind::Slope);
    } else if (lowerText(brushWorld_) == "desert") {
        addRiverButton("Slope 80 to 155", "Single-155-High-B", BrushKind::Slope);
        addRiverButton("Slope 155 to 230", "Single-230-High-B", BrushKind::Slope);
    } else if (lowerText(brushWorld_) == "lush") {
        addRiverButton("Slope 77 to 183", "Single-183-High-B", BrushKind::Slope);
    } else if (lowerText(brushWorld_) == "snowyevergreen") {
        // SnowyEvergreen's ramps, once the tileset has them.
        // The pieces are numbered, Single-189-High-B-01 and -02, so this checks the name without its number.
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-189-High-B"); })) {
            addRiverButton("Slope 189", "Single-189-High-B-01", BrushKind::Slope);
        }
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-Snow255-High-B"); })) {
            addRiverButton("Slope 255", "Single-Snow255-High-B-01", BrushKind::Slope);
        }
    } else if (isEvergreenWorld(brushWorld_)) {
        // Evergreen's ramps from Base-85-1 up to Base-190-2, once the tileset has them. Their pieces are numbered too.
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-190-High-B"); })) {
            addRiverButton(kEvergreenRampButton, "Single-190-High-B-01", BrushKind::Slope);
        }
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-Ever255-High-B"); })) {
            addRiverButton(kEvergreenPeakRampButton, "Single-Ever255-High-B-01", BrushKind::Slope);
        }
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-EG2-High-B"); })) {
            addRiverButton(kEvergreen2RampButton, "Single-EG2-High-B-01", BrushKind::Slope);
        }
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-Stone255-High-B"); })) {
            addRiverButton(kEvergreen2PeakRampButton, "Single-Stone255-High-B-01", BrushKind::Slope);
        }
        if (std::any_of(riverSections_.begin(), riverSections_.end(), [](const RiverSection& river) { return sameRiverKey(river.key, "Single-Ever190-High-B"); })) {
            addRiverButton(kEvergreenPlateauRampButton, "Single-Ever190-High-B-01", BrushKind::Slope);
        }
    } else {
        addRiverButton(metalSlopes ? "Slope 85 to 219" : "Slope", metalSlopes ? "Slope-High-B" : "Slope-High-B-01", BrushKind::Slope);
    }
    if (metalSlopes) {
        addRiverButton("Slope 139 to 255", "Single-255-High-B", BrushKind::Slope);
        // The Ramp button only shows once the tileset has Metal's ramps, Single-Ramp-High-B and the rest.
        const bool hasRamps = std::any_of(riverSections_.begin(), riverSections_.end(),
            [](const RiverSection& river) { return sameRiverKey(river.name, "Single-Ramp-High-B"); });
        if (hasRamps) {
            addRiverButton("Ramp", "Single-Ramp-High-B", BrushKind::Slope);
        }
    }
    const bool greenHills = lowerText(brushWorld_) == "greenworld";
    const bool lavaHills = lowerText(brushWorld_) == "lava";
    const bool lushPools = lowerText(brushWorld_) == "lush";
    const bool archipelagoHills = lowerText(brushWorld_) == "archipelago";
    const bool marsHills = lowerText(brushWorld_) == "mars";
    const bool iceHills = lowerText(brushWorld_) == "ice";
    const bool crystalHills = lowerText(brushWorld_) == "crystal";
    const bool desertHills = lowerText(brushWorld_) == "desert";
    const bool acidHills = lowerText(brushWorld_) == "acid";
    const bool waterWorldReefs = lowerText(brushWorld_) == "waterworld";
    if ((greenHills || lavaHills || lushPools || archipelagoHills || marsHills || iceHills || crystalHills || desertHills || acidHills || waterWorldReefs
            || isEvergreenWorld(brushWorld_))
        && !hillSections_.empty()) {
        std::sort(hillSections_.begin(), hillSections_.end(), [](const HillSection& left, const HillSection& right) {
            return trailingNumber(left.name) < trailingNumber(right.name);
        });
        std::string icon8;
        std::string icon16;
        std::string iconRough;
        std::string iconIsland;
        for (HillSection& hill : hillSections_) {
            try {
                const HpiFile* file = source->archive().find(hill.sectionPath);
                if (file == nullptr) {
                    continue;
                }
                const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
                if (stamp.tileWidth == stamp.tileHeight
                    && (stamp.tileWidth == 8 || stamp.tileWidth == 16 || (hill.lavaPool && stamp.tileWidth == 32))) {
                    hill.tiles = stamp.tileWidth;
                }
                // Dune pictures are known from the start, so dunes already on the map are found.
                if (hill.dune != 0) {
                    for (int tile = 0; tile < stamp.tileWidth * stamp.tileHeight; ++tile) {
                        const std::size_t bytes = static_cast<std::size_t>(stamp.indices[static_cast<std::size_t>(tile)]) * 1024;
                        if (bytes + 1024 <= stamp.tileImages.size()) {
                            dunePictures_.insert(hashBrushTile(stamp.tileImages.data() + bytes));
                        }
                    }
                }
                // A group set when the section was found takes any square size.
                if (!hill.group.empty() && hill.crystalFolder.empty() && stamp.tileWidth > 0 && stamp.tileHeight > 0) {
                    hill.tiles = stamp.tileWidth;
                    hill.tilesHigh = stamp.tileHeight;
                }
                if (!hill.crystalFolder.empty() && stamp.tileWidth == stamp.tileHeight
                    && crystalBrushFor(hill.crystalFolder, stamp.tileWidth, hill.group, hill.noun, hill.baseName, hill.baseHeight)) {
                    hill.tiles = stamp.tileWidth;
                }
                if (hill.seaIsland != 0 && stamp.tileWidth == stamp.tileHeight) {
                    hill.tiles = stamp.tileWidth;
                    hill.seaIsland = lowerText(brushWorld_) == "mars" || lowerText(brushWorld_) == "ice" ? marsIslandKindOf(hill.tiles) : seaIslandKindOf(hill.name, hill.tiles);
                }
            } catch (const std::exception&) {
            }
            int number = 0;
            const int listed = greenHillNumber(hill.name, number) ? listedGreenHillSpan(number) : 0;
            if (!hill.rough && !hill.island && !hill.pool && hill.tiles == 8 && icon8.empty()) {
                icon8 = hill.sectionPath;
            }
            if (!hill.rough && !hill.island && !hill.pool && hill.tiles == 16 && listed == 16 && icon16.empty()) {
                icon16 = hill.sectionPath;
            }
            if (hill.rough && hill.tiles == 8 && iconRough.empty()) {
                iconRough = hill.sectionPath;
            }
            if (hill.seaIsland < 0) {
                hill.seaIsland = 0;
                hill.tiles = 0;
            }
            if (hill.island && iconIsland.empty()) {
                iconIsland = hill.sectionPath;
            }
        }
        if (icon16.empty()) {
            for (const HillSection& hill : hillSections_) {
                if (!hill.rough && !hill.island && !hill.pool && hill.tiles == 16) {
                    icon16 = hill.sectionPath;
                    break;
                }
            }
        }
        const auto addHillButton = [&](const char* buttonName, const std::string& sectionPath) {
            if (sectionPath.empty()) {
                return;
            }
            try {
                const HpiFile* file = source->archive().find(sectionPath);
                if (file == nullptr) {
                    return;
                }
                const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
                int width = 0;
                int height = 0;
                const std::vector<std::uint8_t> rgba = renderSectionRgba(stamp, 128, width, height);
                BrushPaint paint;
                paint.name = buttonName;
                paint.category = brushWorld_;
                paint.kind = BrushKind::Hill;
                paint.preview = uploadTexture(rgba.data(), width, height, false);
                if (paint.preview != 0) {
                    brushPaints_.push_back(std::move(paint));
                }
            } catch (const std::exception&) {
            }
        };
        if (waterWorldReefs) {
            // A middle piece shows the reef running across or down the button.
            const auto reefIcon = [&](std::string_view middle) {
                for (const HillSection& hill : hillSections_) {
                    if (hill.mountain && lowerText(hill.name).starts_with(middle)) {
                        return hill.sectionPath;
                    }
                }
                return std::string();
            };
            addHillButton("Horizontal Reef", reefIcon("ew"));
            addHillButton("Vertical Reef", reefIcon("ns"));
            for (const HillSection& hill : hillSections_) {
                if (hill.group == "Base-1 Misc 16x16" && hill.tiles == 16) {
                    addHillButton("Base-1 Misc 16x16", hill.sectionPath);
                    break;
                }
            }
            for (const char* button : {"Arm-85 Medium", "Arm-85 Large"}) {
                for (const HillSection& hill : hillSections_) {
                    if (hill.group == button && hill.tiles > 0) {
                        addHillButton(button, hill.sectionPath);
                        break;
                    }
                }
            }
        }
        if (isEvergreenWorld(brushWorld_)) {
            // Evergreen's islands on its Base-0 water, once the tileset has them.
            for (const char* button : {"Island", "Island Large", "Pool", "Hill", "Hill Large"}) {
                for (const HillSection& hill : hillSections_) {
                    if (hill.group == button && hill.tiles > 0) {
                        addHillButton(button, hill.sectionPath);
                        break;
                    }
                }
            }
        }
        if (greenHills) {
            addHillButton("Hill-85 8x8", icon8);
            addHillButton("Hill-85 16x16", icon16);
            addHillButton("Rough-85", iconRough);
            addHillButton("Island 8x8", iconIsland);
        }
        if (lavaHills) {
            std::string lava8;
            std::string lava16;
            std::string lavaPools[3];
            for (const HillSection& hill : hillSections_) {
                if (hill.lavaPool) {
                    const int slot = hill.tiles == 8 ? 0 : (hill.tiles == 16 ? 1 : (hill.tiles == 32 ? 2 : -1));
                    if (slot >= 0 && lavaPools[slot].empty()) {
                        lavaPools[slot] = hill.sectionPath;
                    }
                    continue;
                }
                if (hill.rough || hill.island || hill.pool) {
                    continue;
                }
                if (hill.tiles == 8 && lava8.empty()) {
                    lava8 = hill.sectionPath;
                }
                if (hill.tiles == 16 && lava16.empty()) {
                    lava16 = hill.sectionPath;
                }
            }
            addHillButton("Hill-86 8x8", lava8);
            addHillButton("Hill-86 16x16", lava16);
            addHillButton("Pool-86 8x8", lavaPools[0]);
            addHillButton("Pool-86 16x16", lavaPools[1]);
            addHillButton("Pool-86 32x32", lavaPools[2]);
            std::string isle;
            for (const HillSection& hill : hillSections_) {
                if (hill.group == "Island 48x48" && hill.tiles > 0 && isle.empty()) {
                    isle = hill.sectionPath;
                }
            }
            addHillButton("Island 48x48", isle);
        }
        if (lushPools) {
            std::string pool8;
            for (const HillSection& hill : hillSections_) {
                if (hill.pool && hill.tiles == 8 && pool8.empty()) {
                    pool8 = hill.sectionPath;
                }
            }
            addHillButton("Pools 8x8", pool8);
            // A middle piece shows the ridge running across or down the button.
            const auto mountainIcon = [&](std::string_view middle) {
                for (const HillSection& hill : hillSections_) {
                    if (hill.mountain && lowerText(hill.name).starts_with(middle)) {
                        return hill.sectionPath;
                    }
                }
                return std::string();
            };
            addHillButton("Horizontal Mtn", mountainIcon("ew"));
            addHillButton("Vertical Mtn", mountainIcon("ns"));
            const auto groupIcon = [&](std::string_view group) {
                for (const HillSection& hill : hillSections_) {
                    if (hill.group == group && hill.tiles > 0) {
                        return hill.sectionPath;
                    }
                }
                return std::string();
            };
            addHillButton("Island-27 16x16", groupIcon("Island-27 16x16"));
            addHillButton("Hill-77 8x8", groupIcon("Hill-77 8x8"));
            addHillButton("Hill-77 16x16", groupIcon("Hill-77 16x16"));
            addHillButton("Swamp-77", groupIcon("Swamp-77"));
            addHillButton("Pond-77 16x16", groupIcon("Pond-77 16x16"));
            addHillButton("Feature-255", groupIcon("Feature-255"));
        }
        if (acidHills) {
            for (const char* button : {"Pools-85", "Acid-0 16x16", "Hills-85 16x16", "Mounds-85 16x16", "Puddles-85 16x16",
                     "Trails-85 16x16", "Islands-0 16x16", "Islands-0 32x32"}) {
                std::string icon;
                for (const HillSection& hill : hillSections_) {
                    // A 16 x 16 piece makes the icon where the button has one.
                    if (hill.group == button && (hill.tiles == 16 || (icon.empty() && hill.tiles > 0))) {
                        icon = hill.sectionPath;
                        if (hill.tiles != 16) {
                            continue;
                        }
                        break;
                    }
                }
                addHillButton(button, icon);
            }
        }
        if (desertHills) {
            // The 32 x 32 hills make the icon; the 64 x 64 one is too detailed at button size.
            std::string icon;
            for (const HillSection& hill : hillSections_) {
                if (hill.group == "Huge-Hill-80" && hill.tiles > 0 && (icon.empty() || hill.tiles == 32)) {
                    icon = hill.sectionPath;
                    if (hill.tiles == 32) {
                        break;
                    }
                }
            }
            addHillButton("Huge-Hill-80", icon);
            std::string icon16;
            for (const HillSection& hill : hillSections_) {
                if (hill.group == "Hill-80 16x16" && hill.tiles == 16) {
                    icon16 = hill.sectionPath;
                    break;
                }
            }
            addHillButton("Hill-80 16x16", icon16);
            for (const char* button : {"Plateaus-80", "Roughs-80 8x8"}) {
                std::string groupIcon;
                for (const HillSection& hill : hillSections_) {
                    if (hill.group == button && hill.tiles > 0) {
                        groupIcon = hill.sectionPath;
                        break;
                    }
                }
                addHillButton(button, groupIcon);
            }
            std::string duneIcon;
            for (const HillSection& hill : hillSections_) {
                // The top-left corner piece shows the dune meeting the sand.
                if (hill.dune == 1) {
                    duneIcon = hill.sectionPath;
                    break;
                }
            }
            addHillButton("Dunes-80", duneIcon);
        }
        if (crystalHills) {
            for (const char* button : {"Crater-85 8x8", "Crater-85 16x16", "Hills-85 16x16", "Hills-85 32x32", "Island-85 16x16",
                     "Island-85 32x32", "Pond-85", "Reef-85 16x16", "Reef-85 32x32", "Rough-85"}) {
                std::string icon;
                for (const HillSection& hill : hillSections_) {
                    // The 16 x 16 rough makes the icon, the way the other buttons show one tile.
                    if (hill.group == button && hill.tiles > 0 && (icon.empty() || hill.tiles < kBrushCellTiles * 2)) {
                        icon = hill.sectionPath;
                        if (hill.tiles < kBrushCellTiles * 2) {
                            break;
                        }
                    }
                }
                addHillButton(button, icon);
            }
        }
        if (iceHills) {
            std::string ice[7];
            std::string hill8;
            std::string hill16;
            std::string pool16;
            for (const HillSection& hill : hillSections_) {
                if (hill.seaIsland > 0 && hill.seaIsland < 7 && ice[hill.seaIsland].empty()) {
                    ice[hill.seaIsland] = hill.sectionPath;
                }
                if (hill.hill85 && hill.tiles == 8 && hill8.empty()) {
                    hill8 = hill.sectionPath;
                }
                if (hill.hill85 && hill.tiles == 16 && hill16.empty()) {
                    hill16 = hill.sectionPath;
                }
                if (hill.pool85 && hill.tiles == 16 && pool16.empty()) {
                    pool16 = hill.sectionPath;
                }
            }
            addHillButton("Hills-85 8x8", hill8);
            addHillButton("Hills-85 16x16", hill16);
            addHillButton("Pools-85", pool16);
            addHillButton("Island-0 8x8", ice[1]);
            addHillButton("Island-0 16x16", ice[5]);
        }
        if (marsHills) {
            std::string mars8;
            std::string marsPool8;
            std::string marsPool16;
            std::string marsRough16;
            std::string marsFeature16;
            std::string marsIslands[7];
            for (const HillSection& hill : hillSections_) {
                if (hill.seaIsland > 0 && hill.seaIsland < 7 && marsIslands[hill.seaIsland].empty()) {
                    marsIslands[hill.seaIsland] = hill.sectionPath;
                }
                if (hill.marsFeature && hill.tiles == 16 && marsFeature16.empty()) {
                    marsFeature16 = hill.sectionPath;
                }
                if (hill.marsRough && hill.tiles == 16 && marsRough16.empty()) {
                    marsRough16 = hill.sectionPath;
                }
                if (hill.hill85 && hill.tiles == 8 && mars8.empty()) {
                    mars8 = hill.sectionPath;
                }
                if (hill.pool85 && hill.tiles == 8 && marsPool8.empty()) {
                    marsPool8 = hill.sectionPath;
                }
                if (hill.pool85 && hill.tiles == 16 && marsPool16.empty()) {
                    marsPool16 = hill.sectionPath;
                }
            }
            addHillButton("Hills-85 8x8", mars8);
            addHillButton("Pool-85 8x8", marsPool8);
            addHillButton("Pool-85 16x16", marsPool16);
            addHillButton("Rough-85 16x16", marsRough16);
            addHillButton("Feature-85", marsFeature16);
            addHillButton("Island-0 8x8", marsIslands[1]);
            addHillButton("Island-0 16x16", marsIslands[5]);
            addHillButton("Island-0 32x32", marsIslands[6]);
        }
        if (archipelagoHills) {
            std::string archipelago16;
            std::string low8;
            std::string low16;
            std::string seaIcons[5];
            for (const HillSection& hill : hillSections_) {
                if (hill.seaIsland > 0 && hill.seaIsland < 5 && seaIcons[hill.seaIsland].empty()) {
                    seaIcons[hill.seaIsland] = hill.sectionPath;
                }
                if (hill.rough || hill.island || hill.pool || hill.seaIsland != 0) {
                    continue;
                }
                if (!hill.base78 && hill.tiles == 16 && archipelago16.empty()) {
                    archipelago16 = hill.sectionPath;
                }
                if (hill.base78 && hill.tiles == 8 && low8.empty()) {
                    low8 = hill.sectionPath;
                }
                if (hill.base78 && hill.tiles == 16 && low16.empty()) {
                    low16 = hill.sectionPath;
                }
            }
            addHillButton("Hill-194 16x16", archipelago16);
            addHillButton("Hills-78 8x8", low8);
            addHillButton("Hills-78 16x16", low16);
            addHillButton("Island-0 8x8", seaIcons[1]);
            addHillButton("Rocky-0 8x8", seaIcons[2]);
            addHillButton("Island-0 Medium", seaIcons[3]);
            addHillButton("Island-0 Large", seaIcons[4]);
        }
    }
    if (!brushCatalogStampsOnly_) {
        // The flat ground of every base this brush cannot join: every base of the tileset that is
        // neither one of this tileset's own nor a partner land. Pictures those share with a base the
        // brush knows, such as one water drawn for two tilesets, stay with the known base.
        const auto isLevel = [&](const std::string& category, const std::string& name) {
            for (const BrushPaint& paint : brushPaints_) {
                if (paint.kind == BrushKind::Base && lowerText(paint.category) == lowerText(category) && lowerText(paint.name) == lowerText(name)) {
                    return true;
                }
            }
            for (const BrushPaint& partner : brushPartners_) {
                if (lowerText(partner.category) == lowerText(category) && lowerText(partner.name) == lowerText(name)) {
                    return true;
                }
            }
            // A water of the same family as one the brush holds, such as Archipelago Base-0 beside the
            // Desert Base-0 the Mars brush holds, or Metal Base-0 beside GreenWorld's. The two draw the
            // same water, and the few tiles of its own one has are that water too, not foreign ground.
            if (trailingNumber(name) == 0) {
                const std::string family = blendWorldName(category);
                const auto sameWater = [&](const BrushPaint& paint) {
                    return trailingNumber(paint.name) == 0 && sameWaterFamily(blendWorldName(paint.category), family);
                };
                for (const BrushPaint& paint : brushPaints_) {
                    if (paint.kind == BrushKind::Base && sameWater(paint)) {
                        return true;
                    }
                }
                for (const BrushPaint& partner : brushPartners_) {
                    if (sameWater(partner)) {
                        return true;
                    }
                }
            }
            return false;
        };
        const auto picturesOf = [&](const TilesetSection& section, const std::function<void(std::uint64_t)>& visit) {
            try {
                if (const HpiFile* file = source->archive().find(section.archivePath)) {
                    const SectionStamp stamp = loadSectionStamp(source->archive().extract(*file));
                    for (int tile = 0; tile < stamp.tileCount; ++tile) {
                        visit(hashBrushTile(stamp.tileImages.data() + static_cast<std::size_t>(tile) * 1024));
                    }
                }
            } catch (const std::exception&) {
            }
        };
        std::unordered_set<std::uint64_t> known;
        for (const TilesetSection& section : source->sections) {
            const bool base = isBaseBrushTile(section.world, section.name);
            bool stroke = false;
            if (isBrushStrokeWorld(section.world)) {
                for (const int size : {1, 2, 4, 8, 16, 32, 64, 128, 256, 512}) {
                    for (const BrushPaint& partner : brushPartners_) {
                        stroke = stroke || brushStrokeVariation(section.name, partner.name, size) >= 0;
                    }
                    for (const BrushPaint& paint : brushPaints_) {
                        stroke = stroke || brushStrokeVariation(section.name, paint.name, size) >= 0;
                    }
                }
            }
            if ((base || stroke) && (stroke || isLevel(section.category, section.name))) {
                picturesOf(section, [&](std::uint64_t hash) { known.insert(hash); });
            }
        }
        brushBaseTiles_ = known;
        // Each picture's bases, then one name for them. A picture several bases share, such as the water
        // GreenWorld, Ice and Metal all use, is named after the tilesets the brush works with.
        struct Owner {
            std::string category;
            std::string name;
            bool related = false;
        };
        std::unordered_map<std::uint64_t, std::vector<Owner>> owners;
        for (const TilesetSection& section : source->sections) {
            if (!isBaseBrushTile(section.world, section.name) || isLevel(section.category, section.name)) {
                continue;
            }
            bool related = false;
            for (const BrushPaint& paint : brushPaints_) {
                related = related || lowerText(paint.category) == lowerText(section.category);
            }
            for (const BrushPaint& partner : brushPartners_) {
                related = related || lowerText(partner.category) == lowerText(section.category);
            }
            // Only the known bases' own pictures count against it: a partner's shore is remembered with
            // its liquid's tiles too, and those are still that liquid.
            picturesOf(section, [&](std::uint64_t hash) {
                if (known.count(hash) == 0) {
                    std::vector<Owner>& list = owners[hash];
                    const bool listed = std::any_of(list.begin(), list.end(), [&](const Owner& owner) {
                        return owner.category == section.category && owner.name == section.name;
                    });
                    if (!listed) {
                        list.push_back(Owner{section.category, section.name, related});
                    }
                }
            });
        }
        for (const auto& [hash, list] : owners) {
            const bool anyRelated = std::any_of(list.begin(), list.end(), [](const Owner& owner) { return owner.related; });
            std::vector<const Owner*> named;
            for (const Owner& owner : list) {
                if (owner.related || !anyRelated) {
                    named.push_back(&owner);
                }
            }
            const bool sameBase = std::all_of(named.begin(), named.end(), [&](const Owner* owner) { return owner->name == named.front()->name; });
            std::string label;
            for (std::size_t i = 0; i < named.size(); ++i) {
                label += i == 0 ? "" : (i + 1 == named.size() ? " or " : ", ");
                label += sameBase ? named[i]->category : named[i]->category + " " + named[i]->name;
            }
            if (sameBase) {
                label += " " + named.front()->name;
            }
            foreignPictures_.emplace(hash, label);
        }
    }
    if (!brushName_.empty()) {
        bool found = false;
        for (const BrushPaint& paint : brushPaints_) {
            if (paint.name == brushName_) {
                found = true;
            }
        }
        if (!found) {
            brushName_.clear();
        }
    }
    // The worker starts once every button is in brushPaints_, since it keeps pointers into it, and nothing changes
    // brushPaints_ again until the build is collected.
    if (catalogueJob) {
        catalogueBuilding_ = true;
        catalogueWork_ = std::async(std::launch::async, std::move(catalogueJob));
    }
}

void EditorApp::drawTileBrush() {
    // A tileset just chosen in the menu is read on the frame after, so the window shows the spinner first.
    if (brushSwitchPending_) {
        brushSwitchPending_ = false;
    } else {
        ensureBrushPaints();
    }
    if (!showTileBrush_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 400.0f, viewport->WorkPos.y + viewport->WorkSize.y - 220.0f),
        ImGuiCond_FirstUseEver);
    constexpr float kRounding = 5.0f;
    constexpr int kColumns = 3;
    // Each picture is kExtraWidth wider than it is tall, so the row is three of those wider.
    constexpr float kExtraWidth = 15.0f;
    constexpr float kRowWidth = 364.0f + kColumns * kExtraWidth;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;
    const bool visible = ImGui::Begin("Tile Brush", nullptr, flags);
    ImGui::PopStyleVar(3);
    if (!visible) {
        ImGui::End();
        return;
    }

    {
        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
        if (!drawGlass(tileBrushGlass_, windowMin.x, windowMin.y, windowMax.x, windowMax.y, kRounding)) {
            ImGui::GetWindowDrawList()->AddRectFilled(windowMin, windowMax, ImGui::GetColorU32(ImGuiCol_WindowBg), kRounding);
        }
        ImGui::GetWindowDrawList()->AddRect(windowMin, windowMax, theme::glassStyle().outline, kRounding);
    }

    {
        const float startX = ImGui::GetCursorPosX();
        ImGui::PushFont(theme::smallFont());
        const float lineHeight = ImGui::GetTextLineHeight();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::glassStyle().titleText);
        ImGui::TextUnformatted("Tile Brush");
        ImGui::PopStyleColor();
        const float closeSize = lineHeight;
        ImGui::SameLine(startX + kRowWidth - closeSize, 0.0f);
        const ImVec2 closeMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("tile-brush-close", ImVec2(closeSize, lineHeight))) {
            showTileBrush_ = false;
        }
        const bool closeHot = ImGui::IsItemHovered();
        const ImVec2 center(closeMin.x + closeSize * 0.5f, closeMin.y + lineHeight * 0.5f);
        const float arm = closeSize * 0.22f;
        const ImU32 xColor = closeHot ? theme::glassStyle().closeMarkHot : theme::glassStyle().closeMark;
        ImDrawList* titleDraw = ImGui::GetWindowDrawList();
        titleDraw->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), xColor, 1.5f);
        titleDraw->AddLine(ImVec2(center.x - arm, center.y + arm), ImVec2(center.x + arm, center.y - arm), xColor, 1.5f);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
    }

    ImGui::SetNextItemWidth(std::max(40.0f, kRowWidth - ImGui::CalcTextSize("Tileset").x - ImGui::GetStyle().ItemInnerSpacing.x));
    // Evergreen2 is Evergreen's second grass, listed as Evergreen, and Metal2 Metal's second water, listed as Metal.
    const std::string shownTileset = brushTilesetOf(brushWorld_);
    const char* shownWorld = shownTileset.c_str();
    if (ImGui::BeginCombo("Tileset", shownWorld)) {
        for (const char* world : {"GreenWorld", "Lava", "Mars", "Ice", "Metal", "Crystal", "Archipelago", "Acid", "Desert", "Lush", "Quagmire", "Evergreen", "SnowyEvergreen", "WaterWorld"}) {
            const bool selected = std::string_view(shownWorld) == world;
            if (ImGui::Selectable(world, selected) && !selected) {
                brushWorld_ = world;
                brushCatalogKey_.clear();
                brushSwitchPending_ = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // A tileset waiting to be read, or one whose picture catalogue is building, shows a progress ring in place of the
    // buttons: a track, an arc that fills clockwise from the top, the percentage in the middle, and a label below.
    if (brushSwitchPending_ || catalogueBuilding_) {
        constexpr float kRadius = 44.0f;
        constexpr float kThickness = 9.0f;
        constexpr float kGap = 16.0f;
        constexpr float kPi = 3.14159265f;
        const char* label = "Building Picture Catalogue...";
        const float progress = std::clamp(catalogueProgress(), 0.0f, 1.0f);
        char percent[8];
        std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(progress * 100.0f));
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        const float areaHeight = kRadius * 2.0f + kThickness + kGap + labelSize.y + 40.0f;
        const ImVec2 areaMin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(kRowWidth, areaHeight));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 center(areaMin.x + kRowWidth * 0.5f, areaMin.y + 20.0f + kThickness * 0.5f + kRadius);
        draw->PathArcTo(center, kRadius, 0.0f, 2.0f * kPi, 64);
        draw->PathStroke(IM_COL32(255, 255, 255, 45), 0, kThickness);
        if (progress > 0.0f) {
            const float top = -kPi * 0.5f;
            draw->PathArcTo(center, kRadius, top, top + progress * 2.0f * kPi, std::max(8, static_cast<int>(64.0f * progress)));
            draw->PathStroke(theme::accentColor(), 0, kThickness);
        }
        const ImVec2 percentSize = ImGui::CalcTextSize(percent);
        draw->AddText(ImVec2(center.x - percentSize.x * 0.5f, center.y - percentSize.y * 0.5f), theme::accentColor(), percent);
        draw->AddText(ImVec2(center.x - labelSize.x * 0.5f, center.y + kRadius + kThickness * 0.5f + kGap), ImGui::GetColorU32(ImGuiCol_Text), label);
        ImGui::End();
        return;
    }

    if (brushPaints_.empty()) {
        ImGui::PushFont(theme::smallFont());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kRowWidth);
        ImGui::TextWrapped("%s", loadingTilesets()
                ? "Loading base tiles..."
                : "No base tiles were found in the random map tileset.");
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(kRowWidth, 0.0f));
        ImGui::End();
        return;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float thumb = std::floor(
        (kRowWidth - kColumns * kExtraWidth - style.ItemSpacing.x * (kColumns - 1) - style.FramePadding.x * 2.0f * kColumns) / kColumns);
    int column = 0;
    for (std::size_t index = 0; index < brushPaints_.size(); ++index) {
        const BrushPaint& paint = brushPaints_[index];
        const auto group = [](BrushKind kind) {
            if (kind == BrushKind::Base) {
                return 0;
            }
            if (kind == BrushKind::River || kind == BrushKind::Bridge) {
                return 1;
            }
            if (kind == BrushKind::Ridge || kind == BrushKind::Spacer) {
                return 2;
            }
            if (kind == BrushKind::Slope) {
                return 3;
            }
            return 4;
        };
        if (index > 0 && group(paint.kind) != group(brushPaints_[index - 1].kind)) {
            // Rivers, ridges, slopes and hills each start on a row of their own, below the base tiles.
            column = 0;
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
        }
        if (column != 0) {
            ImGui::SameLine();
        }
        column = (column + 1) % kColumns;
        ImGui::PushID((paint.category + "/" + paint.name).c_str());
        ImGui::BeginGroup();
        const float groupX = ImGui::GetCursorPosX();
        // Evergreen's grasses share the name Base-85, and Metal's waters Base-0: the armed one is the one whose land
        // the brush is on.
        const bool sibling = brushSiblingWorlds(paint.category, brushWorld_) && !brushCategoryMatches(paint.category, brushWorld_);
        const bool selected = brushName_ == paint.name && !sibling;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
        }
        // The frame is padded more at the sides than at the top and bottom, so the picture fills it: the side padding
        // matches the top's, the picture widens by the difference, and the square preview is cropped top and bottom
        // to that shape rather than stretched. The button keeps its size.
        const float sideGain = std::max(0.0f, style.FramePadding.x - style.FramePadding.y);
        const ImVec2 pictureSize(thumb + 2.0f * sideGain + kExtraWidth, thumb);
        const float crop = (1.0f - pictureSize.y / pictureSize.x) * 0.5f;
        ImVec2 uvMin(0.0f, crop);
        ImVec2 uvMax(1.0f, 1.0f - crop);
        // A Base button is a window onto the middle of its full-size ground, one pixel to a point, rather than the
        // whole section shrunk to fit.
        if (paint.kind == BrushKind::Base && paint.name.starts_with("Base-") && paint.previewWidth > 0 && paint.previewHeight > 0) {
            const float halfU = std::min(0.5f, pictureSize.x * 0.5f / static_cast<float>(paint.previewWidth));
            const float halfV = std::min(0.5f, pictureSize.y * 0.5f / static_cast<float>(paint.previewHeight));
            uvMin = ImVec2(0.5f - halfU, 0.5f - halfV);
            uvMax = ImVec2(0.5f + halfU, 0.5f + halfV);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.y, style.FramePadding.y));
        const bool pressed = ImGui::ImageButton(
            "brush-paint",
            textureId(paint.preview),
            pictureSize,
            uvMin,
            uvMax,
            ImVec4(0.30f, 0.31f, 0.33f, 1.0f));
        ImGui::PopStyleVar();
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        const ImVec2 buttonMax = ImGui::GetItemRectMax();
        if (selected) {
            ImGui::PopStyleColor(3);
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(buttonMin.x - 1.0f, buttonMin.y - 1.0f),
                ImVec2(buttonMax.x + 1.0f, buttonMax.y + 1.0f),
                theme::accentColor(),
                style.FrameRounding,
                0,
                2.0f);
        }
        if (pressed && sibling) {
            // The other Evergreen grass, or Metal's other water: the brush moves to its land, keeping the tileset's buttons.
            const std::string name = paint.name;
            brushWorld_ = paint.category;
            brushName_.clear();
            armBrush(name);
        } else if (pressed) {
            armBrush(paint.name);
        }
        if (ImGui::IsItemHovered()) {
            const char* hint = "Use this tile as the brush";
            std::string hintText;
            if ((paint.kind == BrushKind::River || paint.kind == BrushKind::Bridge) && isEvergreenWorld(brushWorld_)) {
                const bool second = paint.name.ends_with("85-2");
                hint = paint.kind == BrushKind::River
                    ? (second ? "Paint rivers on Base-85-2 and its straight coasts\nRivers join the rivers beside them and end where they stop"
                              : "Paint rivers on Base-85-1 and its straight coasts\nRivers join the rivers beside them and end where they stop")
                    : (second ? "Place a bridge on a River 85-2 river\nBridges go on straight rivers and bends"
                              : "Place a bridge on a River 85-1 river\nBridges go on straight rivers and bends");
            } else if (paint.kind == BrushKind::River && lowerText(brushWorld_) == "snowyevergreen") {
                hint = "Paint frozen rivers on Base-85, starting at a straight frozen shore or beside another river\nRivers join the rivers beside them and end where they stop";
            } else if (paint.kind == BrushKind::Ridge && paint.name == "Wall 85") {
                hint = "Paint walls on Base-85, starting from a Base-219 cliff or anywhere on the ground\nWalls join the walls beside them\nA cliff corner starts two walls";
            } else if (paint.kind == BrushKind::Ridge && paint.name == "Wall 139") {
                hint = "Paint walls on Base-139\nWalls join the walls beside them";
            } else if (paint.kind == BrushKind::Bridge && paint.name == "Bridge 139") {
                hint = "Start a bridge on a straight Base-139 high coast, then lay it across the dark water\nBridges run straight and split into 3-way and 4-way junctions\nA bridge clicked on open dark water is a lone piece";
            } else if (paint.kind == BrushKind::Bridge && lowerText(brushWorld_) == "snowyevergreen") {
                hint = "Place a crossing on a frozen river\nCrossings go on straight rivers, bends, 3-way junctions and 4-way crossings";
            } else if (paint.kind == BrushKind::River) {
                hint = lowerText(brushWorld_) == "lush"
                    ? "Paint rivers on Base-77 and straight coasts\nAn even-height Base-255 range gets a mountain pass\nRivers join the rivers beside them"
                    : "Paint rivers on Base-85 and straight coasts\nRivers join the rivers beside them";
            } else if (paint.kind == BrushKind::Bridge) {
                hint = lowerText(brushWorld_) == "lush"
                    ? "Place a bridge on a straight river\nBridges go on horizontal and vertical rivers"
                    : "Place a bridge on a river\nBridges go on straight rivers and bends, and on a 4-way crossing when the tileset has one";
            } else if (paint.kind == BrushKind::Ridge) {
                hint = "Paint ridges on Base-85 and straight cliff edges\nRidges join the ridges beside them";
            } else if (paint.kind == BrushKind::Spacer) {
                hint = "Open a gap in a ridge\nSpacers only go on straight ridges";
            } else if (paint.kind == BrushKind::Slope) {
                if (paint.name == "Ramp") {
                    hint = "Turn a straight high coast, from the dark water up to Base-139, into a ramp\nEach ramp stays on its own cliff";
                } else if (paint.name == "Slope 189") {
                    hint = "Turn a Base-85 to Base-189 cliff into a ramp\nRamps join the ramps beside them and meet cliffs at the ends";
                } else if (paint.name == "Slope 255") {
                    hint = "Turn a Base-189 to Base-255 cliff into a ramp\nRamps join the ramps beside them and meet cliffs at the ends";
                } else if (paint.name == kEvergreenPlateauRampButton) {
                    hint = "Turn a straight Base-85-1 to Base-190-1 cliff into a ramp\nRamps join the ramps beside them into one wide ramp and meet cliffs at the ends";
                } else if (paint.name == kEvergreen2PeakRampButton) {
                    hint = "Turn a straight Base-190-2 to Base-255-2 cliff into a ramp\nEach ramp stays on its own cliff";
                } else if (paint.name == kEvergreen2RampButton) {
                    hint = "Turn a straight Base-85-2 to Base-190-2 cliff or an inside corner into a ramp\n"
                           "Ramps join the ramps beside them into one wide ramp and meet cliffs at the ends";
                } else if (paint.name == kEvergreenPeakRampButton) {
                    hint = "Turn a straight Base-190-1 to Base-255-1 cliff into a ramp\nEach ramp stays on its own cliff";
                } else if (paint.name == kEvergreenRampButton) {
                    hint = "Turn a straight Base-85-1 to Base-190-2 cliff or an inside corner into a ramp\n"
                           "Ramps join the ramps beside them into one wide ramp and meet cliffs at the ends";
                } else if (paint.name == "Slope 139 to 255") {
                    hint = "Turn a straight Base-139 to Base-255 cliff into a slope\nEach slope stays on its own cliff";
                } else if (paint.name == "Slope 85 to 219") {
                    hint = "Turn a Base-85 to Base-219 cliff into a slope\nSlopes join the slopes beside them and meet cliffs at the ends";
                } else if (paint.name == "Slope 78 to 194") {
                    hint = "Turn a Base-78 to Base-194 cliff into a slope\nSlopes join the slopes beside them and meet cliffs at the ends";
                } else if (paint.name == "Slope 80 to 155") {
                    hint = "Turn a straight Base-80 to Base-155 cliff into a slope\nEach slope stays on its own cliff";
                } else if (paint.name == "Slope 155 to 230") {
                    hint = "Turn a straight Base-155 to Base-230 cliff into a slope\nEach slope stays on its own cliff";
                } else if (paint.name == "Slope 77 to 183") {
                    hint = "Turn a straight Base-77 to Base-183 cliff into a slope\nEach slope stays on its own cliff";
                } else {
                    hint = "Turn a cliff into a slope\nSlopes join the slopes beside them and meet cliffs at the ends";
                }
            } else if (paint.kind == BrushKind::Hill) {
                const HillSection* group = nullptr;
                bool mixed = false;
                for (const HillSection& hill : hillSections_) {
                    if (hill.group == paint.name && hill.tiles > 0) {
                        mixed = mixed || (group != nullptr && (group->tiles != hill.tiles || group->high() != hill.high()));
                        group = group == nullptr ? &hill : group;
                    }
                }
                if (paint.name == "Dunes-80") {
                    hint = "Paint dunes on Desert Base-80, one 16 x 16 cell at a time\nDrag to paint more; neighbouring dunes join up\nThe edges fade into the sand at the corners";
                } else if (paint.name == "Horizontal Reef") {
                    hint = "Place a reef on WaterWorld Base-0\nIt starts at 32 x 16 with both ends\nKeep dragging left or right to make it longer";
                } else if (paint.name == "Vertical Reef") {
                    hint = "Place a reef on WaterWorld Base-0\nIt starts at 16 x 32 with both ends\nKeep dragging up or down to make it longer";
                } else if (paint.name == "Horizontal Mtn") {
                    hint = "Place a mountain on Lush Base-77\nIt starts at 64 x 32 with both ends\nKeep dragging left or right to make it longer";
                } else if (paint.name == "Vertical Mtn") {
                    hint = "Place a mountain on Lush Base-77\nIt starts at 32 x 64 with both ends\nKeep dragging up or down to make it longer";
                } else if (group != nullptr) {
                    const std::string base = brushWorld_ + " Base-" + std::to_string(group->baseHeight);
                    hintText = mixed
                        ? "Place a random " + group->noun + " on " + base + "\nThe cursor shows the size of the next one\nPress N to skip to another one"
                        : "Place a random " + brushRectLabel(group->tiles, group->high()) + " " + group->noun + " on " + base + "\nThe whole block has to be Base-"
                            + std::to_string(group->baseHeight);
                    hint = hintText.c_str();
                } else if (paint.name == "Island 8x8") {
                    hint = "Place a random island on GreenWorld Base-0\nThe whole 16 x 16 block has to be Base-0";
                } else if (seaIslandKind(paint.name) != 0) {
                    const int kind = seaIslandKind(paint.name);
                    const bool bySize = lowerText(brushWorld_) == "mars" || lowerText(brushWorld_) == "ice";
                    const int tiles = kind == 5 ? 16 : (kind == 6 ? 32 : 8);
                    hintText = "Place a random " + std::to_string(tiles) + " x " + std::to_string(tiles) + " island on " + brushWorld_
                        + " Base-0\nThe whole " + std::to_string(tiles) + " x " + std::to_string(tiles) + " block has to be Base-0";
                    hint = bySize ? hintText.c_str()
                        : kind == 1 ? "Place a random 8 x 8 island on Archipelago Base-0\nThe whole 8 x 8 block has to be Base-0"
                        : kind == 2 ? "Place a random 8 x 8 rocky island on Archipelago Base-0\nThe whole 8 x 8 block has to be Base-0"
                        : kind == 3 ? "Place a random 16 x 16 to 20 x 20 island on Archipelago Base-0\nThe cursor shows the size of the next island\nPress N to skip to another island"
                        : "Place a random 32 x 32 or larger island on Archipelago Base-0\nThe cursor shows the size of the next island\nPress N to skip to another island";
                } else if (paint.name.rfind("Hills-78", 0) == 0) {
                    hint = paint.name.find("16x16") != std::string::npos
                        ? "Place a random 16 x 16 hill on Archipelago Base-78\nThe whole 16 x 16 block has to be Base-78"
                        : "Place a random 8 x 8 hill on Archipelago Base-78\nThe whole 8 x 8 block has to be Base-78";
                } else if (paint.name.rfind("Hill-194", 0) == 0) {
                    hint = "Place a random 16 x 16 hill on Archipelago Base-194\nThe whole 16 x 16 block has to be Base-194";
                } else if (pool85Brush(paint.name)) {
                    const char* size = paint.name.find("8x8") != std::string::npos ? "8 x 8" : "16 x 16";
                    hintText = std::string("Place a random ") + size + " pool on " + brushWorld_ + " Base-85\nThe whole " + size
                        + " block has to be Base-85";
                    hint = hintText.c_str();
                } else if (paint.name.rfind("Hills-85", 0) == 0) {
                    const char* size = paint.name.find("16x16") != std::string::npos ? "16 x 16" : "8 x 8";
                    hintText = std::string("Place a random ") + size + " hill on " + brushWorld_ + " Base-85\nThe whole " + size
                        + " block has to be Base-85";
                    hint = hintText.c_str();
                } else if (paint.name.rfind("Pool-86", 0) == 0) {
                    hint = paint.name.find("32x32") != std::string::npos
                        ? "Place a random 32 x 32 lava pool on Lava Base-86\nThe whole 32 x 32 block has to be Base-86"
                        : (paint.name.find("16x16") != std::string::npos
                            ? "Place a random 16 x 16 lava pool on Lava Base-86\nThe whole 16 x 16 block has to be Base-86"
                            : "Place a random 8 x 8 lava pool on Lava Base-86\nThe whole 8 x 8 block has to be Base-86");
                } else if (paint.name.rfind("Hill-86", 0) == 0) {
                    hint = paint.name.find("16x16") != std::string::npos
                        ? "Place a random 16 x 16 hill on Lava Base-86\nThe whole 16 x 16 block has to be Base-86"
                        : "Place a random 8 x 8 hill on Lava Base-86\nThe whole 8 x 8 block has to be Base-86";
                } else if (paint.name == "Pools 8x8") {
                    hint = "Place a random pool on Lush Base-77\nThe whole 8 x 8 block has to be Base-77";
                } else if (paint.name == "Feature-85") {
                    hint = "Place a random 16 x 16 feature on Mars Base-85\nThe whole 16 x 16 block has to be Base-85";
                } else if (paint.name == "Rough-85 16x16") {
                    hint = "Place a random 16 x 16 rough patch on Mars Base-85\nThe whole 16 x 16 block has to be Base-85";
                } else if (paint.name.rfind("Rough", 0) == 0) {
                    hint = "Place a random rough patch on GreenWorld Base-85\nThe whole 8 x 8 block has to be Base-85";
                } else {
                    hint = paint.name.find("16x16") != std::string::npos
                        ? "Place a random 16 x 16 hill on GreenWorld Base-85\nThe whole 16 x 16 block has to be Base-85"
                        : "Place a random 8 x 8 hill on GreenWorld Base-85\nThe whole 8 x 8 block has to be Base-85";
                }
            }
            if (selected) {
                hint = "Click again or press Esc to leave the brush";
            }
            ImGui::SetTooltip("%s", hint);
        }
        const std::string& caption = paint.label.empty() ? paint.name : paint.label;
        const float textWidth = ImGui::CalcTextSize(caption.c_str()).x;
        const float itemWidth = buttonMax.x - buttonMin.x;
        ImGui::SetCursorPosX(groupX + std::max(0.0f, (itemWidth - textWidth) * 0.5f));
        ImGui::TextUnformatted(caption.c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::End();
}

void EditorApp::ensureMinimap() {
    if (!map_) {
        return;
    }
    const float mapW = static_cast<float>(map_->pixelWidth());
    const float mapH = static_cast<float>(map_->pixelHeight());
    if (mapW < 1.0f || mapH < 1.0f) {
        return;
    }

    const float scale = std::max(ImGui::GetIO().DisplayFramebufferScale.x, 1.0f);
    const float longEdge = 240.0f * static_cast<float>(std::clamp(minimapPercent_, 100, 150)) / 100.0f * scale;
    int texW = 0;
    int texH = 0;
    if (mapW >= mapH) {
        texW = std::clamp(static_cast<int>(std::lround(longEdge)), 32, 1024);
        texH = std::max(1, static_cast<int>(std::lround(static_cast<float>(texW) * mapH / mapW)));
    } else {
        texH = std::clamp(static_cast<int>(std::lround(longEdge)), 32, 1024);
        texW = std::max(1, static_cast<int>(std::lround(static_cast<float>(texH) * mapW / mapH)));
    }
    miniDisplayW_ = static_cast<float>(texW) / scale;
    miniDisplayH_ = static_cast<float>(texH) / scale;
    // While the corner is dragged the old picture is stretched to the new size.
    if (minimapResizing_ && miniTexture_ != 0 && !miniDirty_) {
        return;
    }
    if (!miniDirty_ && miniTexture_ != 0 && texW == miniTexW_ && texH == miniTexH_) {
        return;
    }

    miniImage_.resize(static_cast<std::size_t>(texW) * static_cast<std::size_t>(texH) * 4);
    const float zoom = static_cast<float>(texW) / mapW;
    renderMapView(*map_, miniImage_.data(), texW, texH, static_cast<float>(texW), static_cast<float>(texH), 0.0f, 0.0f, zoom);

    if (miniTexture_ == 0) {
        glGenTextures(1, &miniTexture_);
    }
    glBindTexture(GL_TEXTURE_2D, miniTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (texW != miniTexW_ || texH != miniTexH_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, miniImage_.data());
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, GL_RGBA, GL_UNSIGNED_BYTE, miniImage_.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    miniTexW_ = texW;
    miniTexH_ = texH;
    miniDirty_ = false;
}

void EditorApp::drawMinimap() {
    if (!showMinimap_ || !map_ || view3d_) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 280.0f, viewport->WorkPos.y + 28.0f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(80.0f, 60.0f), ImVec2(800.0f, 800.0f));
    // The window draws its own glass, title and close button, because a title bar
    // that ImGui draws would be painted before (and so hidden by) the glass.
    constexpr float kRounding = 5.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;
    const bool visible = ImGui::Begin("Minimap", nullptr, flags);
    ImGui::PopStyleVar(3);
    if (!visible) {
        ImGui::End();
        return;
    }

    {
        const ImVec2 windowMin = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
        if (!drawGlass(minimapGlass_, windowMin.x, windowMin.y, windowMax.x, windowMax.y, kRounding)) {
            ImGui::GetWindowDrawList()->AddRectFilled(windowMin, windowMax, ImGui::GetColorU32(ImGuiCol_WindowBg), kRounding);
        }
        ImGui::GetWindowDrawList()->AddRect(windowMin, windowMax, theme::glassStyle().outline, kRounding);
    }

    ensureMinimap();
    if (miniTexture_ == 0 || miniDisplayW_ < 1.0f || miniDisplayH_ < 1.0f) {
        ImGui::TextUnformatted("Minimap unavailable");
        ImGui::End();
        return;
    }

    {
        const float startX = ImGui::GetCursorPosX();
        ImGui::PushFont(theme::smallFont());
        const float lineHeight = ImGui::GetTextLineHeight();
        ImGui::PushStyleColor(ImGuiCol_Text, theme::glassStyle().titleText);
        ImGui::TextUnformatted("Minimap");
        ImGui::PopStyleColor();
        const float closeSize = lineHeight;
        ImGui::SameLine(startX + miniDisplayW_ - closeSize, 0.0f);
        const ImVec2 closeMin = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("minimap-close", ImVec2(closeSize, lineHeight))) {
            showMinimap_ = false;
        }
        const bool closeHot = ImGui::IsItemHovered();
        const ImVec2 center(closeMin.x + closeSize * 0.5f, closeMin.y + lineHeight * 0.5f);
        const float arm = closeSize * 0.22f;
        const ImU32 xColor = closeHot ? theme::glassStyle().closeMarkHot : theme::glassStyle().closeMark;
        ImDrawList* titleDraw = ImGui::GetWindowDrawList();
        titleDraw->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), xColor, 1.5f);
        titleDraw->AddLine(ImVec2(center.x - arm, center.y + arm), ImVec2(center.x + arm, center.y - arm), xColor, 1.5f);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 1.0f));
    }

    const ImVec2 imageSize(miniDisplayW_, miniDisplayH_);
    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    // The corner grip resizes the minimap from its smallest size to half as big again. It
    // comes before the picture so the picture's own click does not take the drag.
    constexpr float kGrip = 14.0f;
    const ImVec2 gripMin(imageMin.x + imageSize.x - kGrip, imageMin.y + imageSize.y - kGrip);
    ImGui::SetCursorScreenPos(gripMin);
    ImGui::InvisibleButton("minimap-resize", ImVec2(kGrip, kGrip));
    const bool gripHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (ImGui::IsItemActive()) {
        minimapResizing_ = true;
        const float percent = static_cast<float>(std::clamp(minimapPercent_, 100, 150));
        const float baseW = imageSize.x * 100.0f / percent;
        const float baseH = imageSize.y * 100.0f / percent;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const float wanted = std::max((mouse.x - imageMin.x) / baseW, (mouse.y - imageMin.y) / baseH);
        minimapPercent_ = std::clamp(static_cast<int>(std::lround(wanted * 100.0f)), 100, 150);
    } else if (minimapResizing_) {
        minimapResizing_ = false;
        saveEditorOptions();
    }
    if (gripHot) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
    }
    ImGui::SetCursorScreenPos(imageMin);
    ImGui::InvisibleButton("minimap-image", imageSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 imageMax = ImGui::GetItemRectMax();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddImage(textureId(miniTexture_), imageMin, imageMax);
    draw->AddRect(imageMin, imageMax, IM_COL32(20, 22, 26, 255));

    const float mapW = static_cast<float>(map_->pixelWidth());
    const float mapH = static_cast<float>(map_->pixelHeight());
    if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && mapW > 0.0f && mapH > 0.0f && zoom_ > 0.0f && mapCanvasW_ > 1.0f && mapCanvasH_ > 1.0f) {
        const float u = std::clamp((ImGui::GetIO().MousePos.x - imageMin.x) / imageSize.x, 0.0f, 1.0f);
        const float v = std::clamp((ImGui::GetIO().MousePos.y - imageMin.y) / imageSize.y, 0.0f, 1.0f);
        panX_ = mapCanvasW_ * 0.5f - (u * mapW) * zoom_;
        panY_ = mapCanvasH_ * 0.5f - (v * mapH) * zoom_;
        fit_ = false;
    }
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImGui::SetTooltip("Click or drag to move the view");
        }
    }

    if (zoom_ > 0.0f && mapCanvasW_ > 1.0f && mapCanvasH_ > 1.0f && mapW > 0.0f && mapH > 0.0f) {
        const float left = -panX_ / zoom_;
        const float top = -panY_ / zoom_;
        float rx = imageMin.x + left / mapW * imageSize.x;
        float ry = imageMin.y + top / mapH * imageSize.y;
        float rw = (mapCanvasW_ / zoom_) / mapW * imageSize.x;
        float rh = (mapCanvasH_ / zoom_) / mapH * imageSize.y;
        constexpr float kMinRect = 6.0f;
        if (rw < kMinRect) {
            rx -= (kMinRect - rw) * 0.5f;
            rw = kMinRect;
        }
        if (rh < kMinRect) {
            ry -= (kMinRect - rh) * 0.5f;
            rh = kMinRect;
        }
        const ImVec2 rectMin(rx, ry);
        const ImVec2 rectMax(rx + rw, ry + rh);
        draw->PushClipRect(imageMin, imageMax, true);
        draw->AddRect(rectMin, rectMax, IM_COL32(0, 0, 0, 200), 0.0f, 0, 3.0f);
        draw->AddRect(rectMin, rectMax, IM_COL32(255, 220, 60, 255), 0.0f, 0, 1.0f);
        if (showStarts_) {
            for (const StartPosition& start : map_->starts) {
                const ImVec2 at(
                    imageMin.x + static_cast<float>(start.x) / mapW * imageSize.x,
                    imageMin.y + static_cast<float>(start.y) / mapH * imageSize.y);
                draw->AddCircleFilled(at, 3.5f, startColor(start.player));
            }
        }
        draw->PopClipRect();
    }

    {
        // Three short diagonal lines in the corner show it can be dragged.
        const ImU32 gripColor = gripHot ? IM_COL32(255, 255, 255, 230) : IM_COL32(255, 255, 255, 130);
        for (int line = 1; line <= 3; ++line) {
            const float reach = 3.5f * static_cast<float>(line);
            draw->AddLine(ImVec2(imageMax.x - reach - 1.0f, imageMax.y - 1.0f), ImVec2(imageMax.x - 1.0f, imageMax.y - reach - 1.0f), gripColor, 1.0f);
        }
    }

    // As it grows, the window is moved back inside the screen rather than running off it.
    {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        const float right = viewport->WorkPos.x + viewport->WorkSize.x;
        const float bottom = viewport->WorkPos.y + viewport->WorkSize.y;
        const ImVec2 inside(std::max(viewport->WorkPos.x, std::min(pos.x, right - size.x)), std::max(viewport->WorkPos.y, std::min(pos.y, bottom - size.y)));
        if (inside.x != pos.x || inside.y != pos.y) {
            ImGui::SetWindowPos(inside);
        }
    }

    ImGui::End();
}

}  // namespace galaxy

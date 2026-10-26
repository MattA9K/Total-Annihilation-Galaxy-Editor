#include "ui/EditorApp.hpp"

#include "core/EditorSettings.hpp"
#include "core/MapGenerator.hpp"
#include "core/Version.hpp"
#include "format/GafImage.hpp"
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

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <exception>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace galaxy {
namespace {

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
    try {
        map_ = loadMapFile(path);
        showMapSettings_ = false;
        image_.clear();
        viewTexW_ = 0;
        viewTexH_ = 0;
        fit_ = true;
        deduper_ = {};
        clearHistory();
        clearMapSelection();
        mapDirty_ = false;
        miniDirty_ = true;
        heightDirty_ = true;
        statusNote_.clear();
        error_.clear();
        vanillaTileWarning_.clear();
        vanillaTileWarningShown_ = false;
        if (map_->tileCount > kVanillaTileSlots) {
            showVanillaTileWarning();
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

void EditorApp::invalidateView() {
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

void EditorApp::toggleSelectMode() {
    selectionMode_ = !selectionMode_;
    selecting_ = false;
    pasteArmed_ = false;
}

void EditorApp::toggleDeleteMode() {
    featureDelete_ = !featureDelete_;
    if (featureDelete_) {
        armedFeature_.clear();
    }
}

void EditorApp::drawToolbar() {
    using theme::ToolIcon;
    drawToolbarBackground();
    const float gap = theme::toolbarGap();
    const auto next = [&]() { ImGui::SameLine(0.0f, gap); };

    // Select is a two-state control, so it carries a label for each state.
    if (theme::toolbarItem("select", ToolIcon::Select, selectionMode_ ? "Selecting" : "Select", selectionMode_, "Selecting")) {
        toggleSelectMode();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Select Tiles\nDrag a rectangle around tiles. Shift-drag adds to the selection. Shift+Ctrl-drag removes from it");
    }
    theme::toolbarSeparator();

    ImGui::BeginDisabled(!map_ || !hasSelection());
    if (theme::toolbarItem("cut", ToolIcon::Cut, "Cut")) {
        cutSelection();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Cut\nRemove the selected tiles and keep a copy to paste");
    }
    next();
    if (theme::toolbarItem("copy", ToolIcon::Copy, "Copy")) {
        copySelection(false);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Copy Terrain + Height Map\nCopy the selected tiles and their heights");
    }
    next();
    if (theme::toolbarItem("copy-heights", ToolIcon::CopyHeights, "Copy Heights")) {
        copySelection(true);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Copy Height Map\nCopy only the heights in the selection. Paste leaves the tiles in place");
    }
    ImGui::EndDisabled();
    next();
    ImGui::BeginDisabled(!map_ || !clipboard_);
    if (theme::toolbarItem("paste", ToolIcon::Paste, pasteArmed_ ? "Pasting" : "Paste", pasteArmed_, "Pasting")) {
        armPaste();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Paste\nLeave select mode. Drag to pan, then click to place the copy");
    }
    ImGui::EndDisabled();
    theme::toolbarSeparator();

    ImGui::BeginDisabled(!map_ || !hasSelection());
    if (theme::toolbarItem("modify-height", ToolIcon::ModifyHeight, "Modify Height")) {
        showModifyHeight_ = true;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Modify Height\nAdd, subtract, or set the height of the selected tiles");
    }
    next();
    if (theme::toolbarItem("blend", ToolIcon::Blend, "Blend")) {
        blendSelection();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Blend Textures\nFade the textures across edges where selected tiles touch");
    }
    ImGui::EndDisabled();
    theme::toolbarSeparator();

    if (theme::toolbarItem("height-map", ToolIcon::HeightMap, showHeightGrid_ ? "Hide Heights" : "Show Heights", showHeightGrid_, "Show Heights")) {
        showHeightGrid_ = !showHeightGrid_;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(showHeightGrid_ ? "Hide the height grid" : "Show the height grid");
    }
    theme::toolbarSeparator();

    ImGui::BeginDisabled(!map_ || undo_.empty());
    if (theme::toolbarItem("undo", ToolIcon::Undo, "Undo")) {
        undoPlacement();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Undo the last tile edit");
    }
    ImGui::EndDisabled();
    next();
    ImGui::BeginDisabled(!map_ || redo_.empty());
    if (theme::toolbarItem("redo", ToolIcon::Redo, "Redo")) {
        redoPlacement();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Redo the tile edit that was undone");
    }
    ImGui::EndDisabled();
    theme::toolbarSeparator();

    const std::string opacityLabel = "Opacity " + std::to_string(placeOpacity_) + "%";
    if (theme::toolbarItem("opacity", ToolIcon::Opacity, opacityLabel.c_str(), false, "Opacity 100%")) {
        ImGui::OpenPopup("opacity-menu");
    }
    const ImVec2 opacityMin = ImGui::GetItemRectMin();
    const ImVec2 opacityMax = ImGui::GetItemRectMax();
    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("opacity-menu")) {
        ImGui::SetTooltip("Mix the placed tile into the tile already on the map. 100%% replaces it.");
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

    if (featuresTab_) {
        theme::toolbarSeparator();
        ImGui::BeginDisabled(!map_);
        if (theme::toolbarItem("delete", ToolIcon::Delete, featureDelete_ ? "Deleting" : "Delete", featureDelete_, "Deleting")) {
            toggleDeleteMode();
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Click a tree, rock, metal patch, or other feature to remove it");
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
    if (!map_ || startLocationsTab_ || featuresTab_) {
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
            placeOpacity_);
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

void EditorApp::rememberFeature(FeatureChange change) {
    EditorCommand command;
    command.feature = std::move(change);
    pushCommand(std::move(command), false);
    markMapDirty();
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
        auto change = stampSectionChange(*map_, *clipboard_, tileX, tileY, &deduper_, placeOpacity_);
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
        if (undo_.back().terrain) {
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
        if (redo_.back().terrain) {
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
}

void EditorApp::drawStartLocations() {
    theme::helpText("Click a player, then click the map. Drag a pin to move it. Right-click a pin to remove it.");
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
        row.title = row.filename + "  \xC2\xB7  " + std::to_string(archive.sections.size()) + " sections";
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
        archive.visible = false;
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
bool EditorApp::updateGlass(GlassPanel& panel, float left, float top, float width, float height) {
    constexpr float kEdgeStrip = 48.0f;
    constexpr float kPixelsPerGlassPixel = 4.0f;
    if (!frostedGlass_ || !map_ || zoom_ <= 0.0f || panel.blur.failed() || width < 8.0f || height < 8.0f) {
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
    constexpr float kEdgeStrip = 48.0f;
    const float width = right - left;
    const float height = bottom - top;
    if (!updateGlass(panel, left, top, width, height)) {
        return false;
    }
    // Show only the middle of the picture; the strip around it was there for the blur.
    const float regionW = width + kEdgeStrip * 2.0f;
    const float regionH = height + kEdgeStrip * 2.0f;
    const ImVec2 uv0(kEdgeStrip / regionW, kEdgeStrip / regionH);
    const ImVec2 uv1((kEdgeStrip + width) / regionW, (kEdgeStrip + height) / regionH);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (rounding > 0.0f) {
        draw->AddImageRounded(textureId(panel.blur.texture()), ImVec2(left, top), ImVec2(right, bottom), uv0, uv1, IM_COL32_WHITE, rounding);
    } else {
        draw->AddImage(textureId(panel.blur.texture()), ImVec2(left, top), ImVec2(right, bottom), uv0, uv1);
    }
    return true;
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

void EditorApp::drawSidebar() {
    ImGui::BeginChild("sidebar", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawSidebarBackground();
    ImGui::SetWindowFontScale(std::clamp(sidebarFontPercent_, 75, 200) / 100.0f);
    static const char* const kSidebarTabs[] = {"Tilesets", "Features", "Start Locations"};
    theme::segmented("sidebar-tabs", kSidebarTabs, 3, sidebarTab_, true);
    ImGui::Spacing();
    startLocationsTab_ = sidebarTab_ == 2;
    featuresTab_ = sidebarTab_ == 1;
    {
        if (sidebarTab_ == 0) {
            ImGui::BeginChild("tileset-scroll", ImVec2(0.0f, 0.0f), false);
            ImGui::SetWindowFontScale(std::clamp(sidebarFontPercent_, 75, 200) / 100.0f);
    theme::helpText("Open a category, click a tile, then click the map.");
    const std::string query = trimmed(tilesetSearch_);
    const ImGuiStyle& searchStyle = ImGui::GetStyle();
    const float clearSize = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(query.empty() ? -FLT_MIN : -(clearSize + searchStyle.ItemSpacing.x));
    ImGui::InputTextWithHint("##tileset-search", "Search tilesets", tilesetSearch_, sizeof(tilesetSearch_));
    if (!query.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("x##clear-tileset-search", ImVec2(clearSize, clearSize))) {
            tilesetSearch_[0] = '\0';
        }
    }
    ImGui::Separator();
    if (tilesets_.archives().empty()) {
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
        if (!indexed.visible) {
            continue;
        }
        ++visibleArchives;
        const std::size_t archiveIndex = indexed.archiveIndex;
        ImGui::PushID(static_cast<int>(archiveIndex));
        theme::sourceListTitle(indexed.title.c_str());
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
                            const bool selected = selectedSection_ == index;
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
                            if (cached.preview == 0 && ImGui::Selectable(caption.c_str(), selected, 0, ImVec2(std::max(cardWidth, ImGui::CalcTextSize(caption.c_str()).x), image))) {
                                selectedSection_ = index;
                                pasteArmed_ = false;
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
                            if (pressed) {
                                selectedSection_ = index;
                                pasteArmed_ = false;
                            }
                            if (ImGui::IsItemHovered()) {
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
                            const float advance = ImGui::GetItemRectSize().x + style.ItemSpacing.x;
                            if (n + 1 < pageCount && ImGui::GetItemRectMax().x + advance < regionRight) {
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
    if (visibleArchives == 0 && !query.empty()) {
        ImGui::TextWrapped("No tilesets match \"%s\".", query.c_str());
    }
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

void EditorApp::drawFeatures() {
    theme::helpText("Click a feature, then click the map. Delete in the toolbar removes the feature you click. Right-click removes one too.");
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
    if (hovered && io.MouseWheel != 0.0f && !(io.KeyAlt && placementStamp() != nullptr)) {
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
    }
    if (featuresTab_ && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        featurePressed_ = true;
        pressX_ = io.MousePos.x;
        pressY_ = io.MousePos.y;
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
    if (draggingStartPlayer_ == 0 && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 6.0f) && (!selectionMode_ || startLocationsTab_ || featuresTab_)) {
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
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            std::string removed;
            int removeX = featureCellX;
            int removeY = featureCellY;
            FeatureChange change;
            if (featureAtPointer(removeX, removeY, removed) && removeMapFeature(*map_, removeX, removeY, &change) == FeatureEdit::Removed) {
                rememberFeature(std::move(change));
                statusNote_ = "Removed " + removed;
            } else {
                armedFeature_.clear();
            }
        }
        if (featurePressed_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            const float dx = io.MousePos.x - pressX_;
            const float dy = io.MousePos.y - pressY_;
            featurePressed_ = false;
            if (dx * dx + dy * dy <= 36.0f) {
                if (featureDelete_) {
                    std::string removed;
                    int removeX = featureCellX;
                    int removeY = featureCellY;
                    FeatureChange change;
                    if (featureAtPointer(removeX, removeY, removed) && removeMapFeature(*map_, removeX, removeY, &change) == FeatureEdit::Removed) {
                        rememberFeature(std::move(change));
                        statusNote_ = "Removed " + removed;
                    }
                } else if (overMap && !armedFeature_.empty()) {
                    placeArmedFeature(featureCellX, featureCellY);
                }
            }
        } else if (featurePressed_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            featurePressed_ = false;
        }
        std::string underCursor;
        int underX = 0;
        int underY = 0;
        const bool featureUnderCursor = featureAtPointer(underX, underY, underCursor);
        if (hovered && (featureDelete_ ? featureUnderCursor : (!armedFeature_.empty() || featureUnderCursor))) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
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
                if (zoom_ > 0.45f) {
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
            if (zoom_ > 0.35f) {
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
    if (hoverOverMap_) {
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
        } else if (overMap) {
            ImGui::SetTooltip("Choose a feature on the left\nRight-click a marker to remove it\nHeight %d", hoverHeight_);
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
            ImGui::Text("%d tileset sections indexed", tilesets_.sectionCount());
        }
        return;
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
            ImGui::Text("Start locations. Choose a player, then click the map.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
        }
        finishStatus();
        return;
    }
    if (featuresTab_) {
        if (featureDelete_) {
            ImGui::Text("Delete. Click a feature to remove it.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
        } else if (armedFeature_.empty()) {
            ImGui::Text("Features. Choose one on the left, then click the map.   %s   zoom %.0f%%", map_->displayName.c_str(), zoom_ * 100.0f);
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

bool EditorApp::saveMapTo(const std::filesystem::path& path) {
    if (!map_) {
        return false;
    }
    try {
        std::filesystem::path destination = path;
        if (destination.extension().empty()) {
            destination += ".ufo";
        }
        saveMapFile(*map_, destination);
        map_->source = destination;
        map_->displayName = destination.stem().string();
        mapDirty_ = false;
        statusNote_ = "Saved " + destination.filename().string();
        error_.clear();
        return true;
    } catch (const std::exception& ex) {
        error_ = ex.what();
        return false;
    }
}

void EditorApp::requestClose() {
    if (map_ && mapDirty_) {
        showClosePrompt_ = true;
        quit_ = false;
        return;
    }
    quit_ = true;
}

void EditorApp::drawAbout() {
    if (aboutRequested_) {
        ImGui::OpenPopup("About Galaxy Editor");
        aboutRequested_ = false;
    }
    if (!ImGui::BeginPopupModal("About Galaxy Editor", nullptr, theme::alertFlags())) {
        return;
    }
    // \xC2\xA9 is the copyright sign.
    theme::alertText(applicationTitle().c_str(), "Copyright \xC2\xA9 2026 Black Cat Software\nDeveloped by Matt Andrzejczuk");
    const float okWidth = theme::pushButtonWidth("OK");
    theme::alignRight(okWidth);
    if (theme::defaultButton("OK", ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void EditorApp::drawClosePrompt() {
    if (!showClosePrompt_) {
        return;
    }
    ImGui::OpenPopup("Unsaved Changes");
    if (!ImGui::BeginPopupModal("Unsaved Changes", nullptr, theme::alertFlags())) {
        return;
    }
    theme::alertText(
        "You are about to close the application but there are unsaved changes.",
        "Your changes will be lost if you exit without saving.");
    if (ImGui::Button("Exit Without Saving")) {
        showClosePrompt_ = false;
        quit_ = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    const char* saveLabel = "Save Map & Exit";
    const float saveWidth = theme::pushButtonWidth(saveLabel);
    theme::alignRight(saveWidth);
    if (theme::defaultButton(saveLabel, ImVec2(saveWidth, 0.0f))) {
        if (saveMap()) {
            showClosePrompt_ = false;
            quit_ = true;
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
    gameFolder_ = settings.gameFolder;
    gameFolderAsked_ = settings.gameFolderAsked;
}

void EditorApp::saveEditorOptions() const {
    EditorSettings settings;
    settings.previewSize = previewSize_;
    settings.heightGridSize = heightGridSize_;
    settings.sidebarFontPercent = sidebarFontPercent_;
    settings.autosaveMinutes = autosaveMinutes_;
    settings.autosaveBackups = autosaveBackups_;
    settings.appearance = appearanceMode_;
    settings.gameFolder = gameFolder_;
    settings.gameFolderAsked = gameFolderAsked_;
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
    if (!ImGui::Begin("Map Settings", &showMapSettings_)) {
        ImGui::End();
        return;
    }
    static const char* const kSettingsTabs[] = {"Map", "World", "Special", "Rules"};
    theme::segmented("map-settings-tabs", kSettingsTabs, 4, mapSettingsTab_);
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
    if (!ImGui::Begin(
            "Galaxy Editor Options",
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
    ImGui::SeparatorText("Appearance");
    {
        static const char* const kAppearances[] = {"Auto", "Light", "Dark"};
        if (theme::segmented("appearance", kAppearances, 3, appearanceMode_)) {
            optionsChanged = true;
        }
    }
    theme::helpText(
        "Auto follows the light or dark setting of this computer and changes when it does. Where the system does not report a setting, Auto stays light.");
    ImGui::SeparatorText("Total Annihilation");
    if (gameFolder_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::TextUnformatted("Not set");
        ImGui::PopStyleColor();
    } else {
        ImGui::TextWrapped("%s", pathToUtf8(gameFolder_).c_str());
    }
    if (loadingTilesets()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::Text("Loading %d of %d archives", static_cast<int>(tilesetLoadDone()), static_cast<int>(tilesetLoadTotal()));
        ImGui::PopStyleColor();
    } else if (!gameFolder_.empty() && !gameFolderFound_) {
        ImGui::TextWrapped("This folder was not found.");
    } else if (!gameFolder_.empty() && gameFolderArchives_ == 0) {
        ImGui::TextWrapped("This folder has no .hpi, .ccx, .gp3, or .gpf archives.");
    } else if (!gameFolder_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::Text("%d archives loaded from this folder", static_cast<int>(gameFolderArchives_));
        ImGui::PopStyleColor();
    }
    if (!loadingTilesets() && tilesetLoadTotal() > gameFolderArchives_) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
        ImGui::Text("%d more loaded from beside Galaxy Editor", static_cast<int>(tilesetLoadTotal() - gameFolderArchives_));
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    {
        const std::string chooseLabel = theme::withEllipsis("Choose Folder");
        const float chooseWidth = theme::pushButtonWidth(chooseLabel.c_str());
        if (ImGui::Button(chooseLabel.c_str(), ImVec2(chooseWidth, 0.0f))) {
            chooseGameFolder();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(gameFolder_.empty());
        if (ImGui::Button("Clear", ImVec2(theme::pushButtonWidth("Clear"), 0.0f))) {
            setGameFolder({});
        }
        ImGui::EndDisabled();
    }
    theme::helpText(
        "Galaxy Editor reads tilesets and features from the .hpi, .ccx, .gp3, and .gpf files in this folder, such as totala1.hpi. Archives in the folder beside Galaxy Editor load too, so you can add your own tilesets there. A file with the same name as one in this folder loads only once. Changing the folder reloads everything right away.");
    ImGui::SeparatorText("Tile Previews");
    optionsChanged |= ImGui::SliderInt("Preview size", &previewSize_, 32, 256, "%d px", ImGuiSliderFlags_AlwaysClamp);
    theme::helpText(
        "Thumbnails come from each section in the tileset. Click one, then click the map to stamp its tiles and heights. Drag the map to pan. Esc or right-click clears the selection.");
    ImGui::SeparatorText("Height Map");
    if (ImGui::SliderInt("Grid size", &heightGridSize_, 16, 256, "%d px", ImGuiSliderFlags_AlwaysClamp)) {
        heightGridSize_ = std::clamp((heightGridSize_ + 8) / 16 * 16, 16, 256);
        optionsChanged = true;
    }
    theme::helpText(
        "Spacing is in map pixels. 16 is one height cell and 32 is one tile. Water is blue, low ground is green, hills are yellow, and cliffs and mountains run from orange to white.");
    ImGui::SeparatorText("Tileset List");
    optionsChanged |= ImGui::SliderInt("Sidebar font", &sidebarFontPercent_, 75, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
    theme::helpText("Changes the text size in the sidebar. Tile previews keep the size set above.");
    ImGui::SeparatorText("Autosave");
    optionsChanged |= ImGui::SliderInt("Autosave every", &autosaveMinutes_, 1, 120, "%d min", ImGuiSliderFlags_AlwaysClamp);
    optionsChanged |= ImGui::SliderInt("Backup copies", &autosaveBackups_, 1, 20, "%d", ImGuiSliderFlags_AlwaysClamp);
    theme::helpText(
        "After you edit a map, a backup is written on this timer. The backup does not replace the map from Save. It is named MapName-MM-DD-YYYY-HH-MM-Backup.ufo, with the hour from 00 to 23, and it is stored in the same folder. When that folder already has this many backups of the map, the oldest one is deleted first.");
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
    ImGui::OpenPopup("New Map");
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("New Map", &showNew_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const bool sizeOk = newWidth_ >= kMinMapTiles && newHeight_ >= kMinMapTiles && newWidth_ <= kMaxMapTiles && newHeight_ <= kMaxMapTiles;
    theme::helpText("Creates an empty map. Width and height are in 32-pixel tiles.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Width (tiles)", &newWidth_);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("Height (tiles)", &newHeight_);
    if (sizeOk) {
        const int pixelsWide = newWidth_ * 32;
        const int pixelsHigh = newHeight_ * 32;
        if (newWidth_ % 16 == 0 && newHeight_ % 16 == 0) {
            ImGui::Text("%d x %d pixels. Game size %d x %d.", pixelsWide, pixelsHigh, newWidth_ / 16, newHeight_ / 16);
        } else {
            ImGui::Text("%d x %d pixels. Game size %.2f x %.2f.", pixelsWide, pixelsHigh, newWidth_ / 16.0, newHeight_ / 16.0);
        }
    } else {
        ImGui::TextWrapped("Size must be from %d to %d tiles.", kMinMapTiles, kMaxMapTiles);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const float buttonWidth = theme::pushButtonWidth("Create");
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) {
        showNew_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!sizeOk);
    if (theme::defaultButton("Create", ImVec2(buttonWidth, 0.0f))) {
        try {
            map_ = createBlankMap(newWidth_, newHeight_);
            showMapSettings_ = false;
            image_.clear();
            viewTexW_ = 0;
            viewTexH_ = 0;
            fit_ = true;
            deduper_ = {};
            clearHistory();
            clearMapSelection();
            mapDirty_ = false;
            miniDirty_ = true;
            heightDirty_ = true;
            statusNote_ = "New " + std::to_string(newWidth_) + " x " + std::to_string(newHeight_) + " map";
            error_.clear();
            vanillaTileWarning_.clear();
            vanillaTileWarningShown_ = false;
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

void EditorApp::drawGenerate() {
    if (!showGenerate_) {
        return;
    }
    ImGui::OpenPopup("Generate Map");
    ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Generate Map", &showGenerate_, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    std::map<std::string, std::string> familyLabels;
    for (const TilesetArchive& archive : tilesets_.archives()) {
        for (const TilesetSection& section : archive.sections) {
            const std::string key = tilesetFamilyKey(section.world);
            const auto found = familyLabels.find(key);
            if (found == familyLabels.end()) {
                familyLabels.emplace(key, section.world);
                continue;
            }
            const bool candidateIsBase = lowerText(section.world) == key;
            const bool currentIsBase = lowerText(found->second) == key;
            if (candidateIsBase && !currentIsBase) {
                found->second = section.world;
            } else if (candidateIsBase == currentIsBase && section.world.size() < found->second.size()) {
                found->second = section.world;
            }
        }
    }
    std::vector<std::string> worlds;
    worlds.reserve(familyLabels.size());
    for (const auto& [key, label] : familyLabels) {
        (void)key;
        worlds.push_back(label);
    }
    std::sort(worlds.begin(), worlds.end());
    if (genWorld_.empty() || std::find(worlds.begin(), worlds.end(), genWorld_) == worlds.end()) {
        genWorld_.clear();
        for (const std::string& world : worlds) {
            if (world == "GreenWorld") {
                genWorld_ = world;
                break;
            }
        }
        if (genWorld_.empty() && !worlds.empty()) {
            genWorld_ = worlds.front();
        }
    }

    theme::helpText(
        "Rebuilds the open map from section pieces. Rivers stay connected. Cliffs and shores are chosen from the height at each corner, so a plateau or a coastline meets the grass instead of stepping onto it. The map keeps its size, sea level, and start positions. Features are cleared.");
    ImGui::Spacing();

    if (worlds.empty()) {
        ImGui::TextWrapped("No tileset is loaded. Set the Total Annihilation folder in Options to load the world archives.");
    } else if (ImGui::BeginCombo("World", genWorld_.c_str())) {
        for (const std::string& world : worlds) {
            if (ImGui::Selectable(world.c_str(), world == genWorld_)) {
                genWorld_ = world;
            }
        }
        ImGui::EndCombo();
    }
    if (!worlds.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Packs that share a name before _New are one world. Acid includes Acid_New_1, Acid_New_2, and Acid_New_3.");
    }

    ImGui::SliderInt("Rivers", &genRivers_, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Connected channels. Each piece is a banked river, so the water meets the next channel instead of filling a square.");
    }
    ImGui::SliderInt("Canyons", &genCanyons_, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Raised shelves and mesas. Edge, corner, and flat pieces are picked so neighboring height edges agree.");
    }
    ImGui::SliderInt("Mountains", &genMountains_, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Large peaks placed on the flat ground.");
    }
    ImGui::SliderInt("Hills", &genHills_, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Smaller rises scattered over the grass.");
    }
    ImGui::SliderInt("Shoreline", &genShore_, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Water along the map edge. Coast pieces follow the corner heights, so the water meets the grass. Higher values add the opposite shore, then a full ring.");
    }
    ImGui::SliderInt("Pools", &genPools_, 0, 100, "%d", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Small ponds on the flat ground.");
    }
    ImGui::InputInt("Seed", &genSeed_);
    ImGui::SameLine();
    if (ImGui::Button("New Seed")) {
        std::mt19937 seedRng{std::random_device{}()};
        std::uniform_int_distribution<int> seedPick(1, 999983);
        genSeed_ = seedPick(seedRng);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    const float buttonWidth = theme::pushButtonWidth("Generate");
    theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f))) {
        showGenerate_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(genWorld_.empty() || !map_);
    if (ImGui::Button("Generate", ImVec2(buttonWidth, 0.0f))) {
        const int tileCountBefore = map_->tileCount;
        try {
            MapGeneratorSettings settings;
            settings.world = genWorld_;
            settings.rivers = genRivers_;
            settings.canyons = genCanyons_;
            settings.mountains = genMountains_;
            settings.hills = genHills_;
            settings.shoreline = genShore_;
            settings.pools = genPools_;
            settings.seed = static_cast<unsigned>(std::max(genSeed_, 0));
            generateTerrain(*map_, tilesets_, settings);
            deduper_ = {};
            clearHistory();
            invalidateView();
            markMapDirty();
            statusNote_ = "Generated a new map";
            error_.clear();
            warnIfPastVanillaTiles(tileCountBefore);
            showGenerate_ = false;
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& ex) {
            deduper_ = {};
            clearHistory();
            invalidateView();
            markMapDirty();
            error_ = ex.what();
            warnIfPastVanillaTiles(tileCountBefore);
            showGenerate_ = false;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
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
    if (!mapDirty_) {
        mapDirty_ = true;
        autosaveArmedAt_ = std::chrono::steady_clock::now();
    }
}

void EditorApp::autosaveIfDue() {
    if (!map_ || !mapDirty_) {
        return;
    }
    const int minutes = std::clamp(autosaveMinutes_, 1, 120);
    const int copies = std::clamp(autosaveBackups_, 1, 20);
    if (std::chrono::steady_clock::now() - autosaveArmedAt_ < std::chrono::minutes(minutes)) {
        return;
    }

    try {
        const std::string mapName = backupMapName(map_->source.empty() ? map_->displayName : map_->source.stem().string());
        std::filesystem::path directory = map_->source.has_parent_path() ? map_->source.parent_path() : suggestedOpenDirectory();
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

        saveMapFile(*map_, destination);
        mapDirty_ = false;
        statusNote_ = "Autosaved " + filename;
        error_.clear();
    } catch (const std::exception& ex) {
        autosaveArmedAt_ = std::chrono::steady_clock::now();
        error_ = ex.what();
    }
}

bool EditorApp::draw() {
    updateAppearance();
    if (loadingTilesets()) {
        continueLoadingTilesets(std::chrono::milliseconds(16));
    }
    autosaveIfDue();
    const int placementId = pasteArmed_ ? -2 : selectedSection_;
    if (placementId != placementOffsetFor_) {
        placeOffsetX_ = 0;
        placeOffsetY_ = 0;
        placementOffsetFor_ = placementId;
    }
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_C)) {
            copySelection(true);
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            copySelection(false);
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
            cutSelection();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            armPaste();
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) {
            showNew_ = true;
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) {
            if (const auto chosen = openMapDialog(suggestedOpenDirectory())) {
                openMap(*chosen);
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
        } else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q)) {
            requestClose();
        } else if (!ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
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
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsPopupOpen("Error") && !ImGui::IsPopupOpen("Tile Limit") && !ImGui::IsPopupOpen("Generate Map") && !ImGui::IsPopupOpen("New Map") && !ImGui::IsPopupOpen("Modify Height") && !ImGui::IsPopupOpen("Unsaved Changes")) {
        if (showMapSettings_ && !ImGui::GetIO().WantTextInput) {
            showMapSettings_ = false;
        } else if (pasteArmed_) {
            pasteArmed_ = false;
            statusNote_ = "Paste cancelled";
        } else if (featureDelete_) {
            featureDelete_ = false;
            statusNote_ = "Delete cancelled";
        } else if (featuresTab_ && !armedFeature_.empty()) {
            armedFeature_.clear();
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
        } else {
            selectedSection_ = -1;
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
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(theme::withEllipsis("New Map").c_str(), theme::shortcut("N").c_str())) {
                showNew_ = true;
            }
            if (ImGui::MenuItem(theme::withEllipsis("Open Map").c_str(), theme::shortcut("O").c_str())) {
                if (const auto chosen = openMapDialog(suggestedOpenDirectory())) {
                    openMap(*chosen);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save", theme::shortcut("S").c_str(), false, map_.has_value())) {
                saveMap();
            }
            if (ImGui::MenuItem(theme::withEllipsis("Save As").c_str(), theme::shortcut("Shift+S").c_str(), false, map_.has_value())) {
                saveMapAs();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(theme::withEllipsis("Generate Map").c_str(), nullptr, false, map_.has_value())) {
                std::mt19937 seedRng{std::random_device{}()};
                std::uniform_int_distribution<int> seedPick(1, 999983);
                genSeed_ = seedPick(seedRng);
                showGenerate_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem(theme::quitLabel(), theme::shortcut("Q").c_str())) {
                requestClose();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", theme::shortcut("Z").c_str(), false, map_.has_value() && !undo_.empty())) {
                undoPlacement();
            }
            if (ImGui::MenuItem("Redo", theme::shortcut("Shift+Z").c_str(), false, map_.has_value() && !redo_.empty())) {
                redoPlacement();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Cut", theme::shortcut("X").c_str(), false, map_.has_value() && hasSelection())) {
                cutSelection();
            }
            if (ImGui::MenuItem("Copy Terrain + Height Map", theme::shortcut("C").c_str(), false, map_.has_value() && hasSelection())) {
                copySelection(false);
            }
            if (ImGui::MenuItem("Copy Height Map", theme::shortcut("Shift+C").c_str(), false, map_.has_value() && hasSelection())) {
                copySelection(true);
            }
            if (ImGui::MenuItem("Paste", theme::shortcut("V").c_str(), false, map_.has_value() && clipboard_.has_value())) {
                armPaste();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Select Tiles", nullptr, selectionMode_, map_.has_value())) {
                toggleSelectMode();
            }
            if (ImGui::MenuItem(theme::withEllipsis("Modify Height").c_str(), nullptr, false, map_.has_value() && hasSelection())) {
                showModifyHeight_ = true;
            }
            if (ImGui::MenuItem("Blend Textures", nullptr, false, map_.has_value() && hasSelection())) {
                blendSelection();
            }
            if (ImGui::MenuItem("Delete Features", nullptr, featureDelete_, map_.has_value() && featuresTab_)) {
                toggleDeleteMode();
            }
            if (ImGui::BeginMenu("Tile Opacity")) {
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
        if (ImGui::BeginMenu("Map")) {
            if (ImGui::MenuItem(theme::withEllipsis("Map Settings").c_str(), nullptr, false, map_.has_value())) {
                showMapSettings_ = true;
                mapSettingsFresh_ = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Fit Map", nullptr, false, map_.has_value())) {
                fit_ = true;
            }
            if (ImGui::MenuItem("Actual Size", nullptr, false, map_.has_value())) {
                fit_ = false;
                zoom_ = 1.0f;
            }
            if (ImGui::BeginMenu("Grid")) {
                if (ImGui::MenuItem("Off", nullptr, gridTiles_ == 0)) {
                    gridTiles_ = 0;
                }
                for (const int size : {1, 4, 8, 16, 32}) {
                    const std::string label = std::to_string(size) + " x " + std::to_string(size);
                    if (ImGui::MenuItem(label.c_str(), nullptr, gridTiles_ == size)) {
                        gridTiles_ = size;
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(showHeightGrid_ ? "Hide Height Map" : "Show Height Map")) {
                showHeightGrid_ = !showHeightGrid_;
            }
            ImGui::MenuItem("Feature Markers", nullptr, &showFeatures_);
            ImGui::MenuItem("Start Positions", nullptr, &showStarts_);
            ImGui::MenuItem("Minimap", nullptr, &showMinimap_);
            ImGui::MenuItem("Frosted Glass", nullptr, &frostedGlass_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Options")) {
            if (ImGui::MenuItem(theme::withEllipsis("Editor Options").c_str(), theme::shortcut(",").c_str())) {
                showOptions_ = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About Galaxy Editor")) {
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
    const float sidebarWidth = std::clamp(
        cardWidth + style.WindowPadding.x * 2.0f + style.ScrollbarSize + style.IndentSpacing * 2.0f + 16.0f
            + (sidebarFontScale - 1.0f) * 180.0f,
        300.0f * sidebarFontScale,
        720.0f);
    const float statusHeight = std::floor(ImGui::GetFrameHeight() + 2.0f);
    const float bodyTop = top + toolbarHeight;
    const float bodyHeight = std::max(1.0f, body.y - toolbarHeight - statusHeight);
    // The map goes first so that panning and zooming are settled before the
    // toolbar and sidebar glass copy the part of the map that lies behind them.
    ImGui::SetCursorPos(ImVec2(sidebarWidth, bodyTop));
    ImGui::BeginChild("map-slot", ImVec2(std::max(1.0f, body.x - sidebarWidth), bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawMap();
    ImGui::EndChild();
    ImGui::SetCursorPos(ImVec2(0.0f, top));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, std::floor((toolbarHeight - theme::toolbarItemHeight()) * 0.5f)));
    ImGui::BeginChild("toolbar", ImVec2(body.x, toolbarHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    drawToolbar();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, bodyTop));
    ImGui::BeginChild("sidebar-slot", ImVec2(sidebarWidth, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawSidebar();
    ImGui::EndChild();
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

    if (!error_.empty()) {
        ImGui::OpenPopup("Error");
    }
    if (ImGui::BeginPopupModal("Error", nullptr, theme::alertFlags())) {
        theme::alertText("Galaxy Editor could not finish that.", error_.c_str());
        const float okWidth = theme::pushButtonWidth("OK");
        theme::alignRight(okWidth);
        if (theme::defaultButton("OK", ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            error_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!vanillaTileWarning_.empty()) {
        ImGui::OpenPopup("Tile Limit");
    }
    if (ImGui::BeginPopupModal("Tile Limit", nullptr, theme::alertFlags())) {
        theme::alertText(vanillaTileWarning_.c_str());
        const float okWidth = theme::pushButtonWidth("OK");
        theme::alignRight(okWidth);
        if (theme::defaultButton("OK", ImVec2(okWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            vanillaTileWarning_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    drawGenerate();
    drawNewMap();
    drawModifyHeight();

    ImGui::End();
    drawOptions();
    drawMapSettings();
    drawMinimap();
    if (featureDelete_ && featuresTab_) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* overlay = ImGui::GetForegroundDrawList();
        const ImVec2 square(mouse.x + 10.0f, mouse.y + 12.0f);
        overlay->AddRectFilled(square, ImVec2(square.x + 6.0f, square.y + 6.0f), IM_COL32(220, 32, 32, 240));
        overlay->AddRect(square, ImVec2(square.x + 6.0f, square.y + 6.0f), IM_COL32(0, 0, 0, 230));
    }
    return !quit_;
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
    const float longEdge = 240.0f * scale;
    int texW = 0;
    int texH = 0;
    if (mapW >= mapH) {
        texW = std::clamp(static_cast<int>(std::lround(longEdge)), 32, 512);
        texH = std::max(1, static_cast<int>(std::lround(static_cast<float>(texW) * mapH / mapW)));
    } else {
        texH = std::clamp(static_cast<int>(std::lround(longEdge)), 32, 512);
        texW = std::max(1, static_cast<int>(std::lround(static_cast<float>(texH) * mapW / mapH)));
    }
    miniDisplayW_ = static_cast<float>(texW) / scale;
    miniDisplayH_ = static_cast<float>(texH) / scale;
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
    if (!showMinimap_ || !map_) {
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

    ImGui::End();
}

}  // namespace galaxy

#include "ui/HpiEditor.hpp"

#include "core/EditorSettings.hpp"
#include "platform/FileDialog.hpp"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace galaxy {

namespace fs = std::filesystem;

namespace {

constexpr const char* kDragType = "HPI_ITEMS";

float U() {
    return theme::unit();
}

std::string folded(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::string trimmed(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
    return text.substr(begin, end - begin);
}

int compareFolded(const std::string& a, const std::string& b) {
    return folded(a).compare(folded(b));
}

bool hasArchiveExtension(const fs::path& path) {
    const std::string extension = folded(pathToUtf8(path.extension()));
    return extension == ".hpi" || extension == ".ufo" || extension == ".ccx" || extension == ".gp3" || extension == ".gpf";
}

ImU32 rgba(int r, int g, int b, int a = 255) {
    return IM_COL32(r, g, b, a);
}

ImU32 withAlpha(ImU32 color, int alpha) {
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

ImU32 secondaryText() {
    return ImGui::ColorConvertFloat4ToU32(theme::secondaryTextColor());
}

// The step of a UTF-8 string that starts a character.
bool startsCharacter(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
}

// Shortens text with an ellipsis so it fits maxWidth, using the current font.
std::string fitEnd(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    const std::string dots = theme::ellipsis();
    std::string cut = text;
    while (!cut.empty()) {
        do {
            cut.pop_back();
        } while (!cut.empty() && !startsCharacter(cut.back()));
        if (ImGui::CalcTextSize((cut + dots).c_str()).x <= maxWidth) break;
    }
    return cut + dots;
}

// Splits a name over two lines for the icon view.
std::pair<std::string, std::string> splitLabel(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return {text, std::string()};
    std::size_t fit = 0;
    for (std::size_t i = 1; i <= text.size(); ++i) {
        if (i < text.size() && !startsCharacter(text[i])) continue;
        if (ImGui::CalcTextSize(text.substr(0, i).c_str()).x > maxWidth) break;
        fit = i;
    }
    if (fit == 0) fit = 1;
    return {text.substr(0, fit), fitEnd(text.substr(fit), maxWidth)};
}

std::string humanSize(std::uint64_t bytes) {
    char buffer[48];
    if (bytes < 1000) {
        std::snprintf(buffer, sizeof(buffer), "%llu %s", static_cast<unsigned long long>(bytes), bytes == 1 ? "byte" : "bytes");
        return buffer;
    }
    static const char* const kUnits[] = {"KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = -1;
    while (value >= 1000.0 && unit < 3) {
        value /= 1000.0;
        ++unit;
    }
    std::snprintf(buffer, sizeof(buffer), value >= 100.0 || unit == 0 ? "%.0f %s" : "%.1f %s", value, kUnits[unit]);
    return buffer;
}

std::string countText(int count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

struct KindInfo {
    std::string name;
    ImU32 color;
    std::string label;
};

KindInfo kindOf(const ArchiveItem& item) {
    if (item.directory) return {"Folder", rgba(104, 176, 244), ""};
    const std::size_t dot = item.name.rfind('.');
    const std::string extension = dot == std::string::npos ? std::string() : folded(item.name.substr(dot + 1));
    std::string label = extension;
    for (char& c : label) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    if (label.size() > 4) label.resize(4);
    struct Known {
        const char* extension;
        const char* name;
        ImU32 color;
    };
    static const Known kKnown[] = {
        {"tnt", "TA Map", rgba(70, 168, 96)},
        {"sct", "Map Section", rgba(70, 168, 96)},
        {"ota", "Map Description", rgba(66, 132, 226)},
        {"fbi", "Unit Definition", rgba(66, 132, 226)},
        {"tdf", "Data File", rgba(66, 132, 226)},
        {"gui", "Interface Layout", rgba(66, 132, 226)},
        {"txt", "Text Document", rgba(98, 130, 176)},
        {"cob", "Compiled Script", rgba(234, 140, 40)},
        {"bos", "Script Source", rgba(234, 140, 40)},
        {"h", "Script Header", rgba(234, 140, 40)},
        {"gaf", "Animation Archive", rgba(150, 96, 206)},
        {"pcx", "PCX Image", rgba(150, 96, 206)},
        {"bmp", "Bitmap Image", rgba(150, 96, 206)},
        {"jpg", "JPEG Image", rgba(150, 96, 206)},
        {"jpeg", "JPEG Image", rgba(150, 96, 206)},
        {"png", "PNG Image", rgba(150, 96, 206)},
        {"tga", "Targa Image", rgba(150, 96, 206)},
        {"3do", "3D Model", rgba(40, 168, 172)},
        {"wav", "Audio", rgba(224, 84, 136)},
        {"hpi", "Archive", rgba(168, 120, 70)},
        {"ufo", "Archive", rgba(168, 120, 70)},
        {"ccx", "Archive", rgba(168, 120, 70)},
        {"gp3", "Archive", rgba(168, 120, 70)},
        {"gpf", "Archive", rgba(168, 120, 70)},
    };
    for (const Known& known : kKnown) {
        if (extension == known.extension) return {known.name, known.color, label};
    }
    if (extension.empty()) return {"Document", rgba(146, 150, 158), ""};
    return {label + " File", rgba(146, 150, 158), label};
}

// ------------------------------------------------------------------ pictures

void drawFolderIcon(ImDrawList* draw, ImVec2 min, float size) {
    const float x0 = min.x;
    const float x1 = min.x + size;
    const float y0 = min.y + size * 0.10f;
    const float y1 = y0 + size * 0.80f;
    const float r = std::max(1.5f, size * 0.075f);
    const ImU32 back = rgba(66, 130, 212);
    const ImU32 frontBottom = rgba(98, 172, 244);
    const ImU32 frontTop = rgba(158, 212, 254);
    draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + size * 0.44f, y0 + size * 0.24f), back, r);
    draw->AddRectFilled(ImVec2(x0, y0 + size * 0.09f), ImVec2(x1, y1), back, r);
    const float fy = y0 + size * 0.22f;
    draw->AddRectFilled(ImVec2(x0, fy), ImVec2(x1, y1), frontBottom, r);
    draw->AddRectFilled(ImVec2(x0, fy), ImVec2(x1, fy + (y1 - fy) * 0.55f), withAlpha(frontTop, 190), r, ImDrawFlags_RoundCornersTop);
    draw->AddRect(ImVec2(x0, fy), ImVec2(x1, y1), rgba(44, 100, 180, 150), r, 0, 1.0f);
    draw->AddLine(ImVec2(x0 + r, fy + 1.0f), ImVec2(x1 - r, fy + 1.0f), rgba(255, 255, 255, 130));
}

void drawFileIcon(ImDrawList* draw, ImVec2 min, float size, const KindInfo& kind) {
    const float w = size * 0.74f;
    const float h = size * 0.94f;
    const float x0 = min.x + (size - w) * 0.5f;
    const float y0 = min.y + (size - h) * 0.5f;
    const float x1 = x0 + w;
    const float y1 = y0 + h;
    const float fold = w * 0.32f;
    const ImU32 edge = rgba(158, 160, 168);
    const ImU32 paper = theme::dark() ? rgba(238, 238, 242) : rgba(255, 255, 255);
    if (size >= 30.0f) {
        draw->AddRectFilled(ImVec2(x0 + 1.0f, y0 + 2.0f), ImVec2(x1 + 1.0f, y1 + 2.0f), rgba(0, 0, 0, 38), size * 0.04f);
    }
    const ImVec2 outline[5] = {ImVec2(x0, y0), ImVec2(x1 - fold, y0), ImVec2(x1, y0 + fold), ImVec2(x1, y1), ImVec2(x0, y1)};
    draw->AddConvexPolyFilled(outline, 5, paper);
    draw->AddPolyline(outline, 5, edge, ImDrawFlags_Closed, 1.0f);
    const ImVec2 corner[3] = {ImVec2(x1 - fold, y0), ImVec2(x1 - fold, y0 + fold), ImVec2(x1, y0 + fold)};
    draw->AddConvexPolyFilled(corner, 3, rgba(214, 216, 224));
    draw->AddPolyline(corner, 3, edge, ImDrawFlags_Closed, 1.0f);
    const bool big = size >= 34.0f;
    const float bandTop = y1 - h * (big ? 0.34f : 0.30f);
    if (big) {
        for (int i = 0; i < 2; ++i) {
            const float ly = y0 + fold + h * 0.10f + static_cast<float>(i) * h * 0.11f;
            draw->AddLine(ImVec2(x0 + w * 0.16f, ly), ImVec2(x1 - w * 0.16f, ly), rgba(206, 208, 216), 1.0f);
        }
    }
    draw->AddRectFilled(ImVec2(x0 + 1.0f, bandTop), ImVec2(x1 - 1.0f, y1 - 1.0f), kind.color, 1.5f, ImDrawFlags_RoundCornersBottom);
    if (big && !kind.label.empty()) {
        ImFont* font = ImGui::GetFont();
        const float fontSize = std::max(8.0f, size * 0.20f);
        const ImVec2 text = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, kind.label.c_str());
        draw->AddText(font, fontSize, ImVec2(x0 + (w - text.x) * 0.5f, bandTop + (y1 - 1.0f - bandTop - text.y) * 0.5f), rgba(255, 255, 255), kind.label.c_str());
    }
}

void drawItemIcon(ImDrawList* draw, ImVec2 min, float size, const ArchiveItem& item) {
    if (item.directory) {
        drawFolderIcon(draw, min, size);
    } else {
        drawFileIcon(draw, min, size, kindOf(item));
    }
}

void drawDisclosure(ImDrawList* draw, ImVec2 center, float size, bool open, ImU32 color) {
    const float s = size * 0.5f;
    if (open) {
        draw->AddTriangleFilled(ImVec2(center.x - s, center.y - s * 0.55f), ImVec2(center.x + s, center.y - s * 0.55f), ImVec2(center.x, center.y + s * 0.65f), color);
    } else {
        draw->AddTriangleFilled(ImVec2(center.x - s * 0.55f, center.y - s), ImVec2(center.x - s * 0.55f, center.y + s), ImVec2(center.x + s * 0.65f, center.y), color);
    }
}

void drawChevron(ImDrawList* draw, ImVec2 c, float s, ImU32 color, float direction) {
    const float h = s * 0.46f;
    const float w = s * 0.26f;
    draw->PathLineTo(ImVec2(c.x + direction * w, c.y - h));
    draw->PathLineTo(ImVec2(c.x - direction * w, c.y));
    draw->PathLineTo(ImVec2(c.x + direction * w, c.y + h));
    draw->PathStroke(color, 0, std::max(1.5f, s * 0.16f));
}

void glyphBack(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    drawChevron(draw, c, s, color, 1.0f);
}

void glyphForward(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    drawChevron(draw, c, s, color, -1.0f);
}

void glyphIcons(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    const float a = s * 0.40f;
    const float gap = s * 0.14f;
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            const float x = c.x - gap * 0.5f - a + static_cast<float>(col) * (a + gap);
            const float y = c.y - gap * 0.5f - a + static_cast<float>(row) * (a + gap);
            draw->AddRectFilled(ImVec2(x, y), ImVec2(x + a, y + a), color, s * 0.07f);
        }
    }
}

void glyphList(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    for (int i = -1; i <= 1; ++i) {
        const float y = c.y + static_cast<float>(i) * s * 0.34f;
        draw->AddRectFilled(ImVec2(c.x - s * 0.5f, y - s * 0.07f), ImVec2(c.x - s * 0.36f, y + s * 0.07f), color);
        draw->AddRectFilled(ImVec2(c.x - s * 0.22f, y - s * 0.07f), ImVec2(c.x + s * 0.5f, y + s * 0.07f), color);
    }
}

void drawMagnifier(ImDrawList* draw, ImVec2 c, float s, ImU32 color) {
    const float r = s * 0.30f;
    const ImVec2 lens(c.x - s * 0.08f, c.y - s * 0.08f);
    draw->AddCircle(lens, r, color, 16, std::max(1.2f, s * 0.12f));
    draw->AddLine(ImVec2(lens.x + r * 0.72f, lens.y + r * 0.72f), ImVec2(c.x + s * 0.42f, c.y + s * 0.42f), color, std::max(1.2f, s * 0.14f));
}

// A small pill with a number, over the corner of a dragged picture.
void drawCountBadge(ImDrawList* draw, ImVec2 center, int count) {
    const std::string text = std::to_string(count);
    const float height = std::floor(18.0f * U());
    const float width = std::max(height, ImGui::CalcTextSize(text.c_str()).x + 10.0f * U());
    const ImVec2 min(center.x - width * 0.5f, center.y - height * 0.5f);
    const ImVec2 max(center.x + width * 0.5f, center.y + height * 0.5f);
    draw->AddRectFilled(min, max, rgba(236, 68, 62), height * 0.5f);
    draw->AddRect(min, max, rgba(255, 255, 255, 230), height * 0.5f, 0, 1.5f);
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    draw->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), rgba(255, 255, 255), text.c_str());
}

}  // namespace

// --------------------------------------------------------------------- public

void HpiEditor::show() {
    visible_ = true;
}

std::string HpiEditor::takeError() {
    std::string out;
    out.swap(error_);
    return out;
}

std::string HpiEditor::takeStatus() {
    std::string out;
    out.swap(status_);
    return out;
}

bool HpiEditor::takeQuitApproval() {
    const bool approved = quitApproved_;
    quitApproved_ = false;
    return approved;
}

void HpiEditor::requestQuit() {
    visible_ = true;
    askThen(Then::Quit);
}

void HpiEditor::importPaths(const std::vector<fs::path>& paths) {
    if (paths.empty() || !visible_ || sheet_ != Sheet::None) return;
    if (!hasDocument_) {
        if (paths.size() == 1 && hasArchiveExtension(paths[0])) {
            openArchive(paths[0]);
            return;
        }
        createBlank();
    }
    beginImport(paths);
}

void HpiEditor::note(const std::string& text) {
    note_ = text;
    noteUntil_ = ImGui::GetTime() + 7.0;
    status_ = text;
}

// ---------------------------------------------------------- archive lifecycle

void HpiEditor::discardDocument() {
    document_ = ArchiveDocument();
    hasDocument_ = false;
    folder_.clear();
    back_.clear();
    forward_.clear();
    expanded_.clear();
    selection_.clear();
    anchor_.clear();
    cursor_.clear();
    search_[0] = '\0';
    listingValid_ = false;
}

void HpiEditor::createBlank() {
    discardDocument();
    hasDocument_ = true;
    scrollReset_ = true;
}

void HpiEditor::openArchive(const fs::path& path) {
    try {
        ArchiveDocument opened = ArchiveDocument::open(path);
        discardDocument();
        document_ = std::move(opened);
        hasDocument_ = true;
        lastDirectory_ = path.parent_path();
        scrollReset_ = true;
        std::string message = "Opened " + document_.displayName() + ": " + countText(document_.fileCount(), "file", "files");
        if (document_.droppedDuplicates() > 0) {
            message += ". " + countText(document_.droppedDuplicates(), "repeated name was", "repeated names were") + " left out";
        }
        note(message);
    } catch (const std::exception& ex) {
        error_ = std::string("Could not open ") + pathToUtf8(path.filename()) + ". " + ex.what();
    }
}

void HpiEditor::askThen(Then then) {
    if (!dirty()) {
        runThen(then);
        return;
    }
    then_ = then;
    sheet_ = Sheet::Unsaved;
}

void HpiEditor::runThen(Then then) {
    switch (then) {
    case Then::Nothing: break;
    case Then::NewArchive: createBlank(); break;
    case Then::OpenArchive: openArchive(thenPath_); break;
    case Then::CloseWindow:
        if (dirty()) discardDocument();
        visible_ = false;
        break;
    case Then::Quit: quitApproved_ = true; break;
    }
}

void HpiEditor::requestOpen(const fs::path& path) {
    thenPath_ = path;
    askThen(Then::OpenArchive);
}

void HpiEditor::requestNew() {
    askThen(Then::NewArchive);
}

void HpiEditor::requestCloseWindow() {
    askThen(Then::CloseWindow);
}

void HpiEditor::save() {
    if (!hasDocument_) return;
    if (document_.path().empty()) {
        saveAs();
        return;
    }
    try {
        const int empty = document_.emptyFolderCount();
        document_.save();
        note("Saved " + document_.displayName() + (empty > 0 ? ". Empty folders are not stored in an archive" : ""));
    } catch (const std::exception& ex) {
        error_ = std::string("Could not save the archive. ") + ex.what();
    }
}

bool HpiEditor::saveAs() {
    if (!hasDocument_) return false;
    const fs::path suggested = document_.path().empty() ? fs::path("Untitled.hpi") : document_.path().filename();
    const fs::path directory = document_.path().empty() ? lastDirectory_ : document_.path().parent_path();
    const auto chosen = saveArchiveDialog(directory, suggested);
    if (!chosen) return false;
    fs::path path = *chosen;
    if (!path.has_extension()) path += ".hpi";
    try {
        const int empty = document_.emptyFolderCount();
        document_.saveAs(path);
        lastDirectory_ = path.parent_path();
        note("Saved " + document_.displayName() + (empty > 0 ? ". Empty folders are not stored in an archive" : ""));
        return true;
    } catch (const std::exception& ex) {
        error_ = std::string("Could not save the archive. ") + ex.what();
        return false;
    }
}

// ------------------------------------------------------------ changing things

void HpiEditor::undo() {
    if (!hasDocument_ || !document_.canUndo()) return;
    document_.undo();
    note("Undid the last change");
}

void HpiEditor::redo() {
    if (!hasDocument_ || !document_.canRedo()) return;
    document_.redo();
    note("Redid the change");
}

void HpiEditor::beginImport(const std::vector<fs::path>& sources) {
    if (!hasDocument_ || sources.empty()) return;
    clearSearch();
    ImportPlan plan = document_.planImport(folder_, sources);
    if (plan.files.empty()) {
        note(plan.skipped > 0 ? "Nothing could be imported. " + countText(plan.skipped, "item was", "items were") + " skipped" : "There was nothing to import");
        return;
    }
    pending_ = PendingOperation();
    pending_.kind = Operation::Import;
    pending_.conflicts = plan.conflicts;
    pending_.importPlan = std::move(plan);
    if (pending_.conflicts.empty()) {
        finishOperation(false);
    } else {
        sheet_ = Sheet::Conflict;
    }
}

void HpiEditor::chooseImport(bool folders) {
    if (!hasDocument_) return;
    std::vector<fs::path> chosen;
    if (folders) {
        if (const auto folder = chooseFolderDialog("Choose a folder to import", lastDirectory_)) chosen.push_back(*folder);
    } else {
        chosen = chooseFilesDialog("Choose files to import into this folder", lastDirectory_);
    }
    if (chosen.empty()) return;
    if (chosen.front().has_parent_path()) lastDirectory_ = chosen.front().parent_path();
    beginImport(chosen);
}

void HpiEditor::moveItems(const std::vector<std::string>& paths, const std::string& destination) {
    if (!hasDocument_ || paths.empty()) return;
    MovePlan plan = document_.planMove(paths, destination);
    if (!plan.problem.empty()) {
        note(plan.problem);
        return;
    }
    if (plan.moves.empty()) return;
    pending_ = PendingOperation();
    pending_.kind = Operation::Move;
    pending_.conflicts = plan.conflicts;
    pending_.movePlan = std::move(plan);
    if (pending_.conflicts.empty()) {
        finishOperation(false);
    } else {
        sheet_ = Sheet::Conflict;
    }
}

void HpiEditor::beginExport() {
    if (!hasDocument_) return;
    if (document_.fileCount() == 0) {
        note("This archive has no files to export");
        return;
    }
    std::vector<std::string> paths = selectedPaths();
    if (paths.empty()) paths.push_back(folder_);
    const auto chosen = chooseFolderDialog("Choose where to export", lastDirectory_);
    if (!chosen) return;
    lastDirectory_ = *chosen;
    fs::path destination = *chosen;
    if (paths.size() == 1 && paths[0].empty()) {
        // Everything goes into a folder of its own instead of being scattered over the chosen one.
        std::string stem = pathToUtf8(document_.path().stem());
        if (stem.empty()) stem = "Untitled";
        destination /= pathFromUtf8(stem);
    }
    if (document_.filePathsUnder(paths.size() == 1 ? paths[0] : std::string()).empty() && paths.size() == 1) {
        note("There are no files to export in that folder");
        return;
    }
    pending_ = PendingOperation();
    pending_.kind = Operation::Export;
    pending_.exportPaths = paths;
    pending_.exportDestination = destination;
    pending_.conflicts = document_.exportConflicts(paths, destination);
    if (pending_.conflicts.empty()) {
        finishOperation(false);
    } else {
        sheet_ = Sheet::Conflict;
    }
}

void HpiEditor::finishOperation(bool replace) {
    try {
        switch (pending_.kind) {
        case Operation::Import: {
            const ImportPlan& plan = pending_.importPlan;
            const int count = document_.applyImport(plan, replace);
            selection_.clear();
            for (const ImportEntry& entry : plan.files) {
                const std::string relative = plan.folder.empty() ? entry.path : entry.path.substr(plan.folder.size() + 1);
                const std::size_t slash = relative.find('/');
                selection_.insert(joinArchivePath(plan.folder, relative.substr(0, slash)));
            }
            anchor_ = selection_.empty() ? std::string() : *selection_.begin();
            cursor_ = anchor_;
            std::string message = "Imported " + countText(count, "file", "files") + " into " + (plan.folder.empty() ? document_.displayName() : plan.folder);
            if (plan.skipped > 0) message += ". " + countText(plan.skipped, "item was", "items were") + " skipped";
            note(message);
            break;
        }
        case Operation::Move: {
            const MovePlan& plan = pending_.movePlan;
            const int count = document_.applyMove(plan, replace);
            selection_.clear();
            if (!plan.moves.empty() && archiveParentPath(plan.moves.front().second) == folder_) {
                for (const auto& move : plan.moves) selection_.insert(move.second);
            }
            const std::string target = archiveParentPath(plan.moves.front().second);
            note("Moved " + countText(count, "item", "items") + " to " + (target.empty() ? document_.displayName() : archiveLeafName(target)));
            break;
        }
        case Operation::Export: {
            const ExportResult result = document_.exportTo(pending_.exportPaths, pending_.exportDestination, replace);
            std::string message = "Exported " + countText(result.files, "file", "files") + " to " + pathToUtf8(pending_.exportDestination.filename().empty() ? pending_.exportDestination : pending_.exportDestination.filename());
            if (result.skipped > 0) message += ". " + countText(result.skipped, "file was", "files were") + " skipped";
            note(message);
            break;
        }
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
    pending_ = PendingOperation();
    listingValid_ = false;
}

void HpiEditor::deleteSelection() {
    if (!hasDocument_ || selection_.empty()) return;
    const std::vector<std::string> paths = selectedPaths();
    const int count = document_.remove(paths);
    selection_.clear();
    anchor_.clear();
    cursor_.clear();
    note("Deleted " + countText(count, "item", "items") + ". Undo brings " + (count == 1 ? "it" : "them") + " back");
}

void HpiEditor::beginName(NameFor purpose, const std::string& initial) {
    nameFor_ = purpose;
    std::snprintf(nameBuffer_, sizeof(nameBuffer_), "%s", initial.c_str());
    nameFocus_ = true;
    sheet_ = Sheet::Name;
}

void HpiEditor::commitName() {
    const std::string name = trimmed(nameBuffer_);
    try {
        if (nameFor_ == NameFor::Rename) {
            document_.rename(namePath_, name);
            const std::string renamed = joinArchivePath(archiveParentPath(namePath_), name);
            selection_ = {renamed};
            anchor_ = cursor_ = renamed;
            note("Renamed to " + name);
        } else {
            const std::string created = document_.createFolder(folder_, name);
            selection_ = {created};
            anchor_ = cursor_ = created;
            scrollTo_ = -2;  // find it once the listing is rebuilt
            note("Created folder " + name);
        }
    } catch (const std::exception& ex) {
        error_ = ex.what();
    }
}

// -------------------------------------------------------------------- browsing

void HpiEditor::clearSearch() {
    search_[0] = '\0';
    listingValid_ = false;
}

void HpiEditor::navigate(const std::string& target, bool remember) {
    std::string folder = hasDocument_ ? document_.canonicalPath(normalizeArchivePath(target)) : std::string();
    if (!document_.isFolder(folder)) folder.clear();
    if (remember && folder != folder_) {
        back_.push_back(folder_);
        forward_.clear();
    }
    folder_ = folder;
    for (std::string parent = archiveParentPath(folder_); !parent.empty(); parent = archiveParentPath(parent)) {
        expanded_.insert(folded(parent));
    }
    selection_.clear();
    anchor_.clear();
    cursor_.clear();
    clearSearch();
    scrollReset_ = true;
}

void HpiEditor::goBack() {
    if (back_.empty()) return;
    const std::string previous = back_.back();
    back_.pop_back();
    forward_.push_back(folder_);
    navigate(previous, false);
}

void HpiEditor::goForward() {
    if (forward_.empty()) return;
    const std::string next = forward_.back();
    forward_.pop_back();
    back_.push_back(folder_);
    navigate(next, false);
}

void HpiEditor::goUp() {
    if (folder_.empty()) return;
    const std::string leaving = folder_;
    navigate(archiveParentPath(folder_));
    selection_ = {leaving};
    anchor_ = cursor_ = leaving;
}

void HpiEditor::openItem(const ArchiveItem& item) {
    if (item.directory) {
        navigate(item.path);
        return;
    }
    // A file found by searching: show it where it lives.
    if (search_[0] != '\0') {
        navigate(archiveParentPath(item.path));
        selection_ = {item.path};
        anchor_ = cursor_ = item.path;
        scrollTo_ = -2;
    }
}

void HpiEditor::rebuildListing() {
    const std::string query = trimmed(search_);
    listing_ = query.empty() ? document_.children(folder_) : document_.search(folder_, query);
    const bool searching = !query.empty();
    const int column = sortColumn_;
    const bool ascending = sortAscending_;
    std::stable_sort(listing_.begin(), listing_.end(), [&](const ArchiveItem& a, const ArchiveItem& b) {
        if (a.directory != b.directory) return a.directory;
        int order = 0;
        if (column == 1) {
            const std::uint64_t left = a.directory ? static_cast<std::uint64_t>(a.fileCount) : a.size;
            const std::uint64_t right = b.directory ? static_cast<std::uint64_t>(b.fileCount) : b.size;
            order = left < right ? -1 : (left > right ? 1 : 0);
        } else if (column == 2) {
            order = searching ? compareFolded(archiveParentPath(a.path), archiveParentPath(b.path)) : compareFolded(kindOf(a).name, kindOf(b).name);
        } else {
            order = compareFolded(a.name, b.name);
        }
        if (!ascending) order = -order;
        if (order == 0) order = compareFolded(a.name, b.name);
        return order < 0;
    });
    for (auto it = selection_.begin(); it != selection_.end();) {
        it = document_.find(*it) ? std::next(it) : selection_.erase(it);
    }
    listingGeneration_ = document_.generation();
    listingFolder_ = folder_;
    listingQuery_ = query;
    listingSortColumn_ = column;
    listingSortAscending_ = ascending;
    listingValid_ = true;
    if (scrollTo_ == -2) {
        scrollTo_ = indexOfPath(cursor_);
    }
}

int HpiEditor::indexOfPath(const std::string& path) const {
    for (std::size_t i = 0; i < listing_.size(); ++i) {
        if (listing_[i].path == path) return static_cast<int>(i);
    }
    return -1;
}

void HpiEditor::select(const std::string& path) {
    selection_ = {path};
    anchor_ = path;
    cursor_ = path;
}

void HpiEditor::toggle(const std::string& path) {
    if (!selection_.erase(path)) selection_.insert(path);
    anchor_ = path;
    cursor_ = path;
}

void HpiEditor::selectRange(std::size_t to) {
    if (to >= listing_.size()) return;
    const int from = indexOfPath(anchor_);
    if (from < 0) {
        select(listing_[to].path);
        return;
    }
    const std::size_t lo = std::min(static_cast<std::size_t>(from), to);
    const std::size_t hi = std::max(static_cast<std::size_t>(from), to);
    selection_.clear();
    for (std::size_t i = lo; i <= hi; ++i) selection_.insert(listing_[i].path);
    cursor_ = listing_[to].path;
}

void HpiEditor::selectAll() {
    selection_.clear();
    for (const ArchiveItem& item : listing_) selection_.insert(item.path);
    if (!listing_.empty()) {
        anchor_ = listing_.front().path;
        cursor_ = listing_.back().path;
    }
}

std::vector<std::string> HpiEditor::selectedPaths() const {
    std::vector<std::string> out;
    // In the order they are shown, then any that are not shown.
    for (const ArchiveItem& item : listing_) {
        if (selection_.count(item.path)) out.push_back(item.path);
    }
    for (const std::string& path : selection_) {
        if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    }
    return out;
}

bool HpiEditor::canDropInto(const std::string& folder) const {
    if (dragPaths_.empty()) return false;
    bool anyMoves = false;
    for (const std::string& path : dragPaths_) {
        if (folded(path) == folded(folder)) return false;
        if (archivePathIsInside(folder, path)) return false;
        if (folded(archiveParentPath(path)) != folded(folder)) anyMoves = true;
    }
    return anyMoves;
}

bool HpiEditor::dropTarget(const std::string& folder) {
    if (!ImGui::BeginDragDropTarget()) return false;
    bool hot = false;
    const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
        kDragType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
    if (payload != nullptr && canDropInto(folder)) {
        hot = true;
        if (payload->IsDelivery()) {
            const std::vector<std::string> items = dragPaths_;
            moveItems(items, folder);
        }
    }
    ImGui::EndDragDropTarget();
    return hot;
}

void HpiEditor::interact(const ArchiveItem& item, int index) {
    ImGuiIO& io = ImGui::GetIO();
    const bool extend = io.KeyShift;
    const bool toggling = io.KeyCtrl;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (extend) {
            selectRange(static_cast<std::size_t>(index));
        } else if (toggling) {
            toggle(item.path);
        } else if (!selection_.count(item.path)) {
            select(item.path);
        } else {
            // Already selected: a click without a drag narrows the selection to this item.
            pendingCollapse_ = item.path;
            cursor_ = item.path;
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && pendingCollapse_ == item.path) {
        if (io.MouseDragMaxDistanceSqr[0] < 16.0f) select(item.path);
        pendingCollapse_.clear();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (!selection_.count(item.path)) select(item.path);
        openItemMenu_ = true;
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        pendingCollapse_.clear();
        openItem(item);
    }
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
        if (!selection_.count(item.path)) select(item.path);
        dragPaths_ = selectedPaths();
        const int token = 1;
        ImGui::SetDragDropPayload(kDragType, &token, sizeof(token));
        ImGui::EndDragDropSource();
    }
}

// --------------------------------------------------------------------- drawing

void HpiEditor::draw() {
    if (!visible_) {
        focused_ = false;
        return;
    }
    if (hasDocument_ && !document_.isFolder(folder_)) {
        std::string parent = folder_;
        while (!parent.empty() && !document_.isFolder(parent)) parent = archiveParentPath(parent);
        folder_ = document_.canonicalPath(parent);
        selection_.clear();
        listingValid_ = false;
    }

    const float u = U();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 120.0f * u, viewport->WorkPos.y + 60.0f * u), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::floor(1140.0f * u), std::floor(700.0f * u)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::floor(1000.0f * u), std::floor(480.0f * u)), ImVec2(FLT_MAX, FLT_MAX));

    std::string title = "HPI Editor";
    if (hasDocument_) {
        title += " \xE2\x80\x94 " + document_.displayName();
        if (document_.dirty()) title += " \xE2\x80\x94 Edited";
    }
    title += "###hpi-editor";

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool open = true;
    const bool shown = ImGui::Begin(
        title.c_str(), &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!open && sheet_ == Sheet::None) requestCloseWindow();
    focused_ = shown && (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || sheet_ != Sheet::None);
    if (!shown) {
        ImGui::End();
        return;
    }
    windowCenter_ = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x * 0.5f, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y * 0.5f);

    const ImVec2 total = ImGui::GetContentRegionAvail();
    const float toolbarHeight = theme::toolbarHeight();
    const float pathHeight = std::floor(26.0f * u);
    const float statusHeight = std::floor(ImGui::GetFrameHeight() + 2.0f);
    const float bodyHeight = std::max(1.0f, total.y - toolbarHeight - pathHeight - statusHeight);

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * u, std::floor((toolbarHeight - theme::toolbarItemHeight()) * 0.5f)));
    ImGui::BeginChild("##hpi-toolbar", ImVec2(total.x, toolbarHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    theme::toolbarBackground();
    drawToolbar();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, toolbarHeight));
    drawBody(bodyHeight);

    ImGui::SetCursorPos(ImVec2(0.0f, toolbarHeight + bodyHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * u, std::floor((pathHeight - ImGui::GetFontSize() * 11.0f / 13.0f) * 0.5f) - 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    ImGui::BeginChild("##hpi-path", ImVec2(total.x, pathHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    drawPathBar();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(0.0f, toolbarHeight + bodyHeight + pathHeight));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * u, std::floor((statusHeight - ImGui::GetFontSize() * 11.0f / 13.0f) * 0.5f)));
    ImGui::BeginChild("##hpi-status", ImVec2(total.x, statusHeight), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    theme::bottomBarBackground();
    drawStatus();
    ImGui::EndChild();

    drawMenus();
    drawSheets();
    if (focused_ && sheet_ == Sheet::None) handleKeys();
    ImGui::End();
    drawDragPreview();
}

void HpiEditor::drawToolbar() {
    using theme::ToolIcon;
    const float u = U();
    const float gap = theme::toolbarGap();
    const float frameHeight = ImGui::GetFrameHeight();
    const float topY = ImGui::GetCursorPosY();
    const float startX = ImGui::GetCursorPosX();
    const float centerY = topY + std::floor((theme::toolbarItemHeight() - frameHeight) * 0.5f);
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const auto next = [&]() {
        ImGui::SameLine(0.0f, gap);
        ImGui::SetCursorPosY(topY);
    };
    const auto tip = [](const char* text) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
    };
    const bool has = hasDocument_;

    // Centered on the icons and labels beside it, in a group so the separator after it lines up.
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(centerY);
    static const theme::GlyphPainter kNavGlyphs[] = {glyphBack, glyphForward};
    static const char* const kNavTips[] = {"Back", "Forward"};
    const bool navEnabled[2] = {has && !back_.empty(), has && !forward_.empty()};
    const int nav = theme::iconSegmented("hpi-nav", kNavGlyphs, 2, -1, navEnabled, kNavTips);
    ImGui::SetCursorPosY(topY);
    ImGui::Dummy(ImVec2(0.0f, theme::toolbarItemHeight()));
    ImGui::EndGroup();
    if (nav == 0) goBack();
    if (nav == 1) goForward();
    theme::toolbarSeparator();
    ImGui::SetCursorPosY(topY);

    if (theme::toolbarItem("hpi-new", ToolIcon::NewArchive, "New")) requestNew();
    tip("New Archive\nStart an empty archive, then import files into it");
    next();
    if (theme::toolbarItem("hpi-open", ToolIcon::OpenArchive, "Open")) {
        if (const auto chosen = openArchiveDialog(lastDirectory_)) requestOpen(*chosen);
    }
    tip("Open Archive\nBrowse an HPI, UFO, CCX, GP3 or GPF file");
    next();
    ImGui::BeginDisabled(!has || !document_.dirty());
    if (theme::toolbarItem("hpi-save", ToolIcon::SaveArchive, "Save")) save();
    ImGui::EndDisabled();
    tip("Save\nWrite your changes into the archive file");
    next();
    ImGui::BeginDisabled(!has);
    if (theme::toolbarItem("hpi-save-as", ToolIcon::SaveArchiveAs, "Save As")) saveAs();
    ImGui::EndDisabled();
    tip("Save As\nWrite the archive to a new file");
    theme::toolbarSeparator();
    ImGui::SetCursorPosY(topY);

    ImGui::BeginDisabled(!has);
    if (theme::toolbarItem("hpi-import", ToolIcon::Import, "Import")) chooseImport(false);
    ImGui::EndDisabled();
    tip("Import\nAdd files or folders from your computer to the folder you are viewing. You can also drop them on this window");
    next();
    ImGui::BeginDisabled(!has || document_.fileCount() == 0);
    if (theme::toolbarItem("hpi-export", ToolIcon::Export, "Export")) beginExport();
    ImGui::EndDisabled();
    tip("Export\nCopy the selected items to a folder on your computer. With nothing selected, the folder you are viewing is exported");
    next();
    ImGui::BeginDisabled(!has);
    if (theme::toolbarItem("hpi-new-folder", ToolIcon::NewFolder, "New Folder")) beginName(NameFor::NewFolder, "untitled folder");
    ImGui::EndDisabled();
    tip("New Folder\nMake a folder inside the folder you are viewing");
    next();
    ImGui::BeginDisabled(!has || selection_.empty());
    if (theme::toolbarItem("hpi-delete", ToolIcon::Delete, "Delete")) deleteSelection();
    ImGui::EndDisabled();
    tip("Delete\nRemove the selected items from the archive. Undo brings them back");
    theme::toolbarSeparator();
    ImGui::SetCursorPosY(topY);

    ImGui::BeginDisabled(!has || !document_.canUndo());
    if (theme::toolbarItem("hpi-undo", ToolIcon::Undo, "Undo")) undo();
    ImGui::EndDisabled();
    tip("Undo\nTake back the last change");
    next();
    ImGui::BeginDisabled(!has || !document_.canRedo());
    if (theme::toolbarItem("hpi-redo", ToolIcon::Redo, "Redo")) redo();
    ImGui::EndDisabled();
    tip("Redo\nDo the change again");

    // View switch and search sit at the right edge.
    const float segmentsWidth = std::floor(34.0f * u) * 2.0f;
    const float searchWidth = std::floor(190.0f * u);
    const float rightWidth = segmentsWidth + gap * 2.0f + searchWidth;
    float offset = startX + fullWidth - rightWidth;
    const float minimum = ImGui::GetCursorPosX() + gap * 2.0f;
    ImGui::SameLine(std::max(offset, minimum));
    ImGui::SetCursorPosY(centerY);
    static const theme::GlyphPainter kViewGlyphs[] = {glyphIcons, glyphList};
    static const char* const kViewTips[] = {"View as Icons", "View as List"};
    const int viewed = theme::iconSegmented("hpi-view", kViewGlyphs, 2, view_ == View::Icons ? 0 : 1, nullptr, kViewTips);
    if (viewed == 0) view_ = View::Icons;
    if (viewed == 1) view_ = View::List;
    ImGui::SameLine(0.0f, gap * 2.0f);
    ImGui::SetCursorPosY(centerY);

    ImGui::BeginDisabled(!has);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::floor(frameHeight * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::floor(24.0f * u), ImGui::GetStyle().FramePadding.y));
    ImGui::SetNextItemWidth(searchWidth);
    ImGui::InputTextWithHint("##hpi-search", "Search", search_, sizeof(search_));
    ImGui::PopStyleVar(2);
    const ImVec2 boxMin = ImGui::GetItemRectMin();
    const ImVec2 boxMax = ImGui::GetItemRectMax();
    const bool boxHovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float glyph = std::floor(frameHeight * 0.55f);
    drawMagnifier(draw, ImVec2(boxMin.x + std::floor(12.0f * u), (boxMin.y + boxMax.y) * 0.5f), glyph, secondaryText());
    if (search_[0] != '\0') {
        const ImVec2 center(boxMax.x - std::floor(12.0f * u), (boxMin.y + boxMax.y) * 0.5f);
        const float radius = glyph * 0.42f;
        const bool over = ImGui::IsMouseHoveringRect(ImVec2(center.x - radius * 1.6f, boxMin.y), ImVec2(center.x + radius * 1.6f, boxMax.y));
        draw->AddCircleFilled(center, radius, over ? rgba(120, 124, 132) : rgba(168, 172, 180));
        const float arm = radius * 0.42f;
        draw->AddLine(ImVec2(center.x - arm, center.y - arm), ImVec2(center.x + arm, center.y + arm), rgba(255, 255, 255), 1.4f);
        draw->AddLine(ImVec2(center.x - arm, center.y + arm), ImVec2(center.x + arm, center.y - arm), rgba(255, 255, 255), 1.4f);
        if (over && boxHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            search_[0] = '\0';
        }
    }
    ImGui::EndDisabled();
}

void HpiEditor::drawBody(float height) {
    const float u = U();
    const float width = ImGui::GetContentRegionAvail().x;
    const float sidebarWidth = std::floor(214.0f * u);
    const ImVec2 origin = ImGui::GetCursorPos();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * u, 8.0f * u));
    ImGui::BeginChild("##hpi-sidebar", ImVec2(sidebarWidth, height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    drawSidebar();
    ImGui::EndChild();

    ImGui::SetCursorPos(ImVec2(origin.x + sidebarWidth, origin.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
    const bool scrolls = hasDocument_ && view_ == View::Icons;
    ImGui::BeginChild(
        "##hpi-content", ImVec2(std::max(1.0f, width - sidebarWidth), height), ImGuiChildFlags_None,
        scrolls ? ImGuiWindowFlags_None : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    drawContent();
    ImGui::EndChild();
}

void HpiEditor::drawSidebar() {
    theme::sidebarBackground();
    theme::sourceListTitle(hasDocument_ ? "ARCHIVE" : "NO ARCHIVE");
    ImGui::Dummy(ImVec2(1.0f, 2.0f * U()));
    if (!hasDocument_) return;
    ImGui::BeginChild("##hpi-tree", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    drawTreeRow(std::string(), document_.displayName(), 0, true);
    ImGui::EndChild();
}

void HpiEditor::drawTreeRow(const std::string& path, const std::string& name, int depth, bool isRoot) {
    const float u = U();
    const float rowHeight = std::floor(22.0f * u);
    const float width = ImGui::GetContentRegionAvail().x;
    const float indent = std::floor(14.0f * u) * static_cast<float>(depth);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const std::string key = folded(path);
    const bool hasKids = document_.hasFolders(path);
    const bool expanded = isRoot || expanded_.count(key) > 0;

    ImGui::PushID(isRoot ? "root" : path.c_str());
    ImGui::InvisibleButton("##row", ImVec2(width, rowHeight));
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool hot = dropTarget(path);
    ImGui::PopID();

    const float triangleX = origin.x + std::floor(4.0f * u) + indent;
    const float triangleSize = std::floor(10.0f * u);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool onTriangle = hasKids && !isRoot && mouse.x >= triangleX - 3.0f * u && mouse.x <= triangleX + triangleSize + 4.0f * u;
    if (pressed) {
        if (onTriangle) {
            if (!expanded_.erase(key)) expanded_.insert(key);
        } else {
            navigate(path);
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hasKids && !isRoot && !onTriangle) {
        if (!expanded_.erase(key)) expanded_.insert(key);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool current = folded(folder_) == key;
    const ImVec2 rowMin(origin.x, origin.y + 1.0f);
    const ImVec2 rowMax(origin.x + width, origin.y + rowHeight - 1.0f);
    const float rounding = std::floor(5.0f * u);
    if (current) {
        draw->AddRectFilled(rowMin, rowMax, theme::accentColor(), rounding);
    } else if (hot) {
        draw->AddRectFilled(rowMin, rowMax, withAlpha(theme::accentColor(), 70), rounding);
        draw->AddRect(rowMin, rowMax, theme::accentColor(), rounding, 0, 1.5f);
    } else if (hovered && !(ImGui::GetDragDropPayload() != nullptr)) {
        draw->AddRectFilled(rowMin, rowMax, withAlpha(theme::dark() ? rgba(255, 255, 255) : rgba(0, 0, 0), 14), rounding);
    }
    const ImU32 textColor = current ? rgba(255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
    if (hasKids && !isRoot) {
        drawDisclosure(draw, ImVec2(triangleX + triangleSize * 0.5f, origin.y + rowHeight * 0.5f), triangleSize * 0.7f, expanded, current ? rgba(255, 255, 255, 220) : secondaryText());
    }
    const float iconSize = std::floor(16.0f * u);
    const float iconX = origin.x + std::floor(4.0f * u) + indent + triangleSize + std::floor(5.0f * u);
    const float iconY = origin.y + std::floor((rowHeight - iconSize) * 0.5f);
    if (isRoot) {
        drawFileIcon(draw, ImVec2(iconX, iconY), iconSize, KindInfo{"Archive", rgba(168, 120, 70), ""});
    } else {
        drawFolderIcon(draw, ImVec2(iconX, iconY), iconSize);
    }
    const float textX = iconX + iconSize + std::floor(6.0f * u);
    const std::string label = fitEnd(name, origin.x + width - textX - 6.0f * u);
    draw->AddText(ImVec2(textX, origin.y + std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f)), textColor, label.c_str());

    if (expanded && hasKids) {
        for (const ArchiveItem& child : document_.subfolders(path)) {
            drawTreeRow(child.path, child.name, depth + 1, false);
        }
    }
}

void HpiEditor::drawContent() {
    if (!hasDocument_) {
        drawEmpty();
        return;
    }
    const std::string query = trimmed(search_);
    if (!listingValid_ || listingGeneration_ != document_.generation() || listingFolder_ != folder_ || listingQuery_ != query
        || listingSortColumn_ != sortColumn_ || listingSortAscending_ != sortAscending_) {
        rebuildListing();
    }
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (view_ == View::List) {
        drawList(avail.x, avail.y);
    } else {
        drawIcons(avail.x, avail.y);
    }

    if (listing_.empty()) {
        // An empty folder, or a search with no results.
        ImGui::PushFont(theme::smallFont());
        const char* text = query.empty() ? "This folder is empty. Drop files here to import them." : "No results";
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 min = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(min.x + std::floor((avail.x - size.x) * 0.5f), min.y + std::floor(avail.y * 0.38f)), secondaryText(), text);
        ImGui::PopFont();
    }

    // Clicks on bare space clear the selection or open the folder's own menu.
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup)) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selection_.clear();
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            selection_.clear();
            openBlankMenu_ = true;
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) pendingCollapse_.clear();
    if (!(ImGui::GetDragDropPayload() != nullptr)) dragPaths_.clear();
    scrollTo_ = -1;
    scrollReset_ = false;
}

void HpiEditor::drawList(float width, float height) {
    const float u = U();
    const float rowHeight = std::floor(22.0f * u);
    const bool searching = !trimmed(search_).empty();
    const ImU32 selectedColor = theme::accentColor();
    const ImU32 white = rgba(255, 255, 255);

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(std::floor(8.0f * u), 0.0f));
    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("##hpi-list", 3, flags, ImVec2(width, height))) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 0.0f, 0);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, std::floor(96.0f * u), 1);
    ImGui::TableSetupColumn(searching ? "Location###kind" : "Kind###kind", ImGuiTableColumnFlags_WidthFixed, std::floor(170.0f * u), 2);
    ImGui::TableHeadersRow();
    if (scrollReset_) ImGui::SetScrollY(0.0f);
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty && specs->SpecsCount > 0) {
            sortColumn_ = static_cast<int>(specs->Specs[0].ColumnUserID);
            sortAscending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
            specs->SpecsDirty = false;
            rebuildListing();
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(listing_.size()), rowHeight);
    if (scrollTo_ >= 0) clipper.IncludeItemByIndex(scrollTo_);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const ArchiveItem item = listing_[static_cast<std::size_t>(i)];
            const bool selected = selection_.count(item.path) > 0;
            ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
            ImGui::TableSetColumnIndex(0);
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float nameWidth = ImGui::GetContentRegionAvail().x;
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
            ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, rowHeight));
            ImGui::PopStyleColor(2);
            interact(item, i);
            const bool hot = item.directory && dropTarget(item.path);
            ImGui::PopID();
            if (i == scrollTo_) {
                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const ImVec2 windowMin = ImGui::GetWindowPos();
                if (itemMin.y < windowMin.y + rowHeight * 1.2f) {
                    ImGui::SetScrollHereY(0.0f);
                } else if (itemMin.y + rowHeight > windowMin.y + ImGui::GetWindowHeight()) {
                    ImGui::SetScrollHereY(1.0f);
                }
            }
            if (hot) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, withAlpha(selectedColor, 90));
            } else if (selected) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, selectedColor);
            }
            const bool light = selected && !hot;
            const ImU32 textColor = light ? white : ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 quietColor = light ? rgba(255, 255, 255, 215) : secondaryText();

            const float iconSize = std::floor(16.0f * u);
            drawItemIcon(draw, ImVec2(origin.x, origin.y + std::floor((rowHeight - iconSize) * 0.5f)), iconSize, item);
            const float textX = origin.x + iconSize + std::floor(7.0f * u);
            draw->AddText(
                ImVec2(textX, origin.y + std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f)), textColor,
                fitEnd(item.name, nameWidth - iconSize - 8.0f * u).c_str());

            ImGui::PushFont(theme::smallFont());
            const float quietY = std::floor((rowHeight - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TableSetColumnIndex(1);
            {
                const ImVec2 cell = ImGui::GetCursorScreenPos();
                const std::string size = item.directory ? std::string("--") : humanSize(item.size);
                const float textWidth = ImGui::CalcTextSize(size.c_str()).x;
                draw->AddText(ImVec2(cell.x + std::max(0.0f, ImGui::GetContentRegionAvail().x - textWidth), cell.y + quietY), quietColor, size.c_str());
            }
            ImGui::TableSetColumnIndex(2);
            {
                const ImVec2 cell = ImGui::GetCursorScreenPos();
                std::string text;
                if (searching) {
                    const std::string parent = archiveParentPath(item.path);
                    text = parent.empty() ? document_.displayName() : parent;
                } else {
                    text = kindOf(item).name;
                }
                draw->AddText(ImVec2(cell.x, cell.y + quietY), quietColor, fitEnd(text, ImGui::GetContentRegionAvail().x).c_str());
            }
            ImGui::PopFont();
        }
    }
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void HpiEditor::drawIcons(float width, float) {
    const float u = U();
    const float cellWidth = std::floor(104.0f * u);
    const float cellHeight = std::floor(100.0f * u);
    const float iconSize = std::floor(56.0f * u);
    const float margin = std::floor(10.0f * u);
    columns_ = std::max(1, static_cast<int>((width - margin) / cellWidth));
    const int count = static_cast<int>(listing_.size());
    const int rows = (count + columns_ - 1) / columns_;
    const float labelWidth = cellWidth - std::floor(10.0f * u);
    const bool dark = theme::dark();

    if (scrollReset_) ImGui::SetScrollY(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::Dummy(ImVec2(1.0f, margin * 0.5f));
    ImGuiListClipper clipper;
    clipper.Begin(rows, cellHeight);
    if (scrollTo_ >= 0) clipper.IncludeItemByIndex(scrollTo_ / columns_);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImGui::PushFont(theme::smallFont());
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const float y = ImGui::GetCursorPosY();
            for (int column = 0; column < columns_; ++column) {
                const int index = row * columns_ + column;
                if (index >= count) break;
                const ArchiveItem item = listing_[static_cast<std::size_t>(index)];
                const bool selected = selection_.count(item.path) > 0;
                ImGui::SetCursorPos(ImVec2(margin + static_cast<float>(column) * cellWidth, y));
                ImGui::PushID(index);
                ImGui::InvisibleButton("##cell", ImVec2(cellWidth, cellHeight));
                interact(item, index);
                const bool hot = item.directory && dropTarget(item.path);
                ImGui::PopID();
                const ImVec2 min = ImGui::GetItemRectMin();
                if (index == scrollTo_) {
                    const ImVec2 windowMin = ImGui::GetWindowPos();
                    if (min.y < windowMin.y) {
                        ImGui::SetScrollHereY(0.0f);
                    } else if (min.y + cellHeight > windowMin.y + ImGui::GetWindowHeight()) {
                        ImGui::SetScrollHereY(1.0f);
                    }
                }
                const float centerX = min.x + cellWidth * 0.5f;
                const float rounding = std::floor(6.0f * u);
                const float plate = iconSize + std::floor(10.0f * u);
                const ImVec2 plateMin(centerX - plate * 0.5f, min.y + std::floor(3.0f * u));
                const ImVec2 plateMax(plateMin.x + plate, plateMin.y + plate);
                if (hot) {
                    draw->AddRectFilled(plateMin, plateMax, withAlpha(theme::accentColor(), 70), rounding);
                    draw->AddRect(plateMin, plateMax, theme::accentColor(), rounding, 0, 2.0f);
                } else if (selected) {
                    draw->AddRectFilled(plateMin, plateMax, dark ? rgba(84, 88, 98) : rgba(210, 214, 222), rounding);
                }
                drawItemIcon(draw, ImVec2(centerX - iconSize * 0.5f, plateMin.y + std::floor(5.0f * u)), iconSize, item);

                const auto lines = splitLabel(item.name, labelWidth - std::floor(8.0f * u));
                const float lineHeight = ImGui::GetTextLineHeight();
                const float textTop = plateMax.y + std::floor(3.0f * u);
                const int lineCount = lines.second.empty() ? 1 : 2;
                if (selected && !hot) {
                    float widest = ImGui::CalcTextSize(lines.first.c_str()).x;
                    if (!lines.second.empty()) widest = std::max(widest, ImGui::CalcTextSize(lines.second.c_str()).x);
                    const float padX = std::floor(5.0f * u);
                    draw->AddRectFilled(
                        ImVec2(centerX - widest * 0.5f - padX, textTop - 1.0f), ImVec2(centerX + widest * 0.5f + padX, textTop + lineHeight * static_cast<float>(lineCount) + 1.0f),
                        theme::accentColor(), std::floor(4.0f * u));
                }
                const ImU32 textColor = selected && !hot ? rgba(255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
                const std::string* shownLines[2] = {&lines.first, &lines.second};
                for (int line = 0; line < lineCount; ++line) {
                    const float textWidth = ImGui::CalcTextSize(shownLines[line]->c_str()).x;
                    draw->AddText(ImVec2(centerX - textWidth * 0.5f, textTop + lineHeight * static_cast<float>(line)), textColor, shownLines[line]->c_str());
                }
            }
            ImGui::SetCursorPos(ImVec2(0.0f, y));
            ImGui::Dummy(ImVec2(1.0f, cellHeight));
        }
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
}

void HpiEditor::drawEmpty() {
    const float u = U();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float iconSize = std::floor(104.0f * u);
    const float top = std::floor(avail.y * 0.5f - 130.0f * u);
    const float y = origin.y + std::max(top, 20.0f * u);
    const float cx = origin.x + std::floor(avail.x * 0.5f);
    drawFileIcon(draw, ImVec2(cx - iconSize * 0.5f, y), iconSize, KindInfo{"Archive", rgba(168, 120, 70), "HPI"});

    const auto centered = [&](const char* text, ImFont* font, ImU32 color, float at) {
        ImGui::PushFont(font);
        const ImVec2 size = ImGui::CalcTextSize(text);
        draw->AddText(ImVec2(cx - size.x * 0.5f, at), color, text);
        ImGui::PopFont();
    };
    centered("No Archive Open", theme::boldFont(), ImGui::GetColorU32(ImGuiCol_Text), y + iconSize + 14.0f * u);
    centered("Open an HPI or UFO file to browse what is inside it,", theme::smallFont(), secondaryText(), y + iconSize + 38.0f * u);
    centered("or start an empty archive and import files into it.", theme::smallFont(), secondaryText(), y + iconSize + 54.0f * u);

    const bool mapIsArchive = !currentMapPath_.empty() && hasArchiveExtension(currentMapPath_);
    std::error_code ec;
    const bool offerMap = mapIsArchive && fs::exists(currentMapPath_, ec);
    const std::string openMapLabel = offerMap ? "Open " + pathToUtf8(currentMapPath_.filename()) : std::string();
    const std::string openLabel = theme::withEllipsis("Open Archive");
    const float openWidth = theme::pushButtonWidth(openLabel.c_str());
    const float newWidth = theme::pushButtonWidth("New Archive");
    const float mapWidth = offerMap ? theme::pushButtonWidth(openMapLabel.c_str()) : 0.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float rowWidth = openWidth + newWidth + spacing + (offerMap ? mapWidth + spacing : 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(cx - rowWidth * 0.5f, y + iconSize + 84.0f * u));
    if (theme::defaultButton(openLabel.c_str(), ImVec2(openWidth, 0.0f), false)) {
        if (const auto chosen = openArchiveDialog(lastDirectory_)) requestOpen(*chosen);
    }
    ImGui::SameLine();
    if (ImGui::Button("New Archive", ImVec2(newWidth, 0.0f))) requestNew();
    if (offerMap) {
        ImGui::SameLine();
        if (ImGui::Button(openMapLabel.c_str(), ImVec2(mapWidth, 0.0f))) requestOpen(currentMapPath_);
    }
}

void HpiEditor::drawPathBar() {
    const float u = U();
    ImGui::PushFont(theme::smallFont());
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 windowMin = ImGui::GetWindowPos();
    draw->AddLine(windowMin, ImVec2(windowMin.x + ImGui::GetWindowWidth(), windowMin.y), ImGui::GetColorU32(ImGuiCol_Border));
    if (!hasDocument_) {
        ImGui::PopFont();
        return;
    }

    struct Segment {
        std::string path;
        std::string name;
    };
    std::vector<Segment> segments;
    segments.push_back({std::string(), document_.displayName()});
    std::string running;
    for (const std::string& part : [&] {
             std::vector<std::string> parts;
             std::size_t start = 0;
             while (start < folder_.size()) {
                 const std::size_t slash = folder_.find('/', start);
                 const std::size_t end = slash == std::string::npos ? folder_.size() : slash;
                 parts.push_back(folder_.substr(start, end - start));
                 start = end + 1;
             }
             return parts;
         }()) {
        running = joinArchivePath(running, part);
        segments.push_back({running, part});
    }

    const float iconSize = std::floor(14.0f * u);
    const float chevronWidth = std::floor(16.0f * u);
    const float padX = std::floor(5.0f * u);
    const float available = ImGui::GetContentRegionAvail().x;
    std::vector<float> widths;
    for (const Segment& segment : segments) {
        widths.push_back(iconSize + 5.0f * u + ImGui::CalcTextSize(segment.name.c_str()).x + padX * 2.0f);
    }
    // When the path is too long the first folders are left out, showing only the last ones.
    std::size_t first = 0;
    const auto totalFrom = [&](std::size_t start) {
        float sum = 0.0f;
        for (std::size_t i = start; i < segments.size(); ++i) sum += widths[i] + (i > start ? chevronWidth : 0.0f);
        return sum;
    };
    while (first + 1 < segments.size() && totalFrom(first) > available - (first > 0 ? chevronWidth + 14.0f * u : 0.0f)) ++first;

    const float lineHeight = ImGui::GetTextLineHeight();
    const float barHeight = ImGui::GetWindowHeight();
    float x = ImGui::GetCursorScreenPos().x;
    const float y = windowMin.y + std::floor((barHeight - lineHeight) * 0.5f) + 1.0f;
    if (first > 0) {
        draw->AddText(ImVec2(x + 2.0f * u, y), secondaryText(), theme::ellipsis());
        x += chevronWidth + 6.0f * u;
        drawChevron(draw, ImVec2(x - chevronWidth * 0.5f, y + lineHeight * 0.5f), lineHeight * 0.8f, secondaryText(), -1.0f);
    }
    for (std::size_t i = first; i < segments.size(); ++i) {
        const float width = widths[i];
        const ImVec2 min(x, windowMin.y + 2.0f);
        const ImVec2 max(x + width, windowMin.y + barHeight - 2.0f);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(static_cast<int>(i));
        ImGui::InvisibleButton("##crumb", ImVec2(width, max.y - min.y));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && folded(segments[i].path) != folded(folder_)) navigate(segments[i].path);
        const bool hot = dropTarget(segments[i].path);
        ImGui::PopID();
        const bool last = i + 1 == segments.size();
        if (hot) {
            draw->AddRectFilled(min, max, withAlpha(theme::accentColor(), 70), 4.0f * u);
            draw->AddRect(min, max, theme::accentColor(), 4.0f * u, 0, 1.5f);
        } else if (hovered && !(ImGui::GetDragDropPayload() != nullptr)) {
            draw->AddRectFilled(min, max, theme::dark() ? rgba(255, 255, 255, 22) : rgba(0, 0, 0, 16), 4.0f * u);
        }
        const ImVec2 iconMin(min.x + padX, windowMin.y + std::floor((barHeight - iconSize) * 0.5f));
        if (i == 0) {
            drawFileIcon(draw, iconMin, iconSize, KindInfo{"Archive", rgba(168, 120, 70), ""});
        } else {
            drawFolderIcon(draw, iconMin, iconSize);
        }
        draw->AddText(ImVec2(iconMin.x + iconSize + 5.0f * u, y), last ? ImGui::GetColorU32(ImGuiCol_Text) : secondaryText(), segments[i].name.c_str());
        x += width;
        if (!last) {
            drawChevron(draw, ImVec2(x + chevronWidth * 0.5f, y + lineHeight * 0.5f), lineHeight * 0.8f, secondaryText(), -1.0f);
            x += chevronWidth;
        }
    }
    ImGui::PopFont();
}

void HpiEditor::drawStatus() {
    ImGui::PushFont(theme::smallFont());
    ImGui::PushStyleColor(ImGuiCol_Text, theme::secondaryTextColor());
    std::string text;
    if (!hasDocument_) {
        text = "Open an archive to see what is inside it";
    } else if (!note_.empty() && ImGui::GetTime() < noteUntil_) {
        text = note_;
    } else {
        const std::string query = trimmed(search_);
        if (!query.empty()) {
            text = countText(static_cast<int>(listing_.size()), "result", "results") + " for \"" + query + "\"";
        } else if (!selection_.empty()) {
            std::uint64_t bytes = 0;
            for (const std::string& path : selection_) {
                if (const auto item = document_.find(path)) bytes += item->size;
            }
            text = std::to_string(selection_.size()) + " of " + countText(static_cast<int>(listing_.size()), "item", "items") + " selected, " + humanSize(bytes);
        } else {
            text = countText(static_cast<int>(listing_.size()), "item", "items");
            if (const auto here = document_.find(folder_)) text += ", " + humanSize(here->size) + " in " + countText(here->fileCount, "file", "files");
        }
    }
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void HpiEditor::drawMenus() {
    if (openItemMenu_) {
        ImGui::OpenPopup("hpi-item-menu");
        openItemMenu_ = false;
    }
    if (openBlankMenu_) {
        ImGui::OpenPopup("hpi-blank-menu");
        openBlankMenu_ = false;
    }
    if (ImGui::BeginPopup("hpi-item-menu")) {
        const std::vector<std::string> paths = selectedPaths();
        const auto only = paths.size() == 1 ? document_.find(paths[0]) : std::nullopt;
        if (ImGui::MenuItem("Open", nullptr, false, only && only->directory)) navigate(only->path);
        if (ImGui::MenuItem(theme::withEllipsis("Export").c_str(), nullptr, false, !paths.empty())) beginExport();
        ImGui::Separator();
        if (ImGui::MenuItem(theme::withEllipsis("Rename").c_str(), "F2", false, only.has_value())) {
            namePath_ = only->path;
            beginName(NameFor::Rename, only->name);
        }
        if (ImGui::MenuItem("Delete", theme::shortcut("Backspace").c_str(), false, !paths.empty())) deleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem(theme::withEllipsis("New Folder").c_str())) beginName(NameFor::NewFolder, "untitled folder");
        if (ImGui::MenuItem(theme::withEllipsis("Import Files").c_str())) chooseImport(false);
        if (ImGui::MenuItem(theme::withEllipsis("Import Folder").c_str())) chooseImport(true);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("hpi-blank-menu")) {
        if (ImGui::MenuItem(theme::withEllipsis("New Folder").c_str(), theme::shortcut("Shift+N").c_str(), false, hasDocument_)) beginName(NameFor::NewFolder, "untitled folder");
        if (ImGui::MenuItem(theme::withEllipsis("Import Files").c_str(), theme::shortcut("I").c_str(), false, hasDocument_)) chooseImport(false);
        if (ImGui::MenuItem(theme::withEllipsis("Import Folder").c_str(), nullptr, false, hasDocument_)) chooseImport(true);
        ImGui::Separator();
        if (ImGui::MenuItem(theme::withEllipsis("Export This Folder").c_str(), nullptr, false, hasDocument_ && document_.fileCount() > 0)) {
            selection_.clear();
            beginExport();
        }
        if (ImGui::MenuItem("Select All", theme::shortcut("A").c_str(), false, !listing_.empty())) selectAll();
        ImGui::EndPopup();
    }
}

void HpiEditor::drawSheets() {
    if (sheet_ == Sheet::None) return;
    const float u = U();
    ImGui::SetNextWindowPos(windowCenter_, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (sheet_ == Sheet::Name) {
        ImGui::OpenPopup("HPI Name");
        if (!ImGui::BeginPopupModal("HPI Name", nullptr, theme::alertFlags())) return;
        theme::alertText(nameFor_ == NameFor::Rename ? "Rename this item" : "Name the new folder");
        if (nameFocus_) {
            ImGui::SetKeyboardFocusHere();
            nameFocus_ = false;
        }
        ImGui::SetNextItemWidth(std::floor(380.0f * u));
        const bool entered = ImGui::InputText("##hpi-name", nameBuffer_, sizeof(nameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const std::string name = trimmed(nameBuffer_);
        std::string problem = name.empty() ? std::string() : archiveNameProblem(name);
        if (problem.empty() && !name.empty()) {
            const std::string where = nameFor_ == NameFor::Rename ? archiveParentPath(namePath_) : folder_;
            const std::string target = joinArchivePath(where, name);
            const bool sameItem = nameFor_ == NameFor::Rename && folded(target) == folded(namePath_);
            if (document_.find(target) && !sameItem) problem = "An item named \"" + name + "\" already exists here.";
        }
        ImGui::Dummy(ImVec2(1.0f, 2.0f));
        ImGui::PushFont(theme::smallFont());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.24f, 0.20f, 1.0f));
        ImGui::TextUnformatted(problem.empty() ? " " : problem.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(1.0f, 4.0f));
        const float buttonWidth = std::max(theme::pushButtonWidth("Cancel"), theme::pushButtonWidth("OK"));
        theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            sheet_ = Sheet::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        const bool valid = !name.empty() && problem.empty();
        ImGui::BeginDisabled(!valid);
        if (theme::defaultButton("OK", ImVec2(buttonWidth, 0.0f), false) || (entered && valid)) {
            commitName();
            sheet_ = Sheet::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
        return;
    }

    if (sheet_ == Sheet::Conflict) {
        ImGui::OpenPopup("HPI Replace");
        if (!ImGui::BeginPopupModal("HPI Replace", nullptr, theme::alertFlags())) return;
        const std::size_t count = pending_.conflicts.size();
        std::string message;
        if (count == 1) {
            message = "\"" + archiveLeafName(pending_.conflicts[0]) + "\" already exists in this location.";
        } else {
            message = std::to_string(count) + " items already exist in this location.";
        }
        std::string informative;
        const char* verb = pending_.kind == Operation::Import ? "importing" : (pending_.kind == Operation::Move ? "moving" : "exporting");
        if (pending_.kind == Operation::Export) {
            informative = std::string("Do you want to replace ") + (count == 1 ? "the file" : "the files") + " on your computer with the ones from the archive?";
        } else {
            informative = std::string("Do you want to replace ") + (count == 1 ? "it" : "them") + " with what you are " + verb + "?";
        }
        for (std::size_t i = 0; i < std::min<std::size_t>(count, 4); ++i) informative += "\n" + pending_.conflicts[i];
        if (count > 4) informative += "\nand " + std::to_string(count - 4) + " more";
        theme::alertText(message.c_str(), informative.c_str());
        const float buttonWidth = std::max(theme::pushButtonWidth("Replace"), theme::pushButtonWidth("Stop"));
        theme::alignRight(buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button("Replace", ImVec2(buttonWidth, 0.0f))) {
            sheet_ = Sheet::None;
            ImGui::CloseCurrentPopup();
            finishOperation(true);
        }
        ImGui::SameLine();
        // Stop answers Return, so the destructive choice is never the quick one.
        if (theme::defaultButton("Stop", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            sheet_ = Sheet::None;
            pending_ = PendingOperation();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    ImGui::OpenPopup("HPI Unsaved");
    if (!ImGui::BeginPopupModal("HPI Unsaved", nullptr, theme::alertFlags())) return;
    const std::string message = "Do you want to save the changes made to the archive \"" + document_.displayName() + "\"?";
    theme::alertText(message.c_str(), "Your changes will be lost if you don't save them.");
    const float buttonWidth = std::max({theme::pushButtonWidth("Save"), theme::pushButtonWidth("Cancel"), theme::pushButtonWidth("Don't Save")});
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    if (ImGui::Button("Don't Save", ImVec2(buttonWidth, 0.0f))) {
        const Then then = then_;
        sheet_ = Sheet::None;
        ImGui::CloseCurrentPopup();
        runThen(then);
    }
    ImGui::SameLine();
    theme::alignRight(buttonWidth * 2.0f + spacing);
    if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        sheet_ = Sheet::None;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (theme::defaultButton("Save", ImVec2(buttonWidth, 0.0f))) {
        const bool needsName = document_.path().empty();
        bool saved = false;
        if (needsName) {
            saved = saveAs();
        } else {
            save();
            saved = !document_.dirty();
        }
        if (saved) {
            const Then then = then_;
            sheet_ = Sheet::None;
            ImGui::CloseCurrentPopup();
            runThen(then);
        }
    }
    ImGui::EndPopup();
}

void HpiEditor::drawDragPreview() {
    if (!(ImGui::GetDragDropPayload() != nullptr) || dragPaths_.empty()) return;
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr || !payload->IsDataType(kDragType)) return;
    const float u = U();
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto first = document_.find(dragPaths_.front());
    if (!first) return;

    const float iconSize = std::floor(32.0f * u);
    const float pad = std::floor(6.0f * u);
    const std::string name = fitEnd(dragPaths_.size() == 1 ? first->name : countText(static_cast<int>(dragPaths_.size()), "item", "items"), 220.0f * u);
    const float textWidth = ImGui::CalcTextSize(name.c_str()).x;
    const float width = pad * 3.0f + iconSize + textWidth;
    const float height = iconSize + pad * 2.0f;
    const ImVec2 min(mouse.x + 14.0f * u, mouse.y + 10.0f * u);
    const ImVec2 max(min.x + width, min.y + height);
    const float rounding = std::floor(8.0f * u);
    draw->AddRectFilled(ImVec2(min.x + 1.0f, min.y + 3.0f), ImVec2(max.x + 1.0f, max.y + 3.0f), rgba(0, 0, 0, 55), rounding);
    draw->AddRectFilled(min, max, theme::dark() ? rgba(62, 64, 70, 238) : rgba(252, 252, 253, 238), rounding);
    draw->AddRect(min, max, theme::dark() ? rgba(104, 106, 112) : rgba(176, 178, 184), rounding);
    // A second sheet behind the first suggests several items.
    const ImVec2 iconMin(min.x + pad, min.y + pad);
    if (dragPaths_.size() > 1) {
        drawItemIcon(draw, ImVec2(iconMin.x + 3.0f * u, iconMin.y - 2.0f * u), iconSize, *first);
    }
    drawItemIcon(draw, iconMin, iconSize, *first);
    draw->AddText(ImVec2(iconMin.x + iconSize + pad, min.y + std::floor((height - ImGui::GetTextLineHeight()) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    if (dragPaths_.size() > 1) drawCountBadge(draw, ImVec2(max.x - 2.0f * u, min.y + 2.0f * u), static_cast<int>(dragPaths_.size()));
}

void HpiEditor::handleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    const auto chord = [](ImGuiKeyChord keys) { return ImGui::IsKeyChordPressed(keys); };

    if (chord(ImGuiMod_Ctrl | ImGuiKey_O)) {
        if (const auto chosen = openArchiveDialog(lastDirectory_)) requestOpen(*chosen);
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_N)) {
        requestNew();
    } else if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) {
        saveAs();
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_S)) {
        if (hasDocument_ && document_.dirty()) save();
    } else if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || chord(ImGuiMod_Ctrl | ImGuiKey_Y)) {
        redo();
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) {
        undo();
    } else if (!hasDocument_) {
        return;
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_A)) {
        selectAll();
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_I)) {
        chooseImport(false);
    } else if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N)) {
        beginName(NameFor::NewFolder, "untitled folder");
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_LeftBracket)) {
        goBack();
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_RightBracket)) {
        goForward();
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_UpArrow)) {
        goUp();
    } else if (chord(ImGuiMod_Ctrl | ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
        const std::vector<std::string> paths = selectedPaths();
        if (paths.size() == 1) {
            if (const auto item = document_.find(paths[0])) openItem(*item);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        const std::vector<std::string> paths = selectedPaths();
        if (paths.size() == 1) {
            if (const auto item = document_.find(paths[0])) {
                namePath_ = item->path;
                beginName(NameFor::Rename, item->name);
            }
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || chord(ImGuiMod_Ctrl | ImGuiKey_Backspace)) {
        deleteSelection();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) && !io.KeyCtrl) {
        goUp();
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (search_[0] != '\0') {
            clearSearch();
        } else {
            selection_.clear();
        }
    } else if (!io.KeyCtrl && !io.KeyAlt && !listing_.empty()) {
        int delta = 0;
        const int rowStep = view_ == View::Icons ? columns_ : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) delta = rowStep;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) delta = -rowStep;
        if (view_ == View::Icons && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) delta = 1;
        if (view_ == View::Icons && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) delta = -1;
        if (delta != 0) {
            const int count = static_cast<int>(listing_.size());
            const int current = indexOfPath(cursor_);
            const int target = current < 0 ? (delta > 0 ? 0 : count - 1) : std::clamp(current + delta, 0, count - 1);
            if (io.KeyShift) {
                selectRange(static_cast<std::size_t>(target));
            } else {
                select(listing_[static_cast<std::size_t>(target)].path);
            }
            scrollTo_ = target;
        }
    }
}

}  // namespace galaxy

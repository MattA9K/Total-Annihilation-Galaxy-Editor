#pragma once

#include "core/ArchiveDocument.hpp"

#include <imgui.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace galaxy {

// A file browser for HPI, UFO and similar archives, in its own window. Folders can be browsed,
// and files imported, exported, moved by dragging, renamed and deleted. Nothing is written to the
// archive on disk until it is saved.
class HpiEditor {
public:
    // Opens the window, showing whatever archive was last open.
    void show();
    bool visible() const { return visible_; }
    // True while the window or one of its sheets has the keyboard.
    bool focused() const { return focused_; }
    // The archive has changes that were not saved.
    bool dirty() const { return hasDocument_ && document_.dirty(); }

    // Offered in the empty window, so the map being edited can be browsed in one click.
    void setCurrentMapPath(const std::filesystem::path& path) { currentMapPath_ = path; }

    // Files or folders dropped from the system. They are imported into the folder being
    // browsed, or, with no archive open, a single archive file is opened.
    void importPaths(const std::vector<std::filesystem::path>& paths);

    // The application wants to quit. Asks about unsaved changes first.
    void requestQuit();
    // True once, after the user agreed to quit.
    bool takeQuitApproval();

    void draw();

    std::string takeError();
    std::string takeStatus();

private:
    enum class Sheet { None, Name, Conflict, Unsaved };
    enum class Then { Nothing, NewArchive, OpenArchive, CloseWindow, Quit };
    enum class NameFor { Rename, NewFolder };
    enum class Operation { Import, Move, Export };
    enum class View { Icons, List };

    struct PendingOperation {
        Operation kind = Operation::Import;
        ImportPlan importPlan;
        MovePlan movePlan;
        std::vector<std::string> exportPaths;
        std::filesystem::path exportDestination;
        std::vector<std::string> conflicts;
    };

    // Opening, saving and asking.
    void createBlank();
    void openArchive(const std::filesystem::path& path);
    void requestOpen(const std::filesystem::path& path);
    void requestNew();
    void requestCloseWindow();
    void save();
    bool saveAs();
    void askThen(Then then);
    void runThen(Then then);

    // Changing the archive.
    void beginImport(const std::vector<std::filesystem::path>& sources);
    void chooseImport(bool folders);
    void moveItems(const std::vector<std::string>& paths, const std::string& destination);
    void beginExport();
    void finishOperation(bool replace);
    void discardDocument();
    void deleteSelection();
    void beginName(NameFor purpose, const std::string& initial);
    void commitName();
    void undo();
    void redo();

    // Browsing.
    void navigate(const std::string& folder, bool remember = true);
    void goBack();
    void goForward();
    void goUp();
    void openItem(const ArchiveItem& item);
    void clearSearch();
    void rebuildListing();
    void select(const std::string& path);
    void selectRange(std::size_t to);
    void toggle(const std::string& path);
    void selectAll();
    std::vector<std::string> selectedPaths() const;
    int indexOfPath(const std::string& path) const;
    bool canDropInto(const std::string& folder) const;
    void note(const std::string& text);

    // Drawing.
    void drawToolbar();
    void drawBody(float height);
    void drawSidebar();
    void drawTreeRow(const std::string& path, const std::string& name, int depth, bool isRoot);
    void drawContent();
    void drawList(float width, float height);
    void drawIcons(float width, float height);
    void drawEmpty();
    void drawPathBar();
    void drawStatus();
    void drawMenus();
    void drawSheets();
    void drawDragPreview();
    void handleKeys();
    // Selection and drag logic shared by the list and the icon view for one item.
    void interact(const ArchiveItem& item, int index);
    // Makes a folder accept dragged items. True while a valid drag is over it.
    bool dropTarget(const std::string& folder);

    bool visible_ = false;
    bool focused_ = false;
    bool hasDocument_ = false;
    ArchiveDocument document_;
    std::filesystem::path currentMapPath_;
    std::filesystem::path lastDirectory_;

    std::string folder_;
    std::vector<std::string> back_;
    std::vector<std::string> forward_;
    std::set<std::string> expanded_;  // lower-case folder paths

    std::vector<ArchiveItem> listing_;
    std::uint64_t listingGeneration_ = 0;
    std::string listingFolder_;
    std::string listingQuery_;
    int listingSortColumn_ = -1;
    bool listingSortAscending_ = true;
    bool listingValid_ = false;

    std::set<std::string> selection_;
    std::string anchor_;
    std::string cursor_;
    bool scrollReset_ = false;
    std::string pendingCollapse_;
    int scrollTo_ = -1;
    int columns_ = 1;

    View view_ = View::List;
    int sortColumn_ = 0;
    bool sortAscending_ = true;
    char search_[256] = {};

    std::vector<std::string> dragPaths_;
    bool openItemMenu_ = false;
    bool openBlankMenu_ = false;

    Sheet sheet_ = Sheet::None;
    Then then_ = Then::Nothing;
    std::filesystem::path thenPath_;
    NameFor nameFor_ = NameFor::NewFolder;
    std::string namePath_;
    char nameBuffer_[256] = {};
    bool nameFocus_ = false;
    PendingOperation pending_;

    std::string error_;
    std::string status_;
    std::string note_;
    double noteUntil_ = 0.0;
    bool quitApproved_ = false;
    ImVec2 windowCenter_ = ImVec2(0.0f, 0.0f);
};

}  // namespace galaxy

#pragma once

#include "format/HpiArchive.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy {

// Paths inside an archive use "/" and have no leading or trailing slash. The
// empty string is the top level. Names match without regard to case, like the game.

// Turns "\a\\b/" into "a/b".
std::string normalizeArchivePath(std::string_view path);
std::string archiveParentPath(std::string_view path);
std::string archiveLeafName(std::string_view path);
std::string joinArchivePath(std::string_view folder, std::string_view name);
// True when path is folder itself or lies inside it. The empty folder holds everything.
bool archivePathIsInside(std::string_view path, std::string_view folder);
// Empty when name can be a file or folder name. Otherwise a short sentence that says why not.
std::string archiveNameProblem(std::string_view name);

struct ArchiveItem {
    std::string name;
    std::string path;
    bool directory = false;
    // A file's bytes, or the total of every file beneath a folder.
    std::uint64_t size = 0;
    // Files beneath a folder, however deep.
    int fileCount = 0;
    // Items directly inside a folder.
    int childCount = 0;
};

struct ImportEntry {
    std::string path;  // where it lands in the archive
    std::filesystem::path source;
    std::uint64_t size = 0;
};

struct ImportPlan {
    std::string folder;
    std::vector<ImportEntry> files;
    // Archive paths that already hold a file the import would replace.
    std::vector<std::string> conflicts;
    // Things left out: unreadable, badly named, or in the way of a folder.
    int skipped = 0;
};

struct MovePlan {
    // Each item's old path and its new one.
    std::vector<std::pair<std::string, std::string>> moves;
    // Archive paths that already hold a file the move would replace.
    std::vector<std::string> conflicts;
    // Not empty when the move cannot happen at all.
    std::string problem;
};

struct ExportResult {
    int files = 0;
    int skipped = 0;
};

// An archive being edited: an HPI, UFO, CCX, GP3 or GPF file read into a tree of
// folders and files that can be browsed, changed and written back. Nothing touches
// the file on disk until save. Every change can be undone.
class ArchiveDocument {
public:
    // A new archive with nothing in it.
    ArchiveDocument();
    static ArchiveDocument open(const std::filesystem::path& path);

    // Empty until the archive has been opened from, or saved to, a file.
    const std::filesystem::path& path() const { return path_; }
    std::string displayName() const;
    bool dirty() const { return revision_ != savedRevision_; }
    // Changes whenever the tree does, including undo, redo and save.
    std::uint64_t generation() const { return generation_; }
    int fileCount() const { return static_cast<int>(files_.size()); }
    // Folders with no files anywhere beneath them. An archive cannot store these.
    int emptyFolderCount() const;
    // Entries in the opened file that repeated an earlier name and were set aside.
    int droppedDuplicates() const { return droppedDuplicates_; }

    bool isFolder(std::string_view path) const;
    bool isFile(std::string_view path) const;
    std::optional<ArchiveItem> find(std::string_view path) const;
    // The path with the capital letters the archive uses, for as much of it as exists.
    std::string canonicalPath(std::string_view path) const;
    // Folders first, then names in alphabetical order.
    std::vector<ArchiveItem> children(std::string_view folder) const;
    // Every file and folder beneath folder whose name contains query.
    std::vector<ArchiveItem> search(std::string_view folder, std::string_view query, std::size_t limit = 5000) const;
    // Whether any folder sits directly inside folder.
    bool hasFolders(std::string_view folder) const;
    // The folders directly inside folder, in alphabetical order.
    std::vector<ArchiveItem> subfolders(std::string_view folder) const;
    // The files an item stands for: itself when a file, every file beneath it when a folder.
    std::vector<std::string> filePathsUnder(std::string_view path) const;
    std::vector<std::uint8_t> read(std::string_view filePath) const;

    // Files and folders on disk become files inside folder. Conflicts are listed in the
    // plan, and apply replaces them only when told to. Returns how many files went in.
    ImportPlan planImport(std::string_view folder, const std::vector<std::filesystem::path>& sources) const;
    int applyImport(const ImportPlan& plan, bool replace);

    // Moves files and folders into destination. A folder that already exists there is merged.
    MovePlan planMove(const std::vector<std::string>& paths, std::string_view destination) const;
    int applyMove(const MovePlan& plan, bool replace);

    void rename(std::string_view path, std::string_view newName);
    // Returns the new folder's path.
    std::string createFolder(std::string_view parent, std::string_view name);
    // Returns how many of the given items were removed.
    int remove(const std::vector<std::string>& paths);

    // Writes items to disk beneath destination. A file becomes destination/name, and a
    // folder becomes destination/name/... An empty path means everything in the archive, written
    // straight into destination. Names that could reach outside destination are never written.
    std::vector<std::string> exportConflicts(const std::vector<std::string>& paths, const std::filesystem::path& destination) const;
    ExportResult exportTo(const std::vector<std::string>& paths, const std::filesystem::path& destination, bool replace) const;

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void undo();
    void redo();

    // Writes the archive and reads it back, which ends the undo history.
    void save();
    void saveAs(const std::filesystem::path& destination);

private:
    struct Entry {
        std::string path;
        std::shared_ptr<const std::vector<std::uint8_t>> data;  // set for files added here
        int source = -1;                                         // otherwise an index into archive_
        std::uint64_t size = 0;
    };
    struct Snapshot {
        std::vector<Entry> files;
        std::set<std::string> folders;
        std::uint64_t revision = 0;
    };
    struct Node {
        std::string path;
        std::string name;
        bool directory = false;
        int file = -1;
        std::uint64_t size = 0;
        int fileCount = 0;
        std::vector<std::string> kids;  // folded paths
    };

    void ensureIndex() const;
    const Node* node(std::string_view path) const;
    ArchiveItem itemOf(const Node& node) const;
    std::vector<std::uint8_t> bytesOf(const Entry& entry) const;
    void record();
    void remap(const std::vector<std::pair<std::string, std::string>>& roots, bool replace);
    void changed();

    std::filesystem::path path_;
    std::shared_ptr<HpiArchive> archive_;
    std::vector<Entry> files_;
    std::set<std::string> folders_;  // folders kept even with nothing in them
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::uint64_t revision_ = 1;
    std::uint64_t savedRevision_ = 1;
    std::uint64_t nextRevision_ = 2;
    std::uint64_t generation_ = 1;
    int droppedDuplicates_ = 0;

    mutable bool indexValid_ = false;
    mutable std::unordered_map<std::string, Node> index_;
};

}  // namespace galaxy

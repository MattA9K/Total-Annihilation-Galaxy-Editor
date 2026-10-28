#include "core/ArchiveDocument.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <unordered_set>

namespace galaxy {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kUndoLimit = 40;

std::string folded(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool lessFolded(const std::string& a, const std::string& b) {
    return folded(a) < folded(b);
}

std::vector<std::string> segmentsOf(std::string_view path) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        if (end > start) out.emplace_back(path.substr(start, end - start));
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }
    return out;
}

bool ignoredImportName(const std::string& name) {
    const std::string lower = folded(name);
    return lower == ".ds_store" || lower == "thumbs.db" || lower == "desktop.ini";
}

// A name that is safe to create on any of the three desktop systems.
std::string diskSegment(const std::string& name) {
    if (name.empty() || name == "." || name == "..") return {};
    std::string out = name;
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"|?*\\").find(c) != std::string_view::npos) c = '_';
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out;
}

fs::path fromUtf8(const std::string& text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string toUtf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// The archive path made into a path relative to an export folder, or empty when unsafe.
fs::path diskRelative(const std::string& archivePath) {
    fs::path out;
    for (const std::string& segment : segmentsOf(archivePath)) {
        const std::string safe = diskSegment(segment);
        if (safe.empty()) return {};
        out /= fromUtf8(safe);
    }
    return out;
}

std::string rebased(const std::string& path, const std::string& from, const std::string& to) {
    // path is from itself or inside it.
    return path.size() == from.size() ? to : to + path.substr(from.size());
}

}  // namespace

std::string normalizeArchivePath(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    bool pendingSlash = false;
    for (char c : path) {
        if (c == '\\' || c == '/') {
            pendingSlash = !out.empty();
            continue;
        }
        if (pendingSlash) out.push_back('/');
        pendingSlash = false;
        out.push_back(c);
    }
    return out;
}

std::string archiveParentPath(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

std::string archiveLeafName(std::string_view path) {
    const std::size_t slash = path.rfind('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

std::string joinArchivePath(std::string_view folder, std::string_view name) {
    if (folder.empty()) return std::string(name);
    return std::string(folder) + "/" + std::string(name);
}

bool archivePathIsInside(std::string_view path, std::string_view folder) {
    if (folder.empty()) return true;
    if (path.size() < folder.size()) return false;
    for (std::size_t i = 0; i < folder.size(); ++i) {
        char a = path[i];
        char b = folder[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return path.size() == folder.size() || path[folder.size()] == '/';
}

std::string archiveNameProblem(std::string_view name) {
    if (name.empty()) return "Type a name.";
    if (name == "." || name == "..") return "That name is reserved.";
    if (name.size() > 255) return "That name is too long.";
    for (char c : name) {
        if (c == '/' || c == '\\') return "A name cannot contain a slash.";
        if (static_cast<unsigned char>(c) < 0x20) return "A name cannot contain control characters.";
    }
    return {};
}

ArchiveDocument::ArchiveDocument() = default;

ArchiveDocument ArchiveDocument::open(const fs::path& path) {
    ArchiveDocument doc;
    doc.archive_ = std::make_shared<HpiArchive>(HpiArchive::open(path));
    doc.path_ = path;
    std::unordered_set<std::string> seen;
    const std::vector<HpiFile>& stored = doc.archive_->files();
    doc.files_.reserve(stored.size());
    for (std::size_t i = 0; i < stored.size(); ++i) {
        std::string name = normalizeArchivePath(stored[i].path);
        if (name.empty()) continue;
        if (!seen.insert(folded(name)).second) {
            ++doc.droppedDuplicates_;
            continue;
        }
        Entry entry;
        entry.path = std::move(name);
        entry.source = static_cast<int>(i);
        entry.size = stored[i].size;
        doc.files_.push_back(std::move(entry));
    }
    return doc;
}

std::string ArchiveDocument::displayName() const {
    if (path_.empty()) return "Untitled.hpi";
    return toUtf8(path_.filename());
}

void ArchiveDocument::ensureIndex() const {
    if (indexValid_) return;
    index_.clear();
    Node root;
    root.directory = true;
    index_.emplace("", std::move(root));

    // Makes the folder chain for segments[0 .. count) and returns the last folder's key,
    // or nullptr when a file already sits somewhere along the way.
    auto ensureFolder = [&](const std::vector<std::string>& segments, std::size_t count, std::vector<std::string>* chain) -> bool {
        std::string key;
        std::string display;
        if (chain) chain->push_back("");
        for (std::size_t i = 0; i < count; ++i) {
            const std::string parentKey = key;
            key = key.empty() ? folded(segments[i]) : key + "/" + folded(segments[i]);
            display = display.empty() ? segments[i] : display + "/" + segments[i];
            auto it = index_.find(key);
            if (it == index_.end()) {
                Node folder;
                folder.path = display;
                folder.name = segments[i];
                folder.directory = true;
                index_.emplace(key, std::move(folder));
                index_[parentKey].kids.push_back(key);
            } else if (!it->second.directory) {
                return false;
            }
            if (chain) chain->push_back(key);
        }
        return true;
    };

    for (std::size_t i = 0; i < files_.size(); ++i) {
        const std::vector<std::string> segments = segmentsOf(files_[i].path);
        if (segments.empty()) continue;
        std::vector<std::string> chain;
        if (!ensureFolder(segments, segments.size() - 1, &chain)) continue;
        const std::string parentKey = chain.back();
        const std::string key = parentKey.empty() ? folded(segments.back()) : parentKey + "/" + folded(segments.back());
        if (index_.count(key)) continue;  // a folder of that name already exists
        Node leaf;
        leaf.path = files_[i].path;
        leaf.name = segments.back();
        leaf.file = static_cast<int>(i);
        leaf.size = files_[i].size;
        leaf.fileCount = 1;
        index_.emplace(key, std::move(leaf));
        index_[parentKey].kids.push_back(key);
        for (const std::string& ancestor : chain) {
            Node& n = index_[ancestor];
            n.size += files_[i].size;
            n.fileCount += 1;
        }
    }
    for (const std::string& folder : folders_) {
        const std::vector<std::string> segments = segmentsOf(folder);
        ensureFolder(segments, segments.size(), nullptr);
    }
    indexValid_ = true;
}

const ArchiveDocument::Node* ArchiveDocument::node(std::string_view path) const {
    ensureIndex();
    const auto it = index_.find(folded(normalizeArchivePath(path)));
    return it == index_.end() ? nullptr : &it->second;
}

ArchiveItem ArchiveDocument::itemOf(const Node& n) const {
    ArchiveItem item;
    item.name = n.name;
    item.path = n.path;
    item.directory = n.directory;
    item.size = n.size;
    item.fileCount = n.fileCount;
    item.childCount = static_cast<int>(n.kids.size());
    return item;
}

bool ArchiveDocument::isFolder(std::string_view path) const {
    const Node* n = node(path);
    return n && n->directory;
}

bool ArchiveDocument::isFile(std::string_view path) const {
    const Node* n = node(path);
    return n && !n->directory;
}

std::optional<ArchiveItem> ArchiveDocument::find(std::string_view path) const {
    const Node* n = node(path);
    if (!n) return std::nullopt;
    return itemOf(*n);
}

std::string ArchiveDocument::canonicalPath(std::string_view path) const {
    ensureIndex();
    std::string key;
    std::string display;
    bool exists = true;
    for (const std::string& segment : segmentsOf(path)) {
        std::string name = segment;
        if (exists) {
            const std::string next = key.empty() ? folded(segment) : key + "/" + folded(segment);
            const auto it = index_.find(next);
            if (it != index_.end()) {
                key = next;
                name = it->second.name;
            } else {
                exists = false;
            }
        }
        display = display.empty() ? name : display + "/" + name;
    }
    return display;
}

std::vector<ArchiveItem> ArchiveDocument::children(std::string_view folder) const {
    std::vector<ArchiveItem> out;
    const Node* parent = node(folder);
    if (!parent || !parent->directory) return out;
    out.reserve(parent->kids.size());
    for (const std::string& key : parent->kids) out.push_back(itemOf(index_.at(key)));
    std::sort(out.begin(), out.end(), [](const ArchiveItem& a, const ArchiveItem& b) {
        if (a.directory != b.directory) return a.directory;
        return lessFolded(a.name, b.name);
    });
    return out;
}

bool ArchiveDocument::hasFolders(std::string_view folder) const {
    const Node* parent = node(folder);
    if (!parent || !parent->directory) return false;
    for (const std::string& key : parent->kids) {
        if (index_.at(key).directory) return true;
    }
    return false;
}

std::vector<ArchiveItem> ArchiveDocument::subfolders(std::string_view folder) const {
    std::vector<ArchiveItem> out;
    const Node* parent = node(folder);
    if (!parent || !parent->directory) return out;
    for (const std::string& key : parent->kids) {
        const Node& kid = index_.at(key);
        if (kid.directory) out.push_back(itemOf(kid));
    }
    std::sort(out.begin(), out.end(), [](const ArchiveItem& a, const ArchiveItem& b) { return lessFolded(a.name, b.name); });
    return out;
}

std::vector<ArchiveItem> ArchiveDocument::search(std::string_view folder, std::string_view query, std::size_t limit) const {
    std::vector<ArchiveItem> out;
    const std::string needle = folded(query);
    if (needle.empty()) return out;
    const std::string base = canonicalPath(folder);
    for (const auto& [key, n] : index_) {
        if (n.path.empty() || n.path == base) continue;
        if (!archivePathIsInside(n.path, base)) continue;
        if (folded(n.name).find(needle) == std::string::npos) continue;
        out.push_back(itemOf(n));
    }
    std::sort(out.begin(), out.end(), [](const ArchiveItem& a, const ArchiveItem& b) {
        if (a.directory != b.directory) return a.directory;
        return lessFolded(a.path, b.path);
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

int ArchiveDocument::emptyFolderCount() const {
    ensureIndex();
    int count = 0;
    for (const auto& [key, n] : index_) {
        if (n.directory && !key.empty() && n.fileCount == 0) ++count;
    }
    return count;
}

std::vector<std::string> ArchiveDocument::filePathsUnder(std::string_view path) const {
    std::vector<std::string> out;
    const Node* n = node(path);
    if (!n) return out;
    if (!n->directory) {
        out.push_back(n->path);
        return out;
    }
    for (const Entry& e : files_) {
        if (archivePathIsInside(e.path, n->path)) out.push_back(e.path);
    }
    return out;
}

std::vector<std::uint8_t> ArchiveDocument::bytesOf(const Entry& entry) const {
    if (entry.data) return *entry.data;
    if (!archive_ || entry.source < 0) return {};
    return archive_->extract(archive_->files()[static_cast<std::size_t>(entry.source)]);
}

std::vector<std::uint8_t> ArchiveDocument::read(std::string_view filePath) const {
    const Node* n = node(filePath);
    if (!n || n->directory) throw std::runtime_error("There is no file named \"" + std::string(filePath) + "\" in this archive.");
    return bytesOf(files_[static_cast<std::size_t>(n->file)]);
}

void ArchiveDocument::changed() {
    indexValid_ = false;
    ++generation_;
}

void ArchiveDocument::record() {
    Snapshot snap;
    snap.files = files_;
    snap.folders = folders_;
    snap.revision = revision_;
    undo_.push_back(std::move(snap));
    if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    redo_.clear();
    revision_ = nextRevision_++;
    changed();
}

void ArchiveDocument::undo() {
    if (undo_.empty()) return;
    Snapshot now;
    now.files = std::move(files_);
    now.folders = std::move(folders_);
    now.revision = revision_;
    redo_.push_back(std::move(now));
    Snapshot& snap = undo_.back();
    files_ = std::move(snap.files);
    folders_ = std::move(snap.folders);
    revision_ = snap.revision;
    undo_.pop_back();
    changed();
}

void ArchiveDocument::redo() {
    if (redo_.empty()) return;
    Snapshot now;
    now.files = std::move(files_);
    now.folders = std::move(folders_);
    now.revision = revision_;
    undo_.push_back(std::move(now));
    Snapshot& snap = redo_.back();
    files_ = std::move(snap.files);
    folders_ = std::move(snap.folders);
    revision_ = snap.revision;
    redo_.pop_back();
    changed();
}

// ----------------------------------------------------------------------- import

ImportPlan ArchiveDocument::planImport(std::string_view folder, const std::vector<fs::path>& sources) const {
    ImportPlan plan;
    plan.folder = canonicalPath(folder);
    std::unordered_map<std::string, std::size_t> planned;  // folded path -> index in plan.files

    auto add = [&](const std::string& relative, const fs::path& source) {
        std::string path = canonicalPath(joinArchivePath(plan.folder, relative));
        std::error_code ec;
        const std::uint64_t size = fs::file_size(source, ec);
        if (ec) {
            ++plan.skipped;
            return;
        }
        // A folder already sits where the file would go, or a file where one of its folders would.
        const std::vector<std::string> segments = segmentsOf(path);
        std::string key;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            key = key.empty() ? folded(segments[i]) : key + "/" + folded(segments[i]);
            const Node* n = node(key);
            const bool last = i + 1 == segments.size();
            if (n && n->directory == last) {
                ++plan.skipped;
                return;
            }
        }
        ImportEntry entry;
        entry.path = path;
        entry.source = source;
        entry.size = size;
        const std::string lookup = folded(path);
        const auto it = planned.find(lookup);
        if (it != planned.end()) {
            plan.files[it->second] = std::move(entry);
        } else {
            planned.emplace(lookup, plan.files.size());
            plan.files.push_back(std::move(entry));
        }
    };

    for (const fs::path& source : sources) {
        std::error_code ec;
        const fs::path clean = source.has_filename() ? source : source.parent_path();
        if (fs::is_directory(clean, ec)) {
            const std::string rootName = toUtf8(clean.filename());
            if (!archiveNameProblem(rootName).empty()) {
                ++plan.skipped;
                continue;
            }
            fs::recursive_directory_iterator it(clean, fs::directory_options::skip_permission_denied, ec);
            const fs::recursive_directory_iterator end;
            for (; !ec && it != end; it.increment(ec)) {
                std::error_code entryError;
                if (!it->is_regular_file(entryError) || entryError) continue;
                const fs::path relative = fs::relative(it->path(), clean, entryError);
                if (entryError) {
                    ++plan.skipped;
                    continue;
                }
                std::string joined = rootName;
                bool good = true;
                for (const fs::path& part : relative) {
                    const std::string name = toUtf8(part);
                    if (!archiveNameProblem(name).empty()) {
                        good = false;
                        break;
                    }
                    joined += "/" + name;
                }
                if (!good) {
                    ++plan.skipped;
                    continue;
                }
                if (ignoredImportName(toUtf8(relative.filename()))) continue;
                add(joined, it->path());
            }
        } else if (fs::is_regular_file(clean, ec)) {
            const std::string name = toUtf8(clean.filename());
            if (ignoredImportName(name)) continue;
            if (!archiveNameProblem(name).empty()) {
                ++plan.skipped;
                continue;
            }
            add(name, clean);
        } else {
            ++plan.skipped;
        }
    }

    for (const ImportEntry& entry : plan.files) {
        if (isFile(entry.path)) plan.conflicts.push_back(entry.path);
    }
    return plan;
}

int ArchiveDocument::applyImport(const ImportPlan& plan, bool replace) {
    if (!plan.conflicts.empty() && !replace) throw std::runtime_error("Some files already exist in this folder.");
    if (plan.files.empty()) return 0;

    // Read everything before changing anything, so a failure leaves the archive as it was.
    std::vector<Entry> added;
    added.reserve(plan.files.size());
    std::string failure;
    for (const ImportEntry& file : plan.files) {
        std::ifstream in(file.source, std::ios::binary);
        std::vector<std::uint8_t> bytes;
        if (in) {
            in.seekg(0, std::ios::end);
            const std::streamoff length = in.tellg();
            in.seekg(0, std::ios::beg);
            bytes.resize(static_cast<std::size_t>(std::max<std::streamoff>(length, 0)));
            if (!bytes.empty()) in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        if (!in) {
            failure = "Could not read \"" + toUtf8(file.source.filename()) + "\".";
            break;
        }
        Entry entry;
        entry.path = file.path;
        entry.size = bytes.size();
        entry.data = std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
        added.push_back(std::move(entry));
    }
    if (!failure.empty()) throw std::runtime_error(failure);

    record();
    std::unordered_map<std::string, std::size_t> slot;
    for (std::size_t i = 0; i < files_.size(); ++i) slot.emplace(folded(files_[i].path), i);
    for (Entry& entry : added) {
        const std::string key = folded(entry.path);
        const auto it = slot.find(key);
        if (it != slot.end()) {
            files_[it->second] = std::move(entry);
        } else {
            slot.emplace(key, files_.size());
            files_.push_back(std::move(entry));
        }
    }
    return static_cast<int>(plan.files.size());
}

// ------------------------------------------------------------------------- move

MovePlan ArchiveDocument::planMove(const std::vector<std::string>& paths, std::string_view destination) const {
    MovePlan plan;
    ensureIndex();
    const std::string dest = canonicalPath(destination);
    if (!isFolder(dest)) {
        plan.problem = "That folder no longer exists.";
        return plan;
    }

    std::vector<std::string> roots;
    for (const std::string& raw : paths) {
        const Node* n = node(raw);
        if (n && !n->path.empty()) roots.push_back(n->path);
    }
    // An item inside another selected folder travels with it.
    std::vector<std::string> top;
    for (const std::string& path : roots) {
        bool covered = false;
        for (const std::string& other : roots) {
            if (other.size() < path.size() && archivePathIsInside(path, other)) covered = true;
        }
        if (!covered && std::find_if(top.begin(), top.end(), [&](const std::string& t) { return folded(t) == folded(path); }) == top.end()) {
            top.push_back(path);
        }
    }

    std::unordered_set<std::string> targets;
    std::unordered_set<std::string> movingFiles;
    for (const std::string& from : top) {
        const std::string to = canonicalPath(joinArchivePath(dest, archiveLeafName(from)));
        if (folded(to) == folded(from)) continue;  // already there
        const Node* source = node(from);
        const Node* existing = node(to);
        if (source->directory && archivePathIsInside(dest, from)) {
            plan.problem = "A folder cannot be moved into itself.";
            return plan;
        }
        if (!targets.insert(folded(to)).second) {
            plan.problem = "Two of the items are named \"" + archiveLeafName(from) + "\".";
            return plan;
        }
        if (existing && existing->directory != source->directory) {
            plan.problem = source->directory ? "A file named \"" + archiveLeafName(from) + "\" is already in that folder."
                                             : "A folder named \"" + archiveLeafName(from) + "\" is already in that folder.";
            return plan;
        }
        plan.moves.emplace_back(from, to);
    }

    // Files that would land on a different file.
    std::unordered_set<std::string> leaving;
    for (const auto& [from, to] : plan.moves) {
        for (const std::string& file : filePathsUnder(from)) leaving.insert(folded(file));
    }
    for (const auto& [from, to] : plan.moves) {
        for (const std::string& file : filePathsUnder(from)) {
            const std::string target = canonicalPath(rebased(file, from, to));
            const Node* existing = node(target);
            if (existing && !existing->directory && !leaving.count(folded(target))) plan.conflicts.push_back(target);
        }
    }
    return plan;
}

void ArchiveDocument::remap(const std::vector<std::pair<std::string, std::string>>& roots, bool replace) {
    std::unordered_set<std::string> destinations;
    std::vector<char> moving(files_.size(), 0);
    for (std::size_t i = 0; i < files_.size(); ++i) {
        for (const auto& [from, to] : roots) {
            if (!archivePathIsInside(files_[i].path, from)) continue;
            files_[i].path = rebased(files_[i].path, from, to);
            moving[i] = 1;
            destinations.insert(folded(files_[i].path));
            break;
        }
    }
    std::vector<Entry> kept;
    kept.reserve(files_.size());
    for (std::size_t i = 0; i < files_.size(); ++i) {
        if (!moving[i] && destinations.count(folded(files_[i].path))) {
            if (!replace) throw std::runtime_error("A file with that name already exists.");
            continue;
        }
        kept.push_back(std::move(files_[i]));
    }
    files_ = std::move(kept);

    std::set<std::string> folders;
    for (const std::string& folder : folders_) {
        std::string path = folder;
        for (const auto& [from, to] : roots) {
            if (archivePathIsInside(path, from)) {
                path = rebased(path, from, to);
                break;
            }
        }
        folders.insert(path);
    }
    folders_ = std::move(folders);
}

int ArchiveDocument::applyMove(const MovePlan& plan, bool replace) {
    if (!plan.problem.empty()) throw std::runtime_error(plan.problem);
    if (!plan.conflicts.empty() && !replace) throw std::runtime_error("Some files already exist in that folder.");
    if (plan.moves.empty()) return 0;
    record();
    try {
        remap(plan.moves, replace);
    } catch (...) {
        undo();
        redo_.clear();
        throw;
    }
    return static_cast<int>(plan.moves.size());
}

void ArchiveDocument::rename(std::string_view path, std::string_view newName) {
    const Node* source = node(path);
    if (!source || source->path.empty()) throw std::runtime_error("That item no longer exists.");
    const std::string problem = archiveNameProblem(newName);
    if (!problem.empty()) throw std::runtime_error(problem);
    const std::string from = source->path;
    const std::string to = joinArchivePath(archiveParentPath(from), newName);
    if (to == from) return;
    if (folded(to) != folded(from) && node(to)) {
        throw std::runtime_error("An item named \"" + std::string(newName) + "\" already exists here.");
    }
    record();
    remap({{from, to}}, false);
}

std::string ArchiveDocument::createFolder(std::string_view parent, std::string_view name) {
    const std::string problem = archiveNameProblem(name);
    if (!problem.empty()) throw std::runtime_error(problem);
    const std::string base = canonicalPath(parent);
    if (!isFolder(base)) throw std::runtime_error("That folder no longer exists.");
    const std::string path = joinArchivePath(base, name);
    if (node(path)) throw std::runtime_error("An item named \"" + std::string(name) + "\" already exists here.");
    record();
    folders_.insert(path);
    return path;
}

int ArchiveDocument::remove(const std::vector<std::string>& paths) {
    std::vector<std::string> roots;
    for (const std::string& raw : paths) {
        const Node* n = node(raw);
        if (n && !n->path.empty()) roots.push_back(n->path);
    }
    if (roots.empty()) return 0;
    record();
    std::vector<Entry> kept;
    kept.reserve(files_.size());
    for (Entry& e : files_) {
        bool gone = false;
        for (const std::string& root : roots) {
            if (archivePathIsInside(e.path, root)) {
                gone = true;
                break;
            }
        }
        if (!gone) kept.push_back(std::move(e));
    }
    files_ = std::move(kept);
    for (auto it = folders_.begin(); it != folders_.end();) {
        bool gone = false;
        for (const std::string& root : roots) {
            if (archivePathIsInside(*it, root)) {
                gone = true;
                break;
            }
        }
        it = gone ? folders_.erase(it) : std::next(it);
    }
    // The folders that held the items stay, so the one being browsed does not vanish.
    for (const std::string& root : roots) {
        std::string parent = archiveParentPath(root);
        while (!parent.empty()) {
            folders_.insert(parent);
            parent = archiveParentPath(parent);
        }
    }
    return static_cast<int>(roots.size());
}

// ----------------------------------------------------------------------- export

std::vector<std::string> ArchiveDocument::exportConflicts(const std::vector<std::string>& paths, const fs::path& destination) const {
    std::vector<std::string> out;
    std::unordered_set<std::string> done;
    for (const std::string& raw : paths) {
        const Node* root = node(raw);
        if (!root) continue;
        const std::string strip = root->path.empty() ? std::string() : archiveParentPath(root->path);
        for (const std::string& file : filePathsUnder(root->path)) {
            const std::string relative = strip.empty() ? file : file.substr(strip.size() + 1);
            const fs::path target = diskRelative(relative);
            if (target.empty()) continue;
            std::error_code ec;
            if (fs::exists(destination / target, ec) && done.insert(folded(relative)).second) out.push_back(relative);
        }
    }
    return out;
}

ExportResult ArchiveDocument::exportTo(const std::vector<std::string>& paths, const fs::path& destination, bool replace) const {
    ExportResult result;
    std::unordered_set<std::string> done;
    std::error_code ec;
    fs::create_directories(destination, ec);
    for (const std::string& raw : paths) {
        const Node* root = node(raw);
        if (!root) continue;
        const std::string strip = root->path.empty() ? std::string() : archiveParentPath(root->path);
        for (const std::string& file : filePathsUnder(root->path)) {
            const std::string relative = strip.empty() ? file : file.substr(strip.size() + 1);
            if (!done.insert(folded(relative)).second) continue;
            const fs::path rel = diskRelative(relative);
            if (rel.empty()) {
                ++result.skipped;
                continue;
            }
            const fs::path target = destination / rel;
            std::error_code existsError;
            if (fs::exists(target, existsError) && !replace) {
                ++result.skipped;
                continue;
            }
            std::error_code dirError;
            fs::create_directories(target.parent_path(), dirError);
            const std::vector<std::uint8_t> bytes = read(file);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (out && !bytes.empty()) out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) throw std::runtime_error("Could not write \"" + toUtf8(target) + "\".");
            ++result.files;
        }
    }
    return result;
}

// ------------------------------------------------------------------------- save

void ArchiveDocument::save() {
    if (path_.empty()) throw std::runtime_error("This archive has not been saved yet.");
    saveAs(path_);
}

void ArchiveDocument::saveAs(const fs::path& destination) {
    if (files_.empty()) throw std::runtime_error("An archive needs at least one file before it can be saved.");
    std::vector<HpiWriteFile> out;
    out.reserve(files_.size());
    for (const Entry& e : files_) out.push_back(HpiWriteFile{e.path, bytesOf(e)});
    writeHpiArchive(destination, std::move(out));
    const std::uint64_t generation = generation_;
    ArchiveDocument fresh = open(destination);
    *this = std::move(fresh);
    generation_ = generation + 1;
}

}  // namespace galaxy

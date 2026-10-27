#include "core/FavoritesArchive.hpp"

#include "format/HpiArchive.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace galaxy {
namespace {

constexpr std::size_t kLongestName = 64;
constexpr const char* kNoteFile = "GE-Favorites.txt";
constexpr const char kNoteText[] =
    "This archive holds tiles saved with Galaxy Editor's Add to Favorites.\r\n"
    "Copy it next to Galaxy Editor or into the Total Annihilation folder to use or share it.\r\n";

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return out;
}

std::string trimmedName(std::string_view text) {
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

std::string slashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::vector<HpiWriteFile> everyFile(const HpiArchive& archive) {
    std::vector<HpiWriteFile> files;
    files.reserve(archive.files().size() + 1);
    for (const HpiFile& file : archive.files()) {
        files.push_back(HpiWriteFile{file.path, archive.extract(file)});
    }
    return files;
}

HpiWriteFile noteFile() {
    HpiWriteFile note;
    note.path = kNoteFile;
    note.bytes.assign(kNoteText, kNoteText + sizeof(kNoteText) - 1);
    return note;
}

}  // namespace

bool isFavoritesArchiveName(std::string_view filename) {
    const std::string name = lowered(filename);
    const std::string prefix = lowered(kFavoritesPrefix);
    const std::string extension = ".hpi";
    if (name.size() <= prefix.size() + extension.size()) {
        return false;
    }
    return name.compare(0, prefix.size(), prefix) == 0
        && name.compare(name.size() - extension.size(), extension.size(), extension) == 0;
}

std::string favoritesArchiveFilename(std::string_view name) {
    return std::string(kFavoritesPrefix) + trimmedName(name) + ".hpi";
}

std::string favoritesArchiveName(std::string_view filename) {
    if (!isFavoritesArchiveName(filename)) {
        return {};
    }
    return std::string(filename.substr(kFavoritesPrefix.size(), filename.size() - kFavoritesPrefix.size() - 4));
}

std::string favoritesNameProblem(std::string_view name) {
    const std::string text = trimmedName(name);
    if (text.empty()) {
        return "Enter a name.";
    }
    if (text.size() > kLongestName) {
        return "Use 64 characters or fewer.";
    }
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte == 0x7F || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos) {
            return "A name cannot contain < > : \" / \\ | ? * or control characters.";
        }
    }
    if (text.front() == '.' || text.back() == '.') {
        return "A name cannot begin or end with a period.";
    }
    return {};
}

void createFavoritesArchive(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        throw std::runtime_error(path.filename().string() + " already exists.");
    }
    std::vector<HpiWriteFile> files;
    files.push_back(noteFile());
    writeHpiArchive(path, std::move(files));
}

std::string addFavoriteSection(
    const std::filesystem::path& archivePath,
    std::string_view category,
    std::string_view subcategory,
    const SectionStamp& section) {
    const std::string categoryName = trimmedName(category);
    const std::string subcategoryName = trimmedName(subcategory);
    if (const std::string problem = favoritesNameProblem(categoryName); !problem.empty()) {
        throw std::runtime_error("Category: " + problem);
    }
    if (const std::string problem = favoritesNameProblem(subcategoryName); !problem.empty()) {
        throw std::runtime_error("Subcategory: " + problem);
    }
    // Encode first so a stamp that cannot be saved never touches the archive.
    std::vector<std::uint8_t> bytes = writeSectionStamp(section);

    const HpiArchive archive = HpiArchive::open(archivePath);
    std::vector<HpiWriteFile> files = everyFile(archive);

    const std::string folder = "sections/" + categoryName + "/" + subcategoryName + "/";
    const std::string folderLower = lowered(folder);
    std::set<std::string> taken;
    for (const HpiWriteFile& file : files) {
        const std::string path = lowered(slashes(file.path));
        if (path.compare(0, folderLower.size(), folderLower) == 0) {
            taken.insert(path);
        }
    }
    std::string name;
    for (int number = 1; number < 100000; ++number) {
        char digits[16];
        std::snprintf(digits, sizeof(digits), "%02d", number);
        const std::string candidate = subcategoryName + digits;
        if (taken.count(lowered(folder + candidate + ".sct")) == 0) {
            name = candidate;
            break;
        }
    }
    if (name.empty()) {
        throw std::runtime_error("That subcategory has no room for another tile.");
    }

    files.push_back(HpiWriteFile{folder + name + ".sct", std::move(bytes)});
    writeHpiArchive(archivePath, std::move(files));
    return name;
}

void removeFavoriteSection(const std::filesystem::path& archivePath, std::string_view sectionPath) {
    const HpiArchive archive = HpiArchive::open(archivePath);
    const std::string target = lowered(slashes(std::string(sectionPath)));
    std::vector<HpiWriteFile> kept;
    bool found = false;
    for (const HpiFile& file : archive.files()) {
        if (lowered(slashes(file.path)) == target) {
            found = true;
            continue;
        }
        kept.push_back(HpiWriteFile{file.path, archive.extract(file)});
    }
    if (!found) {
        throw std::runtime_error("That tile is no longer in " + archivePath.filename().string() + ".");
    }
    if (kept.empty()) {
        kept.push_back(noteFile());
    }
    writeHpiArchive(archivePath, std::move(kept));
}

}  // namespace galaxy

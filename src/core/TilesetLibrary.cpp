#include "core/TilesetLibrary.hpp"

#include "format/HpiArchive.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace galaxy {

std::filesystem::path folderBesideAppBundle(const std::filesystem::path& executableDirectory) {
    const std::filesystem::path contents = executableDirectory.parent_path();
    const std::filesystem::path bundle = contents.parent_path();
    if (executableDirectory.filename() == "MacOS" && contents.filename() == "Contents" && bundle.extension() == ".app") {
        return bundle.parent_path();
    }
    return executableDirectory;
}

std::filesystem::path programDirectory() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
        return {};
    }
    return folderBesideAppBundle(std::filesystem::path(buffer.c_str()).parent_path());
#elif defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path();
#else
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    buffer[length] = '\0';
    return std::filesystem::path(buffer).parent_path();
#endif
}

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::vector<std::string> splitPath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

bool isGameArchive(const std::filesystem::path& path) {
    const std::string ext = lower(path.extension().string());
    return ext == ".hpi" || ext == ".ccx" || ext == ".gp3" || ext == ".gpf";
}

std::string trimCopy(std::string_view text) {
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

int footprintOrOne(std::string_view text) {
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+') {
        ++i;
    }
    if (i >= text.size() || std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
        return 1;
    }
    int value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        if (value > 64) {
            return 1;
        }
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    if (value < 1 || value > 64) {
        return 1;
    }
    return value;
}

int numberOrZero(std::string_view text) {
    std::size_t i = 0;
    if (i < text.size() && text[i] == '+') {
        ++i;
    }
    if (i >= text.size() || std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
        return 0;
    }
    int value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        if (value > 100000000) {
            return 0;
        }
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    return value;
}

std::string stripTdfComments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool line = false;
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\0') {
            continue;
        }
        if (line) {
            if (c == '\n') {
                line = false;
                out.push_back('\n');
            }
            continue;
        }
        if (block) {
            if (c == '*' && i + 1 < text.size() && text[i + 1] == '/') {
                block = false;
                ++i;
            } else if (c == '\n') {
                out.push_back('\n');
            }
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            line = true;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            block = true;
            ++i;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

void applyFeatureField(FeatureInfo& feature, std::string key, std::string value) {
    key = lower(trimCopy(key));
    value = trimCopy(value);
    if (key == "world") {
        feature.world = value;
    } else if (key == "category") {
        feature.category = value;
    } else if (key == "footprintx") {
        feature.footprintX = footprintOrOne(value);
    } else if (key == "footprintz") {
        feature.footprintZ = footprintOrOne(value);
    } else if (key == "filename") {
        feature.animFile = value;
    } else if (key == "seqname") {
        feature.sequence = value;
    } else if (key == "object") {
        feature.object = value;
    } else if (key == "metal") {
        feature.metal = numberOrZero(value);
    } else if (key == "energy") {
        feature.energy = numberOrZero(value);
    }
}

bool sameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    if (std::filesystem::exists(a, error) && std::filesystem::exists(b, error)) {
        return std::filesystem::equivalent(a, b, error);
    }
    return false;
}

}  // namespace

std::vector<FeatureInfo> parseFeatureTdf(std::string_view text) {
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    const std::string source = stripTdfComments(text);
    std::vector<FeatureInfo> features;
    int depth = 0;
    bool inFeature = false;
    bool haveName = false;
    std::string pendingName;
    FeatureInfo current;

    for (std::size_t i = 0; i < source.size();) {
        const char c = source[i];
        if (c == '{') {
            ++depth;
            ++i;
            if (depth == 1 && haveName) {
                current = FeatureInfo{};
                current.name = trimCopy(pendingName);
                inFeature = !current.name.empty();
                haveName = false;
            }
            continue;
        }
        if (c == '}') {
            if (depth == 1 && inFeature) {
                features.push_back(std::move(current));
                inFeature = false;
            }
            if (depth > 0) {
                --depth;
            }
            ++i;
            continue;
        }
        if (c == '[') {
            const auto end = source.find(']', i + 1);
            if (end == std::string::npos) {
                break;
            }
            if (depth == 0) {
                pendingName = source.substr(i + 1, end - (i + 1));
                haveName = true;
            }
            i = end + 1;
            continue;
        }
        if (depth == 1 && inFeature && std::isspace(static_cast<unsigned char>(c)) == 0) {
            const auto eq = source.find('=', i);
            const auto stop = source.find_first_of("{}", i);
            if (eq == std::string::npos || (stop != std::string::npos && stop < eq)) {
                ++i;
                continue;
            }
            const auto valueEnd = source.find_first_of(";\n", eq + 1);
            const std::size_t end = valueEnd == std::string::npos ? source.size() : valueEnd;
            applyFeatureField(current, source.substr(i, eq - i), source.substr(eq + 1, end - (eq + 1)));
            i = end < source.size() ? end + 1 : end;
            continue;
        }
        ++i;
    }
    return features;
}

const HpiArchive& TilesetArchive::archive() const {
    if (!contents) {
        throw std::runtime_error("Tileset archive is not open");
    }
    return *contents;
}

std::map<std::string, std::map<std::string, int>> TilesetArchive::categoryCounts() const {
    std::map<std::string, std::map<std::string, int>> counts;
    for (const TilesetSection& section : sections) {
        ++counts[section.world][section.category];
    }
    return counts;
}

int TilesetLibrary::sectionCount() const {
    int count = 0;
    for (const TilesetArchive& archive : archives_) {
        count += static_cast<int>(archive.sections.size());
    }
    return count;
}

void TilesetLibrary::addArchive(const std::filesystem::path& path) {
    for (const TilesetArchive& existing : archives_) {
        if (sameFile(existing.path, path)) {
            return;
        }
    }

    HpiArchive opened = HpiArchive::open(path);
    TilesetArchive info;
    info.path = path;
    info.contents = std::make_unique<HpiArchive>(std::move(opened));
    for (const HpiFile& file : info.contents->files()) {
        const auto parts = splitPath(file.path);
        if (parts.size() < 4 || lower(parts[0]) != "sections") {
            continue;
        }
        const std::string filename = parts.back();
        const auto dot = filename.find_last_of('.');
        if (dot == std::string::npos || lower(filename.substr(dot)) != ".sct") {
            continue;
        }
        TilesetSection section;
        section.world = parts[1];
        section.category = parts[2];
        section.name = filename.substr(0, dot);
        section.archivePath = file.path;
        info.sections.push_back(std::move(section));
    }
    for (const HpiFile& file : info.contents->files()) {
        const auto parts = splitPath(file.path);
        if (parts.size() < 2 || lower(parts[0]) != "features") {
            continue;
        }
        const std::string filename = parts.back();
        const auto dot = filename.find_last_of('.');
        if (dot == std::string::npos || lower(filename.substr(dot)) != ".tdf") {
            continue;
        }
        try {
            const std::vector<std::uint8_t> bytes = info.contents->extract(file);
            if (bytes.size() > 2 * 1024 * 1024) {
                continue;
            }
            const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            for (FeatureInfo feature : parseFeatureTdf(text)) {
                if (feature.world.empty() && parts.size() >= 3) {
                    feature.world = parts[1];
                }
                if (feature.category.empty() && parts.size() >= 4) {
                    feature.category = parts[2];
                }
                info.features.push_back(std::move(feature));
            }
        } catch (const std::exception&) {
        }
    }
    archives_.push_back(std::move(info));
}

std::vector<std::filesystem::path> TilesetLibrary::archivePathsIn(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> found;
    std::error_code error;
    if (directory.empty() || !std::filesystem::is_directory(directory, error)) {
        return found;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) || !isGameArchive(entry.path())) {
            continue;
        }
        found.push_back(entry.path());
    }
    std::sort(found.begin(), found.end(), [](const std::filesystem::path& a, const std::filesystem::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    return found;
}

std::vector<std::filesystem::path> TilesetLibrary::archivePathsFor(
    const std::filesystem::path& gameFolder,
    const std::filesystem::path& programFolder) {
    std::vector<std::filesystem::path> paths = archivePathsIn(gameFolder);
    std::vector<std::string> taken;
    taken.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        taken.push_back(lower(path.filename().string()));
    }
    for (const std::filesystem::path& path : archivePathsIn(programFolder)) {
        const std::string name = lower(path.filename().string());
        if (std::find(taken.begin(), taken.end(), name) != taken.end()) {
            continue;
        }
        taken.push_back(name);
        paths.push_back(path);
    }
    return paths;
}

void TilesetLibrary::clear() {
    archives_.clear();
}

void TilesetLibrary::sortArchives() {
    std::sort(archives_.begin(), archives_.end(), [](const TilesetArchive& a, const TilesetArchive& b) {
        const std::string left = lower(a.path.filename().string());
        const std::string right = lower(b.path.filename().string());
        if (left != right) {
            return left < right;
        }
        return a.path.generic_string() < b.path.generic_string();
    });
}

}  // namespace galaxy

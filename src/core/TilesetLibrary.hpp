#pragma once

#include "format/HpiArchive.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy {

struct FeatureInfo {
    std::string name;
    std::string world;
    std::string category;
    int footprintX = 1;
    int footprintZ = 1;
    int metal = 0;
    int energy = 0;
    std::string animFile;
    std::string sequence;
    std::string object;
};

std::vector<FeatureInfo> parseFeatureTdf(std::string_view text);

struct TilesetSection {
    std::string world;
    std::string category;
    std::string name;
    std::string archivePath;
};

struct TilesetArchive {
    std::filesystem::path path;
    std::vector<TilesetSection> sections;
    std::vector<FeatureInfo> features;
    std::unique_ptr<HpiArchive> contents;

    const HpiArchive& archive() const;
    std::map<std::string, std::map<std::string, int>> categoryCounts() const;
};

// Opens one archive and lists its sections and features. Throws when the file is not a readable archive.
TilesetArchive openTilesetArchive(const std::filesystem::path& path);

class TilesetLibrary {
public:
    // Lists every game archive (.hpi, .ccx, .gp3, .gpf) directly inside a folder
    // without opening any of them, so a caller can load them one at a time. An
    // empty or missing folder gives an empty list.
    static std::vector<std::filesystem::path> archivePathsIn(const std::filesystem::path& directory);
    // The archives to load at startup: everything in the game folder, then the
    // archives next to the program, which is where users add their own tilesets.
    // A program-folder archive whose file name is already taken by the game folder
    // is skipped, so worlds.hpi or totala1.hpi never loads twice.
    static std::vector<std::filesystem::path> archivePathsFor(
        const std::filesystem::path& gameFolder,
        const std::filesystem::path& programFolder);
    // Forgets every loaded archive.
    void clear();
    void addArchive(const std::filesystem::path& path);
    // Reads the archive again from disk. It takes the place of the loaded copy
    // of the same file, so every other archive keeps its position, or goes on
    // the end when the file is new. Returns the archive's position. When the
    // file cannot be read the library is unchanged and this throws.
    std::size_t refreshArchive(const std::filesystem::path& path);
    // Puts the archives in file name order. Call after the last addArchive.
    void sortArchives();

    const std::vector<TilesetArchive>& archives() const { return archives_; }
    int sectionCount() const;

private:
    std::vector<TilesetArchive> archives_;
};

// The folder that holds the running program. Empty when the system will not say.
// A macOS app bundle reports the folder that holds the .app, so tilesets sit
// beside GalaxyEditor.app instead of buried inside it.
std::filesystem::path programDirectory();

// Maps the folder of an executable to the folder users put archives in. For
// /Apps/Galaxy/GalaxyEditor.app/Contents/MacOS it gives /Apps/Galaxy. Any other
// folder comes back unchanged.
std::filesystem::path folderBesideAppBundle(const std::filesystem::path& executableDirectory);

}  // namespace galaxy

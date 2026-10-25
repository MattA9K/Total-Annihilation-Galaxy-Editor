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

class TilesetLibrary {
public:
    void loadDefaultLocations();
    void addArchive(const std::filesystem::path& path);

    const std::vector<TilesetArchive>& archives() const { return archives_; }
    int sectionCount() const;

private:
    std::vector<TilesetArchive> archives_;
};

std::vector<std::filesystem::path> defaultTilesetDirectories();

}  // namespace galaxy

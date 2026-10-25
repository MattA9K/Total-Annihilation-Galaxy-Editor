#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace galaxy {

struct HpiFile {
    std::string path;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::uint8_t compression = 0;
};

class HpiArchive {
public:
    static HpiArchive open(const std::filesystem::path& path);

    const std::filesystem::path& path() const { return path_; }
    const std::vector<HpiFile>& files() const { return files_; }

    const HpiFile* find(std::string_view name) const;
    std::vector<std::uint8_t> extract(const HpiFile& file) const;

private:
    std::filesystem::path path_;
    std::vector<std::uint8_t> data_;
    std::vector<HpiFile> files_;
    std::uint8_t key_ = 0;
};

struct HpiWriteFile {
    std::string path;
    std::vector<std::uint8_t> bytes;
};

// Writes an HPI/UFO archive. Paths use slashes, such as "maps/Name.tnt".
void writeHpiArchive(const std::filesystem::path& path, std::vector<HpiWriteFile> files);

}  // namespace galaxy

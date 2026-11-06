#include "core/ArchiveDocument.hpp"
#include "core/BrushStrokes.hpp"
#include "core/EditorSettings.hpp"
#include "core/I18n.hpp"
#include "core/FavoritesArchive.hpp"
#include "core/MapGenerator.hpp"
#include "core/OrbitCamera.hpp"
#include "core/RiverBrush.hpp"
#include "core/SlopeBrush.hpp"
#include "core/TilesetLibrary.hpp"
#include "core/Version.hpp"
#include "format/GafImage.hpp"
#include "format/HpiArchive.hpp"
#include "format/MapFile.hpp"
#include "format/SctSection.hpp"
#include "format/TaPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct IndexedBmp {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

IndexedBmp readBmp8(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    expect(static_cast<bool>(in), "open " + path.string());
    std::vector<std::uint8_t> file(std::istreambuf_iterator<char>(in), {});
    const auto u16 = [&](std::size_t offset) {
        return static_cast<int>(file[offset] | (file[offset + 1] << 8));
    };
    const auto u32 = [&](std::size_t offset) {
        return static_cast<int>(file[offset] | (file[offset + 1] << 8) | (file[offset + 2] << 16) | (file[offset + 3] << 24));
    };
    const int pixelOffset = u32(10);
    const int width = u32(18);
    const int height = u32(22);
    const int bpp = u16(28);
    expect(bpp == 8, path.string() + " is not an 8-bit BMP");
    const int stride = (width + 3) & ~3;
    IndexedBmp bmp;
    bmp.width = width;
    bmp.height = height;
    bmp.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        const int fileRow = height - 1 - y;
        const std::uint8_t* src = file.data() + pixelOffset + static_cast<std::size_t>(fileRow) * static_cast<std::size_t>(stride);
        std::uint8_t* dst = bmp.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        std::copy(src, src + width, dst);
    }
    return bmp;
}

void expectMatchesBmp(const galaxy::LoadedMap& map, const std::filesystem::path& bmpPath) {
    const IndexedBmp bmp = readBmp8(bmpPath);
    expect(bmp.width == map.pixelWidth() && bmp.height == map.pixelHeight(), bmpPath.string() + " size");
    if (bmp.width != map.pixelWidth() || bmp.height != map.pixelHeight()) {
        return;
    }
    int mismatches = 0;
    for (int ty = 0; ty < map.tileHeight; ++ty) {
        for (int tx = 0; tx < map.tileWidth; ++tx) {
            const std::uint16_t index = map.indices[static_cast<std::size_t>(ty * map.tileWidth + tx)];
            const std::uint8_t* tile = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
            for (int py = 0; py < 32; ++py) {
                for (int px = 0; px < 32; ++px) {
                    const int x = tx * 32 + px;
                    const int y = ty * 32 + py;
                    const std::uint8_t expected = bmp.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(bmp.width) + static_cast<std::size_t>(x)];
                    if (tile[py * 32 + px] != expected) {
                        ++mismatches;
                    }
                }
            }
        }
    }
    expect(mismatches == 0, bmpPath.filename().string() + " differs in " + std::to_string(mismatches) + " pixels");
}

std::vector<std::uint8_t> buildSct(int version, int width, int height, int tileCount, std::uint8_t heightValue, std::uint8_t color) {
    const std::uint32_t minimapAt = 28;
    const std::uint32_t tilesAt = minimapAt + 128u * 128u;
    const std::uint32_t dataAt = tilesAt + static_cast<std::uint32_t>(tileCount) * 1024u;
    const std::uint32_t attrCount = static_cast<std::uint32_t>(width) * 2u * static_cast<std::uint32_t>(height) * 2u;
    const std::uint32_t stride = version == 2 ? 8u : 4u;
    const std::uint32_t heightAt = dataAt + static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * 2u;
    std::vector<std::uint8_t> bytes(heightAt + attrCount * stride, 0);
    auto writeU32 = [&](std::size_t offset, std::uint32_t value) {
        bytes[offset] = static_cast<std::uint8_t>(value & 255u);
        bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 255u);
        bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 255u);
        bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 255u);
    };
    writeU32(0, static_cast<std::uint32_t>(version));
    writeU32(4, minimapAt);
    writeU32(8, static_cast<std::uint32_t>(tileCount));
    writeU32(12, tilesAt);
    writeU32(16, static_cast<std::uint32_t>(width));
    writeU32(20, static_cast<std::uint32_t>(height));
    writeU32(24, dataAt);
    std::fill(bytes.begin() + minimapAt, bytes.begin() + tilesAt, color);
    std::fill(bytes.begin() + tilesAt, bytes.begin() + dataAt, color);
    for (std::uint32_t i = 0; i < attrCount; ++i) {
        bytes[heightAt + i * stride] = heightValue;
        bytes[heightAt + i * stride + 1] = 0xAB;
    }
    return bytes;
}

galaxy::LoadedMap blankMap(int tiles, std::uint8_t fill) {
    galaxy::LoadedMap map;
    map.tileWidth = tiles;
    map.tileHeight = tiles;
    map.tileCount = 1;
    map.indices.assign(static_cast<std::size_t>(tiles * tiles), 0);
    map.tileImages.assign(1024, fill);
    map.heights.assign(static_cast<std::size_t>(tiles * 2 * tiles * 2), 0);
    map.features.push_back(galaxy::FeaturePlacement{1, 2, "Rock"});
    return map;
}

void testHeightGrid() {
    galaxy::LoadedMap map;
    map.tileWidth = 4;
    map.tileHeight = 4;
    map.seaLevel = 75;
    map.heights.assign(8 * 8, 85);
    std::vector<std::uint8_t> overlay(128 * 128 * 4);
    auto at = [&](int x, int y) {
        return overlay.data() + (static_cast<std::size_t>(y) * 128 + static_cast<std::size_t>(x)) * 4;
    };

    galaxy::renderHeightGrid(map, overlay.data(), 128, 128, 128.0f, 128.0f, 0.0f, 0.0f, 1.0f, 32);
    const std::uint8_t* flat = at(16, 16);
    const std::uint8_t* line = at(31, 16);
    expect(flat[1] > flat[0] && flat[1] > flat[2], "flat ground height is green");
    expect(flat[3] > 0 && flat[3] < line[3], "height grid lines are stronger than the fill");

    std::fill(map.heights.begin(), map.heights.end(), 0);
    galaxy::renderHeightGrid(map, overlay.data(), 128, 128, 128.0f, 128.0f, 0.0f, 0.0f, 1.0f, 32);
    const std::uint8_t* water = at(16, 16);
    expect(water[2] > water[0] && water[2] > water[1], "water height is blue");

    std::fill(map.heights.begin(), map.heights.end(), 220);
    galaxy::renderHeightGrid(map, overlay.data(), 128, 128, 128.0f, 128.0f, 0.0f, 0.0f, 1.0f, 32);
    const std::uint8_t* high = at(16, 16);
    expect(high[0] > high[2], "mountain height is warmer than the water blue");
}

void testSaveRoundTrip(const galaxy::LoadedMap& map, const std::filesystem::path& destination, const std::string& label);

void testResizeMap() {
    auto map = galaxy::createBlankMap(4, 3);
    for (int y = 0; y < map.tileHeight; ++y) {
        for (int x = 0; x < map.tileWidth; ++x) {
            map.indices[static_cast<std::size_t>(y) * map.tileWidth + x] = static_cast<std::uint32_t>(y * 10 + x);
        }
    }
    for (int y = 0; y < map.heightHeight(); ++y) {
        for (int x = 0; x < map.heightWidth(); ++x) {
            map.heights[static_cast<std::size_t>(y) * map.heightWidth() + x] = static_cast<std::uint8_t>(y * 20 + x);
        }
    }
    map.featureNames.push_back("Tree");
    map.featureIndices[static_cast<std::size_t>(2) * map.heightWidth() + 1] = 0;
    map.starts.push_back(galaxy::StartPosition{1, 40, 50});
    map.starts.push_back(galaxy::StartPosition{2, 8, 8});
    const char* ota = "[GlobalHeader]\r\n{\r\nsize=1 x 1;\r\nmissionname=Keep Me;\r\n}\r\n";
    map.otaText.assign(ota, ota + std::strlen(ota));
    map.missionName = "Keep Me";

    const auto unchanged = map;
    expect(!galaxy::resizeMap(map, -4, 0, 0, 0), "a resize that leaves no tiles is refused");
    expect(map.tileWidth == unchanged.tileWidth && map.indices == unchanged.indices, "a refused resize leaves the map alone");
    expect(!galaxy::resizeMap(map, 2048, 0, 0, 0), "a resize past 2048 tiles is refused");

    expect(galaxy::resizeMap(map, 0, 0, 1, 0), "adding a tile to the top is allowed");
    expect(map.tileWidth == 4 && map.tileHeight == 4, "the top grows by one tile");
    expect(map.indices[2] == 2, "the new top row continues the old top row");
    expect(map.indices[static_cast<std::size_t>(1) * 4 + 2] == 2, "the old top row moves down one tile");
    expect(map.heights[0] == 0 && map.heights[map.heightWidth()] == 0, "the new height rows continue the old top");
    expect(map.heights[static_cast<std::size_t>(2) * map.heightWidth()] == 0, "the old top height row moves down two cells");
    expect(map.heights[static_cast<std::size_t>(2) * map.heightWidth() + 3] == 3, "a height inside the old map keeps its value");
    bool treeMoved = false;
    for (const galaxy::FeaturePlacement& feature : map.features) {
        treeMoved = treeMoved || (feature.name == "Tree" && feature.x == 1 && feature.y == 4);
    }
    expect(treeMoved, "a feature moves down with the ground");
    expect(map.featureIndices[0] == 0xFFFF && map.featureIndices[map.heightWidth()] == 0xFFFF, "new rows have no features");
    expect(map.starts.size() == 2 && map.starts[0].y == 82 && map.starts[1].y == 40, "start positions move down with the ground");
    const std::string otaText(map.otaText.begin(), map.otaText.end());
    expect(otaText.find("size=1 x 1;") != std::string::npos, "the saved size follows the new map");
    expect(otaText.find("Keep Me") != std::string::npos, "resizing leaves the mission name in place");

    expect(galaxy::resizeMap(map, 1, 0, 0, -1), "adding on the left and removing on the bottom is allowed");
    expect(map.tileWidth == 5 && map.tileHeight == 3, "left and bottom change independently");
    expect(map.indices[0] == 0, "the new left column continues the old left column");
    expect(map.indices[1] == 0 && map.indices[2] == 1, "the old left column moves right one tile");
    treeMoved = false;
    for (const galaxy::FeaturePlacement& feature : map.features) {
        treeMoved = treeMoved || (feature.name == "Tree" && feature.x == 3 && feature.y == 4);
    }
    expect(treeMoved, "a feature moves right with the ground");
    expect(map.starts.size() == 2, "both starts still fit");
    expect(map.starts[0].x == 72 && map.starts[0].y == 82, "a start moves right with the ground");

    map.starts.push_back(galaxy::StartPosition{3, 4, 10});
    expect(galaxy::resizeMap(map, -1, 0, 0, 0), "removing the new left column is allowed");
    expect(map.starts.size() == 2 && map.starts[0].player == 1 && map.starts[1].player == 2, "a start on the removed edge is deleted");
    expect(map.starts[0].x == 40, "the remaining start moves back with the ground");

    auto wide = galaxy::createBlankMap(16, 16);
    wide.otaText.assign(ota, ota + std::strlen(ota));
    expect(galaxy::resizeMap(wide, 1, 0, 0, 0), "a wide map can grow by one tile");
    const std::string wideOta(wide.otaText.begin(), wide.otaText.end());
    expect(wideOta.find("size=2 x 1;") != std::string::npos, "the saved size follows the new map");

    auto saved = galaxy::createBlankMap(4, 3);
    saved.heights[3] = 40;
    saved.featureNames.push_back("Tree");
    saved.featureIndices[1] = 0;
    saved.starts.push_back(galaxy::StartPosition{1, 40, 50});
    expect(galaxy::resizeMap(saved, 0, 1, 1, 0), "a blank map can grow for a save");
    expect(saved.heights[static_cast<std::size_t>(2) * saved.heightWidth() + 3] == 40, "a height survives the shift into the saved map");
    expect(saved.starts.size() == 1 && saved.starts[0].y == 82, "a start survives the shift into the saved map");
    testSaveRoundTrip(saved, std::filesystem::temp_directory_path() / "galaxy-resize-map-test.ufo", "resized map");
}

void testSyntheticSections() {
    for (const int version : {2, 3}) {
        const auto bytes = buildSct(version, 1, 1, 1, 40, 167);
        const galaxy::SectionStamp stamp = galaxy::loadSectionStamp(bytes);
        expect(stamp.version == version, "parsed section version");
        expect(stamp.tileWidth == 1 && stamp.tileHeight == 1 && stamp.tileCount == 1, "parsed 1x1 section");
        expect(stamp.minimap.size() == 128u * 128u && stamp.minimap.front() == 167 && stamp.minimap.back() == 167, "section minimap");
        expect(stamp.tileImages.size() == 1024 && stamp.tileImages.front() == 167 && stamp.tileImages.back() == 167, "section tile bytes");
        expect(stamp.heights.size() == 4 && stamp.heights.front() == 40 && stamp.heights.back() == 40, "section height ignores padding");

        const auto preview = galaxy::renderMinimapRgba(stamp);
        expect(preview.size() == 128u * 128u * 4, "minimap rgba size");
        expect(preview[0] == galaxy::kTaPalette[167][0] && preview[1] == galaxy::kTaPalette[167][1] && preview[2] == galaxy::kTaPalette[167][2], "minimap color");

        int width = 0;
        int height = 0;
        const auto image = galaxy::renderSectionRgba(stamp, 2048, width, height);
        expect(width == 32 && height == 32, "section image is one tile");
        expect(image[0] == galaxy::kTaPalette[167][0] && image[1] == galaxy::kTaPalette[167][1], "section image color");
        int smallW = 0;
        int smallH = 0;
        galaxy::renderSectionRgba(stamp, 16, smallW, smallH);
        expect(smallW == 16 && smallH == 16, "section image scales down");

        galaxy::LoadedMap map = blankMap(4, 100);
        galaxy::TileDeduper deduper;
        const int written = galaxy::stampSection(map, stamp, 0, 0, &deduper);
        expect(written == 1, "stamp writes one tile");
        expect(map.tileCount == 2, "stamp appends a new tile");
        expect(map.indices[0] == 1, "stamped cell uses the new tile");
        const std::vector<std::uint8_t> expected(1024, 167);
        expect(std::memcmp(map.tileImages.data() + 1024, expected.data(), 1024) == 0, "stamped tile bytes");
        expect(map.heights[0] == 40 && map.heights[1] == 40 && map.heights[8] == 40 && map.heights[9] == 40, "stamped heights");
        expect(map.heights[2] == 0, "height outside the stamp");
        expect(map.features.size() == 1 && map.features[0].name == "Rock" && map.features[0].x == 1, "stamp keeps features");
        const int again = galaxy::stampSection(map, stamp, 0, 0, &deduper);
        expect(again == 1 && map.tileCount == 2, "second stamp reuses the tile");
    }

    galaxy::LoadedMap clipped = blankMap(4, 100);
    const galaxy::SectionStamp wide = galaxy::loadSectionStamp(buildSct(3, 2, 2, 1, 40, 167));
    expect(galaxy::stampSection(clipped, wide, 3, 3, nullptr) == 1, "stamp clips to the map");
    expect(clipped.indices[15] == 1, "clipped stamp writes the inside cell");
    expect(clipped.heights[0] == 0, "clipped stamp leaves the far corner");
    expect(clipped.heights[6 * 8 + 6] == 40, "clipped stamp writes the covered height");

    galaxy::LoadedMap untouched = blankMap(4, 100);
    expect(galaxy::stampSection(untouched, galaxy::loadSectionStamp(buildSct(2, 1, 1, 1, 40, 167)), -1, 0, nullptr) == 0, "stamp off the map writes nothing");
    expect(untouched.tileCount == 1, "off-map stamp does not grow the tile pool");

    auto badIndex = buildSct(3, 1, 1, 1, 10, 4);
    badIndex[28u + 128u * 128u + 1024u] = 5;
    bool rejectedIndex = false;
    try {
        galaxy::loadSectionStamp(badIndex);
    } catch (const std::runtime_error&) {
        rejectedIndex = true;
    }
    expect(rejectedIndex, "section rejects a tile index past the pool");

    auto badVersion = buildSct(3, 1, 1, 1, 10, 4);
    badVersion[0] = 1;
    bool rejectedVersion = false;
    try {
        galaxy::loadSectionStamp(badVersion);
    } catch (const std::runtime_error&) {
        rejectedVersion = true;
    }
    expect(rejectedVersion, "section rejects an unknown version");
}

void testGreenFlat(const galaxy::TilesetLibrary& library, const galaxy::LoadedMap& grass, const std::filesystem::path& maps) {
    const galaxy::TilesetSection* found = nullptr;
    const galaxy::TilesetArchive* owner = nullptr;
    for (const galaxy::TilesetArchive& archive : library.archives()) {
        for (const galaxy::TilesetSection& section : archive.sections) {
            if (section.category == "Flat" && section.name == "GreenFlat02") {
                found = &section;
                owner = &archive;
            }
        }
    }
    expect(found != nullptr && owner != nullptr, "GreenFlat02 is catalogued");
    if (found == nullptr || owner == nullptr) {
        return;
    }
    const galaxy::HpiFile* file = owner->archive().find(found->archivePath);
    expect(file != nullptr, "GreenFlat02 can be extracted");
    if (file == nullptr) {
        return;
    }

    galaxy::SectionStamp stamp;
    try {
        stamp = galaxy::loadSectionStamp(owner->archive().extract(*file));
    } catch (const std::exception& ex) {
        expect(false, std::string("GreenFlat02: ") + ex.what());
        return;
    }
    expect(stamp.tileWidth == 8 && stamp.tileHeight == 8,
        "GreenFlat02 is " + std::to_string(stamp.tileWidth) + "x" + std::to_string(stamp.tileHeight));
    expect(stamp.tileCount == 16, "GreenFlat02 has " + std::to_string(stamp.tileCount) + " tiles");
    expect(stamp.minimap.size() == 128u * 128u, "GreenFlat02 minimap");
    const bool flatHeight = !stamp.heights.empty()
        && std::all_of(stamp.heights.begin(), stamp.heights.end(), [](std::uint8_t height) { return height == 85; });
    expect(flatHeight, "GreenFlat02 height is 85");

    if (stamp.tileCount > 0 && stamp.tileImages.size() == static_cast<std::size_t>(stamp.tileCount) * 1024) {
        for (int tile = 0; tile < stamp.tileCount; ++tile) {
            const std::uint8_t* src = stamp.tileImages.data() + static_cast<std::size_t>(tile) * 1024;
            bool match = false;
            for (int existing = 0; existing < grass.tileCount; ++existing) {
                if (std::memcmp(src, grass.tileImages.data() + static_cast<std::size_t>(existing) * 1024, 1024) == 0) {
                    match = true;
                    break;
                }
            }
            expect(match, "GreenFlat02 tile " + std::to_string(tile) + " is in the grass map");
        }
    }

    if (stamp.tileWidth > 0 && stamp.tileHeight > 0 && stamp.tileWidth <= grass.tileWidth && stamp.tileHeight <= grass.tileHeight) {
        const IndexedBmp grassBmp = readBmp8(maps / "64x64_SimpleFlatMapWithGrass.bmp");
        int mismatches = 0;
        if (grassBmp.width == grass.pixelWidth() && grassBmp.height == grass.pixelHeight()) {
            for (int ty = 0; ty < stamp.tileHeight; ++ty) {
                for (int tx = 0; tx < stamp.tileWidth; ++tx) {
                    const std::uint16_t index = stamp.indices[static_cast<std::size_t>(ty * stamp.tileWidth + tx)];
                    if (index >= static_cast<std::uint16_t>(stamp.tileCount)) {
                        ++mismatches;
                        continue;
                    }
                    const std::uint8_t* tile = stamp.tileImages.data() + static_cast<std::size_t>(index) * 1024;
                    for (int py = 0; py < 32; ++py) {
                        for (int px = 0; px < 32; ++px) {
                            const std::uint8_t expected = grassBmp.pixels[static_cast<std::size_t>(ty * 32 + py) * static_cast<std::size_t>(grassBmp.width) + static_cast<std::size_t>(tx * 32 + px)];
                            if (tile[py * 32 + px] != expected) {
                                ++mismatches;
                            }
                        }
                    }
                }
            }
        }
        expect(mismatches == 0, "GreenFlat02 differs from the grass map in " + std::to_string(mismatches) + " pixels");
    }

    auto blank = galaxy::loadMapFile(maps / "64x64_BlankMapNoTiles.ufo");
    const int before = blank.tileCount;
    const int features = static_cast<int>(blank.features.size());
    int written = 0;
    try {
        written = galaxy::stampSection(blank, stamp, 0, 0, nullptr);
    } catch (const std::exception& ex) {
        expect(false, std::string("stamp GreenFlat02: ") + ex.what());
        return;
    }
    const int overlapW = std::min(stamp.tileWidth, blank.tileWidth);
    const int overlapH = std::min(stamp.tileHeight, blank.tileHeight);
    expect(written == overlapW * overlapH, "GreenFlat02 stamp covers the overlap");
    expect(blank.tileCount > before, "GreenFlat02 stamp adds tiles");
    expect(static_cast<int>(blank.features.size()) == features, "GreenFlat02 stamp keeps map features");
    if (!stamp.indices.empty() && !blank.indices.empty()) {
        const std::uint16_t local = stamp.indices[0];
        const std::uint16_t global = blank.indices[0];
        const bool same = local < static_cast<std::uint16_t>(stamp.tileCount)
            && global < static_cast<std::uint16_t>(blank.tileCount)
            && std::memcmp(
                blank.tileImages.data() + static_cast<std::size_t>(global) * 1024,
                stamp.tileImages.data() + static_cast<std::size_t>(local) * 1024,
                1024) == 0;
        expect(same, "placed GreenFlat02 tile matches the section");
    }
    if (!stamp.heights.empty() && !blank.heights.empty()) {
        expect(blank.heights[0] == stamp.heights[0], "placed GreenFlat02 height");
    }
    if (overlapW < blank.tileWidth) {
        expect(blank.heights[static_cast<std::size_t>(overlapW * 2)] == 0, "blank height outside GreenFlat02");
    }
    const int pool = blank.tileCount;
    expect(galaxy::stampSection(blank, stamp, 0, 0, nullptr) == written, "restamping GreenFlat02 covers the same cells");
    expect(blank.tileCount == pool, "restamping GreenFlat02 reuses tile images");
}

std::uint32_t readU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

void expectMapFields(const galaxy::LoadedMap& before, const galaxy::LoadedMap& after, const std::string& label) {
    expect(before.tileWidth == after.tileWidth && before.tileHeight == after.tileHeight, label + " size");
    expect(before.seaLevel == after.seaLevel, label + " sea level");
    expect(before.missionName == after.missionName, label + " mission name");
    expect(before.indices == after.indices, label + " tile indices");
    expect(before.tileImages == after.tileImages, label + " tile images");
    expect(before.heights == after.heights, label + " heights");
    expect(before.featureIndices == after.featureIndices, label + " feature indices");
    expect(before.featurePadding == after.featurePadding, label + " feature padding");
    expect(before.featureNames == after.featureNames, label + " feature names");
    expect(before.features.size() == after.features.size(), label + " features");
    expect(before.starts.size() == after.starts.size(), label + " start count");
    for (std::size_t i = 0; i < before.starts.size() && i < after.starts.size(); ++i) {
        expect(before.starts[i].player == after.starts[i].player
                && before.starts[i].x == after.starts[i].x
                && before.starts[i].y == after.starts[i].y,
            label + " start position");
    }
}

void testSaveRoundTrip(const galaxy::LoadedMap& map, const std::filesystem::path& destination, const std::string& label) {
    galaxy::saveMapFile(map, destination);
    const auto loaded = galaxy::loadMapFile(destination);
    expectMapFields(map, loaded, label);

    if (destination.extension() == ".ufo" || destination.extension() == ".hpi") {
        const galaxy::HpiArchive archive = galaxy::HpiArchive::open(destination);
        const std::string tntPath = "maps/" + destination.stem().string() + ".tnt";
        const galaxy::HpiFile* tntFile = archive.find(tntPath);
        expect(tntFile != nullptr, label + " archive contains " + tntPath);
        if (tntFile != nullptr) {
            const auto tnt = archive.extract(*tntFile);
            expect(tnt.size() >= 48, label + " tnt header");
            if (tnt.size() >= 48) {
                const std::uint32_t minimap = readU32(tnt.data() + 40);
                expect(minimap + 8 <= tnt.size(), label + " minimap pointer");
                if (minimap + 8 <= tnt.size()) {
                    expect(readU32(tnt.data() + minimap) == 252 && readU32(tnt.data() + minimap + 4) == 252, label + " minimap size");
                    if (minimap + 8u + 252u * 252u <= tnt.size()) {
                        // The game reads the picture from its top-left corner:
                        // the playable area fills it from there and only the
                        // right and the bottom are padding.
                        const int playWidth = std::max(1, map.pixelWidth() - 32);
                        const int playHeight = std::max(1, map.pixelHeight() - 128);
                        int shownWidth = 252;
                        int shownHeight = 252;
                        if (playWidth >= playHeight) {
                            shownHeight = std::max(1, playHeight * 252 / playWidth);
                        } else {
                            shownWidth = std::max(1, playWidth * 252 / playHeight);
                        }
                        const std::uint8_t* pixels = tnt.data() + minimap + 8;
                        bool anchored = true;
                        for (int y = 0; y < 252; ++y) {
                            for (int x = 0; x < 252; ++x) {
                                const bool outside = x >= shownWidth || y >= shownHeight;
                                if (outside && pixels[y * 252 + x] != 0x64) {
                                    anchored = false;
                                }
                            }
                        }
                        expect(anchored, label + " minimap is anchored top-left with padding right and below");
                    }
                }
            }
        }
    }

    std::error_code error;
    std::filesystem::remove(destination, error);
    auto sidecar = destination;
    sidecar.replace_extension(".ota");
    std::filesystem::remove(sidecar, error);
}

int countBelow(const galaxy::LoadedMap& map, int height) {
    int count = 0;
    for (std::uint8_t cell : map.heights) {
        if (cell < height) {
            ++count;
        }
    }
    return count;
}

int countAbove(const galaxy::LoadedMap& map, int height) {
    int count = 0;
    for (std::uint8_t cell : map.heights) {
        if (cell > height) {
            ++count;
        }
    }
    return count;
}

int worstBoundaryGap(const galaxy::LoadedMap& map) {
    const int width = map.tileWidth * 2;
    const int height = map.tileHeight * 2;
    if (width <= 0 || height <= 0 || static_cast<int>(map.heights.size()) < width * height) {
        return 0;
    }
    constexpr int step = 32;
    int worst = 0;
    auto gap = [](int left, int right) { return left > right ? left - right : right - left; };
    for (int y = 0; y < height; ++y) {
        for (int x = step; x < width; x += step) {
            const int left = map.heights[static_cast<std::size_t>(y * width + (x - 1))];
            const int right = map.heights[static_cast<std::size_t>(y * width + x)];
            worst = std::max(worst, gap(left, right));
        }
    }
    for (int x = 0; x < width; ++x) {
        for (int y = step; y < height; y += step) {
            const int above = map.heights[static_cast<std::size_t>((y - 1) * width + x)];
            const int below = map.heights[static_cast<std::size_t>(y * width + x)];
            worst = std::max(worst, gap(above, below));
        }
    }
    return worst;
}

void testGenerate(const galaxy::TilesetLibrary& library, const std::filesystem::path& maps) {
    const auto blank = galaxy::loadMapFile(maps / "64x64_BlankMapNoTiles.ufo");
    const auto rocks = galaxy::loadMapFile(maps / "64x64_SimpleFlatMapWithGrass_3Rocks_5Trees.ufo");
    const auto starts = galaxy::loadMapFile(maps / "64x64_SimpleFlatMapWithGrass_With_Player1AndPlayer2StartPositions.ufo");

    galaxy::MapGeneratorSettings flat;
    flat.world = "GreenWorld";
    flat.rivers = 0;
    flat.canyons = 0;
    flat.mountains = 0;
    flat.hills = 0;
    flat.shoreline = 0;
    flat.pools = 0;
    flat.seed = 3;
    auto cleared = rocks;
    galaxy::generateTerrain(cleared, library, flat);
    expect(cleared.tileWidth == 64 && cleared.tileHeight == 64, "generate keeps the map size");
    expect(cleared.seaLevel == rocks.seaLevel, "generate keeps the sea level");
    expect(cleared.features.empty(), "generate clears features");
    expect(!cleared.featureIndices.empty() && cleared.featureIndices.front() == 0xFFFF, "generate clears feature cells");
    bool allGround = true;
    for (std::uint8_t height : cleared.heights) {
        if (height != 85) {
            allGround = false;
        }
    }
    expect(allGround, "zero sliders leave flat ground");

    auto kept = starts;
    galaxy::generateTerrain(kept, library, flat);
    expect(kept.starts.size() == starts.starts.size() && kept.starts[0].x == starts.starts[0].x && kept.starts[1].y == starts.starts[1].y,
        "generate keeps start positions");

    auto canyonMap = blank;
    galaxy::MapGeneratorSettings canyonSettings = flat;
    canyonSettings.canyons = 100;
    canyonSettings.seed = 11;
    galaxy::generateTerrain(canyonMap, library, canyonSettings);
    int canyonHigh = 0;
    int canyonMax = 0;
    for (std::uint8_t height : canyonMap.heights) {
        canyonMax = std::max(canyonMax, static_cast<int>(height));
        if (height > 160) {
            ++canyonHigh;
        }
    }
    expect(canyonHigh > 20, "canyon high cells " + std::to_string(canyonHigh) + " max " + std::to_string(canyonMax));
    expect(countBelow(canyonMap, 100) > 100, "canyons keep low ground beside the cliffs");
    const int canyonGap = worstBoundaryGap(canyonMap);
    expect(canyonGap < 20, "canyon section edges step by " + std::to_string(canyonGap));

    auto mountainMap = blank;
    galaxy::MapGeneratorSettings mountainSettings = flat;
    mountainSettings.mountains = 100;
    mountainSettings.seed = 11;
    galaxy::generateTerrain(mountainMap, library, mountainSettings);
    expect(countAbove(mountainMap, 140) > 10, "mountains raise the ground");

    auto riverMap = blank;
    galaxy::MapGeneratorSettings riverSettings = flat;
    riverSettings.rivers = 100;
    riverSettings.seed = 11;
    galaxy::generateTerrain(riverMap, library, riverSettings);
    expect(countBelow(riverMap, 70) > 30, "rivers lower the ground");
    const int riverGap = worstBoundaryGap(riverMap);
    expect(riverGap < 24, "river section edges step by " + std::to_string(riverGap));

    auto shoreMap = blank;
    galaxy::MapGeneratorSettings shoreSettings = flat;
    shoreSettings.shoreline = 100;
    shoreSettings.seed = 11;
    galaxy::generateTerrain(shoreMap, library, shoreSettings);
    expect(countBelow(shoreMap, 10) > 30, "shoreline adds open water");
    const int shoreGap = worstBoundaryGap(shoreMap);
    expect(shoreGap < 34, "shore section edges step by " + std::to_string(shoreGap));

    auto joined = blank;
    galaxy::MapGeneratorSettings joinedSettings = flat;
    joinedSettings.canyons = 50;
    joinedSettings.shoreline = 40;
    joinedSettings.seed = 11;
    galaxy::generateTerrain(joined, library, joinedSettings);
    const int joinedGap = worstBoundaryGap(joined);
    expect(joinedGap < 20, "cliff and shore edges step by " + std::to_string(joinedGap));

    galaxy::MapGeneratorSettings busy = flat;
    busy.rivers = 70;
    busy.canyons = 70;
    busy.mountains = 40;
    busy.hills = 40;
    busy.shoreline = 30;
    busy.pools = 20;
    busy.seed = 11;
    auto first = blank;
    auto second = blank;
    galaxy::generateTerrain(first, library, busy);
    galaxy::generateTerrain(second, library, busy);
    expect(first.indices == second.indices && first.heights == second.heights, "generate is deterministic for a seed");
    expect(first.missionName == blank.missionName, "generate keeps the mission name");

    galaxy::MapGeneratorSettings other = busy;
    other.seed = 12;
    auto third = blank;
    galaxy::generateTerrain(third, library, other);
    expect(third.indices != first.indices || third.heights != first.heights, "a different seed changes the map");
}

void testAcidFlatGround(const std::filesystem::path& maps) {
    galaxy::TilesetLibrary library;
    library.addArchive(std::filesystem::path(GALAXY_SOURCE_DIR) / "TASect_Acid.hpi");
    auto map = galaxy::loadMapFile(maps / "64x64_BlankMapNoTiles.ufo");
    galaxy::MapGeneratorSettings settings;
    settings.world = "Acid";
    settings.rivers = 0;
    settings.canyons = 0;
    settings.mountains = 0;
    settings.hills = 0;
    settings.shoreline = 0;
    settings.pools = 0;
    settings.seed = 1;
    std::string error;
    try {
        galaxy::generateTerrain(map, library, settings);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    expect(error.empty(), "acid generate: " + error);
    bool acidGround = error.empty() && !map.heights.empty();
    for (std::uint8_t height : map.heights) {
        if (height < 80 || height > 90) {
            acidGround = false;
        }
    }
    expect(acidGround, "acid flat ground uses FlatGr5");
}

void testStampAboveMap() {
    auto bytes = buildSct(3, 1, 2, 2, 11, 10);
    const std::uint32_t tilesAt = 28u + 128u * 128u;
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(tilesAt + 1024u), bytes.begin() + static_cast<std::ptrdiff_t>(tilesAt + 2048u), 20);
    const std::uint32_t dataAt = tilesAt + 2048u;
    bytes[dataAt + 2] = 1;
    const std::uint32_t heightAt = dataAt + 4u;
    for (int sample = 4; sample < 8; ++sample) {
        bytes[heightAt + static_cast<std::uint32_t>(sample) * 4u] = 22;
    }

    const galaxy::SectionStamp stamp = galaxy::loadSectionStamp(bytes);
    galaxy::LoadedMap map = blankMap(4, 100);
    auto change = galaxy::stampSectionChange(map, stamp, 0, -1, nullptr);
    expect(change.has_value() && change->before.width == 1 && change->before.height == 1, "a tile shifted above the map records the visible row");
    expect(map.indices[0] == 1 && map.indices[4] == 0, "the bottom half lands on the top row and the row below stays blank");
    expect(map.tileCount == 2 && map.tileImages[1024] == 20, "the clipped top tile is left out of the map");
    expect(map.heights[0] == 22 && map.heights[1] == 22 && map.heights[8] == 22 && map.heights[9] == 22, "the lower heights land on the map");
    expect(map.heights[16] == 0, "heights below the visible half stay empty");
    galaxy::revertPlacement(map, *change);
    expect(map.indices[0] == 0 && map.heights[0] == 0 && map.tileCount == 1, "undo removes the offset placement");
}

void testWideTilePool() {
    const auto vanillaPath = std::filesystem::temp_directory_path() / "galaxy-vanilla-tiles.ufo";
    galaxy::saveMapFile(galaxy::createBlankMap(2, 2), vanillaPath);
    {
        const galaxy::HpiArchive archive = galaxy::HpiArchive::open(vanillaPath);
        const galaxy::HpiFile* tntFile = archive.find("maps/galaxy-vanilla-tiles.tnt");
        expect(tntFile != nullptr, "vanilla map archive has a tnt");
        if (tntFile != nullptr) {
            const auto tnt = archive.extract(*tntFile);
            expect(readU32(tnt.data()) == galaxy::kTntVersion, "a map within 65536 tiles stays in the vanilla format");
        }
    }
    std::error_code removeError;
    std::filesystem::remove(vanillaPath, removeError);

    galaxy::LoadedMap map = galaxy::createBlankMap(1, 1);
    map.tileCount = galaxy::kVanillaTileSlots;
    map.tileImages.assign(static_cast<std::size_t>(map.tileCount) * 1024, 0);
    const auto stamp = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 40, 167));
    auto change = galaxy::stampSectionChange(map, stamp, 0, 0, nullptr);
    expect(change.has_value(), "a stamp past the vanilla tile limit still places");
    expect(map.tileCount == galaxy::kVanillaTileSlots + 1, "the pool grows past 65536 tiles");
    expect(map.indices[0] == static_cast<std::uint32_t>(galaxy::kVanillaTileSlots), "the new tile uses the next index");
    expect(map.tileImages[static_cast<std::size_t>(galaxy::kVanillaTileSlots) * 1024] == 167, "the new tile image is stored");

    const auto widePath = std::filesystem::temp_directory_path() / "galaxy-wide-tiles.ufo";
    galaxy::saveMapFile(map, widePath);
    const galaxy::HpiArchive archive = galaxy::HpiArchive::open(widePath);
    const galaxy::HpiFile* tntFile = archive.find("maps/galaxy-wide-tiles.tnt");
    expect(tntFile != nullptr, "wide map archive has a tnt");
    if (tntFile != nullptr) {
        const auto tnt = archive.extract(*tntFile);
        expect(readU32(tnt.data()) == galaxy::kTntWideVersion, "maps past 65536 tiles use 32-bit tile indices");
        expect(readU32(tnt.data() + 24) == static_cast<std::uint32_t>(galaxy::kVanillaTileSlots + 1), "wide map stores the full tile count");
        const std::uint32_t ptrData = readU32(tnt.data() + 12);
        expect(readU32(tnt.data() + ptrData) == static_cast<std::uint32_t>(galaxy::kVanillaTileSlots), "wide map stores the new tile index");
    }
    const auto loaded = galaxy::loadMapFile(widePath);
    expect(loaded.tileCount == map.tileCount && loaded.indices[0] == map.indices[0], "wide map reloads the extra tile");
    std::filesystem::remove(widePath, removeError);

    galaxy::revertPlacement(map, *change);
    expect(map.tileCount == galaxy::kVanillaTileSlots && map.indices[0] == 0, "undo removes the tile past the vanilla limit");
}

void testTileOpacity() {
    const auto stamp = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 40, 167));
    galaxy::LoadedMap map = blankMap(4, 100);
    auto change = galaxy::stampSectionChange(map, stamp, 0, 0, nullptr, 50);
    expect(change.has_value(), "half opacity places a tile");
    expect(map.tileCount == 2 && map.indices[0] == 1, "half opacity stores a blended tile");
    expect(map.heights[0] == 20 && map.heights[1] == 20 && map.heights[8] == 20 && map.heights[9] == 20, "half opacity blends the height");

    const int red = (static_cast<int>(galaxy::kTaPalette[100][0]) * 50 + static_cast<int>(galaxy::kTaPalette[167][0]) * 50 + 50) / 100;
    const int green = (static_cast<int>(galaxy::kTaPalette[100][1]) * 50 + static_cast<int>(galaxy::kTaPalette[167][1]) * 50 + 50) / 100;
    const int blue = (static_cast<int>(galaxy::kTaPalette[100][2]) * 50 + static_cast<int>(galaxy::kTaPalette[167][2]) * 50 + 50) / 100;
    int nearest = 0;
    int nearestDistance = 1 << 30;
    for (int index = 0; index < 256; ++index) {
        const int dr = static_cast<int>(galaxy::kTaPalette[index][0]) - red;
        const int dg = static_cast<int>(galaxy::kTaPalette[index][1]) - green;
        const int db = static_cast<int>(galaxy::kTaPalette[index][2]) - blue;
        const int distance = dr * dr + dg * dg + db * db;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    expect(map.tileImages[1024] == static_cast<std::uint8_t>(nearest), "blended pixel uses the nearest palette color");
    expect(nearest != 100 && nearest != 167, "the half mix is a different palette color");

    const int pool = map.tileCount;
    expect(galaxy::stampSection(map, stamp, 1, 0, nullptr, 50) == 1 && map.tileCount == pool && map.indices[1] == 1, "the same mix reuses the blended tile");

    galaxy::LoadedMap faint = blankMap(2, 100);
    galaxy::LoadedMap stronger = blankMap(2, 100);
    expect(galaxy::stampSection(faint, stamp, 0, 0, nullptr, 5) == 1 && faint.heights[0] == 2, "5% opacity keeps most of the old height");
    expect(galaxy::stampSection(stronger, stamp, 0, 0, nullptr, 10) == 1 && stronger.heights[0] == 4, "10% opacity moves the height a little further");

    galaxy::revertPlacement(map, *change);
    expect(map.indices[0] == 0 && map.heights[0] == 0 && map.tileCount == 1, "undo removes the blended tile");
}

void testArchiveFooterAndStarts() {
    const auto path = std::filesystem::temp_directory_path() / "galaxy-copyright.ufo";
    galaxy::LoadedMap map = galaxy::createBlankMap(8, 8);
    map.starts.push_back(galaxy::StartPosition{1, 64, 96});
    map.starts.push_back(galaxy::StartPosition{2, 200, 40});
    galaxy::saveMapFile(map, path);

    {
        std::ifstream in(path, std::ios::binary);
        in.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(in.tellg());
        expect(size >= 36, "saved ufo has a copyright trailer");
        char footer[36] = {};
        if (size >= 36) {
            in.seekg(-36, std::ios::end);
            in.read(footer, 36);
        }
        expect(std::string(footer, footer + 36) == "Copyright 2026 Cavedog Entertainment", "ufo copyright trailer");
    }

    auto loaded = galaxy::loadMapFile(path);
    expect(loaded.starts.size() == 2 && loaded.starts[0].player == 1 && loaded.starts[0].x == 64 && loaded.starts[0].y == 96, "player 1 saved");
    expect(loaded.starts.size() == 2 && loaded.starts[1].player == 2 && loaded.starts[1].x == 200 && loaded.starts[1].y == 40, "player 2 saved");
    loaded.starts.clear();
    loaded.starts.push_back(galaxy::StartPosition{10, 12, 18});
    galaxy::saveMapFile(loaded, path);
    auto again = galaxy::loadMapFile(path);
    expect(again.starts.size() == 1 && again.starts[0].player == 10 && again.starts[0].x == 12 && again.starts[0].y == 18, "updated start saved");
    expect(again.missionName == "Untitled Map", "mission name kept");

    const char* ota =
        "[GlobalHeader]\r\n{\r\n"
        "missionname=Keep Me;\r\n"
        "missiondescription=Keep Description;\r\n"
        "[Schema 0]\r\n{\r\nType=Network 1;\r\n[specials]\r\n{\r\n"
        "[special0]\r\n{\r\nspecialwhat=StartPos1;\r\nXPos=1;\r\nZPos=2;\r\n}\r\n"
        "}\r\n}\r\n"
        "[Schema 1]\r\n{\r\nType=Network 2;\r\n[specials]\r\n{\r\n"
        "[special0]\r\n{\r\nspecialwhat=StartPos2;\r\nXPos=9;\r\nZPos=8;\r\n}\r\n"
        "}\r\n}\r\n}\r\n";
    again.otaText.assign(ota, ota + std::strlen(ota));
    again.starts.clear();
    again.starts.push_back(galaxy::StartPosition{3, 15, 25});
    again.missionName = "Keep Me";
    galaxy::saveMapFile(again, path);
    auto third = galaxy::loadMapFile(path);
    expect(third.starts.size() == 1 && third.starts[0].player == 3 && third.starts[0].x == 15 && third.starts[0].y == 25, "one start after rewrite");
    expect(third.missionName == "Keep Me", "custom mission name kept");
    const galaxy::HpiArchive archive = galaxy::HpiArchive::open(path);
    const galaxy::HpiFile* otaFile = archive.find("maps/galaxy-copyright.ota");
    expect(otaFile != nullptr, "ota packed");
    if (otaFile != nullptr) {
        const auto bytes = archive.extract(*otaFile);
        const std::string text(bytes.begin(), bytes.end());
        expect(text.find("Keep Description") != std::string::npos, "description kept");
        expect(text.find("StartPos2") == std::string::npos, "old schema start removed");
        expect(text.find("StartPos3") != std::string::npos, "new start written");
        expect(text.find("Network 2") != std::string::npos, "second schema kept");
    }
    std::error_code error;
    std::filesystem::remove(path, error);
}

void testBlankMap() {
    bool rejected = false;
    try {
        galaxy::createBlankMap(0, 64);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "blank map rejects a zero size");

    const auto map = galaxy::createBlankMap(32, 48);
    expect(map.tileWidth == 32 && map.tileHeight == 48, "blank map size");
    expect(map.seaLevel == 75, "blank map sea level");
    expect(map.missionName == "Untitled Map", "blank map mission name");
    expect(map.tileCount == 1 && map.tileImages.size() == 1024 && map.tileImages.front() == 0x64, "blank map tile");
    expect(map.indices.size() == 32u * 48u && map.indices.front() == 0, "blank map indices");
    expect(map.heights.size() == 64u * 96u && map.heights.front() == 0, "blank map heights");
    expect(map.featureIndices.size() == map.heights.size() && map.featureIndices.front() == 0xFFFF, "blank map has no features");
    expect(map.features.empty() && map.starts.empty(), "blank map has no placements");
    testSaveRoundTrip(map, std::filesystem::temp_directory_path() / "galaxy-blank-map-test.ufo", "blank map");

    // A tall map: the minimap fills the left part of the picture from the
    // top-left corner and the right part is padding, as in the game's maps.
    auto tall = galaxy::createBlankMap(32, 48);
    tall.tileImages.assign(1024, 5);
    const auto tallPath = std::filesystem::temp_directory_path() / "galaxy-minimap-layout.ufo";
    galaxy::saveMapFile(tall, tallPath);
    {
        const galaxy::HpiArchive archive = galaxy::HpiArchive::open(tallPath);
        const galaxy::HpiFile* tntFile = archive.find("maps/galaxy-minimap-layout.tnt");
        expect(tntFile != nullptr, "minimap layout map has a TNT");
        if (tntFile != nullptr) {
            const auto tnt = archive.extract(*tntFile);
            const std::uint32_t minimap = readU32(tnt.data() + 40);
            const std::uint8_t* pixels = tnt.data() + minimap + 8;
            // 992 x 1408 playable pixels give a picture 177 pixels wide.
            expect(pixels[0] == 5, "minimap top-left is the map");
            expect(pixels[251 * 252 + 0] == 5, "minimap bottom-left is the map");
            expect(pixels[176] == 5 && pixels[177] == 0x64, "minimap pads to the right of the map");
            expect(pixels[251 * 252 + 251] == 0x64, "minimap bottom-right is padding");
        }
    }
    std::error_code removeError;
    std::filesystem::remove(tallPath, removeError);
}

void testPlacementHistory() {
    const auto grass = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 40, 167));
    const auto water = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 10, 9));
    galaxy::LoadedMap map = blankMap(4, 100);
    const auto originalIndices = map.indices;
    const auto originalHeights = map.heights;

    expect(!galaxy::captureTilePatch(map, -2, 0, 1, 1).has_value(), "patch misses the map");
    expect(!galaxy::stampSectionChange(map, grass, -2, 0, nullptr).has_value(), "off-map stamp is not an edit");
    expect(map.tileCount == 1, "off-map stamp keeps the tile pool");

    auto first = galaxy::stampSectionChange(map, grass, 0, 0, nullptr);
    auto second = galaxy::stampSectionChange(map, water, 2, 1, nullptr);
    expect(first.has_value() && second.has_value(), "placements are recorded");
    expect(map.indices[0] == 1 && map.indices[6] == 2, "both placements are visible");
    expect(map.heights[0] == 40 && map.heights[2 * 8 + 4] == 10, "both heights are written");
    expect(map.tileCount == 3 && map.features.size() == 1, "placements append tiles and keep features");

    galaxy::revertPlacement(map, *second);
    expect(map.indices[0] == 1 && map.indices[6] == 0, "undo removes only the later tile");
    expect(map.heights[2 * 8 + 4] == 0 && map.tileCount == 2, "undo restores the later height and tile pool");
    galaxy::revertPlacement(map, *first);
    expect(map.indices == originalIndices && map.heights == originalHeights, "undo restores the blank map");
    expect(map.tileCount == 1 && map.tileImages.size() == 1024 && map.features.size() == 1, "undo drops the added tiles");

    galaxy::reapplyPlacement(map, *first);
    galaxy::reapplyPlacement(map, *second);
    expect(map.indices[0] == 1 && map.indices[6] == 2, "redo restores both tiles");
    expect(map.heights[0] == 40 && map.heights[2 * 8 + 4] == 10, "redo restores both heights");
    expect(map.tileImages[1024] == 167 && map.tileImages[2048] == 9, "redo restores tile images");

    bool rejected = false;
    galaxy::revertPlacement(map, *second);
    try {
        galaxy::revertPlacement(map, *second);
    } catch (const std::exception&) {
        rejected = true;
    }
    expect(rejected, "undo rejects a placement that is no longer the latest");

    galaxy::LoadedMap overlap = blankMap(4, 100);
    auto lower = galaxy::stampSectionChange(overlap, grass, 0, 0, nullptr);
    auto upper = galaxy::stampSectionChange(overlap, water, 0, 0, nullptr);
    expect(lower && upper && overlap.indices[0] == 2 && overlap.heights[0] == 10, "a later stamp covers the earlier one");
    galaxy::revertPlacement(overlap, *upper);
    expect(overlap.indices[0] == 1 && overlap.heights[0] == 40 && overlap.tileCount == 2, "undo uncovers the earlier stamp");
    galaxy::reapplyPlacement(overlap, *upper);
    expect(overlap.indices[0] == 2 && overlap.tileImages[2048] == 9, "redo covers the earlier stamp again");

    galaxy::LoadedMap clipped = blankMap(4, 100);
    const auto wide = galaxy::loadSectionStamp(buildSct(3, 2, 2, 1, 40, 167));
    auto edge = galaxy::stampSectionChange(clipped, wide, 3, 3, nullptr);
    expect(edge && edge->before.width == 1 && edge->before.height == 1, "clipped placement records one cell");
    galaxy::revertPlacement(clipped, *edge);
    expect(clipped.indices[15] == 0 && clipped.heights[6 * 8 + 6] == 0 && clipped.tileCount == 1, "undo removes the clipped placement");

    galaxy::LoadedMap reuse = blankMap(4, 100);
    galaxy::TileDeduper deduper;
    auto placed = galaxy::stampSectionChange(reuse, grass, 0, 0, &deduper);
    expect(placed.has_value(), "deduped placement recorded");
    galaxy::revertPlacement(reuse, *placed);
    deduper = {};
    auto again = galaxy::stampSectionChange(reuse, grass, 1, 1, &deduper);
    std::optional<galaxy::PlacementChange> repeat;
    if (again) {
        repeat = galaxy::stampSectionChange(reuse, grass, 2, 2, &deduper);
    }
    expect(again && repeat && reuse.tileCount == 2 && reuse.indices[1 * 4 + 1] == reuse.indices[2 * 4 + 2], "deduper reuses a tile after undo");
}

void testTerrainClipboard() {
    const auto grass = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 40, 167));
    const auto water = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 10, 9));
    galaxy::LoadedMap map = blankMap(4, 100);
    expect(galaxy::stampSection(map, grass, 1, 1, nullptr) == 1, "clipboard setup grass");
    expect(galaxy::stampSection(map, water, 2, 1, nullptr) == 1, "clipboard setup water");

    const auto copied = galaxy::copyTileRegion(map, 1, 1, 2, 1);
    expect(copied.tileWidth == 2 && copied.tileHeight == 1, "copy size");
    expect(copied.tileCount == 2, "copy keeps both tiles");
    expect(copied.tileImages[0] == 167 && copied.tileImages[1024] == 9, "copy keeps tile colors");
    expect(copied.heights[0] == 40 && copied.heights[2] == 10, "copy keeps heights");
    expect(map.features.size() == 1, "copy leaves features alone");

    const auto block = galaxy::copyTileRegion(map, 0, 0, 3, 2);
    expect(block.tileWidth == 3 && block.tileHeight == 2 && block.tileCount == 3, "copy compacts repeated blank tiles with the stamped ones");
    const int blankCopies = static_cast<int>(std::count(block.indices.begin(), block.indices.end(), static_cast<std::uint16_t>(0)));
    expect(blankCopies == 4, "blank cells share one copied tile");

    const auto edge = galaxy::copyTileRegion(map, 3, 3, 5, 5);
    expect(edge.tileWidth == 1 && edge.tileHeight == 1, "copy clips to the map");
    bool missed = false;
    try {
        galaxy::copyTileRegion(map, 8, 8, 2, 2);
    } catch (const std::exception&) {
        missed = true;
    }
    expect(missed, "copy rejects a region outside the map");

    const auto cleared = galaxy::clearTileRegion(map, 1, 1, 2, 1);
    expect(map.indices[1 * 4 + 1] == 0 && map.indices[1 * 4 + 2] == 0, "cut clears the tiles");
    expect(map.heights[2 * 8 + 2] == 0 && map.heights[2 * 8 + 4] == 0, "cut clears the heights");
    expect(map.tileCount == 3 && map.features.size() == 1, "cut keeps the tile pool and features");
    expect(map.indices[0] == 0 && map.heights[0] == 0, "cut leaves the rest of the map");
    galaxy::revertPlacement(map, cleared);
    expect(map.indices[1 * 4 + 1] == 1 && map.indices[1 * 4 + 2] == 2, "undo cut restores the tiles");
    expect(map.heights[2 * 8 + 2] == 40 && map.heights[2 * 8 + 4] == 10, "undo cut restores the heights");

    auto pasted = galaxy::stampSectionChange(map, copied, 0, 3, nullptr);
    expect(pasted.has_value(), "paste records an edit");
    expect(map.indices[3 * 4] == 1 && map.indices[3 * 4 + 1] == 2, "paste reuses the copied tiles");
    expect(map.heights[6 * 8] == 40 && map.heights[6 * 8 + 2] == 10, "paste writes the copied heights");
    expect(map.tileCount == 3, "paste does not grow the pool when the tiles already exist");
    galaxy::revertPlacement(map, *pasted);
    expect(map.indices[3 * 4] == 0 && map.heights[6 * 8] == 0, "undo paste restores the destination");
}

void testHeightClipboard() {
    const auto grass = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 40, 167));
    const auto water = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 10, 9));
    galaxy::LoadedMap map = blankMap(4, 100);
    expect(galaxy::stampSection(map, grass, 1, 1, nullptr) == 1, "height copy setup grass");
    expect(galaxy::stampSection(map, water, 2, 1, nullptr) == 1, "height copy setup water");
    const int pool = map.tileCount;
    const auto copied = galaxy::copyTileRegion(map, 1, 1, 2, 1, true);
    expect(copied.heightsOnly && copied.tileCount == 0 && copied.tileWidth == 2 && copied.tileHeight == 1, "height copy skips the tiles");
    expect(copied.heights[0] == 40 && copied.heights[2] == 10, "height copy keeps both heights");
    expect(galaxy::sampleHeight(map, 32, 32) == 40 && galaxy::sampleHeight(map, 63, 63) == 40, "hover reads the grass height cell");
    expect(galaxy::sampleHeight(map, 64, 47) == 10 && galaxy::sampleHeight(map, 0, 0) == 0, "hover reads the water cell and the untouched cell");
    expect(galaxy::sampleHeight(map, -1, 0) == -1 && galaxy::sampleHeight(map, 128, 0) == -1, "hover height is empty off the map");

    const std::uint32_t keptTile = map.indices[0];
    const std::uint8_t keptColor = map.tileImages[static_cast<std::size_t>(keptTile) * 1024];
    auto pasted = galaxy::stampSectionChange(map, copied, 0, 0, nullptr);
    expect(pasted.has_value(), "height paste records an edit");
    expect(map.indices[0] == keptTile && map.indices[1] == keptTile, "height paste leaves the tiles");
    expect(map.tileImages[static_cast<std::size_t>(map.indices[0]) * 1024] == keptColor, "height paste leaves the tile picture");
    expect(map.heights[0] == 40 && map.heights[2] == 10, "height paste writes the copied heights");
    expect(map.tileCount == pool, "height paste does not add tiles");
    galaxy::revertPlacement(map, *pasted);
    expect(map.heights[0] == 0 && map.indices[0] == keptTile, "undo height paste restores the old height");

    expect(galaxy::stampSection(map, copied, 0, 0, nullptr, 50) == 2, "partial height paste covers both tiles");
    expect(map.heights[0] == 20 && map.indices[0] == keptTile && map.tileCount == pool, "partial height paste blends height and keeps the tile");
}

void testSelectionMask() {
    const auto added = galaxy::tileSelectionAdd(galaxy::tileSelectionRect(1, 1, 2, 2), 4, 4, 4, 4);
    expect(added.count() == 5 && added.contains(1, 1) && added.contains(4, 4) && !added.contains(3, 3), "shift adds a tile beside the selection");
    const auto removed = galaxy::tileSelectionSubtract(galaxy::tileSelectionRect(1, 1, 3, 1), 2, 1, 2, 1);
    expect(removed.count() == 2 && removed.contains(1, 1) && !removed.contains(2, 1) && removed.contains(3, 1), "shift+ctrl removes a tile from the selection");
    expect(removed.width == 3 && !removed.solid(), "a removed middle tile stays a hole");
    expect(galaxy::tileSelectionSubtract(galaxy::tileSelectionRect(0, 0, 1, 0), 0, 0, 1, 0).empty(), "removing every tile clears the selection");
    expect(galaxy::sameTileSelection(galaxy::TileSelection{}, galaxy::TileSelection{}), "two empty selections match");
    expect(!galaxy::sameTileSelection(removed, galaxy::tileSelectionRect(1, 1, 3, 1)), "a hole does not match the full rectangle");

    const auto grass = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 40, 167));
    const auto water = galaxy::loadSectionStamp(buildSct(3, 1, 1, 1, 10, 9));
    galaxy::LoadedMap map = blankMap(4, 100);
    expect(galaxy::stampSection(map, grass, 1, 1, nullptr) == 1, "selection setup grass");
    expect(galaxy::stampSection(map, water, 3, 1, nullptr) == 1, "selection setup water");
    const auto selection = galaxy::tileSelectionSubtract(galaxy::tileSelectionRect(1, 1, 3, 1), 2, 1, 2, 1);
    const int stride = map.heightWidth();
    const auto heightAt = [&](int tileX, int tileY) {
        return map.heights[static_cast<std::size_t>(tileY * 2) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(tileX * 2)];
    };
    const auto setHeight = [&](int tileX, int tileY, std::uint8_t height) {
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                map.heights[static_cast<std::size_t>(tileY * 2 + dy) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(tileX * 2 + dx)] = height;
            }
        }
    };
    setHeight(2, 1, 7);
    map.indices[1 * 4 + 2] = 1;

    const auto heights = galaxy::copyTileSelection(map, selection, true);
    expect(heights.heightsOnly && heights.tileWidth == 3 && heights.tileMask.size() == 3 && heights.tileMask[1] == 0, "height copy keeps the hole");
    expect(heights.heights[0] == 40 && heights.heights[4] == 10, "height copy reads the tiles on either side");
    setHeight(1, 0, 7);
    expect(galaxy::stampSection(map, heights, 0, 0, nullptr) == 2, "height paste skips the hole");
    expect(heightAt(0, 0) == 40 && heightAt(1, 0) == 7 && heightAt(2, 0) == 10, "the hole keeps the height already on the map");
    expect(map.indices[0] == 0 && map.indices[1] == 0 && map.indices[2] == 0, "height paste leaves the tiles");

    const auto terrain = galaxy::copyTileSelection(map, selection, false);
    expect(terrain.tileMask.size() == 3 && terrain.tileMask[0] == 1 && terrain.tileMask[1] == 0 && terrain.tileCount == 2, "terrain copy skips the hole");
    expect(galaxy::stampSection(map, terrain, 0, 2, nullptr) == 2, "terrain paste skips the hole");
    expect(map.indices[2 * 4] == 1 && map.indices[2 * 4 + 1] == 0 && map.indices[2 * 4 + 2] == 2, "terrain paste writes only the selected tiles");

    const auto cleared = galaxy::clearTileSelection(map, selection);
    expect(heightAt(1, 1) == 0 && heightAt(3, 1) == 0 && heightAt(2, 1) == 7, "cut clears selected tiles and leaves the hole");
    expect(map.indices[1 * 4 + 1] == 0 && map.indices[1 * 4 + 2] == 1 && map.indices[1 * 4 + 3] == 0, "cut leaves the tile in the hole");
    galaxy::revertPlacement(map, cleared);
    expect(heightAt(1, 1) == 40 && heightAt(3, 1) == 10 && heightAt(2, 1) == 7, "undo cut restores the selected tiles and the hole");
    expect(map.indices[1 * 4 + 2] == 1, "undo cut restores the tile that was not selected");
}

void testAdjustSelectionHeight() {
    galaxy::LoadedMap map = blankMap(4, 100);
    const int stride = map.heightWidth();
    const auto cell = [&](int tileX, int tileY, int dx, int dy) -> std::uint8_t& {
        return map.heights[static_cast<std::size_t>(tileY * 2 + dy) * static_cast<std::size_t>(stride) + static_cast<std::size_t>(tileX * 2 + dx)];
    };
    cell(1, 1, 0, 0) = 10;
    cell(1, 1, 1, 0) = 20;
    cell(1, 1, 0, 1) = 30;
    cell(1, 1, 1, 1) = 40;
    cell(2, 1, 0, 0) = 10;
    cell(3, 1, 0, 0) = 20;
    cell(3, 1, 1, 0) = 20;
    cell(3, 1, 0, 1) = 20;
    cell(3, 1, 1, 1) = 20;
    const std::uint32_t keptIndex = map.indices[1 * map.tileWidth + 1];
    const auto selection = galaxy::tileSelectionSubtract(galaxy::tileSelectionRect(1, 1, 3, 1), 2, 1, 2, 1);

    expect(!galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Add, 0).has_value(), "adding zero leaves the height alone");
    expect(cell(1, 1, 0, 0) == 10, "adding zero does not write");

    auto added = galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Add, 85);
    expect(added.has_value(), "adding height records an edit");
    expect(cell(1, 1, 0, 0) == 95 && cell(1, 1, 1, 0) == 105 && cell(1, 1, 0, 1) == 115 && cell(1, 1, 1, 1) == 125, "add moves every sample on the tile");
    expect(cell(3, 1, 0, 0) == 105 && cell(2, 1, 0, 0) == 10 && cell(0, 0, 0, 0) == 0, "add skips the hole and tiles outside the selection");
    expect(map.indices[1 * map.tileWidth + 1] == keptIndex && map.tileCount == 1, "a height edit leaves the tile picture");
    galaxy::revertPlacement(map, *added);
    expect(cell(1, 1, 0, 0) == 10 && cell(1, 1, 1, 1) == 40 && cell(3, 1, 0, 0) == 20, "undo restores the old height");

    auto set = galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Set, 85);
    expect(set.has_value() && cell(1, 1, 0, 0) == 85 && cell(1, 1, 1, 1) == 85 && cell(3, 1, 1, 1) == 85, "set writes 85 on the selected tiles");
    expect(cell(2, 1, 0, 0) == 10, "set leaves the hole");
    auto lowered = galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Subtract, 10);
    expect(lowered.has_value() && cell(1, 1, 0, 0) == 75 && cell(3, 1, 0, 0) == 75 && cell(2, 1, 0, 0) == 10, "subtract removes 10 from the selected height");
    auto topped = galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Add, 200);
    expect(topped.has_value() && cell(1, 1, 0, 0) == 255 && cell(3, 1, 0, 0) == 255, "add stops at 255");
    auto bottomed = galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Subtract, 300);
    expect(bottomed.has_value() && cell(1, 1, 1, 1) == 0 && cell(3, 1, 1, 1) == 0 && cell(2, 1, 0, 0) == 10, "subtract stops at 0");
    expect(!galaxy::adjustSelectionHeight(map, selection, galaxy::HeightAdjust::Set, 0).has_value(), "setting the current height changes nothing");
}

void testBlendSelectionTextures() {
    galaxy::LoadedMap map = blankMap(4, 100);
    map.tileImages.assign(2048, 1);
    std::fill(map.tileImages.begin() + 1024, map.tileImages.end(), static_cast<std::uint8_t>(2));
    map.tileCount = 2;
    map.indices[0] = 0;
    map.indices[1] = 1;
    map.heights[0] = 40;

    map.indices[2] = 1;
    const auto apart = galaxy::tileSelectionSubtract(galaxy::tileSelectionRect(0, 0, 2, 0), 1, 0, 1, 0);
    expect(!galaxy::blendSelectionTextures(map, apart).has_value(), "tiles that do not touch are not blended");
    map.indices[2] = 0;

    galaxy::LoadedMap same = map;
    same.indices[1] = 0;
    expect(!galaxy::blendSelectionTextures(same, galaxy::tileSelectionRect(0, 0, 1, 0)).has_value(), "matching textures stay as they are");
    expect(same.tileCount == 2, "matching textures do not add tiles");

    auto blended = galaxy::blendSelectionTextures(map, galaxy::tileSelectionRect(0, 0, 1, 0));
    expect(blended.has_value(), "touching tiles blend");
    expect(map.tileCount == 4, "each side of the seam gets a new tile");
    const std::uint8_t* left = map.tileImages.data() + static_cast<std::size_t>(map.indices[0]) * 1024;
    const std::uint8_t* right = map.tileImages.data() + static_cast<std::size_t>(map.indices[1]) * 1024;
    expect(left[0] == 1 && right[31] == 2, "the outer edges keep their own texture");
    expect(left[31] != 1 && right[0] != 2 && left[31] == right[0], "the shared edge fades to the same mix");
    expect(map.tileImages[0] == 1 && map.tileImages[31] == 1, "the original left tile is left intact");
    expect(map.tileImages[1024] == 2 && map.tileImages[1024 + 31] == 2, "the original right tile is left intact");
    expect(map.indices[2] == 0 && map.heights[0] == 40, "blend leaves other tiles and the height alone");
    galaxy::revertPlacement(map, *blended);
    expect(map.indices[0] == 0 && map.indices[1] == 1 && map.tileCount == 2, "undo restores the tiles before the blend");
    expect(map.tileImages[0] == 1 && map.tileImages[1024] == 2, "undo removes the blended pictures");
}

void testBackupNames() {
    expect(galaxy::backupFilename("MyMap", 10, 23, 2026, 19, 6) == "MyMap-10-23-2026-19-06-Backup.ufo", "backup filename");
    int month = 0;
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    expect(galaxy::isBackupFilename("MyMap", "MyMap-10-23-2026-19-06-Backup.ufo", month, day, year, hour, minute), "backup name matches");
    expect(month == 10 && day == 23 && year == 2026 && hour == 19 && minute == 6, "backup timestamp");
    expect(!galaxy::isBackupFilename("MyMap", "MyMap.ufo", month, day, year, hour, minute), "the saved map is not a backup");
    expect(!galaxy::isBackupFilename("MyMap", "Other-10-23-2026-19-06-Backup.ufo", month, day, year, hour, minute), "another map's backup is separate");
    expect(galaxy::isBackupFilename("Rivers-10", "Rivers-10-10-23-2026-19-06-Backup.ufo", month, day, year, hour, minute)
            && month == 10 && minute == 6,
        "a hyphen in the map name stays with the name");

    const std::vector<std::string> names = {
        "MyMap.ufo",
        "Other-01-01-2026-00-00-Backup.ufo",
        "MyMap-12-31-2025-23-59-Backup.ufo",
        "MyMap-01-01-2026-00-00-Backup.ufo",
        "MyMap-01-02-2026-00-00-Backup.ufo",
        "MyMap-01-03-2026-00-00-Backup.ufo",
        "MyMap-01-04-2026-00-00-Backup.ufo",
    };
    const auto removeOne = galaxy::backupNamesToDelete(names, "MyMap", 5);
    expect(removeOne.size() == 1 && removeOne[0] == "MyMap-12-31-2025-23-59-Backup.ufo", "the sixth backup removes the oldest");
    const auto removeDown = galaxy::backupNamesToDelete(names, "MyMap", 2);
    expect(removeDown.size() == 4 && removeDown.front() == "MyMap-12-31-2025-23-59-Backup.ufo" && removeDown.back() == "MyMap-01-03-2026-00-00-Backup.ufo",
        "a smaller backlog removes oldest backups until one slot is free");
    expect(galaxy::backupNamesToDelete(names, "MyMap", 6).empty(), "fewer backups than the limit removes nothing");
}

void testAcidFamily(const std::filesystem::path& maps) {
    expect(galaxy::tilesetFamilyKey("Acid") == "acid", "acid family key");
    expect(galaxy::tilesetFamilyKey("Acid_New_3") == "acid", "acid new family key");
    expect(galaxy::tilesetFamilyKey("GreenWorld") == "greenworld", "green family key stays whole");
    expect(galaxy::tilesetFamilyKey("Something_newer") == "something_newer", "newer is not a pack suffix");

    galaxy::TilesetLibrary library;
    const char* packs[] = {"TASect_Acid.hpi", "TASect_Acid_New_1.hpi", "TASect_Acid_New_2.hpi", "TASect_Acid_New_3.hpi"};
    for (const char* pack : packs) {
        const auto path = std::filesystem::path(GALAXY_SOURCE_DIR) / pack;
        expect(std::filesystem::exists(path), std::string(pack) + " is present");
        library.addArchive(path);
    }
    auto map = galaxy::loadMapFile(maps / "64x64_BlankMapNoTiles.ufo");
    galaxy::MapGeneratorSettings settings;
    settings.world = "Acid";
    settings.rivers = 0;
    settings.canyons = 0;
    settings.mountains = 0;
    settings.hills = 0;
    settings.shoreline = 100;
    settings.pools = 0;
    settings.seed = 11;
    std::string error;
    try {
        galaxy::generateTerrain(map, library, settings);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    expect(error.empty(), "acid shoreline generate: " + error);
    const int water = countBelow(map, 10);
    expect(water > 30, "acid shoreline adds open water " + std::to_string(water));
    const int shoreGap = worstBoundaryGap(map);
    expect(shoreGap < 36, "acid shore edges step by " + std::to_string(shoreGap));

    auto fromPack = galaxy::loadMapFile(maps / "64x64_BlankMapNoTiles.ufo");
    settings.world = "Acid_New_3";
    settings.shoreline = 0;
    error.clear();
    try {
        galaxy::generateTerrain(fromPack, library, settings);
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    expect(error.empty(), "acid pack generate: " + error);
    bool acidGround = error.empty() && !fromPack.heights.empty();
    for (std::uint8_t height : fromPack.heights) {
        if (height < 80 || height > 90) {
            acidGround = false;
        }
    }
    expect(acidGround, "an acid pack uses the shared flat ground");
}

void testFeaturePlacement() {
    const char* tdf = R"([Tree1]
{
    // comment
    world = GreenWorld;
    category = Trees;
    footprintx = 2;
    footprintz = nope;
    filename = trees;
    seqname = tree1;
    object = atree;
    [damage]
    {
        world=ignored;
        category=nope;
        filename=hidden;
    }
}
/* [Ghost]
{
    world=no;
}
*/
[Metal]
{
    world=Lava;
    category=Metal;
    metal=80;
    energy=15;
}
[Broken]
{
    footprintx=0;
}
)";
    const auto parsed = galaxy::parseFeatureTdf(tdf);
    expect(parsed.size() == 3, "tdf feature count");
    if (parsed.size() == 3) {
        expect(parsed[0].name == "Tree1", "tdf tree name");
        expect(parsed[0].world == "GreenWorld" && parsed[0].category == "Trees", "tdf tree world");
        expect(parsed[0].footprintX == 2 && parsed[0].footprintZ == 1, "tdf tree footprint");
        expect(parsed[0].animFile == "trees" && parsed[0].sequence == "tree1" && parsed[0].object == "atree", "tdf tree picture names");
        expect(parsed[1].name == "Metal" && parsed[1].world == "Lava" && parsed[1].category == "Metal", "tdf metal");
        expect(parsed[1].metal == 80 && parsed[1].energy == 15, "tdf metal value");
        expect(parsed[1].footprintX == 1 && parsed[1].footprintZ == 1, "tdf metal footprint");
        expect(parsed[2].name == "Broken" && parsed[2].world.empty() && parsed[2].footprintX == 1, "tdf bad footprint");
    }

    galaxy::LoadedMap map = galaxy::createBlankMap(2, 2);
    const auto heights = map.heights;
    const auto tiles = map.indices;
    expect(galaxy::placeMapFeature(map, -1, 0, "Tree1") == galaxy::FeatureEdit::Unchanged, "feature place rejects x");
    expect(galaxy::placeMapFeature(map, 0, 99, "Tree1") == galaxy::FeatureEdit::Unchanged, "feature place rejects y");
    expect(galaxy::placeMapFeature(map, 1, 2, "  ") == galaxy::FeatureEdit::Unchanged, "feature place rejects a blank name");
    expect(galaxy::placeMapFeature(map, 1, 2, "Tree1") == galaxy::FeatureEdit::Placed, "place Tree1");
    expect(map.featureNames.size() == 1 && map.featureNames[0] == "Tree1", "feature name table");
    expect(map.featureIndices[2 * 4 + 1] == 0, "Tree1 cell index");
    expect(map.features.size() == 1 && map.features[0].x == 1 && map.features[0].y == 2 && map.features[0].name == "Tree1", "Tree1 marker");
    expect(galaxy::placeMapFeature(map, 1, 2, "Tree1") == galaxy::FeatureEdit::Unchanged, "placing the same feature again");
    expect(map.featureNames.size() == 1, "same feature does not grow the name table");
    expect(galaxy::placeMapFeature(map, 1, 2, "Rock") == galaxy::FeatureEdit::Placed, "replace Tree1 with Rock");
    expect(map.featureNames.size() == 2 && map.featureIndices[2 * 4 + 1] == 1, "Rock uses the next name");
    expect(galaxy::placeMapFeature(map, 3, 3, "Tree1") == galaxy::FeatureEdit::Placed, "reuse Tree1");
    expect(map.featureNames.size() == 2 && map.featureIndices[3 * 4 + 3] == 0, "Tree1 name is reused");
    expect(map.features.size() == 2, "two feature markers");
    if (map.features.size() == 2) {
        expect(map.features[0].name == "Rock" && map.features[0].x == 1 && map.features[0].y == 2, "first marker is Rock");
        expect(map.features[1].name == "Tree1" && map.features[1].x == 3 && map.features[1].y == 3, "second marker is Tree1");
    }
    expect(map.heights == heights && map.indices == tiles, "features leave tiles and heights alone");
    testSaveRoundTrip(map, std::filesystem::temp_directory_path() / "galaxy-features.ufo", "feature grid");

    expect(galaxy::removeMapFeature(map, 1, 2) == galaxy::FeatureEdit::Removed, "remove Rock");
    expect(map.featureIndices[2 * 4 + 1] == 0xFFFF, "removed cell is empty");
    expect(map.featureNames.size() == 2, "removing a feature keeps its name");
    expect(map.features.size() == 1 && map.features[0].name == "Tree1", "Tree1 marker remains");
    expect(galaxy::removeMapFeature(map, 1, 2) == galaxy::FeatureEdit::Unchanged, "remove an empty cell");

    galaxy::LoadedMap hole = galaxy::createBlankMap(2, 2);
    hole.featureIndices[0] = 0xFFFC;
    expect(galaxy::placeMapFeature(hole, 0, 0, "Tree1") == galaxy::FeatureEdit::OpenSpace, "open space rejects a feature");
    expect(hole.featureIndices[0] == 0xFFFC && hole.featureNames.empty(), "open space stays empty");
    expect(galaxy::removeMapFeature(hole, 0, 0) == galaxy::FeatureEdit::OpenSpace, "open space is not a feature");

    galaxy::LoadedMap full = galaxy::createBlankMap(1, 1);
    full.featureNames.resize(0xFFFC);
    full.featureNames[3] = "Tree1";
    expect(galaxy::placeMapFeature(full, 0, 0, "Rock") == galaxy::FeatureEdit::Full, "feature name table is full");
    expect(full.featureIndices[0] == 0xFFFF, "a full table does not place a new name");
    expect(galaxy::placeMapFeature(full, 0, 0, "Tree1") == galaxy::FeatureEdit::Placed, "a known name still places");
    expect(full.featureIndices[0] == 3 && full.features.size() == 1 && full.features[0].name == "Tree1", "known name reuses its slot");

    galaxy::LoadedMap named = galaxy::createBlankMap(1, 1);
    expect(galaxy::placeMapFeature(named, 0, 0, std::string(200, 'Q')) == galaxy::FeatureEdit::Placed, "long feature name places");
    expect(named.featureNames.size() == 1 && named.featureNames[0] == std::string(127, 'Q'), "feature name keeps 127 characters");
    testSaveRoundTrip(named, std::filesystem::temp_directory_path() / "galaxy-feature-name.ufo", "feature name");

    const auto archivePath = std::filesystem::temp_directory_path() / "galaxy-feature-catalog.hpi";
    const std::string tree = "[Tree1]\n{\nworld=GreenWorld;\ncategory=Trees;\nfootprintx=2;\nfootprintz=3;\n}\n";
    const std::string rock = "[Rock]\n{\ncategory=Rocks;\n}\n";
    galaxy::HpiWriteFile treeFile;
    treeFile.path = "features/Somewhere/Trees/Tree1.tdf";
    treeFile.bytes.assign(tree.begin(), tree.end());
    galaxy::HpiWriteFile rockFile;
    rockFile.path = "features/Lava/Rocks/Rock.tdf";
    rockFile.bytes.assign(rock.begin(), rock.end());
    galaxy::HpiWriteFile junk;
    junk.path = "features/bad.tdf";
    junk.bytes = {1, 2, 3, 4};
    galaxy::writeHpiArchive(archivePath, {treeFile, rockFile, junk});
    galaxy::TilesetLibrary library;
    library.addArchive(archivePath);
    library.addArchive(archivePath);
    expect(library.archives().size() == 1, "feature archive loads once");
    expect(library.sectionCount() == 0, "feature archive has no sections");
    const galaxy::FeatureInfo* treeInfo = nullptr;
    const galaxy::FeatureInfo* rockInfo = nullptr;
    for (const galaxy::FeatureInfo& record : library.archives().front().features) {
        if (record.name == "Tree1") {
            treeInfo = &record;
        } else if (record.name == "Rock") {
            rockInfo = &record;
        }
    }
    expect(library.archives().front().features.size() == 2, "feature archive records");
    expect(treeInfo != nullptr && treeInfo->world == "GreenWorld" && treeInfo->category == "Trees", "tdf world wins over the folder");
    expect(treeInfo != nullptr && treeInfo->footprintX == 2 && treeInfo->footprintZ == 3, "archive footprint");
    expect(rockInfo != nullptr && rockInfo->world == "Lava" && rockInfo->category == "Rocks", "folder supplies a missing world");
    std::error_code removeError;
    std::filesystem::remove(archivePath, removeError);
}

void testFeatureBatchPaste() {
    // Pasting several features is one undo step: the cells go back in reverse
    // order, so the names the paste added leave the table from its end.
    auto map = galaxy::createBlankMap(4, 4);
    const std::vector<std::pair<std::pair<int, int>, std::string>> clip = {
        {{0, 0}, "MetalZone"}, {{3, 1}, "TreeA"}, {{5, 2}, "MetalZone"}, {{1, 4}, "TreeB"}};
    std::vector<galaxy::FeatureChange> changes;
    for (const auto& item : clip) {
        galaxy::FeatureChange change;
        expect(galaxy::placeMapFeature(map, 2 + item.first.first, 1 + item.first.second, item.second, &change) == galaxy::FeatureEdit::Placed,
            "batch paste places each feature");
        changes.push_back(change);
    }
    expect(map.features.size() == 4, "batch paste keeps every position");
    int kept = 0;
    for (const auto& item : clip) {
        for (const auto& feature : map.features) {
            if (feature.x == 2 + item.first.first && feature.y == 1 + item.first.second && feature.name == item.second) {
                ++kept;
            }
        }
    }
    expect(kept == 4, "batch paste keeps the offsets between features");
    const auto afterPaste = map.featureIndices;
    const auto namesAfterPaste = map.featureNames;
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
        galaxy::revertFeatureChange(map, *it);
    }
    expect(map.features.empty() && map.featureNames.empty(), "batch undo removes the features and their new names");
    for (const auto& change : changes) {
        galaxy::reapplyFeatureChange(map, change);
    }
    expect(map.featureIndices == afterPaste && map.featureNames == namesAfterPaste, "batch redo restores the paste");
}

void testFeatureUndo() {
    galaxy::LoadedMap map = galaxy::createBlankMap(2, 2);
    const auto heights = map.heights;
    const auto tiles = map.indices;
    galaxy::FeatureChange first;
    expect(galaxy::placeMapFeature(map, 1, 2, "Tree1", &first) == galaxy::FeatureEdit::Placed, "undo setup places Tree1");
    expect(first.addedName == "Tree1" && first.before == 0xFFFF && first.after == 0, "first feature records the new name");
    galaxy::FeatureChange second;
    expect(galaxy::placeMapFeature(map, 3, 3, "Tree1", &second) == galaxy::FeatureEdit::Placed, "undo setup reuses Tree1");
    expect(second.addedName.empty() && second.after == 0, "reused feature adds no name");
    galaxy::FeatureChange replaced;
    expect(galaxy::placeMapFeature(map, 1, 2, "Rock", &replaced) == galaxy::FeatureEdit::Placed, "undo setup replaces Tree1");
    expect(replaced.addedName == "Rock" && replaced.before == 0 && replaced.after == 1, "replacement records Rock");

    galaxy::revertFeatureChange(map, replaced);
    expect(map.featureIndices[2 * 4 + 1] == 0 && map.featureNames.size() == 1 && map.featureNames[0] == "Tree1", "undo replace restores Tree1");
    expect(map.features.size() == 2 && map.features[0].name == "Tree1" && map.features[1].name == "Tree1", "undo replace restores both markers");
    galaxy::revertFeatureChange(map, second);
    expect(map.featureIndices[3 * 4 + 3] == 0xFFFF && map.featureNames.size() == 1 && map.features.size() == 1, "undo reused feature clears that cell");
    galaxy::revertFeatureChange(map, first);
    expect(map.featureNames.empty() && map.features.empty() && map.featureIndices[2 * 4 + 1] == 0xFFFF, "undo first feature removes its name");

    galaxy::reapplyFeatureChange(map, first);
    galaxy::reapplyFeatureChange(map, second);
    galaxy::reapplyFeatureChange(map, replaced);
    expect(map.featureNames.size() == 2 && map.featureNames[0] == "Tree1" && map.featureNames[1] == "Rock", "redo restores the feature names");
    expect(map.featureIndices[2 * 4 + 1] == 1 && map.featureIndices[3 * 4 + 3] == 0, "redo restores both cells");
    expect(map.features.size() == 2 && map.features[0].name == "Rock" && map.features[1].name == "Tree1", "redo restores the markers");

    galaxy::FeatureChange removed;
    expect(galaxy::removeMapFeature(map, 3, 3, &removed) == galaxy::FeatureEdit::Removed, "remove for undo");
    expect(removed.before == 0 && removed.after == 0xFFFF && removed.addedName.empty(), "remove records the old index");
    galaxy::revertFeatureChange(map, removed);
    expect(map.featureIndices[3 * 4 + 3] == 0 && map.features.size() == 2 && map.featureNames.size() == 2, "undo remove puts the feature back");
    galaxy::reapplyFeatureChange(map, removed);
    expect(map.featureIndices[3 * 4 + 3] == 0xFFFF && map.features.size() == 1 && map.featureNames.size() == 2, "redo remove clears the cell and keeps the name");
    expect(map.heights == heights && map.indices == tiles, "feature undo leaves tiles and heights alone");
}

void appendU8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    appendU16(out, static_cast<std::uint16_t>(value & 0xffff));
    appendU16(out, static_cast<std::uint16_t>(value >> 16));
}

void appendI16(std::vector<std::uint8_t>& out, std::int16_t value) {
    appendU16(out, static_cast<std::uint16_t>(value));
}

std::vector<std::uint8_t> sampleGaf(const std::string& name, int width, int height, int hotX, int hotY, std::uint8_t transparency, bool compressed, const std::vector<std::uint8_t>& image) {
    std::vector<std::uint8_t> out;
    appendU32(out, 0x00010100);
    appendU32(out, 1);
    appendU32(out, 0);
    appendU32(out, 16);
    appendU16(out, 1);
    appendU16(out, 0);
    appendU32(out, 0);
    out.insert(out.end(), name.begin(), name.end());
    out.resize(56, 0);
    appendU32(out, 64);
    appendU32(out, 0);
    appendU16(out, static_cast<std::uint16_t>(width));
    appendU16(out, static_cast<std::uint16_t>(height));
    appendI16(out, static_cast<std::int16_t>(hotX));
    appendI16(out, static_cast<std::int16_t>(hotY));
    appendU8(out, transparency);
    appendU8(out, compressed ? 1 : 0);
    appendU16(out, 0);
    appendU32(out, 0);
    appendU32(out, 88);
    appendU32(out, 0);
    out.insert(out.end(), image.begin(), image.end());
    return out;
}

void testGafImages() {
    const std::vector<std::uint8_t> plain = sampleGaf("metal", 2, 2, 1, 3, 9, false, {1, 2, 9, 4});
    const auto image = galaxy::readGafSequence(plain.data(), plain.size(), "METAL");
    expect(image.has_value(), "uncompressed gaf frame");
    if (image) {
        expect(image->width == 2 && image->height == 2, "uncompressed gaf size");
        expect(image->hotspotX == 1 && image->hotspotY == 3, "uncompressed gaf hotspot");
        expect(image->transparency == 9, "uncompressed gaf transparency");
        expect(image->pixels == std::vector<std::uint8_t>({1, 2, 9, 4}), "uncompressed gaf pixels");
        const auto rgba = galaxy::gafImageRgba(*image);
        expect(rgba.size() == 16 && rgba[11] == 0 && rgba[15] == 255, "gaf transparency is clear");
    }
    expect(!galaxy::readGafSequence(plain.data(), plain.size(), "rock").has_value(), "missing gaf sequence");

    const std::vector<std::uint8_t> compressed = sampleGaf("row", 4, 1, 0, 0, 9, true, {3, 0, 0x05, 0x06, 0x05});
    const auto row = galaxy::readGafSequence(compressed.data(), compressed.size(), "row");
    expect(row.has_value(), "compressed gaf frame");
    if (row) {
        expect(row->pixels == std::vector<std::uint8_t>({9, 9, 5, 5}), "compressed gaf pixels");
    }

    const std::filesystem::path totala = "/Volumes/ExtraStorage/TotalAnnihilation/totala1.hpi";
    if (!std::filesystem::exists(totala)) {
        return;
    }
    bool found = false;
    std::string detail = "no metal sprite";
    try {
        const galaxy::HpiArchive archive = galaxy::HpiArchive::open(totala);
        for (const galaxy::HpiFile& file : archive.files()) {
            std::string path = file.path;
            std::replace(path.begin(), path.end(), '\\', '/');
            for (char& character : path) {
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            if (path.rfind("features/", 0) != 0 || path.size() < 4 || path.substr(path.size() - 4) != ".tdf" || path.find("metal") == std::string::npos) {
                continue;
            }
            const auto textBytes = archive.extract(file);
            const std::string text(reinterpret_cast<const char*>(textBytes.data()), textBytes.size());
            for (const galaxy::FeatureInfo& feature : galaxy::parseFeatureTdf(text)) {
                if (feature.animFile.empty() || feature.sequence.empty() || feature.metal <= 0) {
                    continue;
                }
                const galaxy::HpiFile* gaf = archive.find("anims/" + feature.animFile + ".gaf");
                if (gaf == nullptr) {
                    detail = feature.name + " missing " + feature.animFile;
                    continue;
                }
                const auto gafBytes = archive.extract(*gaf);
                const auto sprite = galaxy::readGafSequence(gafBytes.data(), gafBytes.size(), feature.sequence);
                if (!sprite) {
                    detail = feature.name + " sequence " + feature.sequence + " did not decode";
                    continue;
                }
                int opaque = 0;
                for (std::uint8_t pixel : sprite->pixels) {
                    if (pixel != sprite->transparency) {
                        ++opaque;
                    }
                }
                detail = feature.name + " " + std::to_string(sprite->width) + "x" + std::to_string(sprite->height) + " opaque " + std::to_string(opaque) + " metal " + std::to_string(feature.metal);
                expect(sprite->width > 0 && sprite->height > 0 && opaque > 8, "game sprite pixels " + detail);
                found = opaque > 8;
                break;
            }
            if (found) {
                break;
            }
        }
    } catch (const std::exception& ex) {
        detail = ex.what();
    }
    expect(found, "totala1 metal sprite " + detail);
}

}  // namespace

int countKey(std::string_view text, std::string_view key) {
    std::string lower;
    lower.reserve(text.size());
    for (char character : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    int count = 0;
    for (std::size_t from = 0; (from = lower.find(key, from)) != std::string::npos; from += key.size()) {
        ++count;
    }
    return count;
}

void testMapSettings() {
    auto fresh = galaxy::createBlankMap(64, 64);
    const galaxy::MapSettings defaults = galaxy::readMapSettings(fresh);
    expect(defaults.seaLevel == 75, "new map sea level");
    expect(defaults.name == "Untitled Map", "new map name");
    expect(defaults.description == "4 x 4 Map", "new map description");
    expect(defaults.players == "2" && defaults.memory == "16 mb", "new map notes");
    expect(defaults.aiProfile == "DEFAULT", "new map ai");
    expect(defaults.minWind == 0 && defaults.maxWind == 3000, "new map wind");
    expect(defaults.tidalStrength == 20 && defaults.solarStrength == 20 && defaults.gravity == 112, "new map world");
    expect(defaults.surfaceMetal == 3 && defaults.mohoMetal == 30, "new map metal");
    expect(defaults.humanMetal == 1000 && defaults.humanEnergy == 1000, "new map resources");
    expect(static_cast<int>(defaults.killMultiplier) == 50 && !defaults.lavaWorld, "new map rules");

    galaxy::MapSettings edited = defaults;
    edited.name = "Harbor";
    edited.description = "A wet test";
    edited.planet = "Ice";
    edited.players = "2, 4";
    edited.memory = "32 mb";
    edited.aiProfile = "SeaBattle";
    edited.minWind = 100;
    edited.maxWind = 2500;
    edited.tidalStrength = 35;
    edited.solarStrength = 18;
    edited.gravity = 90;
    edited.seaLevel = 60;
    edited.surfaceMetal = 7;
    edited.mohoMetal = 40;
    edited.lavaWorld = true;
    edited.waterDoesDamage = true;
    edited.waterDamage = 12;
    edited.waterBlocksShots = true;
    edited.humanMetal = 500;
    edited.humanEnergy = 750;
    edited.computerMetal = 1500;
    edited.computerEnergy = 900;
    edited.meteorWeapon = "Meteor";
    edited.meteorRadius = 8;
    edited.meteorDensity = 0.25;
    edited.meteorDuration = 30;
    edited.meteorInterval = 15;
    edited.lineOfSight = true;
    edited.killMultiplier = 2.5;
    edited.maxUnits = 250;
    galaxy::applyMapSettings(fresh, edited);
    const auto saved = std::filesystem::temp_directory_path() / "galaxy-map-settings.ufo";
    galaxy::saveMapFile(fresh, saved);
    const galaxy::LoadedMap loadedMap = galaxy::loadMapFile(saved);
    const galaxy::MapSettings loaded = galaxy::readMapSettings(loadedMap);
    expect(loadedMap.seaLevel == 60 && loaded.seaLevel == 60, "saved sea level");
    expect(loaded.name == "Harbor" && loadedMap.missionName == "Harbor", "saved name");
    expect(loaded.description == "A wet test" && loaded.planet == "Ice", "saved description");
    expect(loaded.players == "2, 4" && loaded.memory == "32 mb", "saved notes");
    expect(loaded.aiProfile == "SeaBattle", "saved ai profile");
    expect(loaded.minWind == 100 && loaded.maxWind == 2500 && loaded.tidalStrength == 35, "saved wind and tide");
    expect(loaded.solarStrength == 18 && loaded.gravity == 90, "saved solar and gravity");
    expect(loaded.surfaceMetal == 7 && loaded.mohoMetal == 40, "saved metal");
    expect(loaded.lavaWorld && loaded.waterDoesDamage && loaded.waterDamage == 12 && loaded.waterBlocksShots, "saved water");
    expect(loaded.humanMetal == 500 && loaded.humanEnergy == 750 && loaded.computerMetal == 1500 && loaded.computerEnergy == 900, "saved resources");
    expect(loaded.meteorWeapon == "Meteor" && loaded.meteorRadius == 8 && loaded.meteorDuration == 30 && loaded.meteorInterval == 15, "saved meteors");
    expect(static_cast<int>(loaded.meteorDensity * 100.0 + 0.5) == 25, "saved meteor density");
    expect(loaded.lineOfSight && static_cast<int>(loaded.killMultiplier * 10.0 + 0.5) == 25 && loaded.maxUnits == 250, "saved rules");

    const char* original =
        "[GlobalHeader]\r\n{\r\n"
        "missionname=Keep Me;\r\n"
        "glamour=shot;\r\n"
        "TidalStrength=4;\r\n"
        "killmul=50;\r\n"
        "[Schema 0]\r\n{\r\n"
        "Type=Network 1;\r\n"
        "aiprofile=DEFAULT;\r\n"
        "SurfaceMetal=3;\r\n"
        "MohoMetal=30;\r\n"
        "[specials]\r\n{\r\n"
        "[special0]\r\n{\r\n"
        "specialwhat=StartPos1;\r\n"
        "XPos=10;\r\n"
        "ZPos=20;\r\n"
        "}\r\n}\r\n}\r\n"
        "[Schema 1]\r\n{\r\n"
        "Type=Network 2;\r\n"
        "aiprofile=Metal;\r\n"
        "SurfaceMetal=9;\r\n"
        "customnote=leave-me;\r\n"
        "}\r\n}\r\n";
    auto custom = galaxy::createBlankMap(8, 8);
    custom.otaText.assign(original, original + std::strlen(original));
    custom.starts.push_back(galaxy::StartPosition{1, 10, 20});
    galaxy::MapSettings customSettings = galaxy::readMapSettings(custom);
    expect(customSettings.name == "Keep Me" && customSettings.glamour == "shot", "reads name and glamour");
    expect(customSettings.tidalStrength == 4 && customSettings.aiProfile == "DEFAULT" && customSettings.surfaceMetal == 3, "reads schema 0");
    customSettings.name = "New Name";
    customSettings.tidalStrength = 40;
    customSettings.surfaceMetal = 12;
    customSettings.aiProfile = "SeaBattle";
    customSettings.mohoMetal = 44;
    galaxy::applyMapSettings(custom, customSettings);
    galaxy::applyMapSettings(custom, galaxy::readMapSettings(custom));
    const std::string rewritten(custom.otaText.begin(), custom.otaText.end());
    expect(rewritten.find("glamour=shot;") != std::string::npos, "glamour stays");
    expect(rewritten.find("customnote=leave-me;") != std::string::npos, "unknown schema key stays");
    expect(rewritten.find("specialwhat=StartPos1;") != std::string::npos, "start stays in the ota");
    expect(rewritten.find("TidalStrength=40;") != std::string::npos, "wind key keeps its spelling");
    expect(countKey(rewritten, "tidalstrength=") == 1, "tide key is not duplicated");
    expect(countKey(rewritten, "surfacemetal=") == 2, "both variants get surface metal");
    expect(countKey(rewritten, "mohometal=") == 2, "moho is added once to the second variant");
    expect(countKey(rewritten, "aiprofile=") == 2, "both variants get the ai profile");
    const galaxy::MapSettings again = galaxy::readMapSettings(custom);
    expect(again.name == "New Name" && again.tidalStrength == 40 && again.surfaceMetal == 12, "rewritten settings read back");
    expect(again.aiProfile == "SeaBattle" && again.mohoMetal == 44 && again.glamour == "shot", "profile and glamour read back");
    galaxy::saveMapFile(custom, std::filesystem::temp_directory_path() / "galaxy-map-settings-custom.ufo");
    const galaxy::LoadedMap roundTrip = galaxy::loadMapFile(std::filesystem::temp_directory_path() / "galaxy-map-settings-custom.ufo");
    const std::string savedText(roundTrip.otaText.begin(), roundTrip.otaText.end());
    expect(roundTrip.starts.size() == 1 && roundTrip.starts[0].x == 10 && roundTrip.starts[0].y == 20, "start survives save");
    expect(savedText.find("customnote=leave-me;") != std::string::npos, "unknown key survives save");
    expect(galaxy::readMapSettings(roundTrip).aiProfile == "SeaBattle", "saved profile");
}

void testEditorSettings() {
    const auto path = std::filesystem::temp_directory_path() / "galaxy-editor-settings.json";
    std::filesystem::remove(path);
    const galaxy::EditorSettings missing = galaxy::loadEditorSettings(path);
    expect(missing.previewSize == 128 && missing.heightGridSize == 32, "missing settings use defaults");
    expect(missing.sidebarFontPercent == 100 && missing.autosaveMinutes == 10 && missing.autosaveBackups == 5, "missing autosave defaults");

    galaxy::EditorSettings settings;
    settings.previewSize = 96;
    settings.heightGridSize = 48;
    settings.sidebarFontPercent = 125;
    settings.autosaveMinutes = 3;
    settings.autosaveBackups = 7;
    galaxy::saveEditorSettings(path, settings);
    const galaxy::EditorSettings loaded = galaxy::loadEditorSettings(path);
    expect(loaded.previewSize == 96 && loaded.heightGridSize == 48, "settings preview and grid");
    expect(loaded.sidebarFontPercent == 125 && loaded.autosaveMinutes == 3 && loaded.autosaveBackups == 7, "settings font and autosave");

    settings.previewSize = 9999;
    settings.heightGridSize = 50;
    settings.sidebarFontPercent = 10;
    settings.autosaveMinutes = 0;
    settings.autosaveBackups = 100;
    galaxy::saveEditorSettings(path, settings);
    const galaxy::EditorSettings clamped = galaxy::loadEditorSettings(path);
    expect(clamped.previewSize == 256 && clamped.heightGridSize == 48, "settings are clamped");
    expect(clamped.sidebarFontPercent == 75 && clamped.autosaveMinutes == 1 && clamped.autosaveBackups == 20, "font and autosave are clamped");

    {
        std::ofstream broken(path, std::ios::trunc);
        broken << "{ not json";
    }
    const galaxy::EditorSettings fallback = galaxy::loadEditorSettings(path);
    expect(fallback.previewSize == 128 && fallback.autosaveMinutes == 10, "broken settings use defaults");
    std::filesystem::remove(path);
    expect(!galaxy::editorSettingsPath().empty(), "settings path is set");
}

void testLanguage() {
    expect(galaxy::languageCount() == 15, "fifteen locales ship with the editor");
    expect(std::string(galaxy::languageAt(0).id) == "en", "English is the first locale");
    expect(std::string(galaxy::languageAt(1).nativeName) == "简体中文", "Chinese is named in Chinese");
    expect(std::string(galaxy::languageAt(7).nativeName) == "한국어", "Korean is named in Korean");
    expect(!galaxy::isKnownLanguage("") && !galaxy::isKnownLanguage("de") && !galaxy::isKnownLanguage("french"), "an empty or unknown id is not a saved language");
    expect(galaxy::isKnownLanguage("zh-CN") && galaxy::isKnownLanguage("nb"), "shipped ids are recognized");

    galaxy::setActiveLanguage("fr");
    expect(std::string(galaxy::activeLanguage()) == "fr", "French becomes the active language");
    expect(std::string(galaxy::text(galaxy::Text::MenuFile)) == "Fichier", "the File menu is translated");
    expect(std::string(galaxy::text(galaxy::Text::Undo)) == "Défaire", "French undo is not the same word as cancel");
    expect(std::string(galaxy::text(galaxy::Text::Cancel)) == "Annuler", "French cancel stays Annuler");
    galaxy::setActiveLanguage("nope");
    expect(std::string(galaxy::activeLanguage()) == "fr", "an unknown id leaves the active language alone");
    expect(std::string(galaxy::textIn("zz", galaxy::Text::MenuFile)) == "File", "an unknown locale falls back to English");
    expect(std::string(galaxy::textIn("pl", galaxy::Text::Cancel)) == "Anuluj", "a named locale can be read without activating it");

    galaxy::setActiveLanguage("pl");
    expect(galaxy::formatText(galaxy::Text::SplashCount, {"3", "10"}) == "3 z 10", "format tokens follow the translation");
    for (int language = 0; language < galaxy::languageCount(); ++language) {
        const char* id = galaxy::languageAt(language).id;
        for (int row = 0; row < static_cast<int>(galaxy::Text::Count); ++row) {
            const char* line = galaxy::textIn(id, static_cast<galaxy::Text>(row));
            if (line == nullptr || line[0] == '\0') {
                expect(false, std::string("missing translation ") + id + " row " + std::to_string(row));
            }
        }
    }
    const std::string& corpus = galaxy::translationCorpus();
    expect(corpus.find("简体中文") != std::string::npos && corpus.find("한국어") != std::string::npos && corpus.find("Беларуская") != std::string::npos, "the font corpus covers the picker names");

    const auto path = std::filesystem::temp_directory_path() / "galaxy-editor-language-test.json";
    std::filesystem::remove(path);
    expect(galaxy::loadEditorSettings(path).language.empty(), "a missing settings file has no language yet");
    galaxy::EditorSettings settings;
    settings.language = "zh-CN";
    settings.previewSize = 64;
    galaxy::saveEditorSettings(path, settings);
    const galaxy::EditorSettings loaded = galaxy::loadEditorSettings(path);
    expect(loaded.language == "zh-CN" && loaded.previewSize == 64, "the language is stored with the other settings");
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{ \"previewSize\": 80 }";
    }
    const galaxy::EditorSettings older = galaxy::loadEditorSettings(path);
    expect(older.language.empty() && older.previewSize == 80, "a settings file from before languages has no language key");
    {
        std::ofstream out(path, std::ios::trunc);
        out << "{ \"language\": \"french\", \"previewSize\": 96 }";
    }
    const galaxy::EditorSettings unknown = galaxy::loadEditorSettings(path);
    expect(unknown.language == "french" && !galaxy::isKnownLanguage(unknown.language), "an unrecognized saved language is kept and still counts as unset");
    std::filesystem::remove(path);
    galaxy::setActiveLanguage("en");
}

void testVersion() {
    const std::string version = galaxy::versionString();
    int numbers = 0;
    bool shapeOk = !version.empty() && std::isdigit(static_cast<unsigned char>(version.front())) != 0 && std::isdigit(static_cast<unsigned char>(version.back())) != 0;
    for (const char c : version) {
        if (c == '.') {
            ++numbers;
        } else if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            shapeOk = false;
        }
    }
    expect(shapeOk && numbers == 2, "version looks like 1.0.0, got " + version);
    std::ifstream versionFile(std::filesystem::path(GALAXY_SOURCE_DIR) / "VERSION");
    std::string fileVersion;
    std::getline(versionFile, fileVersion);
    expect(fileVersion == version, "the compiled-in version matches the VERSION file");
    expect(galaxy::applicationTitle() == "Galaxy Editor v" + version, "the title is the name followed by v and the version");
}

void testAppearanceSettings() {
    const auto settingsPath = std::filesystem::temp_directory_path() / "galaxy-editor-appearance-test.json";
    galaxy::EditorSettings saved;
    expect(saved.appearance == 0, "appearance defaults to following the system");
    saved.appearance = 2;
    galaxy::saveEditorSettings(settingsPath, saved);
    expect(galaxy::loadEditorSettings(settingsPath).appearance == 2, "appearance survives a save and load");
    saved.appearance = 1;
    galaxy::saveEditorSettings(settingsPath, saved);
    expect(galaxy::loadEditorSettings(settingsPath).appearance == 1, "light appearance survives a save and load");
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"appearance\": 9 }";
    }
    expect(galaxy::loadEditorSettings(settingsPath).appearance == 2, "an out-of-range appearance is clamped");
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"previewSize\": 64 }";
    }
    expect(galaxy::loadEditorSettings(settingsPath).appearance == 0, "a settings file from before dark mode follows the system");
    std::error_code ignored;
    std::filesystem::remove(settingsPath, ignored);
}

void testSeaSettings() {
    const auto settingsPath = std::filesystem::temp_directory_path() / "galaxy-editor-sea-test.json";
    galaxy::EditorSettings saved;
    expect(saved.seaOpacity > 0 && saved.seaOpacity < 100, "the sea is translucent by default");
    saved.seaOpacity = 72;
    saved.seaColor = 0x12AB34;
    galaxy::saveEditorSettings(settingsPath, saved);
    const galaxy::EditorSettings loaded = galaxy::loadEditorSettings(settingsPath);
    expect(loaded.seaOpacity == 72, "sea opacity survives a save and load");
    expect(loaded.seaColor == 0x12AB34, "sea color survives a save and load");
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"seaOpacity\": 400, \"seaColor\": -5 }";
    }
    const galaxy::EditorSettings clamped = galaxy::loadEditorSettings(settingsPath);
    expect(clamped.seaOpacity == 100, "an out-of-range sea opacity is clamped");
    expect(clamped.seaColor == 0, "an out-of-range sea color is clamped");
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"previewSize\": 64 }";
    }
    expect(galaxy::loadEditorSettings(settingsPath).seaColor == galaxy::EditorSettings().seaColor, "a settings file from before 3D mode gets the default sea");
    std::error_code ignored;
    std::filesystem::remove(settingsPath, ignored);
}

void testOrbitCamera() {
    const auto close = [](float a, float b, float tolerance = 0.01f) { return std::fabs(a - b) <= tolerance; };
    const auto project = [](const galaxy::OrbitCamera& camera, float x, float y, float z, float out[3]) {
        float matrix[16];
        camera.viewProjection(1.5f, matrix);
        const float cx = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
        const float cy = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
        const float cz = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];
        const float cw = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
        out[0] = cx / cw;
        out[1] = cy / cw;
        out[2] = cz / cw;
        return cw;
    };

    galaxy::OrbitCamera camera;
    camera.frame(2048.0f, 1024.0f);
    expect(close(camera.targetX, 1024.0f) && close(camera.targetZ, 512.0f), "framing looks at the middle of the map");
    float eye[3];
    camera.eye(eye);
    expect(eye[1] > 0.0f && eye[2] > camera.targetZ && close(eye[0], camera.targetX), "the camera starts south of the map, looking north");

    float ndc[3];
    float w = project(camera, camera.targetX, camera.targetY, camera.targetZ, ndc);
    expect(w > 0.0f && close(ndc[0], 0.0f) && close(ndc[1], 0.0f), "the target sits in the middle of the view");
    expect(ndc[2] > -1.0f && ndc[2] < 1.0f, "the target is inside the depth range");
    w = project(camera, camera.targetX + 300.0f, 0.0f, camera.targetZ, ndc);
    expect(w > 0.0f && ndc[0] > 0.0f, "east of the target appears on the right");
    w = project(camera, camera.targetX, 0.0f, camera.targetZ - 300.0f, ndc);
    expect(w > 0.0f && ndc[1] > 0.0f, "north of the target appears higher on the screen");
    w = project(camera, camera.targetX, 200.0f, camera.targetZ, ndc);
    expect(w > 0.0f && ndc[1] > 0.0f, "higher ground appears higher on the screen");
    for (const float corner : {0.0f, 2048.0f}) {
        project(camera, corner, 0.0f, 0.0f, ndc);
        expect(ndc[2] > -1.0f && ndc[2] < 1.0f, "the map corners are inside the depth range");
    }

    const float yawBefore = camera.yaw;
    camera.orbit(100.0f, 0.0f);
    expect(camera.yaw != yawBefore, "dragging sideways turns the camera");
    camera.orbit(0.0f, 100000.0f);
    expect(camera.pitch <= galaxy::OrbitCamera::kMaxPitch, "the camera cannot go past straight down");
    camera.orbit(0.0f, -100000.0f);
    expect(camera.pitch >= galaxy::OrbitCamera::kMinPitch, "the camera cannot go below the ground");

    camera.frame(2048.0f, 1024.0f);
    const float distance = camera.distance;
    camera.zoom(1.0f);
    expect(camera.distance < distance, "scrolling forward moves closer");
    camera.zoom(-1.0f);
    expect(close(camera.distance, distance, 0.5f), "scrolling back returns to the same distance");
    for (int i = 0; i < 200; ++i) {
        camera.zoom(5.0f);
    }
    expect(camera.distance > 0.0f, "zooming in stops before the target");
    for (int i = 0; i < 200; ++i) {
        camera.zoom(-5.0f);
    }
    expect(camera.distance < camera.reach * 10.0f, "zooming out stops");

    camera.frame(2048.0f, 1024.0f);
    camera.move(1.0f, 0.0f, 0.5f, false);
    expect(camera.targetZ < 512.0f && close(camera.targetX, 1024.0f), "forward moves the target north when facing north");
    camera.frame(2048.0f, 1024.0f);
    camera.move(0.0f, 1.0f, 0.5f, false);
    expect(camera.targetX > 1024.0f && close(camera.targetZ, 512.0f), "right moves the target east when facing north");
    camera.frame(2048.0f, 1024.0f);
    camera.turn(1.5707963f);
    camera.move(1.0f, 0.0f, 0.5f, false);
    expect(camera.targetX < 1024.0f && close(camera.targetZ, 512.0f, 0.5f), "forward follows the camera after turning");

    camera.frame(2048.0f, 1024.0f);
    camera.pan(50.0f, 0.0f, 800.0f);
    expect(camera.targetX < 1024.0f, "dragging right slides the ground to the right");
    camera.frame(2048.0f, 1024.0f);
    camera.pan(0.0f, -50.0f, 800.0f);
    expect(camera.targetZ > 512.0f, "dragging up slides the ground up");
    camera.targetX = -500.0f;
    camera.targetZ = 99999.0f;
    camera.keepOver(2048.0f, 1024.0f);
    expect(camera.targetX == 0.0f && camera.targetZ == 1024.0f, "the target stays over the map");
}

void testGameFolder() {
    const auto directory = std::filesystem::temp_directory_path() / "galaxy-game-folder-test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory / "nested");
    for (const char* name : {"totala1.hpi", "Rev31.GP3", "ccdata.ccx", "extras.gpf", "readme.txt", "map.ufo"}) {
        std::ofstream(directory / name) << "x";
    }
    std::ofstream(directory / "nested" / "inner.hpi") << "x";

    const auto found = galaxy::TilesetLibrary::archivePathsIn(directory);
    expect(found.size() == 4, "the game folder scan finds the four archive types and nothing else");
    expect(!found.empty() && found.front().filename() == "ccdata.ccx" && found.back().filename() == "totala1.hpi", "archives come back in name order");
    for (const auto& path : found) {
        expect(path.parent_path() == directory, "the scan stays in the game folder: " + path.string());
    }
    expect(galaxy::TilesetLibrary::archivePathsIn(directory / "missing").empty(), "a missing folder has no archives");
    expect(galaxy::TilesetLibrary::archivePathsIn({}).empty(), "an unset folder has no archives");

    {
        const auto game = directory / "ta";
        const auto program = directory / "program";
        std::filesystem::create_directories(game);
        std::filesystem::create_directories(program);
        for (const char* name : {"worlds.hpi", "totala1.hpi"}) {
            std::ofstream(game / name) << "x";
        }
        for (const char* name : {"WORLDS.HPI", "TotalA1.hpi", "MyTiles.hpi", "readme.txt"}) {
            std::ofstream(program / name) << "x";
        }
        const auto both = galaxy::TilesetLibrary::archivePathsFor(game, program);
        expect(both.size() == 3, "program-folder archives that repeat a game archive name are skipped");
        expect(both.size() == 3 && both[0].parent_path() == game && both[1].parent_path() == game, "the game folder's archives come first");
        expect(both.size() == 3 && both[2] == program / "MyTiles.hpi", "a user's own archive beside the program still loads");
        expect(galaxy::TilesetLibrary::archivePathsFor({}, program).size() == 3, "with no game folder every program-folder archive loads");
        expect(galaxy::TilesetLibrary::archivePathsFor(game, {}).size() == 2, "with no program folder only the game folder loads");
        expect(galaxy::TilesetLibrary::archivePathsFor(game, game).size() == 2, "the same folder twice loads each archive once");
        expect(galaxy::TilesetLibrary::archivePathsFor({}, {}).empty(), "no folders means no archives");
        expect(!galaxy::programDirectory().empty(), "the program folder is known");
        expect(galaxy::folderBesideAppBundle("/Applications/Galaxy Editor/GalaxyEditor.app/Contents/MacOS") == "/Applications/Galaxy Editor",
            "an app bundle uses the folder that holds the .app");
        expect(galaxy::folderBesideAppBundle("/Users/me/build/GalaxyEditor.app/Contents/MacOS") == "/Users/me/build", "a bundle in a build folder uses the build folder");
        expect(galaxy::folderBesideAppBundle("/Users/me/build") == "/Users/me/build", "a plain executable folder is unchanged");
        expect(galaxy::folderBesideAppBundle("/Users/me/Contents/MacOS") == "/Users/me/Contents/MacOS", "a Contents/MacOS folder outside an .app is unchanged");
    }

    galaxy::TilesetLibrary library;
    expect(library.archives().empty(), "an empty library has no archives");
    library.clear();
    expect(library.archives().empty(), "clearing an empty library is fine");

    galaxy::EditorSettings fresh;
    expect(fresh.gameFolder.empty() && !fresh.gameFolderAsked, "the game folder starts unset and unasked");
    expect(galaxy::shouldAskForGameFolder(fresh), "an unset, unasked folder is asked about");
    fresh.gameFolderAsked = true;
    expect(!galaxy::shouldAskForGameFolder(fresh), "a folder that was skipped is not asked about again");
    fresh.gameFolder = directory;
    expect(!galaxy::shouldAskForGameFolder(fresh), "a saved folder that exists is not asked about");
    fresh.gameFolder = directory / "gone";
    expect(galaxy::shouldAskForGameFolder(fresh), "a saved folder that no longer exists is asked about");

    const auto settingsPath = std::filesystem::temp_directory_path() / "galaxy-game-folder-settings.json";
    galaxy::EditorSettings saved;
    saved.gameFolder = galaxy::pathFromUtf8("C:\\Games\\Total \"Annihilation\"\\caf\xC3\xA9");
    saved.gameFolderAsked = true;
    galaxy::saveEditorSettings(settingsPath, saved);
    const galaxy::EditorSettings loaded = galaxy::loadEditorSettings(settingsPath);
    expect(galaxy::pathToUtf8(loaded.gameFolder) == "C:\\Games\\Total \"Annihilation\"\\caf\xC3\xA9", "backslashes, quotes, and accents survive a save and load");
    expect(loaded.gameFolderAsked, "the asked flag survives a save and load");
    expect(loaded.previewSize == saved.previewSize && loaded.appearance == saved.appearance, "other settings are unchanged by the game folder");

    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"gameFolder\": \"/Games/TA\\u0020Folder\", \"gameFolderAsked\": false }";
    }
    const galaxy::EditorSettings escaped = galaxy::loadEditorSettings(settingsPath);
    expect(galaxy::pathToUtf8(escaped.gameFolder) == "/Games/TA Folder" && !escaped.gameFolderAsked, "a hand-written unicode escape and a false flag are read");
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"gameFolder\": \"/Games/unterminated }";
    }
    expect(galaxy::loadEditorSettings(settingsPath).gameFolder.empty(), "an unterminated folder string is ignored");
    {
        std::ofstream out(settingsPath, std::ios::trunc);
        out << "{ \"previewSize\": 64 }";
    }
    const galaxy::EditorSettings older = galaxy::loadEditorSettings(settingsPath);
    expect(older.gameFolder.empty() && !older.gameFolderAsked, "a settings file from before the game folder leaves it unset");

    std::filesystem::remove(settingsPath, ignored);
    std::filesystem::remove_all(directory, ignored);
}

void testFavorites() {
    using namespace galaxy;
    expect(isFavoritesArchiveName("GE-Favorites-FirstOne.hpi"), "favorites archive name");
    expect(isFavoritesArchiveName("ge-favorites-two words.HPI"), "favorites archive name ignores case");
    expect(!isFavoritesArchiveName("GE-Favorites-.hpi"), "favorites archive needs a name");
    expect(!isFavoritesArchiveName("GE-Favorites-One.ccx"), "favorites archive is an hpi");
    expect(!isFavoritesArchiveName("Acid_New_1.hpi"), "tileset is not a favorites archive");
    expect(!isFavoritesArchiveName("My-GE-Favorites-One.hpi"), "prefix must start the name");
    expect(favoritesArchiveFilename("  FirstOne ") == "GE-Favorites-FirstOne.hpi", "favorites file name");
    expect(favoritesArchiveName("GE-Favorites-FirstOne.hpi") == "FirstOne", "favorites archive name part");
    expect(favoriteTileFileName("  Cliff.sct ") == "Cliff", "file name drops a trailing sct");
    expect(favoriteTileFileName("Cliff.SCT") == "Cliff", "file name extension ignores case");
    expect(favoriteTileFileName("  Rivers  ") == "Rivers", "file name is trimmed");
    expect(favoriteTileFileName(".sct").empty(), "only an extension is not a name");
    expect(favoriteTileFileName("Cliff.sct.bak") == "Cliff.sct.bak", "a different extension stays");
    expect(favoritesNameProblem("Rivers").empty(), "plain name is fine");
    expect(favoritesNameProblem("Sm Lakes").empty(), "name with a space is fine");
    expect(!favoritesNameProblem("   ").empty(), "blank name is refused");
    expect(!favoritesNameProblem("a/b").empty(), "slash is refused");
    expect(!favoritesNameProblem("a:b").empty(), "colon is refused");
    expect(!favoritesNameProblem(std::string(65, 'x')).empty(), "long name is refused");
    expect(!favoritesNameProblem(".hidden").empty(), "leading period is refused");

    // A 3 x 2 section with three tile pictures, a gradient of heights, and a varied grid.
    SectionStamp stamp;
    stamp.tileWidth = 3;
    stamp.tileHeight = 2;
    stamp.tileCount = 3;
    stamp.indices = {0, 1, 2, 2, 1, 0};
    stamp.tileImages.resize(3 * 1024);
    for (std::size_t i = 0; i < stamp.tileImages.size(); ++i) {
        stamp.tileImages[i] = static_cast<std::uint8_t>(i / 1024 * 60 + 10 + (i % 7));
    }
    stamp.heights.resize(static_cast<std::size_t>(3 * 2 * 2 * 2));
    for (std::size_t i = 0; i < stamp.heights.size(); ++i) {
        stamp.heights[i] = static_cast<std::uint8_t>(i * 5);
    }
    stamp.tileMask = {1, 0, 1, 1, 1, 1};

    const std::vector<std::uint8_t> bytes = writeSectionStamp(stamp);
    const SectionStamp back = loadSectionStamp(bytes);
    expect(back.version == 3, "written section is version 3");
    expect(back.tileWidth == 3 && back.tileHeight == 2 && back.tileCount == 3, "written section size");
    expect(back.indices == stamp.indices, "written section tile grid");
    expect(back.tileImages == stamp.tileImages, "written section tile pictures");
    expect(back.heights == stamp.heights, "written section heights");
    expect(back.minimap.size() == 128u * 128u, "written section minimap size");
    expect(back.minimap == buildSectionMinimap(stamp), "written section minimap matches");
    // 96 x 64 pixels fit into 128 wide: the picture is 128 x 85 in the top-left corner.
    expect(back.minimap[0] == stamp.tileImages[0], "minimap starts at the first tile");
    expect(back.minimap[84 * 128 + 127] != 0 || back.minimap[84 * 128 + 126] != 0, "minimap fills its proportions");
    expect(back.minimap[100 * 128] == 0, "minimap is padded below a wide section");

    SectionStamp heightsOnly = stamp;
    heightsOnly.heightsOnly = true;
    bool refused = false;
    try {
        writeSectionStamp(heightsOnly);
    } catch (const std::exception&) {
        refused = true;
    }
    expect(refused, "heights-only stamp cannot be written");

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "GE-Favorites-GalaxyTest.hpi";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    createFavoritesArchive(path);
    TilesetArchive blank = openTilesetArchive(path);
    expect(blank.sections.empty(), "new favorites archive holds no tiles");
    bool overwrote = true;
    try {
        createFavoritesArchive(path);
    } catch (const std::exception&) {
        overwrote = false;
    }
    expect(!overwrote, "creating a favorites archive does not replace one");

    expect(addFavoriteSection(path, "Mine", "Rivers", stamp) == "Rivers01", "first tile name");
    expect(addFavoriteSection(path, "Mine", "Rivers", stamp) == "Rivers02", "second tile name");
    expect(addFavoriteSection(path, "mine", "rivers", stamp) == "rivers03", "names match without regard to case");
    expect(addFavoriteSection(path, "Mine", "Sm Lakes", stamp) == "Sm Lakes01", "other subcategory numbers from one");
    expect(addFavoriteSection(path, "Other", "Rivers", stamp) == "Rivers01", "other category numbers from one");

    TilesetLibrary library;
    const std::size_t at = library.refreshArchive(path);
    expect(at == 0 && library.archives().size() == 1, "refresh adds a new archive");
    expect(library.sectionCount() == 5, "five favorites listed");
    const auto counts = library.archives()[0].categoryCounts();
    expect(counts.at("Mine").at("Rivers") == 2 && counts.at("mine").at("rivers") == 1, "favorites keep their folders");
    expect(counts.at("Mine").at("Sm Lakes") == 1 && counts.at("Other").at("Rivers") == 1, "favorites categories");
    const TilesetArchive& listed = library.archives()[0];
    const HpiFile* file = listed.archive().find("sections/Mine/Sm Lakes/Sm Lakes01.sct");
    expect(file != nullptr, "favorite is stored at sections/category/subcategory/name.sct");
    if (file != nullptr) {
        expect(loadSectionStamp(listed.archive().extract(*file)).indices == stamp.indices, "stored favorite reads back");
    }
    expect(nextFavoriteSectionName(path, "Mine", "Ponds") == "Ponds01", "next name starts at one");
    expect(nextFavoriteSectionName(path, "Mine", "Rivers") == "Rivers04", "next name skips names in use");
    expect(nextFavoriteSectionName(path, "bad/name", "Ponds").empty(), "next name needs a usable category");

    removeFavoriteSection(path, "sections/Mine/Rivers/Rivers01.sct");
    expect(library.refreshArchive(path) == 0 && library.sectionCount() == 4, "refresh replaces the archive in place");
    expect(library.archives()[0].archive().find("sections/Mine/Rivers/Rivers02.sct") != nullptr, "other tiles stay");
    expect(addFavoriteSection(path, "Mine", "Rivers", stamp) == "Rivers01", "a freed name is reused");

    expect(addFavoriteSection(path, "Mine", "Rivers", stamp, "  Cliff.sct ") == "Cliff", "custom tile name");
    expect(openTilesetArchive(path).archive().find("sections/Mine/Rivers/Cliff.sct") != nullptr, "custom name is the file name");
    bool duplicateName = false;
    try {
        addFavoriteSection(path, "mine", "rivers", stamp, "cliff");
    } catch (const std::exception&) {
        duplicateName = true;
    }
    expect(duplicateName, "a file name already in that subcategory is refused");
    expect(addFavoriteSection(path, "Mine", "Cliffs", stamp, "Rivers01") == "Rivers01", "custom name can differ from the subcategory");
    expect(addFavoriteSection(path, "Mine", "Cliffs", stamp) == "Cliffs01", "an empty file name is still numbered");
    expect(addFavoriteSection(path, "Mine", "Rivers", stamp, "   ") == "Rivers04", "a blank file name is numbered");
    bool badName = false;
    try {
        addFavoriteSection(path, "Mine", "Rivers", stamp, "no/slash");
    } catch (const std::exception&) {
        badName = true;
    }
    expect(badName, "a file name with a slash is refused");
    expect(openTilesetArchive(path).archive().find("sections/Mine/Rivers/no/slash.sct") == nullptr, "a refused name is not written");

    expect(renameTilesetSection(path, "sections/Mine/Rivers/Cliff.sct", "  Water.sct ") == "Water", "a tile can be renamed");
    {
        const TilesetArchive renamed = openTilesetArchive(path);
        const HpiFile* water = renamed.archive().find("sections/Mine/Rivers/Water.sct");
        expect(water != nullptr && water->path.find("Water.sct") != std::string::npos, "renamed tile uses the new file name");
        expect(renamed.archive().find("sections/Mine/Rivers/Cliff.sct") == nullptr, "the old file name is gone");
        expect(renamed.archive().find("GE-Favorites.txt") != nullptr, "renaming one tile leaves the other files");
        if (water != nullptr) {
            expect(loadSectionStamp(renamed.archive().extract(*water)).indices == stamp.indices, "a renamed tile keeps its pictures");
        }
    }
    bool renamedOnto = false;
    try {
        renameTilesetSection(path, "sections/Mine/Rivers/Water.sct", "Rivers02");
    } catch (const std::exception&) {
        renamedOnto = true;
    }
    expect(renamedOnto, "renaming onto a file already there is refused");
    expect(openTilesetArchive(path).archive().find("sections/Mine/Rivers/Water.sct") != nullptr, "a refused rename leaves the file");
    expect(renameTilesetSection(path, "sections/Mine/Rivers/Water.sct", "water") == "water", "a rename can change only the letter case");
    {
        const TilesetArchive renamed = openTilesetArchive(path);
        const HpiFile* water = renamed.archive().find("sections/Mine/Rivers/water.sct");
        expect(water != nullptr && water->path.find("water.sct") != std::string::npos, "the new letter case is stored");
    }
    bool missingRename = false;
    try {
        renameTilesetSection(path, "sections/Mine/Rivers/Nothing.sct", "Pond");
    } catch (const std::exception&) {
        missingRename = true;
    }
    expect(missingRename, "renaming a tile that is not there is an error");

    bool missing = false;
    try {
        removeFavoriteSection(path, "sections/Mine/Rivers/Nothing.sct");
    } catch (const std::exception&) {
        missing = true;
    }
    expect(missing, "removing a tile that is not there is an error");

    for (const char* name : {"Mine/Rivers/Rivers01", "Mine/Rivers/Rivers02", "Mine/Rivers/rivers03", "Mine/Rivers/Rivers04", "Mine/Rivers/water", "Mine/Cliffs/Rivers01", "Mine/Cliffs/Cliffs01", "Mine/Sm Lakes/Sm Lakes01", "Other/Rivers/Rivers01"}) {
        removeFavoriteSection(path, std::string("sections/") + name + ".sct");
    }
    expect(openTilesetArchive(path).sections.empty(), "an archive emptied of tiles still opens");

    std::filesystem::remove(path, ignored);
}

void testArchiveDocument() {
    using namespace galaxy;
    namespace fs = std::filesystem;
    std::error_code ignored;
    const fs::path work = fs::temp_directory_path() / "ge-archive-document-test";
    fs::remove_all(work, ignored);
    fs::create_directories(work / "in" / "Pack" / "Deep", ignored);
    auto writeText = [](const fs::path& path, const std::string& text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    auto bytesOfText = [](const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); };

    expect(normalizeArchivePath("\\a\\\\b//c/") == "a/b/c", "archive path is normalized");
    expect(archiveParentPath("a/b/c") == "a/b" && archiveParentPath("a").empty(), "archive parent path");
    expect(archiveLeafName("a/b/c") == "c" && archiveLeafName("a") == "a", "archive leaf name");
    expect(archivePathIsInside("Maps/x.tnt", "maps") && !archivePathIsInside("Mapsx/x", "maps") && archivePathIsInside("a", ""), "archive path containment");
    expect(archiveNameProblem("ok.txt").empty() && !archiveNameProblem("").empty() && !archiveNameProblem("a/b").empty() && !archiveNameProblem("..").empty(), "archive names are checked");

    // Build a small archive on disk and open it.
    const fs::path first = work / "first.hpi";
    writeHpiArchive(first, {{"maps/One.tnt", bytesOfText("one")},
                            {"maps/Two.ota", bytesOfText("two")},
                            {"units/arm/Peewee.fbi", bytesOfText("peewee")},
                            {"Readme.txt", bytesOfText("readme")}});
    ArchiveDocument doc = ArchiveDocument::open(first);
    expect(!doc.dirty() && !doc.canUndo() && doc.fileCount() == 4, "opened archive is clean");
    expect(doc.displayName() == "first.hpi", "archive display name");
    std::vector<ArchiveItem> top = doc.children("");
    expect(top.size() == 3 && top[0].directory && top[0].name == "maps" && top[1].name == "units" && top[2].name == "Readme.txt", "top level lists folders first");
    expect(top[0].fileCount == 2 && top[0].childCount == 2 && top[1].fileCount == 1 && top[1].childCount == 1, "folder counts");
    expect(top[0].size == 6, "folder size is the total of its files");
    expect(doc.isFolder("MAPS") && doc.isFile("MAPS/one.TNT") && !doc.isFile("maps"), "lookups ignore case");
    expect(doc.canonicalPath("MAPS/new/x") == "maps/new/x" && doc.canonicalPath("units/ARM") == "units/arm", "canonical path uses archive case");
    expect(doc.read("maps/two.ota") == bytesOfText("two"), "file contents read back");
    expect(doc.search("", "pee").size() == 1 && doc.search("maps", "pee").empty() && doc.search("", "arm").size() == 1, "search");

    // Import a folder and a file, then see conflicts.
    writeText(work / "in" / "Pack" / "a.txt", "A");
    writeText(work / "in" / "Pack" / "Deep" / "b.txt", "B");
    writeText(work / "in" / "Pack" / ".DS_Store", "junk");
    writeText(work / "in" / "readme.TXT", "newer readme");
    ImportPlan plan = doc.planImport("maps", {work / "in" / "Pack", work / "in" / "readme.TXT"});
    expect(plan.files.size() == 3 && plan.skipped == 0 && plan.conflicts.empty(), "import plan covers the folder and the file");
    expect(doc.applyImport(plan, false) == 3 && doc.dirty() && doc.canUndo(), "import adds files");
    expect(doc.isFile("maps/Pack/Deep/b.txt") && doc.isFile("maps/readme.TXT") && !doc.isFile("maps/Pack/.DS_Store"), "imported files are in place");
    expect(doc.read("maps/Pack/a.txt") == bytesOfText("A"), "imported contents");
    plan = doc.planImport("MAPS", {work / "in" / "Pack"});
    expect(plan.conflicts.size() == 2, "re-import finds conflicts");
    bool refused = false;
    try {
        doc.applyImport(plan, false);
    } catch (const std::exception&) {
        refused = true;
    }
    expect(refused, "import refuses conflicts unless told to replace");
    writeText(work / "in" / "Pack" / "a.txt", "A2");
    plan = doc.planImport("maps", {work / "in" / "Pack"});
    expect(doc.applyImport(plan, true) == 2 && doc.read("maps/Pack/a.txt") == bytesOfText("A2") && doc.fileCount() == 7, "import replaces on request");
    doc.undo();
    expect(doc.read("maps/Pack/a.txt") == bytesOfText("A") && doc.fileCount() == 7, "undo restores replaced file");
    doc.undo();
    expect(!doc.dirty() && doc.fileCount() == 4 && !doc.canUndo() && doc.canRedo(), "undo back to the opened state is clean");
    doc.redo();
    doc.redo();
    expect(doc.dirty() && doc.fileCount() == 7, "redo repeats the changes");

    // A folder cannot be imported over a file, nor a file over a folder.
    writeText(work / "in" / "Pack.txt", "x");
    expect(doc.planImport("", {work / "in" / "Pack.txt"}).files.size() == 1, "file next to folder of another name is fine");
    fs::create_directories(work / "in2" / "maps", ignored);
    writeText(work / "in2" / "maps" / "z.txt", "z");
    fs::create_directories(work / "in3", ignored);
    writeText(work / "in3" / "units", "file where a folder is");
    plan = doc.planImport("", {work / "in3" / "units"});
    expect(plan.files.empty() && plan.skipped == 1, "a file cannot take a folder's place");

    // Move.
    MovePlan move = doc.planMove({"maps/Pack"}, "units");
    expect(move.problem.empty() && move.moves.size() == 1 && move.moves[0].second == "units/Pack" && move.conflicts.empty(), "move plan");
    expect(doc.applyMove(move, false) == 1 && doc.isFile("units/Pack/Deep/b.txt") && !doc.isFolder("maps/Pack"), "folder moved with its contents");
    expect(!doc.planMove({"units"}, "units/Pack/Deep").problem.empty(), "a folder cannot move into itself");
    expect(!doc.planMove({"units"}, "units").problem.empty(), "a folder cannot move into itself directly");
    expect(doc.planMove({"units/Pack"}, "units").moves.empty(), "moving an item to where it is does nothing");
    expect(doc.planMove({"units", "units/Pack/a.txt"}, "maps").moves.size() == 1, "a selected item inside a selected folder travels with it");
    move = doc.planMove({"maps/readme.TXT"}, "");
    expect(move.conflicts.size() == 1 && move.conflicts[0] == "Readme.txt", "move onto another file is a conflict");
    expect(doc.applyMove(move, true) == 1 && doc.read("Readme.txt") == bytesOfText("newer readme") && !doc.isFile("maps/readme.TXT"), "move can replace");
    move = doc.planMove({"Readme.txt"}, "maps/One.tnt");
    expect(!move.problem.empty(), "a file cannot move into a file");
    doc.undo();
    expect(doc.read("Readme.txt") == bytesOfText("readme") && doc.isFile("maps/readme.TXT"), "undo of a replacing move");

    // Rename and folders.
    doc.rename("maps/One.tnt", "Uno.tnt");
    expect(doc.isFile("maps/Uno.tnt") && !doc.isFile("maps/One.tnt") && doc.read("maps/Uno.tnt") == bytesOfText("one"), "rename a file");
    doc.rename("maps/Uno.tnt", "UNO.tnt");
    expect(doc.find("maps/uno.tnt")->name == "UNO.tnt", "rename may change only the case");
    bool clash = false;
    try {
        doc.rename("maps/UNO.tnt", "two.OTA");
    } catch (const std::exception&) {
        clash = true;
    }
    expect(clash, "rename onto another name is refused");
    doc.rename("units", "Units2");
    expect(doc.isFile("Units2/arm/Peewee.fbi") && !doc.isFolder("units"), "rename a folder");
    const std::string made = doc.createFolder("Units2", "Empty");
    expect(made == "Units2/Empty" && doc.isFolder("units2/empty") && doc.emptyFolderCount() == 1, "new folder");
    bool duplicateFolder = false;
    try {
        doc.createFolder("Units2", "empty");
    } catch (const std::exception&) {
        duplicateFolder = true;
    }
    expect(duplicateFolder, "duplicate folder is refused");

    // Remove.
    expect(doc.remove({"Units2/arm"}) == 1 && !doc.isFile("Units2/arm/Peewee.fbi"), "remove a folder");
    expect(doc.isFolder("Units2"), "the folder that held the removed item stays");
    doc.undo();
    expect(doc.isFile("Units2/arm/Peewee.fbi"), "undo remove");

    // Export.
    const fs::path out = work / "out";
    ExportResult exported = doc.exportTo({"maps"}, out, false);
    expect(exported.files == 3 && exported.skipped == 0, "export a folder");
    expect(fs::exists(out / "maps" / "UNO.tnt") && fs::exists(out / "maps" / "Two.ota"), "exported folder keeps its name");
    expect(!doc.exportConflicts({"maps"}, out).empty(), "export finds files already on disk");
    exported = doc.exportTo({"maps"}, out, false);
    expect(exported.files == 0 && exported.skipped == 3, "export leaves existing files alone");
    exported = doc.exportTo({"maps/Two.ota", "Readme.txt"}, out, false);
    expect(exported.files == 2 && fs::exists(out / "Readme.txt"), "export a single file");
    const fs::path everything = work / "everything";
    exported = doc.exportTo({""}, everything, false);
    expect(exported.files == doc.fileCount() && fs::exists(everything / "Readme.txt"), "export everything writes straight into the folder");
    {
        std::ifstream in(out / "Readme.txt", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        expect(text == "readme", "exported contents");
    }

    // A hostile name never leaves the export folder.
    const fs::path hostile = work / "hostile.hpi";
    writeHpiArchive(hostile, {{"../escape.txt", bytesOfText("bad")}, {"ok/fine.txt", bytesOfText("good")}, {"bad:name?.txt", bytesOfText("odd")}});
    ArchiveDocument odd = ArchiveDocument::open(hostile);
    const fs::path safeOut = work / "safe" / "inside";
    exported = odd.exportTo({""}, safeOut, false);
    expect(!fs::exists(work / "safe" / "escape.txt") && !fs::exists(work / "escape.txt"), "export never writes outside the folder");
    expect(exported.skipped >= 1 && fs::exists(safeOut / "ok" / "fine.txt") && fs::exists(safeOut / "bad_name_.txt"), "unsafe names are skipped and odd ones renamed");

    // Save, reopen.
    const fs::path saved = work / "saved.hpi";
    doc.saveAs(saved);
    expect(!doc.dirty() && !doc.canUndo() && doc.path() == saved, "saving makes the document clean");
    ArchiveDocument reopened = ArchiveDocument::open(saved);
    expect(reopened.fileCount() == doc.fileCount(), "saved archive has the same files");
    expect(reopened.isFile("Units2/Pack/Deep/b.txt") && reopened.read("Units2/Pack/a.txt") == bytesOfText("A2"), "saved archive keeps imported folders");
    expect(reopened.isFile("Units2/arm/Peewee.fbi") && reopened.read("Units2/arm/Peewee.fbi") == bytesOfText("peewee"), "saved archive keeps moved files");
    expect(reopened.isFile("maps/UNO.tnt"), "saved archive keeps renames");

    // A new archive and the empty case.
    ArchiveDocument blank;
    expect(blank.fileCount() == 0 && !blank.dirty() && blank.children("").empty() && blank.displayName() == "Untitled.hpi", "blank archive");
    bool emptySave = false;
    try {
        blank.saveAs(work / "empty.hpi");
    } catch (const std::exception&) {
        emptySave = true;
    }
    expect(emptySave, "an archive with no files cannot be saved");
    plan = blank.planImport("", {work / "in" / "readme.TXT"});
    blank.applyImport(plan, false);
    blank.saveAs(work / "fresh.hpi");
    expect(ArchiveDocument::open(work / "fresh.hpi").isFile("readme.TXT"), "a blank archive saves after an import");

    fs::remove_all(work, ignored);
}

void testBrushStrokeNames() {
    using namespace galaxy;
    expect(brushStrokeVariation("Base-85-16x16-01", "Base-85", 16) == 1, "base 85 16x16 first variation");
    expect(brushStrokeVariation("base-85-16x16-03", "Base-85", 16) == 3, "stroke name ignores case");
    expect(brushStrokeVariation("Base-219-16x16-04", "Base-219", 16) == 4, "a later variation still matches");
    expect(brushStrokeVariation("Base-0-16x16-01", "Base-0", 16) == 1, "base 0 16x16 stroke");
    expect(brushStrokeVariation("Base-0-16x16-01", "Base-69", 16) < 0, "0 stroke is not a 69 stroke");
    expect(brushStrokeVariation("Base-85-128x128-01", "Base-85", 128) == 1, "128 stroke");
    expect(brushStrokeVariation("Base-85-16x16-10", "Base-85", 16) == 10, "variation 10");
    expect(brushStrokeVariation("Base-85-16x16-01", "Base-219", 16) < 0, "85 stroke is not a 219 stroke");
    expect(brushStrokeVariation("Base-85-32x32-01", "Base-85", 16) < 0, "32x32 is not a 16x16 stroke");
    expect(brushStrokeVariation("Base-85-16x16-01", "Base-8", 16) < 0, "paint name must match in full");
    expect(brushStrokeVariation("Base-85-16x16-", "Base-85", 16) < 0, "missing variation");
    expect(brushStrokeVariation("Base-85-16x16-01a", "Base-85", 16) < 0, "variation is only digits");

    expect(transitionVariation("Cliff-High-BR-01", "Cliff-High-BR") == 1, "cliff variation 1");
    expect(transitionVariation("cliff-high-t-04", "Cliff-High-T") == 4, "cliff variation ignores case");
    expect(transitionVariation("Cliff-High-TL-01", "Cliff-High-T") < 0, "a T edge is not a TL corner");
    expect(transitionVariation("Cliff-Low-BR-01", "Cliff-High-BR") < 0, "a low corner is not a high corner");
    expect(transitionVariation("Coast-Water-B-01", "Coast-Water-B") == 1, "coast variation 1");
    expect(transitionVariation("Coast-Land-BL-02", "Coast-Land-BL") == 2, "coast land variation 2");
    expect(transitionVariation("Coast-Water-BL-01", "Coast-Water-B") < 0, "a B edge is not a BL corner");
    expect(transitionVariation("Cliff-High-BR-01", "") < 0, "no piece means no match");

    // Every corner pattern has the piece the tileset names for it.
    expect(cliffPieceName(0).empty() && cliffPieceName(15).empty(), "flat cells need no cliff");
    expect(coastPieceName(0).empty() && coastPieceName(15).empty(), "flat cells need no coast");
    expect(cliffDiagonal(6) && cliffDiagonal(9) && !cliffDiagonal(3), "only the two diagonal masks are diagonal");
    expect(cliffPieceName(8) == "Cliff-High-BR" && cliffPieceName(4) == "Cliff-High-BL", "bottom corners");
    expect(cliffPieceName(2) == "Cliff-High-TR" && cliffPieceName(1) == "Cliff-High-TL", "top corners");
    expect(cliffPieceName(12) == "Cliff-High-B" && cliffPieceName(3) == "Cliff-High-T", "top and bottom edges");
    expect(cliffPieceName(5) == "Cliff-High-L" && cliffPieceName(10) == "Cliff-High-R", "left and right edges");
    expect(cliffPieceName(7) == "Cliff-Low-BR" && cliffPieceName(11) == "Cliff-Low-BL", "low bottom corners");
    expect(cliffPieceName(13) == "Cliff-Low-TR" && cliffPieceName(14) == "Cliff-Low-TL", "low top corners");
    expect(coastPieceName(8) == "Coast-Land-BR" && coastPieceName(1) == "Coast-Land-TL", "land corners");
    expect(coastPieceName(2) == "Coast-Land-TR" && coastPieceName(4) == "Coast-Land-BL", "more land corners");
    expect(coastPieceName(12) == "Coast-Water-T" && coastPieceName(3) == "Coast-Water-B", "water along the top and bottom");
    expect(coastPieceName(5) == "Coast-Water-R" && coastPieceName(10) == "Coast-Water-L", "water along the right and left");
    expect(coastPieceName(7) == "Coast-Water-BR" && coastPieceName(11) == "Coast-Water-BL", "water in the bottom corners");
    expect(coastPieceName(13) == "Coast-Water-TR" && coastPieceName(14) == "Coast-Water-TL", "water in the top corners");
    expect(transitionPieceName(TransitionStyle::Coast, 8) == "Coast-Land-BR" && transitionPieceName(TransitionStyle::Cliff, 8) == "Cliff-High-BR", "coast and cliff families name their pieces");
    expect(transitionPieceName(TransitionStyle::ShallowCoast, 8) == "ShallowCoast-Land-BR" && transitionPieceName(TransitionStyle::ShallowCoast, 12) == "ShallowCoast-Water-T", "shallow coast pieces carry the Shallow prefix");
    expect(transitionPieceName(TransitionStyle::ShallowDeep, 2) == "ShallowDeep-Shallow-TR" && transitionPieceName(TransitionStyle::ShallowDeep, 12) == "ShallowDeep-Shallow-B" && transitionPieceName(TransitionStyle::ShallowDeep, 5) == "ShallowDeep-Shallow-L", "shallow water in a deep cell");
    expect(transitionPieceName(TransitionStyle::ShallowDeep, 7) == "ShallowDeep-Deep-BR" && transitionPieceName(TransitionStyle::ShallowDeep, 14) == "ShallowDeep-Deep-TL", "deep water in a shallow cell");
    expect(transitionPieceName(TransitionStyle::ShallowDeep, 6).empty() && transitionPieceName(TransitionStyle::ShallowDeep, 15).empty(), "diagonal and flat cells have no ShallowDeep piece");
    expect(brushStrokeVariation("Base-69-32x32-01", "Base-68", 32) == 1 && brushStrokeVariation("Base-68-32x32-02", "Base-68", 32) == 2, "Base-68 uses the strokes named Base-69");
    expect(brushStrokeVariation("Base-69-32x32-01", "Base-85", 32) < 0 && brushStrokeVariation("Base-69-32x32-01", "Base-69", 32) == 1, "the alias does not leak to other paints");
    expect(transitionPieceName(TransitionStyle::Coast, 0).empty() && transitionPieceName(TransitionStyle::None, 8).empty(), "flat cells and unjoined paints have no piece");
    expect(transitionVariation("ShallowCoast-Water-T-01", "Coast-Water-T") < 0, "a shallow coast piece is not a coast piece");

    const BrushGraph deep(std::vector<int>{0, 68, 85, 219});
    expect(deep.style(0, 2) == TransitionStyle::Coast && deep.style(1, 2) == TransitionStyle::ShallowCoast && deep.style(2, 3) == TransitionStyle::Cliff, "each pair of paints has its own family");
    expect(deep.style(0, 1) == TransitionStyle::ShallowDeep && deep.style(1, 0) == TransitionStyle::ShallowDeep, "Base-0 and Base-68 join with ShallowDeep pieces");
    expect(deep.style(0, 3) == TransitionStyle::CoastalCliffDeep && deep.style(1, 3) == TransitionStyle::CoastalCliffShallow && deep.style(3, 1) == TransitionStyle::CoastalCliffShallow, "Base-219 joins Base-0 and Base-68 with CoastalCliff pieces");
    expect(deep.stepToward(3, 0) == 0 && deep.stepToward(1, 0) == 0 && deep.distance(0, 3) == 1 && deep.distance(1, 3) == 1 && deep.distance(0, 2) == 1, "every pair of paints is one step apart");
    expect(deep.compatible(2, 0) && deep.compatible(0, 1) && deep.compatible(0, 3) && deep.compatible(1, 3), "every pair of paints can share a cell");
    const BrushGraph threePaints(std::vector<int>{0, 85, 219});
    expect(threePaints.style(0, 2) == TransitionStyle::CoastalCliffDeep && threePaints.style(0, 1) == TransitionStyle::Coast, "a set without Base-68 still joins 0 and 219");
}

// Keeps the corner levels a test stroke has set so the next stroke can read them.
// Level 0 is Base-0, level 1 is Base-85 and level 2 is Base-219.
struct BrushTestLattice {
    int cellsX = 6;
    int cellsY = 6;
    galaxy::BrushGraph graph;
    std::vector<std::vector<int>> levels;

    // "Water" names Coast-Water. "Lava" names the same shapes Coast-Lava.
    std::string coastLiquid = "Water";

    BrushTestLattice(int cx, int cy, int start, const galaxy::BrushGraph& paints) : cellsX(cx), cellsY(cy), graph(paints), levels(static_cast<std::size_t>(cy + 1), std::vector<int>(static_cast<std::size_t>(cx + 1), start)) {}

    int read(int x, int y) const { return levels[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)]; }

    galaxy::BrushPlan paint(int cellX, int cellY, int cells, int level) {
        galaxy::BrushPlan plan = galaxy::planBrush(cellsX, cellsY, cellX, cellY, cells, level, graph, [&](int x, int y) { return read(x, y); });
        for (const galaxy::BrushNode& node : plan.nodes) {
            levels[static_cast<std::size_t>(node.y)][static_cast<std::size_t>(node.x)] = node.level;
        }
        return plan;
    }

    // An 8 x 8 stamp sets one corner, the point shared by four cells.
    galaxy::BrushPlan paintCorner(int nodeX, int nodeY, int level) {
        return paint(nodeX, nodeY, 0, level);
    }

    // The picture a cell ends up with, named like the tileset names it.
    std::string piece(int cx, int cy) const {
        const std::array<int, 4> corners = {read(cx, cy), read(cx + 1, cy), read(cx, cy + 1), read(cx + 1, cy + 1)};
        if (corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3]) {
            return "Base-" + std::to_string(graph.height(corners[0]));
        }
        const std::string name = galaxy::brushPieceName(graph, corners, coastLiquid);
        return name.empty() ? "Unjoined" : name;
    }

    bool steep() const {
        for (int cy = 0; cy < cellsY; ++cy) {
            for (int cx = 0; cx < cellsX; ++cx) {
                const std::string name = piece(cx, cy);
                if (name == "Unjoined") {
                    return true;
                }
            }
        }
        return false;
    }
};

void testBrushCells() {
    using namespace galaxy;
    const BrushGraph ladder(std::vector<int>{0, 85, 219});
    const BrushGraph deep(std::vector<int>{0, 68, 85, 219});

    // One 16 x 16 click in the middle of an all-85 map: the cell turns to 219 and
    // the eight cells around it turn into cliff pieces. No 219 cell touches an 85 one.
    BrushTestLattice single(5, 5, 1, ladder);
    BrushPlan one = single.paint(2, 2, 1, 2);
    expect(single.piece(2, 2) == "Base-219", "the clicked cell is high ground");
    expect(single.piece(1, 1) == "Cliff-High-BR", "up and left of a click is High-BR");
    expect(single.piece(2, 1) == "Cliff-High-B", "above a click is High-B");
    expect(single.piece(3, 1) == "Cliff-High-BL", "up and right of a click is High-BL");
    expect(single.piece(1, 2) == "Cliff-High-R", "left of a click is High-R");
    expect(single.piece(3, 2) == "Cliff-High-L", "right of a click is High-L");
    expect(single.piece(1, 3) == "Cliff-High-TR", "down and left of a click is High-TR");
    expect(single.piece(2, 3) == "Cliff-High-T", "below a click is High-T");
    expect(single.piece(3, 3) == "Cliff-High-TL", "down and right of a click is High-TL");
    expect(single.piece(0, 0) == "Base-85" && single.piece(4, 4) == "Base-85" && single.piece(0, 2) == "Base-85", "cells farther away stay 85");
    expect(one.cells.size() == 8, "a single click restyles its eight neighbors");
    bool ringHasBlock = false;
    for (const BrushCell& cell : one.cells) {
        ringHasBlock = ringHasBlock || (cell.x == 2 && cell.y == 2);
    }
    expect(!ringHasBlock, "the painted cell is not part of the ring");
    bool ringIsTransition = true;
    for (const BrushCell& cell : one.cells) {
        ringIsTransition = ringIsTransition && cell.level == 1 && cell.mask != 0;
    }
    expect(ringIsTransition, "every ring cell sits between level 1 and level 2");

    // Painting a high cell next to another one merges them into a longer shape.
    BrushTestLattice pair(5, 6, 1, ladder);
    pair.paint(2, 1, 1, 2);
    pair.paint(2, 2, 1, 2);
    expect(pair.piece(2, 1) == "Base-219" && pair.piece(2, 2) == "Base-219", "two clicks give two high cells");
    expect(pair.piece(1, 1) == "Cliff-High-R" && pair.piece(1, 2) == "Cliff-High-R", "the left wall runs down both cells");
    expect(pair.piece(3, 1) == "Cliff-High-L" && pair.piece(3, 2) == "Cliff-High-L", "the right wall runs down both cells");
    expect(pair.piece(2, 0) == "Cliff-High-B" && pair.piece(2, 3) == "Cliff-High-T", "the ends close with a bottom and top edge");
    expect(pair.piece(1, 0) == "Cliff-High-BR" && pair.piece(3, 3) == "Cliff-High-TL", "the ends close with corners");

    // The plateau drawn in the design: five high cells and the cliff ring around them.
    BrushTestLattice figure(6, 6, 1, ladder);
    const int solid[][2] = {{3, 1}, {1, 2}, {2, 2}, {3, 2}, {2, 3}};
    for (const auto& cell : solid) {
        figure.paint(cell[0], cell[1], 1, 2);
    }
    const char* design[6][6] = {
        {"Base-85", "Base-85", "Cliff-High-BR", "Cliff-High-B", "Cliff-High-BL", "Base-85"},
        {"Cliff-High-BR", "Cliff-High-B", "Cliff-Low-TL", "Base-219", "Cliff-High-L", "Base-85"},
        {"Cliff-High-R", "Base-219", "Base-219", "Base-219", "Cliff-High-L", "Base-85"},
        {"Cliff-High-TR", "Cliff-Low-BL", "Base-219", "Cliff-Low-BR", "Cliff-High-TL", "Base-85"},
        {"Base-85", "Cliff-High-TR", "Cliff-High-T", "Cliff-High-TL", "Base-85", "Base-85"},
        {"Base-85", "Base-85", "Base-85", "Base-85", "Base-85", "Base-85"},
    };
    bool designMatches = true;
    for (int cy = 0; cy < 6; ++cy) {
        for (int cx = 0; cx < 6; ++cx) {
            if (figure.piece(cx, cy) != design[cy][cx]) {
                designMatches = false;
                std::cerr << "design cell " << cx << "," << cy << " is " << figure.piece(cx, cy) << " but should be " << design[cy][cx] << "\n";
            }
        }
    }
    expect(designMatches, "five painted cells draw the plateau in the design");

    // A diagonal pair of clicks cannot leave two high corners touching only at a point.
    BrushTestLattice diagonal(4, 4, 1, ladder);
    diagonal.paint(1, 1, 1, 2);
    diagonal.paint(2, 2, 1, 2);
    expect(!diagonal.steep(), "no cell is left with two diagonal high corners");

    // A 32 x 32 brush paints a 2 x 2 block of cells.
    BrushTestLattice big(6, 6, 1, ladder);
    BrushPlan block = big.paint(2, 2, 2, 2);
    expect(big.piece(2, 2) == "Base-219" && big.piece(3, 3) == "Base-219" && big.piece(2, 3) == "Base-219", "a 32x32 brush fills four cells");
    expect(big.piece(1, 1) == "Cliff-High-BR" && big.piece(4, 4) == "Cliff-High-TL" && big.piece(3, 1) == "Cliff-High-B", "the ring closes the larger block");
    expect(block.cells.size() == 12, "a 2 x 2 block has twelve cells around it");

    // Painting 85 into a 219 plateau cuts a hole with the low pieces around it.
    BrushTestLattice flat(5, 5, 1, ladder);
    flat.paint(1, 1, 3, 2);
    BrushPlan hole = flat.paint(2, 2, 1, 1);
    expect(flat.piece(2, 2) == "Base-85", "the erased cell is 85");
    expect(flat.piece(1, 1) == "Cliff-Low-BR" && flat.piece(3, 1) == "Cliff-Low-BL", "the hole's upper corners are Low pieces");
    expect(flat.piece(1, 3) == "Cliff-Low-TR" && flat.piece(3, 3) == "Cliff-Low-TL", "the hole's lower corners are Low pieces");
    expect(flat.piece(2, 1) == "Cliff-High-T" && flat.piece(2, 3) == "Cliff-High-B", "the hole's top and bottom walls");
    expect(flat.piece(1, 2) == "Cliff-High-L" && flat.piece(3, 2) == "Cliff-High-R", "the hole's side walls");
    expect(hole.cells.size() == 8, "erasing one cell restyles its eight neighbors");

    // Painting 85 on 85 changes nothing around it.
    BrushTestLattice grass(4, 4, 1, ladder);
    BrushPlan same = grass.paint(1, 1, 1, 1);
    expect(same.cells.empty(), "85 on 85 leaves the neighbors alone");

    // A block at the map edge only restyles the cells that exist.
    BrushTestLattice edge(3, 3, 1, ladder);
    BrushPlan corner = edge.paint(0, 0, 1, 2);
    expect(edge.piece(0, 0) == "Base-219" && edge.piece(1, 0) == "Cliff-High-L" && edge.piece(0, 1) == "Cliff-High-T" && edge.piece(1, 1) == "Cliff-High-TL", "a corner click ends at the map edge");
    expect(corner.cells.size() == 3, "a corner click restyles three cells");

    // Base-85 painted into water meets it with Coast pieces, the same way 219 meets 85 with cliffs.
    BrushTestLattice shore(5, 5, 0, ladder);
    BrushPlan land = shore.paint(2, 2, 1, 1);
    expect(shore.piece(2, 2) == "Base-85", "the painted cell is land");
    expect(shore.piece(1, 1) == "Coast-Land-BR" && shore.piece(3, 1) == "Coast-Land-BL", "land corners up and to the sides");
    expect(shore.piece(1, 3) == "Coast-Land-TR" && shore.piece(3, 3) == "Coast-Land-TL", "land corners down and to the sides");
    expect(shore.piece(2, 1) == "Coast-Water-T" && shore.piece(2, 3) == "Coast-Water-B", "water above and below the land");
    expect(shore.piece(1, 2) == "Coast-Water-L" && shore.piece(3, 2) == "Coast-Water-R", "water left and right of the land");
    expect(shore.piece(0, 0) == "Base-0" && shore.piece(4, 4) == "Base-0", "cells farther away stay water");
    expect(land.cells.size() == 8, "land in water restyles its eight neighbors");
    bool ringIsCoast = true;
    for (const BrushCell& cell : land.cells) {
        ringIsCoast = ringIsCoast && cell.level == 0 && cell.mask != 0;
    }
    expect(ringIsCoast, "every ring cell sits between level 0 and level 1");

    // 219 painted into water meets it directly with CoastalCliff pieces.
    BrushTestLattice island(6, 6, 0, ladder);
    BrushPlan peak = island.paint(2, 2, 1, 2);
    expect(island.piece(2, 2) == "Base-219", "the painted cell is high ground");
    expect(island.piece(1, 1) == "CC-219-BR-0-ALL" && island.piece(2, 1) == "CC-219-B-0-T" && island.piece(3, 1) == "CC-219-BL-0-ALL", "coastal cliffs above the high ground");
    expect(island.piece(1, 2) == "CC-219-R-0-L" && island.piece(3, 2) == "CC-219-L-0-R", "coastal cliffs beside the high ground");
    expect(island.piece(1, 3) == "CC-219-TR-0-ALL" && island.piece(2, 3) == "CC-219-T-0-B" && island.piece(3, 3) == "CC-219-TL-0-ALL", "coastal cliffs below the high ground");
    expect(island.piece(0, 0) == "Base-0" && island.piece(5, 5) == "Base-0" && !island.steep(), "the rest stays water");
    expect(peak.cells.size() == 8, "a 219 click in water restyles its eight neighbors");

    // Painting 0 into a 219 plateau leaves coastal cliffs around the water.
    BrushTestLattice mesa(6, 6, 2, ladder);
    mesa.paint(2, 2, 1, 0);
    expect(mesa.piece(2, 2) == "Base-0", "the painted cell is water");
    expect(mesa.piece(1, 1) == "CC-219-ALL-0-BR" && mesa.piece(2, 1) == "CC-219-T-0-B" && mesa.piece(3, 3) == "CC-219-ALL-0-TL", "coastal cliffs around the water");
    expect(mesa.piece(0, 0) == "Base-219" && mesa.piece(5, 5) == "Base-219" && !mesa.steep(), "the plateau goes on around it");

    // Painting 0 into 85 gives a pond with coast pieces and nothing steeper.
    BrushTestLattice pond(5, 5, 1, ladder);
    pond.paint(2, 2, 1, 0);
    expect(pond.piece(2, 2) == "Base-0" && pond.piece(1, 1) == "Coast-Water-BR" && pond.piece(2, 1) == "Coast-Water-B", "a pond has water pieces on the land side");
    expect(pond.piece(3, 3) == "Coast-Water-TL" && pond.piece(1, 2) == "Coast-Water-R" && pond.piece(3, 2) == "Coast-Water-L", "the pond's other sides");
    expect(!pond.steep(), "a pond leaves no steep cell");

    // With Base-68 in the set, Base-0 painted on Base-85 still meets it with Coast
    // pieces only: no cliffs, no shallow coast, and no 68 band in between.
    BrushTestLattice lake(5, 5, 2, deep);
    BrushPlan water = lake.paint(2, 2, 1, 0);
    expect(lake.piece(2, 2) == "Base-0", "the painted cell is water");
    expect(lake.piece(1, 1) == "Coast-Water-BR" && lake.piece(2, 1) == "Coast-Water-B" && lake.piece(3, 1) == "Coast-Water-BL", "water meets land with coast pieces above");
    expect(lake.piece(1, 2) == "Coast-Water-R" && lake.piece(3, 2) == "Coast-Water-L", "water meets land with coast pieces on the sides");
    expect(lake.piece(1, 3) == "Coast-Water-TR" && lake.piece(2, 3) == "Coast-Water-T" && lake.piece(3, 3) == "Coast-Water-TL", "water meets land with coast pieces below");
    bool onlyCoast = water.cells.size() == 8;
    for (int cy = 0; cy < 5; ++cy) {
        for (int cx = 0; cx < 5; ++cx) {
            const std::string name = lake.piece(cx, cy);
            const bool transition = name.rfind("Base-", 0) != 0;
            onlyCoast = onlyCoast && (!transition || name.rfind("Coast-", 0) == 0);
        }
    }
    expect(onlyCoast, "painting water on land uses nothing but coast pieces");
    for (const BrushCell& cell : water.cells) {
        onlyCoast = onlyCoast && cell.level == 0 && cell.upper == 2 && cell.mask != 0;
    }
    expect(onlyCoast, "every ring cell joins level 0 to level 2");

    // The same click on land that has a high plateau next to it keeps coast and cliffs apart.
    BrushTestLattice shoreline(8, 5, 2, deep);
    shoreline.paint(5, 2, 1, 3);
    shoreline.paint(1, 2, 1, 0);
    expect(shoreline.piece(5, 2) == "Base-219" && shoreline.piece(1, 2) == "Base-0", "a plateau and a lake share the map");
    expect(shoreline.piece(4, 2) == "Cliff-High-R" && shoreline.piece(2, 2) == "Coast-Water-L", "cliffs next to the plateau and coast next to the lake");
    expect(!shoreline.steep(), "the lake and the plateau never touch a piece they have no match for");

    // CoastalCliff names: Base-219 against Base-0 or Base-68, with or without Base-85.
    {
        expect(coastalCliffName(0, kCornerBottomRight, 0) == "CC-219-BR-0-ALL" && coastalCliffName(68, kCornerTopRight, 0) == "CC-219-TR-68-ALL", "Base-219 on one corner");
        expect(coastalCliffName(0, 7, 0) == "CC-219-ALL-0-BR" && coastalCliffName(68, 11, 0) == "CC-219-ALL-68-BL", "Base-219 on all but one corner");
        expect(coastalCliffName(0, 10, 0) == "CC-219-R-0-L" && coastalCliffName(68, 3, 0) == "CC-219-T-68-B" && coastalCliffName(0, 12, 0) == "CC-219-B-0-T", "Base-219 along one side");
        expect(coastalCliffName(0, 1, 4) == "CC-219-TL-85-BL-0-R" && coastalCliffName(0, 4, 1) == "CC-219-BL-85-TL-0-R", "219 and 85 down the left side with water on the right");
        expect(coastalCliffName(0, 2, 8) == "CC-219-TR-85-BR-0-L" && coastalCliffName(0, 4, 8) == "CC-219-BL-85-BR-0-T", "219 beside 85 with water on the far side");
        expect(coastalCliffName(0, 10, 1) == "CC-219-R-85-TL-0-BL" && coastalCliffName(68, 5, 2) == "CC-219-L-85-TR-68-BR", "219 along one side with 85 and water at the other corners");
        expect(coastalCliffName(68, 3, 4) == "CC-219-T-85-BL-68-BR", "shallow water takes the place of deep water");
        expect(coastalCliffName(0, 6, 0).empty() && coastalCliffName(0, 9, 0).empty() && coastalCliffName(0, 15, 0).empty() && coastalCliffName(0, 0, 0).empty(), "diagonal and flat cells have no coastal cliff");
        expect(coastalCliffName(0, 1, 8).empty() && coastalCliffName(0, 1, 6).empty() && coastalCliffName(85, 1, 0).empty(), "layouts without a section have no name");

        std::vector<std::string> made;
        for (const int low : {0, 68}) {
            for (int high = 1; high < 15; ++high) {
                for (int mid = 0; mid < 15; ++mid) {
                    const std::string name = coastalCliffName(low, high, mid);
                    if (!name.empty()) {
                        made.push_back(name);
                    }
                }
            }
        }
        std::sort(made.begin(), made.end());
        expect(std::adjacent_find(made.begin(), made.end()) == made.end(), "no two layouts share a name");
        expect(made.size() == 56, "28 coastal cliff layouts for each of Base-0 and Base-68");

        const BrushGraph four(std::vector<int>{0, 68, 85, 219});
        expect(brushPieceName(four, {3, 3, 3, 0}) == "CC-219-ALL-0-BR" && brushPieceName(four, {1, 1, 3, 1}) == "CC-219-BL-68-ALL", "corners on 219 and Base-0 or Base-68");
        expect(brushPieceName(four, {3, 0, 2, 0}) == "CC-219-TL-85-BL-0-R", "corners on 219, 85 and 0");
        expect(brushPieceName(four, {3, 3, 1, 2}) == "CC-219-T-85-BR-68-BL", "corners on 219, 85 and 68");
        expect(brushPieceName(four, {3, 1, 0, 0}).empty() && brushPieceName(four, {3, 2, 1, 0}).empty(), "219 with both 0 and 68 has no section");
        expect(brushPieceName(four, {2, 2, 3, 3}) == "Cliff-High-B", "219 against 85 still uses the cliffs");
    }

    // CoastDSL: every layout of land, shallow and deep corners that has a section, and no other.
    {
        const std::vector<std::string> sections = {
            "DSL-Land-L-Shallow-BR-Water-TR", "DSL-Land-L-Shallow-TR-Water-BR", "DSL-Land-R-Shallow-BL-Water-TL", "DSL-Land-R-Shallow-TL-Water-BL",
            "DSL-Land-T-Shallow-BR-Water-BL", "DSL-Land-T-Shallow-BL-Water-BR", "DSL-Land-B-Shallow-TR-Water-TL", "DSL-Land-B-Shallow-TL-Water-TR",
            "DSL-Land-BR-Shallow-TR-Water-L", "DSL-Land-BR-Shallow-BL-Water-T", "DSL-Land-BR-Shallow-BL_and_TR-Water-TL",
            "DSL-Land-BL-Shallow-TL-Water-R", "DSL-Land-BL-Shallow-BR-Water-T", "DSL-Land-BL-Shallow-TL_and_BR-Water-TR",
            "DSL-Land-TL-Shallow-BL-Water-R", "DSL-Land-TL-Shallow-TR-Water-B", "DSL-Land-TL-Shallow-BL_and_TR-Water-BR",
            "DSL-Land-TR-Shallow-BR-Water-L", "DSL-Land-TR-Shallow-TL-Water-B", "DSL-Land-TR-Shallow-TL_and_BR-Water-BL"};
        std::vector<std::string> made;
        for (int land = 1; land < 15; ++land) {
            for (int shallow = 1; shallow < 15; ++shallow) {
                const std::string name = dslPieceName(land, shallow);
                if (!name.empty()) {
                    made.push_back(name);
                }
            }
        }
        std::vector<std::string> wanted = sections;
        std::sort(made.begin(), made.end());
        std::sort(wanted.begin(), wanted.end());
        expect(made == wanted, "the CoastDSL layouts are exactly the 20 sections in the tileset");
        expect(dslPieceName(5, 2) == "DSL-Land-L-Shallow-TR-Water-BR", "land on the left, shallow at the top right");
        expect(dslPieceName(8, 4 | 2) == "DSL-Land-BR-Shallow-BL_and_TR-Water-TL", "one land corner opposite deep water");
        expect(dslPieceName(1, 4) == "DSL-Land-TL-Shallow-BL-Water-R", "a land corner with shallow water below it");
        expect(dslPieceName(1, 8).empty() && dslPieceName(9, 2).empty(), "layouts without a section have no name");
        expect(transitionVariation("DSL-Land-L-Shallow-BR-Water-TR", "DSL-Land-L-Shallow-BR-Water-TR") == 0, "a section without a number is its own variation");
        expect(transitionVariation("DSL-Land-L-Shallow-BR-Water-TR", "DSL-Land-L-Shallow-BR-Water-T") < 0 && transitionVariation("dsl-land-l-shallow-br-water-tr", "DSL-Land-L-Shallow-BR-Water-TR") == 0, "the match is exact and ignores case");
        const std::array<int, 4> corners = {2, 1, 0, 0};
        expect(brushPieceName(deep, corners) == "DSL-Land-TL-Shallow-TR-Water-B", "corners on 85, 68 and 0 pick the matching section");
    }

    // Deep water painted beside shallow water on land makes cells that hold all three.
    {
        BrushTestLattice shore(9, 9, 2, deep);
        shore.paint(3, 3, 1, 1);
        shore.paint(4, 3, 1, 0);
        bool anyDsl = false;
        for (int cy = 0; cy < 9; ++cy) {
            for (int cx = 0; cx < 9; ++cx) {
                anyDsl = anyDsl || shore.piece(cx, cy).rfind("DSL-", 0) == 0;
            }
        }
        expect(anyDsl && !shore.steep(), "land, shallows and deep water meet through CoastDSL sections");
    }

    // Many paints of every kind never leave a cell without a picture or a cliff on water.
    {
        std::uint32_t state = 12345;
        const auto next = [&](int limit) {
            state = state * 1664525u + 1013904223u;
            return static_cast<int>((state >> 8) % static_cast<std::uint32_t>(limit));
        };
        for (int round = 0; round < 6; ++round) {
            BrushTestLattice mix(10, 10, 2, deep);
            bool dslUsed = false;
            bool valid = true;
            bool noCliffOnWater = true;
            for (int click = 0; click < 60; ++click) {
                const int cells = 1 + next(2);
                mix.paint(next(11 - cells), next(11 - cells), cells, next(4));
                for (int cy = 0; cy < 10; ++cy) {
                    for (int cx = 0; cx < 10; ++cx) {
                        const std::string name = mix.piece(cx, cy);
                        valid = valid && name != "Unjoined";
                        dslUsed = dslUsed || name.rfind("DSL-", 0) == 0;
                        const std::array<int, 4> corners = {mix.read(cx, cy), mix.read(cx + 1, cy), mix.read(cx, cy + 1), mix.read(cx + 1, cy + 1)};
                        const int lowest = *std::min_element(corners.begin(), corners.end());
                        const int highest = *std::max_element(corners.begin(), corners.end());
                        noCliffOnWater = noCliffOnWater && !(name.rfind("Cliff", 0) == 0 && (lowest != 2 || highest != 3)) && !(name.rfind("CC-", 0) == 0 && highest != 3);
                    }
                }
            }
            expect(valid, "every cell has a picture after many mixed paints");
            expect(noCliffOnWater, "cliffs sit between Base-85 and Base-219 and coastal cliffs only hold Base-219");
            expect(dslUsed || round > 0, "mixed paints make CoastDSL cells");
        }
    }

    // Base-219 on shallow water uses coastal cliffs, and on land beside water it gets all three paints.
    {
        BrushTestLattice reef(6, 6, 1, deep);
        reef.paint(2, 2, 1, 3);
        expect(reef.piece(2, 2) == "Base-219" && reef.piece(1, 1) == "CC-219-BR-68-ALL" && reef.piece(2, 1) == "CC-219-B-68-T" && reef.piece(3, 3) == "CC-219-TL-68-ALL", "219 on shallow water has coastal cliffs around it");
        expect(!reef.steep(), "219 on shallow water leaves no unjoined cell");

        BrushTestLattice cape(8, 6, 0, deep);
        cape.paint(1, 2, 3, 2);
        cape.paint(3, 2, 1, 3);
        bool three = false;
        for (int cy = 0; cy < 6; ++cy) {
            for (int cx = 0; cx < 8; ++cx) {
                three = three || (cape.piece(cx, cy).rfind("CC-219-", 0) == 0 && cape.piece(cx, cy).find("-85-") != std::string::npos);
            }
        }
        expect(three && !cape.steep(), "219 on land beside water meets both through coastal cliffs that hold 85");
    }

    // Shallow water joins land with ShallowCoast pieces, and steps through land to reach deep water.
    BrushTestLattice shallows(6, 6, 2, deep);
    shallows.paint(2, 2, 1, 1);
    expect(shallows.piece(2, 2) == "Base-68" && shallows.piece(1, 1) == "ShallowCoast-Water-BR" && shallows.piece(2, 1) == "ShallowCoast-Water-B", "shallow water meets land with ShallowCoast pieces");
    expect(!shallows.steep(), "shallow water leaves no unjoined cell");
    BrushTestLattice basin(6, 6, 0, deep);
    BrushPlan shallow = basin.paint(2, 2, 1, 1);
    expect(basin.piece(2, 2) == "Base-68" && basin.piece(1, 1) == "ShallowDeep-Shallow-BR" && basin.piece(2, 1) == "ShallowDeep-Shallow-B" && basin.piece(3, 1) == "ShallowDeep-Shallow-BL", "shallow water in deep water meets it with ShallowDeep pieces");
    expect(basin.piece(1, 2) == "ShallowDeep-Shallow-R" && basin.piece(3, 2) == "ShallowDeep-Shallow-L" && basin.piece(2, 3) == "ShallowDeep-Shallow-T", "on every side");
    bool onlyShallowDeep = shallow.cells.size() == 8;
    for (const BrushCell& cell : shallow.cells) {
        onlyShallowDeep = onlyShallowDeep && cell.level == 0 && cell.upper == 1;
    }
    expect(onlyShallowDeep && !basin.steep(), "deep water never touches land through the shallows");

    BrushTestLattice deepHole(7, 7, 1, deep);
    deepHole.paint(3, 3, 1, 0);
    expect(deepHole.piece(3, 3) == "Base-0" && deepHole.piece(2, 2) == "ShallowDeep-Deep-BR" && deepHole.piece(3, 2) == "ShallowDeep-Shallow-T" && deepHole.piece(4, 3) == "ShallowDeep-Shallow-R", "deep water in shallow water is ringed by ShallowDeep pieces");
    expect(!deepHole.steep(), "a deep hole leaves no unjoined cell");

    // Deep water beside a shallow pool on land never shares a cell with all three.
    BrushTestLattice cove(9, 7, 2, deep);
    cove.paint(2, 3, 1, 1);
    cove.paint(4, 3, 1, 0);
    cove.paint(3, 3, 1, 0);
    expect(!cove.steep(), "land, shallows and deep water meet without a cell holding all three");
    expect(cove.piece(3, 3) == "Base-0" && cove.piece(4, 3) == "Base-0" && cove.piece(2, 3) != "Base-68", "the deep water has taken the cell beside it");
    for (int cy = 0; cy < 7; ++cy) {
        for (int cx = 0; cx < 9; ++cx) {
            const std::string name = cove.piece(cx, cy);
            expect(name.rfind("Cliff", 0) != 0, "water never makes cliffs");
        }
    }

    // An 8 x 8 stamp in open water is the four corner tiles around one meeting point, not a full cell and its ring.
    int cornerX = -1;
    int cornerY = -1;
    expect(galaxy::brushCornerNode(0, 0, 8, 8, cornerX, cornerY) && cornerX == 1 && cornerY == 1, "the top-left of the map snaps to the first meeting of four cells");
    expect(galaxy::brushCornerNode(15, 23, 8, 8, cornerX, cornerY) && cornerX == 1 && cornerY == 1, "tiles around that meeting stay on it");
    expect(galaxy::brushCornerNode(24, 40, 8, 8, cornerX, cornerY) && cornerX == 2 && cornerY == 3, "the next meetings are one cell apart");
    expect(galaxy::brushCornerNode(200, 4, 8, 8, cornerX, cornerY) && cornerX == 7 && cornerY == 1, "a click past the last meeting stays on a point shared by four cells");
    const BrushGraph coast(std::vector<int>{0, 85, 219});
    BrushTestLattice dot(6, 6, 0, coast);
    const BrushPlan corners = dot.paintCorner(3, 3, 1);
    expect(corners.cells.size() == 4, "an 8 x 8 stamp changes four cells");
    expect(dot.piece(2, 2) == "Coast-Land-BR" && dot.piece(3, 2) == "Coast-Land-BL" && dot.piece(2, 3) == "Coast-Land-TR" && dot.piece(3, 3) == "Coast-Land-TL", "those cells are the four land corners");
    expect(dot.piece(1, 1) == "Base-0" && dot.piece(4, 4) == "Base-0" && !dot.steep(), "the water around the corners stays water");

    // Lava is Base-86 on land and Base-41 on the lava, joined by the same coast shapes as GreenWorld.
    const BrushGraph lava(std::vector<int>{41, 86});
    expect(lava.style(0, 1) == TransitionStyle::Coast && lava.compatible(0, 1), "Base-41 and Base-86 join with coast pieces");
    BrushTestLattice lavaShore(5, 5, 0, lava);
    lavaShore.coastLiquid = "Lava";
    lavaShore.paint(2, 2, 1, 1);
    expect(lavaShore.piece(2, 2) == "Base-86", "the painted cell is land");
    expect(lavaShore.piece(2, 1) == "Coast-Lava-T" && lavaShore.piece(2, 3) == "Coast-Lava-B", "lava runs along the top and bottom of the land");
    expect(lavaShore.piece(1, 2) == "Coast-Lava-L" && lavaShore.piece(3, 2) == "Coast-Lava-R", "lava runs along the left and right of the land");
    expect(lavaShore.piece(1, 1) == "Coast-Land-BR" && lavaShore.piece(3, 1) == "Coast-Land-BL" && lavaShore.piece(1, 3) == "Coast-Land-TR" && lavaShore.piece(3, 3) == "Coast-Land-TL", "each corner of the land keeps a Coast-Land piece");
    expect(lavaShore.piece(0, 0) == "Base-41" && !lavaShore.steep(), "the lava farther away stays lava and every cell has a picture");
    BrushTestLattice pool(5, 5, 1, lava);
    pool.coastLiquid = "Lava";
    pool.paint(2, 2, 1, 0);
    expect(pool.piece(2, 2) == "Base-41" && pool.piece(2, 1) == "Coast-Lava-B" && pool.piece(1, 1) == "Coast-Lava-BR" && !pool.steep(), "a lava pool in the land uses Coast-Lava pieces");

    // Lava Base-190 is the ravine plateau above Base-86. It uses cliff pieces there,
    // and it leaves the coast and the Mars blend on their own pairs.
    const BrushGraph lavaRavine(std::vector<int>{41, 85, 86, 190}, {"Lava", "Mars", "Lava", "Lava"});
    expect(lavaRavine.style(0, 2) == TransitionStyle::Coast && lavaRavine.style(1, 2) == TransitionStyle::WorldBlend && lavaRavine.style(2, 3) == TransitionStyle::Cliff, "Base-190 joins Base-86 with cliffs, and the lower lava pairs stay as they were");
    expect(lavaRavine.style(0, 3) == TransitionStyle::None && lavaRavine.style(1, 3) == TransitionStyle::None, "Base-190 does not join the lava sea or Mars directly");
    BrushTestLattice ravine(5, 5, 2, lavaRavine);
    ravine.paint(2, 2, 1, 3);
    expect(ravine.piece(2, 2) == "Base-190" && ravine.piece(2, 1) == "Cliff-High-B" && ravine.piece(1, 1) == "Cliff-High-BR" && !ravine.steep(), "Base-190 on Base-86 is ringed by cliff pieces");
    BrushTestLattice ravineCut(5, 5, 3, lavaRavine);
    ravineCut.paint(2, 2, 1, 2);
    expect(ravineCut.piece(2, 2) == "Base-86" && ravineCut.piece(2, 1) == "Cliff-High-T" && ravineCut.piece(1, 1) == "Cliff-Low-BR" && !ravineCut.steep(), "Base-86 cut into Base-190 uses the low cliff corners");
    BrushTestLattice stillCoast(5, 5, 0, lavaRavine);
    stillCoast.coastLiquid = "Lava";
    stillCoast.paint(2, 2, 1, 2);
    expect(stillCoast.piece(2, 2) == "Base-86" && stillCoast.piece(2, 1) == "Coast-Lava-T" && !stillCoast.steep(), "Base-86 in the lava still uses coast pieces when Base-190 is present");
    BrushTestLattice stillBlend(5, 5, 1, lavaRavine);
    stillBlend.paint(2, 2, 1, 2);
    expect(stillBlend.piece(2, 2) == "Base-86" && stillBlend.piece(2, 1) == "Blend-Mars-T" && !stillBlend.steep(), "Base-86 on Mars still uses blend pieces when Base-190 is present");

    // Mars is Base-0 water, Base-85 land, and Base-255 cliff ground.
    const BrushGraph mars(std::vector<int>{0, 85, 255});
    expect(mars.style(0, 1) == TransitionStyle::Coast && mars.style(1, 2) == TransitionStyle::Cliff && mars.compatible(0, 1) && mars.compatible(1, 2), "Mars joins water to land with coasts and land to Base-255 with cliffs");
    BrushTestLattice marsLand(5, 5, 0, mars);
    marsLand.paint(2, 2, 1, 1);
    expect(marsLand.piece(2, 2) == "Base-85" && marsLand.piece(2, 1) == "Coast-Water-T" && marsLand.piece(1, 1) == "Coast-Land-BR" && !marsLand.steep(), "Mars land in the water uses coast pieces");
    BrushTestLattice marsCliff(5, 5, 1, mars);
    marsCliff.paint(2, 2, 1, 2);
    expect(marsCliff.piece(2, 2) == "Base-255" && marsCliff.piece(2, 1) == "Cliff-High-B" && marsCliff.piece(1, 1) == "Cliff-High-BR" && !marsCliff.steep(), "Mars cliff ground meets Base-85 with cliff pieces");

    // Lava Base-86 sits one step above Mars Base-85. The mask still marks the Mars corners.
    const BrushGraph marsLava(std::vector<int>{85, 86}, {"Mars", "Lava"});
    expect(marsLava.style(0, 1) == TransitionStyle::WorldBlend && marsLava.compatible(0, 1), "Lava Base-86 and Mars Base-85 join with blend pieces");
    expect(transitionPieceName(TransitionStyle::WorldBlend, kCornerTopRight | kCornerBottomRight) == "Blend-Mars-R" && transitionPieceName(TransitionStyle::WorldBlend, kCornerAll & ~kCornerBottomRight) == "Blend-Mars-Low-BR", "blend pieces name the Mars side");
    BrushTestLattice lavaOnMars(5, 5, 0, marsLava);
    lavaOnMars.paint(2, 2, 1, 1);
    expect(lavaOnMars.piece(2, 2) == "Base-86" && lavaOnMars.piece(2, 1) == "Blend-Mars-T" && lavaOnMars.piece(1, 2) == "Blend-Mars-L" && lavaOnMars.piece(1, 1) == "Blend-Mars-Low-BR" && !lavaOnMars.steep(), "lava painted on Mars is ringed by Mars blend pieces");
    BrushTestLattice marsOnLava(5, 5, 1, marsLava);
    marsOnLava.paint(2, 2, 1, 0);
    expect(marsOnLava.piece(2, 2) == "Base-85" && marsOnLava.piece(2, 1) == "Blend-Mars-B" && marsOnLava.piece(1, 2) == "Blend-Mars-R" && marsOnLava.piece(1, 1) == "Blend-Mars-BR" && !marsOnLava.steep(), "Mars painted on lava is ringed by Mars blend pieces");
    expect(transitionPieceName(TransitionStyle::WorldBlend, kCornerTopRight | kCornerBottomRight, "Water", "Green") == "Blend-Green-R" && transitionPieceName(TransitionStyle::WorldBlend, kCornerAll & ~kCornerBottomRight, "Water", "Green") == "Blend-Green-Low-BR", "green blend pieces name the grass side");
    const BrushGraph greenLava(std::vector<int>{85, 86}, {"Green", "Lava"});
    expect(greenLava.world(0) == "Green" && greenLava.world(1) == "Lava" && greenLava.style(0, 1) == TransitionStyle::WorldBlend, "GreenWorld Base-85 and Lava Base-86 join with blend pieces");
    BrushTestLattice lavaOnGreen(5, 5, 0, greenLava);
    lavaOnGreen.paint(2, 2, 1, 1);
    expect(lavaOnGreen.piece(2, 2) == "Base-86" && lavaOnGreen.piece(2, 1) == "Blend-Green-T" && lavaOnGreen.piece(1, 2) == "Blend-Green-L" && lavaOnGreen.piece(1, 1) == "Blend-Green-Low-BR" && !lavaOnGreen.steep(), "lava painted on grass is ringed by green blend pieces");
    BrushTestLattice greenOnLava(5, 5, 1, greenLava);
    greenOnLava.paint(2, 2, 1, 0);
    expect(greenOnLava.piece(2, 2) == "Base-85" && greenOnLava.piece(2, 1) == "Blend-Green-B" && greenOnLava.piece(1, 2) == "Blend-Green-R" && greenOnLava.piece(1, 1) == "Blend-Green-BR" && !greenOnLava.steep(), "grass painted on lava is ringed by green blend pieces");
}

// A small grid of cells for the river rules. Each cell holds a label such as "Base-85".
struct RiverTestGrid {
    int width = 0;
    int height = 0;
    std::vector<std::string> labels;

    RiverTestGrid(int w, int h, const std::string& fill) : width(w), height(h), labels(static_cast<std::size_t>(w * h), fill) {}

    std::string& at(int x, int y) { return labels[static_cast<std::size_t>(y * width + x)]; }

    galaxy::RiverCell look(int x, int y) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return galaxy::RiverCell{};
        }
        return galaxy::classifyRiverLabel(at(x, y));
    }

    galaxy::RiverPlan river(int x, int y) {
        const galaxy::RiverPlan plan = galaxy::planRiver(x, y, [&](int cx, int cy) { return look(cx, cy); });
        apply(plan);
        return plan;
    }

    galaxy::RiverPlan ridge(int x, int y) {
        const galaxy::RiverPlan plan = galaxy::planRidge(x, y, [&](int cx, int cy) { return look(cx, cy); });
        apply(plan);
        return plan;
    }

    galaxy::RiverPlan spacer(int x, int y) {
        const galaxy::RiverPlan plan = galaxy::planRidgeSpacer(x, y, [&](int cx, int cy) { return look(cx, cy); });
        apply(plan);
        return plan;
    }

    galaxy::RiverPlan bridge(int x, int y) {
        const galaxy::RiverPlan plan = galaxy::planRiverBridge(x, y, [&](int cx, int cy) { return look(cx, cy); });
        apply(plan);
        return plan;
    }

    void apply(const galaxy::RiverPlan& plan) {
        if (!plan.allowed()) {
            return;
        }
        for (const galaxy::RiverEdit& edit : plan.edits) {
            at(edit.x, edit.y) = edit.key + "-01";
        }
    }
};

void testRivers() {
    using galaxy::kRiverBottom;
    using galaxy::kRiverLeft;
    using galaxy::kRiverRight;
    using galaxy::kRiverTop;

    // Piece names.
    const galaxy::RiverPiece horizontal = galaxy::parseRiverPiece("River-Horizontal-02");
    expect(horizontal.valid && !horizontal.bridge && horizontal.flexible && horizontal.sides == (kRiverLeft | kRiverRight) && horizontal.key == "River-Horizontal", "River-Horizontal joins left and right");
    expect(galaxy::parseRiverPiece("River-Vertical-01").sides == (kRiverTop | kRiverBottom), "River-Vertical joins top and bottom");
    expect(galaxy::parseRiverPiece("River-R-B-02").sides == (kRiverRight | kRiverBottom), "River-R-B is a bend");
    expect(galaxy::parseRiverPiece("River-T-L-01").sides == (kRiverTop | kRiverLeft), "River-T-L is a bend");
    expect(galaxy::parseRiverPiece("RiverIntersect-4way-01").sides == galaxy::kRiverAllSides, "the 4-way joins every side");
    expect(galaxy::parseRiverPiece("RiverIntersect-3way-L-R-B-01").sides == (kRiverLeft | kRiverRight | kRiverBottom), "a 3-way names its three sides");
    expect(galaxy::parseRiverPiece("River-Base-03").valid && galaxy::parseRiverPiece("River-Base-03").sides == 0, "River-Base is a pond");
    const galaxy::RiverPiece mouth = galaxy::parseRiverPiece("River-Mouth-T-01");
    expect(mouth.valid && mouth.mouth && !mouth.flexible && mouth.sides == kRiverBottom, "River-Mouth-T has water on top and the river below");
    expect(galaxy::parseRiverPiece("River-Mouth-R-01").sides == kRiverLeft, "River-Mouth-R takes the river from the left");
    const galaxy::RiverPiece bridgePiece = galaxy::parseRiverPiece("RiverB-L-B-02");
    expect(bridgePiece.valid && bridgePiece.bridge && !bridgePiece.flexible && bridgePiece.sides == (kRiverLeft | kRiverBottom), "RiverB-L-B is a fixed bridge bend");
    expect(!galaxy::parseRiverPiece("Base-85").valid && !galaxy::parseRiverPiece("Coast-Water-T-01").valid && !galaxy::parseRiverPiece("River-Weird-01").valid, "other names are not river pieces");

    // The piece for a set of sides always exists in the archive's naming.
    expect(galaxy::riverKeyForSides(0) == "River-Base" && galaxy::riverKeyForSides(kRiverTop) == "River-Vertical" && galaxy::riverKeyForSides(kRiverLeft) == "River-Horizontal", "dead ends carry on straight");
    expect(galaxy::riverKeyForSides(kRiverRight | kRiverBottom) == "River-R-B" && galaxy::riverKeyForSides(kRiverTop | kRiverRight) == "River-T-R", "bends are named for their sides");
    expect(galaxy::riverKeyForSides(kRiverLeft | kRiverBottom) == "River-L-B" && galaxy::riverKeyForSides(kRiverTop | kRiverLeft) == "River-T-L", "all four bends exist");
    expect(galaxy::riverKeyForSides(galaxy::kRiverAllSides) == "RiverIntersect-4way", "four sides make the 4-way");
    expect(galaxy::riverKeyForSides(kRiverTop | kRiverRight | kRiverBottom) == "RiverIntersect-3way-T-R-B"
            && galaxy::riverKeyForSides(kRiverTop | kRiverLeft | kRiverBottom) == "RiverIntersect-3way-T-L-B"
            && galaxy::riverKeyForSides(kRiverTop | kRiverLeft | kRiverRight) == "RiverIntersect-3way-T-L-R"
            && galaxy::riverKeyForSides(kRiverLeft | kRiverRight | kRiverBottom) == "RiverIntersect-3way-L-R-B",
        "three sides make the matching 3-way");
    for (int sides = 0; sides < 16; ++sides) {
        const galaxy::RiverPiece piece = galaxy::parseRiverPiece(galaxy::riverKeyForSides(sides));
        expect(piece.valid && piece.flexible, "every side set has a river piece");
        expect(sides == 0 || (piece.sides & sides) == sides, "the piece reaches every side that has a river");
    }

    // Bridges copy the River- name and exist only for straights and bends.
    expect(galaxy::riverBridgeKey(galaxy::parseRiverPiece("River-Horizontal-03")) == "RiverB-Horizontal", "a bridge over River-Horizontal is RiverB-Horizontal");
    expect(galaxy::riverBridgeKey(galaxy::parseRiverPiece("River-T-R-01")) == "RiverB-T-R", "a bridge over River-T-R is RiverB-T-R");
    expect(galaxy::riverBridgeKey(galaxy::parseRiverPiece("River-Base-01")).empty(), "a pond has no bridge");
    expect(galaxy::riverBridgeKey(galaxy::parseRiverPiece("RiverIntersect-4way-01")).empty(), "a junction has no bridge");
    expect(galaxy::riverBridgeKey(galaxy::parseRiverPiece("River-Mouth-B-01")).empty(), "a mouth has no bridge");
    expect(galaxy::riverBridgeKey(galaxy::parseRiverPiece("RiverB-Vertical-01")).empty(), "a bridge has no second bridge");

    // Coast names.
    expect(galaxy::coastWaterSide("Coast-Water-T-02") == kRiverTop && galaxy::coastWaterSide("Coast-Water-L-01") == kRiverLeft, "straight coasts have a water side");
    expect(galaxy::coastWaterSide("Coast-Water-TL-01") == 0 && galaxy::coastWaterSide("Coast-Land-BR-01") == 0 && galaxy::coastWaterSide("ShallowCoast-Water-T-01") == 0, "corners and other coasts have none");

    // What a cell holds.
    expect(galaxy::classifyRiverLabel("Base-85").kind == galaxy::RiverCell::Kind::Land, "Base-85 takes rivers");
    expect(galaxy::classifyRiverLabel("Base-68").kind == galaxy::RiverCell::Kind::Other && galaxy::classifyRiverLabel("Base-68").label == "Base-68", "Base-68 does not");
    expect(galaxy::classifyRiverLabel("Base-219").kind == galaxy::RiverCell::Kind::Other, "Base-219 does not");
    expect(galaxy::classifyRiverLabel("Coast-Water-B-01").kind == galaxy::RiverCell::Kind::Coast, "a straight coast takes a mouth");
    expect(galaxy::classifyRiverLabel("Coast-Water-BR-01").kind == galaxy::RiverCell::Kind::Other, "a coast corner does not");

    // Placing the first river cell.
    RiverTestGrid lone(5, 5, "Base-85");
    expect(lone.river(2, 2).allowed() && lone.at(2, 2) == "River-Base-01", "a lone river is a pond");

    // Drawing a line east.
    RiverTestGrid line(7, 5, "Base-85");
    line.river(1, 2);
    const galaxy::RiverPlan second = line.river(2, 2);
    expect(second.edits.size() == 2, "the second cell also redraws the pond beside it");
    line.river(3, 2);
    expect(line.at(1, 2) == "River-Horizontal-01" && line.at(2, 2) == "River-Horizontal-01" && line.at(3, 2) == "River-Horizontal-01", "a row of rivers runs straight");

    // A bend and a junction.
    line.river(3, 3);
    expect(line.at(3, 2) == "River-L-B-01" && line.at(3, 3) == "River-Vertical-01", "a river turning south bends the cell above");
    line.river(4, 2);
    expect(line.at(3, 2) == "RiverIntersect-3way-L-R-B-01" && line.at(4, 2) == "River-Horizontal-01", "a branch makes a 3-way");
    line.river(3, 1);
    expect(line.at(3, 2) == "RiverIntersect-4way-01", "four neighbours make the 4-way");
    expect(line.at(2, 2) == "River-Horizontal-01", "cells away from the change keep their pieces");

    // Painting on a river again changes nothing.
    const galaxy::RiverPlan again = line.river(3, 2);
    expect(!again.allowed() && again.reason == "A river is already here", "a river on a river is a no-op");

    // Mouths.
    RiverTestGrid shore(6, 6, "Base-85");
    for (int i = 0; i < 5; ++i) {
        shore.at(i, 0) = "Coast-Water-T-02";
        shore.at(5, i + 1) = "Coast-Water-R-01";
    }
    shore.at(5, 0) = "Coast-Water-TR-01";
    expect(shore.river(2, 0).allowed() && shore.at(2, 0) == "River-Mouth-T-01", "a river on Coast-Water-T is River-Mouth-T");
    expect(shore.river(5, 3).allowed() && shore.at(5, 3) == "River-Mouth-R-01", "a river on Coast-Water-R is River-Mouth-R");
    const galaxy::RiverPlan corner = shore.river(5, 0);
    expect(!corner.allowed() && !corner.reason.empty() && shore.at(5, 0) == "Coast-Water-TR-01", "a river on a coast corner does nothing");
    shore.river(2, 1);
    expect(shore.at(2, 1) == "River-Vertical-01" && shore.at(2, 0) == "River-Mouth-T-01", "a river below a mouth runs straight");
    shore.river(2, 2);
    shore.river(3, 2);
    shore.river(4, 2);
    expect(shore.at(2, 2) == "River-T-R-01" && shore.at(3, 2) == "River-Horizontal-01" && shore.at(4, 2) == "River-Horizontal-01", "the river bends away from the mouth");
    shore.river(4, 3);
    expect(shore.at(4, 3) == "River-T-R-01" && shore.at(4, 2) == "River-L-B-01" && shore.at(5, 3) == "River-Mouth-R-01", "a river runs into the side of the mouth that is open");

    // A river beside a mouth's closed side does not join it.
    RiverTestGrid side(5, 3, "Base-85");
    side.at(2, 0) = "Coast-Water-T-01";
    side.river(2, 0);
    side.river(1, 0);
    expect(side.at(2, 0) == "River-Mouth-T-01" && side.at(1, 0) == "River-Base-01", "a river on the side of a mouth leaves it alone");
    side.river(2, 1);
    expect(side.at(2, 0) == "River-Mouth-T-01" && side.at(2, 1) == "River-Vertical-01", "the cell below the mouth joins it");

    // Everything else refuses, and says why.
    RiverTestGrid mixed(4, 1, "Base-85");
    mixed.at(0, 0) = "Base-219";
    mixed.at(1, 0) = "Base-68";
    mixed.at(2, 0) = "Base-0";
    mixed.at(3, 0) = "Cliff-High-TL-01";
    for (int x = 0; x < 4; ++x) {
        const std::string held = mixed.at(x, 0);
        const galaxy::RiverPlan plan = mixed.river(x, 0);
        expect(!plan.allowed() && !plan.reason.empty() && plan.edits.empty() && mixed.at(x, 0) == held, "rivers do nothing on " + held);
    }
    expect(galaxy::planRiver(0, 0, [&](int cx, int cy) { return mixed.look(cx, cy); }).reason.find("Base-219") != std::string::npos, "the reason names the base");
    expect(!galaxy::planRiver(9, 9, [&](int cx, int cy) { return mixed.look(cx, cy); }).allowed(), "off the grid does nothing");

    // Bridges.
    RiverTestGrid span(5, 5, "Base-85");
    span.river(0, 2);
    span.river(1, 2);
    span.river(2, 2);
    expect(span.bridge(1, 2).allowed() && span.at(1, 2) == "RiverB-Horizontal-01", "a bridge goes over River-Horizontal as RiverB-Horizontal");
    const galaxy::RiverPlan twice = span.bridge(1, 2);
    expect(!twice.allowed() && twice.reason == "A bridge is already here", "a second bridge is refused");
    expect(!span.bridge(3, 3).allowed() && span.at(3, 3) == "Base-85", "a bridge on Base-85 does nothing");
    RiverTestGrid wet(3, 3, "Base-85");
    wet.at(1, 0) = "Coast-Water-T-01";
    wet.at(0, 0) = "Base-0";
    expect(!wet.bridge(1, 0).allowed() && !wet.bridge(0, 0).allowed(), "bridges refuse coasts and water");
    RiverTestGrid fork(5, 5, "Base-85");
    fork.river(2, 2);
    expect(!fork.bridge(2, 2).allowed() && fork.at(2, 2) == "River-Base-01", "a pond takes no bridge");
    fork.river(1, 2);
    fork.river(3, 2);
    fork.river(2, 1);
    expect(!fork.bridge(2, 2).allowed() && fork.at(2, 2) == "RiverIntersect-3way-T-L-R-01", "a junction takes no bridge");
    RiverTestGrid bend(4, 4, "Base-85");
    bend.river(1, 1);
    bend.river(2, 1);
    bend.river(1, 2);
    expect(bend.at(1, 1) == "River-R-B-01" && bend.bridge(1, 1).allowed() && bend.at(1, 1) == "RiverB-R-B-01", "a bend takes a bend bridge");
    // A bridge keeps its shape: a river painted beside it does not join the closed side.
    bend.river(0, 1);
    expect(bend.at(1, 1) == "RiverB-R-B-01", "a bridge is not redrawn by its neighbours");
    expect(bend.at(0, 1) == "River-Base-01", "a river beside a bridge's closed side stays a pond");
    bend.river(1, 3);
    expect(bend.at(1, 2) == "River-Vertical-01" && bend.at(1, 3) == "River-Vertical-01", "a river joins the open side of a bridge");
    const galaxy::RiverPlan over = bend.river(1, 1);
    expect(!over.allowed() && bend.at(1, 1) == "RiverB-R-B-01", "the river brush leaves a bridge alone");

    // Recognising cells on a map by their pictures.
    galaxy::RiverCatalog catalog;
    const galaxy::SectionStamp grassStamp = galaxy::loadSectionStamp(buildSct(3, 16, 16, 1, 85, 167));
    const galaxy::SectionStamp bigStamp = galaxy::loadSectionStamp(buildSct(3, 32, 32, 1, 85, 12));
    const galaxy::SectionStamp riverStamp = galaxy::loadSectionStamp(buildSct(3, 16, 16, 1, 40, 90));
    catalog.add(grassStamp, "Base-85");
    catalog.add(bigStamp, "Base-68");
    catalog.add(riverStamp, "River-Horizontal-01");
    galaxy::LoadedMap grassMap = blankMap(32, 167);
    expect(catalog.classify(grassMap, 0, 0).kind == galaxy::RiverCell::Kind::Land && catalog.classify(grassMap, 1, 1).kind == galaxy::RiverCell::Kind::Land, "a map cell with a section's pictures is recognised");
    galaxy::LoadedMap deepMap = blankMap(32, 12);
    expect(catalog.classify(deepMap, 1, 0).label == "Base-68", "every cell of a large stroke is recognised");
    galaxy::LoadedMap riverMap = blankMap(32, 90);
    const galaxy::RiverCell found = catalog.classify(riverMap, 0, 1);
    expect(found.kind == galaxy::RiverCell::Kind::River && found.piece.key == "River-Horizontal", "a river cell is read back as its piece");
    galaxy::LoadedMap strangeMap = blankMap(32, 3);
    expect(catalog.classify(strangeMap, 0, 0).kind == galaxy::RiverCell::Kind::Other && catalog.classify(strangeMap, 0, 0).label.empty(), "unknown pictures are not recognised");
    expect(catalog.classify(grassMap, 2, 0).kind == galaxy::RiverCell::Kind::Other && catalog.classify(grassMap, -1, 0).kind == galaxy::RiverCell::Kind::Other, "cells outside the map are not recognised");
    // Base-85 and Base-219 share their pictures. The height of the cell tells them apart.
    galaxy::RiverCatalog twins;
    twins.add(galaxy::loadSectionStamp(buildSct(3, 16, 16, 1, 85, 167)), "Base-85");
    twins.add(galaxy::loadSectionStamp(buildSct(3, 16, 16, 1, 219, 167)), "Base-219");
    galaxy::LoadedMap lowMap = blankMap(16, 167);
    std::fill(lowMap.heights.begin(), lowMap.heights.end(), static_cast<std::uint8_t>(85));
    galaxy::LoadedMap highMap = blankMap(16, 167);
    std::fill(highMap.heights.begin(), highMap.heights.end(), static_cast<std::uint8_t>(219));
    expect(twins.classify(lowMap, 0, 0).kind == galaxy::RiverCell::Kind::Land, "a cell at height 85 is Base-85");
    expect(twins.classify(highMap, 0, 0).kind == galaxy::RiverCell::Kind::Other && twins.classify(highMap, 0, 0).label == "Base-219", "a cell at height 219 is Base-219 even though the pictures match");
    std::fill(lowMap.heights.begin(), lowMap.heights.end(), static_cast<std::uint8_t>(90));
    expect(twins.classify(lowMap, 0, 0).kind == galaxy::RiverCell::Kind::Land, "a slightly raised Base-85 cell is still Base-85");
    grassMap.tileImages.assign(2048, 167);
    grassMap.tileCount = 2;
    grassMap.indices[0] = 1;
    grassMap.tileImages[1024 + 7] = 5;
    expect(catalog.classify(grassMap, 0, 0).kind == galaxy::RiverCell::Kind::Other && catalog.classify(grassMap, 1, 0).kind == galaxy::RiverCell::Kind::Land, "one changed tile makes the cell unrecognised");
}

void testRidges() {
    using galaxy::kRiverBottom;
    using galaxy::kRiverLeft;
    using galaxy::kRiverRight;
    using galaxy::kRiverTop;

    // Piece names.
    const galaxy::RiverPiece end = galaxy::parseRidgePiece("Ridge-End-B-02");
    expect(end.valid && end.flexible && end.sides == kRiverBottom && end.key == "Ridge-End-B" && end.family == galaxy::LineFamily::Ridge, "Ridge-End-B leaves through the bottom");
    expect(galaxy::parseRidgePiece("Ridge-Base").valid && galaxy::parseRidgePiece("Ridge-Base").sides == 0, "Ridge-Base stands alone");
    expect(galaxy::parseRidgePiece("Ridge-Horizontal-03").sides == (kRiverLeft | kRiverRight) && galaxy::parseRidgePiece("Ridge-Vertical-04").sides == (kRiverTop | kRiverBottom), "straight ridges");
    expect(galaxy::parseRidgePiece("Ridge-Corner-T-R").sides == (kRiverTop | kRiverRight) && galaxy::parseRidgePiece("Ridge-Corner-L-B").sides == (kRiverLeft | kRiverBottom), "corners name their sides");
    expect(galaxy::parseRidgePiece("Ridge-3Way-L-R-B").sides == (kRiverLeft | kRiverRight | kRiverBottom) && galaxy::parseRidgePiece("Ridge-4Way").sides == galaxy::kRiverAllSides, "junctions");
    const galaxy::RiverPiece ridgeMouth = galaxy::parseRidgePiece("Ridge-Mouth-T");
    expect(ridgeMouth.valid && ridgeMouth.mouth && !ridgeMouth.flexible && ridgeMouth.sides == kRiverBottom, "Ridge-Mouth-T has high ground on top and the ridge below");
    expect(galaxy::parseRidgePiece("Ridge-Mouth-R").sides == kRiverLeft, "Ridge-Mouth-R takes the ridge from the left");
    const galaxy::RiverPiece spacerPiece = galaxy::parseRidgePiece("Ridge-L_R-Spacer-01");
    expect(spacerPiece.valid && spacerPiece.bridge && !spacerPiece.flexible && spacerPiece.sides == (kRiverLeft | kRiverRight), "Ridge-L_R-Spacer is a fixed piece");
    expect(galaxy::parseRidgePiece("Ridge-T_B-Spacer-01").sides == (kRiverTop | kRiverBottom), "Ridge-T_B-Spacer joins top and bottom");
    expect(!galaxy::parseRidgePiece("River-Horizontal-01").valid && !galaxy::parseRiverPiece("Ridge-Horizontal-01").valid && !galaxy::parseRidgePiece("Ridge-Weird").valid, "rivers and ridges keep their own names");

    // The piece for every set of sides exists, and ridges end instead of carrying on.
    expect(galaxy::ridgeKeyForSides(0) == "Ridge-Base" && galaxy::ridgeKeyForSides(kRiverTop) == "Ridge-End-T" && galaxy::ridgeKeyForSides(kRiverLeft) == "Ridge-End-L", "one side is an end");
    expect(galaxy::ridgeKeyForSides(kRiverTop | kRiverBottom) == "Ridge-Vertical" && galaxy::ridgeKeyForSides(kRiverLeft | kRiverRight) == "Ridge-Horizontal", "opposite sides are straight");
    expect(galaxy::ridgeKeyForSides(kRiverTop | kRiverRight) == "Ridge-Corner-T-R" && galaxy::ridgeKeyForSides(kRiverRight | kRiverBottom) == "Ridge-Corner-R-B"
            && galaxy::ridgeKeyForSides(kRiverLeft | kRiverBottom) == "Ridge-Corner-L-B" && galaxy::ridgeKeyForSides(kRiverTop | kRiverLeft) == "Ridge-Corner-T-L",
        "all four corners");
    expect(galaxy::ridgeKeyForSides(galaxy::kRiverAllSides) == "Ridge-4Way" && galaxy::ridgeKeyForSides(kRiverTop | kRiverLeft | kRiverRight) == "Ridge-3Way-T-L-R", "junctions follow their sides");
    for (int sides = 0; sides < 16; ++sides) {
        const galaxy::RiverPiece piece = galaxy::parseRidgePiece(galaxy::ridgeKeyForSides(sides));
        expect(piece.valid && piece.flexible && piece.sides == sides, "every side set has a ridge piece that matches it");
    }
    expect(galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-Vertical-02")) == "Ridge-T_B-Spacer", "a spacer fills a vertical ridge");
    expect(galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-Horizontal-01")) == "Ridge-L_R-Spacer", "a spacer fills a horizontal ridge");
    expect(galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-End-T-01")).empty() && galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-Corner-T-R")).empty()
            && galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-4Way")).empty() && galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-Mouth-B")).empty()
            && galaxy::ridgeSpacerKey(galaxy::parseRidgePiece("Ridge-T_B-Spacer-01")).empty(),
        "only straight ridges take a spacer");

    // Cells.
    const galaxy::RiverCell cliff = galaxy::classifyRiverLabel("Cliff-High-L-02");
    expect(cliff.kind == galaxy::RiverCell::Kind::Cliff && cliff.waterSide == kRiverLeft, "a straight cliff takes a mouth");
    expect(galaxy::classifyRiverLabel("Cliff-High-BL-01").kind == galaxy::RiverCell::Kind::Other && galaxy::classifyRiverLabel("Cliff-Low-TL-01").kind == galaxy::RiverCell::Kind::Other, "cliff corners do not");
    expect(galaxy::classifyRiverLabel("Ridge-Corner-L-B-01").kind == galaxy::RiverCell::Kind::Ridge && galaxy::classifyRiverLabel("River-L-B-01").kind == galaxy::RiverCell::Kind::River, "ridge and river cells are told apart");

    // Drawing a ridge east.
    RiverTestGrid lone(5, 5, "Base-85");
    expect(lone.ridge(2, 2).allowed() && lone.at(2, 2) == "Ridge-Base-01", "a lone ridge stands alone");
    RiverTestGrid line(7, 5, "Base-85");
    line.ridge(1, 2);
    line.ridge(2, 2);
    expect(line.at(1, 2) == "Ridge-End-R-01" && line.at(2, 2) == "Ridge-End-L-01", "two ridge cells make two ends");
    line.ridge(3, 2);
    expect(line.at(1, 2) == "Ridge-End-R-01" && line.at(2, 2) == "Ridge-Horizontal-01" && line.at(3, 2) == "Ridge-End-L-01", "a longer ridge is straight between its ends");
    line.ridge(3, 3);
    expect(line.at(3, 2) == "Ridge-Corner-L-B-01" && line.at(3, 3) == "Ridge-End-T-01", "turning south makes a corner");
    line.ridge(4, 2);
    expect(line.at(3, 2) == "Ridge-3Way-L-R-B-01" && line.at(4, 2) == "Ridge-End-L-01", "a branch makes a 3-way");
    line.ridge(3, 1);
    expect(line.at(3, 2) == "Ridge-4Way-01", "four neighbours make the 4-way");
    const galaxy::RiverPlan again = line.ridge(3, 2);
    expect(!again.allowed() && again.reason == "A ridge is already here", "a ridge on a ridge is a no-op");

    // Mouths on cliffs.
    RiverTestGrid mesa(6, 6, "Base-85");
    for (int x = 0; x < 6; ++x) {
        mesa.at(x, 0) = "Cliff-High-T-02";
    }
    mesa.at(5, 0) = "Cliff-High-TR-01";
    mesa.at(0, 3) = "Cliff-High-L-01";
    expect(mesa.ridge(2, 0).allowed() && mesa.at(2, 0) == "Ridge-Mouth-T-01", "a ridge on Cliff-High-T is Ridge-Mouth-T");
    expect(mesa.ridge(0, 3).allowed() && mesa.at(0, 3) == "Ridge-Mouth-L-01", "a ridge on Cliff-High-L is Ridge-Mouth-L");
    const galaxy::RiverPlan cliffCorner = mesa.ridge(5, 0);
    expect(!cliffCorner.allowed() && !cliffCorner.reason.empty() && mesa.at(5, 0) == "Cliff-High-TR-01", "a ridge on a cliff corner does nothing");
    mesa.ridge(2, 1);
    mesa.ridge(2, 2);
    mesa.ridge(2, 3);
    expect(mesa.at(2, 1) == "Ridge-Vertical-01" && mesa.at(2, 2) == "Ridge-Vertical-01" && mesa.at(2, 3) == "Ridge-End-T-01", "the ridge runs away from its mouth");
    mesa.ridge(1, 3);
    expect(mesa.at(1, 3) == "Ridge-Horizontal-01" && mesa.at(0, 3) == "Ridge-Mouth-L-01" && mesa.at(2, 3) == "Ridge-Corner-T-L-01", "a second mouth joins the same ridge");
    RiverTestGrid side(5, 3, "Base-85");
    side.at(2, 0) = "Cliff-High-T-01";
    side.ridge(2, 0);
    side.ridge(1, 0);
    expect(side.at(2, 0) == "Ridge-Mouth-T-01" && side.at(1, 0) == "Ridge-Base-01", "a ridge beside a mouth's closed side leaves it alone");

    // Ridges and rivers never mix.
    RiverTestGrid mixed(8, 1, "Base-85");
    mixed.at(0, 0) = "Base-219";
    mixed.at(1, 0) = "Base-68";
    mixed.at(2, 0) = "Base-0";
    mixed.at(3, 0) = "Coast-Water-T-01";
    mixed.at(4, 0) = "River-Horizontal-01";
    mixed.at(5, 0) = "Cliff-High-BL-01";
    mixed.at(6, 0) = "Cliff-Low-TL-01";
    for (int x = 0; x < 7; ++x) {
        const std::string held = mixed.at(x, 0);
        const galaxy::RiverPlan plan = mixed.ridge(x, 0);
        expect(!plan.allowed() && !plan.reason.empty() && plan.edits.empty() && mixed.at(x, 0) == held, "ridges do nothing on " + held);
    }
    expect(galaxy::planRidge(0, 0, [&](int cx, int cy) { return mixed.look(cx, cy); }).reason.find("Base-219") != std::string::npos, "the reason names the base");
    RiverTestGrid crossed(4, 1, "Base-85");
    crossed.at(0, 0) = "Ridge-Horizontal-01";
    crossed.at(1, 0) = "Cliff-High-T-01";
    expect(!crossed.river(0, 0).allowed() && !crossed.river(1, 0).allowed() && crossed.at(0, 0) == "Ridge-Horizontal-01", "rivers do nothing on a ridge or a cliff");
    expect(!crossed.bridge(0, 0).allowed(), "a river bridge does nothing on a ridge");
    RiverTestGrid beside(4, 1, "Base-85");
    beside.river(1, 0);
    beside.ridge(2, 0);
    expect(beside.at(1, 0) == "River-Base-01" && beside.at(2, 0) == "Ridge-Base-01", "a ridge beside a river does not join it");

    // Spacers.
    RiverTestGrid gap(5, 5, "Base-85");
    gap.ridge(1, 2);
    gap.ridge(2, 2);
    gap.ridge(3, 2);
    expect(gap.spacer(2, 2).allowed() && gap.at(2, 2) == "Ridge-L_R-Spacer-01", "a spacer goes on Ridge-Horizontal");
    const galaxy::RiverPlan twice = gap.spacer(2, 2);
    expect(!twice.allowed() && twice.reason == "A spacer is already here", "a second spacer is refused");
    expect(!gap.spacer(1, 2).allowed() && gap.at(1, 2) == "Ridge-End-R-01", "an end takes no spacer");
    expect(!gap.spacer(0, 0).allowed() && gap.at(0, 0) == "Base-85", "a spacer on Base-85 does nothing");
    expect(!gap.ridge(2, 2).allowed() && gap.at(2, 2) == "Ridge-L_R-Spacer-01", "the ridge brush leaves a spacer alone");
    gap.ridge(4, 2);
    expect(gap.at(2, 2) == "Ridge-L_R-Spacer-01" && gap.at(3, 2) == "Ridge-Horizontal-01", "a spacer is not redrawn by its neighbours");
    gap.ridge(2, 3);
    expect(gap.at(2, 3) == "Ridge-Base-01", "a ridge beside a spacer's closed side stays alone");
    RiverTestGrid tall(4, 5, "Base-85");
    tall.ridge(1, 1);
    tall.ridge(1, 2);
    tall.ridge(1, 3);
    expect(tall.spacer(1, 2).allowed() && tall.at(1, 2) == "Ridge-T_B-Spacer-01", "a spacer goes on Ridge-Vertical");
    RiverTestGrid bendGrid(4, 4, "Base-85");
    bendGrid.ridge(1, 1);
    bendGrid.ridge(2, 1);
    bendGrid.ridge(1, 2);
    expect(!bendGrid.spacer(1, 1).allowed() && bendGrid.at(1, 1) == "Ridge-Corner-R-B-01", "a corner takes no spacer");
    RiverTestGrid wall(3, 3, "Base-85");
    wall.at(1, 0) = "Cliff-High-T-01";
    wall.ridge(1, 0);
    expect(!wall.spacer(1, 0).allowed() && !wall.spacer(0, 0).allowed(), "spacers refuse mouths and base tiles");
}

// A small grid of cliff and slope labels. Clicking applies a slope plan.
struct SlopeTestGrid {
    int width = 0;
    int height = 0;
    std::vector<std::string> labels;

    SlopeTestGrid(int w, int h, const std::string& fill) : width(w), height(h), labels(static_cast<std::size_t>(w * h), fill) {}

    std::string& at(int x, int y) { return labels[static_cast<std::size_t>(y * width + x)]; }

    galaxy::RiverCell look(int x, int y) const {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return galaxy::RiverCell{};
        }
        return galaxy::classifyRiverLabel(labels[static_cast<std::size_t>(y * width + x)]);
    }

    galaxy::RiverPlan slope(int x, int y, const std::function<bool(std::string_view)>& available = {}) {
        const galaxy::RiverPlan plan = galaxy::planSlope(x, y, [&](int cx, int cy) { return look(cx, cy); }, available);
        if (plan.allowed()) {
            for (const galaxy::RiverEdit& edit : plan.edits) {
                at(edit.x, edit.y) = edit.key;
            }
        }
        return plan;
    }
};

void testSlopes() {
    using galaxy::kCornerAll;
    using galaxy::kCornerBottomLeft;
    using galaxy::kCornerBottomRight;
    using galaxy::kCornerTopLeft;
    using galaxy::kCornerTopRight;

    const galaxy::SlopeShape corner = galaxy::parseSlopeShape("Cliff-High-BL-02");
    expect(corner.valid && !corner.slope && corner.mask == kCornerBottomLeft, "Cliff-High-BL is the bottom-left high corner");
    const galaxy::SlopeShape ramp = galaxy::parseSlopeShape("Slope-High-B-TransitionL-01");
    expect(ramp.valid && ramp.slope && ramp.mask == (kCornerBottomLeft | kCornerBottomRight), "a bottom transition is still a bottom cliff");
    const galaxy::SlopeShape low = galaxy::parseSlopeShape("Slope-Low-TL");
    expect(low.valid && low.slope && low.mask == (kCornerAll & ~kCornerTopLeft), "Slope-Low-TL is high ground everywhere except the top left");
    const galaxy::SlopeShape single = galaxy::parseSlopeShape("Single-High-TR");
    expect(single.valid && single.slope && single.mask == kCornerTopRight, "Single-High-TR is the top-right high corner");
    expect(!galaxy::parseSlopeShape("Base-85").valid && !galaxy::parseSlopeShape("Coast-Water-T").valid && !galaxy::parseSlopeShape("Cliff-High-TL_and_BR").valid, "bases, coasts and diagonals are not slopes");

    // Every cliff shape, with every way its sides can already be slopes, names a real slope tile.
    const int masks[] = {
        kCornerTopLeft | kCornerTopRight,
        kCornerBottomLeft | kCornerBottomRight,
        kCornerTopLeft | kCornerBottomLeft,
        kCornerTopRight | kCornerBottomRight,
        kCornerTopLeft,
        kCornerTopRight,
        kCornerBottomLeft,
        kCornerBottomRight,
        kCornerAll & ~kCornerTopLeft,
        kCornerAll & ~kCornerTopRight,
        kCornerAll & ~kCornerBottomLeft,
        kCornerAll & ~kCornerBottomRight,
    };
    const std::set<std::string> archive = {
        "Slope-High-T", "Slope-High-B", "Slope-High-L", "Slope-High-R",
        "Slope-High-T-TransitionL", "Slope-High-T-TransitionR",
        "Slope-High-B-TransitionL", "Slope-High-B-TransitionR",
        "Slope-High-L-TransitionT", "Slope-High-L-TransitionB",
        "Slope-High-R-TransitionT", "Slope-High-R-TransitionB",
        "Slope-High-TL", "Slope-High-TR", "Slope-High-BL", "Slope-High-BR",
        "Slope-Low-TL", "Slope-Low-TR", "Slope-Low-BL", "Slope-Low-BR",
        "Slope-Low-TL-TransitionL", "Slope-Low-TL-TransitionT",
        "Slope-Low-TR-TransitionT", "Slope-Low-TR-TransitionR",
        "Slope-Low-BL-TransitionB", "Slope-Low-BL-TransitionL",
        "Slope-Low-BR-TransitionR", "Slope-Low-BR-TransitionB",
        "Single-High-T", "Single-High-B", "Single-High-L", "Single-High-R",
        "Single-High-TL", "Single-High-TR", "Single-High-BL", "Single-High-BR",
    };
    std::set<std::string> made;
    bool roundTrip = true;
    for (const int mask : masks) {
        std::vector<int> arms;
        for (int bit = 1; bit <= galaxy::kRiverAllSides; bit <<= 1) {
            if ((galaxy::slopeCliffSides(mask) & bit) != 0) {
                arms.push_back(bit);
            }
        }
        std::vector<int> subsets{0};
        for (const int bit : arms) {
            subsets.push_back(bit);
        }
        if (arms.size() == 2) {
            subsets.push_back(arms[0] | arms[1]);
        }
        for (const int continuing : subsets) {
            const std::string name = galaxy::slopePieceName(mask, continuing);
            made.insert(name);
            const galaxy::SlopeShape parsed = galaxy::parseSlopeShape(name);
            roundTrip = roundTrip && parsed.valid && parsed.slope && parsed.mask == mask && archive.count(name) == 1;
        }
    }
    expect(roundTrip && made == archive, "every slope the brush can ask for is one of the slope tiles");

    SlopeTestGrid alone(3, 3, "Base-85");
    alone.at(1, 1) = "Cliff-High-BL";
    expect(alone.slope(1, 1).allowed() && alone.at(1, 1) == "Single-High-BL", "an isolated Cliff-High-BL becomes Single-High-BL, got " + alone.at(1, 1));
    SlopeTestGrid straight(3, 3, "Base-85");
    straight.at(1, 1) = "Cliff-High-B";
    expect(straight.slope(1, 1).allowed() && straight.at(1, 1) == "Single-High-B", "an isolated Cliff-High-B becomes Single-High-B, got " + straight.at(1, 1));
    SlopeTestGrid topRight(3, 3, "Base-85");
    topRight.at(1, 1) = "Cliff-High-TR";
    expect(topRight.slope(1, 1).allowed() && topRight.at(1, 1) == "Single-High-TR", "an isolated Cliff-High-TR becomes Single-High-TR");
    SlopeTestGrid pit(3, 3, "Base-219");
    pit.at(1, 1) = "Cliff-Low-TL";
    expect(pit.slope(1, 1).allowed() && pit.at(1, 1) == "Slope-Low-TL", "an isolated Cliff-Low-TL becomes Slope-Low-TL, got " + pit.at(1, 1));

    // Two bottom cliffs, clicked one after the other. The gentle sides face each other.
    SlopeTestGrid pair(5, 3, "Base-85");
    pair.at(1, 1) = "Cliff-High-B";
    pair.at(2, 1) = "Cliff-High-B";
    pair.at(3, 1) = "Cliff-High-B";
    expect(pair.slope(1, 1).allowed() && pair.at(1, 1) == "Single-High-B" && pair.at(2, 1) == "Cliff-High-B", "the first click converts only that cliff");
    expect(pair.slope(2, 1).allowed() && pair.at(1, 1) == "Slope-High-B-TransitionR" && pair.at(2, 1) == "Slope-High-B-TransitionL", "two slopes meet gently, got " + pair.at(1, 1) + " and " + pair.at(2, 1));
    expect(pair.at(3, 1) == "Cliff-High-B", "a cliff that was not clicked stays a cliff");
    expect(pair.slope(3, 1).allowed() && pair.at(1, 1) == "Slope-High-B-TransitionR" && pair.at(2, 1) == "Slope-High-B" && pair.at(3, 1) == "Slope-High-B-TransitionL", "three slopes are a transition, a ramp and a transition, got " + pair.at(1, 1) + " " + pair.at(2, 1) + " " + pair.at(3, 1));
    const galaxy::RiverPlan again = pair.slope(2, 1);
    expect(!again.allowed() && again.reason.find("already") != std::string::npos && pair.at(2, 1) == "Slope-High-B", "clicking a finished slope changes nothing");

    // A plateau corner becomes Slope-High-BL only once both arms are slopes.
    SlopeTestGrid plateau(4, 5, "Base-85");
    plateau.at(1, 2) = "Cliff-High-B";
    plateau.at(2, 2) = "Cliff-High-BL";
    plateau.at(2, 3) = "Cliff-High-L";
    expect(plateau.slope(2, 2).allowed() && plateau.at(2, 2) == "Single-High-BL" && plateau.at(1, 2) == "Cliff-High-B" && plateau.at(2, 3) == "Cliff-High-L", "clicking only the corner leaves the cliff arms alone");
    plateau.slope(1, 2);
    plateau.slope(2, 3);
    expect(plateau.at(2, 2) == "Slope-High-BL" && plateau.at(1, 2) == "Slope-High-B-TransitionR" && plateau.at(2, 3) == "Slope-High-L-TransitionT", "both arms make Slope-High-BL, got " + plateau.at(2, 2) + " " + plateau.at(1, 2) + " " + plateau.at(2, 3));

    // High-TR becomes Slope-High-TR once both arms are slopes, the same as the other high corners.
    SlopeTestGrid topRightCorner(5, 3, "Base-85");
    topRightCorner.at(2, 1) = "Cliff-High-TR";
    topRightCorner.at(3, 1) = "Cliff-High-T";
    topRightCorner.at(2, 0) = "Cliff-High-R";
    expect(topRightCorner.slope(2, 1).allowed() && topRightCorner.at(2, 1) == "Single-High-TR" && topRightCorner.at(3, 1) == "Cliff-High-T" && topRightCorner.at(2, 0) == "Cliff-High-R", "clicking only High-TR leaves its cliff arms alone");
    topRightCorner.slope(3, 1);
    topRightCorner.slope(2, 0);
    expect(topRightCorner.at(2, 1) == "Slope-High-TR" && topRightCorner.at(3, 1) == "Slope-High-T-TransitionL" && topRightCorner.at(2, 0) == "Slope-High-R-TransitionB", "both arms make Slope-High-TR, got " + topRightCorner.at(2, 1) + " " + topRightCorner.at(3, 1) + " " + topRightCorner.at(2, 0));

    // A low corner with one arm keeps that arm gentle and the other side steep.
    SlopeTestGrid lowArm(4, 4, "Base-219");
    lowArm.at(2, 2) = "Cliff-Low-TL";
    lowArm.at(1, 2) = "Cliff-High-B";
    lowArm.slope(2, 2);
    lowArm.slope(1, 2);
    expect(lowArm.at(2, 2) == "Slope-Low-TL-TransitionL" && lowArm.at(1, 2) == "Slope-High-B-TransitionR", "one arm of a low corner is a transition, got " + lowArm.at(2, 2) + " " + lowArm.at(1, 2));
    lowArm.at(2, 1) = "Cliff-High-R";
    lowArm.slope(2, 1);
    expect(lowArm.at(2, 2) == "Slope-Low-TL" && lowArm.at(1, 2) == "Slope-High-B-TransitionR" && lowArm.at(2, 1) == "Slope-High-R-TransitionB", "both arms of a low corner make Slope-Low, got " + lowArm.at(2, 2) + " " + lowArm.at(2, 1));

    SlopeTestGrid refused(3, 3, "Base-85");
    refused.at(0, 0) = "Base-219";
    refused.at(1, 0) = "Base-68";
    refused.at(2, 0) = "Base-0";
    refused.at(0, 1) = "Coast-Water-T";
    refused.at(1, 1) = "Ridge-Horizontal";
    const galaxy::RiverPlan land = refused.slope(1, 2);
    const galaxy::RiverPlan high = refused.slope(0, 0);
    const galaxy::RiverPlan shallow = refused.slope(1, 0);
    const galaxy::RiverPlan coast = refused.slope(0, 1);
    expect(!land.allowed() && land.reason.find("Base-85") != std::string::npos, "slopes refuse Base-85");
    expect(!high.allowed() && high.reason.find("Base-219") != std::string::npos, "slopes refuse Base-219");
    expect(!shallow.allowed() && shallow.reason.find("Base-68") != std::string::npos, "slopes refuse Base-68");
    expect(!coast.allowed() && coast.reason.find("Coast-Water-T") != std::string::npos, "slopes refuse a coast");
    expect(!refused.slope(1, 1).allowed() && refused.at(1, 1) == "Ridge-Horizontal" && refused.at(1, 2) == "Base-85", "a refused click leaves the cells alone");

    // Mars has the straight singles and no run pieces, so neighbouring slopes stay
    // singles on the 16 x 16 cells. A corner cliff is refused.
    const auto marsSlope = [](std::string_view key) {
        return key == "Single-High-B" || key == "Single-High-T" || key == "Single-High-L" || key == "Single-High-R"
            || key == "Single-High-TL" || key == "Single-High-TR" || key == "Single-High-BL" || key == "Single-High-BR";
    };
    SlopeTestGrid marsPair(4, 3, "Base-85");
    marsPair.at(1, 1) = "Cliff-High-B-01";
    marsPair.at(2, 1) = "Cliff-High-B-01";
    expect(marsPair.slope(1, 1, marsSlope).allowed() && marsPair.at(1, 1) == "Single-High-B" && marsPair.at(2, 1) == "Cliff-High-B-01", "the first Mars slope leaves the next cliff alone");
    expect(marsPair.slope(2, 1, marsSlope).allowed() && marsPair.at(1, 1) == "Single-High-B" && marsPair.at(2, 1) == "Single-High-B", "Mars slopes sit beside each other as singles, got " + marsPair.at(1, 1) + " and " + marsPair.at(2, 1));
    SlopeTestGrid marsCorner(3, 3, "Base-85");
    marsCorner.at(1, 1) = "Cliff-Low-BR-01";
    const galaxy::RiverPlan marsLow = marsCorner.slope(1, 1, marsSlope);
    expect(!marsLow.allowed() && marsLow.reason.find("Cliff-Low-BR") != std::string::npos && marsLow.reason.find("straight") != std::string::npos, "a Mars low corner is refused, got " + marsLow.reason);
    marsCorner.at(1, 1) = "Cliff-High-BL-01";
    const galaxy::RiverPlan marsHigh = marsCorner.slope(1, 1, marsSlope);
    expect(!marsHigh.allowed() && marsHigh.reason.find("Cliff-High-BL") != std::string::npos && marsCorner.at(1, 1) == "Cliff-High-BL-01", "a Mars high corner is refused");

    const auto everySlope = [](std::string_view) { return true; };
    SlopeTestGrid stillJoins(4, 3, "Base-85");
    stillJoins.at(1, 1) = "Cliff-High-B";
    stillJoins.at(2, 1) = "Cliff-High-B";
    stillJoins.slope(1, 1, everySlope);
    expect(stillJoins.slope(2, 1, everySlope).allowed() && stillJoins.at(1, 1) == "Slope-High-B-TransitionR" && stillJoins.at(2, 1) == "Slope-High-B-TransitionL", "a full slope set still joins neighbours");
}

int main() {
    testBrushStrokeNames();
    testBrushCells();
    testRivers();
    testRidges();
    testSlopes();
    testFavorites();
    testArchiveDocument();
    testBlankMap();
    testArchiveFooterAndStarts();
    testTileOpacity();
    testWideTilePool();
    testBackupNames();
    testPlacementHistory();
    testStampAboveMap();
    testTerrainClipboard();
    testHeightClipboard();
    testSelectionMask();
    testAdjustSelectionHeight();
    testBlendSelectionTextures();
    testFeaturePlacement();
    testFeatureUndo();
    testFeatureBatchPaste();
    testGafImages();
    testMapSettings();
    testEditorSettings();
    testLanguage();
    testGameFolder();
    testVersion();
    testAppearanceSettings();
    testSeaSettings();
    testOrbitCamera();
    testResizeMap();
    testSyntheticSections();
    testHeightGrid();

    const std::filesystem::path maps = GALAXY_EXAMPLE_MAPS;
    const std::filesystem::path tileset = std::filesystem::path(GALAXY_SOURCE_DIR) / "GreenWorld.hpi";

    expect(std::filesystem::exists(tileset), "GreenWorld.hpi is in the GalaxyEditor directory");
    galaxy::TilesetLibrary library;
    library.addArchive(tileset);
    expect(library.archives().size() == 1, "one tileset archive");
    expect(library.sectionCount() == 453, "GreenWorld section count");
    const auto counts = library.archives().front().categoryCounts();
    expect(counts.count("GreenWorld") == 1, "world name GreenWorld");
    expect(counts.at("GreenWorld").at("Flat") == 5, "Flat category");
    expect(counts.at("GreenWorld").at("Hills") == 72, "Hills category");
    testGenerate(library, maps);
    testAcidFlatGround(maps);
    testAcidFamily(maps);

    const auto grass = galaxy::loadMapFile(maps / "64x64_SimpleFlatMapWithGrass.ufo");
    expect(grass.tileWidth == 64 && grass.tileHeight == 64, "grass size");
    expect(grass.tileCount == 17, "grass tile pool");
    expect(grass.seaLevel == 75, "grass sea level");
    expect(grass.features.empty(), "grass has no features");
    expect(!grass.heights.empty() && grass.heights.front() == 85, "grass height");
    expect(grass.missionName == "Untitled Map", "grass mission name");
    expectMatchesBmp(grass, maps / "64x64_SimpleFlatMapWithGrass.bmp");
    testGreenFlat(library, grass, maps);

    const auto rendered = galaxy::renderMapRgba(grass);
    expect(rendered.size() == static_cast<std::size_t>(grass.pixelWidth() * grass.pixelHeight() * 4), "rgba size");
    expect(rendered[0] == galaxy::kTaPalette[grass.tileImages[grass.indices[0] * 1024]][0], "rendered red channel");
    expect(galaxy::kTaPalette[167][0] == 47 && galaxy::kTaPalette[167][1] == 119 && galaxy::kTaPalette[167][2] == 27, "grass palette color");

    const auto water = galaxy::loadMapFile(maps / "64x64_MapWithGrassAndWater.ufo");
    expect(water.tileCount == 499, "water tile pool");
    expectMatchesBmp(water, maps / "64x64_MapWithGrassAndWater.bmp");

    const auto rocks = galaxy::loadMapFile(maps / "64x64_SimpleFlatMapWithGrass_3Rocks_5Trees.ufo");
    expect(rocks.features.size() == 8, "eight features");
    expect(rocks.features[0].name == "Rock", "first feature is Rock");

    const auto starts = galaxy::loadMapFile(maps / "64x64_SimpleFlatMapWithGrass_With_Player1AndPlayer2StartPositions.ufo");
    expect(starts.starts.size() == 2, "two start positions");
    expect(starts.starts[0].player == 1 && starts.starts[0].x == 448 && starts.starts[0].y == 555, "player 1");
    expect(starts.starts[1].player == 2 && starts.starts[1].x == 455 && starts.starts[1].y == 1326, "player 2");

    const auto savedir = std::filesystem::temp_directory_path();
    auto edited = grass;
    edited.heights[0] = 90;
    const std::size_t editedPixel = static_cast<std::size_t>(edited.indices[0]) * 1024;
    edited.tileImages[editedPixel] = static_cast<std::uint8_t>(edited.tileImages[editedPixel] ^ 0x5A);
    testSaveRoundTrip(edited, savedir / "galaxy-grass-edit.ufo", "edited grass");
    testSaveRoundTrip(rocks, savedir / "galaxy-rocks.ufo", "rocks");
    testSaveRoundTrip(starts, savedir / "galaxy-starts.tnt", "starts tnt");

    const auto big = galaxy::loadMapFile(maps / "96x96_MapWithGrassAndWater.ufo");
    expect(big.tileWidth == 96 && big.tileHeight == 96, "96 map size");
    expectMatchesBmp(big, maps / "96x96_MapWithGrassAndWater.bmp");

    std::vector<std::uint8_t> view(64 * 64 * 4);
    galaxy::renderMapView(grass, view.data(), 64, 64, 64.0f, 64.0f, -320.0f, -128.0f, 1.0f);
    const IndexedBmp grassBmp = readBmp8(maps / "64x64_SimpleFlatMapWithGrass.bmp");
    int viewMismatches = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            const std::uint8_t index = grassBmp.pixels[static_cast<std::size_t>(y + 128) * grassBmp.width + static_cast<std::size_t>(x + 320)];
            const std::uint8_t* rgb = galaxy::kTaPalette[index];
            const std::uint8_t* got = view.data() + (static_cast<std::size_t>(y * 64 + x) * 4);
            if (got[0] != rgb[0] || got[1] != rgb[1] || got[2] != rgb[2]) {
                ++viewMismatches;
            }
        }
    }
    expect(viewMismatches == 0, "viewport sample differs in " + std::to_string(viewMismatches) + " pixels");

    const auto huge = galaxy::loadMapFile(maps / "ASSHOLE.ufo");
    expect(huge.tileWidth == 620 && huge.tileHeight == 256, "ASSHOLE size");
    expect(huge.pixelWidth() == 19840, "ASSHOLE is wider than a single texture");
    expect(!huge.features.empty(), "ASSHOLE features");
    expect(!huge.starts.empty(), "ASSHOLE starts");
    const float fit = std::min(320.0f / huge.pixelWidth(), 128.0f / huge.pixelHeight());
    std::vector<std::uint8_t> overview(320 * 128 * 4);
    galaxy::renderMapView(huge, overview.data(), 320, 128, 320.0f, 128.0f, 0.0f, 0.0f, fit);
    int unique = 0;
    std::uint8_t seen[256] = {};
    for (std::size_t i = 0; i < overview.size(); i += 4) {
        const std::uint8_t key = overview[i] ^ overview[i + 1] ^ overview[i + 2];
        if (seen[key] == 0) {
            seen[key] = 1;
            ++unique;
        }
    }
    expect(unique > 8, "ASSHOLE overview has terrain color");

    if (failures == 0) {
        std::cout << "All map format checks passed\n";
        return 0;
    }
    std::cerr << failures << " checks failed\n";
    return 1;
}

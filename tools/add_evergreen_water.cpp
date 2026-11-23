// Copies Evergreen water, shores, and the Base-190 cliff into the Tile Brush favorites.
//
// galaxy_add_evergreen_water <favorites.hpi> <UTAS part 3.hpi> <U_EverGreen.hpi> <out.hpi>
//
// The sections are not in the Galaxy Editor repo. UTAS Green SET part 3 holds the shallow
// water (height 65), the shallow-to-deep pieces, and both grasses' coasts. U_EverGreen
// holds the height-190 grass flat and the Cliffs_86 faces that join it to Base-85. There
// is no folder named Cliffs_190 for this grass: Snow_cliffs_190 is snow from 190 to 255
// and is not copied. Sections that are not one of the twelve brush masks are left out.

#include "core/BrushStrokes.hpp"
#include "format/HpiArchive.hpp"
#include "format/SctSection.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

std::string lower(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string slashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool inFolder(const std::string& path, const std::string& folder) {
    return lower(slashes(path)).find("/" + lower(folder) + "/") != std::string::npos;
}

int bandOf(int height) {
    if (height <= 8) {
        return 0;
    }
    if (height >= 60 && height <= 70) {
        return 65;
    }
    if (height >= 78 && height <= 92) {
        return 85;
    }
    if (height >= 180 && height <= 205) {
        return 190;
    }
    return -1;
}

int cornerBand(const galaxy::SectionStamp& stamp, int corner) {
    const int width = stamp.tileWidth * 2;
    const int height = stamp.tileHeight * 2;
    const int x0 = (corner & 1) != 0 ? width - 1 : 0;
    const int y0 = (corner & 2) != 0 ? height - 1 : 0;
    const int dx = (corner & 1) != 0 ? -1 : 1;
    const int dy = (corner & 2) != 0 ? -1 : 1;
    std::map<int, int> counts;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            const int hx = x0 + x * dx;
            const int hy = y0 + y * dy;
            if (hx < 0 || hy < 0 || hx >= width || hy >= height) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(hy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(hx);
            if (index < stamp.heights.size()) {
                ++counts[stamp.heights[index]];
            }
        }
    }
    int mode = -1;
    int most = 0;
    for (const auto& [height, count] : counts) {
        if (count > most) {
            most = count;
            mode = height;
        }
    }
    return bandOf(mode);
}

// The mask marks the high band. Empty when a corner is neither band, or the layout has no piece.
std::string maskName(const galaxy::SectionStamp& stamp, galaxy::TransitionStyle style, int high, int low) {
    if (stamp.tileWidth != 16 || stamp.tileHeight != 16) {
        return {};
    }
    int mask = 0;
    for (int corner = 0; corner < 4; ++corner) {
        const int band = cornerBand(stamp, corner);
        if (band == high) {
            mask |= 1 << corner;
        } else if (band != low) {
            return {};
        }
    }
    return galaxy::transitionPieceName(style, mask);
}

struct Copied {
    std::string source;
    std::vector<std::uint8_t> bytes;
};

bool hasPath(const std::vector<galaxy::HpiWriteFile>& files, const std::string& path) {
    const std::string wanted = lower(path);
    for (const galaxy::HpiWriteFile& file : files) {
        if (lower(slashes(file.path)) == wanted) {
            return true;
        }
    }
    return false;
}

int addGroup(const galaxy::HpiArchive& source, const std::string& folder, galaxy::TransitionStyle style, int high, int low,
    const std::string& outFolder, std::vector<galaxy::HpiWriteFile>& files) {
    std::map<std::string, std::vector<Copied>> groups;
    std::vector<const galaxy::HpiFile*> sections;
    for (const galaxy::HpiFile& file : source.files()) {
        if (inFolder(file.path, folder) && lower(file.path).ends_with(".sct")) {
            sections.push_back(&file);
        }
    }
    std::sort(sections.begin(), sections.end(), [](const galaxy::HpiFile* left, const galaxy::HpiFile* right) {
        return lower(left->path) < lower(right->path);
    });
    int skipped = 0;
    for (const galaxy::HpiFile* file : sections) {
        try {
            const std::vector<std::uint8_t> bytes = source.extract(*file);
            const std::string name = maskName(galaxy::loadSectionStamp(bytes), style, high, low);
            if (name.empty()) {
                ++skipped;
                continue;
            }
            groups[name].push_back(Copied{file->path, bytes});
        } catch (const std::exception& ex) {
            std::cerr << "skip " << file->path << ": " << ex.what() << "\n";
            ++skipped;
        }
    }
    int added = 0;
    const int wantedMasks[] = {1, 2, 4, 8, 3, 12, 5, 10, 14, 13, 11, 7};
    for (const int mask : wantedMasks) {
        const std::string name = galaxy::transitionPieceName(style, mask);
        if (groups.find(name) == groups.end()) {
            std::cerr << "missing " << outFolder << name << "\n";
        }
    }
    for (const auto& [name, copies] : groups) {
        int variation = 1;
        for (const Copied& copy : copies) {
            char digits[8];
            std::snprintf(digits, sizeof(digits), "-%02d", variation++);
            const std::string path = outFolder + name + digits + ".sct";
            if (hasPath(files, path)) {
                continue;
            }
            files.push_back(galaxy::HpiWriteFile{path, copy.bytes});
            ++added;
        }
        std::cout << outFolder << name << " x" << copies.size() << "\n";
    }
    std::cout << folder << ": kept " << added << ", left out " << skipped << "\n";
    return added;
}

galaxy::SectionStamp repeatTile(const galaxy::SectionStamp& tile, int height) {
    galaxy::SectionStamp flat;
    flat.version = 3;
    flat.tileWidth = 16;
    flat.tileHeight = 16;
    flat.tileCount = 1;
    flat.tileImages = tile.tileImages;
    flat.indices.assign(256, 0);
    flat.heights.assign(1024, static_cast<std::uint8_t>(height));
    return flat;
}

// A real map tile whose four corners are the same band, repeated to the brush's 16 x 16 cell.
// The source packs have no flat Base-65 section. The tile itself is already shallow water.
bool addRepeatedFlat(const galaxy::HpiArchive& source, const std::string& folder, int band, const std::string& outPath,
    std::vector<galaxy::HpiWriteFile>& files) {
    if (hasPath(files, outPath)) {
        std::cout << outPath << " already present\n";
        return true;
    }
    std::vector<const galaxy::HpiFile*> sections;
    for (const galaxy::HpiFile& file : source.files()) {
        if (inFolder(file.path, folder) && lower(file.path).ends_with(".sct")) {
            sections.push_back(&file);
        }
    }
    std::sort(sections.begin(), sections.end(), [](const galaxy::HpiFile* left, const galaxy::HpiFile* right) {
        return lower(left->path) < lower(right->path);
    });
    int bestDistance = -1;
    galaxy::SectionStamp best;
    std::string bestSource;
    int bestX = 0;
    int bestY = 0;
    for (const galaxy::HpiFile* file : sections) {
        const galaxy::SectionStamp stamp = galaxy::loadSectionStamp(source.extract(*file));
        if (stamp.tileWidth <= 0 || stamp.heightsOnly) {
            continue;
        }
        const int width = stamp.tileWidth * 2;
        for (int ty = 0; ty < stamp.tileHeight; ++ty) {
            for (int tx = 0; tx < stamp.tileWidth; ++tx) {
                bool pure = true;
                for (int dy = 0; dy < 2 && pure; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const std::size_t index = static_cast<std::size_t>(ty * 2 + dy) * static_cast<std::size_t>(width)
                            + static_cast<std::size_t>(tx * 2 + dx);
                        pure = index < stamp.heights.size() && bandOf(stamp.heights[index]) == band;
                    }
                }
                if (!pure) {
                    continue;
                }
                int distance = 1000;
                for (int oy = 0; oy < stamp.tileHeight; ++oy) {
                    for (int ox = 0; ox < stamp.tileWidth; ++ox) {
                        bool same = true;
                        for (int dy = 0; dy < 2 && same; ++dy) {
                            for (int dx = 0; dx < 2; ++dx) {
                                const std::size_t index = static_cast<std::size_t>(oy * 2 + dy) * static_cast<std::size_t>(width)
                                    + static_cast<std::size_t>(ox * 2 + dx);
                                same = index < stamp.heights.size() && bandOf(stamp.heights[index]) == band;
                            }
                        }
                        if (!same) {
                            distance = std::min(distance, std::abs(ox - tx) + std::abs(oy - ty));
                        }
                    }
                }
                if (distance <= bestDistance) {
                    continue;
                }
                const galaxy::SectionStamp one = galaxy::cropSectionStamp(stamp, tx, ty, 1, 1);
                if (one.tileCount != 1 || one.tileImages.size() != 1024) {
                    continue;
                }
                bestDistance = distance;
                best = one;
                bestSource = file->path;
                bestX = tx;
                bestY = ty;
            }
        }
    }
    if (bestDistance < 0) {
        std::cerr << "missing a height-" << band << " tile in " << folder << "\n";
        return false;
    }
    files.push_back(galaxy::HpiWriteFile{outPath, galaxy::writeSectionStamp(repeatTile(best, band))});
    std::cout << outPath << " from " << bestSource << " tile " << bestX << "," << bestY << "\n";
    return true;
}

bool addWholeFlat(const galaxy::HpiArchive& source, const std::string& fileName, int band, const std::string& outPath,
    std::vector<galaxy::HpiWriteFile>& files) {
    if (hasPath(files, outPath)) {
        std::cout << outPath << " already present\n";
        return true;
    }
    for (const galaxy::HpiFile& file : source.files()) {
        if (!lower(slashes(file.path)).ends_with("/" + lower(fileName))) {
            continue;
        }
        const std::vector<std::uint8_t> bytes = source.extract(file);
        const galaxy::SectionStamp stamp = galaxy::loadSectionStamp(bytes);
        int matched = 0;
        for (const std::uint8_t height : stamp.heights) {
            matched += bandOf(height) == band ? 1 : 0;
        }
        if (stamp.tileWidth != 16 || stamp.tileHeight != 16 || stamp.heights.empty()
            || matched * 100 < static_cast<int>(stamp.heights.size()) * 98) {
            std::cerr << file.path << " is not a flat height " << band << "\n";
            return false;
        }
        files.push_back(galaxy::HpiWriteFile{outPath, bytes});
        std::cout << outPath << " from " << file.path << "\n";
        return true;
    }
    std::cerr << "missing " << fileName << "\n";
    return false;
}

void addStroke(const std::string& basePath, const std::string& strokePath, std::vector<galaxy::HpiWriteFile>& files) {
    if (hasPath(files, strokePath)) {
        return;
    }
    for (const galaxy::HpiWriteFile& file : files) {
        if (lower(slashes(file.path)) == lower(basePath)) {
            files.push_back(galaxy::HpiWriteFile{strokePath, file.bytes});
            std::cout << strokePath << "\n";
            return;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: galaxy_add_evergreen_water <favorites.hpi> <UTAS-part3.hpi> <U-EverGreen.hpi> <out.hpi>\n";
        return 2;
    }
    try {
        const galaxy::HpiArchive favorites = galaxy::HpiArchive::open(argv[1]);
        const galaxy::HpiArchive utas = galaxy::HpiArchive::open(argv[2]);
        const galaxy::HpiArchive evergreen = galaxy::HpiArchive::open(argv[3]);
        std::vector<galaxy::HpiWriteFile> files;
        files.reserve(favorites.files().size() + 400);
        for (const galaxy::HpiFile& file : favorites.files()) {
            files.push_back(galaxy::HpiWriteFile{file.path, favorites.extract(file)});
        }

        const int shallowDeep = addGroup(utas, "Shallow_deep", galaxy::TransitionStyle::ShallowDeep, 65, 0,
            "sections/ShallowDeepTiles/Evergreen/", files);
        const int shallowEg = addGroup(utas, "Shallow coast_eg", galaxy::TransitionStyle::ShallowCoast, 85, 65,
            "sections/ShallowCoastTiles/Evergreen/", files);
        const int shallowEg2 = addGroup(utas, "Shallow coast_eg2", galaxy::TransitionStyle::ShallowCoast, 85, 65,
            "sections/ShallowCoastTiles/Evergreen2/", files);
        const int coastEg = addGroup(utas, "Low coast_EG", galaxy::TransitionStyle::Coast, 85, 0,
            "sections/CoastTiles/Evergreen/", files);
        const int coastEg2 = addGroup(utas, "Low coast_EG2", galaxy::TransitionStyle::Coast, 85, 0,
            "sections/CoastTiles/Evergreen2/", files);
        const int cliffs = addGroup(evergreen, "Cliffs_86", galaxy::TransitionStyle::Cliff, 190, 85,
            "sections/CliffTiles/Evergreen/", files);

        bool flats = true;
        flats = addRepeatedFlat(utas, "Shallow_deep", 0, "sections/BaseTiles/Evergreen/Base-0.sct", files) && flats;
        flats = addRepeatedFlat(utas, "Shallow_deep", 65, "sections/BaseTiles/Evergreen/Base-65.sct", files) && flats;
        flats = addWholeFlat(evergreen, "Flat_Grass_189.sct", 190, "sections/BaseTiles/Evergreen/Base-190.sct", files) && flats;
        flats = addWholeFlat(evergreen, "Flat_Grass_86.sct", 85, "sections/BaseTiles/Evergreen/Base-85.sct", files) && flats;
        flats = addRepeatedFlat(utas, "Shallow coast_eg2", 85, "sections/BaseTiles/Evergreen2/Base-85.sct", files) && flats;

        addStroke("sections/BaseTiles/Evergreen/Base-0.sct", "sections/BaseTileBrushStrokes/Evergreen/Base-0-16x16-01.sct", files);
        addStroke("sections/BaseTiles/Evergreen/Base-65.sct", "sections/BaseTileBrushStrokes/Evergreen/Base-65-16x16-01.sct", files);
        addStroke("sections/BaseTiles/Evergreen/Base-190.sct", "sections/BaseTileBrushStrokes/Evergreen/Base-190-16x16-01.sct", files);
        addStroke("sections/BaseTiles/Evergreen/Base-85.sct", "sections/BaseTileBrushStrokes/Evergreen/Base-85-16x16-01.sct", files);
        addStroke("sections/BaseTiles/Evergreen2/Base-85.sct", "sections/BaseTileBrushStrokes/Evergreen2/Base-85-16x16-01.sct", files);

        galaxy::writeHpiArchive(argv[4], std::move(files));
        std::cout << "wrote " << argv[4] << "\n";
        if (!flats || shallowDeep == 0 || shallowEg == 0 || shallowEg2 == 0 || coastEg == 0 || coastEg2 == 0 || cliffs == 0) {
            return 1;
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

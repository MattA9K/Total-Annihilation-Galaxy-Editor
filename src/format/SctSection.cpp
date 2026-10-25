#include "format/SctSection.hpp"

#include "format/TaPalette.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace galaxy {
namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::uint32_t readU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::int16_t readI16(const std::uint8_t* data) {
    return static_cast<std::int16_t>(data[0] | (data[1] << 8));
}

bool fits(std::size_t fileSize, std::uint32_t offset, std::uint64_t count) {
    if (static_cast<std::uint64_t>(offset) > fileSize) {
        return false;
    }
    return count <= static_cast<std::uint64_t>(fileSize) - offset;
}

void writePixel(std::uint8_t* dst, std::uint8_t index) {
    dst[0] = kTaPalette[index][0];
    dst[1] = kTaPalette[index][1];
    dst[2] = kTaPalette[index][2];
    dst[3] = 255;
}

std::uint64_t hashTile(const std::uint8_t* bytes) {
    std::uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 1024; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

bool stampIncludes(const SectionStamp& section, int tileX, int tileY) {
    if (section.tileMask.empty()) {
        return true;
    }
    if (tileX < 0 || tileY < 0 || tileX >= section.tileWidth || tileY >= section.tileHeight) {
        return false;
    }
    const std::size_t index = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(section.tileWidth) + static_cast<std::size_t>(tileX);
    return index < section.tileMask.size() && section.tileMask[index] != 0;
}

SectionStamp loadSectionStamp(const std::vector<std::uint8_t>& bytes) {
    require(bytes.size() >= 28, "Section file is too small");
    const std::uint32_t version = readU32(bytes.data());
    require(version == 2 || version == 3, "Section file has an unexpected version");

    const std::uint32_t ptrMini = readU32(bytes.data() + 4);
    const std::uint32_t tileCount = readU32(bytes.data() + 8);
    const std::uint32_t ptrTiles = readU32(bytes.data() + 12);
    const std::uint32_t width = readU32(bytes.data() + 16);
    const std::uint32_t height = readU32(bytes.data() + 20);
    const std::uint32_t ptrData = readU32(bytes.data() + 24);

    require(width > 0 && height > 0 && width <= 512 && height <= 512, "Section is larger than 512 tiles");
    require(tileCount > 0 && tileCount <= 65535, "Section tile count is invalid");

    const std::uint64_t cellCount = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t indexBytes = cellCount * 2;
    const int stride = version == 2 ? 8 : 4;
    const std::uint64_t attrCount = static_cast<std::uint64_t>(width) * 2 * height * 2;
    const std::uint64_t tileBytes = static_cast<std::uint64_t>(tileCount) * 1024;
    const std::uint64_t minimapBytes = static_cast<std::uint64_t>(kSectionMinimap) * kSectionMinimap;

    require(fits(bytes.size(), ptrMini, minimapBytes), "Section minimap extends past the file");
    require(fits(bytes.size(), ptrTiles, tileBytes), "Section tiles extend past the file");
    require(fits(bytes.size(), ptrData, indexBytes), "Section tile grid extends past the file");
    const std::uint64_t heightAt = static_cast<std::uint64_t>(ptrData) + indexBytes;
    require(heightAt <= bytes.size() && attrCount * static_cast<std::uint64_t>(stride) <= bytes.size() - heightAt,
        "Section height grid extends past the file");

    SectionStamp stamp;
    stamp.version = static_cast<int>(version);
    stamp.tileWidth = static_cast<int>(width);
    stamp.tileHeight = static_cast<int>(height);
    stamp.tileCount = static_cast<int>(tileCount);
    stamp.minimap.assign(bytes.begin() + ptrMini, bytes.begin() + static_cast<std::ptrdiff_t>(ptrMini + minimapBytes));
    stamp.tileImages.assign(bytes.begin() + ptrTiles, bytes.begin() + static_cast<std::ptrdiff_t>(ptrTiles + tileBytes));
    stamp.indices.resize(static_cast<std::size_t>(cellCount));
    for (std::size_t i = 0; i < stamp.indices.size(); ++i) {
        const std::int16_t index = readI16(bytes.data() + ptrData + i * 2);
        require(index >= 0 && index < stamp.tileCount, "Section tile index is out of range");
        stamp.indices[i] = static_cast<std::uint16_t>(index);
    }
    stamp.heights.resize(static_cast<std::size_t>(attrCount));
    for (std::size_t i = 0; i < stamp.heights.size(); ++i) {
        stamp.heights[i] = bytes[static_cast<std::size_t>(heightAt) + i * static_cast<std::size_t>(stride)];
    }
    return stamp;
}

std::vector<std::uint8_t> renderMinimapRgba(const SectionStamp& section) {
    const std::size_t count = static_cast<std::size_t>(kSectionMinimap) * kSectionMinimap;
    std::vector<std::uint8_t> rgba(count * 4, 0);
    const std::size_t available = std::min(section.minimap.size(), count);
    for (std::size_t i = 0; i < available; ++i) {
        writePixel(rgba.data() + i * 4, section.minimap[i]);
    }
    for (std::size_t i = available; i < count; ++i) {
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

std::vector<std::uint8_t> renderSectionRgba(const SectionStamp& section, int maxEdge, int& width, int& height) {
    const int pixelW = std::max(section.tileWidth, 0) * 32;
    const int pixelH = std::max(section.tileHeight, 0) * 32;
    int step = 1;
    if (maxEdge > 0 && pixelW > 0 && pixelH > 0) {
        const int edge = std::max(pixelW, pixelH);
        if (edge > maxEdge) {
            step = (edge + maxEdge - 1) / maxEdge;
        }
    }
    width = std::max(1, pixelW / step);
    height = std::max(1, pixelH / step);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    if (pixelW <= 0 || pixelH <= 0 || section.indices.empty() || section.tileImages.empty()) {
        return rgba;
    }

    for (int y = 0; y < height; ++y) {
        const int mapY = std::min(y * step, pixelH - 1);
        const int tileY = mapY >> 5;
        const int pixelY = mapY & 31;
        for (int x = 0; x < width; ++x) {
            const int mapX = std::min(x * step, pixelW - 1);
            const int tileX = mapX >> 5;
            const int pixelX = mapX & 31;
            if (tileX < section.tileWidth && tileY < section.tileHeight && !stampIncludes(section, tileX, tileY)) {
                continue;
            }
            std::uint8_t paletteIndex = 0;
            if (tileX < section.tileWidth && tileY < section.tileHeight) {
                const std::uint32_t index = section.indices[static_cast<std::size_t>(tileY) * static_cast<std::size_t>(section.tileWidth) + static_cast<std::size_t>(tileX)];
                if (index < static_cast<std::uint32_t>(section.tileCount)) {
                    paletteIndex = section.tileImages[static_cast<std::size_t>(index) * 1024 + static_cast<std::size_t>(pixelY * 32 + pixelX)];
                }
            }
            writePixel(rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4, paletteIndex);
        }
    }
    return rgba;
}

int stampSection(LoadedMap& map, const SectionStamp& section, int originTileX, int originTileY, TileDeduper* deduper, int opacityPercent) {
    require(section.tileWidth > 0 && section.tileHeight > 0, "Section has no tiles");
    const std::size_t cellCount = static_cast<std::size_t>(section.tileWidth) * static_cast<std::size_t>(section.tileHeight);
    const std::size_t heightCount = static_cast<std::size_t>(section.tileWidth) * 2 * static_cast<std::size_t>(section.tileHeight) * 2;
    require(section.heights.size() == heightCount, "Section height grid is incomplete");
    require(map.tileWidth > 0 && map.tileHeight > 0, "Map has no tiles");
    require(map.indices.size() == static_cast<std::size_t>(map.tileWidth) * static_cast<std::size_t>(map.tileHeight), "Map tile grid is incomplete");
    require(map.heights.size() == static_cast<std::size_t>(map.heightWidth()) * static_cast<std::size_t>(map.heightHeight()), "Map height grid is incomplete");
    require(map.tileImages.size() >= static_cast<std::size_t>(map.tileCount) * 1024, "Map tile pool is incomplete");

    const int opacity = std::clamp(opacityPercent, 0, 100);
    if (section.heightsOnly) {
        if (opacity <= 0) {
            return 0;
        }
        auto blendByte = [&](std::uint8_t destination, std::uint8_t source) -> std::uint8_t {
            return static_cast<std::uint8_t>((static_cast<int>(destination) * (100 - opacity) + static_cast<int>(source) * opacity + 50) / 100);
        };
        const int sectionStride = section.tileWidth * 2;
        const int mapStride = map.heightWidth();
        int written = 0;
        for (int sy = 0; sy < section.tileHeight; ++sy) {
            for (int sx = 0; sx < section.tileWidth; ++sx) {
                const int mx = originTileX + sx;
                const int my = originTileY + sy;
                if (mx < 0 || my < 0 || mx >= map.tileWidth || my >= map.tileHeight || !stampIncludes(section, sx, sy)) {
                    continue;
                }
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        const std::size_t src = static_cast<std::size_t>(sy * 2 + dy) * static_cast<std::size_t>(sectionStride)
                            + static_cast<std::size_t>(sx * 2 + dx);
                        const std::size_t dst = static_cast<std::size_t>(my * 2 + dy) * static_cast<std::size_t>(mapStride)
                            + static_cast<std::size_t>(mx * 2 + dx);
                        map.heights[dst] = blendByte(map.heights[dst], section.heights[src]);
                    }
                }
                ++written;
            }
        }
        return written;
    }

    require(section.tileCount > 0, "Section has no tiles");
    require(section.indices.size() == cellCount, "Section grid is incomplete");
    require(section.tileImages.size() == static_cast<std::size_t>(section.tileCount) * 1024, "Section tile pool is incomplete");

    if (deduper != nullptr && (!deduper->ready || deduper->tileCount != map.tileCount)) {
        deduper->buckets.clear();
        for (int index = 0; index < map.tileCount; ++index) {
            const std::uint8_t* tile = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
            deduper->buckets[hashTile(tile)].push_back(static_cast<std::uint32_t>(index));
        }
        deduper->ready = true;
        deduper->tileCount = map.tileCount;
    }

    if (opacity <= 0) {
        return 0;
    }

    std::vector<int> resolved(static_cast<std::size_t>(section.tileCount), -1);
    std::vector<std::uint8_t> addedImages;
    const int poolStart = map.tileCount;
    int nextIndex = poolStart;

    auto resolveImage = [&](const std::uint8_t* src) -> int {
        auto matches = [&](const std::uint8_t* existing) {
            return std::memcmp(existing, src, 1024) == 0;
        };
        if (deduper != nullptr) {
            const auto found = deduper->buckets.find(hashTile(src));
            if (found != deduper->buckets.end()) {
                for (const std::uint32_t index : found->second) {
                    if (index < static_cast<std::uint32_t>(poolStart)
                        && matches(map.tileImages.data() + static_cast<std::size_t>(index) * 1024)) {
                        return static_cast<int>(index);
                    }
                }
            }
        } else {
            for (int index = 0; index < poolStart; ++index) {
                if (matches(map.tileImages.data() + static_cast<std::size_t>(index) * 1024)) {
                    return index;
                }
            }
        }
        for (int index = poolStart; index < nextIndex; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index - poolStart) * 1024;
            if (matches(addedImages.data() + offset)) {
                return index;
            }
        }
        require(nextIndex < std::numeric_limits<int>::max(), "The tile pool is full");
        const int slot = nextIndex;
        ++nextIndex;
        addedImages.insert(addedImages.end(), src, src + 1024);
        return slot;
    };

    auto nearestPalette = [](int red, int green, int blue) -> std::uint8_t {
        int best = 0;
        int bestDistance = std::numeric_limits<int>::max();
        for (int index = 0; index < 256; ++index) {
            const int dr = static_cast<int>(kTaPalette[index][0]) - red;
            const int dg = static_cast<int>(kTaPalette[index][1]) - green;
            const int db = static_cast<int>(kTaPalette[index][2]) - blue;
            const int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return static_cast<std::uint8_t>(best);
    };
    auto blendTile = [&](const std::uint8_t* destination, const std::uint8_t* source, std::uint8_t* out) {
        const int keep = 100 - opacity;
        for (int pixel = 0; pixel < 1024; ++pixel) {
            if (destination[pixel] == source[pixel]) {
                out[pixel] = destination[pixel];
                continue;
            }
            const std::uint8_t* oldColor = kTaPalette[destination[pixel]];
            const std::uint8_t* newColor = kTaPalette[source[pixel]];
            const int red = (static_cast<int>(oldColor[0]) * keep + static_cast<int>(newColor[0]) * opacity + 50) / 100;
            const int green = (static_cast<int>(oldColor[1]) * keep + static_cast<int>(newColor[1]) * opacity + 50) / 100;
            const int blue = (static_cast<int>(oldColor[2]) * keep + static_cast<int>(newColor[2]) * opacity + 50) / 100;
            out[pixel] = nearestPalette(red, green, blue);
        }
    };
    auto blendByte = [&](std::uint8_t destination, std::uint8_t source) -> std::uint8_t {
        return static_cast<std::uint8_t>((static_cast<int>(destination) * (100 - opacity) + static_cast<int>(source) * opacity + 50) / 100);
    };
    auto resolveLocal = [&](std::uint32_t local) -> int {
        int& slot = resolved[local];
        if (slot >= 0) {
            return slot;
        }
        slot = resolveImage(section.tileImages.data() + static_cast<std::size_t>(local) * 1024);
        return slot;
    };

    struct Cell {
        int mx;
        int my;
        int sx;
        int sy;
        int slot;
    };
    std::vector<Cell> cells;
    cells.reserve(cellCount);
    for (int sy = 0; sy < section.tileHeight; ++sy) {
        const int my = originTileY + sy;
        if (my < 0 || my >= map.tileHeight) {
            continue;
        }
        for (int sx = 0; sx < section.tileWidth; ++sx) {
            const int mx = originTileX + sx;
            if (mx < 0 || mx >= map.tileWidth || !stampIncludes(section, sx, sy)) {
                continue;
            }
            const std::uint32_t local = section.indices[static_cast<std::size_t>(sy) * static_cast<std::size_t>(section.tileWidth) + static_cast<std::size_t>(sx)];
            require(local < static_cast<std::uint32_t>(section.tileCount), "Section tile index is out of range");
            int slot = 0;
            if (opacity >= 100) {
                slot = resolveLocal(local);
            } else {
                const std::uint32_t destinationIndex = map.indices[static_cast<std::size_t>(my) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(mx)];
                require(destinationIndex < static_cast<std::uint32_t>(poolStart), "Map tile index is out of range");
                std::uint8_t blended[1024];
                blendTile(
                    map.tileImages.data() + static_cast<std::size_t>(destinationIndex) * 1024,
                    section.tileImages.data() + static_cast<std::size_t>(local) * 1024,
                    blended);
                slot = resolveImage(blended);
            }
            cells.push_back(Cell{mx, my, sx, sy, slot});
        }
    }

    if (!addedImages.empty()) {
        const std::size_t needed = static_cast<std::size_t>(nextIndex) * 1024;
        if (map.tileImages.size() < needed) {
            map.tileImages.resize(needed);
        }
        std::memcpy(map.tileImages.data() + static_cast<std::size_t>(poolStart) * 1024, addedImages.data(), addedImages.size());
        map.tileCount = nextIndex;
        if (deduper != nullptr) {
            for (int index = poolStart; index < nextIndex; ++index) {
                const std::uint8_t* src = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
                deduper->buckets[hashTile(src)].push_back(static_cast<std::uint32_t>(index));
            }
            deduper->tileCount = nextIndex;
        }
    }

    const int sectionStride = section.tileWidth * 2;
    const int mapStride = map.heightWidth();
    for (const Cell& cell : cells) {
        map.indices[static_cast<std::size_t>(cell.my) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(cell.mx)] =
            static_cast<std::uint32_t>(cell.slot);
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                const std::size_t src = static_cast<std::size_t>(cell.sy * 2 + dy) * static_cast<std::size_t>(sectionStride)
                    + static_cast<std::size_t>(cell.sx * 2 + dx);
                const std::size_t dst = static_cast<std::size_t>(cell.my * 2 + dy) * static_cast<std::size_t>(mapStride)
                    + static_cast<std::size_t>(cell.mx * 2 + dx);
                map.heights[dst] = blendByte(map.heights[dst], section.heights[src]);
            }
        }
    }
    return static_cast<int>(cells.size());
}

std::optional<TilePatch> captureTilePatch(const LoadedMap& map, int originTileX, int originTileY, int tileWidth, int tileHeight) {
    if (map.tileWidth <= 0 || map.tileHeight <= 0 || tileWidth <= 0 || tileHeight <= 0) {
        return std::nullopt;
    }
    const int x0 = std::max(0, originTileX);
    const int y0 = std::max(0, originTileY);
    const int x1 = std::min(map.tileWidth, originTileX + tileWidth);
    const int y1 = std::min(map.tileHeight, originTileY + tileHeight);
    if (x0 >= x1 || y0 >= y1) {
        return std::nullopt;
    }

    TilePatch patch;
    patch.x = x0;
    patch.y = y0;
    patch.width = x1 - x0;
    patch.height = y1 - y0;
    patch.tileCount = map.tileCount;
    patch.indices.resize(static_cast<std::size_t>(patch.width) * static_cast<std::size_t>(patch.height));
    for (int y = 0; y < patch.height; ++y) {
        for (int x = 0; x < patch.width; ++x) {
            const std::size_t src = static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(x0 + x);
            require(src < map.indices.size(), "Map tile grid is incomplete");
            patch.indices[static_cast<std::size_t>(y * patch.width + x)] = map.indices[src];
        }
    }

    const int heightW = patch.width * 2;
    const int heightH = patch.height * 2;
    const int mapStride = map.heightWidth();
    patch.heights.resize(static_cast<std::size_t>(heightW) * static_cast<std::size_t>(heightH));
    for (int y = 0; y < heightH; ++y) {
        for (int x = 0; x < heightW; ++x) {
            const std::size_t src = static_cast<std::size_t>(y0 * 2 + y) * static_cast<std::size_t>(mapStride) + static_cast<std::size_t>(x0 * 2 + x);
            require(src < map.heights.size(), "Map height grid is incomplete");
            patch.heights[static_cast<std::size_t>(y * heightW + x)] = map.heights[src];
        }
    }
    return patch;
}

namespace {

void paintTilePatch(LoadedMap& map, const TilePatch& patch) {
    require(patch.width > 0 && patch.height > 0, "Tile edit is empty");
    require(patch.x >= 0 && patch.y >= 0, "Tile edit is outside the map");
    require(patch.x + patch.width <= map.tileWidth && patch.y + patch.height <= map.tileHeight, "Tile edit is outside the map");
    require(patch.indices.size() == static_cast<std::size_t>(patch.width) * static_cast<std::size_t>(patch.height), "Tile edit grid is incomplete");
    const int heightW = patch.width * 2;
    const int heightH = patch.height * 2;
    require(patch.heights.size() == static_cast<std::size_t>(heightW) * static_cast<std::size_t>(heightH), "Tile edit heights are incomplete");
    require(patch.tileCount > 0 && map.tileImages.size() >= static_cast<std::size_t>(patch.tileCount) * 1024, "Tile edit pool is missing images");
    for (const std::uint32_t index : patch.indices) {
        require(index < static_cast<std::uint32_t>(patch.tileCount), "Tile edit points outside the tile pool");
    }

    for (int y = 0; y < patch.height; ++y) {
        for (int x = 0; x < patch.width; ++x) {
            map.indices[static_cast<std::size_t>(patch.y + y) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(patch.x + x)] =
                patch.indices[static_cast<std::size_t>(y * patch.width + x)];
        }
    }
    const int mapStride = map.heightWidth();
    for (int y = 0; y < heightH; ++y) {
        for (int x = 0; x < heightW; ++x) {
            map.heights[static_cast<std::size_t>(patch.y * 2 + y) * static_cast<std::size_t>(mapStride) + static_cast<std::size_t>(patch.x * 2 + x)] =
                patch.heights[static_cast<std::size_t>(y * heightW + x)];
        }
    }
}

void requireSameRegion(const PlacementChange& change) {
    require(change.before.x == change.after.x && change.before.y == change.after.y
            && change.before.width == change.after.width && change.before.height == change.after.height,
        "Tile edit region does not match");
    require(change.after.tileCount >= change.before.tileCount, "Tile edit pool is invalid");
    const auto added = static_cast<std::size_t>(change.after.tileCount - change.before.tileCount) * 1024;
    require(change.addedTiles.size() == added, "Tile edit images are incomplete");
}

}  // namespace

std::optional<PlacementChange> stampSectionChange(LoadedMap& map, const SectionStamp& section, int originTileX, int originTileY, TileDeduper* deduper, int opacityPercent) {
    auto before = captureTilePatch(map, originTileX, originTileY, section.tileWidth, section.tileHeight);
    if (!before) {
        return std::nullopt;
    }
    const int written = stampSection(map, section, originTileX, originTileY, deduper, opacityPercent);
    if (written <= 0) {
        return std::nullopt;
    }
    auto after = captureTilePatch(map, before->x, before->y, before->width, before->height);
    require(after.has_value(), "Stamped region is missing");
    PlacementChange change;
    change.before = std::move(*before);
    change.after = std::move(*after);
    if (change.after.tileCount > change.before.tileCount) {
        const std::size_t start = static_cast<std::size_t>(change.before.tileCount) * 1024;
        const std::size_t bytes = static_cast<std::size_t>(change.after.tileCount - change.before.tileCount) * 1024;
        require(start + bytes <= map.tileImages.size(), "Stamped tile images are missing");
        change.addedTiles.assign(map.tileImages.begin() + static_cast<std::ptrdiff_t>(start), map.tileImages.begin() + static_cast<std::ptrdiff_t>(start + bytes));
    }
    return change;
}

void revertPlacement(LoadedMap& map, const PlacementChange& change) {
    require(map.tileCount == change.after.tileCount, "Undo does not match the current map");
    requireSameRegion(change);
    map.tileCount = change.before.tileCount;
    map.tileImages.resize(static_cast<std::size_t>(map.tileCount) * 1024);
    paintTilePatch(map, change.before);
}

void reapplyPlacement(LoadedMap& map, const PlacementChange& change) {
    require(map.tileCount == change.before.tileCount, "Redo does not match the current map");
    requireSameRegion(change);
    if (!change.addedTiles.empty()) {
        const std::size_t oldBytes = static_cast<std::size_t>(map.tileCount) * 1024;
        require(map.tileImages.size() >= oldBytes, "Redo tile pool is missing images");
        map.tileImages.resize(oldBytes + change.addedTiles.size());
        std::memcpy(map.tileImages.data() + oldBytes, change.addedTiles.data(), change.addedTiles.size());
    }
    map.tileCount = change.after.tileCount;
    paintTilePatch(map, change.after);
}

SectionStamp copyTileRegion(const LoadedMap& map, int originTileX, int originTileY, int tileWidth, int tileHeight, bool heightsOnly, const TileSelection* included) {
    require(map.tileWidth > 0 && map.tileHeight > 0 && map.tileCount > 0, "Map has no tiles");
    require(tileWidth > 0 && tileHeight > 0, "Copy region is empty");
    const int x0 = std::max(0, originTileX);
    const int y0 = std::max(0, originTileY);
    const int x1 = std::min(map.tileWidth, originTileX + tileWidth);
    const int y1 = std::min(map.tileHeight, originTileY + tileHeight);
    require(x0 < x1 && y0 < y1, "Copy region is outside the map");

    SectionStamp stamp;
    stamp.heightsOnly = heightsOnly;
    stamp.tileWidth = x1 - x0;
    stamp.tileHeight = y1 - y0;
    const std::size_t cellCount = static_cast<std::size_t>(stamp.tileWidth) * static_cast<std::size_t>(stamp.tileHeight);
    stamp.indices.assign(cellCount, 0);
    stamp.heights.assign(static_cast<std::size_t>(stamp.tileWidth) * 2 * static_cast<std::size_t>(stamp.tileHeight) * 2, 0);
    const bool holes = included != nullptr && !included->solid();
    if (holes) {
        stamp.tileMask.assign(cellCount, 0);
    }

    std::vector<int> remap(static_cast<std::size_t>(map.tileCount), -1);
    int next = 0;
    const int mapStride = map.heightWidth();
    const int stampStride = stamp.tileWidth * 2;
    for (int ty = 0; ty < stamp.tileHeight; ++ty) {
        for (int tx = 0; tx < stamp.tileWidth; ++tx) {
            if (holes && !included->contains(x0 + tx, y0 + ty)) {
                continue;
            }
            const std::size_t src = static_cast<std::size_t>(y0 + ty) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(x0 + tx);
            require(src < map.indices.size(), "Map tile grid is incomplete");
            const std::uint32_t index = map.indices[src];
            require(index < static_cast<std::uint32_t>(map.tileCount), "Map tile index is out of range");
            if (remap[index] < 0) {
                remap[index] = next;
                ++next;
            }
            const std::size_t cell = static_cast<std::size_t>(ty * stamp.tileWidth + tx);
            stamp.indices[cell] = static_cast<std::uint32_t>(remap[index]);
            if (holes) {
                stamp.tileMask[cell] = 1;
            }
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const std::size_t heightSrc = static_cast<std::size_t>(y0 * 2 + ty * 2 + dy) * static_cast<std::size_t>(mapStride)
                        + static_cast<std::size_t>(x0 * 2 + tx * 2 + dx);
                    require(heightSrc < map.heights.size(), "Map height grid is incomplete");
                    stamp.heights[static_cast<std::size_t>(ty * 2 + dy) * static_cast<std::size_t>(stampStride) + static_cast<std::size_t>(tx * 2 + dx)] =
                        map.heights[heightSrc];
                }
            }
        }
    }

    if (heightsOnly) {
        return stamp;
    }

    stamp.tileCount = next;
    stamp.tileImages.assign(static_cast<std::size_t>(next) * 1024, 0);
    require(map.tileImages.size() >= static_cast<std::size_t>(map.tileCount) * 1024, "Map tile pool is incomplete");
    for (int index = 0; index < map.tileCount; ++index) {
        if (remap[static_cast<std::size_t>(index)] < 0) {
            continue;
        }
        std::memcpy(
            stamp.tileImages.data() + static_cast<std::size_t>(remap[static_cast<std::size_t>(index)]) * 1024,
            map.tileImages.data() + static_cast<std::size_t>(index) * 1024,
            1024);
    }
    return stamp;
}

PlacementChange clearTileRegion(LoadedMap& map, int originTileX, int originTileY, int tileWidth, int tileHeight) {
    require(map.tileCount > 0, "Map has no blank tile");
    auto before = captureTilePatch(map, originTileX, originTileY, tileWidth, tileHeight);
    require(before.has_value(), "Cut region is outside the map");
    for (int y = 0; y < before->height; ++y) {
        for (int x = 0; x < before->width; ++x) {
            map.indices[static_cast<std::size_t>(before->y + y) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(before->x + x)] = 0;
        }
    }
    const int mapStride = map.heightWidth();
    for (int y = 0; y < before->height * 2; ++y) {
        for (int x = 0; x < before->width * 2; ++x) {
            map.heights[static_cast<std::size_t>(before->y * 2 + y) * static_cast<std::size_t>(mapStride) + static_cast<std::size_t>(before->x * 2 + x)] = 0;
        }
    }
    auto after = captureTilePatch(map, before->x, before->y, before->width, before->height);
    require(after.has_value(), "Cut region is missing");
    PlacementChange change;
    change.before = std::move(*before);
    change.after = std::move(*after);
    return change;
}

namespace {

void orderTileCorners(int& left, int& top, int& right, int& bottom) {
    if (left > right) {
        std::swap(left, right);
    }
    if (top > bottom) {
        std::swap(top, bottom);
    }
}

TileSelection tightenSelection(TileSelection selection) {
    if (selection.empty()) {
        return {};
    }
    int minX = selection.width;
    int minY = selection.height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < selection.height; ++y) {
        for (int x = 0; x < selection.width; ++x) {
            if (selection.mask[static_cast<std::size_t>(y * selection.width + x)] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < 0) {
        return {};
    }
    if (minX == 0 && minY == 0 && maxX == selection.width - 1 && maxY == selection.height - 1) {
        return selection;
    }
    TileSelection tight;
    tight.x = selection.x + minX;
    tight.y = selection.y + minY;
    tight.width = maxX - minX + 1;
    tight.height = maxY - minY + 1;
    tight.mask.resize(static_cast<std::size_t>(tight.width) * static_cast<std::size_t>(tight.height));
    for (int y = 0; y < tight.height; ++y) {
        for (int x = 0; x < tight.width; ++x) {
            tight.mask[static_cast<std::size_t>(y * tight.width + x)] =
                selection.mask[static_cast<std::size_t>((y + minY) * selection.width + (x + minX))];
        }
    }
    return tight;
}

}  // namespace

bool sameTileSelection(const TileSelection& left, const TileSelection& right) {
    if (left.empty() && right.empty()) {
        return true;
    }
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height && left.mask == right.mask;
}

TileSelection tileSelectionRect(int x0, int y0, int x1, int y1) {
    orderTileCorners(x0, y0, x1, y1);
    TileSelection selection;
    selection.x = x0;
    selection.y = y0;
    selection.width = x1 - x0 + 1;
    selection.height = y1 - y0 + 1;
    selection.mask.assign(static_cast<std::size_t>(selection.width) * static_cast<std::size_t>(selection.height), 1);
    return selection;
}

TileSelection tileSelectionAdd(const TileSelection& base, int x0, int y0, int x1, int y1) {
    orderTileCorners(x0, y0, x1, y1);
    if (base.empty()) {
        return tileSelectionRect(x0, y0, x1, y1);
    }
    const int left = std::min(base.x, x0);
    const int top = std::min(base.y, y0);
    const int right = std::max(base.x + base.width - 1, x1);
    const int bottom = std::max(base.y + base.height - 1, y1);
    TileSelection added;
    added.x = left;
    added.y = top;
    added.width = right - left + 1;
    added.height = bottom - top + 1;
    added.mask.assign(static_cast<std::size_t>(added.width) * static_cast<std::size_t>(added.height), 0);
    for (int y = 0; y < base.height; ++y) {
        for (int x = 0; x < base.width; ++x) {
            if (base.mask[static_cast<std::size_t>(y * base.width + x)] == 0) {
                continue;
            }
            const int outX = base.x + x - left;
            const int outY = base.y + y - top;
            added.mask[static_cast<std::size_t>(outY * added.width + outX)] = 1;
        }
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int outX = x - left;
            const int outY = y - top;
            added.mask[static_cast<std::size_t>(outY * added.width + outX)] = 1;
        }
    }
    return added;
}

TileSelection tileSelectionSubtract(const TileSelection& base, int x0, int y0, int x1, int y1) {
    if (base.empty()) {
        return {};
    }
    orderTileCorners(x0, y0, x1, y1);
    TileSelection remaining = base;
    const int left = std::max(base.x, x0);
    const int top = std::max(base.y, y0);
    const int right = std::min(base.x + base.width - 1, x1);
    const int bottom = std::min(base.y + base.height - 1, y1);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            remaining.mask[static_cast<std::size_t>((y - base.y) * base.width + (x - base.x))] = 0;
        }
    }
    return tightenSelection(std::move(remaining));
}

SectionStamp copyTileSelection(const LoadedMap& map, const TileSelection& selection, bool heightsOnly) {
    require(!selection.empty(), "Nothing is selected");
    return copyTileRegion(map, selection.x, selection.y, selection.width, selection.height, heightsOnly, &selection);
}

PlacementChange clearTileSelection(LoadedMap& map, const TileSelection& selection) {
    require(!selection.empty(), "Nothing is selected");
    if (selection.solid()) {
        return clearTileRegion(map, selection.x, selection.y, selection.width, selection.height);
    }
    require(map.tileCount > 0, "Map has no blank tile");
    auto before = captureTilePatch(map, selection.x, selection.y, selection.width, selection.height);
    require(before.has_value(), "Cut region is outside the map");
    const int mapStride = map.heightWidth();
    for (int y = 0; y < selection.height; ++y) {
        for (int x = 0; x < selection.width; ++x) {
            if (selection.mask[static_cast<std::size_t>(y * selection.width + x)] == 0) {
                continue;
            }
            const int tileX = selection.x + x;
            const int tileY = selection.y + y;
            require(tileX >= 0 && tileY >= 0 && tileX < map.tileWidth && tileY < map.tileHeight, "Selected tile is outside the map");
            map.indices[static_cast<std::size_t>(tileY) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(tileX)] = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    map.heights[static_cast<std::size_t>(tileY * 2 + dy) * static_cast<std::size_t>(mapStride) + static_cast<std::size_t>(tileX * 2 + dx)] = 0;
                }
            }
        }
    }
    auto after = captureTilePatch(map, before->x, before->y, before->width, before->height);
    require(after.has_value(), "Cut region is missing");
    PlacementChange change;
    change.before = std::move(*before);
    change.after = std::move(*after);
    return change;
}

std::optional<PlacementChange> adjustSelectionHeight(LoadedMap& map, const TileSelection& selection, HeightAdjust adjust, int amount) {
    require(!selection.empty(), "Nothing is selected");
    require(map.heights.size() == static_cast<std::size_t>(map.heightWidth()) * static_cast<std::size_t>(map.heightHeight()), "Map height grid is incomplete");
    auto before = captureTilePatch(map, selection.x, selection.y, selection.width, selection.height);
    require(before.has_value(), "Height edit is outside the map");
    const int mapStride = map.heightWidth();
    bool changed = false;
    for (int y = 0; y < selection.height; ++y) {
        for (int x = 0; x < selection.width; ++x) {
            if (selection.mask[static_cast<std::size_t>(y * selection.width + x)] == 0) {
                continue;
            }
            const int tileX = selection.x + x;
            const int tileY = selection.y + y;
            require(tileX >= 0 && tileY >= 0 && tileX < map.tileWidth && tileY < map.tileHeight, "Selected tile is outside the map");
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const std::size_t index = static_cast<std::size_t>(tileY * 2 + dy) * static_cast<std::size_t>(mapStride)
                        + static_cast<std::size_t>(tileX * 2 + dx);
                    const int current = map.heights[index];
                    long long next = amount;
                    if (adjust == HeightAdjust::Add) {
                        next = static_cast<long long>(current) + amount;
                    } else if (adjust == HeightAdjust::Subtract) {
                        next = static_cast<long long>(current) - amount;
                    }
                    next = std::clamp(next, 0LL, 255LL);
                    if (next == current) {
                        continue;
                    }
                    map.heights[index] = static_cast<std::uint8_t>(next);
                    changed = true;
                }
            }
        }
    }
    if (!changed) {
        return std::nullopt;
    }
    auto after = captureTilePatch(map, before->x, before->y, before->width, before->height);
    require(after.has_value(), "Height edit is missing");
    PlacementChange change;
    change.before = std::move(*before);
    change.after = std::move(*after);
    return change;
}

std::optional<PlacementChange> blendSelectionTextures(LoadedMap& map, const TileSelection& selection, TileDeduper* deduper) {
    constexpr int kBlendPixels = 16;
    require(!selection.empty(), "Nothing is selected");
    require(map.tileCount > 0, "Map has no tiles");
    require(map.indices.size() == static_cast<std::size_t>(map.tileWidth) * static_cast<std::size_t>(map.tileHeight), "Map tile grid is incomplete");
    require(map.tileImages.size() >= static_cast<std::size_t>(map.tileCount) * 1024, "Map tile pool is incomplete");
    auto before = captureTilePatch(map, selection.x, selection.y, selection.width, selection.height);
    require(before.has_value(), "Blend region is outside the map");

    auto imageOf = [&](int tileX, int tileY) -> const std::uint8_t* {
        const std::uint32_t index = map.indices[static_cast<std::size_t>(tileY) * static_cast<std::size_t>(map.tileWidth) + static_cast<std::size_t>(tileX)];
        require(index < static_cast<std::uint32_t>(map.tileCount), "Map tile index is out of range");
        return map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
    };
    auto nearestPalette = [](int red, int green, int blue) -> std::uint8_t {
        int best = 0;
        int bestDistance = std::numeric_limits<int>::max();
        for (int index = 0; index < 256; ++index) {
            const int dr = static_cast<int>(kTaPalette[index][0]) - red;
            const int dg = static_cast<int>(kTaPalette[index][1]) - green;
            const int db = static_cast<int>(kTaPalette[index][2]) - blue;
            const int distance = dr * dr + dg * dg + db * db;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = index;
            }
        }
        return static_cast<std::uint8_t>(best);
    };

    struct BlendedTile {
        int x = 0;
        int y = 0;
        std::uint8_t pixels[1024] = {};
    };
    std::vector<BlendedTile> blended;
    bool anyChanged = false;
    for (int y = 0; y < selection.height; ++y) {
        for (int x = 0; x < selection.width; ++x) {
            if (selection.mask[static_cast<std::size_t>(y * selection.width + x)] == 0) {
                continue;
            }
            const int tileX = selection.x + x;
            const int tileY = selection.y + y;
            const bool touches = selection.contains(tileX - 1, tileY) || selection.contains(tileX + 1, tileY)
                || selection.contains(tileX, tileY - 1) || selection.contains(tileX, tileY + 1);
            if (!touches) {
                continue;
            }
            const std::uint8_t* own = imageOf(tileX, tileY);
            BlendedTile tile;
            tile.x = tileX;
            tile.y = tileY;
            bool tileChanged = false;
            for (int py = 0; py < 32; ++py) {
                for (int px = 0; px < 32; ++px) {
                    const std::uint8_t ownIndex = own[py * 32 + px];
                    int txNum = 0;
                    int tyNum = 0;
                    const std::uint8_t* horiz = nullptr;
                    const std::uint8_t* vert = nullptr;
                    const std::uint8_t* diag = nullptr;
                    int sampleX = px;
                    int sampleY = py;
                    int diagX = tileX;
                    int diagY = tileY;
                    if (px < kBlendPixels && selection.contains(tileX - 1, tileY)) {
                        txNum = kBlendPixels - px;
                        horiz = imageOf(tileX - 1, tileY);
                        sampleX = 31 - px;
                        diagX = tileX - 1;
                    } else if (px >= 32 - kBlendPixels && selection.contains(tileX + 1, tileY)) {
                        const int dist = 31 - px;
                        txNum = kBlendPixels - dist;
                        horiz = imageOf(tileX + 1, tileY);
                        sampleX = dist;
                        diagX = tileX + 1;
                    }
                    if (py < kBlendPixels && selection.contains(tileX, tileY - 1)) {
                        tyNum = kBlendPixels - py;
                        vert = imageOf(tileX, tileY - 1);
                        sampleY = 31 - py;
                        diagY = tileY - 1;
                    } else if (py >= 32 - kBlendPixels && selection.contains(tileX, tileY + 1)) {
                        const int dist = 31 - py;
                        tyNum = kBlendPixels - dist;
                        vert = imageOf(tileX, tileY + 1);
                        sampleY = dist;
                        diagY = tileY + 1;
                    }
                    if (txNum == 0 && tyNum == 0) {
                        tile.pixels[py * 32 + px] = ownIndex;
                        continue;
                    }
                    if (txNum > 0 && tyNum > 0 && selection.contains(diagX, diagY)) {
                        diag = imageOf(diagX, diagY);
                    }
                    const std::uint8_t horizIndex = horiz != nullptr ? horiz[py * 32 + sampleX] : ownIndex;
                    const std::uint8_t vertIndex = vert != nullptr ? vert[sampleY * 32 + px] : ownIndex;
                    const std::uint8_t diagIndex = diag != nullptr ? diag[sampleY * 32 + sampleX] : ownIndex;
                    if (horizIndex == ownIndex && vertIndex == ownIndex && diagIndex == ownIndex) {
                        tile.pixels[py * 32 + px] = ownIndex;
                        continue;
                    }
                    const int denom = kBlendPixels * 2;
                    const int wOwn = (denom - txNum) * (denom - tyNum);
                    const int wH = txNum * (denom - tyNum);
                    const int wV = (denom - txNum) * tyNum;
                    const int wD = txNum * tyNum;
                    int red = 0;
                    int green = 0;
                    int blue = 0;
                    auto accumulate = [&](std::uint8_t index, int weight) {
                        const std::uint8_t* color = kTaPalette[index];
                        red += static_cast<int>(color[0]) * weight;
                        green += static_cast<int>(color[1]) * weight;
                        blue += static_cast<int>(color[2]) * weight;
                    };
                    accumulate(ownIndex, wOwn);
                    accumulate(horizIndex, wH);
                    accumulate(vertIndex, wV);
                    accumulate(diagIndex, wD);
                    const int total = denom * denom;
                    const std::uint8_t mixed = nearestPalette((red + total / 2) / total, (green + total / 2) / total, (blue + total / 2) / total);
                    tile.pixels[py * 32 + px] = mixed;
                    tileChanged = tileChanged || mixed != ownIndex;
                }
            }
            if (tileChanged) {
                anyChanged = true;
                blended.push_back(tile);
            }
        }
    }
    if (!anyChanged) {
        return std::nullopt;
    }

    if (deduper != nullptr && (!deduper->ready || deduper->tileCount != map.tileCount)) {
        deduper->buckets.clear();
        for (int index = 0; index < map.tileCount; ++index) {
            const std::uint8_t* tile = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
            deduper->buckets[hashTile(tile)].push_back(static_cast<std::uint32_t>(index));
        }
        deduper->ready = true;
        deduper->tileCount = map.tileCount;
    }

    std::vector<std::uint8_t> addedImages;
    const int poolStart = map.tileCount;
    int nextIndex = poolStart;
    auto resolveImage = [&](const std::uint8_t* src) -> int {
        auto matches = [&](const std::uint8_t* existing) {
            return std::memcmp(existing, src, 1024) == 0;
        };
        if (deduper != nullptr) {
            const auto found = deduper->buckets.find(hashTile(src));
            if (found != deduper->buckets.end()) {
                for (const std::uint32_t index : found->second) {
                    if (index < static_cast<std::uint32_t>(poolStart)
                        && matches(map.tileImages.data() + static_cast<std::size_t>(index) * 1024)) {
                        return static_cast<int>(index);
                    }
                }
            }
        } else {
            for (int index = 0; index < poolStart; ++index) {
                if (matches(map.tileImages.data() + static_cast<std::size_t>(index) * 1024)) {
                    return index;
                }
            }
        }
        for (int index = poolStart; index < nextIndex; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index - poolStart) * 1024;
            if (matches(addedImages.data() + offset)) {
                return index;
            }
        }
        require(nextIndex < std::numeric_limits<int>::max(), "The tile pool is full");
        const int slot = nextIndex;
        ++nextIndex;
        addedImages.insert(addedImages.end(), src, src + 1024);
        return slot;
    };

    std::vector<std::pair<int, int>> writes;
    writes.reserve(blended.size());
    for (const BlendedTile& tile : blended) {
        writes.push_back({tile.y * map.tileWidth + tile.x, resolveImage(tile.pixels)});
    }
    if (!addedImages.empty()) {
        const std::size_t needed = static_cast<std::size_t>(nextIndex) * 1024;
        if (map.tileImages.size() < needed) {
            map.tileImages.resize(needed);
        }
        std::memcpy(map.tileImages.data() + static_cast<std::size_t>(poolStart) * 1024, addedImages.data(), addedImages.size());
        map.tileCount = nextIndex;
        if (deduper != nullptr) {
            for (int index = poolStart; index < nextIndex; ++index) {
                const std::uint8_t* src = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
                deduper->buckets[hashTile(src)].push_back(static_cast<std::uint32_t>(index));
            }
            deduper->tileCount = nextIndex;
        }
    }
    for (const auto& write : writes) {
        map.indices[static_cast<std::size_t>(write.first)] = static_cast<std::uint32_t>(write.second);
    }

    auto after = captureTilePatch(map, before->x, before->y, before->width, before->height);
    require(after.has_value(), "Blend region is missing");
    PlacementChange change;
    change.before = std::move(*before);
    change.after = std::move(*after);
    if (change.after.tileCount > change.before.tileCount) {
        const std::size_t start = static_cast<std::size_t>(change.before.tileCount) * 1024;
        const std::size_t bytes = static_cast<std::size_t>(change.after.tileCount - change.before.tileCount) * 1024;
        require(start + bytes <= map.tileImages.size(), "Blended tile images are missing");
        change.addedTiles.assign(
            map.tileImages.begin() + static_cast<std::ptrdiff_t>(start),
            map.tileImages.begin() + static_cast<std::ptrdiff_t>(start + bytes));
    }
    return change;
}

}  // namespace galaxy

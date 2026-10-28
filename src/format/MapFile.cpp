#include "format/MapFile.hpp"

#include "format/HpiArchive.hpp"
#include "format/TaPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace galaxy {
namespace {

std::uint32_t readU32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint16_t readU16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) {
            return false;
        }
    }
    return true;
}

std::string lowerExt(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

std::vector<std::uint8_t> readEntireFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "Could not open " + path.string());
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    require(size >= 0, "Could not read " + path.string());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        require(static_cast<bool>(in), "Could not read " + path.string());
    }
    return data;
}

std::string fieldValue(std::string_view text, std::string_view key, std::size_t from = 0) {
    const std::size_t found = text.find(key, from);
    if (found == std::string_view::npos) {
        return {};
    }
    std::size_t cursor = found + key.size();
    const std::size_t end = text.find(';', cursor);
    if (end == std::string_view::npos) {
        return std::string(text.substr(cursor));
    }
    return std::string(text.substr(cursor, end - cursor));
}

int parseInt(std::string_view text) {
    int value = 0;
    int sign = 1;
    std::size_t i = 0;
    if (!text.empty() && text[0] == '-') {
        sign = -1;
        ++i;
    }
    for (; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            break;
        }
        value = value * 10 + (text[i] - '0');
    }
    return value * sign;
}

void readOta(const std::vector<std::uint8_t>& bytes, LoadedMap& map) {
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    map.missionName = fieldValue(text, "missionname=");

    std::size_t cursor = 0;
    const std::string_view marker = "specialwhat=StartPos";
    while ((cursor = text.find(marker, cursor)) != std::string_view::npos) {
        cursor += marker.size();
        StartPosition start;
        start.player = parseInt(text.substr(cursor));
        const std::string x = fieldValue(text, "XPos=", cursor);
        const std::string z = fieldValue(text, "ZPos=", cursor);
        if (x.empty() || z.empty()) {
            continue;
        }
        start.x = parseInt(x);
        start.y = parseInt(z);
        map.starts.push_back(start);
    }
}

constexpr std::uint16_t kNoFeature = 0xFFFF;
constexpr std::uint16_t kUnknownFeature = 0xFFFE;
constexpr std::uint16_t kVoidFeature = 0xFFFC;
constexpr std::size_t kMaxFeatureNames = 0xFFFC;

bool isSpecialFeature(std::uint16_t feature) {
    return feature == kNoFeature || feature == kUnknownFeature || feature == kVoidFeature;
}

void rebuildFeaturePlacements(LoadedMap& map) {
    map.features.clear();
    const int width = map.heightWidth();
    const int height = map.heightHeight();
    if (width <= 0 || height <= 0) {
        return;
    }
    const std::size_t cells = map.featureIndices.size();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if (i >= cells) {
                return;
            }
            const std::uint16_t feature = map.featureIndices[i];
            if (isSpecialFeature(feature)) {
                continue;
            }
            FeaturePlacement placement;
            placement.x = x;
            placement.y = y;
            placement.name = feature < map.featureNames.size() ? map.featureNames[feature] : ("#" + std::to_string(feature));
            map.features.push_back(std::move(placement));
        }
    }
}

bool featureCell(const LoadedMap& map, int cellX, int cellY, std::size_t& index) {
    if (cellX < 0 || cellY < 0 || cellX >= map.heightWidth() || cellY >= map.heightHeight()) {
        return false;
    }
    index = static_cast<std::size_t>(cellY) * static_cast<std::size_t>(map.heightWidth()) + static_cast<std::size_t>(cellX);
    return index < map.featureIndices.size();
}

int findFeatureName(const LoadedMap& map, const std::string& name) {
    for (std::size_t i = 0; i < map.featureNames.size(); ++i) {
        if (map.featureNames[i] == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string trimmedFeatureName(std::string_view name) {
    std::size_t begin = 0;
    while (begin < name.size() && std::isspace(static_cast<unsigned char>(name[begin])) != 0) {
        ++begin;
    }
    std::size_t end = name.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(name[end - 1])) != 0) {
        --end;
    }
    std::string stored(name.substr(begin, end - begin));
    if (stored.size() > 127) {
        stored.resize(127);
    }
    return stored;
}

LoadedMap parseTnt(const std::vector<std::uint8_t>& tnt, LoadedMap map) {
    require(tnt.size() >= 64, "TNT file is too small");
    const std::uint32_t version = readU32(tnt.data());
    require(version == kTntVersion || version == kTntWideVersion, "TNT file has an unexpected header");
    const bool wideIndices = version == kTntWideVersion;

    const std::uint32_t width = readU32(tnt.data() + 4);
    const std::uint32_t height = readU32(tnt.data() + 8);
    const std::uint32_t ptrData = readU32(tnt.data() + 12);
    const std::uint32_t ptrAttr = readU32(tnt.data() + 16);
    const std::uint32_t ptrGfx = readU32(tnt.data() + 20);
    const std::uint32_t tileCount = readU32(tnt.data() + 24);
    const std::uint32_t animCount = readU32(tnt.data() + 28);
    const std::uint32_t ptrAnims = readU32(tnt.data() + 32);
    const std::uint32_t seaLevel = readU32(tnt.data() + 36);

    require(width % 2 == 0 && height % 2 == 0, "TNT dimensions are not a whole number of tiles");
    map.tileWidth = static_cast<int>(width / 2);
    map.tileHeight = static_cast<int>(height / 2);
    map.seaLevel = static_cast<int>(seaLevel);
    map.tileCount = static_cast<int>(tileCount);

    const std::size_t indexCount = static_cast<std::size_t>(map.tileWidth) * static_cast<std::size_t>(map.tileHeight);
    const std::size_t indexBytes = wideIndices ? 4 : 2;
    require(static_cast<std::uint64_t>(ptrData) + indexCount * indexBytes <= tnt.size(), "TNT tile index grid extends past the file");
    map.indices.resize(indexCount);
    for (std::size_t i = 0; i < indexCount; ++i) {
        const std::uint8_t* cell = tnt.data() + ptrData + i * indexBytes;
        map.indices[i] = wideIndices ? readU32(cell) : readU16(cell);
    }

    const std::size_t gfxBytes = static_cast<std::size_t>(tileCount) * 1024;
    require(static_cast<std::uint64_t>(ptrGfx) + gfxBytes <= tnt.size(), "TNT tile images extend past the file");
    map.tileImages.assign(tnt.begin() + ptrGfx, tnt.begin() + static_cast<std::ptrdiff_t>(ptrGfx + gfxBytes));

    const std::size_t attrCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    require(static_cast<std::uint64_t>(ptrAttr) + attrCount * 4 <= tnt.size(), "TNT height grid extends past the file");
    map.heights.resize(attrCount);
    map.featureIndices.resize(attrCount);
    map.featurePadding.resize(attrCount);

    map.featureNames.resize(animCount);
    if (animCount > 0) {
        require(ptrAnims + static_cast<std::size_t>(animCount) * 132 <= tnt.size(), "TNT feature names extend past the file");
        for (std::uint32_t i = 0; i < animCount; ++i) {
            const std::uint8_t* entry = tnt.data() + ptrAnims + static_cast<std::size_t>(i) * 132;
            std::size_t length = 0;
            while (length < 128 && entry[4 + length] != 0) {
                ++length;
            }
            map.featureNames[i] = std::string(reinterpret_cast<const char*>(entry + 4), length);
        }
    }

    for (std::size_t i = 0; i < attrCount; ++i) {
        const std::uint8_t* cell = tnt.data() + ptrAttr + i * 4;
        map.heights[i] = cell[0];
        const std::uint16_t feature = readU16(cell + 1);
        map.featureIndices[i] = feature;
        map.featurePadding[i] = cell[3];
    }
    rebuildFeaturePlacements(map);
    return map;
}

LoadedMap loadTntAndOta(const std::vector<std::uint8_t>& tnt, const std::vector<std::uint8_t>* ota, LoadedMap map) {
    map = parseTnt(tnt, std::move(map));
    if (ota != nullptr) {
        map.otaText = *ota;
        readOta(*ota, map);
    }
    return map;
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

std::string tdfValue(std::string value) {
    for (char& character : value) {
        if (character == ';' || character == '\n' || character == '\r' || character == '{' || character == '}') {
            character = ' ';
        }
    }
    return value;
}

bool startsWithIgnoreCase(std::string_view text, std::size_t at, std::string_view prefix) {
    if (at + prefix.size() > text.size()) {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const auto left = static_cast<unsigned char>(text[at + i]);
        const auto right = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

std::size_t findIgnoreCase(std::string_view text, std::string_view needle, std::size_t from) {
    if (needle.empty() || from > text.size()) {
        return std::string_view::npos;
    }
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i) {
        if (startsWithIgnoreCase(text, i, needle)) {
            return i;
        }
    }
    return std::string_view::npos;
}

bool braceSpan(std::string_view text, std::size_t from, std::size_t& open, std::size_t& close) {
    open = text.find('{', from);
    if (open == std::string_view::npos) {
        return false;
    }
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 0) {
                close = i;
                return true;
            }
        }
    }
    return false;
}

std::string_view newlineOf(std::string_view text) {
    return text.find("\r\n") != std::string_view::npos ? std::string_view("\r\n") : std::string_view("\n");
}

std::string startEntries(const LoadedMap& map, std::string_view newline) {
    std::string text;
    bool seen[11] = {};
    int special = 0;
    for (const StartPosition& start : map.starts) {
        if (start.player < 1 || start.player > 10 || seen[start.player]) {
            continue;
        }
        seen[start.player] = true;
        const std::string nl(newline);
        text += "\t\t\t[special" + std::to_string(special) + "]" + nl;
        text += "\t\t\t\t{" + nl;
        text += "\t\t\t\tspecialwhat=StartPos" + std::to_string(start.player) + ";" + nl;
        text += "\t\t\t\tXPos=" + std::to_string(start.x) + ";" + nl;
        text += "\t\t\t\tZPos=" + std::to_string(start.y) + ";" + nl;
        text += "\t\t\t\t}" + nl;
        ++special;
    }
    return text;
}

std::string otaWithStarts(std::string text, const LoadedMap& map) {
    const std::string_view newline = newlineOf(text);
    const std::string entries = startEntries(map, newline);
    const std::string nl(newline);
    std::size_t open = 0;
    std::size_t close = 0;
    const std::size_t specials = findIgnoreCase(text, "[specials]", 0);
    if (specials != std::string::npos && braceSpan(text, specials + std::string_view("[specials]").size(), open, close)) {
        text.replace(open + 1, close - open - 1, nl + entries + "\t\t\t");
    } else {
        const std::size_t schema = findIgnoreCase(text, "[schema", 0);
        if (schema != std::string::npos && braceSpan(text, schema, open, close)) {
            const std::string block = nl + "\t\t[specials]" + nl + "\t\t\t{" + nl + entries + "\t\t\t}" + nl + "\t\t";
            text.insert(close, block);
        } else {
            text += nl + "[specials]" + nl + "{" + nl + entries + "}" + nl;
        }
    }

    std::size_t scan = 0;
    const std::size_t kept = findIgnoreCase(text, "[specials]", 0);
    if (kept != std::string::npos && braceSpan(text, kept + std::string_view("[specials]").size(), open, close)) {
        scan = close + 1;
    }
    while (scan < text.size()) {
        const std::size_t header = findIgnoreCase(text, "[special", scan);
        if (header == std::string::npos) {
            break;
        }
        if (startsWithIgnoreCase(text, header, "[specials]")) {
            scan = header + std::string_view("[specials]").size();
            continue;
        }
        std::size_t blockOpen = 0;
        std::size_t blockClose = 0;
        if (!braceSpan(text, header, blockOpen, blockClose)) {
            break;
        }
        const std::string_view body(text.data() + blockOpen, blockClose - blockOpen);
        if (findIgnoreCase(body, "startpos", 0) == std::string_view::npos) {
            scan = blockClose + 1;
            continue;
        }
        std::size_t end = blockClose + 1;
        if (end < text.size() && text[end] == '\r') {
            ++end;
        }
        if (end < text.size() && text[end] == '\n') {
            ++end;
        }
        text.erase(header, end - header);
        scan = header;
    }
    return text;
}

void mapBlocks(const LoadedMap& map, int& wide, int& high) {
    wide = (map.heightWidth() + 31) / 32;
    high = (map.heightHeight() + 31) / 32;
}

std::string defaultDescription(const LoadedMap& map) {
    int blocksWide = 0;
    int blocksHigh = 0;
    mapBlocks(map, blocksWide, blocksHigh);
    return std::to_string(blocksWide) + " x " + std::to_string(blocksHigh) + " Map";
}

std::vector<std::uint8_t> buildDefaultOta(const LoadedMap& map) {
    int blocksWide = 0;
    int blocksHigh = 0;
    mapBlocks(map, blocksWide, blocksHigh);
    const std::string mission = tdfValue(map.missionName.empty() ? "Untitled Map" : map.missionName);
    std::string text;
    text += "[GlobalHeader]\r\n\t{\r\n";
    text += "\tmissionname=" + mission + ";\r\n";
    text += "\tmissiondescription=" + defaultDescription(map) + ";\r\n";
    text += "\tplanet=;\r\n";
    text += "\tmissionhint=;\r\n";
    text += "\tbrief=;\r\n";
    text += "\tnarration=;\r\n";
    text += "\tglamour=;\r\n";
    text += "\tlineofsight=0;\r\n";
    text += "\tmapping=0;\r\n";
    text += "\ttidalstrength=20;\r\n";
    text += "\tsolarstrength=20;\r\n";
    text += "\tlavaworld=0;\r\n";
    text += "\tkillmul=50;\r\n";
    text += "\ttimemul=0;\r\n";
    text += "\tminwindspeed=0;\r\n";
    text += "\tmaxwindspeed=3000;\r\n";
    text += "\tgravity=112;\r\n";
    text += "\twaterdoesdamage=0;\r\n";
    text += "\twaterdamage=0;\r\n";
    text += "\tnumplayers=2;\r\n";
    text += "\tsize=" + std::to_string(blocksWide) + " x " + std::to_string(blocksHigh) + ";\r\n";
    text += "\tmemory=16 mb;\r\n";
    text += "\tuseonlyunits=;\r\n";
    text += "\tSCHEMACOUNT=1;\r\n";
    text += "\t[Schema 0]\r\n\t\t{\r\n";
    text += "\t\tType=Network 1;\r\n";
    text += "\t\taiprofile=DEFAULT;\r\n";
    text += "\t\tSurfaceMetal=3;\r\n";
    text += "\t\tMohoMetal=30;\r\n";
    text += "\t\tHumanMetal=1000;\r\n";
    text += "\t\tComputerMetal=1000;\r\n";
    text += "\t\tHumanEnergy=1000;\r\n";
    text += "\t\tComputerEnergy=1000;\r\n";
    text += "\t\tMeteorWeapon=;\r\n";
    text += "\t\tMeteorRadius=0;\r\n";
    text += "\t\tMeteorDensity=0;\r\n";
    text += "\t\tMeteorDuration=0;\r\n";
    text += "\t\tMeteorInterval=0;\r\n";
    text += "\t\t[specials]\r\n\t\t\t{\r\n";
    text += startEntries(map, "\r\n");
    text += "\t\t\t}\r\n\t\t}\r\n\t}\r\n";
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> otaForSave(const LoadedMap& map) {
    if (map.otaText.empty()) {
        return buildDefaultOta(map);
    }
    const std::string text = otaWithStarts(std::string(map.otaText.begin(), map.otaText.end()), map);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> buildTnt(const LoadedMap& map) {
    require(map.tileWidth > 0 && map.tileHeight > 0, "The map has no tiles to save");
    const auto tileCount = static_cast<std::size_t>(map.tileCount);
    const auto indexCount = static_cast<std::size_t>(map.tileWidth) * static_cast<std::size_t>(map.tileHeight);
    const auto attrCount = static_cast<std::size_t>(map.heightWidth()) * static_cast<std::size_t>(map.heightHeight());
    require(map.indices.size() == indexCount, "The tile grid does not match the map size");
    require(map.heights.size() == attrCount, "The height grid does not match the map size");
    require(map.featureIndices.size() == attrCount, "The feature grid does not match the map size");
    require(map.tileImages.size() == tileCount * 1024, "The tile images do not match the tile count");
    const bool wideIndices = tileCount > static_cast<std::size_t>(kVanillaTileSlots);

    std::vector<std::string> names = map.featureNames;
    for (std::uint16_t feature : map.featureIndices) {
        if (feature == 0xFFFF || feature == 0xFFFE || feature == 0xFFFC) {
            continue;
        }
        if (names.size() <= feature) {
            names.resize(static_cast<std::size_t>(feature) + 1);
        }
    }

    const auto animCount = static_cast<std::uint32_t>(names.size());
    const std::uint64_t ptrData = 0x40;
    const std::uint64_t ptrAttr = ptrData + indexCount * (wideIndices ? 4ull : 2ull);
    const std::uint64_t ptrGfx = ptrAttr + attrCount * 4ull;
    const std::uint64_t ptrAnims = ptrGfx + tileCount * 1024ull;
    const std::uint64_t ptrMini = ptrAnims + static_cast<std::uint64_t>(animCount) * 132ull;
    const std::uint64_t fileEnd = ptrMini + 8ull + 252ull * 252ull;
    require(fileEnd <= 0xffffffffull, "This map is too large to store in one TNT file");

    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(fileEnd));
    appendU32(out, wideIndices ? kTntWideVersion : kTntVersion);
    appendU32(out, static_cast<std::uint32_t>(map.heightWidth()));
    appendU32(out, static_cast<std::uint32_t>(map.heightHeight()));
    appendU32(out, static_cast<std::uint32_t>(ptrData));
    appendU32(out, static_cast<std::uint32_t>(ptrAttr));
    appendU32(out, static_cast<std::uint32_t>(ptrGfx));
    appendU32(out, static_cast<std::uint32_t>(tileCount));
    appendU32(out, animCount);
    appendU32(out, static_cast<std::uint32_t>(ptrAnims));
    appendU32(out, static_cast<std::uint32_t>(map.seaLevel));
    appendU32(out, static_cast<std::uint32_t>(ptrMini));
    appendU32(out, 1);
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, 0);
    appendU32(out, 0);

    for (std::uint32_t index : map.indices) {
        require(index < tileCount, "A tile index points outside the tile images");
        if (wideIndices) {
            appendU32(out, index);
        } else {
            appendU16(out, static_cast<std::uint16_t>(index));
        }
    }
    for (std::size_t i = 0; i < attrCount; ++i) {
        out.push_back(map.heights[i]);
        appendU16(out, map.featureIndices[i]);
        out.push_back(i < map.featurePadding.size() ? map.featurePadding[i] : 0);
    }
    out.insert(out.end(), map.tileImages.begin(), map.tileImages.end());
    for (std::uint32_t i = 0; i < animCount; ++i) {
        appendU32(out, i);
        std::uint8_t name[128] = {};
        const std::string& text = names[i];
        const std::size_t length = std::min(text.size(), std::size_t{127});
        std::memcpy(name, text.data(), length);
        out.insert(out.end(), name, name + 128);
    }

    // The game draws the minimap from its top-left corner and ignores the
    // 32 pixels at the right and the 128 at the bottom of the map, so the
    // picture is the playable area scaled to fit 252 pixels on its longer
    // side, anchored at the top-left, with the palette's padding colour right
    // and below it. The original maps are laid out this way. It is rebuilt
    // from the tiles on every save.
    appendU32(out, 252);
    appendU32(out, 252);
    const int mapWidth = std::max(1, map.pixelWidth() - 32);
    const int mapHeight = std::max(1, map.pixelHeight() - 128);
    int previewWidth = 252;
    int previewHeight = 252;
    if (mapWidth >= mapHeight) {
        previewHeight = std::max(1, mapHeight * 252 / mapWidth);
    } else {
        previewWidth = std::max(1, mapWidth * 252 / mapHeight);
    }
    for (int y = 0; y < 252; ++y) {
        for (int x = 0; x < 252; ++x) {
            if (x >= previewWidth || y >= previewHeight) {
                out.push_back(0x64);
                continue;
            }
            const int mapX = std::min(mapWidth - 1, x * mapWidth / previewWidth);
            const int mapY = std::min(mapHeight - 1, y * mapHeight / previewHeight);
            const int tileX = mapX >> 5;
            const int tileY = mapY >> 5;
            const std::uint32_t index = map.indices[static_cast<std::size_t>(tileY * map.tileWidth + tileX)];
            const std::uint8_t* tile = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
            out.push_back(tile[((mapY & 31) << 5) + (mapX & 31)]);
        }
    }
    return out;
}

void writeBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    const auto parent = path.parent_path();
    const auto temp = (parent.empty() ? std::filesystem::path() : parent) / (path.filename().string() + ".galaxy-tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(out), "Could not write " + temp.string());
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        require(static_cast<bool>(out), "Could not write " + temp.string());
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        std::filesystem::rename(temp, path);
    }
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

bool sameKey(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }
    return true;
}

double parseDouble(std::string_view raw) {
    const std::string text = trimCopy(raw);
    if (text.empty()) {
        return 0;
    }
    std::size_t i = 0;
    double sign = 1;
    if (text[i] == '-') {
        sign = -1;
        ++i;
    }
    double value = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        double place = 0.1;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            value += (text[i] - '0') * place;
            place *= 0.1;
            ++i;
        }
    }
    return sign * value;
}

std::string formatDouble(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    std::string text = buffer;
    const std::size_t dot = text.find('.');
    if (dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text.empty() || text == "-") {
        return "0";
    }
    return text;
}

bool parseFlag(std::string_view text) {
    const std::string value = trimCopy(text);
    return sameKey(value, "1") || sameKey(value, "true") || sameKey(value, "yes");
}

bool locateBraced(const std::string& text, std::size_t header, std::size_t& open, std::size_t& close) {
    return braceSpan(text, header, open, close);
}

bool findGlobalHeader(const std::string& text, std::size_t& open, std::size_t& close) {
    const std::size_t header = findIgnoreCase(text, "[GlobalHeader", 0);
    if (header == std::string::npos) {
        return false;
    }
    const std::size_t after = header + std::string_view("[GlobalHeader").size();
    if (after >= text.size() || (text[after] != ']' && std::isspace(static_cast<unsigned char>(text[after])) == 0)) {
        return false;
    }
    return locateBraced(text, header, open, close);
}

bool findSchema(const std::string& text, int index, std::size_t& open, std::size_t& close) {
    std::size_t scan = 0;
    int seen = 0;
    while (scan < text.size()) {
        const std::size_t header = findIgnoreCase(text, "[Schema", scan);
        if (header == std::string::npos) {
            return false;
        }
        const std::size_t after = header + std::string_view("[Schema").size();
        const unsigned char next = after < text.size() ? static_cast<unsigned char>(text[after]) : 0;
        const bool schema = next == ' ' || next == '\t' || next == ']' || std::isdigit(next) != 0;
        if (!schema || !locateBraced(text, header, open, close)) {
            scan = header + 7;
            continue;
        }
        if (seen == index) {
            return true;
        }
        ++seen;
        scan = close + 1;
    }
    return false;
}

struct KeyHit {
    std::size_t begin = 0;
    std::size_t end = 0;
};

std::vector<KeyHit> keyValues(std::string_view text, std::size_t open, std::size_t close, std::string_view key) {
    std::vector<KeyHit> hits;
    int depth = 0;
    for (std::size_t i = open + 1; i < close && i < text.size();) {
        if (text[i] == '{') {
            ++depth;
            ++i;
            continue;
        }
        if (text[i] == '}') {
            if (depth > 0) {
                --depth;
            }
            ++i;
            continue;
        }
        if (depth == 0 && text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < close && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (depth == 0 && (std::isalpha(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
            const std::size_t keyStart = i;
            ++i;
            while (i < close && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
                ++i;
            }
            const std::string_view found(text.data() + keyStart, i - keyStart);
            while (i < close && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
                ++i;
            }
            if (i < close && text[i] == '=' && sameKey(found, key)) {
                ++i;
                const std::size_t valueStart = i;
                while (i < close && text[i] != ';') {
                    ++i;
                }
                if (i < close && text[i] == ';') {
                    hits.push_back(KeyHit{valueStart, i});
                }
            }
            continue;
        }
        ++i;
    }
    return hits;
}

std::optional<std::string> sectionValue(const std::string& text, std::size_t open, std::size_t close, std::string_view key) {
    const auto hits = keyValues(text, open, close, key);
    if (hits.empty()) {
        return std::nullopt;
    }
    return trimCopy(std::string_view(text.data() + hits.back().begin, hits.back().end - hits.back().begin));
}

void setSectionKey(std::string& text, std::size_t open, std::size_t close, std::string_view key, std::string_view value, std::string_view indent) {
    const auto hits = keyValues(text, open, close, key);
    if (hits.empty()) {
        const std::string line = std::string(newlineOf(text)) + std::string(indent) + std::string(key) + "=" + std::string(value) + ";";
        text.insert(open + 1, line);
        return;
    }
    for (std::size_t i = hits.size(); i > 0; --i) {
        const KeyHit& hit = hits[i - 1];
        text.replace(hit.begin, hit.end - hit.begin, value);
    }
}

void setGlobalKey(std::string& text, std::string_view key, std::string_view value, bool insertIfMissing) {
    std::size_t open = 0;
    std::size_t close = 0;
    if (!findGlobalHeader(text, open, close)) {
        return;
    }
    if (!insertIfMissing && keyValues(text, open, close, key).empty()) {
        return;
    }
    setSectionKey(text, open, close, key, value, "\t");
}

void setSchemaKey(std::string& text, std::string_view key, std::string_view value, bool insertIfMissing) {
    for (int index = 0; index < 8; ++index) {
        std::size_t open = 0;
        std::size_t close = 0;
        if (!findSchema(text, index, open, close)) {
            break;
        }
        if (!insertIfMissing && keyValues(text, open, close, key).empty()) {
            continue;
        }
        setSectionKey(text, open, close, key, value, "\t\t");
    }
}

int settingInt(const std::optional<std::string>& value, int fallback) {
    return value ? parseInt(*value) : fallback;
}

double settingDouble(const std::optional<std::string>& value, double fallback) {
    return value ? parseDouble(*value) : fallback;
}

bool settingFlag(const std::optional<std::string>& value, bool fallback) {
    return value ? parseFlag(*value) : fallback;
}

std::string settingText(const std::optional<std::string>& value, std::string fallback = {}) {
    return value ? *value : std::move(fallback);
}

}  // namespace

MapSettings readMapSettings(const LoadedMap& map) {
    MapSettings settings;
    settings.seaLevel = map.seaLevel;
    settings.name = map.missionName.empty() ? "Untitled Map" : map.missionName;
    if (map.otaText.empty()) {
        settings.description = defaultDescription(map);
        return settings;
    }
    const std::string text(map.otaText.begin(), map.otaText.end());
    std::size_t open = 0;
    std::size_t close = 0;
    if (findGlobalHeader(text, open, close)) {
        settings.name = settingText(sectionValue(text, open, close, "missionname"), settings.name);
        settings.description = settingText(sectionValue(text, open, close, "missiondescription"));
        settings.planet = settingText(sectionValue(text, open, close, "planet"));
        settings.players = settingText(sectionValue(text, open, close, "numplayers"));
        settings.memory = settingText(sectionValue(text, open, close, "memory"));
        settings.missionHint = settingText(sectionValue(text, open, close, "missionhint"));
        settings.brief = settingText(sectionValue(text, open, close, "brief"));
        settings.narration = settingText(sectionValue(text, open, close, "narration"));
        settings.glamour = settingText(sectionValue(text, open, close, "glamour"));
        settings.useOnlyUnits = settingText(sectionValue(text, open, close, "useonlyunits"));
        settings.minWind = settingInt(sectionValue(text, open, close, "minwindspeed"), 0);
        settings.maxWind = settingInt(sectionValue(text, open, close, "maxwindspeed"), 0);
        settings.tidalStrength = settingInt(sectionValue(text, open, close, "tidalstrength"), 0);
        settings.solarStrength = settingInt(sectionValue(text, open, close, "solarstrength"), 0);
        settings.gravity = settingInt(sectionValue(text, open, close, "gravity"), 0);
        settings.waterDamage = settingInt(sectionValue(text, open, close, "waterdamage"), 0);
        settings.lavaWorld = settingFlag(sectionValue(text, open, close, "lavaworld"), false);
        settings.waterDoesDamage = settingFlag(sectionValue(text, open, close, "waterdoesdamage"), false);
        settings.waterBlocksShots = settingFlag(sectionValue(text, open, close, "nosealeveltrigger"), false);
        settings.lineOfSight = settingFlag(sectionValue(text, open, close, "lineofsight"), false);
        settings.mapping = settingFlag(sectionValue(text, open, close, "mapping"), false);
        settings.killMultiplier = settingDouble(sectionValue(text, open, close, "killmul"), 0);
        settings.timeMultiplier = settingDouble(sectionValue(text, open, close, "timemul"), 0);
        settings.maxUnits = settingInt(sectionValue(text, open, close, "MaxUnits"), 0);
    }
    if (findSchema(text, 0, open, close)) {
        settings.aiProfile = settingText(sectionValue(text, open, close, "aiprofile"), "DEFAULT");
        if (settings.aiProfile.empty()) {
            settings.aiProfile = "DEFAULT";
        }
        settings.surfaceMetal = settingInt(sectionValue(text, open, close, "SurfaceMetal"), 0);
        settings.mohoMetal = settingInt(sectionValue(text, open, close, "MohoMetal"), 0);
        settings.humanMetal = settingInt(sectionValue(text, open, close, "HumanMetal"), 0);
        settings.humanEnergy = settingInt(sectionValue(text, open, close, "HumanEnergy"), 0);
        settings.computerMetal = settingInt(sectionValue(text, open, close, "ComputerMetal"), 0);
        settings.computerEnergy = settingInt(sectionValue(text, open, close, "ComputerEnergy"), 0);
        settings.meteorWeapon = settingText(sectionValue(text, open, close, "MeteorWeapon"));
        settings.meteorRadius = settingInt(sectionValue(text, open, close, "MeteorRadius"), 0);
        settings.meteorDensity = settingDouble(sectionValue(text, open, close, "MeteorDensity"), 0);
        settings.meteorDuration = settingInt(sectionValue(text, open, close, "MeteorDuration"), 0);
        settings.meteorInterval = settingInt(sectionValue(text, open, close, "MeteorInterval"), 0);
    }
    return settings;
}

void applyMapSettings(LoadedMap& map, const MapSettings& settings) {
    auto clampInt = [](int value, int min, int max) {
        if (value < min) {
            return min;
        }
        if (value > max) {
            return max;
        }
        return value;
    };
    map.seaLevel = clampInt(settings.seaLevel, 0, 255);
    map.missionName = tdfValue(trimCopy(settings.name));
    if (map.missionName.empty()) {
        map.missionName = "Untitled Map";
    }
    std::string text;
    if (!map.otaText.empty()) {
        text.assign(map.otaText.begin(), map.otaText.end());
    }
    std::size_t headerOpen = 0;
    std::size_t headerClose = 0;
    if (text.empty() || !findGlobalHeader(text, headerOpen, headerClose)) {
        const auto built = buildDefaultOta(map);
        text.assign(built.begin(), built.end());
    }
    std::size_t schemaOpen = 0;
    std::size_t schemaClose = 0;
    if (!findSchema(text, 0, schemaOpen, schemaClose) && findGlobalHeader(text, headerOpen, headerClose)) {
        const std::string nl(newlineOf(text));
        text.insert(headerClose, nl + "\t[Schema 0]" + nl + "\t\t{" + nl + "\t\tType=Network 1;" + nl + "\t\t}");
    }
    const auto flag = [](bool value) { return value ? "1" : "0"; };
    const bool hadWaterBlock = [&] {
        std::size_t open = 0;
        std::size_t close = 0;
        return findGlobalHeader(text, open, close) && !keyValues(text, open, close, "nosealeveltrigger").empty();
    }();
    const bool hadMaxUnits = [&] {
        std::size_t open = 0;
        std::size_t close = 0;
        return findGlobalHeader(text, open, close) && !keyValues(text, open, close, "MaxUnits").empty();
    }();
    setGlobalKey(text, "missionname", map.missionName, true);
    setGlobalKey(text, "missiondescription", tdfValue(settings.description), true);
    setGlobalKey(text, "planet", tdfValue(settings.planet), true);
    setGlobalKey(text, "numplayers", tdfValue(settings.players), true);
    setGlobalKey(text, "memory", tdfValue(settings.memory), true);
    setGlobalKey(text, "missionhint", tdfValue(settings.missionHint), true);
    setGlobalKey(text, "brief", tdfValue(settings.brief), true);
    setGlobalKey(text, "narration", tdfValue(settings.narration), true);
    setGlobalKey(text, "glamour", tdfValue(settings.glamour), true);
    setGlobalKey(text, "useonlyunits", tdfValue(settings.useOnlyUnits), true);
    setGlobalKey(text, "minwindspeed", std::to_string(clampInt(settings.minWind, 0, 20000)), true);
    setGlobalKey(text, "maxwindspeed", std::to_string(clampInt(settings.maxWind, 0, 20000)), true);
    setGlobalKey(text, "tidalstrength", std::to_string(clampInt(settings.tidalStrength, 0, 20000)), true);
    setGlobalKey(text, "solarstrength", std::to_string(clampInt(settings.solarStrength, 0, 20000)), true);
    setGlobalKey(text, "gravity", std::to_string(clampInt(settings.gravity, 0, 20000)), true);
    setGlobalKey(text, "waterdamage", std::to_string(clampInt(settings.waterDamage, 0, 20000)), true);
    setGlobalKey(text, "lavaworld", flag(settings.lavaWorld), true);
    setGlobalKey(text, "waterdoesdamage", flag(settings.waterDoesDamage), true);
    setGlobalKey(text, "lineofsight", flag(settings.lineOfSight), true);
    setGlobalKey(text, "mapping", flag(settings.mapping), true);
    setGlobalKey(text, "killmul", formatDouble(settings.killMultiplier), true);
    setGlobalKey(text, "timemul", formatDouble(settings.timeMultiplier), true);
    setGlobalKey(text, "nosealeveltrigger", flag(settings.waterBlocksShots), hadWaterBlock || settings.waterBlocksShots);
    setGlobalKey(text, "MaxUnits", std::to_string(clampInt(settings.maxUnits, 0, 5000)), hadMaxUnits || settings.maxUnits > 0);
    const std::string profile = tdfValue(trimCopy(settings.aiProfile).empty() ? "DEFAULT" : settings.aiProfile);
    setSchemaKey(text, "aiprofile", profile, true);
    setSchemaKey(text, "SurfaceMetal", std::to_string(clampInt(settings.surfaceMetal, 0, 20000)), true);
    setSchemaKey(text, "MohoMetal", std::to_string(clampInt(settings.mohoMetal, 0, 20000)), true);
    setSchemaKey(text, "HumanMetal", std::to_string(clampInt(settings.humanMetal, 0, 2000000)), true);
    setSchemaKey(text, "HumanEnergy", std::to_string(clampInt(settings.humanEnergy, 0, 2000000)), true);
    setSchemaKey(text, "ComputerMetal", std::to_string(clampInt(settings.computerMetal, 0, 2000000)), true);
    setSchemaKey(text, "ComputerEnergy", std::to_string(clampInt(settings.computerEnergy, 0, 2000000)), true);
    setSchemaKey(text, "MeteorWeapon", tdfValue(settings.meteorWeapon), true);
    setSchemaKey(text, "MeteorRadius", std::to_string(clampInt(settings.meteorRadius, 0, 20000)), true);
    setSchemaKey(text, "MeteorDensity", formatDouble(settings.meteorDensity < 0 ? 0 : settings.meteorDensity), true);
    setSchemaKey(text, "MeteorDuration", std::to_string(clampInt(settings.meteorDuration, 0, 20000)), true);
    setSchemaKey(text, "MeteorInterval", std::to_string(clampInt(settings.meteorInterval, 0, 20000)), true);
    map.otaText.assign(text.begin(), text.end());
}

void writeFeatureChange(FeatureChange* change, int cellX, int cellY, std::uint16_t before, std::uint16_t after, std::string addedName) {
    if (change == nullptr) {
        return;
    }
    change->cellX = cellX;
    change->cellY = cellY;
    change->before = before;
    change->after = after;
    change->addedName = std::move(addedName);
}

FeatureEdit placeMapFeature(LoadedMap& map, int cellX, int cellY, const std::string& name, FeatureChange* change) {
    std::size_t index = 0;
    if (!featureCell(map, cellX, cellY, index)) {
        return FeatureEdit::Unchanged;
    }
    if (map.featureIndices[index] == kVoidFeature) {
        return FeatureEdit::OpenSpace;
    }
    const std::string stored = trimmedFeatureName(name);
    if (stored.empty()) {
        return FeatureEdit::Unchanged;
    }
    int nameIndex = findFeatureName(map, stored);
    if (nameIndex >= static_cast<int>(kVoidFeature)) {
        return FeatureEdit::Full;
    }
    if (nameIndex >= 0 && map.featureIndices[index] == static_cast<std::uint16_t>(nameIndex)) {
        return FeatureEdit::Unchanged;
    }
    const std::uint16_t before = map.featureIndices[index];
    std::string addedName;
    if (nameIndex < 0) {
        if (map.featureNames.size() >= kMaxFeatureNames) {
            return FeatureEdit::Full;
        }
        map.featureNames.push_back(stored);
        nameIndex = static_cast<int>(map.featureNames.size() - 1);
        addedName = stored;
    }
    map.featureIndices[index] = static_cast<std::uint16_t>(nameIndex);
    rebuildFeaturePlacements(map);
    writeFeatureChange(change, cellX, cellY, before, static_cast<std::uint16_t>(nameIndex), std::move(addedName));
    return FeatureEdit::Placed;
}

FeatureEdit removeMapFeature(LoadedMap& map, int cellX, int cellY, FeatureChange* change) {
    std::size_t index = 0;
    if (!featureCell(map, cellX, cellY, index)) {
        return FeatureEdit::Unchanged;
    }
    const std::uint16_t feature = map.featureIndices[index];
    if (feature == kVoidFeature) {
        return FeatureEdit::OpenSpace;
    }
    if (isSpecialFeature(feature)) {
        return FeatureEdit::Unchanged;
    }
    map.featureIndices[index] = kNoFeature;
    rebuildFeaturePlacements(map);
    writeFeatureChange(change, cellX, cellY, feature, kNoFeature, {});
    return FeatureEdit::Removed;
}

void revertFeatureChange(LoadedMap& map, const FeatureChange& change) {
    std::size_t index = 0;
    if (!featureCell(map, change.cellX, change.cellY, index)) {
        throw std::runtime_error("Feature edit no longer fits this map");
    }
    if (!change.addedName.empty()) {
        const bool tail = !map.featureNames.empty()
            && map.featureNames.back() == change.addedName
            && map.featureNames.size() == static_cast<std::size_t>(change.after) + 1;
        if (!tail) {
            throw std::runtime_error("Feature edit no longer fits this map");
        }
        map.featureNames.pop_back();
    }
    map.featureIndices[index] = change.before;
    rebuildFeaturePlacements(map);
}

void reapplyFeatureChange(LoadedMap& map, const FeatureChange& change) {
    std::size_t index = 0;
    if (!featureCell(map, change.cellX, change.cellY, index)) {
        throw std::runtime_error("Feature edit no longer fits this map");
    }
    if (!change.addedName.empty()) {
        if (map.featureNames.size() == static_cast<std::size_t>(change.after)) {
            map.featureNames.push_back(change.addedName);
        } else if (!(change.after < map.featureNames.size() && map.featureNames[change.after] == change.addedName)) {
            throw std::runtime_error("Feature edit no longer fits this map");
        }
    } else if (!isSpecialFeature(change.after) && change.after >= map.featureNames.size()) {
        throw std::runtime_error("Feature edit no longer fits this map");
    }
    map.featureIndices[index] = change.after;
    rebuildFeaturePlacements(map);
}

LoadedMap createBlankMap(int tileWidth, int tileHeight) {
    require(tileWidth >= kMinMapTiles && tileHeight >= kMinMapTiles && tileWidth <= kMaxMapTiles && tileHeight <= kMaxMapTiles,
        "Map size must be between 1 and 2048 tiles");
    const auto tiles = static_cast<std::size_t>(tileWidth) * static_cast<std::size_t>(tileHeight);
    const auto cells = static_cast<std::size_t>(tileWidth) * 2 * static_cast<std::size_t>(tileHeight) * 2;
    LoadedMap map;
    map.displayName = "Untitled Map";
    map.missionName = "Untitled Map";
    map.tileWidth = tileWidth;
    map.tileHeight = tileHeight;
    map.seaLevel = 75;
    map.tileCount = 1;
    map.indices.assign(tiles, 0);
    map.tileImages.assign(1024, 0x64);
    map.heights.assign(cells, 0);
    map.featureIndices.assign(cells, 0xFFFF);
    map.featurePadding.assign(cells, 0);
    return map;
}

LoadedMap loadMapFile(const std::filesystem::path& path) {
    const std::string ext = lowerExt(path);
    LoadedMap map;
    map.source = path;
    map.displayName = path.filename().string();

    if (ext == ".tnt") {
        const auto tnt = readEntireFile(path);
        auto otaPath = path;
        otaPath.replace_extension(".ota");
        if (std::filesystem::exists(otaPath)) {
            const auto ota = readEntireFile(otaPath);
            return loadTntAndOta(tnt, &ota, std::move(map));
        }
        return loadTntAndOta(tnt, nullptr, std::move(map));
    }

    require(ext == ".ufo" || ext == ".hpi" || ext == ".ccx" || ext == ".gpf" || ext == ".gp3",
        "Open a .ufo, .hpi, or .tnt map");

    const HpiArchive archive = HpiArchive::open(path);
    const HpiFile* tntFile = nullptr;
    for (const HpiFile& file : archive.files()) {
        if (file.path.size() >= 4 && iequals(file.path.substr(file.path.size() - 4), ".tnt")) {
            tntFile = &file;
            if (file.path.size() >= 6 && iequals(file.path.substr(0, 5), "maps/")) {
                break;
            }
        }
    }
    require(tntFile != nullptr, "That archive does not contain a TNT map");

    std::string otaName = tntFile->path;
    otaName.replace(otaName.size() - 4, 4, ".ota");
    const HpiFile* otaFile = archive.find(otaName);
    const auto tnt = archive.extract(*tntFile);
    if (otaFile != nullptr) {
        const auto ota = archive.extract(*otaFile);
        map.displayName = std::filesystem::path(tntFile->path).stem().string();
        return loadTntAndOta(tnt, &ota, std::move(map));
    }
    map.displayName = std::filesystem::path(tntFile->path).stem().string();
    return loadTntAndOta(tnt, nullptr, std::move(map));
}

void saveMapFile(const LoadedMap& map, const std::filesystem::path& path) {
    const auto tnt = buildTnt(map);
    const auto ota = otaForSave(map);
    const std::string ext = lowerExt(path);
    if (ext == ".tnt") {
        writeBinaryFile(path, tnt);
        auto otaPath = path;
        otaPath.replace_extension(".ota");
        writeBinaryFile(otaPath, ota);
        return;
    }
    require(ext == ".ufo" || ext == ".hpi" || ext == ".ccx" || ext == ".gpf" || ext == ".gp3",
        "Save as a .ufo, .hpi, or .tnt map");
    std::string stem = path.stem().string();
    if (stem.empty()) {
        stem = "map";
    }
    std::vector<HpiWriteFile> files(2);
    files[0].path = "maps/" + stem + ".tnt";
    files[0].bytes = tnt;
    files[1].path = "maps/" + stem + ".ota";
    files[1].bytes = ota;
    writeHpiArchive(path, std::move(files));
}

std::string backupFilename(std::string_view mapName, int month, int day, int year, int hour, int minute) {
    char suffix[40];
    std::snprintf(suffix, sizeof(suffix), "-%02d-%02d-%04d-%02d-%02d-Backup.ufo", month, day, year, hour, minute);
    return std::string(mapName) + suffix;
}

namespace {

bool takeDigits(std::string_view& text, int digits, int& value) {
    if (text.size() < static_cast<std::size_t>(digits)) {
        return false;
    }
    value = 0;
    const std::size_t start = text.size() - static_cast<std::size_t>(digits);
    for (std::size_t i = start; i < text.size(); ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10 + (text[i] - '0');
    }
    text.remove_suffix(static_cast<std::size_t>(digits));
    return true;
}

bool takeHyphen(std::string_view& text) {
    if (text.empty() || text.back() != '-') {
        return false;
    }
    text.remove_suffix(1);
    return true;
}

}  // namespace

bool isBackupFilename(std::string_view mapName, std::string_view filename, int& month, int& day, int& year, int& hour, int& minute) {
    constexpr std::string_view tail = "-Backup.ufo";
    if (mapName.empty() || filename.size() <= mapName.size() + tail.size() || filename.substr(filename.size() - tail.size()) != tail) {
        return false;
    }
    std::string_view rest = filename.substr(0, filename.size() - tail.size());
    int parsedMinute = 0;
    int parsedHour = 0;
    int parsedYear = 0;
    int parsedDay = 0;
    int parsedMonth = 0;
    if (!takeDigits(rest, 2, parsedMinute) || !takeHyphen(rest)
        || !takeDigits(rest, 2, parsedHour) || !takeHyphen(rest)
        || !takeDigits(rest, 4, parsedYear) || !takeHyphen(rest)
        || !takeDigits(rest, 2, parsedDay) || !takeHyphen(rest)
        || !takeDigits(rest, 2, parsedMonth) || !takeHyphen(rest)) {
        return false;
    }
    if (rest != mapName) {
        return false;
    }
    if (parsedMonth < 1 || parsedMonth > 12 || parsedDay < 1 || parsedDay > 31 || parsedHour > 23 || parsedMinute > 59 || parsedYear < 1) {
        return false;
    }
    month = parsedMonth;
    day = parsedDay;
    year = parsedYear;
    hour = parsedHour;
    minute = parsedMinute;
    return true;
}

std::vector<std::string> backupNamesToDelete(const std::vector<std::string>& filenames, std::string_view mapName, int maxBackups) {
    const int limit = std::max(maxBackups, 1);
    struct Backup {
        std::string name;
        int year = 0;
        int month = 0;
        int day = 0;
        int hour = 0;
        int minute = 0;
    };
    std::vector<Backup> found;
    for (const std::string& filename : filenames) {
        Backup backup;
        if (!isBackupFilename(mapName, filename, backup.month, backup.day, backup.year, backup.hour, backup.minute)) {
            continue;
        }
        backup.name = filename;
        found.push_back(std::move(backup));
    }
    std::sort(found.begin(), found.end(), [](const Backup& a, const Backup& b) {
        if (a.year != b.year) {
            return a.year < b.year;
        }
        if (a.month != b.month) {
            return a.month < b.month;
        }
        if (a.day != b.day) {
            return a.day < b.day;
        }
        if (a.hour != b.hour) {
            return a.hour < b.hour;
        }
        if (a.minute != b.minute) {
            return a.minute < b.minute;
        }
        return a.name < b.name;
    });
    std::vector<std::string> remove;
    while (static_cast<int>(found.size()) >= limit) {
        remove.push_back(found.front().name);
        found.erase(found.begin());
    }
    return remove;
}

std::vector<std::uint8_t> renderMapRgba(const LoadedMap& map) {
    const int width = map.pixelWidth();
    const int height = map.pixelHeight();
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);

    for (int ty = 0; ty < map.tileHeight; ++ty) {
        for (int tx = 0; tx < map.tileWidth; ++tx) {
            const std::uint32_t index = map.indices[static_cast<std::size_t>(ty * map.tileWidth + tx)];
            const std::uint8_t* tile = nullptr;
            if (index < static_cast<std::uint32_t>(map.tileCount)) {
                tile = map.tileImages.data() + static_cast<std::size_t>(index) * 1024;
            }
            for (int py = 0; py < 32; ++py) {
                std::uint8_t* dst = rgba.data()
                    + (static_cast<std::size_t>(ty * 32 + py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(tx * 32)) * 4;
                for (int px = 0; px < 32; ++px) {
                    const std::uint8_t paletteIndex = tile == nullptr ? 0 : tile[py * 32 + px];
                    dst[0] = kTaPalette[paletteIndex][0];
                    dst[1] = kTaPalette[paletteIndex][1];
                    dst[2] = kTaPalette[paletteIndex][2];
                    dst[3] = 255;
                    dst += 4;
                }
            }
        }
    }
    return rgba;
}

void renderMapView(
    const LoadedMap& map,
    std::uint8_t* rgba,
    int textureWidth,
    int textureHeight,
    float canvasWidth,
    float canvasHeight,
    float panX,
    float panY,
    float zoom) {
    const int mapWidth = map.pixelWidth();
    const int mapHeight = map.pixelHeight();
    const float invZoom = zoom == 0.0f ? 0.0f : 1.0f / zoom;
    const std::uint8_t* images = map.tileImages.data();
    const std::uint32_t* indices = map.indices.data();

    for (int sy = 0; sy < textureHeight; ++sy) {
        const float canvasY = (static_cast<float>(sy) + 0.5f) * canvasHeight / static_cast<float>(textureHeight);
        const int mapY = static_cast<int>(std::floor((canvasY - panY) * invZoom));
        std::uint8_t* row = rgba + static_cast<std::size_t>(sy * textureWidth) * 4;
        const bool rowOutside = mapY < 0 || mapY >= mapHeight;
        const int tileY = rowOutside ? 0 : (mapY >> 5);
        const int pixelY = rowOutside ? 0 : (mapY & 31);
        const std::uint32_t* indexRow = rowOutside ? nullptr : indices + static_cast<std::size_t>(tileY * map.tileWidth);

        for (int sx = 0; sx < textureWidth; ++sx) {
            std::uint8_t* dst = row + static_cast<std::size_t>(sx) * 4;
            if (rowOutside) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
                continue;
            }
            const float canvasX = (static_cast<float>(sx) + 0.5f) * canvasWidth / static_cast<float>(textureWidth);
            const int mapX = static_cast<int>(std::floor((canvasX - panX) * invZoom));
            if (mapX < 0 || mapX >= mapWidth) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
                continue;
            }
            const std::uint32_t index = indexRow[mapX >> 5];
            const std::uint8_t paletteIndex = index < static_cast<std::uint32_t>(map.tileCount)
                ? images[static_cast<std::size_t>(index) * 1024 + static_cast<std::size_t>(pixelY * 32 + (mapX & 31))]
                : 0;
            dst[0] = kTaPalette[paletteIndex][0];
            dst[1] = kTaPalette[paletteIndex][1];
            dst[2] = kTaPalette[paletteIndex][2];
            dst[3] = 255;
        }
    }
}

int sampleHeight(const LoadedMap& map, int mapX, int mapY) {
    if (mapX < 0 || mapY < 0 || mapX >= map.pixelWidth() || mapY >= map.pixelHeight() || map.heights.empty()) {
        return -1;
    }
    const int hx = mapX >> 4;
    const int hy = mapY >> 4;
    if (hx < 0 || hy < 0 || hx >= map.heightWidth() || hy >= map.heightHeight()) {
        return -1;
    }
    const std::size_t index = static_cast<std::size_t>(hy) * static_cast<std::size_t>(map.heightWidth()) + static_cast<std::size_t>(hx);
    if (index >= map.heights.size()) {
        return -1;
    }
    return map.heights[index];
}

std::uint8_t lerpByte(int from, int to, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(std::lround(from + (to - from) * clamped));
}

void heightColor(int height, int seaLevel, std::uint8_t& red, std::uint8_t& green, std::uint8_t& blue) {
    if (height < seaLevel) {
        const float t = seaLevel <= 0 ? 1.0f : static_cast<float>(height) / static_cast<float>(seaLevel);
        red = lerpByte(16, 48, t);
        green = lerpByte(42, 140, t);
        blue = lerpByte(120, 220, t);
        return;
    }
    const int rise = height - seaLevel;
    if (rise < 30) {
        const float t = static_cast<float>(rise) / 30.0f;
        red = lerpByte(40, 72, t);
        green = lerpByte(125, 158, t);
        blue = lerpByte(62, 48, t);
        return;
    }
    if (rise < 70) {
        const float t = static_cast<float>(rise - 30) / 40.0f;
        red = lerpByte(72, 220, t);
        green = lerpByte(158, 196, t);
        blue = lerpByte(48, 42, t);
        return;
    }
    if (rise < 130) {
        const float t = static_cast<float>(rise - 70) / 60.0f;
        red = lerpByte(220, 196, t);
        green = lerpByte(196, 86, t);
        blue = lerpByte(42, 32, t);
        return;
    }
    const float t = static_cast<float>(rise - 130) / 55.0f;
    red = lerpByte(196, 245, t);
    green = lerpByte(86, 245, t);
    blue = lerpByte(32, 245, t);
}

void renderHeightGrid(
    const LoadedMap& map,
    std::uint8_t* rgba,
    int textureWidth,
    int textureHeight,
    float canvasWidth,
    float canvasHeight,
    float panX,
    float panY,
    float zoom,
    int gridSize) {
    if (textureWidth <= 0 || textureHeight <= 0 || canvasWidth <= 0.0f || canvasHeight <= 0.0f || zoom == 0.0f) {
        return;
    }
    const int grid = std::max(gridSize, 1);
    const float texelPerMapX = zoom * static_cast<float>(textureWidth) / canvasWidth;
    const float texelPerMapY = zoom * static_cast<float>(textureHeight) / canvasHeight;
    const float cellScreen = static_cast<float>(grid) * std::min(texelPerMapX, texelPerMapY);
    const bool merged = cellScreen < 3.0f;

    for (int sy = 0; sy < textureHeight; ++sy) {
        const float canvasY = (static_cast<float>(sy) + 0.5f) * canvasHeight / static_cast<float>(textureHeight);
        const float mapYf = (canvasY - panY) / zoom;
        const int mapY = static_cast<int>(std::floor(mapYf));
        std::uint8_t* row = rgba + static_cast<std::size_t>(sy * textureWidth) * 4;
        for (int sx = 0; sx < textureWidth; ++sx) {
            std::uint8_t* dst = row + static_cast<std::size_t>(sx) * 4;
            const float canvasX = (static_cast<float>(sx) + 0.5f) * canvasWidth / static_cast<float>(textureWidth);
            const float mapXf = (canvasX - panX) / zoom;
            const int mapX = static_cast<int>(std::floor(mapXf));
            const int height = sampleHeight(map, mapX, mapY);
            if (height < 0) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
                continue;
            }

            const float distX = std::fabs(mapXf / static_cast<float>(grid) - std::round(mapXf / static_cast<float>(grid))) * static_cast<float>(grid);
            const float distY = std::fabs(mapYf / static_cast<float>(grid) - std::round(mapYf / static_cast<float>(grid))) * static_cast<float>(grid);
            const bool onLine = distX * texelPerMapX <= 0.65f || distY * texelPerMapY <= 0.65f;

            std::uint8_t red = 0;
            std::uint8_t green = 0;
            std::uint8_t blue = 0;
            heightColor(height, map.seaLevel, red, green, blue);
            if (onLine && !merged) {
                int slope = 0;
                const int neighbors[] = {
                    sampleHeight(map, mapX - 16, mapY),
                    sampleHeight(map, mapX + 16, mapY),
                    sampleHeight(map, mapX, mapY - 16),
                    sampleHeight(map, mapX, mapY + 16),
                };
                for (const int neighbor : neighbors) {
                    if (neighbor >= 0) {
                        slope = std::max(slope, std::abs(neighbor - height));
                    }
                }
                if (slope >= 20) {
                    red = lerpByte(red, 255, 0.45f);
                    green = lerpByte(green, 255, 0.45f);
                    blue = lerpByte(blue, 255, 0.45f);
                }
            }

            dst[0] = red;
            dst[1] = green;
            dst[2] = blue;
            if (merged) {
                dst[3] = 128;
            } else if (onLine) {
                dst[3] = 230;
            } else {
                dst[3] = 78;
            }
        }
    }
}

}  // namespace galaxy

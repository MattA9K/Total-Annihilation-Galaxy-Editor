#include "core/MapGenerator.hpp"

#include "format/SctSection.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy {

std::string tilesetFamilyKey(std::string_view world) {
    std::string name(world);
    for (char& character : name) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    const auto mark = name.find("_new");
    if (mark != std::string::npos && mark > 0) {
        const std::size_t after = mark + 4;
        if (after == name.size() || name[after] == '_' || std::isdigit(static_cast<unsigned char>(name[after])) != 0) {
            name.resize(mark);
        }
    }
    return name;
}

namespace {

constexpr int kCell = 16;
constexpr int kEdge = 32;

enum class Dir : int { East = 0, South = 1, West = 2, North = 3 };

// Corner bits are NW, NE, SE, SW. A filled corner is plateau or water.
// The 12 non-diagonal masks are the Happyland / marching-squares set:
// straights, outer corners, and inner corners.
struct MarchCandidate {
    int index = -1;
    int mask = 0;
    std::array<int, 4> bits{};
    std::array<std::array<int, kEdge>, 4> edges{};
};

struct Catalog {
    std::vector<SectionStamp> stamps;
    std::vector<int> ground;
    std::vector<int> plateau;
    std::vector<int> water;
    std::array<std::vector<int>, 16> rivers{};
    std::array<int, 16> cliffPiece{};
    std::array<int, 16> coastPiece{};
    int plateauPiece = -1;
    int waterPiece = -1;
    bool cliffReady = false;
    bool coastReady = false;
    std::vector<int> hills;
    std::vector<int> mountains;
    std::vector<int> pools;
};

std::string lowerCopy(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return out;
}

bool contains(std::string_view haystack, std::string_view needle) {
    return lowerCopy(haystack).find(needle) != std::string::npos;
}

int clampPercent(int value) {
    return std::clamp(value, 0, 100);
}

int delta(int left, int right) {
    return left > right ? left - right : right - left;
}

Dir leftOf(Dir dir) {
    return static_cast<Dir>((static_cast<int>(dir) + 3) % 4);
}

Dir rightOf(Dir dir) {
    return static_cast<Dir>((static_cast<int>(dir) + 1) % 4);
}

struct Offset {
    int x = 0;
    int y = 0;
};

Offset stepOf(Dir dir) {
    switch (dir) {
    case Dir::East:
        return {1, 0};
    case Dir::South:
        return {0, 1};
    case Dir::West:
        return {-1, 0};
    case Dir::North:
        return {0, -1};
    }
    return {};
}

int heightSpan(const SectionStamp& stamp) {
    if (stamp.heights.empty()) {
        return 0;
    }
    const auto [low, high] = std::minmax_element(stamp.heights.begin(), stamp.heights.end());
    return static_cast<int>(*high) - static_cast<int>(*low);
}

int meanHeight(const SectionStamp& stamp) {
    if (stamp.heights.empty()) {
        return 0;
    }
    long sum = 0;
    for (std::uint8_t height : stamp.heights) {
        sum += height;
    }
    return static_cast<int>(sum / static_cast<long>(stamp.heights.size()));
}

std::array<int, kEdge> sampleEdge(const std::vector<std::uint8_t>& heights, int width, int height, int edge) {
    std::array<int, kEdge> out{};
    const int length = (edge == 0 || edge == 2) ? width : height;
    for (int i = 0; i < kEdge; ++i) {
        const int pos = length <= 1 ? 0 : (i * (length - 1) + (kEdge / 2)) / (kEdge - 1);
        int x = 0;
        int y = 0;
        if (edge == 0) {
            x = pos;
            y = 0;
        } else if (edge == 1) {
            x = width - 1;
            y = pos;
        } else if (edge == 2) {
            x = pos;
            y = height - 1;
        } else {
            x = 0;
            y = pos;
        }
        out[static_cast<std::size_t>(i)] = heights[static_cast<std::size_t>(y * width + x)];
    }
    return out;
}

bool openWaterTile(const SectionStamp& stamp, int seaLevel) {
    if (stamp.tileWidth <= 0 || stamp.tileHeight <= 0 || stamp.tileWidth > kCell || stamp.tileHeight > kCell) {
        return false;
    }
    if (kCell % stamp.tileWidth != 0 || kCell % stamp.tileHeight != 0) {
        return false;
    }
    const int average = meanHeight(stamp);
    if (average > 15 || average >= seaLevel) {
        return false;
    }
    const int width = stamp.tileWidth * 2;
    const int height = stamp.tileHeight * 2;
    if (width <= 1 || height <= 1 || static_cast<int>(stamp.heights.size()) < width * height) {
        return false;
    }
    for (int edge = 0; edge < 4; ++edge) {
        for (int sample : sampleEdge(stamp.heights, width, height, edge)) {
            if (sample > 15) {
                return false;
            }
        }
    }
    return true;
}

// A banked channel has grass at both ends and a deep notch in the middle.
// A shallow sheet or a sea mouth fails this and is left out, because those
// tiles read as a solid blue square instead of a river.
int riverEdgeKind(const std::array<int, kEdge>& samples) {
    int low = samples[0];
    for (int sample : samples) {
        low = std::min(low, sample);
    }
    const int ends = std::min(samples.front(), samples.back());
    const int middle = samples[kEdge / 2];
    if (ends >= 80 && low <= 40 && middle <= 55) {
        return 1;
    }
    if (low >= 78) {
        return 0;
    }
    return -1;
}

std::optional<int> bankedRiverMask(const SectionStamp& stamp) {
    if (stamp.tileWidth != kCell || stamp.tileHeight != kCell) {
        return std::nullopt;
    }
    const int width = stamp.tileWidth * 2;
    const int height = stamp.tileHeight * 2;
    if (width != kEdge || height != kEdge || static_cast<int>(stamp.heights.size()) < width * height) {
        return std::nullopt;
    }
    int mask = 0;
    for (int edge = 0; edge < 4; ++edge) {
        const int kind = riverEdgeKind(sampleEdge(stamp.heights, width, height, edge));
        if (kind < 0) {
            return std::nullopt;
        }
        if (kind == 1) {
            mask |= 1 << edge;
        }
    }
    if (mask == 0) {
        return std::nullopt;
    }
    return mask;
}

void edgeEnds(const std::array<int, 4>& bits, int edge, bool& startFilled, bool& endFilled) {
    switch (edge) {
    case 0:
        startFilled = bits[0] != 0;
        endFilled = bits[1] != 0;
        break;
    case 1:
        startFilled = bits[1] != 0;
        endFilled = bits[2] != 0;
        break;
    case 2:
        startFilled = bits[3] != 0;
        endFilled = bits[2] != 0;
        break;
    default:
        startFilled = bits[0] != 0;
        endFilled = bits[3] != 0;
        break;
    }
}

bool edgeHorizontal(int edge) {
    return edge == 0 || edge == 2;
}

std::optional<MarchCandidate> marchCandidate(const SectionStamp& stamp, int index, bool water) {
    if (stamp.tileWidth != kCell || stamp.tileHeight != kCell) {
        return std::nullopt;
    }
    const int width = stamp.tileWidth * 2;
    const int height = stamp.tileHeight * 2;
    if (width != kEdge || height != kEdge || static_cast<int>(stamp.heights.size()) < width * height) {
        return std::nullopt;
    }
    constexpr int patch = 6;
    auto mean = [&](int x0, int y0) {
        long sum = 0;
        for (int y = 0; y < patch; ++y) {
            for (int x = 0; x < patch; ++x) {
                sum += stamp.heights[static_cast<std::size_t>((y0 + y) * width + (x0 + x))];
            }
        }
        return static_cast<int>(sum / (patch * patch));
    };
    const int means[4] = {mean(0, 0), mean(width - patch, 0), mean(width - patch, height - patch), mean(0, height - patch)};
    std::array<int, 4> bits{};
    for (int i = 0; i < 4; ++i) {
        if (water) {
            if (means[i] <= 40) {
                bits[static_cast<std::size_t>(i)] = 1;
            } else if (means[i] >= 70) {
                bits[static_cast<std::size_t>(i)] = 0;
            } else {
                return std::nullopt;
            }
        } else if (means[i] >= 190) {
            bits[static_cast<std::size_t>(i)] = 1;
        } else if (means[i] <= 100) {
            bits[static_cast<std::size_t>(i)] = 0;
        } else {
            return std::nullopt;
        }
    }
    const int mask = bits[0] | (bits[1] << 1) | (bits[2] << 2) | (bits[3] << 3);
    if (mask == 0 || mask == 15 || mask == 5 || mask == 10) {
        return std::nullopt;
    }
    MarchCandidate candidate;
    candidate.index = index;
    candidate.mask = mask;
    candidate.bits = bits;
    for (int edge = 0; edge < 4; ++edge) {
        candidate.edges[static_cast<std::size_t>(edge)] = sampleEdge(stamp.heights, width, height, edge);
    }
    return candidate;
}

struct ProfileScore {
    int maxDelta = 0;
    int sum = 0;
};

ProfileScore compareProfiles(const std::array<int, kEdge>& left, const std::array<int, kEdge>& right) {
    ProfileScore score;
    for (int i = 0; i < kEdge; ++i) {
        const int gap = delta(left[static_cast<std::size_t>(i)], right[static_cast<std::size_t>(i)]);
        score.sum += gap;
        score.maxDelta = std::max(score.maxDelta, gap);
    }
    return score;
}

bool betterScore(const ProfileScore& score, int index, const ProfileScore& best, int bestIndex) {
    if (score.maxDelta != best.maxDelta) {
        return score.maxDelta < best.maxDelta;
    }
    if (score.sum != best.sum) {
        return score.sum < best.sum;
    }
    return index < bestIndex;
}

const std::array<int, kEdge>& expectedProfile(
    bool startFilled,
    bool endFilled,
    bool horizontal,
    const std::array<int, kEdge>& flatFilled,
    const std::array<int, kEdge>& flatEmpty,
    const std::array<int, kEdge>& xDown,
    const std::array<int, kEdge>& xUp,
    const std::array<int, kEdge>& yDown,
    const std::array<int, kEdge>& yUp) {
    if (startFilled && endFilled) {
        return flatFilled;
    }
    if (!startFilled && !endFilled) {
        return flatEmpty;
    }
    if (horizontal) {
        return startFilled ? xDown : xUp;
    }
    return startFilled ? yDown : yUp;
}

ProfileScore matchCandidate(
    const MarchCandidate& candidate,
    const std::array<int, kEdge>& flatFilled,
    const std::array<int, kEdge>& flatEmpty,
    const std::array<int, kEdge>& xDown,
    const std::array<int, kEdge>& xUp,
    const std::array<int, kEdge>& yDown,
    const std::array<int, kEdge>& yUp) {
    ProfileScore total;
    for (int edge = 0; edge < 4; ++edge) {
        bool startFilled = false;
        bool endFilled = false;
        edgeEnds(candidate.bits, edge, startFilled, endFilled);
        const ProfileScore score = compareProfiles(
            candidate.edges[static_cast<std::size_t>(edge)],
            expectedProfile(startFilled, endFilled, edgeHorizontal(edge), flatFilled, flatEmpty, xDown, xUp, yDown, yUp));
        total.sum += score.sum;
        total.maxDelta = std::max(total.maxDelta, score.maxDelta);
    }
    return total;
}

void assignMarch(std::array<int, 16>& pieces, const std::vector<MarchCandidate>& candidates, bool water) {
    pieces.fill(-1);
    std::array<std::vector<const MarchCandidate*>, 16> byMask;
    for (const MarchCandidate& candidate : candidates) {
        if (candidate.mask > 0 && candidate.mask < 16) {
            byMask[static_cast<std::size_t>(candidate.mask)].push_back(&candidate);
        }
    }
    const int needed[] = {1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14};
    for (int mask : needed) {
        if (byMask[static_cast<std::size_t>(mask)].empty()) {
            return;
        }
    }
    // Coast ramps disagree by more than cliff ramps. Acid's best set steps by
    // 31. 40 still rejects a raw water-to-grass step, which is about 85.
    const int limit = water ? 40 : 12;
    int bestWorst = limit + 1;
    std::array<int, 16> bestPick{};
    bestPick.fill(-1);
    for (const MarchCandidate* north : byMask[3]) {
        for (const MarchCandidate* east : byMask[6]) {
            for (const MarchCandidate* west : byMask[9]) {
                for (const MarchCandidate* south : byMask[12]) {
                    const std::array<int, kEdge>& flatFilled = north->edges[0];
                    const std::array<int, kEdge>& flatEmpty = north->edges[2];
                    const std::array<int, kEdge>& yDown = north->edges[1];
                    const std::array<int, kEdge>& yUp = south->edges[1];
                    const std::array<int, kEdge>& xDown = west->edges[0];
                    const std::array<int, kEdge>& xUp = east->edges[0];
                    std::array<int, 16> pick{};
                    pick.fill(-1);
                    ProfileScore pickScore[16]{};
                    int worst = 0;
                    bool usable = true;
                    for (const MarchCandidate& candidate : candidates) {
                        if (candidate.mask <= 0 || candidate.mask >= 15 || candidate.mask == 5 || candidate.mask == 10) {
                            continue;
                        }
                        const ProfileScore score = matchCandidate(candidate, flatFilled, flatEmpty, xDown, xUp, yDown, yUp);
                        const std::size_t mask = static_cast<std::size_t>(candidate.mask);
                        if (pick[mask] < 0 || betterScore(score, candidate.index, pickScore[candidate.mask], pick[mask])) {
                            pick[mask] = candidate.index;
                            pickScore[candidate.mask] = score;
                        }
                    }
                    for (int mask : needed) {
                        if (pick[static_cast<std::size_t>(mask)] < 0) {
                            usable = false;
                            break;
                        }
                        worst = std::max(worst, pickScore[mask].maxDelta);
                        if (worst >= bestWorst) {
                            usable = false;
                            break;
                        }
                    }
                    if (usable && worst < bestWorst) {
                        bestWorst = worst;
                        bestPick = pick;
                    }
                }
            }
        }
    }
    if (bestWorst <= limit) {
        pieces = bestPick;
    }
}

int pickFlat(const Catalog& catalog, const std::vector<int>& indices, const std::array<int, kEdge>& target) {
    int best = -1;
    int bestRank = 9;
    int bestMax = 0;
    int bestSum = 0;
    for (int index : indices) {
        const SectionStamp& stamp = catalog.stamps[static_cast<std::size_t>(index)];
        if (stamp.tileWidth <= 0 || stamp.tileHeight <= 0 || stamp.tileWidth > kCell || stamp.tileHeight > kCell) {
            continue;
        }
        if (kCell % stamp.tileWidth != 0 || kCell % stamp.tileHeight != 0) {
            continue;
        }
        const int width = stamp.tileWidth * 2;
        const int height = stamp.tileHeight * 2;
        if (width <= 1 || height <= 1 || static_cast<int>(stamp.heights.size()) < width * height) {
            continue;
        }
        int maxDelta = 0;
        int sum = 0;
        for (int edge = 0; edge < 4; ++edge) {
            const ProfileScore score = compareProfiles(sampleEdge(stamp.heights, width, height, edge), target);
            maxDelta = std::max(maxDelta, score.maxDelta);
            sum += score.sum;
        }
        const int rank = (stamp.tileWidth == kCell && stamp.tileHeight == kCell) ? 0 : 1;
        const bool better = best < 0 || rank < bestRank || (rank == bestRank && (maxDelta < bestMax || (maxDelta == bestMax && sum < bestSum)));
        if (better) {
            best = index;
            bestRank = rank;
            bestMax = maxDelta;
            bestSum = sum;
        }
    }
    return best;
}

void finishMarch(Catalog& catalog, std::array<int, 16>& pieces, int& flatIndex, const std::vector<int>& flats, const std::vector<MarchCandidate>& candidates, bool water) {
    assignMarch(pieces, candidates, water);
    flatIndex = -1;
    const int needed[] = {1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14};
    for (int mask : needed) {
        if (pieces[static_cast<std::size_t>(mask)] < 0) {
            return;
        }
    }
    const SectionStamp& straight = catalog.stamps[static_cast<std::size_t>(pieces[3])];
    const int width = straight.tileWidth * 2;
    const int height = straight.tileHeight * 2;
    const std::array<int, kEdge> target = sampleEdge(straight.heights, width, height, 0);
    flatIndex = pickFlat(catalog, flats, target);
    if (flatIndex < 0) {
        pieces.fill(-1);
        return;
    }
    const SectionStamp& flat = catalog.stamps[static_cast<std::size_t>(flatIndex)];
    const int flatWidth = flat.tileWidth * 2;
    const int flatHeight = flat.tileHeight * 2;
    int flatGap = 0;
    for (int edge = 0; edge < 4; ++edge) {
        flatGap = std::max(flatGap, compareProfiles(sampleEdge(flat.heights, flatWidth, flatHeight, edge), target).maxDelta);
    }
    if (flatGap > 9) {
        flatIndex = -1;
        pieces.fill(-1);
    }
}

int riverProfileDelta(const SectionStamp& left, const SectionStamp& right) {
    const int width = left.tileWidth * 2;
    const int height = left.tileHeight * 2;
    const int otherWidth = right.tileWidth * 2;
    const int otherHeight = right.tileHeight * 2;
    int worst = 0;
    for (int edge = 0; edge < 4; ++edge) {
        worst = std::max(worst, compareProfiles(sampleEdge(left.heights, width, height, edge), sampleEdge(right.heights, otherWidth, otherHeight, edge)).maxDelta);
    }
    return worst;
}

void finishRivers(Catalog& catalog) {
    for (std::vector<int>& list : catalog.rivers) {
        if (list.size() <= 1) {
            continue;
        }
        int medoid = list.front();
        int bestScore = 0;
        bool haveScore = false;
        for (int candidate : list) {
            int score = 0;
            for (int other : list) {
                score += riverProfileDelta(catalog.stamps[static_cast<std::size_t>(candidate)], catalog.stamps[static_cast<std::size_t>(other)]);
            }
            if (!haveScore || score < bestScore || (score == bestScore && candidate < medoid)) {
                bestScore = score;
                medoid = candidate;
                haveScore = true;
            }
        }
        std::vector<int> kept;
        const SectionStamp& center = catalog.stamps[static_cast<std::size_t>(medoid)];
        for (int candidate : list) {
            if (riverProfileDelta(catalog.stamps[static_cast<std::size_t>(candidate)], center) <= 12) {
                kept.push_back(candidate);
            }
        }
        if (kept.empty()) {
            kept.push_back(medoid);
        }
        list = std::move(kept);
    }
}

Catalog buildCatalog(const TilesetLibrary& library, const std::string& world, int seaLevel) {
    Catalog catalog;
    catalog.cliffPiece.fill(-1);
    catalog.coastPiece.fill(-1);
    const std::string wanted = tilesetFamilyKey(world);
    std::vector<MarchCandidate> cliffs;
    std::vector<MarchCandidate> coasts;
    for (const TilesetArchive& archive : library.archives()) {
        for (const TilesetSection& section : archive.sections) {
            if (tilesetFamilyKey(section.world) != wanted) {
                continue;
            }
            const HpiFile* file = archive.archive().find(section.archivePath);
            if (file == nullptr) {
                continue;
            }
            const int index = static_cast<int>(catalog.stamps.size());
            catalog.stamps.push_back(loadSectionStamp(archive.archive().extract(*file)));
            const SectionStamp& stamp = catalog.stamps.back();
            const int span = heightSpan(stamp);
            const int average = meanHeight(stamp);
            const bool square16 = stamp.tileWidth == kCell && stamp.tileHeight == kCell;
            if (contains(section.category, "river") && square16) {
                if (const std::optional<int> mask = bankedRiverMask(stamp)) {
                    catalog.rivers[static_cast<std::size_t>(*mask)].push_back(index);
                }
                continue;
            }
            if (contains(section.category, "coast") || contains(section.category, "shore")) {
                if (const std::optional<MarchCandidate> candidate = marchCandidate(stamp, index, true)) {
                    coasts.push_back(*candidate);
                } else if (openWaterTile(stamp, seaLevel)) {
                    catalog.water.push_back(index);
                }
                continue;
            }
            if (contains(section.category, "ravine")) {
                if (span < 8 && average > 180 && stamp.tileWidth <= kCell && stamp.tileHeight <= kCell && stamp.tileWidth > 0) {
                    catalog.plateau.push_back(index);
                }
                if (const std::optional<MarchCandidate> candidate = marchCandidate(stamp, index, false)) {
                    cliffs.push_back(*candidate);
                }
                continue;
            }
            if (contains(section.category, "pool")) {
                catalog.pools.push_back(index);
                continue;
            }
            if (contains(section.category, "mountain") || contains(section.name, "crater")) {
                catalog.mountains.push_back(index);
                continue;
            }
            if (contains(section.category, "hill") && stamp.tileWidth <= kCell && stamp.tileHeight <= kCell && average < 150
                && catalog.hills.size() < 40) {
                catalog.hills.push_back(index);
                continue;
            }
            if ((contains(section.category, "water") || contains(section.category, "deep sea") || contains(section.category, "flat"))
                && openWaterTile(stamp, seaLevel)) {
                catalog.water.push_back(index);
                continue;
            }
            if ((contains(section.category, "water") || contains(section.category, "deep sea")) && span < 8 && average < seaLevel - 10
                && stamp.tileWidth <= kCell && stamp.tileHeight <= kCell) {
                catalog.water.push_back(index);
                continue;
            }
            // A step of 20 is where the height view draws an edge. Textured flats
            // such as Acid FlatGr5 stay inside 80–90, so a span under 20 is still fill.
            if (contains(section.category, "flat") && !contains(section.name, "219") && !contains(section.name, "shalow")
                && !contains(section.name, "shallow") && span < 20 && average >= seaLevel && stamp.tileWidth <= 32 && stamp.tileHeight <= 32) {
                catalog.ground.push_back(index);
            }
        }
    }
    if (catalog.ground.empty()) {
        throw std::runtime_error(world + " has no flat ground section to generate from");
    }
    std::sort(catalog.ground.begin(), catalog.ground.end(), [&](int a, int b) {
        const SectionStamp& left = catalog.stamps[static_cast<std::size_t>(a)];
        const SectionStamp& right = catalog.stamps[static_cast<std::size_t>(b)];
        return left.tileWidth * left.tileHeight > right.tileWidth * right.tileHeight;
    });
    finishMarch(catalog, catalog.cliffPiece, catalog.plateauPiece, catalog.plateau, cliffs, false);
    finishMarch(catalog, catalog.coastPiece, catalog.waterPiece, catalog.water, coasts, true);
    catalog.cliffReady = catalog.plateauPiece >= 0 && catalog.cliffPiece[3] >= 0;
    catalog.coastReady = catalog.waterPiece >= 0 && catalog.coastPiece[3] >= 0;
    finishRivers(catalog);
    return catalog;
}

const SectionStamp& piece(const Catalog& catalog, int index) {
    return catalog.stamps[static_cast<std::size_t>(index)];
}

void paintStamp(LoadedMap& map, const SectionStamp& stamp, int tileX, int tileY, TileDeduper& deduper) {
    stampSection(map, stamp, tileX, tileY, &deduper);
}

void paintTiled(LoadedMap& map, const SectionStamp& stamp, TileDeduper& deduper) {
    for (int y = 0; y < map.tileHeight; y += stamp.tileHeight) {
        for (int x = 0; x < map.tileWidth; x += stamp.tileWidth) {
            paintStamp(map, stamp, x, y, deduper);
        }
    }
}

void coverCell(LoadedMap& map, const SectionStamp& stamp, int cellX, int cellY, TileDeduper& deduper) {
    if (stamp.tileWidth <= 0 || stamp.tileHeight <= 0 || stamp.tileWidth > kCell || stamp.tileHeight > kCell) {
        return;
    }
    const int originX = cellX * kCell;
    const int originY = cellY * kCell;
    for (int y = 0; y < kCell; y += stamp.tileHeight) {
        for (int x = 0; x < kCell; x += stamp.tileWidth) {
            paintStamp(map, stamp, originX + x, originY + y, deduper);
        }
    }
}

bool fits(const LoadedMap& map, const SectionStamp& stamp, int tileX, int tileY) {
    return tileX >= 0 && tileY >= 0 && tileX + stamp.tileWidth <= map.tileWidth && tileY + stamp.tileHeight <= map.tileHeight;
}

struct LandGrid {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> blocked;
    std::vector<std::uint8_t> river;
    std::vector<std::uint8_t> reserved;

    bool inside(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height;
    }

    std::size_t index(int x, int y) const {
        return static_cast<std::size_t>(y * width + x);
    }
};

bool footprintFree(const LandGrid& grid, int tileX, int tileY, int tileWidth, int tileHeight) {
    const int x0 = tileX / kCell;
    const int y0 = tileY / kCell;
    const int x1 = (tileX + tileWidth - 1) / kCell;
    const int y1 = (tileY + tileHeight - 1) / kCell;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            if (!grid.inside(x, y) || grid.blocked[grid.index(x, y)] != 0) {
                return false;
            }
        }
    }
    return true;
}

void scatter(
    LoadedMap& map,
    const Catalog& catalog,
    const std::vector<int>& indices,
    int count,
    std::mt19937& rng,
    TileDeduper& deduper,
    const LandGrid* grid) {
    if (indices.empty() || count <= 0) {
        return;
    }
    std::uniform_int_distribution<int> which(0, static_cast<int>(indices.size()) - 1);
    std::uniform_int_distribution<int> xPick(0, std::max(map.tileWidth - 1, 0));
    std::uniform_int_distribution<int> yPick(0, std::max(map.tileHeight - 1, 0));
    for (int placed = 0, attempt = 0; placed < count && attempt < count * 16; ++attempt) {
        const int index = indices[static_cast<std::size_t>(which(rng))];
        const int tileX = xPick(rng);
        const int tileY = yPick(rng);
        if (!fits(map, piece(catalog, index), tileX, tileY)) {
            continue;
        }
        const SectionStamp& stamp = piece(catalog, index);
        if (grid != nullptr && !footprintFree(*grid, tileX, tileY, stamp.tileWidth, stamp.tileHeight)) {
            continue;
        }
        paintStamp(map, stamp, tileX, tileY, deduper);
        ++placed;
    }
}

int cellMask(const std::vector<std::uint8_t>& corners, int cornerWidth, int x, int y) {
    const auto bit = [&](int cx, int cy) {
        return corners[static_cast<std::size_t>(cy * cornerWidth + cx)] != 0 ? 1 : 0;
    };
    return bit(x, y) | (bit(x + 1, y) << 1) | (bit(x + 1, y + 1) << 2) | (bit(x, y + 1) << 3);
}

void resolveSaddles(std::vector<std::uint8_t>& corners, int cellsW, int cellsH) {
    const int cornerWidth = cellsW + 1;
    for (int pass = 0; pass < 4; ++pass) {
        bool changed = false;
        for (int y = 0; y < cellsH; ++y) {
            for (int x = 0; x < cellsW; ++x) {
                const int mask = cellMask(corners, cornerWidth, x, y);
                if (mask == 5) {
                    corners[static_cast<std::size_t>((y + 1) * cornerWidth + (x + 1))] = 0;
                    changed = true;
                } else if (mask == 10) {
                    corners[static_cast<std::size_t>(y * cornerWidth + (x + 1))] = 0;
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
}

void stampCell(LoadedMap& map, const Catalog& catalog, int index, int cellX, int cellY, TileDeduper& deduper) {
    const SectionStamp& stamp = piece(catalog, index);
    if (stamp.tileWidth == kCell && stamp.tileHeight == kCell) {
        paintStamp(map, stamp, cellX * kCell, cellY * kCell, deduper);
        return;
    }
    coverCell(map, stamp, cellX, cellY, deduper);
}

void stampField(
    LoadedMap& map,
    const Catalog& catalog,
    LandGrid& grid,
    const std::vector<std::uint8_t>& corners,
    const std::array<int, 16>& pieces,
    int flatIndex,
    TileDeduper& deduper,
    std::vector<std::uint8_t>* reserved) {
    const int cornerWidth = grid.width + 1;
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const int mask = cellMask(corners, cornerWidth, x, y);
            if (mask == 0) {
                continue;
            }
            const int index = mask == 15 ? flatIndex : pieces[static_cast<std::size_t>(mask)];
            if (index < 0) {
                continue;
            }
            stampCell(map, catalog, index, x, y, deduper);
            grid.blocked[grid.index(x, y)] = 1;
            if (reserved != nullptr) {
                (*reserved)[static_cast<std::size_t>(y * cornerWidth + x)] = 1;
                (*reserved)[static_cast<std::size_t>(y * cornerWidth + x + 1)] = 1;
                (*reserved)[static_cast<std::size_t>((y + 1) * cornerWidth + x)] = 1;
                (*reserved)[static_cast<std::size_t>((y + 1) * cornerWidth + x + 1)] = 1;
            }
        }
    }
}

void fillBand(
    std::vector<std::uint8_t>& corners,
    int cornerWidth,
    int cornerHeight,
    bool horizontal,
    bool fromStart,
    int bandDepth,
    int maxDepth,
    std::mt19937& rng) {
    const int along = horizontal ? cornerWidth : cornerHeight;
    const int across = horizontal ? cornerHeight : cornerWidth;
    int depth = std::min(bandDepth, maxDepth);
    std::uniform_int_distribution<int> roll(0, 4);
    for (int i = 0; i < along; ++i) {
        if (i > 0 && (i % 2) == 0) {
            const int choice = roll(rng);
            if (choice == 0 && depth < maxDepth) {
                ++depth;
            } else if (choice == 1 && depth > 1) {
                --depth;
            }
        }
        for (int j = 0; j <= depth && j < across; ++j) {
            int x = 0;
            int y = 0;
            if (horizontal && fromStart) {
                x = i;
                y = j;
            } else if (horizontal) {
                x = i;
                y = across - 1 - j;
            } else if (fromStart) {
                x = j;
                y = i;
            } else {
                x = across - 1 - j;
                y = i;
            }
            corners[static_cast<std::size_t>(y * cornerWidth + x)] = 1;
        }
    }
}

void paintShore(LoadedMap& map, const Catalog& catalog, LandGrid& grid, int amount, std::mt19937& rng, TileDeduper& deduper) {
    if (amount <= 0 || !catalog.coastReady || grid.width < 2 || grid.height < 2) {
        return;
    }
    const int limit = std::max(1, std::min(grid.width, grid.height) / 3);
    int depth = std::max(1, (amount * limit) / 100);
    const int maxDepth = std::max(1, (std::min(grid.width, grid.height) - 1) / 2);
    depth = std::min(depth, maxDepth);
    std::uniform_int_distribution<int> sidePick(0, 3);
    const int side = sidePick(rng);
    const bool north = side == 0 || amount >= 80 || (amount >= 45 && side == 2);
    const bool east = side == 1 || amount >= 80 || (amount >= 45 && side == 3);
    const bool south = side == 2 || amount >= 80 || (amount >= 45 && side == 0);
    const bool west = side == 3 || amount >= 80 || (amount >= 45 && side == 1);
    const int cornerWidth = grid.width + 1;
    const int cornerHeight = grid.height + 1;
    std::vector<std::uint8_t> wet(static_cast<std::size_t>(cornerWidth * cornerHeight), 0);
    if (north) {
        fillBand(wet, cornerWidth, cornerHeight, true, true, depth, maxDepth, rng);
    }
    if (east) {
        fillBand(wet, cornerWidth, cornerHeight, false, false, depth, maxDepth, rng);
    }
    if (south) {
        fillBand(wet, cornerWidth, cornerHeight, true, false, depth, maxDepth, rng);
    }
    if (west) {
        fillBand(wet, cornerWidth, cornerHeight, false, true, depth, maxDepth, rng);
    }
    resolveSaddles(wet, grid.width, grid.height);
    grid.reserved.assign(wet.size(), 0);
    stampField(map, catalog, grid, wet, catalog.coastPiece, catalog.waterPiece, deduper, &grid.reserved);
}

void paintRectangle(std::vector<std::uint8_t>& high, const LandGrid& grid, int x0, int y0, int x1, int y1) {
    const int cornerWidth = grid.width + 1;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (x < 1 || y < 1 || x >= grid.width || y >= grid.height) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(y * cornerWidth + x);
            if (!grid.reserved.empty() && grid.reserved[index] != 0) {
                continue;
            }
            high[index] = 1;
        }
    }
}

void paintCanyons(LoadedMap& map, const Catalog& catalog, LandGrid& grid, int amount, std::mt19937& rng, TileDeduper& deduper) {
    if (amount <= 0 || !catalog.cliffReady || grid.width < 3 || grid.height < 3) {
        return;
    }
    const int cornerWidth = grid.width + 1;
    std::vector<std::uint8_t> high(static_cast<std::size_t>(cornerWidth * (grid.height + 1)), 0);
    const int paintableX = grid.width - 1;
    const int paintableY = grid.height - 1;
    int bands = std::max(1, (amount * std::max(grid.width, grid.height)) / 80);
    bands = std::min(bands, 8);
    int thickCap = amount >= 65 ? 3 : 2;
    if (std::min(grid.width, grid.height) < 6) {
        bands = 1;
        thickCap = 2;
    }
    std::uniform_int_distribution<int> coin(0, 1);
    for (int band = 0; band < bands; ++band) {
        const bool horizontal = coin(rng) == 0;
        int thick = thickCap;
        int along = 2 + amount / 20;
        if (horizontal) {
            thick = std::min(thick, paintableY);
            along = std::min(std::max(along, thick), paintableX);
            int originX = 1;
            int originY = 1;
            if (paintableX > along) {
                std::uniform_int_distribution<int> pick(0, paintableX - along);
                originX = 1 + pick(rng);
            }
            if (paintableY > thick) {
                std::uniform_int_distribution<int> pick(0, paintableY - thick);
                originY = 1 + pick(rng);
            }
            paintRectangle(high, grid, originX, originY, originX + along, originY + thick);
        } else {
            thick = std::min(thick, paintableX);
            along = std::min(std::max(along, thick), paintableY);
            int originX = 1;
            int originY = 1;
            if (paintableX > thick) {
                std::uniform_int_distribution<int> pick(0, paintableX - thick);
                originX = 1 + pick(rng);
            }
            if (paintableY > along) {
                std::uniform_int_distribution<int> pick(0, paintableY - along);
                originY = 1 + pick(rng);
            }
            paintRectangle(high, grid, originX, originY, originX + thick, originY + along);
        }
    }
    if (amount >= 25 && std::min(grid.width, grid.height) >= 6) {
        const int cuts = std::max(1, amount / 40);
        for (int cut = 0; cut < cuts; ++cut) {
            if (coin(rng) == 0) {
                std::uniform_int_distribution<int> pick(2, grid.height - 2);
                const int y = pick(rng);
                for (int x = 1; x < grid.width; ++x) {
                    const std::size_t above = static_cast<std::size_t>((y - 1) * cornerWidth + x);
                    const std::size_t below = static_cast<std::size_t>((y + 1) * cornerWidth + x);
                    if (high[above] != 0 && high[below] != 0) {
                        high[static_cast<std::size_t>(y * cornerWidth + x)] = 0;
                    }
                }
            } else {
                std::uniform_int_distribution<int> pick(2, grid.width - 2);
                const int x = pick(rng);
                for (int y = 1; y < grid.height; ++y) {
                    const std::size_t left = static_cast<std::size_t>(y * cornerWidth + (x - 1));
                    const std::size_t right = static_cast<std::size_t>(y * cornerWidth + (x + 1));
                    if (high[left] != 0 && high[right] != 0) {
                        high[static_cast<std::size_t>(y * cornerWidth + x)] = 0;
                    }
                }
            }
        }
    }
    resolveSaddles(high, grid.width, grid.height);
    stampField(map, catalog, grid, high, catalog.cliffPiece, catalog.plateauPiece, deduper, nullptr);
}

int riverLinks(const LandGrid& grid, int x, int y) {
    int links = 0;
    if (y > 0 && grid.river[grid.index(x, y - 1)] != 0) {
        ++links;
    }
    if (x + 1 < grid.width && grid.river[grid.index(x + 1, y)] != 0) {
        ++links;
    }
    if (y + 1 < grid.height && grid.river[grid.index(x, y + 1)] != 0) {
        ++links;
    }
    if (x > 0 && grid.river[grid.index(x - 1, y)] != 0) {
        ++links;
    }
    return links;
}

bool extendRiverEnd(LandGrid& grid, int startX, int startY) {
    struct Step {
        int x = 0;
        int y = 0;
        int parent = -1;
    };
    std::vector<Step> nodes;
    std::vector<std::uint8_t> seen(grid.river.size(), 0);
    nodes.push_back({startX, startY, -1});
    seen[grid.index(startX, startY)] = 1;
    const int limit = grid.width * grid.height;
    for (std::size_t head = 0; head < nodes.size() && static_cast<int>(nodes.size()) < limit; ++head) {
        const Step current = nodes[head];
        const int offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        for (const auto& offset : offsets) {
            const int nextX = current.x + offset[0];
            const int nextY = current.y + offset[1];
            if (!grid.inside(nextX, nextY)) {
                continue;
            }
            const std::size_t nextIndex = grid.index(nextX, nextY);
            if (grid.river[nextIndex] != 0) {
                if (current.x == startX && current.y == startY) {
                    continue;
                }
                for (int at = static_cast<int>(head); at > 0; at = nodes[static_cast<std::size_t>(at)].parent) {
                    grid.river[grid.index(nodes[static_cast<std::size_t>(at)].x, nodes[static_cast<std::size_t>(at)].y)] = 1;
                }
                return true;
            }
            if (grid.blocked[nextIndex] != 0 || seen[nextIndex] != 0) {
                continue;
            }
            seen[nextIndex] = 1;
            nodes.push_back({nextX, nextY, static_cast<int>(head)});
        }
    }
    return false;
}

void linkRiverEnds(LandGrid& grid) {
    const int cellCount = std::max(grid.width * grid.height, 1);
    for (int pass = 0; pass < cellCount; ++pass) {
        int startX = -1;
        int startY = -1;
        for (int y = 0; y < grid.height && startX < 0; ++y) {
            for (int x = 0; x < grid.width; ++x) {
                if (grid.river[grid.index(x, y)] != 0 && riverLinks(grid, x, y) == 1) {
                    startX = x;
                    startY = y;
                    break;
                }
            }
        }
        if (startX < 0) {
            return;
        }
        if (!extendRiverEnd(grid, startX, startY)) {
            grid.river[grid.index(startX, startY)] = 0;
        }
    }
}

void carveRivers(LandGrid& grid, int amount, std::mt19937& rng) {
    if (amount <= 0 || grid.width < 1 || grid.height < 1) {
        return;
    }
    int target = std::max(2, (amount * grid.width * grid.height) / 220);
    target = std::min(target, std::max(1, (grid.width * grid.height * 2) / 3));
    std::uniform_int_distribution<int> xPick(0, grid.width - 1);
    std::uniform_int_distribution<int> yPick(0, grid.height - 1);
    std::uniform_int_distribution<int> dirPick(0, 3);
    std::uniform_int_distribution<int> percent(0, 99);
    int filled = 0;
    for (int attempt = 0; filled < target && attempt < target * 30; ++attempt) {
        int x = xPick(rng);
        int y = yPick(rng);
        Dir dir = static_cast<Dir>(dirPick(rng));
        if (grid.blocked[grid.index(x, y)] != 0) {
            continue;
        }
        const int length = 2 + static_cast<int>(dirPick(rng)) + std::max(grid.width, grid.height) / 3;
        for (int step = 0; step < length && filled < target; ++step) {
            if (!grid.inside(x, y) || grid.blocked[grid.index(x, y)] != 0) {
                dir = percent(rng) < 50 ? leftOf(dir) : rightOf(dir);
                const Offset turn = stepOf(dir);
                x += turn.x;
                y += turn.y;
                continue;
            }
            if (grid.river[grid.index(x, y)] == 0) {
                grid.river[grid.index(x, y)] = 1;
                ++filled;
            }
            if (percent(rng) < 28) {
                dir = percent(rng) < 50 ? leftOf(dir) : rightOf(dir);
            }
            const Offset ahead = stepOf(dir);
            x += ahead.x;
            y += ahead.y;
        }
    }
    linkRiverEnds(grid);
}

int neighborMask(const LandGrid& grid, int x, int y, const std::vector<std::uint8_t>& field) {
    int mask = 0;
    if (y > 0 && field[grid.index(x, y - 1)] != 0) {
        mask |= 1;
    }
    if (x + 1 < grid.width && field[grid.index(x + 1, y)] != 0) {
        mask |= 2;
    }
    if (y + 1 < grid.height && field[grid.index(x, y + 1)] != 0) {
        mask |= 4;
    }
    if (x > 0 && field[grid.index(x - 1, y)] != 0) {
        mask |= 8;
    }
    return mask;
}

void stampIndexed(LoadedMap& map, const Catalog& catalog, const std::vector<int>& indices, int cellX, int cellY, std::mt19937& rng, TileDeduper& deduper) {
    if (indices.empty()) {
        return;
    }
    std::uniform_int_distribution<int> pick(0, static_cast<int>(indices.size()) - 1);
    const SectionStamp& stamp = piece(catalog, indices[static_cast<std::size_t>(pick(rng))]);
    if (stamp.tileWidth == kCell && stamp.tileHeight == kCell) {
        paintStamp(map, stamp, cellX * kCell, cellY * kCell, deduper);
        return;
    }
    coverCell(map, stamp, cellX, cellY, deduper);
}

void clearFeatures(LoadedMap& map) {
    const std::size_t cells = map.heights.size();
    map.featureIndices.assign(cells, 0xFFFF);
    map.featurePadding.assign(cells, 0);
    map.features.clear();
}

}  // namespace

void generateTerrain(LoadedMap& map, const TilesetLibrary& library, const MapGeneratorSettings& settings) {
    if (map.tileWidth <= 0 || map.tileHeight <= 0) {
        throw std::runtime_error("Open a map before generating terrain");
    }
    if (settings.world.empty()) {
        throw std::runtime_error("Choose a world to generate from");
    }
    const Catalog catalog = buildCatalog(library, settings.world, map.seaLevel);
    const int rivers = clampPercent(settings.rivers);
    const int canyons = clampPercent(settings.canyons);
    const int mountains = clampPercent(settings.mountains);
    const int hills = clampPercent(settings.hills);
    const int shoreline = clampPercent(settings.shoreline);
    const int pools = clampPercent(settings.pools);

    clearFeatures(map);
    TileDeduper deduper;
    paintTiled(map, piece(catalog, catalog.ground.front()), deduper);

    std::mt19937 rng(settings.seed);
    LandGrid grid;
    grid.width = map.tileWidth / kCell;
    grid.height = map.tileHeight / kCell;
    const bool gridded = grid.width > 0 && grid.height > 0;
    if (gridded) {
        grid.blocked.assign(static_cast<std::size_t>(grid.width * grid.height), 0);
        grid.river.assign(grid.blocked.size(), 0);
        paintShore(map, catalog, grid, shoreline, rng, deduper);
        paintCanyons(map, catalog, grid, canyons, rng, deduper);
        carveRivers(grid, rivers, rng);
        for (int y = 0; y < grid.height; ++y) {
            for (int x = 0; x < grid.width; ++x) {
                if (grid.river[grid.index(x, y)] == 0) {
                    continue;
                }
                const int mask = neighborMask(grid, x, y, grid.river);
                if (mask == 0 || catalog.rivers[static_cast<std::size_t>(mask)].empty()) {
                    continue;
                }
                stampIndexed(map, catalog, catalog.rivers[static_cast<std::size_t>(mask)], x, y, rng, deduper);
                grid.blocked[grid.index(x, y)] = 1;
            }
        }
    }

    const int area = map.tileWidth * map.tileHeight;
    int hillCount = (hills * area) / (100 * 16 * 16);
    if (hills > 0 && hillCount < 1) {
        hillCount = 1;
    }
    const LandGrid* mask = gridded ? &grid : nullptr;
    scatter(map, catalog, catalog.hills, hillCount, rng, deduper, mask);

    int mountainCount = (mountains * area) / (100 * 28 * 28);
    if (mountains >= 15 && mountainCount < 1) {
        mountainCount = 1;
    }
    mountainCount = std::min(mountainCount, std::max(1, area / (24 * 24)));
    if (mountains == 0) {
        mountainCount = 0;
    }
    scatter(map, catalog, catalog.mountains, mountainCount, rng, deduper, mask);

    int poolCount = (pools * std::max(1, (map.tileWidth / kCell) * (map.tileHeight / kCell))) / 180;
    if (pools > 0 && poolCount < 1) {
        poolCount = 1;
    }
    scatter(map, catalog, catalog.pools, poolCount, rng, deduper, mask);
}

}  // namespace galaxy

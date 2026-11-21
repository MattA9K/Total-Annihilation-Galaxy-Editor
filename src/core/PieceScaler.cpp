#include "core/PieceScaler.hpp"

#include "format/TaPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace galaxy {

namespace {

using Colour = std::array<double, 3>;

Colour colourOf(std::uint8_t index) {
    return {static_cast<double>(kTaPalette[index][0]), static_cast<double>(kTaPalette[index][1]), static_cast<double>(kTaPalette[index][2])};
}

double distance(const Colour& left, const Colour& right) {
    const double r = left[0] - right[0];
    const double g = left[1] - right[1];
    const double b = left[2] - right[2];
    return std::sqrt(r * r + g * g + b * b);
}

// The average colour of the (2 * radius + 1) square around each pixel, cut short at the edges.
std::vector<Colour> patchMeans(const PaletteImage& image, int radius) {
    const int w = image.width;
    const int h = image.height;
    // Summed-area table, one row and column larger than the picture.
    std::vector<Colour> sums(static_cast<std::size_t>(w + 1) * static_cast<std::size_t>(h + 1), Colour{0.0, 0.0, 0.0});
    const auto sumAt = [&](int x, int y) -> Colour& {
        return sums[static_cast<std::size_t>(y) * static_cast<std::size_t>(w + 1) + static_cast<std::size_t>(x)];
    };
    for (int y = 0; y < h; ++y) {
        Colour row = {0.0, 0.0, 0.0};
        for (int x = 0; x < w; ++x) {
            const Colour c = colourOf(image.at(x, y));
            for (int k = 0; k < 3; ++k) {
                row[static_cast<std::size_t>(k)] += c[static_cast<std::size_t>(k)];
                sumAt(x + 1, y + 1)[static_cast<std::size_t>(k)] = sumAt(x + 1, y)[static_cast<std::size_t>(k)] + row[static_cast<std::size_t>(k)];
            }
        }
    }
    std::vector<Colour> means(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - radius);
        const int y1 = std::min(h, y + radius + 1);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - radius);
            const int x1 = std::min(w, x + radius + 1);
            const double area = static_cast<double>((x1 - x0) * (y1 - y0));
            Colour mean;
            for (int k = 0; k < 3; ++k) {
                const std::size_t c = static_cast<std::size_t>(k);
                mean[c] = (sumAt(x1, y1)[c] - sumAt(x0, y1)[c] - sumAt(x1, y0)[c] + sumAt(x0, y0)[c]) / area;
            }
            means[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = mean;
        }
    }
    return means;
}

// A box blur of radius r, cut short at the edges.
std::vector<float> blurred(const std::vector<float>& values, int w, int h, int r) {
    std::vector<float> across(values.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            int count = 0;
            for (int dx = -r; dx <= r; ++dx) {
                const int sx = x + dx;
                if (sx >= 0 && sx < w) {
                    sum += values[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(sx)];
                    ++count;
                }
            }
            across[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = static_cast<float>(sum / count);
        }
    }
    std::vector<float> out(values.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            int count = 0;
            for (int dy = -r; dy <= r; ++dy) {
                const int sy = y + dy;
                if (sy >= 0 && sy < h) {
                    sum += across[static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
                    ++count;
                }
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = static_cast<float>(sum / count);
        }
    }
    return out;
}

// A repeatable number from 0 to 1 for each pixel, for dithering the fade.
double noiseAt(int x, int y, std::uint32_t seed) {
    std::uint32_t h = seed * 0x9E3779B1u ^ static_cast<std::uint32_t>(x) * 0x85EBCA77u ^ static_cast<std::uint32_t>(y) * 0xC2B2AE3Du;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return static_cast<double>(h & 0xFFFFFFu) / static_cast<double>(0x1000000u);
}

// A pixel this many spreads from a land's average colour still counts as its flat ground, and
// one past kPlainFar does not count at all.
constexpr double kPlainNear = 2.5;
constexpr double kPlainFar = 4.5;
// The fade from a land's own tiles into the halved band, in halved pixels.
constexpr int kFadeRadius = 3;

// A section of the picture's tiles, 32 pixels to a tile, with the given heights. Identical tiles
// share one picture, as the game's own sections do.
SectionStamp stampFromImage(const PaletteImage& out, std::vector<std::uint8_t> heights) {
    SectionStamp result;
    result.version = 3;
    result.tileWidth = out.width / 32;
    result.tileHeight = out.height / 32;
    result.indices.resize(static_cast<std::size_t>(result.tileWidth) * static_cast<std::size_t>(result.tileHeight));
    std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> seen;
    std::array<std::uint8_t, 1024> tile{};
    for (int ty = 0; ty < result.tileHeight; ++ty) {
        for (int tx = 0; tx < result.tileWidth; ++tx) {
            for (int row = 0; row < 32; ++row) {
                std::memcpy(tile.data() + row * 32, out.pixels.data() + static_cast<std::size_t>(ty * 32 + row) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(tx * 32), 32);
            }
            const std::uint64_t hash = brushTileHash(tile.data());
            std::uint32_t found = static_cast<std::uint32_t>(result.tileCount);
            for (const std::uint32_t candidate : seen[hash]) {
                if (std::memcmp(result.tileImages.data() + static_cast<std::size_t>(candidate) * 1024, tile.data(), 1024) == 0) {
                    found = candidate;
                    break;
                }
            }
            if (found == static_cast<std::uint32_t>(result.tileCount)) {
                result.tileImages.insert(result.tileImages.end(), tile.begin(), tile.end());
                seen[hash].push_back(found);
                ++result.tileCount;
            }
            result.indices[static_cast<std::size_t>(ty) * static_cast<std::size_t>(result.tileWidth) + static_cast<std::size_t>(tx)] = found;
        }
    }
    result.heights = std::move(heights);
    return result;
}

}  // namespace

PaletteImage sectionImage(const SectionStamp& stamp) {
    PaletteImage image;
    image.width = std::max(0, stamp.tileWidth) * 32;
    image.height = std::max(0, stamp.tileHeight) * 32;
    image.pixels.assign(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height), 0);
    for (int ty = 0; ty < stamp.tileHeight; ++ty) {
        for (int tx = 0; tx < stamp.tileWidth; ++tx) {
            const std::size_t cell = static_cast<std::size_t>(ty) * static_cast<std::size_t>(stamp.tileWidth) + static_cast<std::size_t>(tx);
            if (cell >= stamp.indices.size()) {
                continue;
            }
            const std::size_t bytes = static_cast<std::size_t>(stamp.indices[cell]) * 1024;
            if (bytes + 1024 > stamp.tileImages.size()) {
                continue;
            }
            for (int row = 0; row < 32; ++row) {
                std::memcpy(image.pixels.data() + static_cast<std::size_t>(ty * 32 + row) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(tx * 32),
                    stamp.tileImages.data() + bytes + static_cast<std::size_t>(row) * 32, 32);
            }
        }
    }
    return image;
}

PaletteImage halveImage(const PaletteImage& image) {
    PaletteImage half;
    half.width = image.width / 2;
    half.height = image.height / 2;
    half.pixels.resize(static_cast<std::size_t>(half.width) * static_cast<std::size_t>(half.height));
    for (int y = 0; y < half.height; ++y) {
        for (int x = 0; x < half.width; ++x) {
            const std::array<std::uint8_t, 4> block = {image.at(2 * x, 2 * y), image.at(2 * x + 1, 2 * y), image.at(2 * x, 2 * y + 1), image.at(2 * x + 1, 2 * y + 1)};
            Colour mean = {0.0, 0.0, 0.0};
            for (const std::uint8_t index : block) {
                const Colour c = colourOf(index);
                for (std::size_t k = 0; k < 3; ++k) {
                    mean[k] += c[k] / 4.0;
                }
            }
            std::uint8_t best = block[0];
            double bestDistance = 1.0e30;
            for (const std::uint8_t index : block) {
                const double d = distance(colourOf(index), mean);
                if (d < bestDistance) {
                    bestDistance = d;
                    best = index;
                }
            }
            half.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(half.width) + static_cast<std::size_t>(x)] = best;
        }
    }
    return half;
}

PieceLand makePieceLand(const PaletteImage& ground, int height) {
    PieceLand land;
    land.ground = ground;
    land.height = height;
    const PaletteImage half = halveImage(ground);
    if (half.width <= 0 || half.height <= 0) {
        return land;
    }
    const std::vector<Colour> means = patchMeans(half, 2);
    Colour mean = {0.0, 0.0, 0.0};
    for (const Colour& m : means) {
        for (std::size_t k = 0; k < 3; ++k) {
            mean[k] += m[k];
        }
    }
    for (std::size_t k = 0; k < 3; ++k) {
        mean[k] /= static_cast<double>(means.size());
    }
    double square = 0.0;
    for (const Colour& m : means) {
        const double d = distance(m, mean);
        square += d * d;
    }
    land.mean = mean;
    land.spread = std::max(3.0, std::sqrt(square / static_cast<double>(means.size())));
    return land;
}

std::array<int, 4> pieceCornerLands(const SectionStamp& piece, const std::vector<PieceLand>& lands, const std::vector<int>& candidates) {
    std::array<int, 4> corners = {-1, -1, -1, -1};
    const PaletteImage image = sectionImage(piece);
    if (image.width < 64 || image.height < 64) {
        return corners;
    }
    // Two tiles square at each corner: well clear of a transition, which runs between the middles of the sides.
    const int patch = 64;
    const int heightWidth = piece.tileWidth * 2;
    const int heightHeight = piece.tileHeight * 2;
    for (int corner = 0; corner < 4; ++corner) {
        const int x0 = (corner & 1) != 0 ? image.width - patch : 0;
        const int y0 = (corner & 2) != 0 ? image.height - patch : 0;
        Colour mean = {0.0, 0.0, 0.0};
        for (int y = y0; y < y0 + patch; ++y) {
            for (int x = x0; x < x0 + patch; ++x) {
                const Colour c = colourOf(image.at(x, y));
                for (std::size_t k = 0; k < 3; ++k) {
                    mean[k] += c[k];
                }
            }
        }
        for (std::size_t k = 0; k < 3; ++k) {
            mean[k] /= static_cast<double>(patch * patch);
        }
        double height = 0.0;
        int samples = 0;
        const int hx0 = (corner & 1) != 0 ? heightWidth - 4 : 0;
        const int hy0 = (corner & 2) != 0 ? heightHeight - 4 : 0;
        for (int y = hy0; y < hy0 + 4 && y < heightHeight; ++y) {
            for (int x = hx0; x < hx0 + 4 && x < heightWidth; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(heightWidth) + static_cast<std::size_t>(x);
                if (index < piece.heights.size()) {
                    height += piece.heights[index];
                    ++samples;
                }
            }
        }
        height = samples > 0 ? height / samples : 0.0;
        double bestCost = 1.0e30;
        for (const int candidate : candidates) {
            if (candidate < 0 || static_cast<std::size_t>(candidate) >= lands.size()) {
                continue;
            }
            const double rise = std::abs(height - lands[static_cast<std::size_t>(candidate)].height);
            if (rise > 24.0) {
                continue;
            }
            const double cost = distance(mean, lands[static_cast<std::size_t>(candidate)].mean) + 3.0 * rise;
            if (cost < bestCost) {
                bestCost = cost;
                corners[static_cast<std::size_t>(corner)] = candidate;
            }
        }
    }
    return corners;
}

PieceGround pieceGround(const PaletteImage& image, const std::vector<PieceLand>& lands, const std::vector<int>& pieceLands) {
    PieceGround ground;
    const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    ground.plain.assign(count, 0.0f);
    ground.land.assign(count, -1);
    if (pieceLands.empty() || count == 0) {
        return ground;
    }
    const std::vector<Colour> means = patchMeans(image, 2);
    std::vector<float> raw(count, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        double bestScore = 1.0e30;
        for (const int index : pieceLands) {
            const PieceLand& land = lands[static_cast<std::size_t>(index)];
            const double score = distance(means[i], land.mean) / land.spread;
            if (score < bestScore) {
                bestScore = score;
                ground.land[i] = index;
            }
        }
        raw[i] = static_cast<float>(std::clamp((kPlainFar - bestScore) / (kPlainFar - kPlainNear), 0.0, 1.0));
    }
    // A pixel counts as flat ground once its neighbourhood mostly does, so a lone pixel that
    // matches by chance stays in the band and the land's own tiles fade in over a few pixels.
    const std::vector<float> soft = blurred(raw, image.width, image.height, kFadeRadius);
    for (std::size_t i = 0; i < count; ++i) {
        ground.plain[i] = static_cast<float>(std::clamp((soft[i] - 0.3) / 0.55, 0.0, 1.0));
    }
    return ground;
}

SectionStamp halvePiece(const SectionStamp& piece, const std::vector<PieceLand>& lands, const std::vector<int>& joined, std::uint32_t seed) {
    const PaletteImage full = sectionImage(piece);
    const PaletteImage half = halveImage(full);
    std::vector<int> pieceLands;
    for (const int index : joined) {
        if (index >= 0 && static_cast<std::size_t>(index) < lands.size()
            && std::find(pieceLands.begin(), pieceLands.end(), index) == pieceLands.end()
            && lands[static_cast<std::size_t>(index)].ground.width >= 32 && lands[static_cast<std::size_t>(index)].ground.height >= 32) {
            pieceLands.push_back(index);
        }
    }
    const PieceGround ground = pieceGround(half, lands, pieceLands);

    // Flat ground is each land's texture from its top-left corner. anchorPieceGround moves it into
    // line with the map's grid where the piece is placed.
    PaletteImage out = half;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x);
            const int index = ground.land[i];
            if (index < 0 || ground.plain[i] <= 0.0f) {
                continue;
            }
            if (ground.plain[i] >= 1.0f || noiseAt(x, y, seed) < ground.plain[i]) {
                const PieceLand& land = lands[static_cast<std::size_t>(index)];
                out.pixels[i] = land.ground.at(x % land.ground.width, y % land.ground.height);
            }
        }
    }

    // Two heights a tile: each new height is the average of the 2 x 2 heights it covers.
    const int heightWidth = piece.tileWidth * 2;
    const int halfWidth = out.width / 32 * 2;
    const int halfHeight = out.height / 32 * 2;
    std::vector<std::uint8_t> heights(static_cast<std::size_t>(halfWidth) * static_cast<std::size_t>(halfHeight), 0);
    for (int y = 0; y < halfHeight; ++y) {
        for (int x = 0; x < halfWidth; ++x) {
            int sum = 0;
            int samples = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const std::size_t index = static_cast<std::size_t>(2 * y + dy) * static_cast<std::size_t>(heightWidth) + static_cast<std::size_t>(2 * x + dx);
                    if (index < piece.heights.size()) {
                        sum += piece.heights[index];
                        ++samples;
                    }
                }
            }
            heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(halfWidth) + static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(samples > 0 ? (sum + samples / 2) / samples : 0);
        }
    }
    return stampFromImage(out, std::move(heights));
}

SectionStamp anchorPieceGround(const SectionStamp& piece, const std::vector<const PaletteImage*>& grounds, int tileX, int tileY) {
    struct Shift {
        const PaletteImage* ground;
        int dx;
        int dy;
    };
    std::vector<Shift> shifts;
    for (const PaletteImage* ground : grounds) {
        if (ground == nullptr || ground->width < 32 || ground->height < 32) {
            continue;
        }
        const int dx = ((tileX * 32) % ground->width + ground->width) % ground->width;
        const int dy = ((tileY * 32) % ground->height + ground->height) % ground->height;
        if (dx != 0 || dy != 0) {
            shifts.push_back({ground, dx, dy});
        }
    }
    if (shifts.empty()) {
        return piece;
    }
    const PaletteImage image = sectionImage(piece);
    PaletteImage out = image;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            for (const Shift& shift : shifts) {
                const PaletteImage& ground = *shift.ground;
                bool flat = true;
                for (int v = -1; v <= 1 && flat; ++v) {
                    for (int u = -1; u <= 1 && flat; ++u) {
                        const int sx = x + u;
                        const int sy = y + v;
                        if (sx < 0 || sy < 0 || sx >= image.width || sy >= image.height) {
                            continue;
                        }
                        flat = image.at(sx, sy) == ground.at(sx % ground.width, sy % ground.height);
                    }
                }
                if (flat) {
                    out.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x)] =
                        ground.at((x + shift.dx) % ground.width, (y + shift.dy) % ground.height);
                    break;
                }
            }
        }
    }
    return stampFromImage(out, piece.heights);
}


std::vector<std::uint8_t> encodeTileHashes(std::vector<std::uint64_t> hashes) {
    std::sort(hashes.begin(), hashes.end());
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
    std::vector<std::uint8_t> bytes;
    bytes.reserve(hashes.size() * 8);
    for (const std::uint64_t hash : hashes) {
        for (int shift = 0; shift < 64; shift += 8) {
            bytes.push_back(static_cast<std::uint8_t>((hash >> shift) & 0xFF));
        }
    }
    return bytes;
}

std::vector<std::uint64_t> decodeTileHashes(const std::vector<std::uint8_t>& bytes) {
    std::vector<std::uint64_t> hashes;
    hashes.reserve(bytes.size() / 8);
    for (std::size_t at = 0; at + 8 <= bytes.size(); at += 8) {
        std::uint64_t hash = 0;
        for (int k = 7; k >= 0; --k) {
            hash = (hash << 8) | bytes[at + static_cast<std::size_t>(k)];
        }
        hashes.push_back(hash);
    }
    // Kept sorted for a binary search, even if the file was not.
    if (!std::is_sorted(hashes.begin(), hashes.end())) {
        std::sort(hashes.begin(), hashes.end());
    }
    return hashes;
}

std::vector<std::string> blendPieceWorlds(std::string_view name) {
    std::string lowered(name);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (!lowered.starts_with("blend-")) {
        return {};
    }
    const bool threeWay = lowered.starts_with("blend-tri-");
    std::string rest = lowered.substr(threeWay ? 10 : 6);
    // The land words, longest first, so "archi" is read before any shorter word inside it.
    struct Word {
        const char* word;
        const char* world;
    };
    static constexpr Word kWords[] = {
        {"crystal", "Crystal"}, {"desert", "Desert"}, {"archi", "Archipelago"}, {"green", "GreenWorld"}, {"grass", "GreenWorld"},
        {"metal", "Metal"}, {"acid", "Acid"}, {"lava", "Lava"}, {"lush", "Lush"}, {"mars", "Mars"}, {"ice", "Ice"},
    };
    std::vector<std::string> worlds;
    std::size_t at = 0;
    while (at < rest.size()) {
        bool matched = false;
        for (const Word& word : kWords) {
            const std::string_view text(word.word);
            if (rest.compare(at, text.size(), text) == 0) {
                if (std::find(worlds.begin(), worlds.end(), word.world) == worlds.end()) {
                    worlds.emplace_back(word.world);
                }
                at += text.size();
                matched = true;
                break;
            }
        }
        if (!matched) {
            ++at;
        }
    }
    if (worlds.empty()) {
        return {};
    }
    if (threeWay && worlds.size() < 3 && std::find(worlds.begin(), worlds.end(), "Lava") == worlds.end()) {
        worlds.emplace_back("Lava");
    }
    if (!threeWay && worlds.size() == 1) {
        worlds.emplace_back(worlds.front() == "Archipelago" ? "GreenWorld" : "Lava");
    }
    return worlds;
}

}  // namespace galaxy

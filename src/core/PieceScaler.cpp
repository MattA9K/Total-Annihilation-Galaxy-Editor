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

// A 16 x 16 piece's heights for its 8 x 8 version, two a tile: each is the average of the 2 x 2 heights it covers.
std::vector<std::uint8_t> halvedHeights(const SectionStamp& piece) {
    const int heightWidth = piece.tileWidth * 2;
    const int halfWidth = piece.tileWidth / 2 * 2;
    const int halfHeight = piece.tileHeight / 2 * 2;
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
    return heights;
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

PieceGround pieceGround(const PaletteImage& image, const std::vector<PieceLand>& lands, const std::vector<int>& pieceLands, bool byColour) {
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
        double nearest = 1.0e30;
        int nearestLand = -1;
        double nearestScore = 1.0e30;
        for (const int index : pieceLands) {
            const PieceLand& land = lands[static_cast<std::size_t>(index)];
            const double away = distance(means[i], land.mean);
            const double score = away / land.spread;
            if (score < bestScore) {
                bestScore = score;
                ground.land[i] = index;
            }
            if (away < nearest) {
                nearest = away;
                nearestLand = index;
                nearestScore = score;
            }
        }
        if (byColour && nearestLand != ground.land[i] && nearestScore < kPlainFar) {
            bestScore = nearestScore;
            ground.land[i] = nearestLand;
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

SectionStamp halvePiece(const SectionStamp& piece, const std::vector<PieceLand>& lands, const std::vector<int>& joined, std::uint32_t seed, bool byColour) {
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
    const PieceGround ground = pieceGround(half, lands, pieceLands, byColour);

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

    return stampFromImage(out, halvedHeights(piece));
}

namespace {

// Full-detail coasts. A pixel of a coast piece is flat ground, as the index of its ground, or part of a band.
constexpr std::uint8_t kBandKind = 255;

// Patches are 48 pixels square and overlap the patches beside them by 16, the depth of a side strip.
constexpr int kPatch = 48;
constexpr int kOverlap = 16;
constexpr int kStrip = 16;
// A side's outermost pixels are always its strip's, so the pieces either side of it meet the same picture.
constexpr int kStripHard = 3;
// The band's width, as a share of the 16 x 16 band's.
constexpr float kBandScale = 0.65f;
// How much of the band's water side is matched from the water's edge rather than the land's.
constexpr float kWaterEdge = 40.0f;
// How far a patch may come from the place in the 16 x 16 piece that its own place halves.
constexpr int kSearch = 170;
// Flat ground is found by 6 x 6 windows of its base's texture.
constexpr int kWindow = 6;
// How far into a piece the shape of a side's strip fades into the piece's own.
constexpr int kSideBlend = 64;
// Flat ground in fewer pixels than this, away from the piece's edges, lies within a band.
constexpr int kSmallFlat = 2000;
// How far a band ends from a side that is all its lower ground.
constexpr int kSideRoom = 16;

std::uint64_t windowHash(const PaletteImage& image, int x, int y) {
    std::uint64_t hash = 1469598103934665603ull;
    for (int v = 0; v < kWindow; ++v) {
        for (int u = 0; u < kWindow; ++u) {
            hash = (hash ^ image.at((x + u) % image.width, (y + v) % image.height)) * 1099511628211ull;
        }
    }
    return hash;
}

// Every 6 x 6 window of a base's texture, wrapping round its edges, sorted.
std::vector<std::uint64_t> textureWindows(const PaletteImage& ground) {
    std::vector<std::uint64_t> windows;
    if (ground.width < kWindow || ground.height < kWindow) {
        return windows;
    }
    windows.reserve(static_cast<std::size_t>(ground.width) * static_cast<std::size_t>(ground.height));
    for (int y = 0; y < ground.height; ++y) {
        for (int x = 0; x < ground.width; ++x) {
            windows.push_back(windowHash(ground, x, y));
        }
    }
    std::sort(windows.begin(), windows.end());
    windows.erase(std::unique(windows.begin(), windows.end()), windows.end());
    return windows;
}

// The pixels of a picture that are a base's texture, laid at any alignment: covered by a window found in it, in a
// neighbourhood that is largely so, so a lone window that matches by chance does not count.
std::vector<std::uint8_t> groundPixels(const PaletteImage& image, const std::vector<std::uint64_t>& windows) {
    const int w = image.width;
    const int h = image.height;
    std::vector<float> covered(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
    for (int y = 0; y + kWindow <= h; ++y) {
        for (int x = 0; x + kWindow <= w; ++x) {
            if (!std::binary_search(windows.begin(), windows.end(), windowHash(image, x, y))) {
                continue;
            }
            for (int v = 0; v < kWindow; ++v) {
                for (int u = 0; u < kWindow; ++u) {
                    covered[static_cast<std::size_t>(y + v) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x + u)] = 1.0f;
                }
            }
        }
    }
    const std::vector<float> soft = blurred(covered, w, h, 3);
    std::vector<std::uint8_t> ground(soft.size());
    for (std::size_t i = 0; i < soft.size(); ++i) {
        ground[i] = covered[i] > 0.0f && soft[i] >= 0.3f ? 1 : 0;
    }
    return ground;
}

// Felzenszwalb and Huttenlocher's squared distance transform along one line.
void squaredDistances(const std::vector<double>& f, int n, std::vector<double>& d, std::vector<int>& v, std::vector<double>& z) {
    const auto meet = [&](int q, int r) {
        return ((f[static_cast<std::size_t>(q)] + static_cast<double>(q) * q) - (f[static_cast<std::size_t>(r)] + static_cast<double>(r) * r)) / (2.0 * q - 2.0 * r);
    };
    int k = 0;
    v[0] = 0;
    z[0] = -1.0e30;
    z[1] = 1.0e30;
    for (int q = 1; q < n; ++q) {
        double s = meet(q, v[static_cast<std::size_t>(k)]);
        while (s <= z[static_cast<std::size_t>(k)]) {
            --k;
            s = meet(q, v[static_cast<std::size_t>(k)]);
        }
        ++k;
        v[static_cast<std::size_t>(k)] = q;
        z[static_cast<std::size_t>(k)] = s;
        z[static_cast<std::size_t>(k) + 1] = 1.0e30;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<std::size_t>(k) + 1] < q) {
            ++k;
        }
        const double gap = q - v[static_cast<std::size_t>(k)];
        d[static_cast<std::size_t>(q)] = gap * gap + f[static_cast<std::size_t>(v[static_cast<std::size_t>(k)])];
    }
}

// How far each pixel is from the nearest pixel of a mask, in pixels.
std::vector<float> distanceTo(const std::vector<std::uint8_t>& mask, int w, int h) {
    const std::size_t width = static_cast<std::size_t>(w);
    const std::size_t n = static_cast<std::size_t>(std::max(w, h));
    std::vector<double> squares(mask.size());
    std::vector<double> f(n);
    std::vector<double> d(n);
    std::vector<double> z(n + 1);
    std::vector<int> v(n);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            f[static_cast<std::size_t>(y)] = mask[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] != 0 ? 0.0 : 1.0e20;
        }
        squaredDistances(f, h, d, v, z);
        for (int y = 0; y < h; ++y) {
            squares[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = d[static_cast<std::size_t>(y)];
        }
    }
    std::vector<float> out(mask.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            f[static_cast<std::size_t>(x)] = squares[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
        }
        squaredDistances(f, w, d, v, z);
        for (int x = 0; x < w; ++x) {
            out[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = static_cast<float>(std::sqrt(std::min(d[static_cast<std::size_t>(x)], 1.0e12)));
        }
    }
    return out;
}

// The direction a distance field grows in at each pixel, as a unit vector, or zero where it is flat.
void directions(const std::vector<float>& field, int w, int h, int smoothing, std::vector<float>& nx, std::vector<float>& ny) {
    const std::vector<float> smooth = blurred(field, w, h, smoothing);
    nx.assign(field.size(), 0.0f);
    ny.assign(field.size(), 0.0f);
    const std::size_t width = static_cast<std::size_t>(w);
    for (int y = 1; y + 1 < h; ++y) {
        for (int x = 1; x + 1 < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
            const float gx = smooth[i + 1] - smooth[i - 1];
            const float gy = smooth[i + width] - smooth[i - width];
            const float length = std::sqrt(gx * gx + gy * gy);
            if (length > 0.2f) {
                nx[i] = gx / length;
                ny[i] = gy / length;
            }
        }
    }
}

// A 16 x 16 coast piece's band. The piece joins two or three grounds, highest first, such as land, shallows and
// water; each pixel is flat ground of one of them or part of a band between two. Flat ground that is small and away
// from the piece's edges lies within a band, as do rocks: a patch of the band that touches only one ground's side is
// part of that side.
struct SourceBand {
    int width = 0;
    int height = 0;
    int grounds = 0;
    std::vector<std::uint8_t> kind;  // the pixel's own flat ground, or kBandKind
    std::vector<std::uint8_t> side;  // the ground whose side the pixel is on, or kBandKind in a band
    std::vector<std::uint8_t> pair;  // in a band, higher * grounds + lower of its two nearest sides
    std::vector<std::vector<float>> to;  // for each ground, the distance to its side
    std::vector<std::vector<float>> normalX;  // for each ground, the way away from its side
    std::vector<std::vector<float>> normalY;
    std::vector<std::vector<float>> pairWidth;  // for each pair, its band's width, carried over the whole piece
};

SourceBand sourceBand(const PaletteImage& image, const std::vector<const std::vector<std::uint64_t>*>& windows) {
    SourceBand band;
    band.width = image.width;
    band.height = image.height;
    band.grounds = static_cast<int>(windows.size());
    const int w = image.width;
    const int h = image.height;
    const std::size_t width = static_cast<std::size_t>(w);
    const std::size_t n = width * static_cast<std::size_t>(h);
    const std::size_t grounds = windows.size();
    band.kind.assign(n, kBandKind);
    for (std::size_t g = 0; g < grounds; ++g) {
        const std::vector<std::uint8_t> flat = groundPixels(image, *windows[g]);
        for (std::size_t i = 0; i < n; ++i) {
            if (band.kind[i] == kBandKind && flat[i] != 0) {
                band.kind[i] = static_cast<std::uint8_t>(g);
            }
        }
    }
    // Groups of 4-connected pixels that pass a test, each with its pixels, whether it touches the piece's edge,
    // and which sides it touches.
    const int dx[4] = {1, -1, 0, 0};
    const int dy[4] = {0, 0, 1, -1};
    std::vector<std::uint8_t> seen(n, 0);
    std::vector<std::size_t> stack;
    std::vector<std::size_t> members;
    const auto group = [&](std::size_t start, const auto& inside, const auto& neighbour) {
        members.clear();
        bool edge = false;
        stack.push_back(start);
        seen[start] = 1;
        while (!stack.empty()) {
            const std::size_t i = stack.back();
            stack.pop_back();
            members.push_back(i);
            const int x = static_cast<int>(i % width);
            const int y = static_cast<int>(i / width);
            edge = edge || x == 0 || y == 0 || x == w - 1 || y == h - 1;
            for (int k = 0; k < 4; ++k) {
                const int sx = x + dx[k];
                const int sy = y + dy[k];
                if (sx < 0 || sy < 0 || sx >= w || sy >= h) {
                    continue;
                }
                const std::size_t j = static_cast<std::size_t>(sy) * width + static_cast<std::size_t>(sx);
                if (inside(j)) {
                    if (seen[j] == 0) {
                        seen[j] = 1;
                        stack.push_back(j);
                    }
                } else {
                    neighbour(j);
                }
            }
        }
        return edge;
    };
    band.side.assign(n, kBandKind);
    for (std::size_t start = 0; start < n; ++start) {
        if (band.kind[start] == kBandKind || seen[start] != 0) {
            continue;
        }
        const std::uint8_t ground = band.kind[start];
        const bool edge = group(start, [&](std::size_t j) { return band.kind[j] == ground; }, [](std::size_t) {});
        if (edge || members.size() >= static_cast<std::size_t>(kSmallFlat)) {
            for (const std::size_t i : members) {
                band.side[i] = ground;
            }
        }
    }
    // The rest is in a band, unless it touches only one ground's side. Groups do not touch one another, so a
    // group can join a side as soon as it is found.
    std::fill(seen.begin(), seen.end(), 0);
    for (std::size_t start = 0; start < n; ++start) {
        if (band.side[start] != kBandKind || seen[start] != 0) {
            continue;
        }
        unsigned touched = 0;
        group(start, [&](std::size_t j) { return band.side[j] == kBandKind; }, [&](std::size_t j) { touched |= 1u << band.side[j]; });
        if (touched != 0 && (touched & (touched - 1)) == 0) {
            std::uint8_t ground = 0;
            while ((touched >> ground) != 1u) {
                ++ground;
            }
            for (const std::size_t i : members) {
                band.side[i] = ground;
            }
        }
    }
    band.to.resize(grounds);
    band.normalX.resize(grounds);
    band.normalY.resize(grounds);
    for (std::size_t g = 0; g < grounds; ++g) {
        std::vector<std::uint8_t> mask(n, 0);
        for (std::size_t i = 0; i < n; ++i) {
            mask[i] = band.side[i] == g ? 1 : 0;
        }
        band.to[g] = distanceTo(mask, w, h);
        directions(band.to[g], w, h, 6, band.normalX[g], band.normalY[g]);
    }
    // Each band pixel belongs to the band between its two nearest sides.
    band.pair.assign(n, kBandKind);
    std::vector<std::vector<float>> sums(grounds * grounds);
    std::vector<std::vector<float>> counts(grounds * grounds);
    for (std::size_t i = 0; i < n; ++i) {
        if (band.side[i] != kBandKind || grounds < 2) {
            continue;
        }
        std::size_t first = 0;
        std::size_t second = 1;
        if (band.to[second][i] < band.to[first][i]) {
            std::swap(first, second);
        }
        for (std::size_t g = 2; g < grounds; ++g) {
            if (band.to[g][i] < band.to[first][i]) {
                second = first;
                first = g;
            } else if (band.to[g][i] < band.to[second][i]) {
                second = g;
            }
        }
        const std::size_t pair = std::min(first, second) * grounds + std::max(first, second);
        band.pair[i] = static_cast<std::uint8_t>(pair);
        if (sums[pair].empty()) {
            sums[pair].assign(n, 0.0f);
            counts[pair].assign(n, 0.0f);
        }
        sums[pair][i] = band.to[first][i] + band.to[second][i];
        counts[pair][i] = 1.0f;
    }
    // The band's width, smoothed along it and carried out over the rest of the piece.
    band.pairWidth.resize(grounds * grounds);
    for (std::size_t pair = 0; pair < grounds * grounds; ++pair) {
        if (sums[pair].empty()) {
            continue;
        }
        std::vector<float>& out = band.pairWidth[pair];
        out.assign(n, 0.0f);
        std::vector<std::uint8_t> known(n, 0);
        for (const int radius : {16, 48, 128, 256}) {
            const std::vector<float> sum = blurred(sums[pair], w, h, radius);
            const std::vector<float> count = blurred(counts[pair], w, h, radius);
            for (std::size_t i = 0; i < n; ++i) {
                if (known[i] == 0 && count[i] > 1.0e-4f) {
                    out[i] = sum[i] / count[i];
                    known[i] = 1;
                }
            }
        }
    }
    return band;
}

// The bands an 8 x 8 piece should have. Each ground's side is the halved side, and a band runs from the side
// of its higher ground for kBandScale of the 16 x 16 band's width.
struct TargetBand {
    int width = 0;
    int height = 0;
    int grounds = 0;
    std::vector<std::vector<float>> from;  // for each ground, the distance from its halved side
    std::vector<std::vector<float>> pairWidth;  // for each pair, the width its band should have
    // From classify:
    std::vector<std::uint8_t> flat;  // the pixel's flat ground, or kBandKind in a band
    std::vector<std::uint8_t> higher;
    std::vector<std::uint8_t> lower;
    std::vector<float> along;  // in a band, the distance from its higher ground's side
    std::vector<float> bandWidth;
    std::vector<float> normalX;
    std::vector<float> normalY;
};

TargetBand halvedTarget(const SourceBand& source) {
    TargetBand target;
    target.width = source.width / 2;
    target.height = source.height / 2;
    target.grounds = source.grounds;
    const std::size_t n = static_cast<std::size_t>(target.width) * static_cast<std::size_t>(target.height);
    const std::size_t sourceWidth = static_cast<std::size_t>(source.width);
    const std::size_t grounds = static_cast<std::size_t>(source.grounds);
    target.from.resize(grounds);
    for (std::size_t g = 0; g < grounds; ++g) {
        std::vector<std::uint8_t> mask(n, 0);
        for (int y = 0; y < target.height; ++y) {
            for (int x = 0; x < target.width; ++x) {
                int count = 0;
                for (int v = 0; v < 2; ++v) {
                    for (int u = 0; u < 2; ++u) {
                        count += source.side[static_cast<std::size_t>(2 * y + v) * sourceWidth + static_cast<std::size_t>(2 * x + u)] == g ? 1 : 0;
                    }
                }
                mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width) + static_cast<std::size_t>(x)] = count >= 2 ? 1 : 0;
            }
        }
        target.from[g] = distanceTo(mask, target.width, target.height);
    }
    target.pairWidth.resize(source.pairWidth.size());
    for (std::size_t pair = 0; pair < source.pairWidth.size(); ++pair) {
        if (source.pairWidth[pair].empty()) {
            continue;
        }
        target.pairWidth[pair].resize(n);
        for (int y = 0; y < target.height; ++y) {
            for (int x = 0; x < target.width; ++x) {
                target.pairWidth[pair][static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width) + static_cast<std::size_t>(x)] =
                    kBandScale * source.pairWidth[pair][static_cast<std::size_t>(2 * y) * sourceWidth + static_cast<std::size_t>(2 * x)];
            }
        }
    }
    return target;
}

// The smallest of the values within r pixels across and down of each pixel, cut short at the edges.
std::vector<float> eroded(const std::vector<float>& values, int w, int h, int r) {
    const std::size_t width = static_cast<std::size_t>(w);
    std::vector<float> across(values.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float least = values[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
            for (int sx = std::max(0, x - r); sx <= std::min(w - 1, x + r); ++sx) {
                least = std::min(least, values[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(sx)]);
            }
            across[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = least;
        }
    }
    std::vector<float> out(values.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float least = across[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
            for (int sy = std::max(0, y - r); sy <= std::min(h - 1, y + r); ++sy) {
                least = std::min(least, across[static_cast<std::size_t>(sy) * width + static_cast<std::size_t>(x)]);
            }
            out[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = least;
        }
    }
    return out;
}

// A band must end before a side whose ends are both its lower ground, or it would stop in a straight line along
// that side. Where it would come near one it narrows, gradually, so its far edge keeps following its own shape.
// corners are the piece's grounds at its top left, top right, bottom left and bottom right.
void keepBandsOffSides(TargetBand& target, const std::array<std::size_t, 4>& corners) {
    const int w = target.width;
    const int h = target.height;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    const std::size_t grounds = static_cast<std::size_t>(target.grounds);
    struct Side {
        std::size_t first;
        std::size_t second;
        bool vertical;
        bool far;
    };
    const Side sides[4] = {{corners[0], corners[2], true, false}, {corners[1], corners[3], true, true}, {corners[0], corners[1], false, false}, {corners[2], corners[3], false, true}};
    for (const Side& side : sides) {
        if (side.first != side.second) {
            continue;
        }
        const std::size_t lower = side.first;
        for (std::size_t higher = 0; higher < lower; ++higher) {
            std::vector<float>& widths = target.pairWidth[higher * grounds + lower];
            if (widths.empty()) {
                continue;
            }
            std::vector<float> limit(n);
            std::vector<float> share(n);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const std::size_t p = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                    const int depth = side.vertical ? (side.far ? w - 1 - x : x) : (side.far ? h - 1 - y : y);
                    limit[p] = std::max(0.0f, target.from[higher][p] + static_cast<float>(depth - kSideRoom));
                    share[p] = widths[p] > limit[p] ? limit[p] / widths[p] : 1.0f;
                }
            }
            share = blurred(eroded(share, w, h, 24), w, h, 24);
            for (std::size_t p = 0; p < n; ++p) {
                widths[p] = std::min(widths[p] * share[p], limit[p]);
            }
        }
    }
}

// Which ground or band each pixel of a target is. A pixel lies on the lower of the grounds about it, unless the
// band from a higher ground's side reaches it; where two bands could, the one it is further into by share wins.
void classify(TargetBand& target) {
    const std::size_t n = static_cast<std::size_t>(target.width) * static_cast<std::size_t>(target.height);
    const std::size_t grounds = static_cast<std::size_t>(target.grounds);
    target.flat.assign(n, kBandKind);
    target.higher.assign(n, 0);
    target.lower.assign(n, 0);
    target.along.assign(n, 0.0f);
    target.bandWidth.assign(n, 0.0f);
    std::vector<std::vector<float>> nx(grounds);
    std::vector<std::vector<float>> ny(grounds);
    for (std::size_t g = 0; g < grounds; ++g) {
        directions(target.from[g], target.width, target.height, 3, nx[g], ny[g]);
    }
    target.normalX.assign(n, 0.0f);
    target.normalY.assign(n, 0.0f);
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t base = grounds;
        for (std::size_t g = 0; g < grounds && base == grounds; ++g) {
            if (target.from[g][i] <= 0.0f) {
                base = g;
            }
        }
        if (base == grounds) {
            // Between sides: the lower of the two nearest.
            std::size_t first = 0;
            std::size_t second = grounds > 1 ? 1 : 0;
            if (target.from[second][i] < target.from[first][i]) {
                std::swap(first, second);
            }
            for (std::size_t g = 2; g < grounds; ++g) {
                if (target.from[g][i] < target.from[first][i]) {
                    second = first;
                    first = g;
                } else if (target.from[g][i] < target.from[second][i]) {
                    second = g;
                }
            }
            base = std::max(first, second);
        }
        float bestShare = 1.0f;
        std::size_t bestHigher = grounds;
        for (std::size_t g = 0; g < base; ++g) {
            const std::vector<float>& widths = target.pairWidth[g * grounds + base];
            if (widths.empty() || widths[i] <= 0.0f) {
                continue;
            }
            const float share = target.from[g][i] / widths[i];
            if (share < bestShare) {
                bestShare = share;
                bestHigher = g;
            }
        }
        if (bestHigher == grounds) {
            target.flat[i] = static_cast<std::uint8_t>(base);
            continue;
        }
        target.higher[i] = static_cast<std::uint8_t>(bestHigher);
        target.lower[i] = static_cast<std::uint8_t>(base);
        target.along[i] = target.from[bestHigher][i];
        target.bandWidth[i] = target.pairWidth[bestHigher * grounds + base][i];
        target.normalX[i] = nx[bestHigher][i];
        target.normalY[i] = ny[bestHigher][i];
    }
}

// How badly source pixel q stands in for target pixel p. Near its higher ground a band is matched by its distance
// from that side, and near its lower ground by its distance from that side, so both edges keep their full-size
// look and the middle of the band is what gets cut out.
double guideCost(const TargetBand& target, std::size_t p, const SourceBand& source, std::size_t q) {
    if (target.flat[p] != kBandKind) {
        // A rock lying on a ground is on its side, but is not flat ground to copy where the piece is flat.
        const double gap = std::min(source.to[target.flat[p]][q], 40.0f);
        return gap * gap + (source.kind[q] == target.flat[p] ? 0.0 : 900.0);
    }
    const std::size_t higher = target.higher[p];
    const std::size_t lower = target.lower[p];
    const float along = target.along[p];
    const float width = target.bandWidth[p];
    const float toHigher = source.to[higher][q];
    const float toLower = source.to[lower][q];
    double cost = 0.0;
    if (along <= width - kWaterEdge) {
        const double gap = toHigher - along;
        const double short_ = std::max(0.0f, kWaterEdge - toLower);
        cost = gap * gap + short_ * short_;
    } else {
        const double gap = toLower - (width - along);
        const double short_ = std::max(0.0f, (width - kWaterEdge) - toHigher);
        cost = gap * gap + short_ * short_;
    }
    // Another band, or a third ground's side, is the wrong place.
    if (source.grounds > 2) {
        const std::uint8_t side = source.side[q];
        const bool wrong = side == kBandKind ? source.pair[q] != higher * static_cast<std::size_t>(source.grounds) + lower : side != higher && side != lower;
        if (wrong) {
            cost += 2500.0;
        }
    }
    // Keep the light on the same side of the band.
    if (target.normalX[p] != 0.0f || target.normalY[p] != 0.0f) {
        cost += 400.0 * (1.0 - (target.normalX[p] * source.normalX[higher][q] + target.normalY[p] * source.normalY[higher][q]));
    }
    return cost;
}

double colourGap(std::uint8_t left, std::uint8_t right) {
    double sum = 0.0;
    for (int k = 0; k < 3; ++k) {
        const double d = static_cast<double>(kTaPalette[left][k]) - static_cast<double>(kTaPalette[right][k]);
        sum += d * d;
    }
    return sum;
}

// An 8 x 8 piece being drawn: its picture, which pixels are drawn and what each is, and which belong to a side
// strip and may not be drawn over.
struct Canvas {
    PaletteImage image;
    std::vector<std::uint8_t> drawn;
    std::vector<std::uint8_t> kind;
    std::vector<std::uint8_t> fixed;

    Canvas(int w, int h) {
        image.width = w;
        image.height = h;
        const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
        image.pixels.assign(n, 0);
        drawn.assign(n, 0);
        kind.assign(n, kBandKind);
        fixed.assign(n, 0);
    }
};

// The cheapest path down a band of kOverlap columns, starting at column first, through a kPatch square of
// costs; one column a row. Fixed pixels may not be on it.
std::vector<int> seamDown(const std::vector<double>& costs, int first, const std::vector<std::uint8_t>& fixed) {
    const std::size_t band = static_cast<std::size_t>(kOverlap);
    std::vector<double> total(static_cast<std::size_t>(kPatch) * band);
    std::vector<int> from(total.size(), 0);
    for (int v = 0; v < kPatch; ++v) {
        for (int k = 0; k < kOverlap; ++k) {
            const std::size_t at = static_cast<std::size_t>(v) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(first + k);
            const double cost = costs[at] + (fixed[at] != 0 ? 1.0e12 : 0.0);
            const std::size_t here = static_cast<std::size_t>(v) * band + static_cast<std::size_t>(k);
            if (v == 0) {
                total[here] = cost;
                continue;
            }
            double best = 1.0e300;
            int bestK = k;
            for (int step = -1; step <= 1; ++step) {
                const int above = k + step;
                if (above < 0 || above >= kOverlap) {
                    continue;
                }
                const double candidate = total[static_cast<std::size_t>(v - 1) * band + static_cast<std::size_t>(above)];
                if (candidate < best) {
                    best = candidate;
                    bestK = above;
                }
            }
            total[here] = cost + best;
            from[here] = bestK;
        }
    }
    int k = 0;
    double best = 1.0e300;
    for (int j = 0; j < kOverlap; ++j) {
        const double candidate = total[static_cast<std::size_t>(kPatch - 1) * band + static_cast<std::size_t>(j)];
        if (candidate < best) {
            best = candidate;
            k = j;
        }
    }
    std::vector<int> seam(static_cast<std::size_t>(kPatch));
    for (int v = kPatch - 1; v >= 0; --v) {
        seam[static_cast<std::size_t>(v)] = first + k;
        if (v > 0) {
            k = from[static_cast<std::size_t>(v) * band + static_cast<std::size_t>(k)];
        }
    }
    return seam;
}

template <typename T>
std::vector<T> transposedSquare(const std::vector<T>& square) {
    std::vector<T> out(square.size());
    for (int v = 0; v < kPatch; ++v) {
        for (int u = 0; u < kPatch; ++u) {
            out[static_cast<std::size_t>(u) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(v)] =
                square[static_cast<std::size_t>(v) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(u)];
        }
    }
    return out;
}

// Draws the bands of target over canvas with full-size patches of source. Each patch is the one near its halved
// place that best matches the bands there and the picture already drawn, and is laid down inside the cheapest
// seams through its overlaps. groundOf turns the piece's grounds into the maker's. wrapX or wrapY make the
// picture join itself across that pair of sides.
void drawBand(const PaletteImage& source, const SourceBand& band, const TargetBand& target, const std::vector<int>& groundOf, Canvas& canvas, bool wrapX, bool wrapY) {
    const int w = target.width;
    const int h = target.height;
    const std::size_t sourceWidth = static_cast<std::size_t>(source.width);
    const int step = kPatch - kOverlap;
    const auto places = [&](int length, bool wrap) {
        std::vector<int> out;
        if (wrap) {
            for (int at = 0; at < length; at += step) {
                out.push_back(at);
            }
        } else {
            for (int at = 0;; at += step) {
                out.push_back(std::min(at, length - kPatch));
                if (at + kPatch >= length) {
                    break;
                }
            }
        }
        return out;
    };
    const auto wrapped = [](int value, int length, bool wrap) { return wrap ? ((value % length) + length) % length : value; };
    const std::size_t square = static_cast<std::size_t>(kPatch) * static_cast<std::size_t>(kPatch);
    std::vector<double> gaps(square);
    std::vector<std::uint8_t> drawnHere(square);
    std::vector<std::uint8_t> fixedHere(square);
    std::vector<std::uint8_t> keep(square);
    for (const int py : places(h, wrapY)) {
        for (const int px : places(w, wrapX)) {
            const auto pixel = [&](int u, int v) {
                return static_cast<std::size_t>(wrapped(py + v, h, wrapY)) * static_cast<std::size_t>(w) + static_cast<std::size_t>(wrapped(px + u, w, wrapX));
            };
            // Patches with no band near them are left as flat ground.
            bool nearBand = false;
            for (int v = -8; v < kPatch + 8 && !nearBand; ++v) {
                for (int u = -8; u < kPatch + 8; ++u) {
                    const int x = wrapped(px + u, w, wrapX);
                    const int y = wrapped(py + v, h, wrapY);
                    if (x >= 0 && y >= 0 && x < w && y < h && target.flat[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] == kBandKind) {
                        nearBand = true;
                        break;
                    }
                }
            }
            if (!nearBand) {
                continue;
            }
            const auto sourceKind = [&](std::size_t q) {
                return band.kind[q] == kBandKind ? kBandKind : static_cast<std::uint8_t>(groundOf[band.kind[q]]);
            };
            const auto overlapGap = [&](std::size_t p, std::size_t q) -> double {
                if (canvas.drawn[p] == 0) {
                    return 0.0;
                }
                const std::uint8_t here = canvas.kind[p];
                const std::uint8_t there = sourceKind(q);
                if (here != kBandKind && here == there) {
                    return 0.0;
                }
                // Flat ground against the band, or two grounds, still compare by colour, so a seam between them
                // follows where they look alike rather than running straight.
                return colourGap(canvas.image.pixels[p], source.pixels[q]) + (here != there ? 600.0 : 0.0);
            };
            // Shortlist by the band alone, on a coarse grid, then pick by the band and the overlaps.
            struct Candidate {
                double cost;
                int x;
                int y;
            };
            std::vector<Candidate> candidates;
            const int centreX = 2 * (px + kPatch / 2);
            const int centreY = 2 * (py + kPatch / 2);
            for (int qy = std::max(0, centreY - kSearch - kPatch / 2); qy <= std::min(source.height - kPatch, centreY + kSearch - kPatch / 2); qy += 2) {
                for (int qx = std::max(0, centreX - kSearch - kPatch / 2); qx <= std::min(source.width - kPatch, centreX + kSearch - kPatch / 2); qx += 2) {
                    double cost = 0.0;
                    for (int v = 0; v < kPatch; v += 4) {
                        for (int u = 0; u < kPatch; u += 4) {
                            cost += guideCost(target, pixel(u, v), band, static_cast<std::size_t>(qy + v) * sourceWidth + static_cast<std::size_t>(qx + u));
                        }
                    }
                    candidates.push_back({cost, qx, qy});
                }
            }
            if (candidates.empty()) {
                continue;
            }
            const std::size_t shortlist = std::min<std::size_t>(40, candidates.size());
            std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(shortlist), candidates.end(), [](const Candidate& a, const Candidate& b) {
                return a.cost < b.cost || (a.cost == b.cost && (a.y < b.y || (a.y == b.y && a.x < b.x)));
            });
            double best = 1.0e300;
            int bestX = candidates.front().x;
            int bestY = candidates.front().y;
            for (std::size_t c = 0; c < shortlist; ++c) {
                double cost = 0.0;
                for (int v = 0; v < kPatch; v += 2) {
                    for (int u = 0; u < kPatch; u += 2) {
                        const std::size_t p = pixel(u, v);
                        const std::size_t q = static_cast<std::size_t>(candidates[c].y + v) * sourceWidth + static_cast<std::size_t>(candidates[c].x + u);
                        cost += guideCost(target, p, band, q) + 0.08 * overlapGap(p, q);
                    }
                }
                if (cost < best) {
                    best = cost;
                    bestX = candidates[c].x;
                    bestY = candidates[c].y;
                }
            }
            for (int v = 0; v < kPatch; ++v) {
                for (int u = 0; u < kPatch; ++u) {
                    const std::size_t i = static_cast<std::size_t>(v) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(u);
                    const std::size_t p = pixel(u, v);
                    gaps[i] = overlapGap(p, static_cast<std::size_t>(bestY + v) * sourceWidth + static_cast<std::size_t>(bestX + u));
                    drawnHere[i] = canvas.drawn[p];
                    fixedHere[i] = canvas.fixed[p];
                }
            }
            // Where the patch overlaps drawn pixels along a side, the drawn pixels beyond the cheapest seam stay.
            const auto anyDrawn = [&](int u0, int u1, int v0, int v1) {
                for (int v = v0; v < v1; ++v) {
                    for (int u = u0; u < u1; ++u) {
                        if (drawnHere[static_cast<std::size_t>(v) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(u)] != 0) {
                            return true;
                        }
                    }
                }
                return false;
            };
            std::fill(keep.begin(), keep.end(), 0);
            const auto keepAt = [&](int u, int v) { keep[static_cast<std::size_t>(v) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(u)] = 1; };
            if (anyDrawn(0, kOverlap, 0, kPatch)) {
                const std::vector<int> seam = seamDown(gaps, 0, fixedHere);
                for (int v = 0; v < kPatch; ++v) {
                    for (int u = 0; u < seam[static_cast<std::size_t>(v)]; ++u) {
                        keepAt(u, v);
                    }
                }
            }
            if (anyDrawn(kPatch - kOverlap, kPatch, 0, kPatch)) {
                const std::vector<int> seam = seamDown(gaps, kPatch - kOverlap, fixedHere);
                for (int v = 0; v < kPatch; ++v) {
                    for (int u = seam[static_cast<std::size_t>(v)] + 1; u < kPatch; ++u) {
                        keepAt(u, v);
                    }
                }
            }
            const std::vector<double> gapsAcross = transposedSquare(gaps);
            const std::vector<std::uint8_t> fixedAcross = transposedSquare(fixedHere);
            if (anyDrawn(0, kPatch, 0, kOverlap)) {
                const std::vector<int> seam = seamDown(gapsAcross, 0, fixedAcross);
                for (int u = 0; u < kPatch; ++u) {
                    for (int v = 0; v < seam[static_cast<std::size_t>(u)]; ++v) {
                        keepAt(u, v);
                    }
                }
            }
            if (anyDrawn(0, kPatch, kPatch - kOverlap, kPatch)) {
                const std::vector<int> seam = seamDown(gapsAcross, kPatch - kOverlap, fixedAcross);
                for (int u = 0; u < kPatch; ++u) {
                    for (int v = seam[static_cast<std::size_t>(u)] + 1; v < kPatch; ++v) {
                        keepAt(u, v);
                    }
                }
            }
            for (int v = 0; v < kPatch; ++v) {
                for (int u = 0; u < kPatch; ++u) {
                    const std::size_t i = static_cast<std::size_t>(v) * static_cast<std::size_t>(kPatch) + static_cast<std::size_t>(u);
                    if (fixedHere[i] != 0 || (drawnHere[i] != 0 && keep[i] != 0)) {
                        continue;
                    }
                    const std::size_t p = pixel(u, v);
                    const std::size_t q = static_cast<std::size_t>(bestY + v) * sourceWidth + static_cast<std::size_t>(bestX + u);
                    canvas.image.pixels[p] = source.pixels[q];
                    canvas.kind[p] = sourceKind(q);
                    canvas.drawn[p] = 1;
                }
            }
        }
    }
}

// Lays each ground's own texture, from its top-left corner, over the canvas's flat ground and anything not drawn.
void settleGround(Canvas& canvas, const TargetBand& target, const std::vector<int>& groundOf, const std::vector<PaletteImage>& grounds) {
    for (int y = 0; y < target.height; ++y) {
        for (int x = 0; x < target.width; ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * static_cast<std::size_t>(target.width) + static_cast<std::size_t>(x);
            std::uint8_t kind = canvas.kind[p];
            if (canvas.drawn[p] == 0) {
                kind = static_cast<std::uint8_t>(groundOf[target.flat[p] != kBandKind ? target.flat[p] : target.lower[p]]);
            }
            if (kind != kBandKind) {
                const PaletteImage& ground = grounds[kind];
                canvas.image.pixels[p] = ground.at(x % ground.width, y % ground.height);
            }
            canvas.kind[p] = kind;
            canvas.drawn[p] = 1;
        }
    }
}

// The four corners of a cell named by a coast piece's code: a corner (tl) or a side (b).
std::optional<std::array<bool, 4>> codeCorners(const std::string& code) {
    static const std::pair<const char*, std::array<bool, 4>> kCodes[] = {
        {"tl", {true, false, false, false}}, {"tr", {false, true, false, false}}, {"bl", {false, false, true, false}}, {"br", {false, false, false, true}},
        {"t", {true, true, false, false}}, {"b", {false, false, true, true}}, {"l", {true, false, true, false}}, {"r", {false, true, false, true}},
    };
    for (const auto& [text, corners] : kCodes) {
        if (code == text) {
            return corners;
        }
    }
    return std::nullopt;
}

std::vector<std::string> dashParts(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t dash = text.find('-', start);
        parts.push_back(text.substr(start, dash == std::string::npos ? std::string::npos : dash - start));
        if (dash == std::string::npos) {
            break;
        }
        start = dash + 1;
    }
    return parts;
}

}  // namespace

std::optional<std::array<int, 4>> coastPieceLevels(std::string_view name) {
    std::string lowered(name);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    const std::vector<std::string> parts = dashParts(lowered);
    if (parts.size() < 3) {
        return std::nullopt;
    }
    // A two-ground piece: the ground its word names lies at its code's corners, the other ground elsewhere.
    const auto twoWay = [&](int named, int other) -> std::optional<std::array<int, 4>> {
        const std::optional<std::array<bool, 4>> marked = codeCorners(parts[2]);
        if (!marked) {
            return std::nullopt;
        }
        std::array<int, 4> levels{};
        for (std::size_t corner = 0; corner < 4; ++corner) {
            levels[corner] = (*marked)[corner] ? named : other;
        }
        return levels;
    };
    if (parts[0] == "coast") {
        if (parts[1] == "land") {
            return twoWay(0, 2);
        }
        if (parts[1] == "water" || parts[1] == "lava") {
            return twoWay(2, 0);
        }
    } else if (parts[0] == "shallowcoast") {
        if (parts[1] == "land") {
            return twoWay(0, 1);
        }
        if (parts[1] == "water") {
            return twoWay(1, 0);
        }
    } else if (parts[0] == "shallowdeep") {
        if (parts[1] == "shallow") {
            return twoWay(1, 2);
        }
        if (parts[1] == "deep") {
            return twoWay(2, 1);
        }
    } else if (parts[0] == "dsl" && parts.size() >= 7 && parts[1] == "land" && parts[3] == "shallow" && parts[5] == "water") {
        // DSL-Land-B-Shallow-TL_and_BR-Water-TR names every corner once.
        std::array<int, 4> levels = {-1, -1, -1, -1};
        const std::pair<int, std::string> named[3] = {{0, parts[2]}, {1, parts[4]}, {2, parts[6]}};
        for (const auto& [level, codes] : named) {
            std::size_t start = 0;
            while (start <= codes.size()) {
                const std::size_t join = codes.find("_and_", start);
                const std::optional<std::array<bool, 4>> marked = codeCorners(codes.substr(start, join == std::string::npos ? std::string::npos : join - start));
                if (!marked) {
                    return std::nullopt;
                }
                for (std::size_t corner = 0; corner < 4; ++corner) {
                    if ((*marked)[corner]) {
                        if (levels[corner] >= 0) {
                            return std::nullopt;
                        }
                        levels[corner] = level;
                    }
                }
                if (join == std::string::npos) {
                    break;
                }
                start = join + 5;
            }
        }
        if (std::find(levels.begin(), levels.end(), -1) != levels.end()) {
            return std::nullopt;
        }
        return levels;
    }
    return std::nullopt;
}

std::optional<LandCorners> coastPieceCorners(std::string_view name) {
    std::string lowered(name);
    for (char& character : lowered) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    if (!lowered.starts_with("coast-")) {
        return std::nullopt;
    }
    const std::optional<std::array<int, 4>> levels = coastPieceLevels(name);
    if (!levels) {
        return std::nullopt;
    }
    return LandCorners{(*levels)[0] == 0, (*levels)[1] == 0, (*levels)[2] == 0, (*levels)[3] == 0};
}

DetailedCoastMaker::DetailedCoastMaker(std::vector<PaletteImage> grounds, const std::vector<Join>& joins) : grounds_(std::move(grounds)) {
    for (const PaletteImage& ground : grounds_) {
        windows_.push_back(textureWindows(ground));
    }
    // A join's strip 0 is a side with its higher ground above the coast, from the straight piece with the lower
    // ground along the bottom; 1 has the higher ground below; 2 has it to the left; and 3 to the right. Each
    // straight piece is drawn to join itself across the sides its coast crosses.
    for (const Join& join : joins) {
        if (join.higher < 0 || join.lower < 0 || join.higher >= join.lower || static_cast<std::size_t>(join.lower) >= grounds_.size()) {
            continue;
        }
        const std::vector<int> groundOf = {join.higher, join.lower};
        const std::vector<const std::vector<std::uint64_t>*> windows = {&windows_[static_cast<std::size_t>(join.higher)], &windows_[static_cast<std::size_t>(join.lower)]};
        std::array<Strip, 4> strips;
        for (std::size_t s = 0; s < 4; ++s) {
            const PaletteImage source = sectionImage(join.straights[s]);
            const SourceBand band = sourceBand(source, windows);
            TargetBand target = halvedTarget(band);
            const std::array<std::array<std::size_t, 4>, 4> stripCorners = {{{0, 0, 1, 1}, {1, 1, 0, 0}, {0, 1, 0, 1}, {1, 0, 1, 0}}};
            keepBandsOffSides(target, stripCorners[s]);
            classify(target);
            Canvas canvas(target.width, target.height);
            drawBand(source, band, target, groundOf, canvas, s < 2, s >= 2);
            settleGround(canvas, target, groundOf, grounds_);
            strips[s].image = std::move(canvas.image);
            strips[s].kinds = std::move(canvas.kind);
            strips[s].fromHigher = target.from[0];
            strips[s].width = target.pairWidth[1].empty() ? std::vector<float>(target.from[0].size(), 0.0f) : target.pairWidth[1];
        }
        joinHigher_.push_back(join.higher);
        joinLower_.push_back(join.lower);
        strips_.push_back(std::move(strips));
    }
}

DetailedCoastMaker::DetailedCoastMaker(PaletteImage land, PaletteImage water, const std::array<SectionStamp, 4>& straights)
    : DetailedCoastMaker(std::vector<PaletteImage>{std::move(land), std::move(water)}, std::vector<Join>{Join{0, 1, straights}}) {}

int DetailedCoastMaker::joinOf(int higher, int lower) const {
    for (std::size_t j = 0; j < joinHigher_.size(); ++j) {
        if (joinHigher_[j] == higher && joinLower_[j] == lower) {
            return static_cast<int>(j);
        }
    }
    return -1;
}

bool DetailedCoastMaker::canMake(const std::array<int, 4>& corners) const {
    for (const int corner : corners) {
        if (corner < 0 || static_cast<std::size_t>(corner) >= grounds_.size()) {
            return false;
        }
    }
    const int ends[4][2] = {{corners[0], corners[2]}, {corners[1], corners[3]}, {corners[0], corners[1]}, {corners[2], corners[3]}};
    for (const auto& end : ends) {
        if (end[0] != end[1] && joinOf(std::min(end[0], end[1]), std::max(end[0], end[1])) < 0) {
            return false;
        }
    }
    return true;
}

SectionStamp DetailedCoastMaker::make(const SectionStamp& piece, LandCorners corners) const {
    return make(piece, std::array<int, 4>{corners[0] ? 0 : 1, corners[1] ? 0 : 1, corners[2] ? 0 : 1, corners[3] ? 0 : 1});
}

SectionStamp DetailedCoastMaker::make(const SectionStamp& piece, const std::array<int, 4>& corners) const {
    // The grounds the piece joins, highest first.
    std::vector<int> groundOf(corners.begin(), corners.end());
    std::sort(groundOf.begin(), groundOf.end());
    groundOf.erase(std::unique(groundOf.begin(), groundOf.end()), groundOf.end());
    const auto local = [&](int ground) { return static_cast<std::size_t>(std::find(groundOf.begin(), groundOf.end(), ground) - groundOf.begin()); };
    std::vector<const std::vector<std::uint64_t>*> windows;
    for (const int ground : groundOf) {
        windows.push_back(&windows_[static_cast<std::size_t>(ground)]);
    }
    const PaletteImage source = sectionImage(piece);
    const SourceBand band = sourceBand(source, windows);
    TargetBand target = halvedTarget(band);
    const int w = target.width;
    const int h = target.height;
    const std::size_t grounds = groundOf.size();
    struct Side {
        int first;  // the ground at the side's top or left end
        int second;
        bool vertical;
        bool far;
    };
    const Side sides[4] = {
        {corners[0], corners[2], true, false},
        {corners[1], corners[3], true, true},
        {corners[0], corners[1], false, false},
        {corners[2], corners[3], false, true},
    };
    // Near a side the coast crosses, the piece's coast bends to meet the shape of the side's strip.
    for (const Side& side : sides) {
        const int join = side.first == side.second ? -1 : joinOf(std::min(side.first, side.second), std::max(side.first, side.second));
        if (join < 0) {
            continue;
        }
        const Strip& strip = strips_[static_cast<std::size_t>(join)][side.vertical ? (side.first < side.second ? 0 : 1) : (side.first < side.second ? 2 : 3)];
        if (strip.image.width != w || strip.image.height != h) {
            continue;
        }
        const std::size_t higher = local(std::min(side.first, side.second));
        const std::size_t lower = local(std::max(side.first, side.second));
        std::vector<float>& widths = target.pairWidth[higher * grounds + lower];
        if (widths.empty()) {
            widths.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
        }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int depth = side.vertical ? (side.far ? w - 1 - x : x) : (side.far ? h - 1 - y : y);
                if (depth >= kSideBlend) {
                    continue;
                }
                const float t = 1.0f - static_cast<float>(depth) / static_cast<float>(kSideBlend);
                const float weight = t * t * (3.0f - 2.0f * t);
                const std::size_t p = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                target.from[higher][p] += weight * (strip.fromHigher[p] - target.from[higher][p]);
                widths[p] += weight * (strip.width[p] - widths[p]);
            }
        }
    }
    keepBandsOffSides(target, {local(corners[0]), local(corners[1]), local(corners[2]), local(corners[3])});
    classify(target);
    // Each side the coast crosses is its shared strip; every other side is flat ground for a few pixels.
    Canvas canvas(w, h);
    for (const Side& side : sides) {
        const int join = side.first == side.second ? -1 : joinOf(std::min(side.first, side.second), std::max(side.first, side.second));
        const Strip* strip = join < 0 ? nullptr : &strips_[static_cast<std::size_t>(join)][side.vertical ? (side.first < side.second ? 0 : 1) : (side.first < side.second ? 2 : 3)];
        if (strip != nullptr && (strip->image.width != w || strip->image.height != h)) {
            strip = nullptr;
        }
        const bool crossed = side.first != side.second;
        const int depth = crossed && strip != nullptr ? kStrip : kStripHard + 1;
        if (crossed && strip == nullptr) {
            continue;
        }
        const int length = side.vertical ? h : w;
        for (int d = 0; d < depth; ++d) {
            for (int i = 0; i < length; ++i) {
                const int x = side.vertical ? (side.far ? w - 1 - d : d) : i;
                const int y = side.vertical ? i : (side.far ? h - 1 - d : d);
                const std::size_t p = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
                if (strip != nullptr) {
                    canvas.image.pixels[p] = strip->image.pixels[p];
                    canvas.kind[p] = strip->kinds[p];
                } else {
                    const PaletteImage& ground = grounds_[static_cast<std::size_t>(side.first)];
                    canvas.kind[p] = static_cast<std::uint8_t>(side.first);
                    canvas.image.pixels[p] = ground.at(x % ground.width, y % ground.height);
                }
                canvas.drawn[p] = 1;
                if (d < kStripHard) {
                    canvas.fixed[p] = 1;
                }
            }
        }
    }
    drawBand(source, band, target, groundOf, canvas, false, false);
    settleGround(canvas, target, groundOf, grounds_);
    return stampFromImage(canvas.image, halvedHeights(piece));
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
        {"metal", "Metal"}, {"acid", "Acid"}, {"lava", "Lava"}, {"lush", "Lush"}, {"mars", "Mars"}, {"ice", "Ice"}, {"quag", "Quagmire"}, {"snow", "SnowyEvergreen"}, {"ever2", "Evergreen2"}, {"ever", "Evergreen"},
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

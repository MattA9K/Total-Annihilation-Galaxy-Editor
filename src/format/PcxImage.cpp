#include "format/PcxImage.hpp"

#include <algorithm>

namespace galaxy {

namespace {

constexpr std::size_t kHeaderSize = 128;
constexpr std::size_t kPaletteSize = 769;  // a 0x0C marker, then 256 RGB triples
constexpr int kMaxSide = 16384;

int read16(const std::uint8_t* p) {
    return static_cast<int>(p[0]) | (static_cast<int>(p[1]) << 8);
}

}  // namespace

std::optional<PcxImage> decodePcx(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kHeaderSize) return std::nullopt;
    if (data[0] != 0x0A || data[2] > 1) return std::nullopt;
    const bool encoded = data[2] == 1;
    const int bits = data[3];
    const int width = read16(data + 8) - read16(data + 4) + 1;
    const int height = read16(data + 10) - read16(data + 6) + 1;
    const int planes = data[65];
    const int bytesPerLine = read16(data + 66);
    if (width <= 0 || height <= 0 || width > kMaxSide || height > kMaxSide) return std::nullopt;

    enum class Layout { Paletted8, Rgb, Planar16, Packed16, Mono };
    Layout layout;
    if (bits == 8 && planes == 1) {
        layout = Layout::Paletted8;
    } else if (bits == 8 && (planes == 3 || planes == 4)) {
        layout = Layout::Rgb;
    } else if (bits == 1 && planes == 4) {
        layout = Layout::Planar16;
    } else if (bits == 4 && planes == 1) {
        layout = Layout::Packed16;
    } else if (bits == 1 && planes == 1) {
        layout = Layout::Mono;
    } else {
        return std::nullopt;
    }
    if (static_cast<long long>(bytesPerLine) * 8 < static_cast<long long>(width) * bits) return std::nullopt;

    // The 256-colour palette sits after the pixels, so the pixels stop where it starts.
    std::size_t end = size;
    const bool trailingPalette = layout == Layout::Paletted8 && size >= kHeaderSize + kPaletteSize && data[size - kPaletteSize] == 0x0C;
    if (trailingPalette) end = size - kPaletteSize;

    const std::size_t lineBytes = static_cast<std::size_t>(bytesPerLine) * static_cast<std::size_t>(planes);
    const std::size_t total = lineBytes * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> raw;
    raw.reserve(total);
    std::size_t at = kHeaderSize;
    while (raw.size() < total && at < end) {
        const std::uint8_t byte = data[at++];
        if (encoded && (byte & 0xC0) == 0xC0) {
            if (at >= end) break;
            const std::uint8_t value = data[at++];
            const std::size_t count = std::min<std::size_t>(byte & 0x3F, total - raw.size());
            raw.insert(raw.end(), count, value);
        } else {
            raw.push_back(byte);
        }
    }
    if (raw.size() < total) return std::nullopt;

    // Colours for the paletted layouts.
    std::uint8_t palette[256][3] = {};
    if (layout == Layout::Paletted8) {
        for (int i = 0; i < 256; ++i) {
            for (int c = 0; c < 3; ++c) {
                // Without a palette at the end, show the indices as shades of grey.
                palette[i][c] = trailingPalette ? data[size - kPaletteSize + 1 + static_cast<std::size_t>(i) * 3 + static_cast<std::size_t>(c)]
                                                : static_cast<std::uint8_t>(i);
            }
        }
    } else if (layout == Layout::Planar16 || layout == Layout::Packed16) {
        for (int i = 0; i < 16; ++i) {
            for (int c = 0; c < 3; ++c) palette[i][c] = data[16 + i * 3 + c];
        }
    } else if (layout == Layout::Mono) {
        palette[1][0] = palette[1][1] = palette[1][2] = 255;
    }

    PcxImage image;
    image.width = width;
    image.height = height;
    image.bitsPerPixel = bits * planes;
    image.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* line = raw.data() + lineBytes * static_cast<std::size_t>(y);
        std::uint8_t* out = image.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        for (int x = 0; x < width; ++x, out += 4) {
            out[3] = 255;
            int index = 0;
            switch (layout) {
            case Layout::Rgb:
                for (int c = 0; c < 3; ++c) out[c] = line[static_cast<std::size_t>(c) * static_cast<std::size_t>(bytesPerLine) + static_cast<std::size_t>(x)];
                if (planes == 4) out[3] = line[static_cast<std::size_t>(3) * static_cast<std::size_t>(bytesPerLine) + static_cast<std::size_t>(x)];
                continue;
            case Layout::Paletted8: index = line[x]; break;
            case Layout::Packed16: index = (line[x / 2] >> ((x % 2) == 0 ? 4 : 0)) & 0x0F; break;
            case Layout::Mono: index = (line[x / 8] >> (7 - x % 8)) & 1; break;
            case Layout::Planar16:
                for (int p = 0; p < 4; ++p) {
                    const std::uint8_t byte = line[static_cast<std::size_t>(p) * static_cast<std::size_t>(bytesPerLine) + static_cast<std::size_t>(x / 8)];
                    index |= ((byte >> (7 - x % 8)) & 1) << p;
                }
                break;
            }
            out[0] = palette[index][0];
            out[1] = palette[index][1];
            out[2] = palette[index][2];
        }
    }
    return image;
}

}  // namespace galaxy

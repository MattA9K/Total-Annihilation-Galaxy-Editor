#include "format/BmpImage.hpp"

#include <fstream>
#include <stdexcept>

namespace galaxy {

namespace {

void put16(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value & 0xFF);
    out[at + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
}

void put32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t value) {
    put16(out, at, value & 0xFFFF);
    put16(out, at + 2, value >> 16);
}

}  // namespace

std::vector<std::uint8_t> encodeBmp24(
    int width,
    int height,
    const std::uint8_t* indices,
    const std::uint8_t (*palette)[3]) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("A BMP needs a width and height of at least 1");
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    const std::size_t stride = (w * 3 + 3) & ~static_cast<std::size_t>(3);
    const std::size_t headerSize = 14 + 40;
    const std::size_t imageSize = stride * h;
    if (headerSize + imageSize > 0xFFFFFFFFu) {
        throw std::invalid_argument("The picture is too large for a BMP");
    }
    std::vector<std::uint8_t> out(headerSize + imageSize, 0);
    // BITMAPFILEHEADER
    out[0] = 'B';
    out[1] = 'M';
    put32(out, 2, static_cast<std::uint32_t>(out.size()));
    put32(out, 10, static_cast<std::uint32_t>(headerSize));
    // BITMAPINFOHEADER
    put32(out, 14, 40);
    put32(out, 18, static_cast<std::uint32_t>(width));
    put32(out, 22, static_cast<std::uint32_t>(height));  // positive: rows are bottom-up
    put16(out, 26, 1);                                    // planes
    put16(out, 28, 24);                                   // bits per pixel
    put32(out, 30, 0);                                    // BI_RGB
    put32(out, 34, static_cast<std::uint32_t>(imageSize));
    put32(out, 38, 2835);                                 // 72 DPI
    put32(out, 42, 2835);
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* source = indices + (h - 1 - y) * w;
        std::uint8_t* row = out.data() + headerSize + y * stride;
        for (std::size_t x = 0; x < w; ++x) {
            const std::uint8_t* rgb = palette[source[x]];
            row[x * 3] = rgb[2];
            row[x * 3 + 1] = rgb[1];
            row[x * 3 + 2] = rgb[0];
        }
    }
    return out;
}

void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not open " + path.string() + " for writing");
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("Could not write " + path.string());
    }
}

}  // namespace galaxy

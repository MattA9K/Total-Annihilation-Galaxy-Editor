#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace galaxy {

// A .pcx picture turned into rows of RGBA, top row first.
struct PcxImage {
    int width = 0;
    int height = 0;
    // 1, 4, 8, 24 or 32: how many bits each pixel had in the file.
    int bitsPerPixel = 0;
    std::vector<std::uint8_t> rgba;
};

// Reads 8-bit paletted pictures (the palette is the 769 bytes at the end of the file, as
// Total Annihilation writes them), 24 and 32-bit colour, 16-colour and black and white ones,
// run-length encoded or not. Nothing when the bytes are not a PCX picture this can read.
std::optional<PcxImage> decodePcx(const std::uint8_t* data, std::size_t size);

}  // namespace galaxy

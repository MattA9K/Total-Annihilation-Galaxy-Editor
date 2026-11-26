#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace galaxy {

// Encodes a picture of palette indices as an uncompressed 24-bit BMP.
// indices holds width * height bytes in rows, top row first; palette maps each
// index to red, green and blue. The file stores rows bottom-up, each padded to
// a multiple of 4 bytes, as the format requires.
std::vector<std::uint8_t> encodeBmp24(
    int width,
    int height,
    const std::uint8_t* indices,
    const std::uint8_t (*palette)[3]);

// Writes bytes to a file, replacing it. Throws when the file cannot be written.
void writeFileBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes);

}  // namespace galaxy

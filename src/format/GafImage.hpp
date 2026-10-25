#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace galaxy {

// First frame of one named sequence inside a Total Annihilation .gaf file.
// Pixels are palette indices. hotspotX/hotspotY is the point that sits on the map.
struct GafImage {
    int width = 0;
    int height = 0;
    int hotspotX = 0;
    int hotspotY = 0;
    std::uint8_t transparency = 0;
    std::vector<std::uint8_t> pixels;
};

std::optional<GafImage> readGafSequence(const std::uint8_t* data, std::size_t size, std::string_view sequence);
std::vector<std::uint8_t> gafImageRgba(const GafImage& image);

}  // namespace galaxy

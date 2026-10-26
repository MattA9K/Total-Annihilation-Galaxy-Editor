#include "platform/AppIcon.hpp"

#include "third_party/stb_image.h"
#include "ui/EmbeddedData.hpp"

#include <GLFW/glfw3.h>

#include <vector>

namespace galaxy {

namespace {

struct DecodedIcon {
    int width = 0;
    int height = 0;
    unsigned char* pixels = nullptr;
};

DecodedIcon decode(const unsigned char* data, unsigned long size) {
    DecodedIcon icon;
    if (size == 0) {
        return icon;
    }
    int channels = 0;
    icon.pixels = stbi_load_from_memory(data, static_cast<int>(size), &icon.width, &icon.height, &channels, 4);
    return icon;
}

}  // namespace

void applyApplicationIcon(GLFWwindow* window) {
    // The window system picks whichever size suits the place the icon is shown.
    DecodedIcon icons[] = {
        decode(kWindowIcon256Data, kWindowIcon256Size),
        decode(kWindowIcon64Data, kWindowIcon64Size),
        decode(kWindowIcon32Data, kWindowIcon32Size),
    };
    std::vector<GLFWimage> images;
    for (const DecodedIcon& icon : icons) {
        if (icon.pixels != nullptr) {
            images.push_back(GLFWimage{icon.width, icon.height, icon.pixels});
        }
    }
    if (!images.empty()) {
        glfwSetWindowIcon(window, static_cast<int>(images.size()), images.data());
    }
    for (const DecodedIcon& icon : icons) {
        if (icon.pixels != nullptr) {
            stbi_image_free(icon.pixels);
        }
    }
}

}  // namespace galaxy

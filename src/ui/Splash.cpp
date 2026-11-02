#include "ui/Splash.hpp"
#include "core/Version.hpp"
#include "ui/EmbeddedData.hpp"

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
// The Windows header stops at OpenGL 1.1, which predates this constant.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#endif

#include <imgui.h>

#include "third_party/stb_image.h"
#include "ui/Theme.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace galaxy {

namespace {

ImU32 gray(int value, int alpha = 255) {
    return IM_COL32(value, value, value, alpha);
}

// The pale line under the gold logo. The big letters are yellow with almost no
// blue, and this line is the light gold band in the middle of the picture.
float measureTitleBottom(const SplashImage& image) {
    constexpr float fallback = 341.0f / 475.0f;
    const std::size_t pixels = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    if (!image.valid() || image.rgba.size() < pixels * 4) {
        return fallback;
    }
    const int x0 = image.width * 35 / 100;
    const int x1 = image.width * 65 / 100;
    int bandEnd = -1;
    int runStart = -1;
    for (int y = image.height / 2; y < image.height; ++y) {
        int count = 0;
        const unsigned char* row = image.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4;
        for (int x = x0; x < x1; ++x) {
            const unsigned char* pixel = row + static_cast<std::size_t>(x) * 4;
            const int red = pixel[0];
            const int green = pixel[1];
            const int blue = pixel[2];
            if (red > 170 && green > 170 && blue > 90 && std::abs(red - green) < 40) {
                ++count;
            }
        }
        if (count >= 20) {
            if (runStart < 0) {
                runStart = y;
            }
            bandEnd = y;
        } else if (runStart >= 0) {
            runStart = -1;
        }
    }
    if (bandEnd < 0) {
        return fallback;
    }
    return static_cast<float>(bandEnd + 1) / static_cast<float>(image.height);
}

}  // namespace

SplashImage decodeSplashImage() {
    SplashImage image;
    if (kSplashImageSize == 0) {
        return image;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(kSplashImageData, static_cast<int>(kSplashImageSize), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return image;
    }
    image.width = width;
    image.height = height;
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    stbi_image_free(pixels);
    return image;
}

SplashScreen::SplashScreen(SplashImage image) : image_(std::move(image)), titleBottom_(measureTitleBottom(image_)) {}

SplashScreen::~SplashScreen() {
    if (texture_ != 0) {
        GLuint texture = texture_;
        glDeleteTextures(1, &texture);
    }
}

float SplashScreen::windowHeight() const {
    if (!image_.valid()) {
        return kStrip;
    }
    return kWidth * static_cast<float>(image_.height) / static_cast<float>(image_.width) + kStrip;
}

void SplashScreen::uploadTexture() {
    if (!image_.valid() || image_.rgba.empty() || texture_ != 0) {
        return;
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_.width, image_.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_.rgba.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    texture_ = texture;
    // The pixels now live on the graphics card.
    std::vector<unsigned char>().swap(image_.rgba);
}

void SplashScreen::draw(float progress, const std::string& status, const std::string& count) const {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const float u = size.x / kWidth;
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), size, IM_COL32(0, 0, 0, 255));

    if (texture_ != 0 && image_.width > 0) {
        const float imageHeight = size.x * static_cast<float>(image_.height) / static_cast<float>(image_.width);
        draw->AddImage(static_cast<ImTextureID>(static_cast<std::intptr_t>(texture_)), ImVec2(0.0f, 0.0f), ImVec2(size.x, imageHeight));

        const std::string version = std::string("v") + versionString();
        ImFont* versionFont = theme::smallFont();
        const float versionSize = versionFont->FontSize * ImGui::GetIO().FontGlobalScale;
        const ImVec2 versionWidth = versionFont->CalcTextSizeA(versionSize, 10000.0f, 0.0f, version.c_str());
        const float versionX = (size.x - versionWidth.x) * 0.5f;
        const float versionY = imageHeight * titleBottom_ + 5.0f * u;
        draw->AddText(versionFont, versionSize, ImVec2(versionX + u, versionY + u), IM_COL32(0, 0, 0, 160), version.c_str());
        draw->AddText(versionFont, versionSize, ImVec2(versionX, versionY), IM_COL32(214, 204, 148, 235), version.c_str());
    }

    const float margin = 28.0f * u;
    const float barHeight = 10.0f * u;
    const float barTop = size.y - 18.0f * u - barHeight;
    const float barLeft = margin;
    const float barRight = size.x - margin;

    ImFont* font = theme::smallFont();
    const float fontSize = font->FontSize * ImGui::GetIO().FontGlobalScale;
    const float textTop = barTop - 7.0f * u - fontSize;
    draw->AddText(font, fontSize, ImVec2(barLeft, textTop), gray(178), status.c_str());
    if (!count.empty()) {
        const ImVec2 width = font->CalcTextSizeA(fontSize, 10000.0f, 0.0f, count.c_str());
        draw->AddText(font, fontSize, ImVec2(barRight - width.x, textTop), gray(150), count.c_str());
    }

    // An Aqua-style bar: a dark groove with a glossy blue fill.
    const float round = barHeight * 0.5f;
    draw->AddRectFilled(ImVec2(barLeft, barTop), ImVec2(barRight, barTop + barHeight), gray(38), round);
    const float fraction = std::clamp(progress, 0.0f, 1.0f);
    if (fraction > 0.0f) {
        const float inset = std::max(1.0f, u);
        const float left = barLeft + inset;
        const float top = barTop + inset;
        const float bottom = barTop + barHeight - inset;
        const float right = std::max(left + (bottom - top), left + (barRight - barLeft - 2.0f * inset) * fraction);
        const float innerRound = (bottom - top) * 0.5f;
        draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), IM_COL32(44, 118, 232, 255), innerRound);
        draw->AddRectFilled(ImVec2(left, top), ImVec2(right, top + (bottom - top) * 0.5f), IM_COL32(255, 255, 255, 64), innerRound, ImDrawFlags_RoundCornersTop);
    }
    draw->AddRect(ImVec2(barLeft, barTop), ImVec2(barRight, barTop + barHeight), gray(104), round, 0, std::max(1.0f, u));
}

}  // namespace galaxy

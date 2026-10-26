#pragma once

#include <cstdint>

typedef unsigned int GLuint;

namespace galaxy {

// Frosted glass for the sidebar. Give it a small picture of the map that lies
// behind the sidebar and it blurs and brightens that picture on the graphics
// card. The picture is a few thousand pixels, so the work is a handful of tiny
// draw calls, and the caller only needs to run it when the map view changes.
class GlassBlur {
public:
    struct Settings {
        // Blur strength in picture pixels (one pass is run twice, so the
        // visible spread is about 1.4 times this).
        float sigma = 3.5f;
        // How far the blurred colors move toward the tint: 0 keeps them, 1 turns
        // the glass into the plain tint (white for light mode, near black for dark).
        float whiteness = 0.52f;
        float tintRed = 1.0f;
        float tintGreen = 1.0f;
        float tintBlue = 1.0f;
        // Extra color strength applied before the white, so a little of the map's
        // color still shows through.
        float saturation = 1.5f;
        // Shown where the map is missing (transparent pixels).
        float backgroundRed = 0.5f;
        float backgroundGreen = 0.5f;
        float backgroundBlue = 0.5f;
    };

    // Blurs a width x height RGBA picture. Returns false if the graphics card
    // could not run the effect, in which case the caller draws a flat color.
    bool update(const std::uint8_t* rgba, int width, int height, const Settings& settings);

    // The finished glass. Valid after update() returned true.
    GLuint texture() const { return finalTexture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool failed() const { return failed_; }

private:
    bool initialize();
    bool resize(int width, int height);

    bool ready_ = false;
    bool failed_ = false;

    GLuint program_ = 0;
    GLuint vertexArray_ = 0;
    GLuint sourceTexture_ = 0;
    GLuint pingTexture_ = 0;
    GLuint finalTexture_ = 0;
    GLuint framebuffer_ = 0;

    int width_ = 0;
    int height_ = 0;

    int directionUniform_ = -1;
    int sigmaUniform_ = -1;
    int compositeUniform_ = -1;
    int backgroundUniform_ = -1;
    int finishUniform_ = -1;
    int whitenessUniform_ = -1;
    int tintUniform_ = -1;
    int saturationUniform_ = -1;
    int imageUniform_ = -1;
};

}  // namespace galaxy

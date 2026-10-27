#pragma once

#include <cstdint>

namespace galaxy {

// Draws the map as a height-displaced plane with a see-through sea plane, into a
// texture the editor shows. The heights live in a small texture and the plane's
// vertices are placed from it on the graphics card, so changing the map costs a
// texture upload and no mesh rebuild.
class Terrain3D {
public:
    struct Sea {
        float red = 0.2f;
        float green = 0.5f;
        float blue = 0.85f;
        float opacity = 0.45f;
        // Height of the plane, in the same units as the height map.
        float level = 0.0f;
    };

    Terrain3D() = default;
    ~Terrain3D();
    Terrain3D(const Terrain3D&) = delete;
    Terrain3D& operator=(const Terrain3D&) = delete;

    // True once the graphics driver has turned out unable to draw the 3D view.
    bool failed() const { return failed_; }

    // One byte per height cell, row by row. A cell covers cellSize map pixels.
    void setHeights(const std::uint8_t* heights, int cellsWide, int cellsHigh, float cellSize);
    // The map picture draped over the plane: RGBA, the first row is the north edge.
    void setColors(const std::uint8_t* rgba, int width, int height);

    // Draws into an offscreen target of this size and returns its texture. The
    // picture is upside down, as OpenGL draws it. Returns 0 when it cannot draw.
    // viewProjection is column-major, as OrbitCamera produces it.
    unsigned int render(int width, int height, const float viewProjection[16], const Sea& sea, const float background[3]);

    // Map pixels the plane rises for each step of height.
    static constexpr float kHeightScale = 1.0f;

private:
    bool init();
    void resizeTarget(int width, int height);
    void release();

    bool ready_ = false;
    bool failed_ = false;

    unsigned int terrainProgram_ = 0;
    unsigned int seaProgram_ = 0;
    unsigned int vertexArray_ = 0;
    unsigned int framebuffer_ = 0;
    unsigned int depthBuffer_ = 0;
    unsigned int targetTexture_ = 0;
    unsigned int heightTexture_ = 0;
    unsigned int colorTexture_ = 0;
    int targetWidth_ = 0;
    int targetHeight_ = 0;

    int cellsWide_ = 0;
    int cellsHigh_ = 0;
    float cellSize_ = 16.0f;
    bool haveColors_ = false;

    // Uniform locations, looked up once.
    int terrainUniforms_[8] = {};
    int seaUniforms_[4] = {};
};

}  // namespace galaxy

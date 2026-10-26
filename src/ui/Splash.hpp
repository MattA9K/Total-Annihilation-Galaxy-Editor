#pragma once

#include <string>
#include <vector>

namespace galaxy {

struct SplashImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;

    bool valid() const { return width > 0 && height > 0; }
};

// Decodes src/SplashScreen.png, which the build embeds in the executable. The
// result is not valid when the build had no splash image.
SplashImage decodeSplashImage();

// A picture with a progress bar underneath it. The window is kWidth points wide
// and as tall as the picture plus the strip that holds the bar.
class SplashScreen {
public:
    static constexpr float kWidth = 720.0f;
    static constexpr float kStrip = 58.0f;

    explicit SplashScreen(SplashImage image);
    ~SplashScreen();
    SplashScreen(const SplashScreen&) = delete;
    SplashScreen& operator=(const SplashScreen&) = delete;

    // Window height in points for a window kWidth points wide.
    float windowHeight() const;

    // Needs a current OpenGL context. Call once before the first draw.
    void uploadTexture();

    // Call between NewFrame and Render. progress runs from 0 to 1. The status
    // sits left of the bar and the count sits at its right end.
    void draw(float progress, const std::string& status, const std::string& count) const;

private:
    SplashImage image_;
    unsigned int texture_ = 0;
};

}  // namespace galaxy

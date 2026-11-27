#pragma once

namespace galaxy {

// Shift + two-finger touchpad panning on the 2D map.
//
// GLFW reports touchpad movement as scroll offsets in both axes, signed so that the content should
// follow the fingers: positive x moves the picture right, positive y moves it down (with macOS
// natural scrolling, and the platform's own setting elsewhere). The camera pan is in screen pixels,
// so the same offset moves the map by the same distance on screen at any zoom.
//
// macOS turns Shift + a plain mouse wheel into a horizontal scroll, but leaves a touchpad's precise
// deltas on their own axes, so a touchpad pans in both directions as is, and Shift + a mouse wheel
// pans sideways, as Shift + wheel does in most programs.
struct PanDelta {
    float x = 0.0f;
    float y = 0.0f;
};

inline PanDelta touchpadPanDelta(float wheelX, float wheelY, float pixelsPerUnit) {
    return {wheelX * pixelsPerUnit, wheelY * pixelsPerUnit};
}

// How many screen points one scroll unit is on each platform, as GLFW scales it:
// - macOS: a touchpad's point deltas are multiplied by 0.1, so 10 gives them back.
// - Wayland: axis values (about 10 per wheel step) are divided by 10, so 10 again.
// - Windows: one unit is a whole wheel notch (WHEEL_DELTA); precision touchpads send fractions.
// - X11: GLFW only gets whole button 4-7 clicks, so each click is one fixed step.
enum class ScrollPlatform { MacOS, Wayland, Windows, X11 };

inline float scrollPixelsPerUnit(ScrollPlatform platform) {
    switch (platform) {
    case ScrollPlatform::MacOS:
    case ScrollPlatform::Wayland:
        return 10.0f;
    case ScrollPlatform::Windows:
        return 50.0f;
    case ScrollPlatform::X11:
        return 20.0f;
    }
    return 10.0f;
}

} // namespace galaxy

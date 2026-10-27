#pragma once

namespace galaxy {

// A camera that circles a point on the map. Positions are in map pixels, with x
// running right, z running down the 2D map, and y running up. The camera math
// lives here, apart from any drawing, so it can be tested on its own.
struct OrbitCamera {
    // The point the camera looks at and turns around.
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZ = 0.0f;
    // How far the camera sits from the target.
    float distance = 1000.0f;
    // Yaw 0 looks from the south edge toward the north edge, so the map appears
    // the way it does in the 2D view. Pitch is the angle above the ground.
    float yaw = 0.0f;
    float pitch = 0.7853982f;
    // The longest distance across the map. It limits zoom and sets how far the
    // camera can see.
    float reach = 2048.0f;

    static constexpr float kMinPitch = 0.05f;
    static constexpr float kMaxPitch = 1.5f;
    static constexpr float kFieldOfView = 0.8726646f;  // 50 degrees, top to bottom

    // Looks at the middle of a map of this size so that all of it is in view.
    void frame(float worldWidth, float worldHeight);

    // Mouse drag in pixels. Dragging right turns the map to the right.
    void orbit(float dragX, float dragY);
    // Mouse drag in pixels that slides the ground with the pointer.
    void pan(float dragX, float dragY, float viewportHeight);
    // Mouse wheel steps. Positive moves closer.
    void zoom(float wheel);
    // Moves the target along the ground relative to where the camera faces.
    // Forward and right are -1, 0, or 1 and seconds is the time they were held.
    void move(float forward, float right, float seconds, bool fast);
    // Turns around the target by an angle in radians.
    void turn(float radians);
    // Keeps the target over the map.
    void keepOver(float worldWidth, float worldHeight);

    void eye(float out[3]) const;
    // Column-major projection times view, ready for an OpenGL uniform.
    void viewProjection(float aspect, float out[16]) const;
    float nearPlane() const;
    float farPlane() const;
};

}  // namespace galaxy

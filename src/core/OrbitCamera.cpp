#include "core/OrbitCamera.hpp"

#include <algorithm>
#include <cmath>

namespace galaxy {
namespace {

constexpr float kTwoPi = 6.2831853f;

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalize(Vec3 v) {
    const float length = std::sqrt(dot(v, v));
    if (length < 1.0e-6f) {
        return {0.0f, 0.0f, -1.0f};
    }
    return {v.x / length, v.y / length, v.z / length};
}

float minDistance(const OrbitCamera& camera) {
    return std::max(48.0f, camera.reach * 0.01f);
}

float maxDistance(const OrbitCamera& camera) {
    return std::max(camera.reach * 2.5f, 512.0f);
}

}  // namespace

void OrbitCamera::frame(float worldWidth, float worldHeight) {
    reach = std::max(1.0f, std::sqrt(worldWidth * worldWidth + worldHeight * worldHeight));
    targetX = worldWidth * 0.5f;
    targetZ = worldHeight * 0.5f;
    targetY = 0.0f;
    yaw = 0.0f;
    pitch = 0.7853982f;
    // Far enough back that the map fills the view from its corner to its far side.
    distance = std::clamp(reach * 0.85f, minDistance(*this), maxDistance(*this));
}

void OrbitCamera::orbit(float dragX, float dragY) {
    yaw -= dragX * 0.005f;
    yaw = std::fmod(yaw, kTwoPi);
    pitch = std::clamp(pitch + dragY * 0.005f, kMinPitch, kMaxPitch);
}

void OrbitCamera::pan(float dragX, float dragY, float viewportHeight) {
    if (viewportHeight < 1.0f) {
        return;
    }
    // How much ground one screen pixel covers at the distance of the target.
    const float perPixel = 2.0f * std::tan(kFieldOfView * 0.5f) * distance / viewportHeight;
    const float sideways = perPixel;
    const float depth = perPixel / std::max(std::sin(pitch), 0.3f);
    const float forwardX = -std::sin(yaw);
    const float forwardZ = -std::cos(yaw);
    const float rightX = std::cos(yaw);
    const float rightZ = -std::sin(yaw);
    targetX += -rightX * dragX * sideways + forwardX * dragY * depth;
    targetZ += -rightZ * dragX * sideways + forwardZ * dragY * depth;
}

void OrbitCamera::zoom(float wheel) {
    distance = std::clamp(distance * std::exp(-wheel * 0.12f), minDistance(*this), maxDistance(*this));
}

void OrbitCamera::move(float forward, float right, float seconds, bool fast) {
    const float speed = distance * (fast ? 2.4f : 0.8f) * seconds;
    const float forwardX = -std::sin(yaw);
    const float forwardZ = -std::cos(yaw);
    const float rightX = std::cos(yaw);
    const float rightZ = -std::sin(yaw);
    targetX += (forwardX * forward + rightX * right) * speed;
    targetZ += (forwardZ * forward + rightZ * right) * speed;
}

void OrbitCamera::turn(float radians) {
    yaw = std::fmod(yaw + radians, kTwoPi);
}

void OrbitCamera::keepOver(float worldWidth, float worldHeight) {
    targetX = std::clamp(targetX, 0.0f, std::max(worldWidth, 0.0f));
    targetZ = std::clamp(targetZ, 0.0f, std::max(worldHeight, 0.0f));
}

void OrbitCamera::eye(float out[3]) const {
    out[0] = targetX + distance * std::cos(pitch) * std::sin(yaw);
    out[1] = targetY + distance * std::sin(pitch);
    out[2] = targetZ + distance * std::cos(pitch) * std::cos(yaw);
}

float OrbitCamera::nearPlane() const {
    return std::max(2.0f, distance * 0.02f);
}

float OrbitCamera::farPlane() const {
    return distance + reach * 1.5f + 1024.0f;
}

void OrbitCamera::viewProjection(float aspect, float out[16]) const {
    float eyePosition[3];
    eye(eyePosition);
    const Vec3 position{eyePosition[0], eyePosition[1], eyePosition[2]};
    const Vec3 target{targetX, targetY, targetZ};
    const Vec3 forward = normalize(target - position);
    const Vec3 side = normalize(cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 up = cross(side, forward);

    float view[16] = {
        side.x, up.x, -forward.x, 0.0f,
        side.y, up.y, -forward.y, 0.0f,
        side.z, up.z, -forward.z, 0.0f,
        -dot(side, position), -dot(up, position), dot(forward, position), 1.0f,
    };

    const float nearZ = nearPlane();
    const float farZ = farPlane();
    const float focal = 1.0f / std::tan(kFieldOfView * 0.5f);
    float projection[16] = {};
    projection[0] = focal / std::max(aspect, 0.01f);
    projection[5] = focal;
    projection[10] = (farZ + nearZ) / (nearZ - farZ);
    projection[11] = -1.0f;
    projection[14] = 2.0f * farZ * nearZ / (nearZ - farZ);

    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += projection[k * 4 + row] * view[column * 4 + k];
            }
            out[column * 4 + row] = sum;
        }
    }
}

}  // namespace galaxy

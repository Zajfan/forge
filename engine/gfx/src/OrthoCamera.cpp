#include "forge/gfx/OrthoCamera.hpp"

#include <algorithm>
#include <cmath>

namespace forge::gfx {

// ─── View matrices ────────────────────────────────────────────────────────────
//
// Convention: Y-up world space, -Z forward.
// Camera sits far from the target along the axis it looks along.

static constexpr float kArmLen = 8192.f;  // distance from target to camera eye

glm::mat4 OrthoCamera::viewMatrix() const noexcept {
    glm::vec3 eye, up;
    switch (dir) {
    case Dir::Top:
        // Looking straight down (-Y).  "Up" on screen = -Z (into the scene).
        eye = target + glm::vec3{ 0.f, kArmLen, 0.f };
        up  = { 0.f, 0.f, -1.f };
        break;
    case Dir::Front:
        // Looking in +Z direction (from behind the scene toward the viewer).
        // "Up" on screen = +Y.
        eye = target - glm::vec3{ 0.f, 0.f, kArmLen };
        up  = { 0.f, 1.f, 0.f };
        break;
    case Dir::Right:
        // Looking in -X direction (from the right side of the scene).
        eye = target + glm::vec3{ kArmLen, 0.f, 0.f };
        up  = { 0.f, 1.f, 0.f };
        break;
    }
    return glm::lookAt(eye, target, up);
}

glm::mat4 OrthoCamera::projMatrix(float pixW, float pixH) const noexcept {
    const float halfW = zoom * pixW * 0.5f;
    const float halfH = zoom * pixH * 0.5f;
    return glm::ortho(-halfW, halfW, -halfH, halfH, -kArmLen * 2.f, kArmLen * 2.f);
}

// ─── Input ────────────────────────────────────────────────────────────────────

void OrthoCamera::pan(float dx, float dy) noexcept {
    // Move target opposite to drag direction, scaled by zoom
    // Axis mapping depends on view direction
    switch (dir) {
    case Dir::Top:
        // Screen X → world X;  screen Y → world Z (inverted since Y=down on screen)
        target.x -= dx * zoom;
        target.z += dy * zoom;
        break;
    case Dir::Front:
        target.x -= dx * zoom;
        target.y += dy * zoom;
        break;
    case Dir::Right:
        target.z += dx * zoom;
        target.y += dy * zoom;
        break;
    }
}

void OrthoCamera::zoomBy(float delta, float sensitivity) noexcept {
    zoom *= std::pow(1.f + sensitivity, -delta);
    zoom  = std::clamp(zoom, 0.01f, 64.f);
}

} // namespace forge::gfx

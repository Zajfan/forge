#pragma once

#include <forge/geo/Math.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace forge::gfx {

// ─── OrbitCamera ─────────────────────────────────────────────────────────────

/// An orbit (tumble) camera that rotates around a target point.
///
/// Input model:
///   Left-drag   → orbit (yaw + pitch)
///   Right-drag  → pan  (translate target)
///   Scroll      → zoom (change distance)
///   frameAABB() → fit camera to scene bounds
///
/// Conventions:
///   Y-up world space.
///   Yaw = rotation around Y axis (degrees).
///   Pitch = elevation above/below XZ plane (degrees, clamped ±89°).
struct OrbitCamera {
    glm::vec3 target   = { 0.f,  0.f,  0.f };
    float     distance = 512.f;
    float     yaw      =  45.f;   ///< degrees
    float     pitch    =  25.f;   ///< degrees, positive = above target
    float     fovY     =  60.f;   ///< vertical FOV in degrees
    float     nearZ    =   1.f;
    float     farZ     = 16384.f;

    // ── Matrices ──────────────────────────────────────────────────────────────

    [[nodiscard]] glm::mat4 viewMatrix()                       const noexcept;
    [[nodiscard]] glm::mat4 projMatrix(float aspectRatio)      const noexcept;
    [[nodiscard]] glm::mat4 vpMatrix  (float aspectRatio)      const noexcept;

    /// Camera position in world space.
    [[nodiscard]] glm::vec3 position()                         const noexcept;

    // ── Input ─────────────────────────────────────────────────────────────────

    /// Orbit (rotate) by mouse delta in pixels.
    /// sensitivity: degrees per pixel (default 0.3)
    void orbit(float dx, float dy, float sensitivity = 0.3f) noexcept;

    /// Pan the target by mouse delta in pixels.
    /// sensitivity scales with current distance so pan feels consistent at all zoom levels.
    void pan(float dx, float dy, float sensitivity = 0.001f) noexcept;

    /// Zoom by scroll wheel delta.
    /// Exponential zoom keeps the rate consistent at large and small distances.
    void zoom(float delta, float sensitivity = 0.1f) noexcept;

    // ── Utility ───────────────────────────────────────────────────────────────

    /// Position and orient the camera to fit an AABB.
    /// Sets target = AABB centre, distance = 2× diagonal, reasonable pitch/yaw.
    void frameAABB(const geo::AABB& box) noexcept;

    /// Unproject a screen-space point (NDC) into a world-space ray direction.
    [[nodiscard]] glm::vec3 unproject(glm::vec2 ndc, float aspectRatio) const noexcept;
};

} // namespace forge::gfx

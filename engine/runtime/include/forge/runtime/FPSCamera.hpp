#pragma once

#include <forge/geo/Math.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace forge::runtime {

/// First-person camera controlled by yaw (horizontal) and pitch (vertical).
///
/// Y-up world convention.  Pitch is clamped to ±89° to prevent gimbal flip.
/// Position is set externally by PlayerController after physics integration.
struct FPSCamera {
    glm::vec3 position = { 0.f, 160.f, 0.f }; ///< Eye position in world space
    float     yaw      = 0.f;     ///< Rotation around Y axis (degrees)
    float     pitch    = 0.f;     ///< Elevation above XZ plane (degrees)
    float     fovY     = 90.f;    ///< Vertical FOV — wider than orbit (FPS feel)
    float     nearZ    = 2.f;     ///< Near clip — small for close geometry
    float     farZ     = 8192.f;

    // ── Matrices ──────────────────────────────────────────────────────────────

    [[nodiscard]] glm::mat4 viewMatrix()              const noexcept;
    [[nodiscard]] glm::mat4 projMatrix(float aspect)  const noexcept;

    // ── Basis vectors ─────────────────────────────────────────────────────────

    [[nodiscard]] glm::vec3 forward() const noexcept; ///< Look direction (unit)
    [[nodiscard]] glm::vec3 right()   const noexcept; ///< Camera right (unit)
    [[nodiscard]] glm::vec3 up()      const noexcept; ///< Camera up (unit)

    // ── Input ─────────────────────────────────────────────────────────────────

    /// Apply relative mouse delta in pixels.
    /// @param sensitivity  Degrees per pixel (default 0.15).
    void applyMouseDelta(float dx, float dy, float sensitivity = 0.15f) noexcept;
};

} // namespace forge::runtime
